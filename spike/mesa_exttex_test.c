/* mesa_exttex_test.c — can Mesa llvmpipe take glTexImage2D DIRECTLY on a GL_TEXTURE_EXTERNAL_OES
 * texture (no real EGLImage) and still sample it through a samplerExternalOES essl3 shader?
 * If yes, our glEGLImageTargetTexture2DOES can be a plain pixel upload and we need only our own
 * libEGL_adreno.so — no glforward shim/host surgery. If no, we must create a real EGLImage in the
 * glhost (extend shim+host). */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>

static GLuint mkshader(GLenum t, const char *s)
{
    GLuint sh = glCreateShader(t); glShaderSource(sh, 1, &s, NULL); glCompileShader(sh);
    GLint ok = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) { char l[1024]; glGetShaderInfoLog(sh, sizeof l, NULL, l); printf("compile: %s\n", l); return 0; }
    return sh;
}

int main(void)
{
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

    const uint8_t bars[8][4] = { {255,0,0,255},{0,255,0,255},{0,0,255,255},{255,255,0,255},
                                 {0,255,255,255},{255,0,255,255},{255,255,255,255},{32,32,32,255} };
    GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_EXTERNAL_OES, t);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_EXTERNAL_OES, 0, GL_RGBA, 8, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, bars);
    GLenum e = glGetError();
    printf("glTexImage2D(GL_TEXTURE_EXTERNAL_OES) err=0x%x %s\n", e, e ? "(not allowed)" : "(accepted)");
    if (e) { printf("EXTTEX: FAIL (need real EGLImage)\n"); return 5; }

    const char *vs = "#version 320 es\nlayout(location=0) in vec2 pos;\nout vec2 uv;\n"
                     "void main(){ uv=pos*0.5+0.5; gl_Position=vec4(pos,0.0,1.0);}\n";
    const char *fs = "#version 320 es\n#extension GL_OES_EGL_image_external_essl3 : require\n"
                     "precision mediump float;\nuniform samplerExternalOES tex;\nin vec2 uv;\nout vec4 o;\n"
                     "void main(){ o=texture(tex,uv);}\n";
    GLuint v = mkshader(GL_VERTEX_SHADER, vs), f = mkshader(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 3;
    GLuint pr = glCreateProgram(); glAttachShader(pr, v); glAttachShader(pr, f); glLinkProgram(pr);
    GLint ok = 0; glGetProgramiv(pr, GL_LINK_STATUS, &ok);
    if (!ok) { char l[1024]; glGetProgramInfoLog(pr, sizeof l, NULL, l); printf("link: %s\n", l); return 3; }
    glUseProgram(pr); glUniform1i(glGetUniformLocation(pr, "tex"), 0);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_EXTERNAL_OES, t);

    GLuint fbo, dst; glGenFramebuffers(1, &fbo); glGenTextures(1, &dst);
    glBindTexture(GL_TEXTURE_2D, dst);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dst, 0);
    glViewport(0, 0, 8, 1);
    const float quad[] = { -1,-1, 3,-1, -1,3 };
    GLuint vbo; glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    uint8_t out[8][4]; glReadPixels(0, 0, 8, 1, GL_RGBA, GL_UNSIGNED_BYTE, out);
    int good = 0;
    for (int i = 0; i < 8; i++)
        good += (abs(out[i][0] - bars[i][0]) < 8 && abs(out[i][1] - bars[i][1]) < 8 && abs(out[i][2] - bars[i][2]) < 8);
    printf("EXTTEX: %s (%d/8 bars)\n", good == 8 ? "OK" : "FAIL", good);
    return good == 8 ? 0 : 5;
}
