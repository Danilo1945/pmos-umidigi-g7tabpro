#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

int main() {
    int fd = open("/dev/dri/card0", O_RDWR);
    if (fd < 0) { perror("open card0"); return 1; }

    drmModeRes *res = drmModeGetResources(fd);
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
    drmModeModeInfo mode = conn->modes[0];
    uint32_t crtc_id = res->crtcs[0];

    struct drm_mode_create_dumb creq = {0};
    creq.width = mode.hdisplay;
    creq.height = mode.vdisplay;
    creq.bpp = 32;
    int r = drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq);
    printf("DUMB create: ret=%d, handle=%u, pitch=%u, size=%llu\n", r, creq.handle, creq.pitch, creq.size);

    uint32_t fb_id = 0;
    r = drmModeAddFB(fd, mode.hdisplay, mode.vdisplay, 24, 32, creq.pitch, creq.handle, &fb_id);
    printf("AddFB: ret=%d, fb_id=%u\n", r, fb_id);

    int set_ret = drmModeSetCrtc(fd, crtc_id, fb_id, 0, 0, &conn_id, 1, &mode);
    printf("drmModeSetCrtc DUMB ret=%d\n", set_ret);
    if (set_ret < 0) perror("SetCrtc DUMB error");

    return 0;
}
