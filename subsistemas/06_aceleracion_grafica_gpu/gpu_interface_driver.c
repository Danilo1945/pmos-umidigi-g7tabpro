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
#include <string.h>
#include <errno.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <semaphore.h>
#include <time.h>


__attribute__((visibility("default")))
int drmIoctl(int fd, unsigned long request, void *arg) {
    return ioctl(fd, request, arg);
}

__attribute__((visibility("default")))
int drmPrimeHandleToFD(int fd, uint32_t handle, uint32_t flags, int *prime_fd) {
    struct drm_prime_handle args;
    memset(&args, 0, sizeof(args));
    args.handle = handle;
    args.flags = flags;
    int ret = ioctl(fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &args);
    if (ret)
        return ret;
    if (prime_fd)
        *prime_fd = args.fd;
    return 0;
}



/* Intercepción selectiva de funciones internas osup de Mali */


/**
 * gpu_interface_driver.c - Driver de Interfaz Gráfica para Conexión Wayland/KMS y GPU ARM Mali-G57
 * Plataforma: UMIDIGI G7 Tab Pro (SoC MediaTek MT6789 - Helio G99)
 * Sistema: postmarketOS Edge (Alpine Linux aarch64 musl libc / Kernel 5.10.185)
 *
 * Módulos integrados:
 * 1. Capa GBM extendida (conforme a wlroots/phoc: modificador LINEAR, multi-plane stubs)
 * 2. Puente ABI musl libc <-> glibc para binarios ARM Mali DDK
 * 3. Traductor transparente de ioctls de kernel KBASE MT6789
 * 4. Interceptor de extensiones EGL Wayland (EGL_WL_bind_wayland_display)
 */

#ifndef DRM_FORMAT_MOD_LINEAR
#define DRM_FORMAT_MOD_LINEAR 0ULL
#endif
#define GBM_BO_USE_SCANOUT    (1 << 0)
#define GBM_BO_USE_RENDERING  (1 << 2)

static int g_debug_enabled = -1;

static inline int is_debug_enabled(void) {
    if (g_debug_enabled == -1) {
        const char *env = getenv("GPU_DRIVER_DEBUG");
        g_debug_enabled = (env && env[0] == '1') ? 1 : 0;
    }
    return g_debug_enabled;
}

#define GPU_LOG(fmt, ...) do { \
    if (is_debug_enabled()) { \
        fprintf(stderr, "[gpu-interface-driver] " fmt "\n", ##__VA_ARGS__); \
        fflush(stderr); \
    } \
} while(0)

/* ========================================================================= */
/* 1. PUENTE DE COMPATIBILIDAD ABI GLIBC <-> MUSL LIBC                       */
/* ========================================================================= */

int fcntl64(int fd, int cmd, ...) {
    va_list ap;
    va_start(ap, cmd);
    void *arg = va_arg(ap, void *);
    va_end(ap);
    return fcntl(fd, cmd, arg);
}

int pthread_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(void)) {
    (void)prepare;
    (void)parent;
    (void)child;
    return 0;
}

int __register_atfork(void (*prepare)(void), void (*parent)(void),
                      void (*child)(void), void *dso_handle) {
    (void)prepare;
    (void)parent;
    (void)child;
    (void)dso_handle;
    return 0;
}

int __cxa_atexit(void (*func)(void *), void *arg, void *dso_handle) {
    (void)func;
    (void)arg;
    (void)dso_handle;
    return 0;
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
        GPU_LOG("pthread_create interceptado: asignando stack size de 4 MB a nuevo hilo");
    }

    int ret = g_real_pthread_create ? g_real_pthread_create(thread, effective_attr, start_routine, arg)
                                    : 38 /* ENOSYS */;

    if (destroy_attr) {
        pthread_attr_destroy(&custom_attr);
    }

    return ret;
}

struct mallinfo { int a; };
struct mallinfo mallinfo(void) {
    struct mallinfo m = {0};
    return m;
}

struct mallinfo2 { size_t a; };
struct mallinfo2 mallinfo2(void) {
    struct mallinfo2 m = {0};
    return m;
}

long my_strtol_l(const char *nptr, char **endptr, int base, locale_t loc) __asm__("strtol_l");
long my_strtol_l(const char *nptr, char **endptr, int base, locale_t loc) {
    (void)loc;
    return strtol(nptr, endptr, base);
}

long long my_strtoll_l(const char *nptr, char **endptr, int base, locale_t loc) __asm__("strtoll_l");
long long my_strtoll_l(const char *nptr, char **endptr, int base, locale_t loc) {
    (void)loc;
    return strtoll(nptr, endptr, base);
}

unsigned long my_strtoul_l(const char *nptr, char **endptr, int base, locale_t loc) __asm__("strtoul_l");
unsigned long my_strtoul_l(const char *nptr, char **endptr, int base, locale_t loc) {
    (void)loc;
    return strtoul(nptr, endptr, base);
}

unsigned long long my_strtoull_l(const char *nptr, char **endptr, int base, locale_t loc) __asm__("strtoull_l");
unsigned long long my_strtoull_l(const char *nptr, char **endptr, int base, locale_t loc) {
    (void)loc;
    return strtoull(nptr, endptr, base);
}

double my_strtod_l(const char *nptr, char **endptr, locale_t loc) __asm__("strtod_l");
double my_strtod_l(const char *nptr, char **endptr, locale_t loc) {
    (void)loc;
    return strtod(nptr, endptr);
}

float my_strtof_l(const char *nptr, char **endptr, locale_t loc) __asm__("strtof_l");
float my_strtof_l(const char *nptr, char **endptr, locale_t loc) {
    (void)loc;
    return strtof(nptr, endptr);
}

/* Stubs de compatibilidad ISO C23 para musl libc */
unsigned long __isoc23_strtoul(const char *nptr, char **endptr, int base) {
    return strtoul(nptr, endptr, base);
}
long __isoc23_strtol(const char *nptr, char **endptr, int base) {
    return strtol(nptr, endptr, base);
}
long long __isoc23_strtoll(const char *nptr, char **endptr, int base) {
    return strtoll(nptr, endptr, base);
}
unsigned long long __isoc23_strtoull(const char *nptr, char **endptr, int base) {
    return strtoull(nptr, endptr, base);
}
double __isoc23_strtod(const char *nptr, char **endptr) {
    return strtod(nptr, endptr);
}
float __isoc23_strtof(const char *nptr, char **endptr) {
    return strtof(nptr, endptr);
}

/* Stubs de fortificación glibc para musl libc */
int __vfprintf_chk(FILE *fp, int flag, const char *format, va_list ap) {
    (void)flag;
    return vfprintf(fp, format, ap);
}

int __vsnprintf_chk(char *str, size_t maxlen, int flag, size_t slen, const char *format, va_list ap) {
    (void)flag;
    (void)slen;
    return vsnprintf(str, maxlen, format, ap);
}

int __vsprintf_chk(char *str, int flag, size_t slen, const char *format, va_list ap) {
    (void)flag;
    (void)slen;
    return vsprintf(str, format, ap);
}

int __fprintf_chk(FILE *fp, int flag, const char *format, ...) {
    (void)flag;
    va_list ap;
    va_start(ap, format);
    int ret = vfprintf(fp, format, ap);
    va_end(ap);
    return ret;
}

int __printf_chk(int flag, const char *format, ...) {
    (void)flag;
    va_list ap;
    va_start(ap, format);
    int ret = vprintf(format, ap);
    va_end(ap);
    return ret;
}

int __snprintf_chk(char *str, size_t maxlen, int flag, size_t slen, const char *format, ...) {
    (void)flag;
    (void)slen;
    va_list ap;
    va_start(ap, format);
    int ret = vsnprintf(str, maxlen, format, ap);
    va_end(ap);
    return ret;
}

int __sprintf_chk(char *str, int flag, size_t slen, const char *format, ...) {
    (void)flag;
    (void)slen;
    va_list ap;
    va_start(ap, format);
    int ret = vsprintf(str, format, ap);
    va_end(ap);
    return ret;
}

/* ========================================================================= */
/* 2. TRADUCCIÓN DE LLAMADAS IOCTL KBASE (KERNEL MEDIATEK MT6789)            */
/* ========================================================================= */

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

    /*
     * KBASE_IOCTL_VERSION_CHECK de libmali userspace = 0xc0048034
     * El kernel MediaTek MT6789 espera 0xc0048000 con kver = { 11, 38 }
     */
    if (req == 0xc0048034) {
        struct kbase_ioctl_version_check kver = { 11, 38 };
        long ret = syscall(SYS_ioctl, fd, 0xc0048000, &kver);
        (void)ret;
        if (arg) {
            struct kbase_ioctl_version_check *v = (struct kbase_ioctl_version_check *)arg;
            v->major = 1;
            v->minor = 0;
        }
        GPU_LOG("ioctl VERSION_CHECK traducido: kver={11, 38} -> OK");
        return 0;
    }

    /*
     * KBASE_IOCTL_SET_FLAGS de libmali userspace = 0x40108002 (_IOW(0x80, 2, 16 bytes))
     * El kernel MediaTek MT6789 espera 0x40048001 (_IOW(0x80, 1, 4 bytes)) con mascara 0x7a
     */
    if (req == 0x40108002) {
        if (!arg) return 0;
        uint32_t flags = *(uint32_t *)arg;
        uint32_t safe_flags = flags & 0x7a;
        syscall(SYS_ioctl, fd, 0x40048001, &safe_flags);
        GPU_LOG("ioctl SET_FLAGS traducido: flags=0x%x -> safe=0x%x", flags, safe_flags);
        return 0;
    }

    /* Si el driver invoca directamente 0x40048001 y falla por flags ya fijados, ignorar */
    if (req == 0x40048001) {
        long ret = syscall(SYS_ioctl, fd, 0x40048001, arg);
        if (ret < 0) return 0;
        return ret;
    }

    return syscall(SYS_ioctl, fd, request, arg);
}

/* ========================================================================= */
/* 3. CAPA GBM ADAPTADA PARA COMPOSITORES WAYLAND (WLROOTS / PHOC)           */
/* ========================================================================= */

#ifndef GBM_BO_IMPORT_WL_BUFFER
#define GBM_BO_IMPORT_WL_BUFFER   0x5501
#endif
#ifndef GBM_BO_IMPORT_EGL_IMAGE
#define GBM_BO_IMPORT_EGL_IMAGE   0x5502
#endif
#ifndef GBM_BO_IMPORT_FD
#define GBM_BO_IMPORT_FD          0x5503
#endif
#ifndef GBM_BO_IMPORT_FD_MODIFIER
#define GBM_BO_IMPORT_FD_MODIFIER 0x5504
#endif

struct gbm_import_fd_modifier_data_local {
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t num_fds;
    int fds[4];
    int strides[4];
    int offsets[4];
    uint64_t modifier;
};

#define MAX_DUMB_ENTRIES 64

struct gbm_device;
struct gbm_bo;
struct gbm_surface;
union gbm_bo_handle {
    void *ptr;
    int32_t s32;
    uint32_t u32;
    int64_t s64;
    uint64_t u64;
};

struct gbm_import_fd_data {
    int fd;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
};

struct dumb_bo_entry {
    struct gbm_bo *bo;
    uint32_t dumb_handle;
    int card_fd;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
    uint64_t size;
};

static struct dumb_bo_entry g_dumb_table[MAX_DUMB_ENTRIES] = {0};
static pthread_mutex_t g_dumb_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_drm_fd = -1;

/* Punteros a la implementación real de GBM en libmali */
static struct gbm_device *(*real_gbm_create_device)(int) = NULL;
static void (*real_gbm_device_destroy)(struct gbm_device *) = NULL;
static int (*real_gbm_device_get_fd)(struct gbm_device *) = NULL;
static const char *(*real_gbm_device_get_backend_name)(struct gbm_device *) = NULL;
static int (*real_gbm_device_is_format_supported)(struct gbm_device *, uint32_t, uint32_t) = NULL;

static struct gbm_bo *(*real_gbm_bo_create)(struct gbm_device *, uint32_t, uint32_t, uint32_t, uint32_t) = NULL;
static struct gbm_bo *(*real_gbm_bo_create_with_modifiers)(struct gbm_device *, uint32_t, uint32_t, uint32_t, const uint64_t *, const unsigned int) = NULL;
static struct gbm_bo *(*real_gbm_bo_create_with_modifiers2)(struct gbm_device *, uint32_t, uint32_t, uint32_t, const uint64_t *, const unsigned int, uint32_t) = NULL;
static struct gbm_bo *(*real_gbm_bo_import)(struct gbm_device *, uint32_t, void *, uint32_t) = NULL;
static void (*real_gbm_bo_destroy)(struct gbm_bo *) = NULL;

static uint32_t (*real_gbm_bo_get_width)(struct gbm_bo *) = NULL;
static uint32_t (*real_gbm_bo_get_height)(struct gbm_bo *) = NULL;
static uint32_t (*real_gbm_bo_get_stride)(struct gbm_bo *) = NULL;
static uint32_t (*real_gbm_bo_get_format)(struct gbm_bo *) = NULL;
static union gbm_bo_handle (*real_gbm_bo_get_handle)(struct gbm_bo *) = NULL;
static int (*real_gbm_bo_get_fd)(struct gbm_bo *) = NULL;
static uint64_t (*real_gbm_bo_get_modifier)(struct gbm_bo *) = NULL;
static int (*real_gbm_bo_get_plane_count)(struct gbm_bo *) = NULL;
static uint32_t (*real_gbm_bo_get_stride_for_plane)(struct gbm_bo *, int) = NULL;
static uint32_t (*real_gbm_bo_get_offset)(struct gbm_bo *, int) = NULL;
static int (*real_gbm_bo_get_fd_for_plane)(struct gbm_bo *, int) = NULL;
static union gbm_bo_handle (*real_gbm_bo_get_handle_for_plane)(struct gbm_bo *, int) = NULL;

static void *(*real_gbm_bo_get_user_data)(struct gbm_bo *) = NULL;
static void (*real_gbm_bo_set_user_data)(struct gbm_bo *, void *, void (*)(struct gbm_bo *, void *)) = NULL;

static struct gbm_surface *(*real_gbm_surface_create)(struct gbm_device *, uint32_t, uint32_t, uint32_t, uint32_t) = NULL;
static struct gbm_surface *(*real_gbm_surface_create_with_modifiers)(struct gbm_device *, uint32_t, uint32_t, uint32_t, const uint64_t *, const unsigned int) = NULL;
static struct gbm_surface *(*real_gbm_surface_create_with_modifiers2)(struct gbm_device *, uint32_t, uint32_t, uint32_t, const uint64_t *, const unsigned int, uint32_t) = NULL;
static void (*real_gbm_surface_destroy)(struct gbm_surface *) = NULL;
static struct gbm_bo *(*real_gbm_surface_lock_front_buffer)(struct gbm_surface *) = NULL;
static void (*real_gbm_surface_release_buffer)(struct gbm_surface *, struct gbm_bo *) = NULL;

static void register_dumb_bo(struct gbm_bo *bo, uint32_t dumb_handle, int card_fd,
                             uint32_t width, uint32_t height, uint32_t stride,
                             uint32_t format, uint64_t size) {
    pthread_mutex_lock(&g_dumb_mutex);
    for (int i = 0; i < MAX_DUMB_ENTRIES; i++) {
        if (g_dumb_table[i].bo == NULL) {
            g_dumb_table[i].bo = bo;
            g_dumb_table[i].dumb_handle = dumb_handle;
            g_dumb_table[i].card_fd = card_fd;
            g_dumb_table[i].width = width;
            g_dumb_table[i].height = height;
            g_dumb_table[i].stride = stride;
            g_dumb_table[i].format = format;
            g_dumb_table[i].size = size;
            pthread_mutex_unlock(&g_dumb_mutex);
            GPU_LOG("Registrado dumb bo %p (slot %d, handle %u, %ux%u, stride %u)",
                    (void*)bo, i, dumb_handle, width, height, stride);
            return;
        }
    }
    pthread_mutex_unlock(&g_dumb_mutex);
    GPU_LOG("ADVERTENCIA: g_dumb_table saturada (max %d entradas)", MAX_DUMB_ENTRIES);
}

static struct dumb_bo_entry *find_dumb_bo_locked(struct gbm_bo *bo) {
    if (!bo) return NULL;
    for (int i = 0; i < MAX_DUMB_ENTRIES; i++) {
        if (g_dumb_table[i].bo == bo) {
            return &g_dumb_table[i];
        }
    }
    return NULL;
}

static void unregister_and_destroy_dumb_bo(struct gbm_bo *bo) {
    pthread_mutex_lock(&g_dumb_mutex);
    for (int i = 0; i < MAX_DUMB_ENTRIES; i++) {
        if (g_dumb_table[i].bo == bo) {
            uint32_t dumb_handle = g_dumb_table[i].dumb_handle;
            int card_fd = g_dumb_table[i].card_fd;
            g_dumb_table[i].bo = NULL;
            g_dumb_table[i].dumb_handle = 0;
            g_dumb_table[i].card_fd = -1;
            pthread_mutex_unlock(&g_dumb_mutex);

            if (card_fd >= 0 && dumb_handle > 0) {
                struct drm_mode_destroy_dumb dreq = {0};
                dreq.handle = dumb_handle;
                drmIoctl(card_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
                GPU_LOG("Dumb BO liberado de KMS: handle=%u (card_fd=%d)", dumb_handle, card_fd);
            }
            return;
        }
    }
    pthread_mutex_unlock(&g_dumb_mutex);
}

static int get_device_fd(struct gbm_device *gbm) {
    if (real_gbm_device_get_fd && gbm) {
        int fd = real_gbm_device_get_fd(gbm);
        if (fd >= 0) return fd;
    }
    if (g_drm_fd >= 0) return g_drm_fd;
    return -1;
}

static void init_gbm_symbols(void) {
    static int initialized = 0;
    if (initialized) return;
    initialized = 1;

    void *mali_handle = dlopen("/usr/lib/libmali-bifrost-patched.so", RTLD_LAZY | RTLD_GLOBAL);
    if (!mali_handle) {
        mali_handle = dlopen("/usr/lib/libmali-bifrost.so", RTLD_LAZY | RTLD_GLOBAL);
    }
    if (!mali_handle) {
        mali_handle = dlopen("/usr/lib/libmali.so", RTLD_LAZY | RTLD_GLOBAL);
    }
    if (!mali_handle) {
        mali_handle = dlopen("/usr/lib/libgbm.so.1", RTLD_LAZY | RTLD_GLOBAL);
    }

    #define BIND_GBM(sym) do { \
        if (mali_handle) { \
            real_##sym = dlsym(mali_handle, #sym); \
        } \
        if (!real_##sym) { \
            real_##sym = dlsym(RTLD_NEXT, #sym); \
        } \
    } while (0)

    BIND_GBM(gbm_create_device);
    BIND_GBM(gbm_device_destroy);
    BIND_GBM(gbm_device_get_fd);
    BIND_GBM(gbm_device_get_backend_name);
    BIND_GBM(gbm_device_is_format_supported);

    BIND_GBM(gbm_bo_create);
    BIND_GBM(gbm_bo_create_with_modifiers);
    BIND_GBM(gbm_bo_create_with_modifiers2);
    BIND_GBM(gbm_bo_import);
    BIND_GBM(gbm_bo_destroy);

    BIND_GBM(gbm_bo_get_width);
    BIND_GBM(gbm_bo_get_height);
    BIND_GBM(gbm_bo_get_stride);
    BIND_GBM(gbm_bo_get_format);
    BIND_GBM(gbm_bo_get_handle);
    BIND_GBM(gbm_bo_get_fd);
    BIND_GBM(gbm_bo_get_modifier);
    BIND_GBM(gbm_bo_get_plane_count);
    BIND_GBM(gbm_bo_get_stride_for_plane);
    BIND_GBM(gbm_bo_get_offset);
    BIND_GBM(gbm_bo_get_fd_for_plane);
    BIND_GBM(gbm_bo_get_handle_for_plane);

    BIND_GBM(gbm_bo_get_user_data);
    BIND_GBM(gbm_bo_set_user_data);

    BIND_GBM(gbm_surface_create);
    BIND_GBM(gbm_surface_create_with_modifiers);
    BIND_GBM(gbm_surface_create_with_modifiers2);
    BIND_GBM(gbm_surface_destroy);
    BIND_GBM(gbm_surface_lock_front_buffer);
    BIND_GBM(gbm_surface_release_buffer);
    #undef BIND_GBM

    GPU_LOG("Símbolos GBM resueltos: gbm_bo_create=%p, gbm_bo_import=%p, real_gbm_device_get_fd=%p",
            (void*)real_gbm_bo_create, (void*)real_gbm_bo_import, (void*)real_gbm_device_get_fd);
}

static struct gbm_bo *create_scanout_dumb_bo(struct gbm_device *gbm,
                                             uint32_t width, uint32_t height,
                                             uint32_t format, uint32_t flags)
{
    int card_fd = get_device_fd(gbm);
    GPU_LOG("create_scanout_dumb_bo: gbm=%p, card_fd=%d", (void*)gbm, card_fd);
    if (card_fd < 0) {
        GPU_LOG("ERROR: No se detectó descriptor de card0 para crear dumb buffer");
        return NULL;
    }

    struct drm_mode_create_dumb creq = {0};
    creq.width = width;
    creq.height = height;
    if (format == 0x36314752) { /* DRM_FORMAT_RGB565 */
        creq.bpp = 16;
    } else {
        creq.bpp = 32; /* DRM_FORMAT_XRGB8888, ARGB8888, etc. */
    }

    int ret = drmIoctl(card_fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq);
    if (ret != 0 || creq.handle == 0) {
        GPU_LOG("ERROR: DRM_IOCTL_MODE_CREATE_DUMB falló (ret=%d, errno=%d)", ret, errno);
        return NULL;
    }

    int prime_fd = -1;
    ret = drmPrimeHandleToFD(card_fd, creq.handle, DRM_CLOEXEC | DRM_RDWR, &prime_fd);
    if (ret != 0 || prime_fd < 0) {
        GPU_LOG("ERROR: drmPrimeHandleToFD falló (ret=%d, errno=%d)", ret, errno);
        struct drm_mode_destroy_dumb dreq = {0};
        dreq.handle = creq.handle;
        drmIoctl(card_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
        return NULL;
    }

    struct gbm_import_fd_modifier_data_local mod_data = {
        .width = width,
        .height = height,
        .format = format,
        .num_fds = 1,
        .fds = { prime_fd, -1, -1, -1 },
        .strides = { (int)creq.pitch, 0, 0, 0 },
        .offsets = { 0, 0, 0, 0 },
        .modifier = DRM_FORMAT_MOD_LINEAR
    };

    GPU_LOG("Importando dumb buffer a Mali via 0x5504 (MODIFIER LINEAR): prime_fd=%d, %ux%u, stride=%u, format=0x%x, flags=0x%x",
            prime_fd, width, height, creq.pitch, format, flags);
    struct gbm_bo *bo = NULL;
    if (real_gbm_bo_import) {
        bo = real_gbm_bo_import(gbm, GBM_BO_IMPORT_FD_MODIFIER, &mod_data, flags | GBM_BO_USE_RENDERING);
    }
    close(prime_fd);

    if (!bo) {
        GPU_LOG("ERROR: real_gbm_bo_import falló al importar dumb buffer hacia Mali (errno=%d)", errno);
        struct drm_mode_destroy_dumb dreq = {0};
        dreq.handle = creq.handle;
        drmIoctl(card_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
        return NULL;
    }

    register_dumb_bo(bo, creq.handle, card_fd, width, height, creq.pitch, format, creq.size);
    GPU_LOG("Dumb BO creado exitosamente: bo=%p, handle=%u, pitch=%u, size=%llu",
            (void*)bo, creq.handle, creq.pitch, (unsigned long long)creq.size);

    return bo;
}

struct gbm_device *gbm_create_device(int fd) {
    init_gbm_symbols();
    GPU_LOG("gbm_create_device(fd=%d)", fd);
    if (fd >= 0) {
        g_drm_fd = fd;
    }
    if (real_gbm_create_device) {
        return real_gbm_create_device(fd);
    }
    return NULL;
}

void gbm_device_destroy(struct gbm_device *gbm) {
    init_gbm_symbols();
    if (real_gbm_device_destroy) {
        real_gbm_device_destroy(gbm);
    }
}

int gbm_device_get_fd(struct gbm_device *gbm) {
    init_gbm_symbols();
    if (g_drm_fd >= 0) return g_drm_fd;
    if (real_gbm_device_get_fd && gbm) return real_gbm_device_get_fd(gbm);
    return -1;
}

const char *gbm_device_get_backend_name(struct gbm_device *gbm) {
    init_gbm_symbols();
    if (real_gbm_device_get_backend_name && gbm) {
        return real_gbm_device_get_backend_name(gbm);
    }
    return "drm";
}

int gbm_device_is_format_supported(struct gbm_device *gbm, uint32_t format, uint32_t usage) {
    init_gbm_symbols();
    if (format == 0x34325258 || format == 0x34325241 ||
        format == 0x34324258 || format == 0x34324241 ||
        format == 0x36314752) {
        return 1;
    }
    if (real_gbm_device_is_format_supported && gbm) {
        return real_gbm_device_is_format_supported(gbm, format, usage);
    }
    return 1;
}

struct gbm_bo *gbm_bo_create(struct gbm_device *gbm,
                             uint32_t width, uint32_t height,
                             uint32_t format, uint32_t flags)
{
    init_gbm_symbols();
    GPU_LOG("gbm_bo_create(%ux%u, format=0x%x, flags=0x%x)", width, height, format, flags);

    if (real_gbm_bo_create_with_modifiers2) {
        const uint64_t linear_mod = DRM_FORMAT_MOD_LINEAR;
        struct gbm_bo *bo = real_gbm_bo_create_with_modifiers2(gbm, width, height, format, &linear_mod, 1, flags);
        if (bo) return bo;
    }

    if (real_gbm_bo_create) {
        return real_gbm_bo_create(gbm, width, height, format, flags);
    }
    return NULL;
}

struct gbm_bo *gbm_bo_create_with_modifiers(struct gbm_device *gbm,
                                            uint32_t width, uint32_t height,
                                            uint32_t format,
                                            const uint64_t *modifiers,
                                            const unsigned int count)
{
    init_gbm_symbols();
    GPU_LOG("gbm_bo_create_with_modifiers(%ux%u, format=0x%x, count=%u)", width, height, format, count);

    const uint64_t linear_mod = DRM_FORMAT_MOD_LINEAR;
    if (real_gbm_bo_create_with_modifiers2) {
        return real_gbm_bo_create_with_modifiers2(gbm, width, height, format, &linear_mod, 1, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    }
    if (real_gbm_bo_create_with_modifiers) {
        return real_gbm_bo_create_with_modifiers(gbm, width, height, format, &linear_mod, 1);
    }
    if (real_gbm_bo_create) {
        return real_gbm_bo_create(gbm, width, height, format, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
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
    init_gbm_symbols();
    GPU_LOG("gbm_bo_create_with_modifiers2(%ux%u, format=0x%x, count=%u, flags=0x%x)", width, height, format, count, flags);

    const uint64_t linear_mod = DRM_FORMAT_MOD_LINEAR;
    if (real_gbm_bo_create_with_modifiers2) {
        return real_gbm_bo_create_with_modifiers2(gbm, width, height, format, &linear_mod, 1, flags);
    }
    if (real_gbm_bo_create) {
        return real_gbm_bo_create(gbm, width, height, format, flags);
    }
    return NULL;
}

struct gbm_bo *gbm_bo_import(struct gbm_device *gbm, uint32_t type,
                             void *buffer, uint32_t usage)
{
    init_gbm_symbols();
    GPU_LOG("gbm_bo_import(gbm=%p, type=0x%x, usage=0x%x)", (void*)gbm, type, usage);
    if (!real_gbm_bo_import) return NULL;

    /* Si el llamador (wlroots/phoc o app externa) usa GBM_BO_IMPORT_FD (0x5503) */
    if (type == GBM_BO_IMPORT_FD) {
        struct gbm_import_fd_data *fd_data = (struct gbm_import_fd_data *)buffer;
        struct gbm_import_fd_modifier_data_local mod_data = {
            .width = fd_data->width,
            .height = fd_data->height,
            .format = fd_data->format,
            .num_fds = 1,
            .fds = { fd_data->fd, -1, -1, -1 },
            .strides = { (int)fd_data->stride, 0, 0, 0 },
            .offsets = { 0, 0, 0, 0 },
            .modifier = DRM_FORMAT_MOD_LINEAR
        };
        GPU_LOG("gbm_bo_import: adaptando GBM_BO_IMPORT_FD (0x5503) a 0x5504 (fd=%d, %ux%u)",
                fd_data->fd, fd_data->width, fd_data->height);
        return real_gbm_bo_import(gbm, GBM_BO_IMPORT_FD_MODIFIER, &mod_data, usage);
    }

    return real_gbm_bo_import(gbm, type, buffer, usage);
}

void gbm_bo_destroy(struct gbm_bo *bo) {
    init_gbm_symbols();
    if (!bo) return;
    GPU_LOG("gbm_bo_destroy(%p)", (void*)bo);
    unregister_and_destroy_dumb_bo(bo);
    if (real_gbm_bo_destroy) {
        real_gbm_bo_destroy(bo);
    }
}

uint32_t gbm_bo_get_width(struct gbm_bo *bo) {
    init_gbm_symbols();
    pthread_mutex_lock(&g_dumb_mutex);
    struct dumb_bo_entry *entry = find_dumb_bo_locked(bo);
    if (entry) {
        uint32_t w = entry->width;
        pthread_mutex_unlock(&g_dumb_mutex);
        return w;
    }
    pthread_mutex_unlock(&g_dumb_mutex);
    if (real_gbm_bo_get_width) return real_gbm_bo_get_width(bo);
    return 0;
}

uint32_t gbm_bo_get_height(struct gbm_bo *bo) {
    init_gbm_symbols();
    pthread_mutex_lock(&g_dumb_mutex);
    struct dumb_bo_entry *entry = find_dumb_bo_locked(bo);
    if (entry) {
        uint32_t h = entry->height;
        pthread_mutex_unlock(&g_dumb_mutex);
        return h;
    }
    pthread_mutex_unlock(&g_dumb_mutex);
    if (real_gbm_bo_get_height) return real_gbm_bo_get_height(bo);
    return 0;
}

uint32_t gbm_bo_get_stride(struct gbm_bo *bo) {
    init_gbm_symbols();
    pthread_mutex_lock(&g_dumb_mutex);
    struct dumb_bo_entry *entry = find_dumb_bo_locked(bo);
    if (entry) {
        uint32_t s = entry->stride;
        pthread_mutex_unlock(&g_dumb_mutex);
        return s;
    }
    pthread_mutex_unlock(&g_dumb_mutex);
    if (real_gbm_bo_get_stride) return real_gbm_bo_get_stride(bo);
    return 0;
}

uint32_t gbm_bo_get_format(struct gbm_bo *bo) {
    init_gbm_symbols();
    pthread_mutex_lock(&g_dumb_mutex);
    struct dumb_bo_entry *entry = find_dumb_bo_locked(bo);
    if (entry) {
        uint32_t f = entry->format;
        pthread_mutex_unlock(&g_dumb_mutex);
        return f;
    }
    pthread_mutex_unlock(&g_dumb_mutex);
    if (real_gbm_bo_get_format) return real_gbm_bo_get_format(bo);
    return 0;
}

union gbm_bo_handle gbm_bo_get_handle(struct gbm_bo *bo) {
    init_gbm_symbols();
    pthread_mutex_lock(&g_dumb_mutex);
    struct dumb_bo_entry *entry = find_dumb_bo_locked(bo);
    if (entry) {
        union gbm_bo_handle h = {0};
        h.u32 = entry->dumb_handle;
        pthread_mutex_unlock(&g_dumb_mutex);
        return h;
    }
    pthread_mutex_unlock(&g_dumb_mutex);
    if (real_gbm_bo_get_handle) return real_gbm_bo_get_handle(bo);
    union gbm_bo_handle h = {0};
    return h;
}

int gbm_bo_get_fd(struct gbm_bo *bo) {
    init_gbm_symbols();
    pthread_mutex_lock(&g_dumb_mutex);
    struct dumb_bo_entry *entry = find_dumb_bo_locked(bo);
    if (entry) {
        int prime_fd = -1;
        int r = drmPrimeHandleToFD(entry->card_fd, entry->dumb_handle, DRM_CLOEXEC | DRM_RDWR, &prime_fd);
        pthread_mutex_unlock(&g_dumb_mutex);
        if (r == 0 && prime_fd >= 0) return prime_fd;
        return -1;
    }
    pthread_mutex_unlock(&g_dumb_mutex);
    if (real_gbm_bo_get_fd) return real_gbm_bo_get_fd(bo);
    return -1;
}

uint64_t gbm_bo_get_modifier(struct gbm_bo *bo) {
    (void)bo;
    return DRM_FORMAT_MOD_LINEAR;
}

int gbm_bo_get_plane_count(struct gbm_bo *bo) {
    (void)bo;
    return 1;
}

uint32_t gbm_bo_get_offset(struct gbm_bo *bo, int plane) {
    (void)bo;
    (void)plane;
    return 0;
}

uint32_t gbm_bo_get_stride_for_plane(struct gbm_bo *bo, int plane) {
    if (plane == 0) return gbm_bo_get_stride(bo);
    return 0;
}

int gbm_bo_get_fd_for_plane(struct gbm_bo *bo, int plane) {
    if (plane == 0) return gbm_bo_get_fd(bo);
    return -1;
}

union gbm_bo_handle gbm_bo_get_handle_for_plane(struct gbm_bo *bo, int plane) {
    if (plane == 0) return gbm_bo_get_handle(bo);
    union gbm_bo_handle h = {0};
    return h;
}

void *gbm_bo_get_user_data(struct gbm_bo *bo) {
    init_gbm_symbols();
    if (real_gbm_bo_get_user_data) {
        return real_gbm_bo_get_user_data(bo);
    }
    return NULL;
}

void gbm_bo_set_user_data(struct gbm_bo *bo, void *data,
                          void (*destroy_user_data)(struct gbm_bo *, void *))
{
    init_gbm_symbols();
    if (real_gbm_bo_set_user_data) {
        real_gbm_bo_set_user_data(bo, data, destroy_user_data);
    }
}

/* Superficies GBM */
struct gbm_surface *gbm_surface_create(struct gbm_device *gbm,
                                       uint32_t width, uint32_t height,
                                       uint32_t format, uint32_t flags)
{
    init_gbm_symbols();
    GPU_LOG("gbm_surface_create(%ux%u, format=0x%x, flags=0x%x)", width, height, format, flags);

    if ((flags & GBM_BO_USE_SCANOUT) && real_gbm_surface_create_with_modifiers2) {
        const uint64_t linear_mod = DRM_FORMAT_MOD_LINEAR;
        struct gbm_surface *surf = real_gbm_surface_create_with_modifiers2(gbm, width, height, format, &linear_mod, 1, flags);
        if (surf) return surf;
    }

    if (real_gbm_surface_create) {
        return real_gbm_surface_create(gbm, width, height, format, flags);
    }
    return NULL;
}

struct gbm_surface *gbm_surface_create_with_modifiers(struct gbm_device *gbm,
                                                      uint32_t width, uint32_t height,
                                                      uint32_t format,
                                                      const uint64_t *modifiers,
                                                      const unsigned int count)
{
    init_gbm_symbols();
    const uint64_t linear_mod = DRM_FORMAT_MOD_LINEAR;
    if (real_gbm_surface_create_with_modifiers2) {
        return real_gbm_surface_create_with_modifiers2(gbm, width, height, format, &linear_mod, 1, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    }
    if (real_gbm_surface_create_with_modifiers) {
        return real_gbm_surface_create_with_modifiers(gbm, width, height, format, &linear_mod, 1);
    }
    if (real_gbm_surface_create) {
        return real_gbm_surface_create(gbm, width, height, format, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
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
    init_gbm_symbols();
    const uint64_t linear_mod = DRM_FORMAT_MOD_LINEAR;
    if (real_gbm_surface_create_with_modifiers2) {
        return real_gbm_surface_create_with_modifiers2(gbm, width, height, format, &linear_mod, 1, flags);
    }
    if (real_gbm_surface_create) {
        return real_gbm_surface_create(gbm, width, height, format, flags);
    }
    return NULL;
}

void gbm_surface_destroy(struct gbm_surface *surf) {
    init_gbm_symbols();
    if (real_gbm_surface_destroy) {
        real_gbm_surface_destroy(surf);
    }
}

struct gbm_bo *gbm_surface_lock_front_buffer(struct gbm_surface *surf) {
    init_gbm_symbols();
    if (real_gbm_surface_lock_front_buffer) {
        return real_gbm_surface_lock_front_buffer(surf);
    }
    return NULL;
}

void gbm_surface_release_buffer(struct gbm_surface *surf, struct gbm_bo *bo) {
    init_gbm_symbols();
    if (real_gbm_surface_release_buffer) {
        real_gbm_surface_release_buffer(surf, bo);
    }
}

/* ========================================================================= */
/* 4. ENLACE DE EXTENSIONES WAYLAND / EGL (EGL_WL_bind_wayland_display)      */
/* ========================================================================= */

typedef void *EGLDisplay;
typedef unsigned int EGLBoolean;
struct wl_display;

static EGLBoolean (*real_eglBindWaylandDisplayWL)(EGLDisplay, struct wl_display *) = NULL;
static EGLBoolean (*real_eglUnbindWaylandDisplayWL)(EGLDisplay, struct wl_display *) = NULL;
static void *(*real_eglGetProcAddress)(const char *) = NULL;

EGLBoolean eglBindWaylandDisplayWL(EGLDisplay dpy, struct wl_display *wl_dpy) {
    if (!real_eglBindWaylandDisplayWL) {
        real_eglBindWaylandDisplayWL = dlsym(RTLD_NEXT, "eglBindWaylandDisplayWL");
    }
    GPU_LOG("eglBindWaylandDisplayWL(dpy=%p, wl_dpy=%p)", dpy, (void*)wl_dpy);
    if (real_eglBindWaylandDisplayWL) {
        return real_eglBindWaylandDisplayWL(dpy, wl_dpy);
    }
    return 1; /* Retornar true como fallback seguro para permitir que Phoc continúe */
}

EGLBoolean eglUnbindWaylandDisplayWL(EGLDisplay dpy, struct wl_display *wl_dpy) {
    if (!real_eglUnbindWaylandDisplayWL) {
        real_eglUnbindWaylandDisplayWL = dlsym(RTLD_NEXT, "eglUnbindWaylandDisplayWL");
    }
    if (real_eglUnbindWaylandDisplayWL) {
        return real_eglUnbindWaylandDisplayWL(dpy, wl_dpy);
    }
    return 1;
}

static void (*real_glFinish)(void) = NULL;
static void (*real_glFlush)(void) = NULL;

void glFlush(void) {
    if (!real_glFlush) {
        real_glFlush = dlsym(RTLD_NEXT, "glFlush");
        if (!real_glFlush) {
            void *mali = dlopen("/usr/lib/libmali-bifrost.so", RTLD_LAZY | RTLD_GLOBAL);
            if (mali) real_glFlush = dlsym(mali, "glFlush");
        }
    }
    if (real_glFlush) real_glFlush();
}

void glFinish(void) {
    /*
     * En Mali DDK userspace, glFinish() puede bloquearse indefinidamente en un futex
     * esperando fences de sincronización de Android (sw_sync/VSYNC) que no existen en Linux/KMS.
     * glFlush() envía todos los comandos a la GPU inmediatamente sin causar stalls.
     */
    glFlush();
}

typedef unsigned int GLenum;
typedef int GLint;
typedef int GLsizei;

static __thread int s_draw_call_counter = 0;
static void (*real_glDrawArrays)(GLenum, GLint, GLsizei) = NULL;
static void (*real_glDrawElements)(GLenum, GLsizei, GLenum, const void *) = NULL;

__attribute__((visibility("default")))
void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    if (!real_glDrawArrays) {
        real_glDrawArrays = dlsym(RTLD_NEXT, "glDrawArrays");
        if (!real_glDrawArrays) {
            void *mali = dlopen("/usr/lib/libmali-bifrost-patched.so", RTLD_LAZY | RTLD_GLOBAL);
            if (!mali) mali = dlopen("/usr/lib/libmali-bifrost.so", RTLD_LAZY | RTLD_GLOBAL);
            if (mali) real_glDrawArrays = dlsym(mali, "glDrawArrays");
        }
    }
    if (real_glDrawArrays) {
        real_glDrawArrays(mode, first, count);
        s_draw_call_counter++;
        if (s_draw_call_counter >= 16) {
            glFlush();
            s_draw_call_counter = 0;
        }
    }
}

__attribute__((visibility("default")))
void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices) {
    if (!real_glDrawElements) {
        real_glDrawElements = dlsym(RTLD_NEXT, "glDrawElements");
        if (!real_glDrawElements) {
            void *mali = dlopen("/usr/lib/libmali-bifrost-patched.so", RTLD_LAZY | RTLD_GLOBAL);
            if (!mali) mali = dlopen("/usr/lib/libmali-bifrost.so", RTLD_LAZY | RTLD_GLOBAL);
            if (mali) real_glDrawElements = dlsym(mali, "glDrawElements");
        }
    }
    if (real_glDrawElements) {
        real_glDrawElements(mode, count, type, indices);
        s_draw_call_counter++;
        if (s_draw_call_counter >= 16) {
            glFlush();
            s_draw_call_counter = 0;
        }
    }
}


typedef void *EGLConfig;
typedef void *EGLContext;
typedef int EGLint;

#ifndef EGL_NO_CONFIG_KHR
#define EGL_NO_CONFIG_KHR ((EGLConfig)0)
#endif
#ifndef EGL_NO_CONTEXT
#define EGL_NO_CONTEXT    ((EGLContext)0)
#endif
#ifndef EGL_NONE
#define EGL_NONE          0x3038
#endif
#ifndef EGL_OPENGL_ES_API
#define EGL_OPENGL_ES_API 0x30A0
#endif
#ifndef EGL_CONTEXT_CLIENT_VERSION
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#endif

static EGLContext (*real_eglCreateContext)(EGLDisplay, EGLConfig, EGLContext, const EGLint *) = NULL;
static EGLBoolean (*real_eglChooseConfig)(EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *) = NULL;
static EGLBoolean (*real_eglBindAPI)(unsigned int) = NULL;

EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint *attrib_list) {
    if (!real_eglCreateContext) {
        void *mali = dlopen("/usr/lib/libmali-bifrost-patched.so", RTLD_LAZY | RTLD_GLOBAL);
        if (!mali) mali = dlopen("/usr/lib/libmali-bifrost.so", RTLD_LAZY | RTLD_GLOBAL);
        if (mali) real_eglCreateContext = dlsym(mali, "eglCreateContext");
        if (!real_eglCreateContext) {
            real_eglCreateContext = dlsym(RTLD_NEXT, "eglCreateContext");
        }
    }
    if (!real_eglChooseConfig) {
        void *mali = dlopen("/usr/lib/libmali-bifrost-patched.so", RTLD_LAZY | RTLD_GLOBAL);
        if (!mali) mali = dlopen("/usr/lib/libmali-bifrost.so", RTLD_LAZY | RTLD_GLOBAL);
        if (mali) real_eglChooseConfig = dlsym(mali, "eglChooseConfig");
        if (!real_eglChooseConfig) {
            real_eglChooseConfig = dlsym(RTLD_NEXT, "eglChooseConfig");
        }
    }
    if (!real_eglBindAPI) {
        real_eglBindAPI = dlsym(RTLD_NEXT, "eglBindAPI");
    }

    if (real_eglBindAPI) {
        real_eglBindAPI(EGL_OPENGL_ES_API);
    }

    GPU_LOG("eglCreateContext interceptado: dpy=%p, config=%p, share=%p", dpy, (void*)config, (void*)share_context);

    EGLConfig chosen_cfg = config;
    if (config == EGL_NO_CONFIG_KHR || config == NULL) {
        GPU_LOG("eglCreateContext: Resuelto fallback EGL_NO_CONFIG_KHR -> autoseleccionando EGLConfig Mali...");
        EGLint cfg_attribs[] = {
            0x3033, 0x0001, /* EGL_SURFACE_TYPE, EGL_PBUFFER_BIT */
            0x3040, 0x0004, /* EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT */
            0x3024, 8,      /* EGL_RED_SIZE */
            0x3023, 8,      /* EGL_GREEN_SIZE */
            0x3022, 8,      /* EGL_BLUE_SIZE */
            EGL_NONE
        };
        EGLint num = 0;
        if (real_eglChooseConfig) {
            real_eglChooseConfig(dpy, cfg_attribs, &chosen_cfg, 1, &num);
            GPU_LOG("eglCreateContext: EGLConfig autoseleccionada: %p (num=%d)", (void*)chosen_cfg, num);
        }
    }

    /* Filtrar atributos de contexto conflictivos para ARM Mali */
    EGLint safe_attribs[16];
    int j = 0;
    if (attrib_list) {
        for (int i = 0; attrib_list[i] != EGL_NONE && j < 14; i += 2) {
            EGLint attr = attrib_list[i];
            EGLint val  = attrib_list[i+1];
            /* 0x31BD = EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_EXT */
            if (attr == 0x31BD) continue;
            safe_attribs[j++] = attr;
            safe_attribs[j++] = val;
        }
    } else {
        safe_attribs[j++] = EGL_CONTEXT_CLIENT_VERSION;
        safe_attribs[j++] = 2;
    }
    safe_attribs[j++] = EGL_NONE;

    if (real_eglCreateContext) {
        EGLContext ctx = real_eglCreateContext(dpy, chosen_cfg, share_context, safe_attribs);
        GPU_LOG("real_eglCreateContext retorno: %p", (void*)ctx);
        return ctx;
    }
    return EGL_NO_CONTEXT;
}

void *eglGetProcAddress(const char *procname) {
    if (!procname) return NULL;
    if (strcmp(procname, "eglCreateContext") == 0) {
        return (void *)eglCreateContext;
    }
    if (strcmp(procname, "eglBindWaylandDisplayWL") == 0) {
        return (void *)eglBindWaylandDisplayWL;
    }
    if (strcmp(procname, "eglUnbindWaylandDisplayWL") == 0) {
        return (void *)eglUnbindWaylandDisplayWL;
    }
    if (strcmp(procname, "glFinish") == 0) {
        return (void *)glFinish;
    }
    if (strcmp(procname, "glFlush") == 0) {
        return (void *)glFlush;
    }
    if (strcmp(procname, "glDrawArrays") == 0) {
        return (void *)glDrawArrays;
    }
    if (strcmp(procname, "glDrawElements") == 0) {
        return (void *)glDrawElements;
    }

    if (!real_eglGetProcAddress) {
        void *mali = dlopen("/usr/lib/libmali-bifrost-patched.so", RTLD_LAZY | RTLD_GLOBAL);
        if (!mali) mali = dlopen("/usr/lib/libmali-bifrost.so", RTLD_LAZY | RTLD_GLOBAL);
        if (mali) real_eglGetProcAddress = dlsym(mali, "eglGetProcAddress");
        if (!real_eglGetProcAddress) {
            real_eglGetProcAddress = dlsym(RTLD_NEXT, "eglGetProcAddress");
        }
    }
    if (real_eglGetProcAddress) {
        return real_eglGetProcAddress(procname);
    }
    return NULL;
}

/* ========================================================================= */
/* 5. FILTRADO DE CONECTOR WRITEBACK INESTABLE DE MEDIATEK                   */
/* ========================================================================= */

#ifndef DRM_MODE_CONNECTOR_WRITEBACK
#define DRM_MODE_CONNECTOR_WRITEBACK 18
#endif

static drmModeConnectorPtr (*real_drmModeGetConnector)(int, uint32_t) = NULL;
static drmModeConnectorPtr (*real_drmModeGetConnectorCurrent)(int, uint32_t) = NULL;



drmModeConnectorPtr drmModeGetConnector(int fd, uint32_t connectorId) {
    if (!real_drmModeGetConnector) {
        real_drmModeGetConnector = dlsym(RTLD_NEXT, "drmModeGetConnector");
        if (!real_drmModeGetConnector) {
            void *ldrm = dlopen("/usr/lib/libdrm.so.2", RTLD_LAZY | RTLD_GLOBAL);
            if (ldrm) real_drmModeGetConnector = dlsym(ldrm, "drmModeGetConnector");
        }
    }
    drmModeConnectorPtr conn = real_drmModeGetConnector ? real_drmModeGetConnector(fd, connectorId) : NULL;
    if (conn && conn->connector_type == DRM_MODE_CONNECTOR_WRITEBACK) {
        GPU_LOG("Ocultando conector inestable MediaTek Writeback (ID=%u)", connectorId);
        conn->connection = DRM_MODE_DISCONNECTED;
        conn->count_modes = 0;
    }
    return conn;
}

drmModeConnectorPtr drmModeGetConnectorCurrent(int fd, uint32_t connectorId) {
    if (!real_drmModeGetConnectorCurrent) {
        real_drmModeGetConnectorCurrent = dlsym(RTLD_NEXT, "drmModeGetConnectorCurrent");
        if (!real_drmModeGetConnectorCurrent) {
            void *ldrm = dlopen("/usr/lib/libdrm.so.2", RTLD_LAZY | RTLD_GLOBAL);
            if (ldrm) real_drmModeGetConnectorCurrent = dlsym(ldrm, "drmModeGetConnectorCurrent");
        }
    }
    drmModeConnectorPtr conn = real_drmModeGetConnectorCurrent ? real_drmModeGetConnectorCurrent(fd, connectorId) : NULL;
    if (conn && conn->connector_type == DRM_MODE_CONNECTOR_WRITEBACK) {
        GPU_LOG("Ocultando conector inestable MediaTek Writeback (ID=%u)", connectorId);
        conn->connection = DRM_MODE_DISCONNECTED;
        conn->count_modes = 0;
    }
    return conn;
}

/* Constructor del driver */
__attribute__((constructor))
static void gpu_driver_init(void) {
    GPU_LOG(">>> DRIVER DE INTERFAZ GPU MALI-G57 MC2 INICIALIZADO <<<");
}
