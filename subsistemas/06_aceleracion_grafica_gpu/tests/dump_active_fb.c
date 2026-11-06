#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/mman.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

int main(int argc, char *argv[]) {
    const char *out_path = argc > 1 ? argv[1] : "/tmp/screenshot_hardware.ppm";

    int fd = open("/dev/dri/card0", O_RDWR);
    if (fd < 0) { perror("open card0"); return 1; }

    drmModeRes *res = drmModeGetResources(fd);
    if (!res) { perror("drmModeGetResources"); return 1; }

    uint32_t active_fb_id = 0;
    for (int i = 0; i < res->count_crtcs; i++) {
        drmModeCrtc *crtc = drmModeGetCrtc(fd, res->crtcs[i]);
        if (crtc && crtc->buffer_id > 0) {
            active_fb_id = crtc->buffer_id;
            drmModeFreeCrtc(crtc);
            break;
        }
        if (crtc) drmModeFreeCrtc(crtc);
    }

    if (active_fb_id == 0) {
        printf("No se encontró CRTC con framebuffer activo\n");
        return 1;
    }

    drmModeFB *fb = drmModeGetFB(fd, active_fb_id);
    if (!fb) { perror("drmModeGetFB"); return 1; }

    printf("Volcando FB ID %u: %ux%u (pitch %u, handle %u)...\n",
           fb->fb_id, fb->width, fb->height, fb->pitch, fb->handle);

    struct drm_mode_map_dumb mreq = {0};
    mreq.handle = fb->handle;
    if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) != 0) {
        perror("DRM_IOCTL_MODE_MAP_DUMB");
        return 1;
    }

    uint64_t size = (uint64_t)fb->pitch * fb->height;
    void *map = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, mreq.offset);
    if (map == MAP_FAILED) {
        perror("mmap dumb buffer");
        return 1;
    }

    FILE *out = fopen(out_path, "wb");
    if (!out) { perror("fopen out"); return 1; }

    // Escribir cabecera PPM binaria (P6)
    fprintf(out, "P6\n%u %u\n255\n", fb->width, fb->height);

    uint8_t *src = (uint8_t *)map;
    uint8_t rgb[3];
    for (uint32_t y = 0; y < fb->height; y++) {
        uint32_t *row = (uint32_t *)(src + y * fb->pitch);
        for (uint32_t x = 0; x < fb->width; x++) {
            uint32_t px = row[x];
            // Format ARGB8888 / XRGB8888 -> R, G, B
            rgb[0] = (px >> 16) & 0xFF;
            rgb[1] = (px >> 8) & 0xFF;
            rgb[2] = px & 0xFF;
            fwrite(rgb, 1, 3, out);
        }
    }

    fclose(out);
    munmap(map, size);
    drmModeFreeFB(fb);
    drmModeFreeResources(res);
    close(fd);

    printf("Captura guardada con éxito en: %s (%llu bytes)\n", out_path, (unsigned long long)size);
    return 0;
}
