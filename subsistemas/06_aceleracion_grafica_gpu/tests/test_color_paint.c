#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/mman.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

int main() {
    int fd = open("/dev/dri/card0", O_RDWR);
    if (fd < 0) return 1;

    drmModeRes *res = drmModeGetResources(fd);
    uint32_t conn_id = 0;
    drmModeConnector *conn = NULL;
    for (int i = 0; i < res->count_connectors; i++) {
        conn = drmModeGetConnector(fd, res->connectors[i]);
        if (conn && conn->connection == DRM_MODE_CONNECTED) {
            conn_id = conn->connector_id;
            break;
        }
    }
    drmModeModeInfo mode = conn->modes[0];
    uint32_t crtc_id = res->crtcs[0];

    struct drm_mode_create_dumb creq = {0};
    creq.width = mode.hdisplay;
    creq.height = mode.vdisplay;
    creq.bpp = 32;
    drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq);

    struct drm_mode_map_dumb mreq = {0};
    mreq.handle = creq.handle;
    drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq);
    uint32_t *map = mmap(0, creq.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, mreq.offset);
    if (map != MAP_FAILED) {
        for (size_t i = 0; i < creq.size / 4; i++) {
            map[i] = 0xFF00FF00; /* Verde brillante (ARGB) */
        }
        munmap(map, creq.size);
    }

    uint32_t fb_id = 0;
    drmModeAddFB(fd, mode.hdisplay, mode.vdisplay, 24, 32, creq.pitch, creq.handle, &fb_id);
    int ret = drmModeSetCrtc(fd, crtc_id, fb_id, 0, 0, &conn_id, 1, &mode);
    printf("Color paint SetCrtc ret=%d, fb_id=%u\n", ret, fb_id);

    return 0;
}
