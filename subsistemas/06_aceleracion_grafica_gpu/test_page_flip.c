#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

static void page_flip_handler(int fd, unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec, void *user_data) {
    printf("    [VBLANK] Page flip completado! seq=%u, time=%u.%06u\n", sequence, tv_sec, tv_usec);
}

int main() {
    setbuf(stdout, NULL);
    printf("=== TEST PAGE FLIP EN SILICIO REAL: GPU MALI-G57 -> LCD ===\n");

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

    struct gbm_device *gbm = gbm_create_device(card_fd);

    /* Buffer 1: Verde */
    printf("[3] Creando BO 1 (Verde)...\n");
    struct gbm_bo *bo1 = gbm_bo_create(gbm, mode.hdisplay, mode.vdisplay,
                                       GBM_FORMAT_XRGB8888,
                                       GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    union gbm_bo_handle h1 = gbm_bo_get_handle(bo1);
    uint32_t stride1 = gbm_bo_get_stride(bo1);
    uint32_t fb_id1 = 0;
    drmModeAddFB(card_fd, mode.hdisplay, mode.vdisplay, 24, 32, stride1, h1.u32, &fb_id1);
    printf("    BO1 Handle=%u, fb_id1=%u\n", h1.u32, fb_id1);

    /* Buffer 2: Azul */
    printf("[4] Creando BO 2 (Azul)...\n");
    struct gbm_bo *bo2 = gbm_bo_create(gbm, mode.hdisplay, mode.vdisplay,
                                       GBM_FORMAT_XRGB8888,
                                       GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    union gbm_bo_handle h2 = gbm_bo_get_handle(bo2);
    uint32_t stride2 = gbm_bo_get_stride(bo2);
    uint32_t fb_id2 = 0;
    drmModeAddFB(card_fd, mode.hdisplay, mode.vdisplay, 24, 32, stride2, h2.u32, &fb_id2);
    printf("    BO2 Handle=%u, fb_id2=%u\n", h2.u32, fb_id2);

    /* Inicializar EGL */
    printf("[5] Inicializando EGL 1.5 en GPU ARM Mali-G57...\n");
    PFNEGLGETPLATFORMDISPLAYEXTPROC my_get_plat = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay dpy = my_get_plat(0x31D7, gbm, NULL);
    EGLint maj = 0, min = 0;
    eglInitialize(dpy, &maj, &min);

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

    PFNEGLCREATEIMAGEKHRPROC my_eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC my_glEGLImageTargetRenderbufferStorageOES = (PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC)eglGetProcAddress("glEGLImageTargetRenderbufferStorageOES");

    /* Renderizar Verde en BO 1 */
    printf("[6] Renderizando Verde en BO1...\n");
    EGLImageKHR img1 = my_eglCreateImageKHR(dpy, EGL_NO_CONTEXT, EGL_NATIVE_PIXMAP_KHR, (EGLClientBuffer)bo1, NULL);
    GLuint rbo1 = 0, fbo1 = 0;
    glGenRenderbuffers(1, &rbo1);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo1);
    my_glEGLImageTargetRenderbufferStorageOES(GL_RENDERBUFFER, img1);
    glGenFramebuffers(1, &fbo1);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo1);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbo1);
    glViewport(0, 0, mode.hdisplay, mode.vdisplay);
    glClearColor(0.0f, 0.8f, 0.4f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();

    /* Renderizar Azul en BO 2 */
    printf("[7] Renderizando Azul en BO2...\n");
    EGLImageKHR img2 = my_eglCreateImageKHR(dpy, EGL_NO_CONTEXT, EGL_NATIVE_PIXMAP_KHR, (EGLClientBuffer)bo2, NULL);
    GLuint rbo2 = 0, fbo2 = 0;
    glGenRenderbuffers(1, &rbo2);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo2);
    my_glEGLImageTargetRenderbufferStorageOES(GL_RENDERBUFFER, img2);
    glGenFramebuffers(1, &fbo2);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo2);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbo2);
    glViewport(0, 0, mode.hdisplay, mode.vdisplay);
    glClearColor(0.1f, 0.3f, 0.9f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();

    /* Modeset con Frame 1 (Verde) */
    printf("[8] Modesetting con Frame 1 (Verde, fb_id=%u)...\n", fb_id1);
    int sret = drmModeSetCrtc(card_fd, crtc_id, fb_id1, 0, 0, &conn_id, 1, &mode);
    printf("    drmModeSetCrtc ret=%d\n", sret);
    printf(">>> PANTALLA: VERDE (1.5 segundos) <<<\n");
    usleep(1500000);

    /* Page Flip a Frame 2 (Azul) */
    printf("[9] Ejecutando drmModePageFlip a Frame 2 (Azul, fb_id=%u)...\n", fb_id2);
    int fret = drmModePageFlip(card_fd, crtc_id, fb_id2, DRM_MODE_PAGE_FLIP_EVENT, NULL);
    printf("    drmModePageFlip ret=%d\n", fret);

    if (fret == 0) {
        printf("[10] Esperando VBLANK event...\n");
        drmEventContext ev = {
            .version = DRM_EVENT_CONTEXT_VERSION,
            .page_flip_handler = page_flip_handler,
        };
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(card_fd, &fds);
        struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
        int s = select(card_fd + 1, &fds, NULL, NULL, &tv);
        if (s > 0) {
            drmHandleEvent(card_fd, &ev);
            printf(">>> PANTALLA: AZUL (1.5 segundos) <<<\n");
            usleep(1500000);
        } else {
            printf("[!] Timeout esperando VBLANK event\n");
        }
    }

    printf("=== TEST PAGE FLIP COMPLETADO EXITOSAMENTE ===\n");
    _exit(0);
}
