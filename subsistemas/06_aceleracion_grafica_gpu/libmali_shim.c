#define _GNU_SOURCE
#include <fcntl.h>
#include <stdarg.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <locale.h>
#include <stdint.h>
#include <dlfcn.h>

/**
 * libmali_shim.c - Puente de compatibilidad ABI glibc <-> musl libc y traducción de ioctl
 * para el driver ARM Mali Valhall en Linux postmarketOS Edge (aarch64).
 */

int fcntl64(int fd, int cmd, ...) {
    va_list ap;
    va_start(ap, cmd);
    void *arg = va_arg(ap, void *);
    va_end(ap);
    return fcntl(fd, cmd, arg);
}

int __register_atfork(void (*prepare)(void), void (*parent)(void),
                      void (*child)(void), void *dso_handle) {
    return pthread_atfork(prepare, parent, child);
}


struct mallinfo { int a; };
struct mallinfo mallinfo(void) { struct mallinfo m = {0}; return m; }
struct mallinfo2 { size_t a; };
struct mallinfo2 mallinfo2(void) { struct mallinfo2 m = {0}; return m; }

long strtol_l(const char *nptr, char **endptr, int base, locale_t loc) { return strtol(nptr, endptr, base); }
long long strtoll_l(const char *nptr, char **endptr, int base, locale_t loc) { return strtoll(nptr, endptr, base); }
unsigned long strtoul_l(const char *nptr, char **endptr, int base, locale_t loc) { return strtoul(nptr, endptr, base); }
unsigned long long strtoull_l(const char *nptr, char **endptr, int base, locale_t loc) { return strtoull(nptr, endptr, base); }
double strtod_l(const char *nptr, char **endptr, locale_t loc) { return strtod(nptr, endptr); }
float strtof_l(const char *nptr, char **endptr, locale_t loc) { return strtof(nptr, endptr); }

struct kbase_ioctl_version_check {
    unsigned short major;
    unsigned short minor;
};

#ifdef __GLIBC__
int ioctl(int fd, unsigned long request, ...) {
#else
int ioctl(int fd, int request, ...) {
#endif
    va_list ap;
    va_start(ap, request);
    void *arg = va_arg(ap, void *);
    va_end(ap);

    unsigned int req = (unsigned int)request;

    // KBASE_IOCTL_VERSION_CHECK de libmali userspace = 0xc0048034
    // El kernel MediaTek MT6789 espera 0xc0048000 con kver = { 11, 38 }
    if (req == 0xc0048034) {
        struct kbase_ioctl_version_check kver = { 11, 38 };
        long ret = syscall(SYS_ioctl, fd, 0xc0048000, &kver);
        (void)ret;
        if (arg) {
            struct kbase_ioctl_version_check *v = (struct kbase_ioctl_version_check *)arg;
            v->major = 1;
            v->minor = 0;
        }
        return 0;
    }

    // KBASE_IOCTL_SET_FLAGS de libmali userspace = 0x40108002 (_IOW(0x80, 2, 16 bytes))
    // El kernel MediaTek MT6789 espera 0x40048001 (_IOW(0x80, 1, 4 bytes)) con máscara 0x7a
    if (req == 0x40108002) {
        if (!arg) return 0;
        uint32_t flags = *(uint32_t *)arg;
        uint32_t safe_flags = flags & 0x7a;
        syscall(SYS_ioctl, fd, 0x40048001, &safe_flags);
        return 0;
    }


    return syscall(SYS_ioctl, fd, request, arg);
}


