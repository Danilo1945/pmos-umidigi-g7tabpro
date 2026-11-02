#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/time.h>

static void emit(int fd, int type, int code, int val) {
    struct input_event ie;
    memset(&ie, 0, sizeof(ie));
    gettimeofday(&ie.time, NULL);
    ie.type = type;
    ie.code = code;
    ie.value = val;
    write(fd, &ie, sizeof(ie));
}

int main(int argc, char **argv) {
    const char *dev = "/dev/input/event2";
    if (argc > 1 && strcmp(argv[1], "info") == 0) {
        int fd = open(dev, O_RDONLY);
        if (fd < 0) { perror("open"); return 1; }
        char name[256] = "Unknown";
        ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        printf("Device Name: %s\n", name);
        
        int codes[] = {ABS_X, ABS_Y, ABS_MT_POSITION_X, ABS_MT_POSITION_Y, ABS_MT_TRACKING_ID, ABS_MT_SLOT, ABS_MT_PRESSURE, ABS_MT_TOUCH_MAJOR};
        const char *names[] = {"ABS_X", "ABS_Y", "ABS_MT_POSITION_X", "ABS_MT_POSITION_Y", "ABS_MT_TRACKING_ID", "ABS_MT_SLOT", "ABS_MT_PRESSURE", "ABS_MT_TOUCH_MAJOR"};
        for (int i = 0; i < 8; i++) {
            struct input_absinfo abs;
            if (ioctl(fd, EVIOCGABS(codes[i]), &abs) >= 0) {
                printf("%s: min=%d max=%d fuzz=%d flat=%d res=%d\n", names[i], abs.minimum, abs.maximum, abs.fuzz, abs.flat, abs.resolution);
            }
        }
        close(fd);
        return 0;
    }

    if (argc > 1 && strcmp(argv[1], "monitor") == 0) {
        int fd = open(dev, O_RDONLY);
        if (fd < 0) { perror("open"); return 1; }
        printf("Monitoreando eventos en %s (Ctrl+C para salir)...\n", dev);
        struct input_event ev;
        while (read(fd, &ev, sizeof(ev)) > 0) {
            printf("time=%ld.%06ld type=0x%02x code=0x%04x val=%d\n",
                   ev.time.tv_sec, ev.time.tv_usec, ev.type, ev.code, ev.value);
            fflush(stdout);
        }
        close(fd);
        return 0;
    }

    if (argc > 1 && strcmp(argv[1], "swipe_up") == 0) {
        int fd = open(dev, O_RDWR);
        if (fd < 0) { perror("open"); return 1; }
        printf("Inyectando swipe hacia arriba (desbloqueo Phosh: 600,1700 -> 600,400)...\n");
        int tracking_id = 100;
        int x = 600;
        int start_y = 1700;
        int end_y = 400;
        int steps = 25;

        // Touch DOWN
        emit(fd, EV_ABS, ABS_MT_SLOT, 0);
        emit(fd, EV_ABS, ABS_MT_TRACKING_ID, tracking_id);
        emit(fd, EV_ABS, ABS_MT_POSITION_X, x);
        emit(fd, EV_ABS, ABS_MT_POSITION_Y, start_y);
        emit(fd, EV_ABS, ABS_MT_PRESSURE, 50);
        emit(fd, EV_KEY, BTN_TOUCH, 1);
        emit(fd, EV_SYN, SYN_REPORT, 0);
        usleep(20000);

        // Move
        for (int i = 1; i <= steps; i++) {
            int y = start_y + (end_y - start_y) * i / steps;
            emit(fd, EV_ABS, ABS_MT_POSITION_X, x);
            emit(fd, EV_ABS, ABS_MT_POSITION_Y, y);
            emit(fd, EV_ABS, ABS_MT_PRESSURE, 50);
            emit(fd, EV_SYN, SYN_REPORT, 0);
            usleep(15000); // 15ms por paso
        }

        // Touch UP
        emit(fd, EV_ABS, ABS_MT_TRACKING_ID, -1);
        emit(fd, EV_KEY, BTN_TOUCH, 0);
        emit(fd, EV_SYN, SYN_REPORT, 0);

        close(fd);
        printf("Swipe completado con éxito.\n");
        return 0;
    }

    if (argc > 1 && strcmp(argv[1], "tap") == 0) {
        int x = 600, y = 960;
        if (argc >= 4) {
            x = atoi(argv[2]);
            y = atoi(argv[3]);
        }
        int fd = open(dev, O_RDWR);
        if (fd < 0) { perror("open"); return 1; }
        printf("Inyectando tap en (%d, %d)...\n", x, y);

        // Touch DOWN
        emit(fd, EV_ABS, ABS_MT_SLOT, 0);
        emit(fd, EV_ABS, ABS_MT_TRACKING_ID, 101);
        emit(fd, EV_ABS, ABS_MT_POSITION_X, x);
        emit(fd, EV_ABS, ABS_MT_POSITION_Y, y);
        emit(fd, EV_ABS, ABS_MT_PRESSURE, 50);
        emit(fd, EV_KEY, BTN_TOUCH, 1);
        emit(fd, EV_SYN, SYN_REPORT, 0);
        usleep(50000);

        // Touch UP
        emit(fd, EV_ABS, ABS_MT_TRACKING_ID, -1);
        emit(fd, EV_KEY, BTN_TOUCH, 0);
        emit(fd, EV_SYN, SYN_REPORT, 0);

        close(fd);
        printf("Tap completado.\n");
        return 0;
    }

    printf("Uso: %s [info|monitor|swipe_up|tap [x y]]\n", argv[0]);
    return 0;
}
