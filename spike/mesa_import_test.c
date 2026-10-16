/* mesa_import_test.c — prove the heart of M2 in isolation (no mss, no glforward):
 * can the container's Mesa llvmpipe take a filled source, expose it as an EGLImage, bind it to a
 * GL_TEXTURE_EXTERNAL_OES texture, and sample it from an essl3 samplerExternalOES shader (what the
 * mss's Rgba2yuv shader does)? We build the source as a GL_TEXTURE_2D (our shim will fill it from
 * the shared buffer's pixels), make an EGLImage of it, and render through an external sampler.
 *
 * Exit 0 and "M2-CORE: OK" means the external-image + essl3-external-shader path works in Mesa. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>

#define CHECK_GL(where) do { GLenum e = glGetError(); if (e) { printf("GL error 0x%x at %s\n", e, where); return 3; } } while (0)

static GLuint mkshader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1024]; glGetShaderInfoLog(s, sizeof log, NULL, log); printf("shader compile failed: %s\n", log); return 0; }
    return s;
}

int main(void)
{
    EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    EGLint ma, mi;
    if (d == EGL_NO_DISPLAY || !eglInitialize(d, &ma, &mi)) { printf("egl init failed\n"); return 2; }
    eglBindAPI(EGL_OPENGL_ES_API);
    EGLConfig cfg; EGLint n = 0;
    EGLint ca[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    eglChooseConfig(d, ca, &cfg, 1, &n);
    EGLint cx[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(d, cfg, EGL_NO_CONTEXT, cx);
    EGLint pb[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
    EGLSurface s = eglCreatePbufferSurface(d, cfg, pb);
    if (!eglMakeCurrent(d, s, s, ctx)) { printf("makeCurrent failed\n"); return 2; }

    PFNEGLCREATEIMAGEKHRPROC pCreateImage = (void *)eglGetProcAddress("eglCreateImageKHR");
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC pTargetTex = (void *)eglGetProcAddress("glEGLImageTargetTexture2DOES");
    printf("eglCreateImageKHR=%p glEGLImageTargetTexture2DOES=%p\n", (void *)pCreateImage, (void *)pTargetTex);
    if (!pCreateImage || !pTargetTex) { printf("M2-CORE: missing entrypoints\n"); return 4; }

    /* Source: a 8x1 GL_TEXTURE_2D with 8 known colour bars (this is what our shim fills from the
     * shared buffer). */
    const uint8_t bars[8][4] = {
        {255,0,0,255},{0,255,0,255},{0,0,255,255},{255,255,0,255},
        {0,255,255,255},{255,0,255,255},{255,255,255,255},{32,32,32,255} };
    GLuint srctex; glGenTextures(1, &srctex);
    glBindTexture(GL_TEXTURE_2D, srctex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, bars);
    CHECK_GL("source tex");

    /* EGLImage of that 2D texture, then bind it as an EXTERNAL texture. */
    EGLint attrs[] = { EGL_GL_TEXTURE_LEVEL_KHR, 0, EGL_NONE };
    EGLImageKHR img = pCreateImage(d, ctx, EGL_GL_TEXTURE_2D_KHR, (EGLClientBuffer)(uintptr_t)srctex, attrs);
    if (img == EGL_NO_IMAGE_KHR) { printf("M2-CORE: eglCreateImageKHR(GL_TEXTURE_2D) failed 0x%x\n", eglGetError()); return 4; }
    GLuint extex; glGenTextures(1, &extex);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, extex);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    pTargetTex(GL_TEXTURE_EXTERNAL_OES, img);
    CHECK_GL("bind external");

    /* essl3 shader sampling the external texture (same extension the Rgba2yuv shader uses). */
    const char *vs = "#version 320 es\nlayout(location=0) in vec2 pos;\nout vec2 uv;\n"
                     "void main(){ uv = pos*0.5+0.5; gl_Position = vec4(pos,0.0,1.0);}\n";
    const char *fs = "#version 320 es\n#extension GL_OES_EGL_image_external_essl3 : require\n"
                     "precision mediump float;\nuniform samplerExternalOES tex;\nin vec2 uv;\nout vec4 o;\n"
                     "void main(){ o = texture(tex, uv);}\n";
    GLuint v = mkshader(GL_VERTEX_SHADER, vs), f = mkshader(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 3;
    GLuint pr = glCreateProgram(); glAttachShader(pr, v); glAttachShader(pr, f); glLinkProgram(pr);
    GLint ok = 0; glGetProgramiv(pr, GL_LINK_STATUS, &ok);
    if (!ok) { char log[1024]; glGetProgramInfoLog(pr, sizeof log, NULL, log); printf("link failed: %s\n", log); return 3; }
    glUseProgram(pr);
    glUniform1i(glGetUniformLocation(pr, "tex"), 0);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_EXTERNAL_OES, extex);

    /* Render the sampled external texture into an 8x1 RGBA FBO and read it back. */
    GLuint fbo, dst; glGenFramebuffers(1, &fbo); glGenTextures(1, &dst);
    glBindTexture(GL_TEXTURE_2D, dst);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dst, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { printf("fbo incomplete\n"); return 3; }
    glViewport(0, 0, 8, 1);
    const float quad[] = { -1,-1, 3,-1, -1,3 };   /* big triangle covers the viewport */
    GLuint vbo; glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    CHECK_GL("draw");

    uint8_t out[8][4];
    glReadPixels(0, 0, 8, 1, GL_RGBA, GL_UNSIGNED_BYTE, out);
    CHECK_GL("readpixels");
    int good = 0;
    for (int i = 0; i < 8; i++) {
        int match = (abs(out[i][0] - bars[i][0]) < 8 && abs(out[i][1] - bars[i][1]) < 8 && abs(out[i][2] - bars[i][2]) < 8);
        printf("  bar %d: got %3d,%3d,%3d  want %3d,%3d,%3d  %s\n", i,
               out[i][0], out[i][1], out[i][2], bars[i][0], bars[i][1], bars[i][2], match ? "ok" : "MISMATCH");
        good += match;
    }
    printf("M2-CORE: %s (%d/8 bars through external sampler)\n", good == 8 ? "OK" : "FAIL", good);
    return good == 8 ? 0 : 5;
}
