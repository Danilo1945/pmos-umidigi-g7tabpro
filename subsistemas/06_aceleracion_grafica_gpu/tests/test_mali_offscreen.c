#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <dlfcn.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

struct gbm_device;
struct gbm_bo;

#define DRM_FORMAT_ARGB8888 0x34325241
#define GBM_BO_USE_RENDERING (1 << 2)

int main() {
    int fd = open("/dev/dri/card0", O_RDWR);
    if (fd < 0) { perror("open card0"); return 1; }

    void *mali = dlopen("/usr/lib/libmali-bifrost.so", RTLD_LAZY | RTLD_GLOBAL);
    if (!mali) { perror("dlopen libmali"); return 1; }

    struct gbm_device *(*create_device)(int) = dlsym(mali, "gbm_create_device");
    struct gbm_bo *(*bo_create)(struct gbm_device *, uint32_t, uint32_t, uint32_t, uint32_t) = dlsym(mali, "gbm_bo_create");

    EGLDisplay (*eglGetPlatformDisplay)(EGLenum, void *, const EGLint *) = dlsym(mali, "eglGetPlatformDisplay");
    EGLBoolean (*eglInitializeFunc)(EGLDisplay, EGLint *, EGLint *) = dlsym(mali, "eglInitialize");
    const char *(*eglQueryStringFunc)(EGLDisplay, EGLint) = dlsym(mali, "eglQueryString");

    struct gbm_device *gbm = create_device(fd);
    printf("gbm device: %p\n", gbm);

    EGLDisplay dpy = eglGetPlatformDisplay ? eglGetPlatformDisplay(0x31D7, gbm, NULL) : NULL;
    printf("eglGetPlatformDisplay: %p\n", dpy);

    EGLint maj = 0, min = 0;
    EGLBoolean init_ok = eglInitializeFunc ? eglInitializeFunc(dpy, &maj, &min) : 0;
    printf("eglInitialize: %d, version: %d.%d\n", init_ok, maj, min);

    if (init_ok) {
        printf("EGL Vendor: %s\n", eglQueryStringFunc(dpy, EGL_VENDOR));
        printf("EGL Version: %s\n", eglQueryStringFunc(dpy, EGL_VERSION));
    }

    return 0;
}
