#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

int main() {
    setbuf(stdout, NULL);
    printf("=== TEST GL_CLEAR CON FBO + MALI GPU ===\n");
    int fd = open("/dev/dri/card0", O_RDWR);
    if (fd < 0) { perror("open card0"); return 1; }

    struct gbm_device *gbm = gbm_create_device(fd);
    PFNEGLGETPLATFORMDISPLAYEXTPROC my_get_plat = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay dpy = my_get_plat(0x31D7, gbm, NULL);

    EGLint maj, min;
    eglInitialize(dpy, &maj, &min);
    printf("[1] EGL %d.%d, Vendor: %s\n", maj, min, eglQueryString(dpy, EGL_VENDOR));

    EGLint cfg_attribs[] = {
        EGL_SURFACE_TYPE, EGL_DONT_CARE,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_NONE
    };
    EGLConfig cfg;
    EGLint num;
    eglChooseConfig(dpy, cfg_attribs, &cfg, 1, &num);

    EGLint ctx_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attribs);

    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
    printf("[2] Context OK: GL_VENDOR: %s, GL_RENDERER: %s\n",
           glGetString(GL_VENDOR), glGetString(GL_RENDERER));

    printf("[3] Creando BO GBM y EGLImage FBO...\n");
    struct gbm_bo *bo = gbm_bo_create(gbm, 64, 64, GBM_FORMAT_XRGB8888, GBM_BO_USE_RENDERING);
    if (!bo) {
        printf("[!] gbm_bo_create retorno NULL\n");
        return 1;
    }

    PFNEGLCREATEIMAGEKHRPROC my_eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC my_glEGLImageTargetRenderbufferStorageOES = (PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC)eglGetProcAddress("glEGLImageTargetRenderbufferStorageOES");

    EGLImageKHR img = my_eglCreateImageKHR(dpy, EGL_NO_CONTEXT, EGL_NATIVE_PIXMAP_KHR, (EGLClientBuffer)bo, NULL);
    if (!img) {
        printf("[!] eglCreateImageKHR retorno NULL\n");
        return 1;
    }

    GLuint rbo = 0, fbo = 0;
    glGenRenderbuffers(1, &rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    my_glEGLImageTargetRenderbufferStorageOES(GL_RENDERBUFFER, img);

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbo);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    printf("[4] Framebuffer status: 0x%x (0x8CD5 = COMPLETE)\n", status);

    glViewport(0, 0, 64, 64);
    printf("[5] Ejecutando glClearColor(0.0, 0.784, 1.0, 1.0) y glClear...\n");
    glClearColor(0.0f, 0.784f, 1.0f, 1.0f); // Azul cian
    glClear(GL_COLOR_BUFFER_BIT);

    printf("[6] Ejecutando glFinish...\n");
    glFinish();
    printf("[7] glFinish retorno exitosamente!\n");

    unsigned char pixel[4] = {0};
    glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    printf("[8] Pixel readback (32, 32): R=%u, G=%u, B=%u, A=%u\n", pixel[0], pixel[1], pixel[2], pixel[3]);

    if (pixel[1] > 180 && pixel[2] > 200) {
        printf(">>> EXITO TOTAL: Píxel cian verificado por glReadPixels! <<<\n");
    } else {
        printf("[!] Píxel no coincide con el color cian.\n");
    }

    printf("=== TEST GL_CLEAR COMPLETADO ===\n");
    _exit(0);
}
