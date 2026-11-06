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
    printf("=== TEST SCANOUT CON DRM_SET_MASTER Y _EXIT ===\n");

    int card_fd = open("/dev/dri/card0", O_RDWR);
    if (card_fd < 0) { perror("card0"); _exit(1); }

    int mret = drmSetMaster(card_fd);
    printf("[0] drmSetMaster ret=%d\n", mret);

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
    printf("[1] Panel DSI-1 ID: %u, CRTC ID: %u, Modo: %ux%u @ %uHz\n",
           conn_id, crtc_id, mode.hdisplay, mode.vdisplay, mode.vrefresh);

    printf("[2] Creando dispositivo GBM...\n");
    struct gbm_device *gbm = gbm_create_device(card_fd);

    printf("[3] Creando BO con flag GBM_BO_USE_SCANOUT (1200x1920 XRGB8888)...\n");
    struct gbm_bo *bo = gbm_bo_create(gbm, mode.hdisplay, mode.vdisplay,
                                      GBM_FORMAT_XRGB8888,
                                      GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!bo) {
        printf("[!] ERROR: gbm_bo_create retorno NULL\n");
        _exit(1);
    }

    union gbm_bo_handle h = gbm_bo_get_handle(bo);
    uint32_t stride = gbm_bo_get_stride(bo);
    printf("    Dumb Handle: %u, Stride: %u\n", h.u32, stride);

    printf("[4] Registrando Dumb Buffer en KMS (drmModeAddFB)...\n");
    uint32_t fb_id = 0;
    int r = drmModeAddFB(card_fd, mode.hdisplay, mode.vdisplay, 24, 32, stride, h.u32, &fb_id);
    printf("    AddFB ret=%d, fb_id=%u\n", r, fb_id);

    printf("[5] Inicializando EGL con display GBM...\n");
    EGLDisplay dpy = eglGetDisplay((EGLNativeDisplayType)gbm);
    EGLint maj = 0, min = 0;
    eglInitialize(dpy, &maj, &min);
    printf("    EGL Vendor: %s, Version: %d.%d\n", eglQueryString(dpy, EGL_VENDOR), maj, min);

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

    EGLImageKHR img = my_eglCreateImageKHR(dpy, EGL_NO_CONTEXT, EGL_NATIVE_PIXMAP_KHR, (EGLClientBuffer)bo, NULL);
    GLuint rbo = 0, fbo = 0;
    glGenRenderbuffers(1, &rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    my_glEGLImageTargetRenderbufferStorageOES(GL_RENDERBUFFER, img);

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbo);

    printf("[6] Renderizando Verde Esmeralda en GPU Mali...\n");
    glClearColor(0.0f, 0.8f, 0.4f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();

    printf("[7] Mostrando en el panel fisico via KMS (drmModeSetCrtc)...\n");
    int sret = drmModeSetCrtc(card_fd, crtc_id, fb_id, 0, 0, &conn_id, 1, &mode);
    printf("    drmModeSetCrtc ret=%d\n", sret);

    printf("[8] Exito total, saliendo con _exit(0)...\n");
    _exit(0);
}
