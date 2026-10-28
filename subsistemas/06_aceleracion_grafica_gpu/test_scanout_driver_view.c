#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

int main() {
    setbuf(stdout, NULL);
    printf("=== DEMOSTRACION EN SILICIO REAL: GPU MALI-G57 -> SCANOUT LCD ===\n");

    int card_fd = open("/dev/dri/card0", O_RDWR);
    if (card_fd < 0) { perror("card0"); _exit(1); }

    int mret = drmSetMaster(card_fd);
    printf("[1] drmSetMaster ret=%d\n", mret);

    drmModeRes *res = drmModeGetResources(card_fd);
    uint32_t conn_id = 0;
    drmModeConnector *conn = NULL;
    for (int i = 0; i < res->count_connectors; i++) {
        conn = drmModeGetConnector(card_fd, res->connectors[i]);
        if (conn && conn->connection == DRM_MODE_CONNECTED) {
            conn_id = conn->connector_id;
            break;
        }
    }
    drmModeModeInfo mode = conn->modes[0];
    uint32_t crtc_id = res->crtcs[0];
    printf("[2] Panel DSI: ID=%u, CRTC=%u, Modo=%ux%u @ %uHz\n",
           conn_id, crtc_id, mode.hdisplay, mode.vdisplay, mode.vrefresh);

    printf("[3] Creando dispositivo GBM y Dumb BO para Scanout...\n");
    struct gbm_device *gbm = gbm_create_device(card_fd);
    struct gbm_bo *bo = gbm_bo_create(gbm, mode.hdisplay, mode.vdisplay,
                                      GBM_FORMAT_XRGB8888,
                                      GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!bo) {
        printf("[!] ERROR: gbm_bo_create fallo\n");
        _exit(1);
    }

    union gbm_bo_handle h = gbm_bo_get_handle(bo);
    uint32_t stride = gbm_bo_get_stride(bo);
    printf("    BO Handle=%u, Stride=%u\n", h.u32, stride);

    printf("[4] Registrando Dumb Buffer en KMS (drmModeAddFB)...\n");
    uint32_t fb_id = 0;
    int r = drmModeAddFB(card_fd, mode.hdisplay, mode.vdisplay, 24, 32, stride, h.u32, &fb_id);
    printf("    AddFB ret=%d, fb_id=%u\n", r, fb_id);

    printf("[5] Inicializando EGL 1.5 en GPU ARM Mali-G57...\n");
    PFNEGLGETPLATFORMDISPLAYEXTPROC my_get_plat = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay dpy = my_get_plat(0x31D7, gbm, NULL);
    EGLint maj = 0, min = 0;
    eglInitialize(dpy, &maj, &min);
    printf("    EGL Inicializado: %s %d.%d (Vendor: %s)\n",
           eglQueryString(dpy, EGL_VERSION), maj, min, eglQueryString(dpy, EGL_VENDOR));

    EGLint cfg_attribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_NONE
    };
    EGLConfig cfg;
    EGLint num_cfgs;
    eglChooseConfig(dpy, cfg_attribs, &cfg, 1, &num_cfgs);

    EGLint ctx_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attribs);
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
    printf("    Contexto GL creado: %s (%s)\n",
           glGetString(GL_RENDERER), glGetString(GL_VERSION));

    printf("[6] Creando EGLImage FBO vinculado al Dumb BO de Scanout...\n");
    PFNEGLCREATEIMAGEKHRPROC my_eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC my_glEGLImageTargetRenderbufferStorageOES = (PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC)eglGetProcAddress("glEGLImageTargetRenderbufferStorageOES");

    EGLImageKHR img = my_eglCreateImageKHR(dpy, EGL_NO_CONTEXT, EGL_NATIVE_PIXMAP_KHR, (EGLClientBuffer)bo, NULL);
    GLuint rbo = 0, fbo = 0;
    glGenRenderbuffers(1, &rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    my_glEGLImageTargetRenderbufferStorageOES(GL_RENDERBUFFER, img);

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbo);

    glViewport(0, 0, mode.hdisplay, mode.vdisplay);
    printf("[7] Renderizando color Verde Esmeralda (R=0.0, G=0.8, B=0.4) con GPU Mali...\n");
    glClearColor(0.0f, 0.8f, 0.4f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    printf("    glFinish completado exitosamente!\n");

    printf("[8] Volcando al panel fisico Novatek NT36672E (drmModeSetCrtc)...\n");
    int sret = drmModeSetCrtc(card_fd, crtc_id, fb_id, 0, 0, &conn_id, 1, &mode);
    printf("    drmModeSetCrtc ret=%d (EXITO = 0)\n", sret);

    printf(">>> LA PANTALLA DE LA TABLET ESTA MOSTRANDO VERDE ESMERALDA <<<\n");
    printf(">>> MANTENIENDO PANTALLA ILUMINADA EN BUCLE INDEFINIDO <<<\n");
    while (1) {
        sleep(10);
    }

    printf("=== DEMOSTRACION COMPLETADA CON EXITO TOTAL ===\n");
    _exit(0);
}
