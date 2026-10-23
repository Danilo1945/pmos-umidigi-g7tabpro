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
    if (!res) { perror("drmModeGetResources"); return 1; }

    for (int i = 0; i < res->count_crtcs; i++) {
        drmModeCrtc *crtc = drmModeGetCrtc(fd, res->crtcs[i]);
        if (crtc) {
            printf("CRTC #%d (ID %u): buffer_id=%u, %ux%u at (%d,%d), mode_valid=%d (%s)\n",
                   i, crtc->crtc_id, crtc->buffer_id,
                   crtc->width, crtc->height, crtc->x, crtc->y,
                   crtc->mode_valid, crtc->mode.name);
            if (crtc->buffer_id > 0) {
                drmModeFB *fb = drmModeGetFB(fd, crtc->buffer_id);
                if (fb) {
                    printf("  -> Activo FB ID %u: %ux%u, bpp=%u, depth=%u, pitch=%u, handle=%u\n",
                           fb->fb_id, fb->width, fb->height, fb->bpp, fb->depth, fb->pitch, fb->handle);
                    drmModeFreeFB(fb);
                }
            }
            drmModeFreeCrtc(crtc);
        }
    }
    drmModeFreeResources(res);
    close(fd);
    return 0;
}
