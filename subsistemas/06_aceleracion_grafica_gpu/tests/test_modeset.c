#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <dlfcn.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

struct gbm_device;
struct gbm_bo;

#define DRM_FORMAT_ARGB8888 0x34325241
#define GBM_BO_USE_SCANOUT (1 << 0)
#define GBM_BO_USE_RENDERING (1 << 2)

int main() {
    printf("[1] Abriendo card0...\n"); fflush(stdout);
    int fd = open("/dev/dri/card0", O_RDWR);
    if (fd < 0) { perror("open card0"); return 1; }

    printf("[2] Obteniendo recursos DRM...\n"); fflush(stdout);
    drmModeRes *res = drmModeGetResources(fd);
    if (!res) { perror("drmModeGetResources"); return 1; }

    uint32_t conn_id = 0;
    drmModeConnector *conn = NULL;
    for (int i = 0; i < res->count_connectors; i++) {
        conn = drmModeGetConnector(fd, res->connectors[i]);
        if (conn && conn->connection == DRM_MODE_CONNECTED) {
            conn_id = conn->connector_id;
            break;
        }
        if (conn) drmModeFreeConnector(conn);
    }
    if (!conn) { printf("No connected connector\n"); return 1; }

    printf("[3] Conector: %u, Modos: %d\n", conn_id, conn->count_modes); fflush(stdout);
    drmModeModeInfo mode = conn->modes[0];
    printf("[4] Modo: %s %dx%d @ %dHz\n", mode.name, mode.hdisplay, mode.vdisplay, mode.vrefresh); fflush(stdout);

    uint32_t crtc_id = res->crtcs[0];
    printf("[5] CRTC ID: %u\n", crtc_id); fflush(stdout);

    printf("[6] Cargando libmali-bifrost.so...\n"); fflush(stdout);
    void *mali = dlopen("/usr/lib/libmali-bifrost.so", RTLD_LAZY | RTLD_GLOBAL);
    if (!mali) { printf("Error dlopen mali: %s\n", dlerror()); return 1; }

    struct gbm_device *(*create_device)(int) = dlsym(mali, "gbm_create_device");
    struct gbm_bo *(*bo_create)(struct gbm_device *, uint32_t, uint32_t, uint32_t, uint32_t) = dlsym(mali, "gbm_bo_create");
    uint32_t (*bo_get_stride)(struct gbm_bo *) = dlsym(mali, "gbm_bo_get_stride");
    union { void *p; uint32_t u32; } (*bo_get_handle)(struct gbm_bo *) = (void*)dlsym(mali, "gbm_bo_get_handle");

    printf("[7] Creando dispositivo GBM...\n"); fflush(stdout);
    struct gbm_device *gbm = create_device(fd);
    printf("[8] gbm_create_device ret: %p\n", (void*)gbm); fflush(stdout);

    printf("[9] Creando GBM BO (%dx%d)...\n", mode.hdisplay, mode.vdisplay); fflush(stdout);
    struct gbm_bo *bo = bo_create(gbm, mode.hdisplay, mode.vdisplay, DRM_FORMAT_ARGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    printf("[10] gbm_bo_create ret: %p\n", (void*)bo); fflush(stdout);

    uint32_t stride = bo_get_stride(bo);
    uint32_t handle = bo_get_handle(bo).u32;
    printf("[11] BO handle: %u, stride: %u\n", handle, stride); fflush(stdout);

    uint32_t fb_id = 0;
    uint32_t handles[4] = {handle, 0, 0, 0};
    uint32_t pitches[4] = {stride, 0, 0, 0};
    uint32_t offsets[4] = {0, 0, 0, 0};
    uint64_t modifiers[4] = {0, 0, 0, 0};
    int r = drmModeAddFB2WithModifiers(fd, mode.hdisplay, mode.vdisplay, DRM_FORMAT_ARGB8888, handles, pitches, offsets, modifiers, &fb_id, 0);
    printf("[12] AddFB2: ret=%d, fb_id=%u\n", r, fb_id); fflush(stdout);

    drmSetMaster(fd);
    int set_ret = drmModeSetCrtc(fd, crtc_id, fb_id, 0, 0, &conn_id, 1, &mode);
    printf("[13] drmModeSetCrtc ret=%d\n", set_ret); fflush(stdout);
    if (set_ret < 0) perror("SetCrtc error");

    printf("[14] FIN EXITOSO\n"); fflush(stdout);
    return 0;
}
