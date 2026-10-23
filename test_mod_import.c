#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <dlfcn.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
#include <drm_fourcc.h>

#ifndef GBM_BO_IMPORT_FD_MODIFIER
#define GBM_BO_IMPORT_FD_MODIFIER 0x5504
#endif

struct gbm_import_fd_modifier_data_local {
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t num_fds;
    int fds[4];
    int strides[4];
    int offsets[4];
    uint64_t modifier;
};

int main() {
    int card_fd = open("/dev/dri/card0", O_RDWR);
    if (card_fd < 0) { perror("open card0"); return 1; }

    void *mali = dlopen("/usr/lib/libmali-bifrost.so", RTLD_LAZY | RTLD_GLOBAL);
    if (!mali) { printf("dlopen fail: %s\n", dlerror()); return 1; }

    struct gbm_device *(*real_gbm_create_device)(int) = dlsym(mali, "gbm_create_device");
    struct gbm_bo *(*real_gbm_bo_import)(struct gbm_device *, uint32_t, void *, uint32_t) = dlsym(mali, "gbm_bo_import");

    struct gbm_device *gbm = real_gbm_create_device(card_fd);
    printf("GBM device: %p\n", (void*)gbm);
    fflush(stdout);

    struct drm_mode_create_dumb creq = {0};
    creq.width = 1200;
    creq.height = 1920;
    creq.bpp = 32;
    drmIoctl(card_fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq);

    int prime_fd = -1;
    drmPrimeHandleToFD(card_fd, creq.handle, DRM_CLOEXEC | DRM_RDWR, &prime_fd);
    printf("Dumb handle=%u, pitch=%u, prime_fd=%d\n", creq.handle, creq.pitch, prime_fd);
    fflush(stdout);

    struct gbm_import_fd_modifier_data_local mod_data = {
        .width = 1200,
        .height = 1920,
        .format = DRM_FORMAT_XRGB8888,
        .num_fds = 1,
        .fds = { prime_fd, -1, -1, -1 },
        .strides = { (int)creq.pitch, 0, 0, 0 },
        .offsets = { 0, 0, 0, 0 },
        .modifier = DRM_FORMAT_MOD_LINEAR
    };

    struct gbm_bo *bo = real_gbm_bo_import(gbm, GBM_BO_IMPORT_FD_MODIFIER, &mod_data, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    printf("RESULTADO GBM_BO_IMPORT_FD_MODIFIER: bo = %p\n", (void*)bo);
    fflush(stdout);

    close(prime_fd);
    close(card_fd);
    return 0;
}
