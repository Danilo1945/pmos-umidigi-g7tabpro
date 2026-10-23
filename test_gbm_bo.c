#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <dlfcn.h>
#include <sys/ioctl.h>

struct gbm_device;
struct gbm_bo;

union gbm_bo_handle {
    void *ptr;
    int32_t s32;
    uint32_t u32;
    int64_t s64;
    uint64_t u64;
};

struct drm_mode_fb_cmd2 {
    uint32_t fb_id;
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;
    uint32_t flags;
    uint32_t handles[4];
    uint32_t pitches[4];
    uint32_t offsets[4];
    uint64_t modifier[4];
};

#define DRM_IOCTL_MODE_ADDFB2 0xC04464B8
#define DRM_FORMAT_XRGB8888 0x34325258
#define DRM_FORMAT_ARGB8888 0x34325241
#define GBM_BO_USE_SCANOUT (1 << 0)
#define GBM_BO_USE_RENDERING (1 << 2)

int main() {
    int fd = open("/dev/dri/card0", O_RDWR);
    if (fd < 0) { perror("open card0"); return 1; }

    void *h = dlopen("/usr/lib/libmali-bifrost.so", RTLD_LAZY | RTLD_GLOBAL);
    if (!h) { perror("dlopen libmali"); return 1; }

    struct gbm_device *(*create_device)(int) = dlsym(h, "gbm_create_device");
    struct gbm_bo *(*bo_create)(struct gbm_device *, uint32_t, uint32_t, uint32_t, uint32_t) = dlsym(h, "gbm_bo_create");
    uint32_t (*bo_get_stride)(struct gbm_bo *) = dlsym(h, "gbm_bo_get_stride");
    union gbm_bo_handle (*bo_get_handle)(struct gbm_bo *) = dlsym(h, "gbm_bo_get_handle");
    int (*bo_get_fd)(struct gbm_bo *) = dlsym(h, "gbm_bo_get_fd");

    struct gbm_device *gbm = create_device(fd);
    printf("gbm device: %p\n", gbm);

    struct gbm_bo *bo = bo_create(gbm, 1200, 1920, DRM_FORMAT_ARGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    printf("gbm bo: %p\n", bo);
    if (!bo) return 1;

    uint32_t stride = bo_get_stride(bo);
    union gbm_bo_handle handle = bo_get_handle(bo);
    int dma_fd = bo_get_fd ? bo_get_fd(bo) : -1;
    printf("stride=%u, handle.u32=%u, dma_fd=%d\n", stride, handle.u32, dma_fd);

    struct drm_mode_fb_cmd2 fb2 = {0};
    fb2.width = 1200;
    fb2.height = 1920;
    fb2.pixel_format = DRM_FORMAT_ARGB8888;
    fb2.flags = 0;
    fb2.handles[0] = handle.u32;
    fb2.pitches[0] = stride;

    int ret = ioctl(fd, DRM_IOCTL_MODE_ADDFB2, &fb2);
    printf("ioctl ADDFB2 ret=%d, fb_id=%u\n", ret, fb2.fb_id);
    if (ret < 0) perror("ADDFB2 error");

    return 0;
}
