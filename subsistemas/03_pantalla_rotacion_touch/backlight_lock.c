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

    int fd = open(BACKLIGHT_PATH, O_RDWR);
    if (fd < 0) {
        perror("Error abriendo " BACKLIGHT_PATH);
        return 1;
    }

    char buf[32];
    while (running) {
        lseek(fd, 0, SEEK_SET);
        int n = read(fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            int val = atoi(buf);
            if (val != TARGET_VAL) {
                lseek(fd, 0, SEEK_SET);
                write(fd, TARGET_BRIGHTNESS, strlen(TARGET_BRIGHTNESS));
            }
        } else {
            lseek(fd, 0, SEEK_SET);
            write(fd, TARGET_BRIGHTNESS, strlen(TARGET_BRIGHTNESS));
        }
        usleep(50000); // 50 ms
    }

    close(fd);
    return 0;
}
