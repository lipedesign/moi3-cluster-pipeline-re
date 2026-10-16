/* mesa_caps.c — probe what the container's Mesa (llvmpipe, software) can do, specifically the
 * pieces the mss's import path needs: EGL_EXT_image_dma_buf_import and GL_OES_EGL_image_external.
 * If these are absent, the "run the real mss end to end in Mesa" route hits a wall here. */
#include <stdio.h>
#include <string.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

static int has(const char *exts, const char *name) { return exts && strstr(exts, name) != NULL; }

int main(void)
{
    EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    if (d == EGL_NO_DISPLAY) { printf("no surfaceless display\n"); return 2; }
    EGLint ma, mi;
    if (!eglInitialize(d, &ma, &mi)) { printf("eglInitialize failed 0x%x\n", eglGetError()); return 2; }
    printf("EGL %d.%d  vendor=%s\n", ma, mi, eglQueryString(d, EGL_VENDOR));
    const char *ee = eglQueryString(d, EGL_EXTENSIONS);
    printf("EGL dma_buf_import      : %s\n", has(ee, "EGL_EXT_image_dma_buf_import") ? "YES" : "no");
    printf("EGL dma_buf_import_mods : %s\n", has(ee, "EGL_EXT_image_dma_buf_import_modifiers") ? "YES" : "no");
    printf("EGL image_base          : %s\n", has(ee, "EGL_KHR_image_base") ? "YES" : "no");
    printf("EGL KHR_image           : %s\n", has(ee, "EGL_KHR_image") ? "YES" : "no");

    eglBindAPI(EGL_OPENGL_ES_API);
    EGLConfig cfg; EGLint n = 0;
    EGLint ca[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    eglChooseConfig(d, ca, &cfg, 1, &n);
    EGLint cx[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE };
    EGLContext ctx = eglCreateContext(d, cfg, EGL_NO_CONTEXT, cx);
    EGLint pb[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
    EGLSurface s = eglCreatePbufferSurface(d, cfg, pb);
    if (!eglMakeCurrent(d, s, s, ctx)) { printf("makeCurrent failed 0x%x\n", eglGetError()); return 2; }
    printf("GL_VERSION  : %s\n", (const char *)glGetString(GL_VERSION));
    printf("GL_RENDERER : %s\n", (const char *)glGetString(GL_RENDERER));
    const char *ge = (const char *)glGetString(GL_EXTENSIONS);
    printf("GL image_external       : %s\n", has(ge, "GL_OES_EGL_image_external") ? "YES" : "no");
    printf("GL image_external_essl3 : %s\n", has(ge, "GL_OES_EGL_image_external_essl3") ? "YES" : "no");
    printf("GL EGL_image            : %s\n", has(ge, "GL_OES_EGL_image") ? "YES" : "no");
    return 0;
}
