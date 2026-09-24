/*
 * libseat_direct.c - Minimal, rock-solid libseat implementation for wlroots/phoc on Android kernels.
 * Zero libc dependencies, zero dlsym, pure AArch64 syscalls.
 */

#define _GNU_SOURCE
#include <unistd.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/types.h>

#ifndef __NR_openat
#define __NR_openat 56
#endif
#ifndef __NR_close
#define __NR_close 57
#endif
#ifndef __NR_write
#define __NR_write 64
#endif
#ifndef __NR_eventfd2
#define __NR_eventfd2 19
#endif

static inline long my_sys_close(int fd) {
    register long x8 __asm__("x8") = __NR_close;
    register long x0 __asm__("x0") = fd;
    __asm__ __volatile__("svc #0" : "=r"(x0) : "r"(x8), "0"(x0) : "memory");
    return x0;
}

static inline long my_sys_write(int fd, const void *buf, size_t count) {
    register long x8 __asm__("x8") = __NR_write;
    register long x0 __asm__("x0") = fd;
    register long x1 __asm__("x1") = (long)buf;
    register long x2 __asm__("x2") = count;
    __asm__ __volatile__("svc #0" : "=r"(x0) : "r"(x8), "0"(x0), "r"(x1), "r"(x2) : "memory");
    return x0;
}

static inline long my_sys_openat(int dirfd, const char *pathname, int flags) {
    register long x8 __asm__("x8") = __NR_openat;
    register long x0 __asm__("x0") = dirfd;
    register long x1 __asm__("x1") = (long)pathname;
    register long x2 __asm__("x2") = flags;
    register long x3 __asm__("x3") = 0;
    __asm__ __volatile__("svc #0" : "=r"(x0) : "r"(x8), "0"(x0), "r"(x1), "r"(x2), "r"(x3) : "memory");
    return x0;
}

static inline long my_sys_eventfd2(unsigned int initval, int flags) {
    register long x8 __asm__("x8") = __NR_eventfd2;
    register long x0 __asm__("x0") = initval;
    register long x1 __asm__("x1") = flags;
    __asm__ __volatile__("svc #0" : "=r"(x0) : "r"(x8), "0"(x0), "r"(x1) : "memory");
    return x0;
}

#ifndef EFD_NONBLOCK
#define EFD_NONBLOCK 00004000
#endif
#ifndef EFD_CLOEXEC
#define EFD_CLOEXEC 02000000
#endif

static size_t my_strlen(const char *s) {
    size_t len = 0;
    if (!s) return 0;
    while (s[len]) len++;
    return len;
}

static void log_msg(const char *msg) {
    if (msg) {
        my_sys_write(2, msg, my_strlen(msg));
    }
}

struct libseat_seat_listener {
    void (*enable_seat)(void *seat, void *data);
    void (*disable_seat)(void *seat, void *data);
};

struct libseat {
    const struct libseat_seat_listener *listener;
    void *userdata;
    int ev_fd;
};

static struct libseat static_seat_instance = {0, 0, -1};

void *libseat_open_seat(const struct libseat_seat_listener *listener, void *data) {
    log_msg("[libseat-direct] libseat_open_seat() inicializado con exito.\n");
    static_seat_instance.listener = listener;
    static_seat_instance.userdata = data;
    static_seat_instance.ev_fd = (int)my_sys_eventfd2(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (listener && listener->enable_seat) {
        log_msg("[libseat-direct] Disparando callback enable_seat().\n");
        listener->enable_seat(&static_seat_instance, data);
    }
    return &static_seat_instance;
}

const char *libseat_seat_name(void *seat) {
    return "seat0";
}

int libseat_open_device(void *seat, const char *path, int *fd_out) {
    if (!path || !fd_out) return -1;
    int fd = (int)my_sys_openat(-100 /* AT_FDCWD */, path, O_RDWR | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        log_msg("[libseat-direct] Error abriendo device: ");
        log_msg(path);
        log_msg("\n");
        return -1;
    }
    *fd_out = fd;
    log_msg("[libseat-direct] Device abierto OK: ");
    log_msg(path);
    log_msg("\n");
    return fd;
}

int libseat_close_device(void *seat, int device_id) {
    if (device_id >= 0) {
        my_sys_close(device_id);
    }
    return 0;
}

int libseat_get_fd(void *seat) {
    struct libseat *s = (struct libseat *)seat;
    return s ? s->ev_fd : -1;
}

int libseat_dispatch(void *seat, int timeout) {
    return 0;
}

int libseat_switch_session(void *seat, int session) {
    return 0;
}

int libseat_close_seat(void *seat) {
    struct libseat *s = (struct libseat *)seat;
    if (s && s->ev_fd >= 0) {
        my_sys_close(s->ev_fd);
        s->ev_fd = -1;
    }
    return 0;
}

int libseat_disable_seat(void *seat) {
    return 0;
}

void libseat_set_log_handler(void *handler) {}
void libseat_set_log_level(int level) {}
