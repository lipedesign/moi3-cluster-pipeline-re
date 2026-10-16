#!/usr/bin/env python3
# apply.py — patch a copy of the glforward sources to add the EGLImage import path the mss needs,
# without touching the bench's glforward. Run inside the container: python3 -I apply.py <glf-src> <out>
# It copies <glf-src> to <out> and inserts our additions at known anchors, then <out>/build.sh builds.
import sys, os, shutil

src, out = sys.argv[1], sys.argv[2]
if os.path.exists(out): shutil.rmtree(out)
shutil.copytree(src, out)

def patch(path, anchor, text, after=True, once=True):
    with open(path) as f: s = f.read()
    n = s.count(anchor)
    if n < 1: raise SystemExit(f"anchor not found in {path}: {anchor!r}")
    repl = (anchor + text) if after else (text + anchor)
    s = s.replace(anchor, repl, 1 if once else -1)
    with open(path, "w") as f: f.write(s)

shim = os.path.join(out, "shim.c")
host = os.path.join(out, "host.c")
proto = os.path.join(out, "proto.h")

# --- proto.h: two new ops (explicit values, outside the OPX auto range and the GL range) ---
patch(proto, "enum {", r'''
    OPX_IMG_CREATE = 90,    /* w,h,stride,fourcc, blob pixels -> handle (EGLImage from a GL texture) */
    OPX_IMG_TARGET = 91,    /* target, handle (no reply) */
    OPX_CLIENT_ARRAY = 92,  /* idx,size,type,norm,stride, blob -> transient VBO for a client-side array */''', after=True)

# --- shim.c: the import entrypoints, inserted right after the generated shim include ---
SHIM_FUNCS = r'''
/* === M2 EGLImage import (moi3-cluster-pipeline-re) === */
#include <sys/mman.h>
/* weak: resolved at runtime from our fakegbm when the mss imports via a gbm_bo (native-pixmap) */
extern int gbm_bo_get_fd(void *) __attribute__((weak));
extern unsigned int gbm_bo_get_width(void *) __attribute__((weak));
extern unsigned int gbm_bo_get_height(void *) __attribute__((weak));
extern unsigned int gbm_bo_get_stride(void *) __attribute__((weak));
void *eglCreateImageKHR(EGLDisplay dpy, EGLContext ctx, EGLenum target, EGLClientBuffer buffer, const EGLint *attr)
{
    (void)dpy; (void)ctx;
    int fd = -1; unsigned int w = 0, h = 0, stride = 0, fourcc = 0; int i;
    for (i = 0; attr && attr[i] != 0x3038 /*EGL_NONE*/; i += 2) {
        int k = attr[i], v = attr[i + 1];
        if (k == 0x3057) w = (unsigned)v;            /* EGL_WIDTH */
        else if (k == 0x3056) h = (unsigned)v;       /* EGL_HEIGHT */
        else if (k == 0x3271) fourcc = (unsigned)v;  /* EGL_LINUX_DRM_FOURCC_EXT */
        else if (k == 0x3272) fd = v;                /* EGL_DMA_BUF_PLANE0_FD_EXT */
        else if (k == 0x3274) stride = (unsigned)v;  /* EGL_DMA_BUF_PLANE0_PITCH_EXT */
    }
    if (fd < 0 && buffer && gbm_bo_get_fd) {
        fd = gbm_bo_get_fd(buffer); w = gbm_bo_get_width(buffer); h = gbm_bo_get_height(buffer); stride = gbm_bo_get_stride(buffer);
    }
    fprintf(stderr, "[glfwd/img] eglCreateImageKHR target=%#x fd=%d %ux%u stride=%u fourcc=%#x\n", target, fd, w, h, stride, fourcc);
    if (fd < 0 || !w || !h) return (void *)0;
    if (!stride) stride = w * 4;
    unsigned long sz = (unsigned long)stride * h;
    void *pm = mmap(0, sz, PROT_READ, MAP_SHARED, fd, 0);
    if (pm == MAP_FAILED) { fprintf(stderr, "[glfwd/img] mmap failed\n"); return (void *)0; }
    W_begin(OPX_IMG_CREATE);
    W_u64(w); W_u64(h); W_u64(stride); W_u64(fourcc); W_blob(pm, sz);
    W_call();
    uint64_t handle = R_u64();
    W_done();
    munmap(pm, sz);
    fprintf(stderr, "[glfwd/img] image handle=%llu\n", (unsigned long long)handle);
    return (void *)(uintptr_t)handle;
}
unsigned int eglDestroyImageKHR(EGLDisplay dpy, void *image) { (void)dpy; (void)image; return 1; }
void glEGLImageTargetTexture2DOES(unsigned int target, void *image)
{
    fprintf(stderr, "[glfwd/img] glEGLImageTargetTexture2DOES target=%#x image=%p\n", target, image);
    W_begin(OPX_IMG_TARGET); W_u64((uint64_t)target); W_u64((uint64_t)(uintptr_t)image); W_end();
}
void *eglGetPlatformDisplayEXT(unsigned int platform, void *nd, const int *attr)
{ (void)platform; (void)nd; (void)attr; return eglGetDisplay((EGLNativeDisplayType)0); }
'''
patch(shim, '#include "gen/shim_gen.inc"', SHIM_FUNCS, after=True)

# register them with eglGetProcAddress (the mss resolves these by name)
SHIM_PROCS = r'''    {"eglCreateImageKHR", (void *)eglCreateImageKHR},
    {"eglDestroyImageKHR", (void *)eglDestroyImageKHR},
    {"glEGLImageTargetTexture2DOES", (void *)glEGLImageTargetTexture2DOES},
    {"eglGetPlatformDisplayEXT", (void *)eglGetPlatformDisplayEXT},
'''
patch(shim, "    {NULL, NULL}", SHIM_PROCS, after=False)

# --- shim.c: client-side vertex array capture. Defined just before attrib_ptr (line ~843), which is
# before the shim_gen include (~1020), so both attrib_ptr and the generated glDrawArrays can use it. ---
SHIM_VA = r'''/* === client-side vertex array capture (moi3-cluster-pipeline-re) ===
 * The mss's Rgba2yuv quad uses glVertexAttribPointer with client memory (no VBO), which the base
 * forwarder drops. We remember each attrib and, at glDrawArrays, ship the array bytes so glhost can
 * stage them in a transient VBO (OPX_CLIENT_ARRAY). */
struct moi3_va { int set, client; GLint size; GLenum type; GLboolean norm; GLsizei stride; const void *ptr; };
static struct moi3_va g_va[16];
static void moi3_track_attrib(GLuint index, GLint size, GLenum type, GLboolean norm, GLsizei stride, const void *p, int client) {
    if (index < 16) { g_va[index].set = 1; g_va[index].client = client; g_va[index].size = size;
        g_va[index].type = type; g_va[index].norm = norm; g_va[index].stride = stride; g_va[index].ptr = p; }
}
static size_t moi3_tsize(GLenum t) {
    switch (t) {
    case 0x1400: case 0x1401: return 1;              /* GL_BYTE, GL_UNSIGNED_BYTE */
    case 0x1402: case 0x1403: case 0x140B: return 2; /* GL_SHORT, GL_UNSIGNED_SHORT, GL_HALF_FLOAT */
    default: return 4;                               /* GL_INT/UINT/FLOAT/FIXED */
    }
}
static void moi3_send_client_arrays(GLint first, GLsizei count) {
    if (first < 0 || count <= 0) return;
    for (int i = 0; i < 16; i++) {
        if (!g_va[i].set || !g_va[i].client || !g_va[i].ptr) continue;
        size_t es = (size_t)g_va[i].size * moi3_tsize(g_va[i].type);
        size_t stride = g_va[i].stride ? (size_t)g_va[i].stride : es;
        size_t span = (size_t)(first + count) * stride;
        W_begin(OPX_CLIENT_ARRAY);
        W_u64((uint64_t)i); W_u64((uint64_t)g_va[i].size); W_u64((uint64_t)g_va[i].type);
        W_u64((uint64_t)g_va[i].norm); W_u64((uint64_t)g_va[i].stride);
        W_blob(g_va[i].ptr, span);
        W_end();
    }
}
'''
patch(shim, "static void attrib_ptr(uint32_t op,", SHIM_VA, after=False)
# record each attrib as it is set (client flag = no VBO bound)
patch(shim, "    static int warned;",
      "    moi3_track_attrib(index, size, type, norm, stride, p, !ST->bound[0]);\n", after=False)
# at glDrawArrays, ship the client arrays before the draw op
patch(os.path.join(out, "gen", "shim_gen.inc"),
      "void glDrawArrays(GLenum mode, GLint first, GLsizei count) {",
      "\n  moi3_send_client_arrays(first, count);", after=True)

# --- host.c: globals + the two op handlers ---
HOST_TOP = r'''
#include <EGL/eglext.h>
#include <GLES2/gl2ext.h>
#include <string.h>
static void *g_img_slots[256]; static int g_img_n;
static PFNEGLCREATEIMAGEKHRPROC g_realCreateImage;
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC g_realTargetTex;
/* transient VBOs staged for client-side arrays (OPX_CLIENT_ARRAY), freed after each glDrawArrays */
static GLuint g_tmpvbo[16]; static int g_tmpvbo_n;
static void moi3_cleanup_arrays(void) {
    for (int _i = 0; _i < g_tmpvbo_n; _i++) if (g_tmpvbo[_i]) glDeleteBuffers(1, &g_tmpvbo[_i]);
    g_tmpvbo_n = 0;
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}
/* demo capture: read the currently-bound framebuffer (the mss's Rgba2yuv render target) to a PPM.
 * Called right after a forwarded glDrawArrays, when the convert's FBO and viewport are bound. */
static void moi3_dump_fbo(const char *tag) {
    static int _on = 0;
    if (_on >= 4) return;
    GLint _fb = -1, _prog = -1, _atex = 0, _extbind = 0, _2dbind = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &_fb);
    glGetIntegerv(GL_CURRENT_PROGRAM, &_prog);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &_atex);
    glGetIntegerv(0x8D67 /*GL_TEXTURE_BINDING_EXTERNAL_OES*/, &_extbind);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &_2dbind);
    GLint _vp[4] = {0,0,0,0}; glGetIntegerv(GL_VIEWPORT, _vp);
    int _w = _vp[2] > 0 ? _vp[2] : 1280, _h = _vp[3] > 0 ? _vp[3] : 480;
    unsigned char *_p = (unsigned char *)malloc((size_t)_w * _h * 4);
    if (!_p) return;
    memset(_p, 0, (size_t)_w * _h * 4);
    glReadPixels(0, 0, _w, _h, GL_RGBA, GL_UNSIGNED_BYTE, _p);
    GLenum _e = glGetError();
    unsigned _max = 0, _nz = 0;
    for (size_t _i = 0; _i < (size_t)_w * _h * 4; _i++) { if (_p[_i] > _max) _max = _p[_i]; if (_p[_i]) _nz++; }
    char _fn[160]; snprintf(_fn, sizeof _fn, "/out/frames/output_%s_%03d.ppm", tag, _on);
    FILE *_f = fopen(_fn, "wb");
    if (_f) { fprintf(_f, "P6\n%d %d\n255\n", _w, _h);
        for (int _i = 0; _i < _w * _h; _i++) fwrite(_p + _i * 4, 1, 3, _f);
        fclose(_f); }
    fprintf(stderr, "[glhost] dump %s #%d: %dx%d vp=%d,%d,%d,%d fbo=%d prog=%d atex=%#x extbind=%d tex2d=%d maxpx=%u nz=%u readerr=%#x\n",
        tag, _on, _w, _h, _vp[0], _vp[1], _vp[2], _vp[3], _fb, _prog, _atex, _extbind, _2dbind, _max, _nz, _e);
    fflush(stderr);
    _on++;
    free(_p);
}
'''
patch(host, "static EGLContext g_cur_ctx = EGL_NO_CONTEXT;", HOST_TOP, after=True)

HOST_CASES = r'''    case OPX_CLIENT_ARRAY: {
        uint64_t idx = R_u64(), size = R_u64(), type = R_u64(), norm = R_u64(), stride = R_u64();
        size_t nb; const void *data = R_blob(&nb);
        GLuint vbo = 0; glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)nb, data, GL_STREAM_DRAW);
        glVertexAttribPointer((GLuint)idx, (GLint)size, (GLenum)type, (GLboolean)norm, (GLsizei)stride, (const void *)0);
        glEnableVertexAttribArray((GLuint)idx);
        if (g_tmpvbo_n < 16) g_tmpvbo[g_tmpvbo_n++] = vbo;
        LOG("[glhost] client array idx=%llu size=%llu type=%#llx stride=%llu bytes=%zu glerr=%#x\n",
            (unsigned long long)idx, (unsigned long long)size, (unsigned long long)type,
            (unsigned long long)stride, nb, glGetError());
    } return 1;
    case OPX_IMG_CREATE: {
        uint64_t w = R_u64(), h = R_u64(), stride = R_u64(), fourcc = R_u64();
        size_t nb; const void *px = R_blob(&nb);
        /* Validate the IPC-supplied geometry against the actual blob size before any read:
         * reject zero dims, a stride not a multiple of 4, smaller than a row (w*4) or larger
         * than the blob can hold (stride*h <= nb, checked overflow-safe as stride <= nb/h),
         * and cap the dimensions. Prevents an out-of-bounds read from a dimension/length
         * mismatch (the sender is the shimmed process, but a bad frame must not read OOB). */
        if (!w || !h || !stride || (stride & 3u) || w > 8192u || h > 8192u ||
            stride < w * 4u || stride > nb / h || !px) {
            LOG("[glhost] IMG_CREATE: bad geometry w=%llu h=%llu stride=%llu blob=%zu - rejected\n",
                (unsigned long long)w, (unsigned long long)h, (unsigned long long)stride, nb);
            P_u64(0); REPLY();
            return 1;
        }
        /* demo capture: write the frame the mss imported (as it arrived over the dma-buf) to a PPM,
         * channels put back to R,G,B (the buffer is XR24 = B,G,R,X in memory). First few frames only. */
        { static int dn = 0; if (dn < 3 && px && w && h && stride) {
            char fn[128]; snprintf(fn, sizeof fn, "/out/frames/import_%03d.ppm", dn++);
            FILE *fp = fopen(fn, "wb");
            if (fp) {
                fprintf(fp, "P6\n%llu %llu\n255\n", (unsigned long long)w, (unsigned long long)h);
                const unsigned char *base = (const unsigned char *)px;
                for (uint64_t y = 0; y < h; y++) {
                    const unsigned char *row = base + y * stride;
                    for (uint64_t x = 0; x < w; x++) {
                        unsigned char rgb[3] = { row[x * 4 + 2], row[x * 4 + 1], row[x * 4 + 0] };
                        fwrite(rgb, 1, 3, fp);
                    }
                }
                fclose(fp);
                LOG("[glhost] dumped imported frame %s (%llux%llu stride %llu)\n", fn,
                    (unsigned long long)w, (unsigned long long)h, (unsigned long long)stride);
            }
        } }
        if (!g_realCreateImage) g_realCreateImage = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
        if (!g_realTargetTex) g_realTargetTex = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
        GLuint tex = 0; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint)(stride / 4));
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        uint64_t handle = 0;
        if (g_realCreateImage) {
            EGLint ia[] = { EGL_GL_TEXTURE_LEVEL_KHR, 0, EGL_NONE };
            EGLImageKHR img = g_realCreateImage(D, g_cur_ctx, EGL_GL_TEXTURE_2D_KHR, (EGLClientBuffer)(uintptr_t)tex, ia);
            if (img != EGL_NO_IMAGE_KHR && g_img_n < 256) { g_img_slots[g_img_n] = img; handle = (uint64_t)(++g_img_n); }
            LOG("[glhost] IMG_CREATE %llux%llu fourcc=%#llx glerr=%#x -> img %p handle %llu\n",
                (unsigned long long)w, (unsigned long long)h, (unsigned long long)fourcc, glGetError(), img, (unsigned long long)handle);
        } else LOG("[glhost] IMG_CREATE: no eglCreateImageKHR in Mesa\n");
        P_u64(handle); REPLY();
    } return 1;
    case OPX_IMG_TARGET: {
        uint64_t target = R_u64(), handle = R_u64();
        if (handle >= 1 && handle <= (uint64_t)g_img_n && g_realTargetTex) g_realTargetTex((GLenum)target, g_img_slots[handle - 1]);
        LOG("[glhost] IMG_TARGET target=%#llx handle=%llu glerr=%#x\n", (unsigned long long)target, (unsigned long long)handle, glGetError());
    } return 1;
'''
patch(host, "    case OPX_HELLO: {", HOST_CASES, after=False)

# --- gen/host_gen.inc: right after the forwarded glDrawArrays, capture the render target
# (the mss's Rgba2yuv FBO has the YUV output at this point; FBO + viewport are still bound). ---
hostgen = os.path.join(out, "gen", "host_gen.inc")
patch(hostgen, "  glDrawArrays(mode, first, count);", "\n  moi3_dump_fbo(\"draw\");\n  moi3_cleanup_arrays();", after=True)

# diagnostics: is the shader program ever created, and what does glUseProgram bind?
patch(hostgen, "  glUseProgram(program);",
      "\n  fprintf(stderr, \"[glhost] glUseProgram(%u)\\n\", program); fflush(stderr);", after=True)
patch(hostgen, "  *ret_ = (uint64_t)glCreateProgram();",
      "\n  fprintf(stderr, \"[glhost] glCreateProgram\\n\"); fflush(stderr);", after=True)
patch(hostgen, "  *ret_ = (uint64_t)glCreateShader(type);",
      "\n  fprintf(stderr, \"[glhost] glCreateShader(%#x)\\n\", type); fflush(stderr);", after=True)

# The standalone mss (no RSI stream config) sets the convert's output viewport to 0x0, so the
# shader renders nothing. Clamp a zero viewport to the stream size so the quad covers the frame.
patch(hostgen, "  glViewport(x, y, width, height);",
      "  if (width <= 0) width = 1280;\n  if (height <= 0) height = 480;\n", after=False)


print("patched OK:", out)
