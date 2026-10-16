/* moi3_shader_demo.c — reproduce the MOI3 head-unit's RGBA->cluster-NV12 conversion on a PC bench.
 *
 * Two passes:
 *   (A) a standard ITU-R BT.601 studio-swing RGB->Y'CbCr conversion (our own GLSL), per pixel,
 *       into a 1280x480 image we can view and round-trip. The multi-screen-server performs the
 *       same standard BT.601 conversion (an RE finding — see docs/shareserver-pipeline.md).
 *   (B) the head unit's full encoder shader (the NV12 packer), which you SUPPLY yourself from
 *       your own extracted firmware, passed as <encoder.glsl>. No firmware shader ships here.
 *
 * Input is a captured RGBA frame (P6 PPM). Mesa llvmpipe (surfaceless), no GPU. Container-only.
 *
 *   moi3_shader_demo <input.ppm> <encoder.glsl> <out_yuv.bin> <out_nv12.bin>
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>

#define IN_W 1280
#define IN_H 480
#define FBO_W 320   /* NV12 packed: lstride/4 */
#define FBO_H 720   /* lscanl*3/2 */

static char *readfile(const char *p, long *n) {
    FILE *f = fopen(p, "rb"); if (!f) { fprintf(stderr, "open %s failed\n", p); exit(2); }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc(sz + 1); fread(b, 1, sz, f); b[sz] = 0; fclose(f); if (n) *n = sz; return b;
}

/* load a P6 PPM (w x h RGB) into an RGBA buffer */
static uint8_t *read_ppm_rgba(const char *p, int *w, int *h) {
    long n; uint8_t *d = (uint8_t *)readfile(p, &n);
    if (d[0] != 'P' || d[1] != '6') { fprintf(stderr, "not P6\n"); exit(2); }
    int i = 2, vals[3], k = 0;
    while (k < 3) { while (d[i]==' '||d[i]=='\t'||d[i]=='\n'||d[i]=='\r') i++;
        int v = 0; while (d[i] >= '0' && d[i] <= '9') v = v*10 + (d[i++]-'0'); vals[k++] = v; }
    i++; *w = vals[0]; *h = vals[1];
    uint8_t *rgba = malloc((size_t)*w * *h * 4);
    for (int j = 0; j < *w * *h; j++) { rgba[j*4+0]=d[i+j*3+0]; rgba[j*4+1]=d[i+j*3+1]; rgba[j*4+2]=d[i+j*3+2]; rgba[j*4+3]=255; }
    free(d); return rgba;
}

static GLuint mkshader(GLenum t, const char *s) {
    GLuint sh = glCreateShader(t); glShaderSource(sh, 1, &s, NULL); glCompileShader(sh);
    GLint ok = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    char l[4096]; glGetShaderInfoLog(sh, sizeof l, NULL, l);
    if (!ok) { fprintf(stderr, "compile FAIL: %s\n", l); return 0; }
    if (l[0]) fprintf(stderr, "compile log: %s\n", l);
    return sh;
}
static GLuint mkprog(const char *vs, const char *fs) {
    GLuint v = mkshader(GL_VERTEX_SHADER, vs), f = mkshader(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;
    GLuint p = glCreateProgram(); glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    char l[4096]; glGetProgramInfoLog(p, sizeof l, NULL, l);
    if (!ok) { fprintf(stderr, "link FAIL: %s\n", l); return 0; }
    fprintf(stderr, "program linked OK\n");
    return p;
}
/* robust external texture (Mesa rejects glTexImage2D straight on GL_TEXTURE_EXTERNAL_OES at this
 * size): upload to a GL_TEXTURE_2D, wrap it as an EGLImage (EGL_GL_TEXTURE_2D), bind that as
 * GL_TEXTURE_EXTERNAL_OES. Same path proven in spike/mesa_import_test.c. */
static GLuint mk_exttex(EGLDisplay disp, EGLContext cctx, const uint8_t *rgba, int w, int h) {
    PFNEGLCREATEIMAGEKHRPROC p_eglCreateImageKHR =
        (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC p_glEGLImageTargetTexture2DOES =
        (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
    if (!p_eglCreateImageKHR || !p_glEGLImageTargetTexture2DOES) { fprintf(stderr, "no EGLImage procs\n"); exit(5); }
    while (glGetError()) {}
    GLuint t2d; glGenTextures(1, &t2d); glBindTexture(GL_TEXTURE_2D, t2d);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (glGetError()) { fprintf(stderr, "tex2d upload err\n"); exit(5); }
    EGLint ia[] = { EGL_GL_TEXTURE_LEVEL_KHR, 0, EGL_NONE };
    EGLImageKHR img = p_eglCreateImageKHR(disp, cctx, EGL_GL_TEXTURE_2D_KHR, (EGLClientBuffer)(uintptr_t)t2d, ia);
    if (img == EGL_NO_IMAGE_KHR) { fprintf(stderr, "eglCreateImageKHR failed %#x\n", eglGetError()); exit(5); }
    GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_EXTERNAL_OES, t);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    p_glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, img);
    if (glGetError()) { fprintf(stderr, "bind external err\n"); exit(5); }
    return t;
}
static GLuint mk_fbo(int w, int h) {
    GLuint fbo, tex; glGenFramebuffers(1, &fbo); glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { fprintf(stderr, "FBO incomplete\n"); exit(5); }
    return fbo;
}

/* Pass A — standard ITU-R BT.601 studio-swing RGB->Y'CbCr, our own GLSL.
 * The multi-screen-server converts with the same standard BT.601 matrix (RE finding, see docs/).
 * Coefficients are the standard values: luma rows = (0.299,0.587,0.114) x 219/255,
 * chroma rows x 224/255, offsets 16/128/128 over 256. No firmware source is reproduced here. */
static const char *VS_SIMPLE =
    "#version 320 es\nlayout(location=0) in vec2 pos;\nout vec2 uv;\n"
    "void main(){ uv = pos*0.5+0.5; gl_Position = vec4(pos,0.0,1.0); }\n";
static const char *FS_YUV_PERPIXEL =
    "#version 320 es\n#extension GL_OES_EGL_image_external_essl3 : require\n"
    "precision highp float;\nuniform samplerExternalOES tex_unit;\nin vec2 uv;\nout vec4 fragColor;\n"
    "vec3 rgbToYCbCr(vec3 c) {\n"
    "    float y  = dot(c, vec3( 0.256788,  0.504129,  0.097906)) + 0.0625;\n"
    "    float cb = dot(c, vec3(-0.148223, -0.290993,  0.439216)) + 0.5;\n"
    "    float cr = dot(c, vec3( 0.439216, -0.367788, -0.071428)) + 0.5;\n"
    "    return vec3(y, cb, cr);\n"
    "}\n"
    "void main(){ fragColor = vec4(rgbToYCbCr(texture(tex_unit, uv).rgb), 1.0); }\n";

int main(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "usage: %s in.ppm encoder.glsl out_yuv.bin out_nv12.bin\n", argv[0]); return 2; }
    int w, h; uint8_t *in = read_ppm_rgba(argv[1], &w, &h);
    fprintf(stderr, "input %dx%d\n", w, h);

    EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    EGLint ma, mi; eglInitialize(d, &ma, &mi); eglBindAPI(EGL_OPENGL_ES_API);
    EGLConfig cfg; EGLint n = 0;
    EGLint ca[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    eglChooseConfig(d, ca, &cfg, 1, &n);
    EGLint cx[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(d, cfg, EGL_NO_CONTEXT, cx);
    EGLint pb[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
    EGLSurface s = eglCreatePbufferSurface(d, cfg, pb);
    eglMakeCurrent(d, s, s, ctx);
    fprintf(stderr, "GL_RENDERER = %s\n", glGetString(GL_RENDERER));

    GLuint tex = mk_exttex(d, ctx, in, w, h);
    const float fsquad[] = { -1,-1, 3,-1, -1,3 };   /* oversized tri, uv 0..1 */
    GLuint vbo; glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof fsquad, fsquad, GL_STATIC_DRAW);

    /* ---- (A) per-pixel YUV via our standard BT.601 conversion (viewable) ---- */
    GLuint pa = mkprog(VS_SIMPLE, FS_YUV_PERPIXEL);
    if (!pa) return 3;
    GLuint fboA = mk_fbo(w, h); glBindFramebuffer(GL_FRAMEBUFFER, fboA); glViewport(0, 0, w, h);
    glUseProgram(pa); glUniform1i(glGetUniformLocation(pa, "tex_unit"), 0);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_EXTERNAL_OES, tex);
    glBindBuffer(GL_ARRAY_BUFFER, vbo); glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_TRIANGLES, 0, 3); glFinish();
    uint8_t *yuv = malloc((size_t)w * h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, yuv);
    fprintf(stderr, "(A) per-pixel YUV readback glerr=%#x sample[0]=%u,%u,%u\n", glGetError(), yuv[0], yuv[1], yuv[2]);
    FILE *fo = fopen(argv[3], "wb"); fwrite(yuv, 1, (size_t)w * h * 4, fo); fclose(fo);

    /* ---- (B) the head unit's encoder shader (user-supplied), its quad + uniforms -> packed NV12 ---- */
    long sl; char *full_fs = readfile(argv[2], &sl);
    const char *VS_BIN =
        "#version 300 es\nprecision highp float;\nprecision highp int;\n"
        "in highp vec4 position;\nin highp vec2 texcoord;\nout highp vec2 texcoord_out;\n"
        "void main(){ gl_Position = position; texcoord_out = texcoord; }\n";
    GLuint pb2 = mkprog(VS_BIN, full_fs);
    if (pb2) {
        GLuint fboB = mk_fbo(FBO_W, FBO_H); glBindFramebuffer(GL_FRAMEBUFFER, fboB); glViewport(0, 0, FBO_W, FBO_H);
        glUseProgram(pb2);
        glUniform1i(glGetUniformLocation(pb2, "tex_unit"), 0);
        glUniform1i(glGetUniformLocation(pb2, "width"), IN_W);
        glUniform1i(glGetUniformLocation(pb2, "height"), IN_H);
        glUniform1f(glGetUniformLocation(pb2, "width_fbo"), (float)FBO_W);
        glUniform1f(glGetUniformLocation(pb2, "height_fbo"), (float)FBO_H);
        glUniform1i(glGetUniformLocation(pb2, "lstride"), IN_W);
        glUniform1i(glGetUniformLocation(pb2, "lscanl"), IN_H);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_EXTERNAL_OES, tex);
        /* the encoder's quad (RE finding): pos (-1,1)(-1,-1)(1,-1)(1,1), tex (1,0)(0,0)(0,1)(1,1) */
        const float pos[] = { -1,1, -1,-1, 1,-1, 1,1 };
        const float texc[] = { 1,0, 0,0, 0,1, 1,1 };
        GLint lpos = glGetAttribLocation(pb2, "position"), ltex = glGetAttribLocation(pb2, "texcoord");
        GLuint vb2, vt2; glGenBuffers(1, &vb2); glGenBuffers(1, &vt2);
        glBindBuffer(GL_ARRAY_BUFFER, vb2); glBufferData(GL_ARRAY_BUFFER, sizeof pos, pos, GL_STATIC_DRAW);
        glEnableVertexAttribArray(lpos); glVertexAttribPointer(lpos, 2, GL_FLOAT, GL_FALSE, 0, 0);
        glBindBuffer(GL_ARRAY_BUFFER, vt2); glBufferData(GL_ARRAY_BUFFER, sizeof texc, texc, GL_STATIC_DRAW);
        glEnableVertexAttribArray(ltex); glVertexAttribPointer(ltex, 2, GL_FLOAT, GL_FALSE, 0, 0);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4); glFinish();
        uint8_t *nv = malloc((size_t)FBO_W * FBO_H * 4);
        glReadPixels(0, 0, FBO_W, FBO_H, GL_RGBA, GL_UNSIGNED_BYTE, nv);
        unsigned nz = 0; for (size_t i = 0; i < (size_t)FBO_W*FBO_H*4; i++) if (nv[i]) nz++;
        fprintf(stderr, "(B) full shader ran: %dx%d readback glerr=%#x nonzero=%u/%u\n", FBO_W, FBO_H, glGetError(), nz, FBO_W*FBO_H*4);
        FILE *fn = fopen(argv[4], "wb"); fwrite(nv, 1, (size_t)FBO_W * FBO_H * 4, fn); fclose(fn);
    }
    fprintf(stderr, "done\n");
    return 0;
}
