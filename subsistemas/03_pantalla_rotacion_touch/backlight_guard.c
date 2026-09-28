/*
 * backlight_guard.c - Supervisor de seguridad contra picos de brillo (Anti-UVLO)
 * UMIDIGI G7 Tab Pro (Helio G99 / MT6789)
 * Previene caídas bruscas de tensión (brownouts) limitando el brillo a <= 80 (nominal 70).
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#define BACKLIGHT_PATH "/sys/class/leds/lcd-backlight/brightness"
#define MAX_SAFE_BRIGHTNESS 80
#define NOMINAL_BRIGHTNESS "70\n"

int main(int argc, char *argv[]) {
    if (daemon(1, 1) < 0) {
        perror("daemon");
    }

    char buf[32];
    while (1) {
        int fd = open(BACKLIGHT_PATH, O_RDWR);
        if (fd >= 0) {
            int n = read(fd, buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = '\0';
                int val = atoi(buf);
                if (val > MAX_SAFE_BRIGHTNESS) {
                    lseek(fd, 0, SEEK_SET);
                    write(fd, NOMINAL_BRIGHTNESS, strlen(NOMINAL_BRIGHTNESS));
                }
            }
            close(fd);
        }
        usleep(50000); // Chequeo cada 50 ms
    }
    return 0;
}
