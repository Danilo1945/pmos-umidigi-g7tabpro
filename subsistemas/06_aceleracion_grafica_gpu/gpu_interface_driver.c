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
#include <sys/mman.h>
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
#include <math.h>
#include <arm_neon.h>


static int g_kms_fd = -1;
int is_active_dumb_handle(uint32_t handle);

static inline int is_valid_jc_ptr(uint64_t ptr) {
    return (ptr > 0x10000ULL && (ptr >> 48) == 0ULL);
}

__attribute__((visibility("default")))
int drmIoctl(int fd, unsigned long request, void *arg) {
    if (fd >= 0 && g_kms_fd < 0) {
        struct drm_get_cap c = { .capability = DRM_CAP_DUMB_BUFFER };
        if (ioctl(fd, DRM_IOCTL_GET_CAP, &c) == 0 && c.value != 0) {
            g_kms_fd = fd;
        }
    }

    if (request == DRM_IOCTL_MODE_DESTROY_DUMB && arg) {
        struct drm_mode_destroy_dumb *d = (struct drm_mode_destroy_dumb *)arg;
        if (is_active_dumb_handle(d->handle)) {
            return 0;
        }
    }

    if (request == DRM_IOCTL_GEM_CLOSE && arg) {
        struct drm_gem_close *c = (struct drm_gem_close *)arg;
        if (is_active_dumb_handle(c->handle)) {
            return 0;
        }
    }

    if (request == DRM_IOCTL_MODE_CURSOR || request == DRM_IOCTL_MODE_CURSOR2) {
        errno = EINVAL;
        return -1;
    }

    if (request == DRM_IOCTL_DROP_MASTER) {
        return 0; /* Prevenir pérdida de DRM Master en card0 durante sesión activa */
    }

    if (request == DRM_IOCTL_MODE_PAGE_FLIP) {
        ioctl(fd, DRM_IOCTL_SET_MASTER, 0);
        for (int tries = 0; tries < 25; tries++) {
            int r = ioctl(fd, request, arg);
            if (r == 0 || errno != EBUSY) return r;
            usleep(1000);
        }
        return ioctl(fd, request, arg);
    }

    if (request == DRM_IOCTL_MODE_SETCRTC) {
        ioctl(fd, DRM_IOCTL_SET_MASTER, 0);
    }

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

__attribute__((visibility("default")))
int drmPrimeFDToHandle(int fd, int prime_fd, uint32_t *handle) {
    struct drm_prime_handle args;
    memset(&args, 0, sizeof(args));
    args.fd = prime_fd;
    args.flags = 0;
    int ret = ioctl(fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &args);
    if (ret)
        return ret;
    if (handle)
        *handle = args.handle;
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
    if (g_debug_enabled < 0) {
        g_debug_enabled = (getenv("MALI_DEBUG") != NULL);
    }
    return g_debug_enabled;
}

static inline void kmsg_always(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) {
        fprintf(stderr, "%s", buf);
        fflush(stderr);
    }
}

static inline void kmsg_log(const char *fmt, ...) {
    if (!is_debug_enabled()) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) {
        fprintf(stderr, "%s", buf);
        fflush(stderr);
    }
}

#define GPU_LOG(fmt, ...) do { \
    if (is_debug_enabled()) { \
        kmsg_log("[gpu-driver] " fmt "\n", ##__VA_ARGS__); \
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

static inline void flush_dcache_range(uint64_t addr, size_t size) {
    if (addr < 0x100000ULL) return; /* Prevenir Data Abort en VAs de GPU no mapeadas en CPU */
    uintptr_t start = (uintptr_t)addr & ~63ULL;
    uintptr_t end = ((uintptr_t)addr + size + 63ULL) & ~63ULL;
    for (uintptr_t p = start; p < end; p += 64) {
        __asm__ volatile("dc cvac, %0" : : "r"(p) : "memory");
    }
    __asm__ volatile("dsb sy" : : : "memory");
}

static void flush_all_gpu_mappings(void) {
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        uintptr_t start = 0, end = 0;
        char perms[16] = {0};
        char path[256] = {0};
        char *p_cur = line;
        char *endptr = NULL;
        start = strtoul(p_cur, &endptr, 16);
        if (endptr && *endptr == '-') {
            p_cur = endptr + 1;
            end = strtoul(p_cur, &endptr, 16);
        } else {
            continue;
        }
        while (*endptr == ' ') endptr++;
        int pi = 0;
        while (*endptr && *endptr != ' ' && pi < 15) perms[pi++] = *endptr++;
        perms[pi] = '\0';
        char *last_space = strrchr(line, ' ');
        if (last_space) {
            strncpy(path, last_space + 1, sizeof(path) - 1);
            char *nl = strchr(path, '\n');
            if (nl) *nl = '\0';
        }
        if (perms[1] == 'w') {
            size_t size = end - start;
            if (strstr(path, "/dev/mali") || (start >= 0x7000000000ULL && start < 0x7c00000000ULL)) {
                if (size <= 32 * 1024 * 1024) {
                    flush_dcache_range(start, size);
                    GPU_LOG("  [FLUSH_MAP] 0x%016lx - 0x%016lx (%lu KB) %s %s",
                            (unsigned long)start, (unsigned long)end,
                            (unsigned long)(size / 1024), perms, path);
                }
            }
        }
    }
    fclose(f);
    __asm__ volatile("dsb sy" : : : "memory");
}

struct kbase_ioctl_version_check {
    unsigned short major;
    unsigned short minor;
};

#define MAX_BOUNCE_FDS 256
struct bounce_buffer {
    uint64_t cpu_base;
    uint64_t gpu_base;
    uint32_t rolling_idx;
};
static struct bounce_buffer g_bounce_table[MAX_BOUNCE_FDS];

static int ensure_bounce_buffer(int fd) {
    if (fd < 0 || fd >= MAX_BOUNCE_FDS) return -1;
    if (g_bounce_table[fd].cpu_base != 0) return 0;

    /* Reservar 256 páginas (1 MB) = 4096 slots de 256 bytes exclusivos para re-alineación de Job Descriptors */
    union {
        struct {
            uint64_t va_pages;
            uint64_t commit_pages;
            uint64_t extent;
            uint64_t flags;
        } in;
        struct {
            uint32_t flags;
            uint32_t pad;
            uint64_t gpu_va;
        } out;
    } alloc;
    memset(&alloc, 0, sizeof(alloc));
    alloc.in.va_pages = 256;
    alloc.in.commit_pages = 256;
    alloc.in.extent = 0;
    alloc.in.flags = 0x17; /* BASE_MEM_PROT_CPU_RD|WR, BASE_MEM_PROT_GPU_RD|WR, BASE_MEM_SAME_VA (0x10) */

    long ret = syscall(SYS_ioctl, fd, 0xc0208005, &alloc);
    if (ret >= 0 && alloc.out.gpu_va != 0) {
        void *ptr = mmap((void *)(uintptr_t)alloc.out.gpu_va, 256 * 4096,
                         PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED,
                         fd, alloc.out.gpu_va);
        if (ptr == MAP_FAILED) {
            ptr = mmap(NULL, 256 * 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, alloc.out.gpu_va);
        }
        if (ptr != MAP_FAILED) {
            g_bounce_table[fd].cpu_base = (uint64_t)(uintptr_t)ptr;
            g_bounce_table[fd].gpu_base = alloc.out.gpu_va;
            g_bounce_table[fd].rolling_idx = 0;
            kmsg_always("[VALHALL_BOUNCE] Bounce buffer SAME_VA asignado para fd=%d: cpu_base=0x%llx, gpu_base=0x%llx (1MB / 4096 slots)\n",
                        fd, (unsigned long long)g_bounce_table[fd].cpu_base,
                        (unsigned long long)g_bounce_table[fd].gpu_base);
            return 0;
        } else {
            kmsg_always("[VALHALL_BOUNCE] ERROR: mmap fallo para fd=%d (errno=%d)\n", fd, errno);
            return -1;
        }
    } else {
        kmsg_always("[VALHALL_BOUNCE] ERROR: ioctl MEM_ALLOC (0xc0208005) fallo para fd=%d ret=%ld (errno=%d)\n", fd, ret, errno);
        return -1;
    }
}

static inline int alloc_bounce_slot(int fd, void **out_cpu_ptr, uint64_t *out_gpu_va) {
    return 0;
}

static inline void *gpu_to_cpu_jc(int fd, uint64_t gpu_addr) {
    if (fd >= 0 && fd < MAX_BOUNCE_FDS && g_bounce_table[fd].gpu_base != 0 &&
        gpu_addr >= g_bounce_table[fd].gpu_base &&
        gpu_addr < g_bounce_table[fd].gpu_base + (256 * 4096)) {
        uint64_t offset = gpu_addr - g_bounce_table[fd].gpu_base;
        return (void *)(uintptr_t)(g_bounce_table[fd].cpu_base + offset);
    }
    return (void *)(uintptr_t)gpu_addr;
}

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
    if (req == DRM_IOCTL_DROP_MASTER) {
        return 0; /* Nunca soltar DRM Master de card0 */
    }
    if (is_debug_enabled() && (((req >> 8) & 0xFF) == 0x80)) {
        GPU_LOG("KBASE ioctl fd=%d req=0x%x", fd, req);
    }

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
     * KBASE_IOCTL_JOB_SUBMIT de libmali userspace = 0x40108002 (_IOW(0x80, 2, 16 bytes))
     * libmali-bifrost.so (r25p0/r32p0) construye base_jd_atom_v2 de 64 bytes (stride=0x40)
     * CON seq_nr en offset +0, jc en +8, udata en +16, extres_list en +32, nr_extres en +40,
     * pre_dep en +44, atom_number en +48, prio en +49, device_nr en +50, core_req en +52.
     * En mali_kbase_mt6789.ko, stride=0x40 se interpreta como atom legacy SIN seq_nr y se copia
     * a user_atom+8 (desplazando todos los campos +8 bytes), mientras que stride=0x48 (72 bytes)
     * se copia directamente a user_atom+0 alineando todos los campos byte a byte.
     */
    if (req == 0x40108002) {
        if (!arg) return -1;
        struct kbase_ioctl_job_submit {
            uint64_t addr;
            uint32_t nr_atoms;
            uint32_t stride;
        } *sub = (void *)arg;

        if (sub->stride == 0x40 && sub->nr_atoms > 0 && sub->nr_atoms <= 256) {
            const uint8_t *src = (const uint8_t *)(uintptr_t)sub->addr;

            static int s_maps_logged = 0;
            if (!s_maps_logged) {
                s_maps_logged = 1;
                FILE *fmaps = fopen("/proc/self/maps", "r");
                if (fmaps) {
                    char lbuf[256];
                    while (fgets(lbuf, sizeof(lbuf), fmaps)) {
                        if (strstr(lbuf, "7be") || strstr(lbuf, "mali")) {
                            char *nl = strchr(lbuf, '\n');
                            if (nl) *nl = 0;
                            GPU_LOG("  [MAPS] %s", lbuf);
                        }
                    }
                    fclose(fmaps);
                }
            }

            // Log de átomos originales recibidos de libmali
            for (uint32_t i = 0; i < sub->nr_atoms; i++) {
                const uint64_t *q = (const uint64_t *)&src[i * 64];
                GPU_LOG("  Atom[%u] raw64: +0(seq)=0x%llx +8(jc)=0x%llx +16(u0)=0x%llx +24(u1)=0x%llx +32(ext)=0x%llx +40=0x%llx +48=0x%llx +56=0x%llx",
                        i,
                        (unsigned long long)q[0], (unsigned long long)q[1],
                        (unsigned long long)q[2], (unsigned long long)q[3],
                        (unsigned long long)q[4], (unsigned long long)q[5],
                        (unsigned long long)q[6], (unsigned long long)q[7]);
                uint32_t core_req = (uint32_t)(q[6] >> 32);
                if ((core_req & 0x2ff) == 0x209 && q[1] > 0x10000) {
                    const uint64_t *jit_ptr = (const uint64_t *)(uintptr_t)q[1];
                    for (int j = 0; j < 2; j++) {
                        const uint64_t *jp = &jit_ptr[j * 6];
                        GPU_LOG("    JIT_INFO[%d] @ 0x%llx: w0(gpu_addr)=0x%llx w1(va_pg)=0x%llx w2(commit)=0x%llx w3(ext)=0x%llx w4(id/fl)=0x%llx w5(heap)=0x%llx",
                                j, (unsigned long long)(uintptr_t)jp,
                                (unsigned long long)jp[0], (unsigned long long)jp[1],
                                (unsigned long long)jp[2], (unsigned long long)jp[3],
                                (unsigned long long)jp[4], (unsigned long long)jp[5]);
                    }
                    flush_dcache_range(q[1], 256);
                }
            }

            // Detección de átomo heterogéneo que contiene NULL barrier entre Compute y Vertex/Tiler
            int split_atom_idx = -1;
            uint64_t compute_last_job = 0;
            uint64_t vertex_first_job = 0;
            uint64_t vertex_last_job = 0;
            uint64_t tiler_first_job = 0;
            uint8_t orig_split_atom_num = 0;

            for (uint32_t i = 0; i < sub->nr_atoms; i++) {
                const uint64_t *q = (const uint64_t *)&src[i * 64];
                uint32_t core_req = (uint32_t)(q[6] >> 32);
                if (!(core_req & 0x200) && is_valid_jc_ptr(q[1])) {
                    uint64_t cur = q[1];
                    uint64_t prev = 0;
                    int step = 0;
                    while (is_valid_jc_ptr(cur) && step < 16) {
                        uint64_t *jc_ptr = (uint64_t *)(uintptr_t)cur;
                        uint32_t job_type = (uint32_t)((jc_ptr[2] >> 1) & 0x7f);
                        if (0 && job_type == 1 && prev != 0 && is_valid_jc_ptr(jc_ptr[3])) {
                            split_atom_idx = (int)i;
                            compute_last_job = prev;
                            vertex_first_job = jc_ptr[3];
                            orig_split_atom_num = src[i * 64 + 48];
                            break;
                        }
                        prev = cur;
                        cur = jc_ptr[3];
                        step++;
                    }
                }
            }

            if (split_atom_idx >= 0 && vertex_first_job != 0) {
                // Encontrar la transición de Vertex (type 5) a Tiler (type 7)
                uint64_t vcur = vertex_first_job;
                uint64_t vprev = 0;
                int vstep = 0;
                while (is_valid_jc_ptr(vcur) && vstep < 16) {
                    uint64_t *vptr = (uint64_t *)(uintptr_t)vcur;
                    uint32_t jtype = (uint32_t)((vptr[2] >> 1) & 0x7f);
                    if (jtype == 7 && vprev != 0) {
                        vertex_last_job = vprev;
                        tiler_first_job = vcur;
                        break;
                    }
                    vprev = vcur;
                    vcur = vptr[3];
                    vstep++;
                }
                GPU_LOG("  [DETECT_3WAY_SPLIT] Compute (last=0x%llx) -> Vertex (first=0x%llx, last=0x%llx) -> Tiler (first=0x%llx)",
                        (unsigned long long)compute_last_job, (unsigned long long)vertex_first_job,
                        (unsigned long long)vertex_last_job, (unsigned long long)tiler_first_job);
            }

            uint8_t atoms72[256 * 72];
            memset(atoms72, 0, sizeof(atoms72));
            uint32_t out_nr_atoms = 0;
            uint8_t max_atom_num = 0;
            for (uint32_t i = 0; i < sub->nr_atoms; i++) {
                if (src[i * 64 + 48] > max_atom_num) max_atom_num = src[i * 64 + 48];
            }
            uint8_t new_vertex_atom_num = (max_atom_num < 250) ? (max_atom_num + 1) : 251;
            uint8_t new_tiler_atom_num = (max_atom_num < 250) ? (max_atom_num + 2) : 252;

            kmsg_always("[gpu-driver] ATOM_CHECK: nr_atoms=%u, split_idx=%d, clast=0x%llx, vfirst=0x%llx, tfirst=0x%llx\n",
                        sub->nr_atoms, split_atom_idx,
                        (unsigned long long)compute_last_job,
                        (unsigned long long)vertex_first_job,
                        (unsigned long long)tiler_first_job);

            if (split_atom_idx >= 0 && compute_last_job != 0 && vertex_first_job != 0 && tiler_first_job != 0) {
                // 1. Terminar la cadena Compute en compute_last_job (JC->next = 0)
                uint64_t *clast_ptr = (uint64_t *)(uintptr_t)compute_last_job;
                clast_ptr[3] = 0;
                flush_dcache_range(compute_last_job, 64);
                GPU_LOG("  [ATOM_SPLIT_3WAY] Compute chain terminada en 0x%llx (next=0)", (unsigned long long)compute_last_job);

                // 2. Terminar la cadena Vertex en vertex_last_job (JC->next = 0)
                uint64_t *vlast_ptr = (uint64_t *)(uintptr_t)vertex_last_job;
                vlast_ptr[3] = 0;
                flush_dcache_range(vertex_last_job, 64);
                GPU_LOG("  [ATOM_SPLIT_3WAY] Vertex chain terminada en 0x%llx (next=0)", (unsigned long long)vertex_last_job);

                // 3. Asegurar barrier en la cabeza de Vertex y Tiler
                uint64_t *vhead = (uint64_t *)(uintptr_t)vertex_first_job;
                vhead[2] |= 1ULL;
                flush_dcache_range(vertex_first_job, 64);

                uint64_t *thead = (uint64_t *)(uintptr_t)tiler_first_job;
                thead[2] |= 1ULL;
                flush_dcache_range(tiler_first_job, 64);

                // 4. Construir los átomos divididos en 3 etapas puras
                for (uint32_t i = 0; i < sub->nr_atoms; i++) {
                    if ((int)i == split_atom_idx) {
                        // Átomo A: Pure Compute (Slot 1)
                        memcpy(&atoms72[out_nr_atoms * 72], &src[i * 64], 64);
                        uint8_t *a_ptr = &atoms72[out_nr_atoms * 72];
                        *(uint32_t *)&a_ptr[52] = 0x4a; // Pure CS: BASE_JD_REQ_CS | BASE_JD_REQ_CF | BASE_JD_REQ_COHERENT_GROUP
                        GPU_LOG("  [ATOM_SPLIT_3WAY] Inyectando Atom[%u] (Compute): jc=0x%llx, atom_num=%u, core_req=0x%x",
                                out_nr_atoms, (unsigned long long)*(uint64_t *)&a_ptr[8], a_ptr[48], *(uint32_t *)&a_ptr[52]);
                        out_nr_atoms++;

                        // Átomo B: Pure Vertex (Slot 1, depende de Compute)
                        memcpy(&atoms72[out_nr_atoms * 72], &src[i * 64], 64);
                        uint8_t *b_ptr = &atoms72[out_nr_atoms * 72];
                        *(uint64_t *)&b_ptr[8] = vertex_first_job;
                        b_ptr[48] = new_vertex_atom_num; // 5
                        b_ptr[44] = orig_split_atom_num; // 2 (depende de Compute)
                        b_ptr[45] = 1; // BASE_JD_DEP_DATA
                        b_ptr[46] = 0;
                        b_ptr[47] = 0;
                        *(uint32_t *)&b_ptr[52] = 0x4a; // Pure CS: BASE_JD_REQ_CS | BASE_JD_REQ_CF | BASE_JD_REQ_COHERENT_GROUP
                        GPU_LOG("  [ATOM_SPLIT_3WAY] Inyectando Atom[%u] (Vertex): jc=0x%llx, atom_num=%u, dep=%u, core_req=0x%x",
                                out_nr_atoms, (unsigned long long)vertex_first_job, b_ptr[48], b_ptr[44], *(uint32_t *)&b_ptr[52]);
                        out_nr_atoms++;

                        // Átomo C: Pure Tiler (Slot 2, depende de Vertex)
                        memcpy(&atoms72[out_nr_atoms * 72], &src[i * 64], 64);
                        uint8_t *c_ptr = &atoms72[out_nr_atoms * 72];
                        *(uint64_t *)&c_ptr[8] = tiler_first_job;
                        c_ptr[48] = new_tiler_atom_num; // 6
                        c_ptr[44] = new_vertex_atom_num; // 5 (depende de Vertex)
                        c_ptr[45] = 1; // BASE_JD_DEP_DATA
                        c_ptr[46] = 0;
                        c_ptr[47] = 0;
                        *(uint32_t *)&c_ptr[52] = 0x4c; // Pure T: BASE_JD_REQ_T | BASE_JD_REQ_CF | BASE_JD_REQ_COHERENT_GROUP
                        GPU_LOG("  [ATOM_SPLIT_3WAY] Inyectando Atom[%u] (Tiler): jc=0x%llx, atom_num=%u, dep=%u, core_req=0x%x",
                                out_nr_atoms, (unsigned long long)tiler_first_job, c_ptr[48], c_ptr[44], *(uint32_t *)&c_ptr[52]);
                        out_nr_atoms++;
                    } else {
                        memcpy(&atoms72[out_nr_atoms * 72], &src[i * 64], 64);
                        uint8_t *o_ptr = &atoms72[out_nr_atoms * 72];
                        // Fragment Shader dependía de Atom 2 (orig_split_atom_num) -> redirigir a Tiler (6)
                        if (o_ptr[44] == orig_split_atom_num) {
                            o_ptr[44] = new_tiler_atom_num;
                            GPU_LOG("  [ATOM_SPLIT_3WAY] Redirigida dep[0] de Atom[%u] (num=%u): %u -> %u (Tiler)",
                                    out_nr_atoms, o_ptr[48], orig_split_atom_num, new_tiler_atom_num);
                        }
                        if (o_ptr[46] == orig_split_atom_num) {
                            o_ptr[46] = new_tiler_atom_num;
                            GPU_LOG("  [ATOM_SPLIT_3WAY] Redirigida dep[1] de Atom[%u] (num=%u): %u -> %u (Tiler)",
                                    out_nr_atoms, o_ptr[48], orig_split_atom_num, new_tiler_atom_num);
                        }
                        out_nr_atoms++;
                    }
                }
            } else {
                // Sin split necesario: copia directa 64 -> 72 bytes
                for (uint32_t i = 0; i < sub->nr_atoms; i++) {
                    memcpy(&atoms72[i * 72], &src[i * 64], 64);
                }
                out_nr_atoms = sub->nr_atoms;
            }

            // Preservar átomos y flags originales de libmali para Valhall v9
            for (uint32_t i = 0; i < out_nr_atoms; i++) {
                uint32_t *creq_ptr = (uint32_t *)&atoms72[i * 72 + 52];
                if (0 && !(*creq_ptr & 0x200)) {
                    // Flags de coherencia preservados sin alteraciones forzadas
                    *creq_ptr |= 0x48;
                }
                if (*creq_ptr & 0x01) { // BASE_JD_REQ_FS
                    uint64_t fjc = *(const uint64_t *)&atoms72[i * 72 + 8];
                    if (is_valid_jc_ptr(fjc)) {
                        uint64_t *fj0 = (uint64_t *)(uintptr_t)fjc;
                        uint32_t type = (uint32_t)((fj0[2] >> 1) & 0x7f);
                        GPU_LOG("  [FRAGMENT_INSPECT] fjc=0x%llx: type=%u, w2=0x%llx, next=0x%llx, w4=0x%llx, w5=0x%llx (tag=%u)",
                                (unsigned long long)fjc, type, (unsigned long long)fj0[2],
                                (unsigned long long)fj0[3], (unsigned long long)fj0[4],
                                (unsigned long long)fj0[5], (unsigned int)(fj0[5] & 0x3));
                        if (is_valid_jc_ptr(fj0[5])) {
                            const uint64_t *fbd0 = (const uint64_t *)(uintptr_t)(fj0[5] & ~3ULL);
                            GPU_LOG("    [FBD0_DUMP] fbd0 @ 0x%llx:", (unsigned long long)(fj0[5] & ~3ULL));
                            for (int fw = 0; fw < 8; fw += 4) {
                                GPU_LOG("      fbd0[%02d..%02d]: 0x%016llx 0x%016llx 0x%016llx 0x%016llx",
                                        fw, fw + 3, fbd0[fw], fbd0[fw+1], fbd0[fw+2], fbd0[fw+3]);
                            }
                        }
                        // Fusión de fragment job real sobre pre-frame dummy con bounds nulos
                        flush_dcache_range((uintptr_t)fj0, 128);
                        if (type == 9 && fj0[4] == 0 && is_valid_jc_ptr(fj0[3])) {
                            uint64_t real_fj = fj0[3];
                            const uint64_t *fj1 = (const uint64_t *)(uintptr_t)real_fj;
                            GPU_LOG("  [DUMP_FJ1] @ 0x%llx: w0=0x%llx w1=0x%llx w2=0x%llx w3=0x%llx w4=0x%llx w5=0x%llx w6=0x%llx w7=0x%llx w8=0x%llx w9=0x%llx w10=0x%llx w11=0x%llx",
                                    (unsigned long long)real_fj, fj1[0], fj1[1], fj1[2], fj1[3], fj1[4], fj1[5], fj1[6], fj1[7], fj1[8], fj1[9], fj1[10], fj1[11]);
                            fj0[4] = fj1[4]; /* Bound Min/Max X/Y (tiles) */
                            fj0[5] = fj1[5]; /* Framebuffer Pointer con tag 3 */
                            fj0[6] = fj1[6]; /* Tile Enable Map */
                            fj0[7] = fj1[7]; /* Tile Enable Map Row Stride */
                            fj0[8] = fj1[8];
                            fj0[9] = fj1[9];
                            fj0[10] = fj1[10];
                            fj0[11] = fj1[11];
                            fj0[12] = fj1[12];
                            fj0[13] = fj1[13];
                            fj0[14] = fj1[14];
                            fj0[15] = fj1[15];
                            fj0[2] &= 0x00000000ffffffffULL; /* Limpiar dep1 y dep2 */
                            fj0[2] |= 1ULL; /* job_barrier = 1 (bit 0) */
                            fj0[3] = fj1[3]; /* Preservar el resto de la cadena */
                            /* Preservar FBD tag original emitido por libmali */
                            flush_dcache_range((uintptr_t)fj0, 128);
                            GPU_LOG("  [VALHALL_MERGE_FRAGMENT] Fusionado fragment job @ 0x%llx sobre pre-frame @ 0x%llx (128B aligned! w4=0x%llx w5=0x%llx next=0x%llx)",
                                    (unsigned long long)real_fj, (unsigned long long)fjc, (unsigned long long)fj0[4], (unsigned long long)fj0[5], (unsigned long long)fj0[3]);

                            if (is_valid_jc_ptr(fj0[5])) {
                                const uint64_t *mfbd = (const uint64_t *)(uintptr_t)(fj0[5] & ~3ULL);
                                GPU_LOG("  [MFBD_DUMP] @ 0x%llx:", (unsigned long long)(fj0[5] & ~3ULL));
                                for (int mw = 0; mw < 16; mw += 4) {
                                    GPU_LOG("    mfbd[%02d..%02d]: 0x%016llx 0x%016llx 0x%016llx 0x%016llx",
                                            mw, mw + 3, mfbd[mw], mfbd[mw+1], mfbd[mw+2], mfbd[mw+3]);
                                }
                            }
                        }
                    }
                }
            }

            for (uint32_t i = 0; i < out_nr_atoms; i++) {
                uint32_t core_req = *(const uint32_t *)&atoms72[i * 72 + 52];
                uint64_t sjc = *(const uint64_t *)&atoms72[i * 72 + 8];
                if (!(core_req & 0x200) && is_valid_jc_ptr(sjc)) {
                    // Bouncing deshabilitado: preservamos descriptores legítimos de libmali
                    if (0 && (sjc & 0x7f) != 0) {
                        void *bounce_cpu = NULL;
                        uint64_t bounce_gpu = 0;
                        if (alloc_bounce_slot(fd, &bounce_cpu, &bounce_gpu)) {
                            memcpy(bounce_cpu, (const void *)(uintptr_t)sjc, 256);
                            flush_dcache_range((uintptr_t)bounce_cpu, 256);
                            *(uint64_t *)&atoms72[i * 72 + 8] = bounce_gpu;
                            GPU_LOG("  [VALHALL_BOUNCE] Head JC de Atom[%u] desalineado (0x%llx) -> bounce gpu=0x%llx (cpu=0x%llx)",
                                    i, (unsigned long long)sjc, (unsigned long long)bounce_gpu, (unsigned long long)(uintptr_t)bounce_cpu);
                            sjc = bounce_gpu;
                        }
                    }

                    uint64_t cur = sjc;
                    int step = 0;
                    while (is_valid_jc_ptr(cur) && step < 16) {
                        uint64_t *p_rw = (uint64_t *)gpu_to_cpu_jc(fd, cur);
                        const uint64_t *p = (const uint64_t *)gpu_to_cpu_jc(fd, cur);
                        uint32_t type = (uint32_t)((p[2] >> 1) & 0x7f);
                        uint32_t jidx = (uint32_t)(p[2] >> 16) & 0xffff;
                        uint32_t dep1 = (uint32_t)(p[2] >> 32) & 0xffff;
                        uint32_t dep2 = (uint32_t)(p[2] >> 48) & 0xffff;
                        uint32_t bar = (uint32_t)(p[2] & 0x1);

                        // Bouncing deshabilitado: preservamos descriptores legítimos de libmali
                        if (0 && p[3] != 0 && (p[3] & 0x7f) != 0) {
                            uint64_t unaligned_next = p[3];
                            void *bounce_cpu = NULL;
                            uint64_t bounce_gpu = 0;
                            if (alloc_bounce_slot(fd, &bounce_cpu, &bounce_gpu)) {
                                void *src_cpu = gpu_to_cpu_jc(fd, unaligned_next);
                                memcpy(bounce_cpu, src_cpu, 256);
                                p_rw[3] = bounce_gpu;
                                flush_dcache_range((uintptr_t)bounce_cpu, 256);
                                flush_dcache_range((uintptr_t)p_rw, 64);
                                GPU_LOG("  [VALHALL_BOUNCE] Job @ 0x%llx next desalineado (0x%llx) -> bounce gpu=0x%llx (cpu=0x%llx)",
                                         (unsigned long long)cur, (unsigned long long)unaligned_next,
                                         (unsigned long long)bounce_gpu, (unsigned long long)(uintptr_t)bounce_cpu);
                            }
                        }

                        // Limpiar bits 9..15 en w2 preservando bit 8 (job_barrier)
                        if (0 && (p[2] & 0x000000000000fe00ULL)) {
                            GPU_LOG("  [VALHALL_FIX_FLAGS] Limpiando bits [15:9] en JC @ 0x%llx: 0x%llx -> 0x%llx",
                                    (unsigned long long)cur, (unsigned long long)p[2],
                                    (unsigned long long)(p[2] & ~0x000000000000fe00ULL));
                            p_rw[2] &= ~0x000000000000fe00ULL;
                            flush_dcache_range((uintptr_t)&p_rw[2], 64);
                        }

                        /* Preservar FBD tag legítimo de libmali */

                        // Flush universal de descriptor de trabajo para silicio Valhall
                        flush_dcache_range((uintptr_t)p_rw, 256);

                        /* Descriptores originales de silicio Valhall preservados íntegros */
                        GPU_LOG("  [BEFORE_SUBMIT] ATOM[%u] JC[%u.%d] @ 0x%llx: type=%u, idx=0x%x, dep1=0x%x, dep2=0x%x, bar=%u, next=0x%llx",
                                i, i, step, (unsigned long long)cur, type, jidx, dep1, dep2, bar, (unsigned long long)p[3]);
                        for (int w = 0; w < 32; w += 4) {
                            GPU_LOG("    w[%02d..%02d]: 0x%016llx 0x%016llx 0x%016llx 0x%016llx",
                                    w, w + 3,
                                    (unsigned long long)p[w], (unsigned long long)p[w + 1],
                                    (unsigned long long)p[w + 2], (unsigned long long)p[w + 3]);
                        }
                        if (0 && type == 5) {
                            /* Valhall v9 ejecuta vertex shaders en shader cores como COMPUTE (type 4) */
                            p_rw[2] = (p_rw[2] & ~0xfeULL) | (4ULL << 1);
                            flush_dcache_range((uintptr_t)p_rw, 256);
                            GPU_LOG("  [VALHALL_VERTEX_ADAPT] Convertido Vertex Job en JC[%u.%d] @ 0x%llx a COMPUTE (type 4)",
                                    i, step, (unsigned long long)cur);
                        }
                        if (type == 9 && is_valid_jc_ptr(p[5])) {
                            uint64_t fb_ptr = p[5] & ~0x3ULL;
                            void *fb_cpu = gpu_to_cpu_jc(fd, fb_ptr);
                            flush_dcache_range((uintptr_t)fb_cpu, 256);
                            const uint64_t *fb = (const uint64_t *)fb_cpu;
                            GPU_LOG("    [FB_DUMP] w5=0x%llx (tag=%u, base=0x%llx):",
                                    (unsigned long long)p[5], (unsigned int)(p[5] & 0x3), (unsigned long long)fb_ptr);
                            for (int fw = 0; fw < 32; fw += 4) {
                                GPU_LOG("      fb[%02d..%02d]: 0x%016llx 0x%016llx 0x%016llx 0x%016llx",
                                        fw, fw + 3,
                                        (unsigned long long)fb[fw], (unsigned long long)fb[fw + 1],
                                        (unsigned long long)fb[fw + 2], (unsigned long long)fb[fw + 3]);
                            }
                            if (is_valid_jc_ptr(fb[2])) {
                                const uint64_t *sub_ptr = (const uint64_t *)(uintptr_t)(fb[2] & ~0x3ULL);
                                GPU_LOG("      [FB_SUB fb[2]] @ 0x%llx: 0x%016llx 0x%016llx 0x%016llx 0x%016llx",
                                        (unsigned long long)fb[2], sub_ptr[0], sub_ptr[1], sub_ptr[2], sub_ptr[3]);
                            }
                            if (is_valid_jc_ptr(fb[3])) {
                                const uint64_t *sub_ptr = (const uint64_t *)(uintptr_t)(fb[3] & ~0x3ULL);
                                GPU_LOG("      [FB_SUB fb[3]] @ 0x%llx: 0x%016llx 0x%016llx 0x%016llx 0x%016llx",
                                        (unsigned long long)fb[3], sub_ptr[0], sub_ptr[1], sub_ptr[2], sub_ptr[3]);
                            }
                            if (is_valid_jc_ptr(fb[7])) {
                                const uint64_t *sub_ptr = (const uint64_t *)(uintptr_t)(fb[7] & ~0x3ULL);
                                GPU_LOG("      [FB_SUB fb[7]] @ 0x%llx: 0x%016llx 0x%016llx 0x%016llx 0x%016llx",
                                        (unsigned long long)fb[7], sub_ptr[0], sub_ptr[1], sub_ptr[2], sub_ptr[3]);
                            }
                        }
                        cur = p[3];
                        step++;
                    }
                }
            }

            struct kbase_ioctl_job_submit sub72 = {
                .addr = (uint64_t)(uintptr_t)atoms72,
                .nr_atoms = out_nr_atoms,
                .stride = 0x48
            };
            long ret = syscall(SYS_ioctl, fd, 0x40108002, &sub72);
            GPU_LOG("ioctl JOB_SUBMIT (0x40108002): nr_atoms=%u (orig=%u), stride 0x40->0x48, ret=%ld (errno=%d)",
                    out_nr_atoms, sub->nr_atoms, ret, ret < 0 ? errno : 0);
            return ret;
        }
        long ret = syscall(SYS_ioctl, fd, 0x40108002, arg);
        GPU_LOG("ioctl JOB_SUBMIT (0x40108002) directo: nr_atoms=%u, stride=0x%x, ret=%ld",
                sub->nr_atoms, sub->stride, ret);
        return ret;
    }

    /*
     * KBASE_IOCTL_SET_FLAGS = 0x40048001 (_IOW(0x80, 1, 4 bytes))
     * El kernel MediaTek MT6789 espera máscara segura 0x7a.
     */
    if (req == 0x40048001) {
        uint32_t flags = arg ? *(uint32_t *)arg : 0;
        uint32_t safe_flags = flags & 0x7a;
        long ret = syscall(SYS_ioctl, fd, 0x40048001, &safe_flags);
        GPU_LOG("ioctl SET_FLAGS (0x40048001): flags=0x%x -> safe=0x%x (ret=%ld)", flags, safe_flags, ret);
        if (ret < 0) return 0;
        return ret;
    }

    /*
     * KBASE_IOCTL_GET_GPUPROPS = 0x40108003 (_IOW(0x80, 3, 16 bytes))
     * El kernel MT6789 retorna prop_id=1 con 0x9093 y prop_id=55 con 0x90930010.
     * libmali busca en su tabla de arquitecturas Valhall el ID de Mali-G57 (0x9091).
     * Al recibir 0x9093, libmali emite "Unknown Product ID" y cae en fallback genérico Bifrost.
     * Parcheamos TLV prop_id=1 -> 0x9091 y prop_id=55 -> 0x90910010 para activar
     * el backend nativo Valhall Mali-G57 en libmali.
     */
    if (req == 0x40108003) {
        long ret = syscall(SYS_ioctl, fd, request, arg);
        if (ret >= 0 && arg) {
            struct {
                uint64_t buffer;
                uint32_t size;
                uint32_t flags;
            } *p = (void *)arg;
            if (p->buffer && p->size >= 4) {
                uint8_t *buf = (uint8_t *)(uintptr_t)p->buffer;
                uint32_t sz = p->size;
                uint32_t pos = 0;
                while (pos + 4 <= sz) {
                    uint32_t hdr = *(uint32_t *)(buf + pos);
                    uint32_t type = hdr & 0x3;
                    uint32_t prop_id = hdr >> 2;
                    static const int sz_table[4] = { 1, 2, 4, 8 };
                    int val_size = sz_table[type];
                    pos += 4;
                    if (pos + val_size > sz) break;
                    if (prop_id == 1 && val_size == 4) {
                        uint32_t *v = (uint32_t *)(buf + pos);
                        if (*v == 0x9093 || *v == 0x9091) {
                            GPU_LOG("ioctl GET_GPUPROPS: Parcheado prop_id=1 0x%x -> 0x9090 (Mali-G57/G77 Valhall)", *v);
                            *v = 0x9090;
                        }
                    } else if (prop_id == 55 && val_size == 4) {
                        uint32_t *v = (uint32_t *)(buf + pos);
                        if (*v == 0x90930010 || *v == 0x90910010) {
                            GPU_LOG("ioctl GET_GPUPROPS: Parcheado prop_id=55 0x%x -> 0x90900000 (Mali-G57/G77 Valhall)", *v);
                            *v = 0x90900000;
                        }
                    }
                    pos += val_size;
                }
            }
        }
        return ret;
    }

    if (req == 0xc0208005) {
        uint64_t in_p[4] = {0};
        if (arg) memcpy(in_p, arg, sizeof(in_p));
        long ret = syscall(SYS_ioctl, fd, request, arg);
        if (ret >= 0 && arg) {
            uint64_t out_p[4] = {0};
            memcpy(out_p, arg, sizeof(out_p));
            GPU_LOG("ioctl MEM_ALLOC (0xc0208005): ret=%ld, in_pages=%llu, in_flags=0x%llx -> out_flags=0x%llx, gpu_va=0x%llx",
                    ret, (unsigned long long)in_p[0], (unsigned long long)in_p[3],
                    (unsigned long long)out_p[0], (unsigned long long)out_p[1]);
        }
        return ret;
    }

    if (((req >> 8) & 0xFF) == 0x80) {
        long ret = syscall(SYS_ioctl, fd, request, arg);
        GPU_LOG("ioctl KBASE fd=%d cmd=0x%x (nr=%u, size=%u) -> ret=%ld (errno=%d)",
                fd, req, req & 0xFF, (req >> 16) & 0x3FFF, ret, ret < 0 ? errno : 0);
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
    int prime_fd;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
    uint64_t size;
    void *cpu_map;
};

static struct dumb_bo_entry g_dumb_table[MAX_DUMB_ENTRIES] = {0};
static pthread_mutex_t g_dumb_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_drm_fd = -1;

int is_active_dumb_handle(uint32_t handle) {
    if (!handle) return 0;
    pthread_mutex_lock(&g_dumb_mutex);
    for (int i = 0; i < MAX_DUMB_ENTRIES; i++) {
        if (g_dumb_table[i].bo && g_dumb_table[i].dumb_handle == handle) {
            pthread_mutex_unlock(&g_dumb_mutex);
            return 1;
        }
    }
    pthread_mutex_unlock(&g_dumb_mutex);
    return 0;
}

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

static void register_dumb_bo(struct gbm_bo *bo, uint32_t dumb_handle, int card_fd, int prime_fd,
                             uint32_t width, uint32_t height, uint32_t stride,
                             uint32_t format, uint64_t size, void *cpu_map) {
    pthread_mutex_lock(&g_dumb_mutex);
    for (int i = 0; i < MAX_DUMB_ENTRIES; i++) {
        if (g_dumb_table[i].bo == NULL) {
            g_dumb_table[i].bo = bo;
            g_dumb_table[i].dumb_handle = dumb_handle;
            g_dumb_table[i].card_fd = card_fd;
            g_dumb_table[i].prime_fd = prime_fd;
            g_dumb_table[i].width = width;
            g_dumb_table[i].height = height;
            g_dumb_table[i].stride = stride;
            g_dumb_table[i].format = format;
            g_dumb_table[i].size = size;
            g_dumb_table[i].cpu_map = cpu_map;
            pthread_mutex_unlock(&g_dumb_mutex);
            GPU_LOG("Registrado dumb bo %p (slot %d, handle %u, prime_fd %d, %ux%u, stride %u, map=%p)",
                    (void*)bo, i, dumb_handle, prime_fd, width, height, stride, cpu_map);
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

static struct dumb_bo_entry *find_dumb_bo_by_handle_locked(uint32_t handle) {
    if (!handle) return NULL;
    for (int i = 0; i < MAX_DUMB_ENTRIES; i++) {
        if (g_dumb_table[i].bo != NULL && g_dumb_table[i].dumb_handle == handle) {
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
            int prime_fd = g_dumb_table[i].prime_fd;
            g_dumb_table[i].bo = NULL;
            pthread_mutex_unlock(&g_dumb_mutex);

            if (prime_fd >= 0) {
                close(prime_fd);
            }
            /*
             * NOTA CRÍTICA DE HARDWARE MEDIATEK:
             * NUNCA destruir el dumb handle con DRM_IOCTL_MODE_DESTROY_DUMB mientras el CRTC
             * o el pipeline DSI/CMDQ esté activo. La destrucción prematura del dumb buffer en KMS
             * provoca que el DMA de MediaTek intente leer una página liberada, activando
             * una violación de protección IOMMU / device_apc_mt6789 y disparando el reinicio del SoC.
             * Los buffers KMS se liberan de forma segura por el kernel al cerrar card_fd.
             */
            kmsg_log("[GPU-GBM] Dumb BO desvinculado (handle=%u preservado en KMS para scanout seguro)\n", dumb_handle);
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
    /* Solo crear Dumb BO KMS si el buffer es solicitado explícitamente para scanout */
    if (!(flags & GBM_BO_USE_SCANOUT)) {
        return NULL;
    }

    int card_fd = -1;
    if (g_kms_fd >= 0) {
        card_fd = g_kms_fd;
    } else {
        card_fd = get_device_fd(gbm);
    }
    if (card_fd < 0) {
        card_fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
        if (card_fd >= 0) g_drm_fd = card_fd;
        kmsg_log("[GPU-GBM] Fallback open card0: fd=%d\n", card_fd);
    }
    kmsg_log("[GPU-GBM] create_scanout_dumb_bo: gbm=%p, card_fd=%d (kms_fd=%d), %ux%u format=0x%x flags=0x%x\n",
             (void*)gbm, card_fd, g_kms_fd, width, height, format, flags);
    if (card_fd < 0) {
        kmsg_log("[GPU-GBM] ERROR: No se detectó descriptor de card0 para crear dumb buffer\n", 0);
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
        kmsg_log("[GPU-GBM] ERROR: DRM_IOCTL_MODE_CREATE_DUMB falló (ret=%d, errno=%d)\n", ret, errno);
        return NULL;
    }

    int prime_fd = -1;
    ret = drmPrimeHandleToFD(card_fd, creq.handle, DRM_CLOEXEC | DRM_RDWR, &prime_fd);
    if (ret != 0 || prime_fd < 0) {
        kmsg_log("[GPU-GBM] ERROR: drmPrimeHandleToFD falló (ret=%d, errno=%d)\n", ret, errno);
        struct drm_mode_destroy_dumb dreq = {0};
        dreq.handle = creq.handle;
        drmIoctl(card_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
        return NULL;
    }

    int saved_prime_fd = fcntl(prime_fd, F_DUPFD_CLOEXEC, 0);

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

    kmsg_log("[GPU-GBM] Importando dumb buffer a Mali via 0x5504: prime_fd=%d, %ux%u stride=%u\n",
             prime_fd, width, height, creq.pitch);
    struct gbm_bo *bo = NULL;
    if (real_gbm_bo_import) {
        bo = real_gbm_bo_import(gbm, GBM_BO_IMPORT_FD_MODIFIER, &mod_data, flags | GBM_BO_USE_RENDERING);
    }
    close(prime_fd);

    if (!bo) {
        kmsg_log("[GPU-GBM] ERROR: real_gbm_bo_import falló (errno=%d)\n", errno);
        if (saved_prime_fd >= 0) close(saved_prime_fd);
        struct drm_mode_destroy_dumb dreq = {0};
        dreq.handle = creq.handle;
        drmIoctl(card_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
        return NULL;
    }

    void *cpu_map = NULL;
    struct drm_mode_map_dumb mreq = { .handle = creq.handle };
    if (drmIoctl(card_fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) == 0) {
        void *m = mmap(NULL, creq.size, PROT_READ | PROT_WRITE, MAP_SHARED, card_fd, mreq.offset);
        if (m != MAP_FAILED) {
            cpu_map = m;
        }
    }
    if (!cpu_map && saved_prime_fd >= 0) {
        void *m = mmap(NULL, creq.size, PROT_READ | PROT_WRITE, MAP_SHARED, saved_prime_fd, 0);
        if (m != MAP_FAILED) {
            cpu_map = m;
        }
    }

    register_dumb_bo(bo, creq.handle, card_fd, saved_prime_fd, width, height, creq.pitch, format, creq.size, cpu_map);
    kmsg_log("[GPU-GBM] Dumb BO registrado OK: bo=%p, handle=%u, prime_fd=%d, pitch=%u, size=%llu, map=%p\n",
             (void*)bo, creq.handle, saved_prime_fd, creq.pitch, (unsigned long long)creq.size, cpu_map);
    GPU_LOG("Dumb BO creado exitosamente: bo=%p, handle=%u, pitch=%u, size=%llu, map=%p",
            (void*)bo, creq.handle, creq.pitch, (unsigned long long)creq.size, cpu_map);

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

    struct gbm_bo *dumb_bo = create_scanout_dumb_bo(gbm, width, height, format, flags);
    if (dumb_bo) return dumb_bo;

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

    struct gbm_bo *dumb_bo = create_scanout_dumb_bo(gbm, width, height, format, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (dumb_bo) return dumb_bo;

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

    struct gbm_bo *dumb_bo = create_scanout_dumb_bo(gbm, width, height, format, flags);
    if (dumb_bo) return dumb_bo;

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

    if (real_gbm_bo_get_handle) {
        union gbm_bo_handle h = real_gbm_bo_get_handle(bo);
        int card_fd = (g_kms_fd >= 0) ? g_kms_fd : g_drm_fd;
        if (card_fd >= 0 && h.u32 > 0) {
            int test_fd = -1;
            if (drmPrimeHandleToFD(card_fd, h.u32, 0, &test_fd) == 0) {
                close(test_fd);
                return h;
            }
            if (real_gbm_bo_get_fd) {
                int pfd = real_gbm_bo_get_fd(bo);
                if (pfd >= 0) {
                    uint32_t kms_h = 0;
                    if (drmPrimeFDToHandle(card_fd, pfd, &kms_h) == 0) {
                        close(pfd);
                        h.u32 = kms_h;
                        return h;
                    }
                    close(pfd);
                }
            }
        }
        return h;
    }
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

static void filter_ext_string(char *str, const char *ext) {
    size_t len = strlen(ext);
    char *p;
    while ((p = strstr(str, ext)) != NULL) {
        memset(p, ' ', len);
    }
}

static const char *(*real_eglQueryString)(EGLDisplay, int) = NULL;
__attribute__((visibility("default")))
const char *eglQueryString(EGLDisplay dpy, int name) {
    if (!real_eglQueryString) {
        void *mali = dlopen("/usr/lib/libmali-bifrost-patched.so", RTLD_LAZY | RTLD_GLOBAL);
        if (!mali) mali = dlopen("/usr/lib/libmali-bifrost.so", RTLD_LAZY | RTLD_GLOBAL);
        if (mali) real_eglQueryString = dlsym(mali, "eglQueryString");
        if (!real_eglQueryString) real_eglQueryString = dlsym(RTLD_NEXT, "eglQueryString");
    }
    const char *orig = real_eglQueryString ? real_eglQueryString(dpy, name) : NULL;
    /* 0x3055 = EGL_EXTENSIONS: remover extensiones que causan asunciones de buffer age
     * y daño parcial en wlroots, forzando redibujado consistente y evitando glitches */
    if (name == 0x3055 && orig) {
        static char s_filtered_egl_exts[4096];
        strncpy(s_filtered_egl_exts, orig, sizeof(s_filtered_egl_exts) - 1);
        s_filtered_egl_exts[sizeof(s_filtered_egl_exts) - 1] = '\0';
        filter_ext_string(s_filtered_egl_exts, "EGL_WL_bind_wayland_display");
        filter_ext_string(s_filtered_egl_exts, "EGL_KHR_partial_update");
        filter_ext_string(s_filtered_egl_exts, "EGL_EXT_buffer_age");
        filter_ext_string(s_filtered_egl_exts, "EGL_KHR_swap_buffers_with_damage");
        filter_ext_string(s_filtered_egl_exts, "EGL_EXT_swap_buffers_with_damage");
        return s_filtered_egl_exts;
    }
    return orig;
}

EGLBoolean eglBindWaylandDisplayWL(EGLDisplay dpy, struct wl_display *wl_dpy) {
    (void)dpy; (void)wl_dpy;
    return 0; /* Retornar EGL_FALSE para que wlroots NO cree wl_drm y los clientes usen wl_shm */
}

EGLBoolean eglUnbindWaylandDisplayWL(EGLDisplay dpy, struct wl_display *wl_dpy) {
    (void)dpy; (void)wl_dpy;
    return 0;
}

static void (*real_glFinish)(void) = NULL;
static void (*real_glFlush)(void) = NULL;

void glFlush(void) {
    return;
}

void glFinish(void) {
    return;
}

__attribute__((visibility("default")))
unsigned int glGetError(void) {
    return 0; /* GL_NO_ERROR */
}

typedef unsigned int GLenum;
typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLuint;
typedef unsigned char GLboolean;
typedef float GLfloat;

/* ========================================================================= */
/* MOTOR RASTERIZADOR HÍBRIDO GLES2 + GPU FRAGMENT SCANOUT PARA VALHALL V9   */
/* ========================================================================= */

#define MAX_SHADOW_TEXTURES 2048
#define MAX_SHADOW_Programs 64
#define MAX_SHADOW_RBOS     64
#define MAX_SHADOW_FBOS     64
#define MAX_EGL_IMG_MAP     128

struct shadow_tex {
    int width;
    int height;
    GLenum format;
    uint32_t *pixels; /* Siempre almacenado como ARGB8888 (0xAARRGGBB en little-endian = BGRA bytes) */
    int has_alpha;
};

struct egl_img_map_entry {
    void *img;
    struct dumb_bo_entry *dumb;
};

struct shadow_prog {
    int is_tex;      /* 0=quad color, 1=tex rgba, 2=tex rgbx, 3=tex external */
    GLint loc_proj;
    GLint loc_tex_proj;
    GLint loc_color;
    GLint loc_alpha;
    GLint loc_tex;
    float proj[9];
    float tex_proj[9];
    float color[4];
    float alpha;
};

static struct shadow_tex g_shadow_tex[MAX_SHADOW_TEXTURES];
static struct egl_img_map_entry g_egl_img_map[MAX_EGL_IMG_MAP];
static struct dumb_bo_entry *g_rbo_dumb[MAX_SHADOW_RBOS];
static GLuint g_fbo_rbo[MAX_SHADOW_FBOS];
static GLuint g_fbo_tex[MAX_SHADOW_FBOS];
static struct shadow_prog g_progs[MAX_SHADOW_Programs];

static GLuint g_bound_tex = 0;
static GLuint g_bound_rbo = 0;
static GLuint g_bound_fbo = 0;
static GLuint g_current_prog = 0;
static GLint g_unpack_row_length = 0;
static GLint g_vp_x = 0, g_vp_y = 0, g_vp_w = 1200, g_vp_h = 1920;
static GLboolean g_scissor_enabled = 0;
static GLint g_sc_x = 0, g_sc_y = 0, g_sc_w = 1200, g_sc_h = 1920;
static GLboolean g_blend_enabled = 0;
static int g_pending_gpu_clear = 0;
static const float *g_attrib0_ptr = NULL;
static GLint g_attrib0_size = 2;
static GLsizei g_attrib0_stride = 0;

static void register_egl_image_dumb(void *img, struct dumb_bo_entry *dumb) {
    if (!img || !dumb) return;
    for (int i = 0; i < MAX_EGL_IMG_MAP; i++) {
        if (g_egl_img_map[i].img == img || g_egl_img_map[i].img == NULL) {
            g_egl_img_map[i].img = img;
            g_egl_img_map[i].dumb = dumb;
            return;
        }
    }
    g_egl_img_map[0].img = img;
    g_egl_img_map[0].dumb = dumb;
}

static struct dumb_bo_entry *find_egl_image_dumb(void *img) {
    if (!img) return NULL;
    for (int i = 0; i < MAX_EGL_IMG_MAP; i++) {
        if (g_egl_img_map[i].img == img) return g_egl_img_map[i].dumb;
    }
    return NULL;
}

static struct dumb_bo_entry *get_active_fbo_dumb(void) {
    if (g_bound_fbo > 0 && g_bound_fbo < MAX_SHADOW_FBOS) {
        GLuint rbo = g_fbo_rbo[g_bound_fbo];
        if (rbo > 0 && rbo < MAX_SHADOW_RBOS && g_rbo_dumb[rbo]) {
            return g_rbo_dumb[rbo];
        }
        /* En wlroots gles2, cada FBO de scanout (1, 2, 3...) corresponde secuencialmente a los dumb_bo de scanout (0, 1, 2...) */
        int idx = (int)g_bound_fbo - 1;
        if (idx >= 0 && idx < MAX_DUMB_ENTRIES && g_dumb_table[idx].bo && g_dumb_table[idx].cpu_map) {
            return &g_dumb_table[idx];
        }
    }
    /* Fallback: devolver el primer dumb_bo con cpu_map válido */
    for (int i = 0; i < MAX_DUMB_ENTRIES; i++) {
        if (g_dumb_table[i].bo && g_dumb_table[i].cpu_map) {
            return &g_dumb_table[i];
        }
    }
    return NULL;
}

static void *resolve_mali_gl(const char *name) {
    static void *mali = NULL;
    if (!mali) {
        mali = dlopen("/usr/lib/libmali-bifrost-patched.so", RTLD_LAZY | RTLD_GLOBAL);
        if (!mali) mali = dlopen("/usr/lib/libmali-bifrost.so", RTLD_LAZY | RTLD_GLOBAL);
    }
    void *fn = mali ? dlsym(mali, name) : NULL;
    if (!fn) fn = dlsym(RTLD_NEXT, name);
    return fn;
}

__attribute__((visibility("default")))
void glBindTexture(GLenum target, GLuint texture) {
    static void (*real_fn)(GLenum, GLuint) = NULL;
    if (!real_fn) real_fn = resolve_mali_gl("glBindTexture");
    if (texture < MAX_SHADOW_TEXTURES) g_bound_tex = texture;
    if (real_fn) real_fn(target, texture);
}

__attribute__((visibility("default")))
void glDeleteTextures(GLsizei n, const GLuint *textures) {
    static void (*real_fn)(GLsizei, const GLuint *) = NULL;
    if (!real_fn) real_fn = resolve_mali_gl("glDeleteTextures");
    if (textures) {
        for (GLsizei i = 0; i < n; i++) {
            GLuint t = textures[i];
            if (t > 0 && t < MAX_SHADOW_TEXTURES) {
                if (g_shadow_tex[t].pixels) {
                    free(g_shadow_tex[t].pixels);
                    g_shadow_tex[t].pixels = NULL;
                }
                g_shadow_tex[t].width = 0;
                g_shadow_tex[t].height = 0;
            }
        }
    }
    if (real_fn) real_fn(n, textures);
}

__attribute__((visibility("default")))
void glPixelStorei(GLenum pname, GLint param) {
    static void (*real_fn)(GLenum, GLint) = NULL;
    if (!real_fn) real_fn = resolve_mali_gl("glPixelStorei");
    if (pname == 0x0CF2 /* GL_UNPACK_ROW_LENGTH_EXT */) {
        g_unpack_row_length = param;
    }
    if (real_fn) real_fn(pname, param);
}

static void convert_and_store_tex_region(struct shadow_tex *st, int xoffset, int yoffset,
                                         int width, int height, GLenum format,
                                         const void *pixels, int row_len)
{
    if (!st || !st->pixels || !pixels) return;
    int src_stride_px = (row_len > 0) ? row_len : width;
    const uint32_t *src32 = (const uint32_t *)pixels;
    /* GL_BGRA_EXT = 0x80E1, GL_RGBA = 0x1908, GL_RGB = 0x1907 */
    int is_rgba = (format == 0x1908);

    if (!is_rgba) {
        /* Para formatos compatibles con ARGB/BGRA nativo: copia directa línea a línea vía memcpy */
        int copy_w = width;
        if (xoffset + copy_w > st->width) copy_w = st->width - xoffset;
        if (copy_w <= 0) return;
        size_t line_bytes = (size_t)copy_w * sizeof(uint32_t);

        for (int y = 0; y < height; y++) {
            int dy = yoffset + y;
            if (dy < 0 || dy >= st->height) continue;
            uint32_t *dst_row = st->pixels + dy * st->width + xoffset;
            const uint32_t *src_row = src32 + y * src_stride_px;
            memcpy(dst_row, src_row, line_bytes);
        }
        return;
    }

    for (int y = 0; y < height; y++) {
        int dy = yoffset + y;
        if (dy < 0 || dy >= st->height) continue;
        uint32_t *dst_row = st->pixels + dy * st->width;
        const uint32_t *src_row = src32 + y * src_stride_px;
        for (int x = 0; x < width; x++) {
            int dx = xoffset + x;
            if (dx < 0 || dx >= st->width) continue;
            uint32_t px = src_row[x];
            /* Convertir 0xAABBGGRR -> 0xAARRGGBB */
            uint32_t a = px & 0xFF000000u;
            uint32_t r = (px & 0x000000FFu) << 16;
            uint32_t g = (px & 0x0000FF00u);
            uint32_t b = (px & 0x00FF0000u) >> 16;
            dst_row[dx] = a | r | g | b;
        }
    }
}

__attribute__((visibility("default")))
void glTexImage2D(GLenum target, GLint level, GLint internalformat,
                  GLsizei width, GLsizei height, GLint border,
                  GLenum format, GLenum type, const void *pixels)
{
    static void (*real_fn)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *) = NULL;
    if (!real_fn) real_fn = resolve_mali_gl("glTexImage2D");

    if (level == 0 && g_bound_tex > 0 && g_bound_tex < MAX_SHADOW_TEXTURES && width > 0 && height > 0) {
        struct shadow_tex *st = &g_shadow_tex[g_bound_tex];
        if (st->width != width || st->height != height || !st->pixels) {
            free(st->pixels);
            st->pixels = (uint32_t *)calloc((size_t)width * (size_t)height, sizeof(uint32_t));
            st->width = width;
            st->height = height;
        }
        st->format = format;
        if (pixels && st->pixels) {
            convert_and_store_tex_region(st, 0, 0, width, height, format, pixels, g_unpack_row_length);
        }
        kmsg_always("[GPU-TEX] glTexImage2D tex=%u %dx%d fmt=0x%x row_len=%d pixels=%p\n",
                    g_bound_tex, width, height, format, g_unpack_row_length, pixels);
        return;
    }
    if (real_fn) real_fn(target, level, internalformat, width, height, border, format, type, pixels);
}

__attribute__((visibility("default")))
void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                     GLsizei width, GLsizei height, GLenum format, GLenum type,
                     const void *pixels)
{
    static void (*real_fn)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *) = NULL;
    if (!real_fn) real_fn = resolve_mali_gl("glTexSubImage2D");

    if (level == 0 && g_bound_tex > 0 && g_bound_tex < MAX_SHADOW_TEXTURES && pixels) {
        struct shadow_tex *st = &g_shadow_tex[g_bound_tex];
        if (st->pixels) {
            convert_and_store_tex_region(st, xoffset, yoffset, width, height, format, pixels, g_unpack_row_length);
        }
        return;
    }
    if (real_fn) real_fn(target, level, xoffset, yoffset, width, height, format, type, pixels);
}

__attribute__((visibility("default")))
void glBindRenderbuffer(GLenum target, GLuint renderbuffer) {
    static void (*real_fn)(GLenum, GLuint) = NULL;
    if (!real_fn) real_fn = resolve_mali_gl("glBindRenderbuffer");
    if (renderbuffer < MAX_SHADOW_RBOS) g_bound_rbo = renderbuffer;
    if (real_fn) real_fn(target, renderbuffer);
}

__attribute__((visibility("default")))
void glBindFramebuffer(GLenum target, GLuint framebuffer) {
    static void (*real_fn)(GLenum, GLuint) = NULL;
    if (!real_fn) real_fn = resolve_mali_gl("glBindFramebuffer");
    if (framebuffer < MAX_SHADOW_FBOS) g_bound_fbo = framebuffer;
    if (real_fn) real_fn(target, framebuffer);
}

__attribute__((visibility("default")))
void glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer) {
    static void (*real_fn)(GLenum, GLenum, GLenum, GLuint) = NULL;
    if (!real_fn) real_fn = resolve_mali_gl("glFramebufferRenderbuffer");
    if (g_bound_fbo > 0 && g_bound_fbo < MAX_SHADOW_FBOS && renderbuffer < MAX_SHADOW_RBOS) {
        g_fbo_rbo[g_bound_fbo] = renderbuffer;
    }
    if (real_fn) real_fn(target, attachment, renderbuffertarget, renderbuffer);
}

__attribute__((visibility("default")))
void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {
    static void (*real_fn)(GLenum, GLenum, GLenum, GLuint, GLint) = NULL;
    if (!real_fn) real_fn = resolve_mali_gl("glFramebufferTexture2D");
    if (g_bound_fbo > 0 && g_bound_fbo < MAX_SHADOW_FBOS && texture < MAX_SHADOW_TEXTURES) {
        g_fbo_tex[g_bound_fbo] = texture;
    }
    if (real_fn) real_fn(target, attachment, textarget, texture, level);
}

static void (*real_glEGLImageTargetRenderbufferStorageOES)(GLenum, void *) = NULL;
__attribute__((visibility("default")))
void glEGLImageTargetRenderbufferStorageOES(GLenum target, void *image) {
    if (!real_glEGLImageTargetRenderbufferStorageOES) {
        real_glEGLImageTargetRenderbufferStorageOES = resolve_mali_gl("glEGLImageTargetRenderbufferStorageOES");
    }
    if (g_bound_rbo > 0 && g_bound_rbo < MAX_SHADOW_RBOS) {
        struct dumb_bo_entry *d = find_egl_image_dumb(image);
        if (d) g_rbo_dumb[g_bound_rbo] = d;
    }
    if (real_glEGLImageTargetRenderbufferStorageOES) {
        real_glEGLImageTargetRenderbufferStorageOES(target, image);
    }
}

static void (*real_glEGLImageTargetTexture2DOES)(GLenum, void *) = NULL;
__attribute__((visibility("default")))
void glEGLImageTargetTexture2DOES(GLenum target, void *image) {
    if (!real_glEGLImageTargetTexture2DOES) {
        real_glEGLImageTargetTexture2DOES = resolve_mali_gl("glEGLImageTargetTexture2DOES");
    }
    if (g_bound_tex > 0 && g_bound_tex < MAX_SHADOW_TEXTURES) {
        struct dumb_bo_entry *d = find_egl_image_dumb(image);
        if (d && d->cpu_map && d->width > 0 && d->height > 0) {
            struct shadow_tex *st = &g_shadow_tex[g_bound_tex];
            if (st->width != (int)d->width || st->height != (int)d->height || !st->pixels) {
                free(st->pixels);
                st->pixels = (uint32_t *)calloc((size_t)d->width * (size_t)d->height, sizeof(uint32_t));
                st->width = (int)d->width;
                st->height = (int)d->height;
            }
            if (st->pixels) {
                convert_and_store_tex_region(st, 0, 0, (int)d->width, (int)d->height,
                                             0x80E1 /* BGRA */, d->cpu_map, (int)(d->stride / 4));
            }
        }
    }
    if (real_glEGLImageTargetTexture2DOES) {
        real_glEGLImageTargetTexture2DOES(target, image);
    }
}

__attribute__((visibility("default")))
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    static void (*real_fn)(GLint, GLint, GLsizei, GLsizei) = NULL;
    if (!real_fn) real_fn = resolve_mali_gl("glViewport");
    g_vp_x = x; g_vp_y = y; g_vp_w = width; g_vp_h = height;
    if (real_fn) real_fn(x, y, width, height);
}

__attribute__((visibility("default")))
void glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    static void (*real_fn)(GLint, GLint, GLsizei, GLsizei) = NULL;
    if (!real_fn) real_fn = resolve_mali_gl("glScissor");
    g_sc_x = x; g_sc_y = y; g_sc_w = width; g_sc_h = height;
    if (real_fn) real_fn(x, y, width, height);
}

__attribute__((visibility("default")))
void glEnable(GLenum cap) {
    static void (*real_fn)(GLenum) = NULL;
    if (!real_fn) real_fn = resolve_mali_gl("glEnable");
    if (cap == 0x0C11 /* GL_SCISSOR_TEST */) g_scissor_enabled = 1;
    else if (cap == 0x0BE2 /* GL_BLEND */) g_blend_enabled = 1;
    if (real_fn) real_fn(cap);
}

__attribute__((visibility("default")))
void glDisable(GLenum cap) {
    static void (*real_fn)(GLenum) = NULL;
    if (!real_fn) real_fn = resolve_mali_gl("glDisable");
    if (cap == 0x0C11 /* GL_SCISSOR_TEST */) g_scissor_enabled = 0;
    else if (cap == 0x0BE2 /* GL_BLEND */) g_blend_enabled = 0;
    if (real_fn) real_fn(cap);
}

static GLuint g_last_fs_type = 0;
__attribute__((visibility("default")))
void glShaderSource(GLuint shader, GLsizei count, const char *const*string, const GLint *length) {
    static void (*real_fn)(GLuint, GLsizei, const char *const*, const GLint *) = NULL;
    if (!real_fn) real_fn = resolve_mali_gl("glShaderSource");
    if (count > 0 && string && string[0]) {
        const char *s = string[0];
        if (strstr(s, "gl_FragColor = color")) g_last_fs_type = 0;
        else if (strstr(s, "vec4(texture2D(tex, v_texcoord).rgb, 1.0)")) g_last_fs_type = 2;
        else if (strstr(s, "texture2D(tex, v_texcoord)")) g_last_fs_type = 1;
        else if (strstr(s, "samplerExternalOES")) g_last_fs_type = 3;
    }
    if (real_fn) real_fn(shader, count, string, length);
}

__attribute__((visibility("default")))
void glLinkProgram(GLuint program) {
    static void (*real_fn)(GLuint) = NULL;
    if (!real_fn) real_fn = resolve_mali_gl("glLinkProgram");
    if (real_fn) real_fn(program);
    if (program > 0 && program < MAX_SHADOW_Programs) {
        g_progs[program].is_tex = g_last_fs_type;
        g_progs[program].alpha = 1.0f;
        /* Identidad por defecto para tex_proj */
        memset(g_progs[program].tex_proj, 0, sizeof(g_progs[program].tex_proj));
        g_progs[program].tex_proj[0] = 1.0f;
        g_progs[program].tex_proj[4] = 1.0f;
        g_progs[program].tex_proj[8] = 1.0f;
    }
}

__attribute__((visibility("default")))
GLint glGetUniformLocation(GLuint program, const char *name) {
    static GLint (*real_fn)(GLuint, const char *) = NULL;
    if (!real_fn) real_fn = resolve_mali_gl("glGetUniformLocation");
    GLint loc = real_fn ? real_fn(program, name) : -1;
    if (program > 0 && program < MAX_SHADOW_Programs && name) {
        if (strcmp(name, "proj") == 0) g_progs[program].loc_proj = loc;
        else if (strcmp(name, "tex_proj") == 0) g_progs[program].loc_tex_proj = loc;
        else if (strcmp(name, "color") == 0) g_progs[program].loc_color = loc;
        else if (strcmp(name, "alpha") == 0) g_progs[program].loc_alpha = loc;
        else if (strcmp(name, "tex") == 0) g_progs[program].loc_tex = loc;
    }
    return loc;
}

__attribute__((visibility("default")))
void glUseProgram(GLuint program) {
    static void (*real_fn)(GLuint) = NULL;
    if (!real_fn) real_fn = resolve_mali_gl("glUseProgram");
    if (program < MAX_SHADOW_Programs) g_current_prog = program;
    if (real_fn) real_fn(program);
}

__attribute__((visibility("default")))
void glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
    static void (*real_fn)(GLint, GLsizei, GLboolean, const GLfloat *) = NULL;
    if (!real_fn) real_fn = resolve_mali_gl("glUniformMatrix3fv");
    if (g_current_prog > 0 && g_current_prog < MAX_SHADOW_Programs && value) {
        struct shadow_prog *sp = &g_progs[g_current_prog];
        float m[9];
        if (transpose) {
            /* Si transpose=GL_TRUE, value ya está en orden por filas; si GL_FALSE, value es column-major */
            memcpy(m, value, 9 * sizeof(float));
        } else {
            /* En GLES2 transpose siempre es GL_FALSE: column-major en memoria.
             * En el shader de wlroots: vec3(x,y,1) * mat3 -> multiplica vector fila por columnas de mat3!
             * Específicamente: (v * M).x = dot(v, col0) = x*v[0] + y*v[1] + 1*v[2]
             *                  (v * M).y = dot(v, col1) = x*v[3] + y*v[4] + 1*v[5] */
            memcpy(m, value, 9 * sizeof(float));
        }
        if (location == sp->loc_proj) memcpy(sp->proj, m, sizeof(m));
        else if (location == sp->loc_tex_proj) memcpy(sp->tex_proj, m, sizeof(m));
    }
    if (real_fn) real_fn(location, count, transpose, value);
}

__attribute__((visibility("default")))
void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) {
    static void (*real_fn)(GLint, GLfloat, GLfloat, GLfloat, GLfloat) = NULL;
    if (!real_fn) real_fn = resolve_mali_gl("glUniform4f");
    if (g_current_prog > 0 && g_current_prog < MAX_SHADOW_Programs) {
        struct shadow_prog *sp = &g_progs[g_current_prog];
        if (location == sp->loc_color) {
            sp->color[0] = v0; sp->color[1] = v1; sp->color[2] = v2; sp->color[3] = v3;
        }
    }
    if (real_fn) real_fn(location, v0, v1, v2, v3);
}

__attribute__((visibility("default")))
void glUniform1f(GLint location, GLfloat v0) {
    static void (*real_fn)(GLint, GLfloat) = NULL;
    if (!real_fn) real_fn = resolve_mali_gl("glUniform1f");
    if (g_current_prog > 0 && g_current_prog < MAX_SHADOW_Programs) {
        struct shadow_prog *sp = &g_progs[g_current_prog];
        if (location == sp->loc_alpha) sp->alpha = v0;
    }
    if (real_fn) real_fn(location, v0);
}

__attribute__((visibility("default")))
void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized,
                           GLsizei stride, const void *pointer)
{
    static void (*real_fn)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *) = NULL;
    if (!real_fn) real_fn = resolve_mali_gl("glVertexAttribPointer");
    if (index == 0) {
        g_attrib0_ptr = (const float *)pointer;
        g_attrib0_size = size;
        g_attrib0_stride = stride;
    }
    if (real_fn) real_fn(index, size, type, normalized, stride, pointer);
}

__attribute__((visibility("default")))
void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                  GLenum format, GLenum type, void *pixels)
{
    static void (*real_fn)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *) = NULL;
    if (!real_fn) real_fn = resolve_mali_gl("glReadPixels");
    if (g_pending_gpu_clear) {
        glFinish();
        g_pending_gpu_clear = 0;
    }
    struct dumb_bo_entry *d = get_active_fbo_dumb();
    if (d && d->cpu_map && pixels && width > 0 && height > 0) {
        int stride_px = (int)(d->stride / 4);
        const uint32_t *src_base = (const uint32_t *)d->cpu_map;
        if (format == 0x1907 /* GL_RGB */) {
            uint8_t *dst8 = (uint8_t *)pixels;
            for (int row = 0; row < height; row++) {
                int sy = y + row;
                if (sy < 0 || sy >= (int)d->height) continue;
                const uint32_t *src_row = src_base + sy * stride_px;
                uint8_t *dst_row = dst8 + row * width * 3;
                for (int col = 0; col < width; col++) {
                    int sx = x + col;
                    if (sx < 0 || sx >= (int)d->width) continue;
                    uint32_t px = src_row[sx]; /* 0xAARRGGBB */
                    dst_row[col * 3 + 0] = (px >> 16) & 0xFFu; /* R */
                    dst_row[col * 3 + 1] = (px >> 8)  & 0xFFu; /* G */
                    dst_row[col * 3 + 2] = (px)       & 0xFFu; /* B */
                }
            }
            return;
        }

        uint32_t *dst32 = (uint32_t *)pixels;
        int is_rgba = (format == 0x1908 /* GL_RGBA */);
        for (int row = 0; row < height; row++) {
            int sy = y + row;
            if (sy < 0 || sy >= (int)d->height) continue;
            const uint32_t *src_row = src_base + sy * stride_px;
            uint32_t *dst_row = dst32 + row * width;
            for (int col = 0; col < width; col++) {
                int sx = x + col;
                if (sx < 0 || sx >= (int)d->width) continue;
                uint32_t px = src_row[sx]; /* 0xAARRGGBB */
                if (is_rgba) {
                    uint32_t a = px & 0xFF000000u;
                    uint32_t r = (px >> 16) & 0xFFu;
                    uint32_t g = (px & 0x0000FF00u);
                    uint32_t b = (px & 0xFFu) << 16;
                    px = a | b | g | r;
                }
                dst_row[col] = px;
            }
        }
        return;
    }
    if (real_fn) real_fn(x, y, width, height, format, type, pixels);
}

static __thread int s_draw_call_counter = 0;
static void (*real_glDrawArrays)(GLenum, GLint, GLsizei) = NULL;
static void (*real_glDrawElements)(GLenum, GLsizei, GLenum, const void *) = NULL;
static float g_clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};

__attribute__((visibility("default")))
void glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
    static void (*real_fn)(GLfloat, GLfloat, GLfloat, GLfloat) = NULL;
    if (!real_fn) real_fn = resolve_mali_gl("glClearColor");
    g_clear_color[0] = red;
    g_clear_color[1] = green;
    g_clear_color[2] = blue;
    g_clear_color[3] = alpha;
    if (real_fn) real_fn(red, green, blue, alpha);
}

static inline uint32_t pack_argb(float r, float g, float b, float a) {
    int ir = (int)(r * 255.0f + 0.5f); if (ir < 0) ir = 0; else if (ir > 255) ir = 255;
    int ig = (int)(g * 255.0f + 0.5f); if (ig < 0) ig = 0; else if (ig > 255) ig = 255;
    int ib = (int)(b * 255.0f + 0.5f); if (ib < 0) ib = 0; else if (ib > 255) ib = 255;
    int ia = (int)(a * 255.0f + 0.5f); if (ia < 0) ia = 0; else if (ia > 255) ia = 255;
    return ((uint32_t)ia << 24) | ((uint32_t)ir << 16) | ((uint32_t)ig << 8) | (uint32_t)ib;
}

static inline uint32_t blend_premult_argb(uint32_t src, uint32_t dst) {
    uint32_t sa = (src >> 24) & 0xFFu;
    if (sa == 255) return src | 0xFF000000u;
    if (sa == 0) return dst;
    uint32_t inv_a = 255u - sa;
    uint32_t sr = (src >> 16) & 0xFFu;
    uint32_t sg = (src >> 8) & 0xFFu;
    uint32_t sb = src & 0xFFu;
    uint32_t dr = (dst >> 16) & 0xFFu;
    uint32_t dg = (dst >> 8) & 0xFFu;
    uint32_t db = dst & 0xFFu;
    uint32_t out_r = sr + ((dr * inv_a) >> 8); if (out_r > 255u) out_r = 255u;
    uint32_t out_g = sg + ((dg * inv_a) >> 8); if (out_g > 255u) out_g = 255u;
    uint32_t out_b = sb + ((db * inv_a) >> 8); if (out_b > 255u) out_b = 255u;
    return 0xFF000000u | (out_r << 16) | (out_g << 8) | out_b;
}

/* Rutina NEON ultra-rápida de mezcla premultiplicada para 4 píxeles ARGB a la vez */
static inline void blend_row_neon(uint32_t *dst, const uint32_t *src, int count) {
    int i = 0;
    static const uint8_t s_dup_alpha_idx[16] = {
        3, 3, 3, 3,
        7, 7, 7, 7,
        11, 11, 11, 11,
        15, 15, 15, 15
    };
    uint8x16_t v_idx = vld1q_u8(s_dup_alpha_idx);
    uint8x16_t v_255 = vdupq_n_u8(255);
    static const uint8_t s_alpha_mask[16] = {
        0, 0, 0, 0xFF,
        0, 0, 0, 0xFF,
        0, 0, 0, 0xFF,
        0, 0, 0, 0xFF
    };
    uint8x16_t v_amask = vld1q_u8(s_alpha_mask);

    for (; i <= count - 4; i += 4) {
        uint8x16_t vsrc = vld1q_u8((const uint8_t *)(src + i));
        uint8x16_t vdst = vld1q_u8((const uint8_t *)(dst + i));

        uint8x16_t valpha = vqtbl1q_u8(vsrc, v_idx);
        uint8x16_t vinv_a = vsubq_u8(v_255, valpha);

        uint16x8_t prod_lo = vmull_u8(vget_low_u8(vdst), vget_low_u8(vinv_a));
        uint16x8_t prod_hi = vmull_u8(vget_high_u8(vdst), vget_high_u8(vinv_a));

        uint8x8_t scaled_lo = vshrn_n_u16(prod_lo, 8);
        uint8x8_t scaled_hi = vshrn_n_u16(prod_hi, 8);
        uint8x16_t scaled_dst = vcombine_u8(scaled_lo, scaled_hi);

        uint8x16_t res = vqaddq_u8(vsrc, scaled_dst);
        res = vorrq_u8(res, v_amask);

        vst1q_u8((uint8_t *)(dst + i), res);
    }
    for (; i < count; i++) {
        dst[i] = blend_premult_argb(src[i], dst[i]);
    }
}

/* Rutina NEON de mezcla con escalado global de opacidad (alpha_scale) */
static inline void blend_row_neon_alpha(uint32_t *dst, const uint32_t *src, int count, uint32_t alpha_scale) {
    int i = 0;
    static const uint8_t s_dup_alpha_idx[16] = {
        3, 3, 3, 3,
        7, 7, 7, 7,
        11, 11, 11, 11,
        15, 15, 15, 15
    };
    uint8x16_t v_idx = vld1q_u8(s_dup_alpha_idx);
    uint8x16_t v_255 = vdupq_n_u8(255);
    static const uint8_t s_alpha_mask[16] = {
        0, 0, 0, 0xFF,
        0, 0, 0, 0xFF,
        0, 0, 0, 0xFF,
        0, 0, 0, 0xFF
    };
    uint8x16_t v_amask = vld1q_u8(s_alpha_mask);
    uint8x8_t v_ascale = vdup_n_u8((uint8_t)alpha_scale);

    for (; i <= count - 4; i += 4) {
        uint8x16_t vsrc_raw = vld1q_u8((const uint8_t *)(src + i));
        uint16x8_t s_lo = vmull_u8(vget_low_u8(vsrc_raw), v_ascale);
        uint16x8_t s_hi = vmull_u8(vget_high_u8(vsrc_raw), v_ascale);
        uint8x16_t vsrc = vcombine_u8(vshrn_n_u16(s_lo, 8), vshrn_n_u16(s_hi, 8));

        uint8x16_t vdst = vld1q_u8((const uint8_t *)(dst + i));
        uint8x16_t valpha = vqtbl1q_u8(vsrc, v_idx);
        uint8x16_t vinv_a = vsubq_u8(v_255, valpha);

        uint16x8_t prod_lo = vmull_u8(vget_low_u8(vdst), vget_low_u8(vinv_a));
        uint16x8_t prod_hi = vmull_u8(vget_high_u8(vdst), vget_high_u8(vinv_a));

        uint8x8_t scaled_lo = vshrn_n_u16(prod_lo, 8);
        uint8x8_t scaled_hi = vshrn_n_u16(prod_hi, 8);
        uint8x16_t scaled_dst = vcombine_u8(scaled_lo, scaled_hi);

        uint8x16_t res = vqaddq_u8(vsrc, scaled_dst);
        res = vorrq_u8(res, v_amask);

        vst1q_u8((uint8_t *)(dst + i), res);
    }
    for (; i < count; i++) {
        uint32_t spx = src[i];
        uint32_t sa = (((spx >> 24) & 0xFFu) * alpha_scale) >> 8;
        uint32_t sr = (((spx >> 16) & 0xFFu) * alpha_scale) >> 8;
        uint32_t sg = (((spx >> 8) & 0xFFu) * alpha_scale) >> 8;
        uint32_t sb = ((spx & 0xFFu) * alpha_scale) >> 8;
        spx = (sa << 24) | (sr << 16) | (sg << 8) | sb;
        dst[i] = blend_premult_argb(spx, dst[i]);
    }
}

/* Rutina NEON para copia invertida en X a 4 píxeles por ciclo */
static inline void copy_row_neon_rev(uint32_t *dst, const uint32_t *src_end, int count) {
    static const uint8_t s_rev_table[16] = {
        12, 13, 14, 15,
         8,  9, 10, 11,
         4,  5,  6,  7,
         0,  1,  2,  3
    };
    uint8x16_t rev_mask = vld1q_u8(s_rev_table);
    static const uint8_t s_alpha_mask[16] = {
        0, 0, 0, 0xFF,
        0, 0, 0, 0xFF,
        0, 0, 0, 0xFF,
        0, 0, 0, 0xFF
    };
    uint8x16_t v_amask = vld1q_u8(s_alpha_mask);

    int i = 0;
    for (; i <= count - 4; i += 4) {
        const uint32_t *s = src_end - (i + 4);
        uint8x16_t v = vld1q_u8((const uint8_t *)s);
        uint8x16_t r = vqtbl1q_u8(v, rev_mask);
        r = vorrq_u8(r, v_amask);
        vst1q_u8((uint8_t *)(dst + i), r);
    }
    for (; i < count; i++) {
        dst[i] = *(src_end - 1 - i) | 0xFF000000u;
    }
}

/* Rutina NEON para mezcla premultiplicada invertida en X a 4 píxeles por ciclo */
static inline void blend_row_neon_rev(uint32_t *dst, const uint32_t *src_end, int count) {
    static const uint8_t s_rev_table[16] = {
        12, 13, 14, 15,
         8,  9, 10, 11,
         4,  5,  6,  7,
         0,  1,  2,  3
    };
    uint8x16_t rev_mask = vld1q_u8(s_rev_table);
    static const uint8_t s_dup_alpha_idx[16] = {
        3, 3, 3, 3,
        7, 7, 7, 7,
        11, 11, 11, 11,
        15, 15, 15, 15
    };
    uint8x16_t v_idx = vld1q_u8(s_dup_alpha_idx);
    uint8x16_t v_255 = vdupq_n_u8(255);
    static const uint8_t s_alpha_mask[16] = {
        0, 0, 0, 0xFF,
        0, 0, 0, 0xFF,
        0, 0, 0, 0xFF,
        0, 0, 0, 0xFF
    };
    uint8x16_t v_amask = vld1q_u8(s_alpha_mask);

    int i = 0;
    for (; i <= count - 4; i += 4) {
        const uint32_t *s = src_end - (i + 4);
        uint8x16_t vraw = vld1q_u8((const uint8_t *)s);
        uint8x16_t vsrc = vqtbl1q_u8(vraw, rev_mask);
        uint8x16_t vdst = vld1q_u8((const uint8_t *)(dst + i));

        uint8x16_t valpha = vqtbl1q_u8(vsrc, v_idx);
        uint8x16_t vinv_a = vsubq_u8(v_255, valpha);

        uint16x8_t prod_lo = vmull_u8(vget_low_u8(vdst), vget_low_u8(vinv_a));
        uint16x8_t prod_hi = vmull_u8(vget_high_u8(vdst), vget_high_u8(vinv_a));

        uint8x8_t scaled_lo = vshrn_n_u16(prod_lo, 8);
        uint8x8_t scaled_hi = vshrn_n_u16(prod_hi, 8);
        uint8x16_t scaled_dst = vcombine_u8(scaled_lo, scaled_hi);

        uint8x16_t res = vqaddq_u8(vsrc, scaled_dst);
        res = vorrq_u8(res, v_amask);

        vst1q_u8((uint8_t *)(dst + i), res);
    }
    for (; i < count; i++) {
        dst[i] = blend_premult_argb(*(src_end - 1 - i), dst[i]);
    }
}

__attribute__((visibility("default")))
void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    (void)mode;
    s_draw_call_counter++;
    struct dumb_bo_entry *d = get_active_fbo_dumb();
    if (s_draw_call_counter <= 25) {
        kmsg_always("[GPU-DRAW #%d] fbo=%u, dumb=%p (map=%p), prog=%u (is_tex=%d), tex=%u (%dx%d, px=%p), vp=%dx%d, ptr=%p\n",
                    s_draw_call_counter, g_bound_fbo, (void*)d, d ? d->cpu_map : NULL,
                    g_current_prog, (g_current_prog < MAX_SHADOW_Programs) ? g_progs[g_current_prog].is_tex : -1,
                    g_bound_tex, (g_bound_tex < MAX_SHADOW_TEXTURES) ? g_shadow_tex[g_bound_tex].width : -1,
                    (g_bound_tex < MAX_SHADOW_TEXTURES) ? g_shadow_tex[g_bound_tex].height : -1,
                    (g_bound_tex < MAX_SHADOW_TEXTURES) ? (void*)g_shadow_tex[g_bound_tex].pixels : NULL,
                    g_vp_w, g_vp_h, (const void*)g_attrib0_ptr);
    }
    if (!d || !d->cpu_map || g_current_prog == 0 || g_current_prog >= MAX_SHADOW_Programs) {
        return;
    }

    struct shadow_prog *sp = &g_progs[g_current_prog];
    if (sp->is_tex > 0 && g_bound_tex > 1 && s_draw_call_counter <= 500) {
        kmsg_always("[GPU-DRAW-WIN #%d] fbo=%u prog=%u(is_tex=%d) tex=%u (%dx%d) alpha=%.2f\n",
                    s_draw_call_counter, g_bound_fbo, g_current_prog, sp->is_tex,
                    g_bound_tex,
                    (g_bound_tex < MAX_SHADOW_TEXTURES) ? g_shadow_tex[g_bound_tex].width : -1,
                    (g_bound_tex < MAX_SHADOW_TEXTURES) ? g_shadow_tex[g_bound_tex].height : -1,
                    sp->alpha);
    }
    int fb_w = (int)d->width;
    int fb_h = (int)d->height;
    int fb_stride_px = (int)(d->stride / 4);
    uint32_t *fb_px = (uint32_t *)d->cpu_map;

    /* Coordenadas de entrada de los 4 vértices del quad en wlroots GLES2:
     * quad_verts = { 0, 0,  0, 1,  1, 0,  1, 1 } */
    const float vx[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
    const float vy[4] = { 0.0f, 1.0f, 0.0f, 1.0f };

    float sx[4], sy[4], u[4], v[4];
    float min_sx = 1e9f, max_sx = -1e9f, min_sy = 1e9f, max_sy = -1e9f;
    for (int i = 0; i < 4; i++) {
        float ndc_x = vx[i] * sp->proj[0] + vy[i] * sp->proj[1] + sp->proj[2];
        float ndc_y = vx[i] * sp->proj[3] + vy[i] * sp->proj[4] + sp->proj[5];
        sx[i] = (float)g_vp_x + (ndc_x + 1.0f) * 0.5f * (float)g_vp_w;
        sy[i] = (float)g_vp_y + (ndc_y + 1.0f) * 0.5f * (float)g_vp_h;
        u[i]  = vx[i] * sp->tex_proj[0] + vy[i] * sp->tex_proj[1] + sp->tex_proj[2];
        v[i]  = vx[i] * sp->tex_proj[3] + vy[i] * sp->tex_proj[4] + sp->tex_proj[5];
        if (sx[i] < min_sx) min_sx = sx[i];
        if (sx[i] > max_sx) max_sx = sx[i];
        if (sy[i] < min_sy) min_sy = sy[i];
        if (sy[i] > max_sy) max_sy = sy[i];
    }

    int x0 = (int)(min_sx + 0.5f);
    int x1 = (int)(max_sx + 0.5f);
    int y0 = (int)(min_sy + 0.5f);
    int y1 = (int)(max_sy + 0.5f);
    if (s_draw_call_counter <= 60) {
        kmsg_always("  [QUAD #%d] prog=%u(tex=%d) tex=%u box=(%d,%d)-(%d,%d) sc=%d(%d,%d %dx%d) u=(%.2f,%.2f,%.2f,%.2f) v=(%.2f,%.2f,%.2f,%.2f)\n",
                    s_draw_call_counter, g_current_prog, sp->is_tex, g_bound_tex,
                    x0, y0, x1, y1, g_scissor_enabled, g_sc_x, g_sc_y, g_sc_w, g_sc_h,
                    u[0], u[1], u[2], u[3], v[0], v[1], v[2], v[3]);
    }

    /* Aplicar recorte de Scissor y límites del Framebuffer */
    int clip_x0 = 0, clip_y0 = 0, clip_x1 = fb_w, clip_y1 = fb_h;
    if (g_scissor_enabled) {
        if (g_sc_x > clip_x0) clip_x0 = g_sc_x;
        if (g_sc_y > clip_y0) clip_y0 = g_sc_y;
        if (g_sc_x + g_sc_w < clip_x1) clip_x1 = g_sc_x + g_sc_w;
        if (g_sc_y + g_sc_h < clip_y1) clip_y1 = g_sc_y + g_sc_h;
    }

    int rx0 = (x0 > clip_x0) ? x0 : clip_x0;
    int ry0 = (y0 > clip_y0) ? y0 : clip_y0;
    int rx1 = (x1 < clip_x1) ? x1 : clip_x1;
    int ry1 = (y1 < clip_y1) ? y1 : clip_y1;
    if (rx0 >= rx1 || ry0 >= ry1) return;

    if (sp->is_tex == 0) {
        /* Quad de color sólido optimizado */
        uint32_t col = pack_argb(sp->color[0], sp->color[1], sp->color[2], sp->color[3]);
        int do_blend = (g_blend_enabled && ((col >> 24) < 255));
        int count = rx1 - rx0;
        if (count <= 0) return;

        if (!do_blend) {
            uint32_t opaque_col = col | 0xFF000000u;
            uint32x4_t vcol = vdupq_n_u32(opaque_col);
            for (int py = ry0; py < ry1; py++) {
                uint32_t *dst_row = fb_px + py * fb_stride_px + rx0;
                int x = 0;
                for (; x <= count - 4; x += 4) {
                    vst1q_u32(dst_row + x, vcol);
                }
                for (; x < count; x++) {
                    dst_row[x] = opaque_col;
                }
            }
        } else {
            for (int py = ry0; py < ry1; py++) {
                uint32_t *dst_row = fb_px + py * fb_stride_px;
                for (int px = rx0; px < rx1; px++) {
                    dst_row[px] = blend_premult_argb(col, dst_row[px]);
                }
            }
        }
        return;
    }

    /* Quad con textura */
    if (g_bound_tex == 0 || g_bound_tex >= MAX_SHADOW_TEXTURES) return;
    struct shadow_tex *st = &g_shadow_tex[g_bound_tex];
    if (!st->pixels || st->width <= 0 || st->height <= 0) return;

    float quad_w = (max_sx - min_sx);
    float quad_h = (max_sy - min_sy);
    if (quad_w < 1.0f || quad_h < 1.0f) return;

    float min_u = 1e9f, max_u = -1e9f, min_v = 1e9f, max_v = -1e9f;
    for (int i = 0; i < 4; i++) {
        if (u[i] < min_u) min_u = u[i];
        if (u[i] > max_u) max_u = u[i];
        if (v[i] < min_v) min_v = v[i];
        if (v[i] > max_v) max_v = v[i];
    }
    if (max_u - min_u < 0.001f) { min_u = 0.0f; max_u = 1.0f; }
    if (max_v - min_v < 0.001f) { min_v = 0.0f; max_v = 1.0f; }
    float u_left = min_u, u_right = max_u;
    float v_top = min_v,  v_bottom = max_v;
    if (sx[0] > sx[2]) { float tmp = u_left; u_left = u_right; u_right = tmp; }
    if (sy[0] > sy[1]) { float tmp = v_top; v_top = v_bottom; v_bottom = tmp; }

    int tw = st->width;
    int th = st->height;
    int is_opaque_shader = (sp->is_tex == 2) || !g_blend_enabled;
    uint32_t alpha_scale = (uint32_t)(sp->alpha * 256.0f);
    if (alpha_scale > 256u) alpha_scale = 256u;

    /* Detección de coincidencia 1:1 directa (muy común en Wayland: fondos, ventanas, capas, rotaciones) */
    int is_1to1 = (fabsf(quad_w - (float)tw) < 1.0f && fabsf(quad_h - (float)th) < 1.0f &&
                   fabsf(fabsf(u_right - u_left) - 1.0f) < 0.02f &&
                   fabsf(fabsf(v_bottom - v_top) - 1.0f) < 0.02f);

    if (is_1to1) {
        int base_x = (int)(min_sx + 0.5f);
        int base_y = (int)(min_sy + 0.5f);
        int rev_x = (u_left > u_right);
        int rev_y = (v_top > v_bottom);

        for (int py = ry0; py < ry1; py++) {
            int row_rel = py - base_y;
            if (row_rel < 0 || row_rel >= th) continue;
            int ty = rev_y ? (th - 1 - row_rel) : row_rel;
            const uint32_t *src_row = st->pixels + ty * tw;
            uint32_t *dst_row = fb_px + py * fb_stride_px;

            int tx_start = rx0 - base_x;
            int count = rx1 - rx0;
            if (tx_start < 0) {
                count += tx_start;
                rx0 -= tx_start;
                tx_start = 0;
            }
            if (tx_start + count > tw) count = tw - tx_start;
            if (count <= 0) continue;

            if (!rev_x) {
                /* Dirección X normal */
                if (is_opaque_shader && alpha_scale >= 255u) {
                    /* Fast-path instantáneo memcpy: ~0.3ms por frame completo */
                    memcpy(dst_row + rx0, src_row + tx_start, (size_t)count * sizeof(uint32_t));
                } else if (alpha_scale >= 255u) {
                    /* Vectorizado NEON a 4 píxeles por ciclo */
                    blend_row_neon(dst_row + rx0, src_row + tx_start, count);
                } else {
                    /* Vectorizado NEON con modulación de opacidad */
                    blend_row_neon_alpha(dst_row + rx0, src_row + tx_start, count, alpha_scale);
                }
            } else {
                /* Dirección X invertida (rotación 180° / flip horizontal) */
                const uint32_t *src_end = src_row + (tw - tx_start);
                if (is_opaque_shader && alpha_scale >= 255u) {
                    copy_row_neon_rev(dst_row + rx0, src_end, count);
                } else {
                    blend_row_neon_rev(dst_row + rx0, src_end, count);
                }
            }
        }
        return;
    }

    /* Caso con escalado: Aritmética de punto fijo 16.16 (sin punto flotante por píxel) */
    float inv_qw = 1.0f / quad_w;
    float inv_qh = 1.0f / quad_h;
    float u_span = (u_right - u_left) * (float)tw;
    int32_t step_u_fp = (int32_t)(u_span * inv_qw * 65536.0f);

    for (int py = ry0; py < ry1; py++) {
        float fy = ((float)py + 0.5f - min_sy) * inv_qh;
        float tv = v_top + fy * (v_bottom - v_top);
        int ty = (int)(tv * (float)th);
        if (ty < 0) ty = 0; else if (ty >= th) ty = th - 1;
        const uint32_t *src_row = st->pixels + ty * tw;
        uint32_t *dst_row = fb_px + py * fb_stride_px;

        float u_start_f = (u_left + ((float)rx0 + 0.5f - min_sx) * inv_qw * (u_right - u_left)) * (float)tw;
        int32_t u_curr_fp = (int32_t)(u_start_f * 65536.0f);

        for (int px = rx0; px < rx1; px++) {
            int tx = u_curr_fp >> 16;
            u_curr_fp += step_u_fp;
            if (tx < 0) tx = 0; else if (tx >= tw) tx = tw - 1;

            uint32_t spx = src_row[tx];
            if (is_opaque_shader && alpha_scale >= 255u) {
                dst_row[px] = spx | 0xFF000000u;
            } else {
                if (sp->is_tex == 2) spx |= 0xFF000000u;
                if (alpha_scale < 255u) {
                    uint32_t sa = (((spx >> 24) & 0xFFu) * alpha_scale) >> 8;
                    uint32_t sr = (((spx >> 16) & 0xFFu) * alpha_scale) >> 8;
                    uint32_t sg = (((spx >> 8) & 0xFFu) * alpha_scale) >> 8;
                    uint32_t sb = ((spx & 0xFFu) * alpha_scale) >> 8;
                    spx = (sa << 24) | (sr << 16) | (sg << 8) | sb;
                }
                dst_row[px] = blend_premult_argb(spx, dst_row[px]);
            }
        }
    }
}

__attribute__((visibility("default")))
void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices) {
    (void)mode; (void)count; (void)type; (void)indices;
}

static void (*real_glClear)(unsigned int) = NULL;
__attribute__((visibility("default")))
void glClear(unsigned int mask) {
    struct dumb_bo_entry *d = get_active_fbo_dumb();
    if (d && d->cpu_map && (mask & 0x4000 /* GL_COLOR_BUFFER_BIT */)) {
        uint32_t col = pack_argb(g_clear_color[0], g_clear_color[1], g_clear_color[2], 1.0f);
        int cx0 = 0, cy0 = 0, cx1 = (int)d->width, cy1 = (int)d->height;
        if (g_scissor_enabled) {
            if (g_sc_x > cx0) cx0 = g_sc_x;
            if (g_sc_y > cy0) cy0 = g_sc_y;
            if (g_sc_x + g_sc_w < cx1) cx1 = g_sc_x + g_sc_w;
            if (g_sc_y + g_sc_h < cy1) cy1 = g_sc_y + g_sc_h;
        }
        int stride_px = (int)(d->stride / 4);
        uint32_t *fb_px = (uint32_t *)d->cpu_map;
        int count = cx1 - cx0;
        if (count <= 0 || cy1 <= cy0) return;

        uint32x4_t vcol = vdupq_n_u32(col);
        for (int py = cy0; py < cy1; py++) {
            uint32_t *row = fb_px + py * stride_px + cx0;
            int x = 0;
            for (; x <= count - 4; x += 4) {
                vst1q_u32(row + x, vcol);
            }
            for (; x < count; x++) {
                row[x] = col;
            }
        }
        return;
    }
    if (!real_glClear) {
        void *mali = dlopen("/usr/lib/libmali-bifrost-patched.so", RTLD_LAZY | RTLD_GLOBAL);
        if (!mali) mali = dlopen("/usr/lib/libmali-bifrost.so", RTLD_LAZY | RTLD_GLOBAL);
        if (mali) real_glClear = dlsym(mali, "glClear");
        if (!real_glClear) real_glClear = dlsym(RTLD_NEXT, "glClear");
    }
    if (real_glClear) {
        real_glClear(mask);
    }
}

static EGLBoolean (*real_eglSwapBuffers)(EGLDisplay, void *) = NULL;
__attribute__((visibility("default")))
EGLBoolean eglSwapBuffers(EGLDisplay dpy, void *surface) {
    if (!real_eglSwapBuffers) {
        void *mali = dlopen("/usr/lib/libmali-bifrost-patched.so", RTLD_LAZY | RTLD_GLOBAL);
        if (!mali) mali = dlopen("/usr/lib/libmali-bifrost.so", RTLD_LAZY | RTLD_GLOBAL);
        if (mali) real_eglSwapBuffers = dlsym(mali, "eglSwapBuffers");
        if (!real_eglSwapBuffers) real_eglSwapBuffers = dlsym(RTLD_NEXT, "eglSwapBuffers");
    }
    EGLBoolean ret = real_eglSwapBuffers ? real_eglSwapBuffers(dpy, surface) : 0;
    return ret;
}

__attribute__((visibility("default")))
EGLBoolean eglSwapBuffersWithDamageKHR(EGLDisplay dpy, void *surface, int *rects, int n_rects) {
    (void)rects; (void)n_rects;
    return eglSwapBuffers(dpy, surface);
}

__attribute__((visibility("default")))
EGLBoolean eglSwapBuffersWithDamageEXT(EGLDisplay dpy, void *surface, int *rects, int n_rects) {
    (void)rects; (void)n_rects;
    return eglSwapBuffers(dpy, surface);
}

__attribute__((visibility("default")))
EGLBoolean eglSetDamageRegionKHR(EGLDisplay dpy, void *surface, int *rects, int n_rects) {
    (void)dpy; (void)surface; (void)rects; (void)n_rects;
    return 1;
}


#ifndef EGL_LINUX_DMA_BUF_EXT
#define EGL_LINUX_DMA_BUF_EXT 0x3270
#endif
#ifndef EGL_WIDTH
#define EGL_WIDTH 0x3057
#endif
#ifndef EGL_HEIGHT
#define EGL_HEIGHT 0x3056
#endif
#ifndef EGL_LINUX_DRM_FOURCC_EXT
#define EGL_LINUX_DRM_FOURCC_EXT 0x3271
#endif
#ifndef EGL_DMA_BUF_PLANE0_FD_EXT
#define EGL_DMA_BUF_PLANE0_FD_EXT 0x3272
#endif
#ifndef EGL_DMA_BUF_PLANE0_OFFSET_EXT
#define EGL_DMA_BUF_PLANE0_OFFSET_EXT 0x3273
#endif
#ifndef EGL_DMA_BUF_PLANE0_PITCH_EXT
#define EGL_DMA_BUF_PLANE0_PITCH_EXT 0x3274
#endif
#ifndef EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT
#define EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT 0x3275
#endif
#ifndef EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT
#define EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT 0x3276
#endif

#ifndef EGL_NONE
#define EGL_NONE 0x3038
#endif
#ifndef EGL_NO_CONTEXT
#define EGL_NO_CONTEXT ((EGLContext)0)
#endif

typedef void *EGLContext;
typedef void *EGLConfig;
typedef int EGLint;
typedef void *EGLImageKHR;
typedef void *EGLClientBuffer;

static EGLImageKHR (*real_eglCreateImageKHR)(EGLDisplay, EGLContext, unsigned int, EGLClientBuffer, const int *) = NULL;
static EGLBoolean (*real_eglDestroyImageKHR)(EGLDisplay, EGLImageKHR) = NULL;

#ifndef EGL_NATIVE_PIXMAP_KHR
#define EGL_NATIVE_PIXMAP_KHR 0x30B0
#endif

__attribute__((visibility("default")))
EGLImageKHR eglCreateImageKHR(EGLDisplay dpy, EGLContext ctx, unsigned int target,
                              EGLClientBuffer buffer, const int *attrib_list)
{
    if (!real_eglCreateImageKHR) {
        void *mali = dlopen("/usr/lib/libmali-bifrost-patched.so", RTLD_LAZY | RTLD_GLOBAL);
        if (!mali) mali = dlopen("/usr/lib/libmali-bifrost.so", RTLD_LAZY | RTLD_GLOBAL);
        if (mali) real_eglCreateImageKHR = dlsym(mali, "eglCreateImageKHR");
        if (!real_eglCreateImageKHR) real_eglCreateImageKHR = dlsym(RTLD_NEXT, "eglCreateImageKHR");
    }

    if (target == EGL_LINUX_DMA_BUF_EXT && attrib_list) {
        int patched_attribs[64];
        int count = 0;
        int has_mod = 0;
        int dma_fd = -1;
        int fd_idx = -1;
        uint32_t w = 0, h = 0, stride = 0, fourcc = 0;

        for (int i = 0; attrib_list[i] != EGL_NONE && count < 56; i += 2) {
            int attr = attrib_list[i];
            int val = attrib_list[i + 1];

            if (attr == EGL_WIDTH) w = (uint32_t)val;
            else if (attr == EGL_HEIGHT) h = (uint32_t)val;
            else if (attr == EGL_LINUX_DRM_FOURCC_EXT) fourcc = (uint32_t)val;
            else if (attr == EGL_DMA_BUF_PLANE0_FD_EXT) {
                dma_fd = val;
                fd_idx = count + 1;
            }
            else if (attr == EGL_DMA_BUF_PLANE0_PITCH_EXT) stride = (uint32_t)val;
            else if (attr == EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT) {
                has_mod = 1;
                val = 0; /* Forzar DRM_FORMAT_MOD_LINEAR (lo) */
            } else if (attr == EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT) {
                has_mod = 1;
                val = 0; /* Forzar DRM_FORMAT_MOD_LINEAR (hi) */
            }

            patched_attribs[count++] = attr;
            patched_attribs[count++] = val;
        }

        /* Buscar matched_entry en nuestra dumb table para enlazar el FBO/RBO al cpu_map */
        struct dumb_bo_entry *matched_entry = NULL;
        pthread_mutex_lock(&g_dumb_mutex);
        for (int i = 0; i < MAX_DUMB_ENTRIES; i++) {
            if (g_dumb_table[i].bo && g_dumb_table[i].prime_fd == dma_fd) {
                matched_entry = &g_dumb_table[i];
                break;
            }
        }
        if (!matched_entry && dma_fd >= 0 && g_kms_fd >= 0) {
            uint32_t q_h = 0;
            if (drmPrimeFDToHandle(g_kms_fd, dma_fd, &q_h) == 0 && q_h > 0) {
                matched_entry = find_dumb_bo_by_handle_locked(q_h);
            }
        }
        if (!matched_entry && w > 0 && h > 0) {
            for (int i = 0; i < MAX_DUMB_ENTRIES; i++) {
                if (g_dumb_table[i].bo && g_dumb_table[i].width == w && g_dumb_table[i].height == h) {
                    matched_entry = &g_dumb_table[i];
                    break;
                }
            }
        }
        if (matched_entry && fd_idx >= 0 && fcntl(dma_fd, F_GETFD) < 0 && matched_entry->prime_fd >= 0) {
            patched_attribs[fd_idx] = matched_entry->prime_fd;
            dma_fd = matched_entry->prime_fd;
        }
        pthread_mutex_unlock(&g_dumb_mutex);

        /* Intento 1: Llamada directa a libmali con atributos originales de wlroots */
        if (real_eglCreateImageKHR) {
            EGLImageKHR direct_img = real_eglCreateImageKHR(dpy, ctx, target, buffer, attrib_list);
            if (direct_img) {
                if (matched_entry) register_egl_image_dumb(direct_img, matched_entry);
                return direct_img;
            }
        }

        if (!has_mod && count < 54) {
            patched_attribs[count++] = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT;
            patched_attribs[count++] = 0;
            patched_attribs[count++] = EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT;
            patched_attribs[count++] = 0;
        }
        patched_attribs[count] = EGL_NONE;

        kmsg_log("[GPU-EGL] >>> eglCreateImageKHR(DMA_BUF): %ux%u, format=0x%x, fd=%d, stride=%u -> mod=LINEAR\n",
                 w, h, fourcc, dma_fd, stride);
        EGLImageKHR img = real_eglCreateImageKHR ? real_eglCreateImageKHR(dpy, ctx, target, buffer, patched_attribs) : NULL;

        /* Fallback nativo Mali: Si DMA_BUF import falla, importar vía EGL_NATIVE_PIXMAP_KHR sobre el BO de Mali */
        if (!img && matched_entry && matched_entry->bo) {
            kmsg_log("[GPU-EGL] Fallback DMA_BUF -> EGL_NATIVE_PIXMAP_KHR con bo=%p\n", (void*)matched_entry->bo);
            img = real_eglCreateImageKHR ? real_eglCreateImageKHR(dpy, EGL_NO_CONTEXT, EGL_NATIVE_PIXMAP_KHR, (EGLClientBuffer)matched_entry->bo, NULL) : NULL;
        }

        if (img && matched_entry) register_egl_image_dumb(img, matched_entry);
        kmsg_log("[GPU-EGL] <<< eglCreateImageKHR(DMA_BUF) RET=%p\n", (void*)img);
        return img;
    }

    kmsg_log("[GPU-EGL] >>> eglCreateImageKHR: target=0x%x, buf=%p\n", target, (void*)buffer);
    EGLImageKHR img = real_eglCreateImageKHR ? real_eglCreateImageKHR(dpy, ctx, target, buffer, attrib_list) : NULL;
    if (img && target == EGL_NATIVE_PIXMAP_KHR && buffer) {
        pthread_mutex_lock(&g_dumb_mutex);
        struct dumb_bo_entry *de = find_dumb_bo_locked((struct gbm_bo *)buffer);
        pthread_mutex_unlock(&g_dumb_mutex);
        if (de) register_egl_image_dumb(img, de);
    }
    kmsg_log("[GPU-EGL] <<< eglCreateImageKHR RET=%p\n", (void*)img);
    return img;
}

__attribute__((visibility("default")))
EGLBoolean eglDestroyImageKHR(EGLDisplay dpy, EGLImageKHR img) {
    if (!real_eglDestroyImageKHR) {
        void *mali = dlopen("/usr/lib/libmali-bifrost-patched.so", RTLD_LAZY | RTLD_GLOBAL);
        if (!mali) mali = dlopen("/usr/lib/libmali-bifrost.so", RTLD_LAZY | RTLD_GLOBAL);
        if (mali) real_eglDestroyImageKHR = dlsym(mali, "eglDestroyImageKHR");
        if (!real_eglDestroyImageKHR) real_eglDestroyImageKHR = dlsym(RTLD_NEXT, "eglDestroyImageKHR");
    }
    kmsg_log("[GPU-EGL] >>> eglDestroyImageKHR: img=%p\n", (void*)img);
    EGLBoolean ret = real_eglDestroyImageKHR ? real_eglDestroyImageKHR(dpy, img) : 1;
    return ret;
}

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

__attribute__((visibility("default")))
GLenum glGetGraphicsResetStatus(void) {
    return 0; /* GL_NO_ERROR */
}

__attribute__((visibility("default")))
GLenum glGetGraphicsResetStatusEXT(void) {
    return 0; /* GL_NO_ERROR */
}

__attribute__((visibility("default")))
GLenum glGetGraphicsResetStatusKHR(void) {
    return 0; /* GL_NO_ERROR */
}

void *eglGetProcAddress(const char *procname) {
    if (!procname) return NULL;
    /* Bloquear extensiones de daño parcial para forzar a wlroots a redibujado completo consistente */
    if (strcmp(procname, "eglSetDamageRegionKHR") == 0 ||
        strcmp(procname, "eglSwapBuffersWithDamageKHR") == 0 ||
        strcmp(procname, "eglSwapBuffersWithDamageEXT") == 0) {
        return NULL;
    }
    if (strcmp(procname, "glGetGraphicsResetStatus") == 0 ||
        strcmp(procname, "glGetGraphicsResetStatusEXT") == 0 ||
        strcmp(procname, "glGetGraphicsResetStatusKHR") == 0) {
        return (void *)glGetGraphicsResetStatusEXT;
    }
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
    if (strcmp(procname, "glClear") == 0) {
        return (void *)glClear;
    }
    if (strcmp(procname, "eglSwapBuffers") == 0) {
        return (void *)eglSwapBuffers;
    }
    if (strcmp(procname, "eglCreateImageKHR") == 0 || strcmp(procname, "eglCreateImage") == 0) {
        return (void *)eglCreateImageKHR;
    }
    if (strcmp(procname, "eglDestroyImageKHR") == 0 || strcmp(procname, "eglDestroyImage") == 0) {
        return (void *)eglDestroyImageKHR;
    }
    if (strcmp(procname, "glEGLImageTargetRenderbufferStorageOES") == 0) {
        return (void *)glEGLImageTargetRenderbufferStorageOES;
    }
    if (strcmp(procname, "glEGLImageTargetTexture2DOES") == 0) {
        return (void *)glEGLImageTargetTexture2DOES;
    }
    if (strcmp(procname, "glBindTexture") == 0) return (void *)glBindTexture;
    if (strcmp(procname, "glDeleteTextures") == 0) return (void *)glDeleteTextures;
    if (strcmp(procname, "glPixelStorei") == 0) return (void *)glPixelStorei;
    if (strcmp(procname, "glTexImage2D") == 0) return (void *)glTexImage2D;
    if (strcmp(procname, "glTexSubImage2D") == 0) return (void *)glTexSubImage2D;
    if (strcmp(procname, "glBindRenderbuffer") == 0) return (void *)glBindRenderbuffer;
    if (strcmp(procname, "glBindFramebuffer") == 0) return (void *)glBindFramebuffer;
    if (strcmp(procname, "glFramebufferRenderbuffer") == 0) return (void *)glFramebufferRenderbuffer;
    if (strcmp(procname, "glFramebufferTexture2D") == 0) return (void *)glFramebufferTexture2D;
    if (strcmp(procname, "glViewport") == 0) return (void *)glViewport;
    if (strcmp(procname, "glScissor") == 0) return (void *)glScissor;
    if (strcmp(procname, "glEnable") == 0) return (void *)glEnable;
    if (strcmp(procname, "glDisable") == 0) return (void *)glDisable;
    if (strcmp(procname, "glShaderSource") == 0) return (void *)glShaderSource;
    if (strcmp(procname, "glLinkProgram") == 0) return (void *)glLinkProgram;
    if (strcmp(procname, "glGetUniformLocation") == 0) return (void *)glGetUniformLocation;
    if (strcmp(procname, "glUseProgram") == 0) return (void *)glUseProgram;
    if (strcmp(procname, "glUniformMatrix3fv") == 0) return (void *)glUniformMatrix3fv;
    if (strcmp(procname, "glUniform4f") == 0) return (void *)glUniform4f;
    if (strcmp(procname, "glUniform1f") == 0) return (void *)glUniform1f;
    if (strcmp(procname, "glVertexAttribPointer") == 0) return (void *)glVertexAttribPointer;
    if (strcmp(procname, "glReadPixels") == 0) return (void *)glReadPixels;
    if (strcmp(procname, "glClearColor") == 0) return (void *)glClearColor;
    if (strcmp(procname, "glGetError") == 0) return (void *)glGetError;
    if (strcmp(procname, "eglQueryString") == 0) return (void *)eglQueryString;

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

drmModeResPtr drmModeGetResources(int fd) {
    if (fd >= 0) g_kms_fd = fd;
    static drmModeResPtr (*real_drmModeGetResources)(int) = NULL;
    if (!real_drmModeGetResources) {
        real_drmModeGetResources = dlsym(RTLD_NEXT, "drmModeGetResources");
        if (!real_drmModeGetResources) {
            void *ldrm = dlopen("/usr/lib/libdrm.so.2", RTLD_LAZY | RTLD_GLOBAL);
            if (ldrm) real_drmModeGetResources = dlsym(ldrm, "drmModeGetResources");
        }
    }
    return real_drmModeGetResources ? real_drmModeGetResources(fd) : NULL;
}

drmModeConnectorPtr drmModeGetConnector(int fd, uint32_t connectorId) {
    if (fd >= 0) g_kms_fd = fd;
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
    if (fd >= 0) g_kms_fd = fd;
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

static int safe_drm_set_master(int fd) {
    if (fd < 0) return 0;
    return ioctl(fd, DRM_IOCTL_SET_MASTER, 0);
}

int drmModeSetCrtc(int fd, uint32_t crtcId, uint32_t bufferId,
                   uint32_t x, uint32_t y, uint32_t *connectors, int count,
                   drmModeModeInfoPtr mode)
{
    if (fd >= 0) {
        g_kms_fd = fd;
        int mret = safe_drm_set_master(fd);
        kmsg_log("[GPU-KMS] drmModeSetCrtc: safe_drm_set_master(fd=%d) -> ret=%d (errno=%d)\n",
                 fd, mret, mret < 0 ? errno : 0);
    }
    static int (*real_drmModeSetCrtc)(int, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t *, int, drmModeModeInfoPtr) = NULL;
    if (!real_drmModeSetCrtc) {
        real_drmModeSetCrtc = dlsym(RTLD_NEXT, "drmModeSetCrtc");
        if (!real_drmModeSetCrtc) {
            void *ldrm = dlopen("/usr/lib/libdrm.so.2", RTLD_LAZY | RTLD_GLOBAL);
            if (ldrm) real_drmModeSetCrtc = dlsym(ldrm, "drmModeSetCrtc");
        }
    }
    kmsg_log("[GPU-KMS] >>> drmModeSetCrtc: fd=%d, crtc=%u, fb=%u, count=%d, mode=%ux%u@%u\n",
             fd, crtcId, bufferId, count, mode ? mode->hdisplay : 0, mode ? mode->vdisplay : 0, mode ? mode->vrefresh : 0);
    int ret = real_drmModeSetCrtc ? real_drmModeSetCrtc(fd, crtcId, bufferId, x, y, connectors, count, mode) : -1;
    kmsg_log("[GPU-KMS] <<< drmModeSetCrtc RET=%d (errno=%d)\n", ret, ret < 0 ? errno : 0);
    return ret;
}

static void flip_180_inplace(uint32_t *map, int width, int height, int stride_px) {
    if (!map || width <= 0 || height <= 0 || stride_px <= 0) return;
    int half_h = height / 2;
    for (int y = 0; y < half_h; y++) {
        uint32_t *row_top = map + y * stride_px;
        uint32_t *row_bot = map + (height - 1 - y) * stride_px;
        int left = 0;
        int right = width - 1;
        while (left < right) {
            uint32_t t_l = row_top[left];
            uint32_t t_r = row_top[right];
            uint32_t b_l = row_bot[left];
            uint32_t b_r = row_bot[right];
            row_top[left]  = b_r;
            row_top[right] = b_l;
            row_bot[left]  = t_r;
            row_bot[right] = t_l;
            left++;
            right--;
        }
    }
}

int drmModePageFlip(int fd, uint32_t crtc_id, uint32_t fb_id,
                    uint32_t flags, void *user_data)
{
    if (fd >= 0) {
        g_kms_fd = fd;
        safe_drm_set_master(fd);
    }
    static int (*real_drmModePageFlip)(int, uint32_t, uint32_t, uint32_t, void *) = NULL;
    if (!real_drmModePageFlip) {
        real_drmModePageFlip = dlsym(RTLD_NEXT, "drmModePageFlip");
        if (!real_drmModePageFlip) {
            void *ldrm = dlopen("/usr/lib/libdrm.so.2", RTLD_LAZY | RTLD_GLOBAL);
            if (ldrm) real_drmModePageFlip = dlsym(ldrm, "drmModePageFlip");
        }
    }

    static int s_flip_180_scanout = -1;
    if (s_flip_180_scanout < 0) {
        const char *e = getenv("GPU_FLIP_180");
        s_flip_180_scanout = (e && atoi(e) == 0) ? 0 : 1;
    }

    if (s_flip_180_scanout) {
        pthread_mutex_lock(&g_dumb_mutex);
        for (int i = 0; i < MAX_DUMB_ENTRIES; i++) {
            if (g_dumb_table[i].bo && g_dumb_table[i].cpu_map && g_dumb_table[i].width > 0 && g_dumb_table[i].height > 0) {
                flip_180_inplace((uint32_t *)g_dumb_table[i].cpu_map, (int)g_dumb_table[i].width, (int)g_dumb_table[i].height, (int)(g_dumb_table[i].stride / 4));
            }
        }
        pthread_mutex_unlock(&g_dumb_mutex);
    }

    kmsg_log("[GPU-KMS] >>> drmModePageFlip: fd=%d, crtc=%u, fb=%u, flags=0x%x\n", fd, crtc_id, fb_id, flags);
    int ret = real_drmModePageFlip ? real_drmModePageFlip(fd, crtc_id, fb_id, flags, user_data) : -1;
    kmsg_log("[GPU-KMS] <<< drmModePageFlip RET=%d (errno=%d)\n", ret, ret < 0 ? errno : 0);

    return ret;
}

int drmModeAddFB(int fd, uint32_t width, uint32_t height, uint8_t depth,
                 uint8_t bpp, uint32_t pitch, uint32_t bo_handle,
                 uint32_t *buf_id)
{
    if (fd >= 0) g_kms_fd = fd;
    static int (*real_drmModeAddFB)(int, uint32_t, uint32_t, uint8_t, uint8_t, uint32_t, uint32_t, uint32_t *) = NULL;
    if (!real_drmModeAddFB) {
        real_drmModeAddFB = dlsym(RTLD_NEXT, "drmModeAddFB");
        if (!real_drmModeAddFB) {
            void *ldrm = dlopen("/usr/lib/libdrm.so.2", RTLD_LAZY | RTLD_GLOBAL);
            if (ldrm) real_drmModeAddFB = dlsym(ldrm, "drmModeAddFB");
        }
    }

    uint32_t effective_handle = bo_handle;
    pthread_mutex_lock(&g_dumb_mutex);
    struct dumb_bo_entry *entry = find_dumb_bo_by_handle_locked(bo_handle);
    if (entry && entry->prime_fd >= 0 && entry->card_fd != fd) {
        uint32_t imported_handle = 0;
        int r = drmPrimeFDToHandle(fd, entry->prime_fd, &imported_handle);
        if (r == 0 && imported_handle > 0) {
            kmsg_log("[GPU-KMS] drmModeAddFB: prime_fd=%d cruzado a fd=%d -> handle=%u (orig=%u en card_fd=%d)\n",
                     entry->prime_fd, fd, imported_handle, bo_handle, entry->card_fd);
            effective_handle = imported_handle;
        }
    }
    pthread_mutex_unlock(&g_dumb_mutex);

    kmsg_log("[GPU-KMS] >>> drmModeAddFB: fd=%d, %ux%u, pitch=%u, bo_handle=%u (eff=%u)\n",
             fd, width, height, pitch, bo_handle, effective_handle);
    int ret = real_drmModeAddFB ? real_drmModeAddFB(fd, width, height, depth, bpp, pitch, effective_handle, buf_id) : -1;
    kmsg_log("[GPU-KMS] <<< drmModeAddFB RET=%d, fb_id=%u (errno=%d)\n", ret, buf_id ? *buf_id : 0, ret < 0 ? errno : 0);
    return ret;
}

int drmModeAddFB2(int fd, uint32_t width, uint32_t height,
                  uint32_t pixel_format, const uint32_t bo_handles[4],
                  const uint32_t pitches[4], const uint32_t offsets[4],
                  uint32_t *buf_id, uint32_t flags)
{
    if (fd >= 0) g_kms_fd = fd;
    static int (*real_drmModeAddFB2)(int, uint32_t, uint32_t, uint32_t, const uint32_t *, const uint32_t *, const uint32_t *, uint32_t *, uint32_t) = NULL;
    if (!real_drmModeAddFB2) {
        real_drmModeAddFB2 = dlsym(RTLD_NEXT, "drmModeAddFB2");
        if (!real_drmModeAddFB2) {
            void *ldrm = dlopen("/usr/lib/libdrm.so.2", RTLD_LAZY | RTLD_GLOBAL);
            if (ldrm) real_drmModeAddFB2 = dlsym(ldrm, "drmModeAddFB2");
        }
    }

    uint32_t eff_handles[4] = {
        bo_handles ? bo_handles[0] : 0,
        bo_handles ? bo_handles[1] : 0,
        bo_handles ? bo_handles[2] : 0,
        bo_handles ? bo_handles[3] : 0
    };

    if (bo_handles && bo_handles[0] > 0) {
        pthread_mutex_lock(&g_dumb_mutex);
        struct dumb_bo_entry *entry = find_dumb_bo_by_handle_locked(bo_handles[0]);
        if (entry && entry->prime_fd >= 0 && entry->card_fd != fd) {
            uint32_t imported_handle = 0;
            int r = drmPrimeFDToHandle(fd, entry->prime_fd, &imported_handle);
            if (r == 0 && imported_handle > 0) {
                kmsg_log("[GPU-KMS] drmModeAddFB2: PRIME cruzado OK! prime_fd=%d a fd=%d -> handle=%u (orig=%u en card_fd=%d)\n",
                         entry->prime_fd, fd, imported_handle, bo_handles[0], entry->card_fd);
                eff_handles[0] = imported_handle;
            } else {
                kmsg_log("[GPU-KMS] ERROR drmPrimeFDToHandle(fd=%d, prime_fd=%d): ret=%d errno=%d\n",
                         fd, entry->prime_fd, r, errno);
            }
        }
        pthread_mutex_unlock(&g_dumb_mutex);
    }

    kmsg_log("[GPU-KMS] >>> drmModeAddFB2: fd=%d, %ux%u, format=0x%x, h0=%u (eff=%u), pitch0=%u, flags=0x%x\n",
             fd, width, height, pixel_format, bo_handles ? bo_handles[0] : 0, eff_handles[0], pitches ? pitches[0] : 0, flags);
    int ret = real_drmModeAddFB2 ? real_drmModeAddFB2(fd, width, height, pixel_format, eff_handles, pitches, offsets, buf_id, flags) : -1;
    kmsg_log("[GPU-KMS] <<< drmModeAddFB2 RET=%d, fb_id=%u (errno=%d)\n", ret, buf_id ? *buf_id : 0, ret < 0 ? errno : 0);
    return ret;
}

int drmModeSetPlane(int fd, uint32_t plane_id, uint32_t crtc_id,
                    uint32_t fb_id, uint32_t flags,
                    int32_t crtc_x, int32_t crtc_y,
                    uint32_t crtc_w, uint32_t crtc_h,
                    uint32_t src_x, uint32_t src_y,
                    uint32_t src_w, uint32_t src_h)
{
    static int (*real_drmModeSetPlane)(int, uint32_t, uint32_t, uint32_t, uint32_t, int32_t, int32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) = NULL;
    if (!real_drmModeSetPlane) {
        real_drmModeSetPlane = dlsym(RTLD_NEXT, "drmModeSetPlane");
        if (!real_drmModeSetPlane) {
            void *ldrm = dlopen("/usr/lib/libdrm.so.2", RTLD_LAZY | RTLD_GLOBAL);
            if (ldrm) real_drmModeSetPlane = dlsym(ldrm, "drmModeSetPlane");
        }
    }
    kmsg_log("[GPU-KMS] >>> drmModeSetPlane: plane=%u, crtc=%u, fb=%u\n", plane_id, crtc_id, fb_id);
    int ret = real_drmModeSetPlane ? real_drmModeSetPlane(fd, plane_id, crtc_id, fb_id, flags, crtc_x, crtc_y, crtc_w, crtc_h, src_x, src_y, src_w, src_h) : -1;
    kmsg_log("[GPU-KMS] <<< drmModeSetPlane RET=%d (errno=%d)\n", ret, ret < 0 ? errno : 0);
    return ret;
}

int drmModeAtomicCommit(int fd, drmModeAtomicReqPtr req, uint32_t flags, void *user_data) {
    static int (*real_drmModeAtomicCommit)(int, drmModeAtomicReqPtr, uint32_t, void *) = NULL;
    if (!real_drmModeAtomicCommit) {
        real_drmModeAtomicCommit = dlsym(RTLD_NEXT, "drmModeAtomicCommit");
        if (!real_drmModeAtomicCommit) {
            void *ldrm = dlopen("/usr/lib/libdrm.so.2", RTLD_LAZY | RTLD_GLOBAL);
            if (ldrm) real_drmModeAtomicCommit = dlsym(ldrm, "drmModeAtomicCommit");
        }
    }
    kmsg_log("[GPU-KMS] >>> drmModeAtomicCommit: fd=%d, flags=0x%x\n", fd, flags);
    int ret = real_drmModeAtomicCommit ? real_drmModeAtomicCommit(fd, req, flags, user_data) : -1;
    kmsg_log("[GPU-KMS] <<< drmModeAtomicCommit RET=%d (errno=%d)\n", ret, ret < 0 ? errno : 0);
    return ret;
}

__attribute__((visibility("default")))
int drmModeConnectorSetProperty(int fd, uint32_t connector_id, uint32_t property_id, uint64_t value) {
    static int (*real_drmModeConnectorSetProperty)(int, uint32_t, uint32_t, uint64_t) = NULL;
    if (!real_drmModeConnectorSetProperty) {
        real_drmModeConnectorSetProperty = dlsym(RTLD_NEXT, "drmModeConnectorSetProperty");
        if (!real_drmModeConnectorSetProperty) {
            void *ldrm = dlopen("/usr/lib/libdrm.so.2", RTLD_LAZY | RTLD_GLOBAL);
            if (ldrm) real_drmModeConnectorSetProperty = dlsym(ldrm, "drmModeConnectorSetProperty");
        }
    }
    int ret = real_drmModeConnectorSetProperty ? real_drmModeConnectorSetProperty(fd, connector_id, property_id, value) : -1;
    kmsg_log("[GPU-KMS] >>> drmModeConnectorSetProperty: fd=%d, conn=%u, prop=%u, val=%llu -> ret=%d (errno=%d)\n",
             fd, connector_id, property_id, (unsigned long long)value, ret, ret < 0 ? errno : 0);
    /* En el kernel MediaTek MT6789, cambiar la propiedad DPMS en conectores DSI retorna Permission denied
     * a pesar de que el panel Novatek ya está físicamente encendido y activo. Enmascaramos el fallo para
     * que wlroots declare la salida DSI-1 habilitada y active los monitores para los clientes Wayland. */
    if (ret < 0) {
        kmsg_log("[GPU-KMS] <<< DPMS enmascarado exitoso para connector=%u: retornando 0 (OK)\n", connector_id);
        return 0;
    }
    return ret;
}

__attribute__((visibility("default")))
int drmModeObjectSetProperty(int fd, uint32_t object_id, uint32_t object_type, uint32_t property_id, uint64_t value) {
    static int (*real_drmModeObjectSetProperty)(int, uint32_t, uint32_t, uint32_t, uint64_t) = NULL;
    if (!real_drmModeObjectSetProperty) {
        real_drmModeObjectSetProperty = dlsym(RTLD_NEXT, "drmModeObjectSetProperty");
        if (!real_drmModeObjectSetProperty) {
            void *ldrm = dlopen("/usr/lib/libdrm.so.2", RTLD_LAZY | RTLD_GLOBAL);
            if (ldrm) real_drmModeObjectSetProperty = dlsym(ldrm, "drmModeObjectSetProperty");
        }
    }
    int ret = real_drmModeObjectSetProperty ? real_drmModeObjectSetProperty(fd, object_id, object_type, property_id, value) : -1;
    if (ret < 0 && object_type == DRM_MODE_OBJECT_CONNECTOR) {
        kmsg_log("[GPU-KMS] <<< drmModeObjectSetProperty enmascarado para connector=%u: retornando 0 (OK)\n", object_id);
        return 0;
    }
    return ret;
}

__attribute__((visibility("default")))
int drmCloseBufferHandle(int fd, uint32_t handle) {
    if (is_active_dumb_handle(handle)) {
        kmsg_log("[GPU-KMS] drmCloseBufferHandle: handle=%u activo en KMS preservado (ret=0)\n", handle);
        return 0;
    }
    struct drm_gem_close req = { .handle = handle };
    return drmIoctl(fd, DRM_IOCTL_GEM_CLOSE, &req);
}

__attribute__((visibility("default")))
int drmModeSetCursor(int fd, uint32_t crtcId, uint32_t bo_handle,
                     uint32_t width, uint32_t height)
{
    if (bo_handle == 0) return 0; /* Desactivar hardware cursor retorna exito limpiamente */
    errno = EINVAL;
    return -1;
}

__attribute__((visibility("default")))
int drmModeSetCursor2(int fd, uint32_t crtcId, uint32_t bo_handle,
                      uint32_t width, uint32_t height, int32_t hot_x, int32_t hot_y)
{
    if (bo_handle == 0) return 0; /* Desactivar hardware cursor retorna exito limpiamente */
    errno = EINVAL;
    return -1;
}

__attribute__((visibility("default")))
int drmModeMoveCursor(int fd, uint32_t crtcId, int x, int y) {
    (void)fd; (void)crtcId; (void)x; (void)y;
    return 0;
}

__attribute__((visibility("default")))
int drmModeRmFB(int fd, uint32_t bufferId) {
    static int (*real_drmModeRmFB)(int, uint32_t) = NULL;
    if (!real_drmModeRmFB) {
        real_drmModeRmFB = dlsym(RTLD_NEXT, "drmModeRmFB");
        if (!real_drmModeRmFB) {
            void *ldrm = dlopen("/usr/lib/libdrm.so.2", RTLD_LAZY | RTLD_GLOBAL);
            if (ldrm) real_drmModeRmFB = dlsym(ldrm, "drmModeRmFB");
        }
    }
    kmsg_log("[GPU-KMS] >>> drmModeRmFB: fd=%d, fb_id=%u\n", fd, bufferId);
    int ret = real_drmModeRmFB ? real_drmModeRmFB(fd, bufferId) : 0;
    kmsg_log("[GPU-KMS] <<< drmModeRmFB RET=%d\n", ret);
    return ret;
}

__attribute__((visibility("default")))
int drmHandleEvent(int fd, drmEventContextPtr evctx) {
    static int (*real_drmHandleEvent)(int, drmEventContextPtr) = NULL;
    if (!real_drmHandleEvent) {
        real_drmHandleEvent = dlsym(RTLD_NEXT, "drmHandleEvent");
        if (!real_drmHandleEvent) {
            void *ldrm = dlopen("/usr/lib/libdrm.so.2", RTLD_LAZY | RTLD_GLOBAL);
            if (ldrm) real_drmHandleEvent = dlsym(ldrm, "drmHandleEvent");
        }
    }
    int ret = real_drmHandleEvent ? real_drmHandleEvent(fd, evctx) : -1;
    return ret;
}

/* Constructor del driver */
__attribute__((constructor))
static void gpu_driver_init(void) {
    kmsg_always("[gpu-driver] >>> DRIVER DE INTERFAZ GPU MALI-G57 MC2 INICIALIZADO (Valhall Type 5 Fix + Fast Scanout) <<<\n");
}
