#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <linux/watchdog.h>

static int open_and_test(const char *path, int kmsg) {
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd >= 0) {
        int dummy = 0;
        if (ioctl(fd, WDIOC_KEEPALIVE, &dummy) == 0 || write(fd, "\x01", 1) == 1) {
            if (kmsg >= 0) {
                dprintf(kmsg, "<1>[wdt_pet] Watchdog OK en %s (fd=%d)\n", path, fd);
            }
            return fd;
        }
        close(fd);
    }
    return -1;
}

static int resolve_watchdog_fd(int kmsg) {
    int fd;

    // 1. Probar nodos estándar si devtmpfs o mknod los creó (incluyendo major 10:130)
    unlink("/dev/wdt_std");
    mknod("/dev/wdt_std", S_IFCHR | 0660, makedev(10, 130));
    fd = open_and_test("/dev/wdt_std", kmsg);
    if (fd >= 0) {
        unlink("/dev/watchdog0");
        unlink("/dev/watchdog");
        mknod("/dev/watchdog0", S_IFCHR | 0660, makedev(10, 130));
        mknod("/dev/watchdog", S_IFCHR | 0660, makedev(10, 130));
        unlink("/dev/wdt_std");
        return fd;
    }
    unlink("/dev/wdt_std");

    fd = open_and_test("/dev/watchdog0", kmsg);
    if (fd >= 0) return fd;
    fd = open_and_test("/dev/watchdog", kmsg);
    if (fd >= 0) return fd;

    // 2. Probar sysfs dev files si existen
    const char *sys_paths[] = {
        "/sys/class/watchdog/watchdog0/dev",
        "/sys/class/misc/watchdog/dev",
        NULL
    };
    for (int i = 0; sys_paths[i]; i++) {
        FILE *f = fopen(sys_paths[i], "r");
        if (f) {
            int maj = 0, min = 0;
            if (fscanf(f, "%d:%d", &maj, &min) == 2) {
                unlink("/dev/watchdog0");
                mknod("/dev/watchdog0", S_IFCHR | 0660, makedev(maj, min));
                fclose(f);
                fd = open_and_test("/dev/watchdog0", kmsg);
                if (fd >= 0) return fd;
            } else {
                fclose(f);
            }
        }
    }

    // 3. Buscar en /proc/devices por "watchdog"
    FILE *pf = fopen("/proc/devices", "r");
    if (pf) {
        char line[256];
        while (fgets(line, sizeof(line), pf)) {
            int maj = 0;
            char name[64] = {0};
            if (sscanf(line, "%d %63s", &maj, name) == 2) {
                if (strcmp(name, "watchdog") == 0) {
                    unlink("/dev/watchdog0");
                    mknod("/dev/watchdog0", S_IFCHR | 0660, makedev(maj, 0));
                    unlink("/dev/watchdog");
                    mknod("/dev/watchdog", S_IFCHR | 0660, makedev(maj, 0));
                    fclose(pf);
                    if (kmsg >= 0) {
                        dprintf(kmsg, "<1>[wdt_pet] Detectado watchdog dinamico en /proc/devices: major %d\n", maj);
                    }
                    fd = open_and_test("/dev/watchdog0", kmsg);
                    if (fd >= 0) return fd;
                    fd = open_and_test("/dev/watchdog", kmsg);
                    if (fd >= 0) return fd;
                    return -1;
                }
            }
        }
        fclose(pf);
    }

    // 4. Fallback de escaneo por majors dinámicos estándar (235..255)
    for (int maj = 235; maj <= 255; maj++) {
        unlink("/dev/wdt_scan");
        mknod("/dev/wdt_scan", S_IFCHR | 0660, makedev(maj, 0));
        fd = open_and_test("/dev/wdt_scan", kmsg);
        if (fd >= 0) {
            unlink("/dev/watchdog0");
            unlink("/dev/watchdog");
            mknod("/dev/watchdog0", S_IFCHR | 0660, makedev(maj, 0));
            mknod("/dev/watchdog", S_IFCHR | 0660, makedev(maj, 0));
            return fd;
        }
    }
    unlink("/dev/wdt_scan");

    return -1;
}

int main(int argc, char *argv[]) {
    int kmsg = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    if (kmsg >= 0) {
        dprintf(kmsg, "<1>[wdt_pet] MediaTek Watchdog Feeder v14 iniciado (PID %d)\n", getpid());
    }

    int fd = -1;
    unsigned long count = 0;

    while (1) {
        if (fd < 0) {
            fd = resolve_watchdog_fd(kmsg);
            if (fd < 0 && kmsg >= 0 && (count % 10 == 0)) {
                dprintf(kmsg, "<3>[wdt_pet] Esperando registro del Watchdog en el kernel...\n");
            }
        }

        if (fd >= 0) {
            int dummy = 0;
            if (ioctl(fd, WDIOC_KEEPALIVE, &dummy) < 0) {
                if (write(fd, "\x01", 1) < 0) {
                    close(fd);
                    fd = -1;
                }
            }
            if (fd >= 0) {
                count++;
                if (kmsg >= 0 && (count % 30 == 0)) {
                    dprintf(kmsg, "<1>[wdt_pet] Latido #%lu enviado al Watchdog (sistema vivo)\n", count);
                }
            }
        }

        usleep(500000); // Latido cada 500ms (2 veces por segundo)
    }
    return 0;
}
