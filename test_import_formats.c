#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <dlfcn.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
#include <drm_fourcc.h>

int main() {
    int card_fd = open("/dev/dri/card0", O_RDWR);
    if (card_fd < 0) { perror("open card0"); return 1; }

    void *mali = dlopen("/usr/lib/libmali-bifrost.so", RTLD_LAZY | RTLD_GLOBAL);
    if (!mali) { printf("dlopen fail: %s\n", dlerror()); return 1; }

    struct gbm_device *(*real_gbm_create_device)(int) = dlsym(mali, "gbm_create_device");
    struct gbm_bo *(*real_gbm_bo_import)(struct gbm_device *, uint32_t, void *, uint32_t) = dlsym(mali, "gbm_bo_import");

    struct gbm_device *gbm = real_gbm_create_device(card_fd);
    printf("GBM device: %p\n", (void*)gbm);

    // Crear Dumb BO en card0
    struct drm_mode_create_dumb creq = {0};
    creq.width = 1200;
    creq.height = 1920;
    creq.bpp = 32;
    int ret = drmIoctl(card_fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq);
    printf("Create dumb: ret=%d, handle=%u, pitch=%u\n", ret, creq.handle, creq.pitch);

    int prime_fd = -1;
    ret = drmPrimeHandleToFD(card_fd, creq.handle, DRM_CLOEXEC | DRM_RDWR, &prime_fd);
    printf("PrimeHandleToFD: ret=%d, fd=%d\n", ret, prime_fd);

    uint32_t formats[] = {
        DRM_FORMAT_XRGB8888,
        DRM_FORMAT_ARGB8888,
        DRM_FORMAT_XBGR8888,
        DRM_FORMAT_ABGR8888,
        0
    };
    const char *format_names[] = {
        "XRGB8888", "ARGB8888", "XBGR8888", "ABGR8888"
    };

    uint32_t usages[] = {
        0,
        GBM_BO_USE_SCANOUT,
        GBM_BO_USE_RENDERING,
        GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING
    };
    const char *usage_names[] = {
        "0", "SCANOUT", "RENDERING", "SCANOUT|RENDERING"
    };

    for (int f = 0; formats[f]; f++) {
        for (int u = 0; u < 4; u++) {
            struct gbm_import_fd_data data = {
                .fd = prime_fd,
                .width = 1200,
                .height = 1920,
                .stride = creq.pitch,
                .format = formats[f]
            };
            struct gbm_bo *bo = real_gbm_bo_import(gbm, GBM_BO_IMPORT_FD, &data, usages[u]);
            printf("Format %-8s + Usage %-18s -> bo = %p\n", format_names[f], usage_names[u], (void*)bo);
            if (bo) {
                void (*bo_destroy)(struct gbm_bo *) = dlsym(mali, "gbm_bo_destroy");
                if (bo_destroy) bo_destroy(bo);
            }
        }
    }

    close(prime_fd);
    close(card_fd);
    return 0;
}
