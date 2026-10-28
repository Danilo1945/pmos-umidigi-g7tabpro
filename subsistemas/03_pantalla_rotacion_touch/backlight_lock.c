#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>

#define BACKLIGHT_PATH "/sys/class/leds/lcd-backlight/brightness"
#define TARGET_BRIGHTNESS "15\n"
#define TARGET_VAL 15

static volatile int running = 1;

void handle_signal(int sig) {
    (void)sig;
    running = 0;
}

int main(void) {
    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);

    int fd = -1;
    char buf[32];
    while (running) {
        if (fd < 0) {
            fd = open(BACKLIGHT_PATH, O_RDWR);
            if (fd < 0) {
                usleep(50000); // 50 ms espera nodo sysfs
                continue;
            }
        }

        lseek(fd, 0, SEEK_SET);
        int n = read(fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            int val = atoi(buf);
            if (val != TARGET_VAL) {
                lseek(fd, 0, SEEK_SET);
                if (write(fd, TARGET_BRIGHTNESS, strlen(TARGET_BRIGHTNESS)) < 0) {
                    close(fd);
                    fd = -1;
                }
            }
        } else {
            lseek(fd, 0, SEEK_SET);
            if (write(fd, TARGET_BRIGHTNESS, strlen(TARGET_BRIGHTNESS)) < 0) {
                close(fd);
                fd = -1;
            }
        }
        usleep(50000); // 50 ms
    }

    if (fd >= 0) close(fd);
    return 0;
}
