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
#include <string.h>
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

/* ========================================================================= */
/* INTERCEPCIÓN PTHREAD_CREATE: STACK SIZE DE 4 MB PARA SHADERS MALI        */
/* Evita desbordamiento de pila en musl libc (128 KB por defecto)           */
/* ========================================================================= */

typedef int (*real_pthread_create_fn)(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
static real_pthread_create_fn g_real_pthread_create = NULL;

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg)
{
    if (!g_real_pthread_create) {
        g_real_pthread_create = (real_pthread_create_fn)dlsym(RTLD_NEXT, "pthread_create");
    }

    pthread_attr_t custom_attr;
    int destroy_attr = 0;
    const pthread_attr_t *effective_attr = attr;

    size_t stacksize = 0;
    if (attr) {
        pthread_attr_getstacksize(attr, &stacksize);
    }

    if (!attr || stacksize < 4 * 1024 * 1024) {
        pthread_attr_init(&custom_attr);
        if (attr) {
            int detachstate = 0;
            if (pthread_attr_getdetachstate(attr, &detachstate) == 0) {
                pthread_attr_setdetachstate(&custom_attr, detachstate);
            }
            size_t guardsize = 0;
            if (pthread_attr_getguardsize(attr, &guardsize) == 0) {
                pthread_attr_setguardsize(&custom_attr, guardsize);
            }
        }
        pthread_attr_setstacksize(&custom_attr, 4 * 1024 * 1024);
        effective_attr = &custom_attr;
        destroy_attr = 1;
    }

    int ret = g_real_pthread_create ? g_real_pthread_create(thread, effective_attr, start_routine, arg)
                                    : 38 /* ENOSYS */;

    if (destroy_attr) {
        pthread_attr_destroy(&custom_attr);
    }

    return ret;
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

    if (req == 0x40108002) {
        if (!arg) return -1;
        struct kbase_ioctl_job_submit {
            uint64_t addr;
            uint32_t nr_atoms;
            uint32_t stride;
        } *sub = (void *)arg;

        if (sub->stride == 0x40 && sub->nr_atoms > 0 && sub->nr_atoms <= 256) {
            uint8_t atoms72[256 * 72];
            const uint8_t *src = (const uint8_t *)(uintptr_t)sub->addr;
            memset(atoms72, 0, sub->nr_atoms * 72);
            for (uint32_t i = 0; i < sub->nr_atoms; i++) {
                memcpy(&atoms72[i * 72], &src[i * 64], 64);
            }
            struct kbase_ioctl_job_submit sub72 = {
                .addr = (uint64_t)(uintptr_t)atoms72,
                .nr_atoms = sub->nr_atoms,
                .stride = 0x48
            };
            return syscall(SYS_ioctl, fd, 0x40108002, &sub72);
        }
        return syscall(SYS_ioctl, fd, 0x40108002, arg);
    }

    if (req == 0x40048001) {
        uint32_t flags = arg ? *(uint32_t *)arg : 0;
        uint32_t safe_flags = flags & 0x7a;
        long ret = syscall(SYS_ioctl, fd, 0x40048001, &safe_flags);
        if (ret < 0) return 0;
        return ret;
    }

    return syscall(SYS_ioctl, fd, request, arg);
}

/* ========================================================================= */
/* INTERCEPTACIÓN GBM: Forzado de Modificador LINEAR (0) en Scanout           */
/* Evita fallos de hardware en el display controller mediatek_drm por AFBC.   */
/* ========================================================================= */

struct gbm_device;
struct gbm_bo;
struct gbm_surface;

#define GBM_BO_USE_SCANOUT (1 << 0)
#define GBM_BO_USE_RENDERING (1 << 2)

struct gbm_bo *gbm_bo_create(struct gbm_device *gbm,
                             uint32_t width, uint32_t height,
                             uint32_t format, uint32_t flags)
{
    static struct gbm_bo *(*real_gbm_bo_create)(struct gbm_device *, uint32_t, uint32_t, uint32_t, uint32_t) = NULL;
    static struct gbm_bo *(*real_gbm_bo_create_with_modifiers2)(struct gbm_device *, uint32_t, uint32_t, uint32_t, const uint64_t *, const unsigned int, uint32_t) = NULL;

    if (!real_gbm_bo_create) {
        real_gbm_bo_create = dlsym(RTLD_NEXT, "gbm_bo_create");
    }
    if (!real_gbm_bo_create_with_modifiers2) {
        real_gbm_bo_create_with_modifiers2 = dlsym(RTLD_NEXT, "gbm_bo_create_with_modifiers2");
    }

    if ((flags & GBM_BO_USE_SCANOUT) && real_gbm_bo_create_with_modifiers2) {
        const uint64_t linear_mod = 0; // DRM_FORMAT_MOD_LINEAR
        struct gbm_bo *bo = real_gbm_bo_create_with_modifiers2(gbm, width, height, format, &linear_mod, 1, flags);
        if (bo) return bo;
    }

    if (real_gbm_bo_create) {
        return real_gbm_bo_create(gbm, width, height, format, flags);
    }
    return NULL;
}

struct gbm_bo *gbm_bo_create_with_modifiers2(struct gbm_device *gbm,
                                            uint32_t width, uint32_t height,
                                            uint32_t format,
                                            const uint64_t *modifiers,
                                            const unsigned int count,
                                            uint32_t flags)
{
    static struct gbm_bo *(*real_gbm_bo_create_with_modifiers2)(struct gbm_device *, uint32_t, uint32_t, uint32_t, const uint64_t *, const unsigned int, uint32_t) = NULL;
    if (!real_gbm_bo_create_with_modifiers2) {
        real_gbm_bo_create_with_modifiers2 = dlsym(RTLD_NEXT, "gbm_bo_create_with_modifiers2");
    }

    if ((flags & GBM_BO_USE_SCANOUT) && real_gbm_bo_create_with_modifiers2) {
        const uint64_t linear_mod = 0; // DRM_FORMAT_MOD_LINEAR
        return real_gbm_bo_create_with_modifiers2(gbm, width, height, format, &linear_mod, 1, flags);
    }

    if (real_gbm_bo_create_with_modifiers2) {
        return real_gbm_bo_create_with_modifiers2(gbm, width, height, format, modifiers, count, flags);
    }
    return NULL;
}

struct gbm_surface *gbm_surface_create(struct gbm_device *gbm,
                                       uint32_t width, uint32_t height,
                                       uint32_t format, uint32_t flags)
{
    static struct gbm_surface *(*real_gbm_surface_create)(struct gbm_device *, uint32_t, uint32_t, uint32_t, uint32_t) = NULL;
    static struct gbm_surface *(*real_gbm_surface_create_with_modifiers2)(struct gbm_device *, uint32_t, uint32_t, uint32_t, const uint64_t *, const unsigned int, uint32_t) = NULL;

    if (!real_gbm_surface_create) {
        real_gbm_surface_create = dlsym(RTLD_NEXT, "gbm_surface_create");
    }
    if (!real_gbm_surface_create_with_modifiers2) {
        real_gbm_surface_create_with_modifiers2 = dlsym(RTLD_NEXT, "gbm_surface_create_with_modifiers2");
    }

    if ((flags & GBM_BO_USE_SCANOUT) && real_gbm_surface_create_with_modifiers2) {
        const uint64_t linear_mod = 0; // DRM_FORMAT_MOD_LINEAR
        struct gbm_surface *surf = real_gbm_surface_create_with_modifiers2(gbm, width, height, format, &linear_mod, 1, flags);
        if (surf) return surf;
    }

    if (real_gbm_surface_create) {
        return real_gbm_surface_create(gbm, width, height, format, flags);
    }
    return NULL;
}

struct gbm_surface *gbm_surface_create_with_modifiers2(struct gbm_device *gbm,
                                                      uint32_t width, uint32_t height,
                                                      uint32_t format,
                                                      const uint64_t *modifiers,
                                                      const unsigned int count,
                                                      uint32_t flags)
{
    static struct gbm_surface *(*real_gbm_surface_create_with_modifiers2)(struct gbm_device *, uint32_t, uint32_t, uint32_t, const uint64_t *, const unsigned int, uint32_t) = NULL;
    if (!real_gbm_surface_create_with_modifiers2) {
        real_gbm_surface_create_with_modifiers2 = dlsym(RTLD_NEXT, "gbm_surface_create_with_modifiers2");
    }

    if ((flags & GBM_BO_USE_SCANOUT) && real_gbm_surface_create_with_modifiers2) {
        const uint64_t linear_mod = 0; // DRM_FORMAT_MOD_LINEAR
        return real_gbm_surface_create_with_modifiers2(gbm, width, height, format, &linear_mod, 1, flags);
    }

    if (real_gbm_surface_create_with_modifiers2) {
        return real_gbm_surface_create_with_modifiers2(gbm, width, height, format, modifiers, count, flags);
    }
    return NULL;
}


