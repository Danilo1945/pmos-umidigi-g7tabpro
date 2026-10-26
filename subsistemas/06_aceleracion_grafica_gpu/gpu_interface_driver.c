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


static int g_kms_fd = -1;
int is_active_dumb_handle(uint32_t handle);

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
    if (g_debug_enabled == -1) {
        const char *env1 = getenv("GPU_DRIVER_DEBUG");
        const char *env2 = getenv("GPU_INTERFACE_DEBUG");
        g_debug_enabled = ((env1 && env1[0] == '1') || (env2 && env2[0] == '1')) ? 1 : 0;
    }
    return g_debug_enabled;
}

static inline void kmsg_log(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) {
        int kfd = open("/dev/kmsg", O_WRONLY);
        if (kfd >= 0) {
            write(kfd, buf, n);
            close(kfd);
        }
        fprintf(stderr, "%s", buf);
        fflush(stderr);
    }
}

#define GPU_LOG(fmt, ...) do { \
    kmsg_log("[gpu-driver] " fmt "\n", ##__VA_ARGS__); \
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
            uint8_t orig_split_atom_num = 0;

            for (uint32_t i = 0; i < sub->nr_atoms; i++) {
                const uint64_t *q = (const uint64_t *)&src[i * 64];
                uint32_t core_req = (uint32_t)(q[6] >> 32);
                if (!(core_req & 0x200) && q[1] > 0x10000) {
                    uint64_t cur = q[1];
                    uint64_t prev = 0;
                    int step = 0;
                    while (cur > 0x10000 && step < 16) {
                        uint64_t *jc_ptr = (uint64_t *)(uintptr_t)cur;
                        uint32_t job_type = (uint32_t)((jc_ptr[2] >> 1) & 0x7f);
                        if (0 && job_type == 1 && prev != 0 && jc_ptr[3] > 0x10000) {
                            split_atom_idx = (int)i;
                            compute_last_job = prev;
                            vertex_first_job = jc_ptr[3];
                            orig_split_atom_num = src[i * 64 + 48];
                            GPU_LOG("  [DETECT_SPLIT] Átomo[%u] (atom_num=%u) contiene NULL barrier @ 0x%llx entre Compute (last=0x%llx) y Vertex (first=0x%llx)",
                                    i, orig_split_atom_num, (unsigned long long)cur,
                                    (unsigned long long)compute_last_job, (unsigned long long)vertex_first_job);
                            break;
                        }
                        prev = cur;
                        cur = jc_ptr[3];
                        step++;
                    }
                }
            }

            uint8_t atoms72[256 * 72];
            memset(atoms72, 0, sizeof(atoms72));
            uint32_t out_nr_atoms = 0;
            const uint8_t new_vertex_atom_num = 5;

            if (split_atom_idx >= 0 && compute_last_job != 0 && vertex_first_job != 0) {
                // 1. Terminar la cadena Compute en compute_last_job (JC[1.2]->next = 0)
                uint64_t *clast_ptr = (uint64_t *)(uintptr_t)compute_last_job;
                clast_ptr[3] = 0;
                flush_dcache_range(compute_last_job, 64);
                GPU_LOG("  [ATOM_SPLIT] Compute chain terminada en 0x%llx (next=0)", (unsigned long long)compute_last_job);

                if (vertex_first_job > 0x10000) {
                    uint64_t *p4000 = (uint64_t *)(uintptr_t)0x7befe74000;
                    GPU_LOG("  [PAGE_4000_DUMP] Dumping 0x7befe74000..0x7befe74200:");
                    for (int w = 0; w < 64; w += 4) {
                        GPU_LOG("    offset +0x%03x: 0x%016llx 0x%016llx 0x%016llx 0x%016llx",
                                w * 8,
                                (unsigned long long)p4000[w], (unsigned long long)p4000[w + 1],
                                (unsigned long long)p4000[w + 2], (unsigned long long)p4000[w + 3]);
                    }
                }

                // Asegurar flush de la cadena Vertex (JC[1.4..1.5]) y loguear sus descriptores completos
                uint64_t vcur = vertex_first_job;
                int vstep = 0;
                while (vcur > 0x10000 && vstep < 16) {
                    uint64_t *vptr = (uint64_t *)(uintptr_t)vcur;
                    GPU_LOG("  [VERTEX_CHAIN] JC[%d] @ 0x%llx (w0..w7):", vstep, (unsigned long long)vcur);
                    for (int w = 0; w < 32; w += 4) {
                        GPU_LOG("    w[%02d..%02d]: 0x%016llx 0x%016llx 0x%016llx 0x%016llx",
                                w, w + 3,
                                (unsigned long long)vptr[w], (unsigned long long)vptr[w + 1],
                                (unsigned long long)vptr[w + 2], (unsigned long long)vptr[w + 3]);
                    }
                    if (vptr[30] > 0x10000) {
                        const uint64_t *p30 = (const uint64_t *)(uintptr_t)vptr[30];
                        GPU_LOG("    [PTR_INSPECT] vptr[30]=0x%llx contents: 0x%016llx 0x%016llx 0x%016llx 0x%016llx",
                                (unsigned long long)vptr[30],
                                (unsigned long long)p30[0], (unsigned long long)p30[1],
                                (unsigned long long)p30[2], (unsigned long long)p30[3]);
                    }
                    flush_dcache_range(vcur, 256);
                    vcur = vptr[3];
                    vstep++;
                }

                // 2. Construir los átomos divididos
                for (uint32_t i = 0; i < sub->nr_atoms; i++) {
                    if ((int)i == split_atom_idx) {
                        // Átomo A: Compute (JC[1.0..1.2])
                        memcpy(&atoms72[out_nr_atoms * 72], &src[i * 64], 64);
                        uint8_t *a_ptr = &atoms72[out_nr_atoms * 72];
                        *(uint32_t *)&a_ptr[52] = 0x4a; // Pure Compute: BASE_JD_REQ_CS | BASE_JD_REQ_CF | BASE_JD_REQ_COHERENT_GROUP
                        GPU_LOG("  [ATOM_SPLIT] Inyectando Atom[%u] (Compute): jc=0x%llx, atom_num=%u, dep=%u, core_req=0x%x",
                                out_nr_atoms, (unsigned long long)*(uint64_t *)&a_ptr[8], a_ptr[48], a_ptr[44], *(uint32_t *)&a_ptr[52]);
                        out_nr_atoms++;

                        // Átomo B: Vertex + Tiler (JC[1.4..1.5])
                        memcpy(&atoms72[out_nr_atoms * 72], &src[i * 64], 64);
                        uint8_t *b_ptr = &atoms72[out_nr_atoms * 72];
                        *(uint64_t *)&b_ptr[8] = vertex_first_job; // jc = JC[1.4]
                        b_ptr[48] = new_vertex_atom_num; // atom_number = 5
                        b_ptr[44] = orig_split_atom_num; // pre_dep[0].atom_id = 2 (depende de Compute)
                        b_ptr[45] = 1; // dependency_type = BASE_JD_DEP_DATA
                        b_ptr[46] = 0;
                        b_ptr[47] = 0;
                        *(uint32_t *)&b_ptr[52] = 0x4c; // Pure Geometry/Tiler: BASE_JD_REQ_T | BASE_JD_REQ_CF | BASE_JD_REQ_COHERENT_GROUP

                        // Forzar job_barrier = 1 en JC[2.0] (primer trabajo de geometría)
                        uint64_t *vptr0 = (uint64_t *)(uintptr_t)vertex_first_job;
                        vptr0[2] |= (1ULL << 8); // job_barrier = 1
                        flush_dcache_range(vertex_first_job, 256);

                        GPU_LOG("  [ATOM_SPLIT] Inyectando Atom[%u] (Vertex+Tiler): jc=0x%llx, atom_num=%u, dep=%u, core_req=0x%x, barrier=1",
                                out_nr_atoms, (unsigned long long)vertex_first_job, b_ptr[48], b_ptr[44], *(uint32_t *)&b_ptr[52]);
                        out_nr_atoms++;
                    } else {
                        memcpy(&atoms72[out_nr_atoms * 72], &src[i * 64], 64);
                        uint8_t *o_ptr = &atoms72[out_nr_atoms * 72];
                        // Si dependía de orig_split_atom_num (e.g. Fragment Shader dependía de Atom 2),
                        // redirigir la dependencia hacia new_vertex_atom_num (5)
                        if (o_ptr[44] == orig_split_atom_num) {
                            o_ptr[44] = new_vertex_atom_num;
                            GPU_LOG("  [ATOM_SPLIT] Redirigida dependencia de Atom[%u] (atom_num=%u): pre_dep[0]=%u (Vertex+Tiler)",
                                    out_nr_atoms, o_ptr[48], new_vertex_atom_num);
                        }
                        if (o_ptr[46] == orig_split_atom_num) {
                            o_ptr[46] = new_vertex_atom_num;
                            GPU_LOG("  [ATOM_SPLIT] Redirigida dependencia de Atom[%u] (atom_num=%u): pre_dep[1]=%u (Vertex+Tiler)",
                                    out_nr_atoms, o_ptr[48], new_vertex_atom_num);
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

            // Corrección de cadenas Fragment Shader: omitir dummy SFBD (tag=1, tiles=0)
            for (uint32_t i = 0; i < out_nr_atoms; i++) {
                uint32_t *creq_ptr = (uint32_t *)&atoms72[i * 72 + 52];
                if (!(*creq_ptr & 0x200)) {
                    // Asegurar flags de coherencia y flush para silicio multi-core MC2
                    *creq_ptr |= 0x48; // BASE_JD_REQ_COHERENT_GROUP | BASE_JD_REQ_CF
                }
                if (*creq_ptr & 0x01) { // BASE_JD_REQ_FS
                    uint64_t fjc = *(const uint64_t *)&atoms72[i * 72 + 8];
                    if (fjc > 0x10000 && (fjc >> 32) == 0x7b) {
                        uint64_t *fj0 = (uint64_t *)(uintptr_t)fjc;
                        uint32_t type = (uint32_t)((fj0[2] >> 1) & 0x7f);
                        GPU_LOG("  [FRAGMENT_INSPECT] fjc=0x%llx: type=%u, w2=0x%llx, next=0x%llx, w4=0x%llx, w5=0x%llx (tag=%u)",
                                (unsigned long long)fjc, type, (unsigned long long)fj0[2],
                                (unsigned long long)fj0[3], (unsigned long long)fj0[4],
                                (unsigned long long)fj0[5], (unsigned int)(fj0[5] & 0x3));
                        if (fj0[5] > 0x10000 && ((fj0[5] & ~3ULL) >> 32) == 0x7b) {
                            const uint64_t *fbd0 = (const uint64_t *)(uintptr_t)(fj0[5] & ~3ULL);
                            GPU_LOG("    [FBD0_DUMP] fbd0 @ 0x%llx:", (unsigned long long)(fj0[5] & ~3ULL));
                            for (int fw = 0; fw < 8; fw += 4) {
                                GPU_LOG("      fbd0[%02d..%02d]: 0x%016llx 0x%016llx 0x%016llx 0x%016llx",
                                        fw, fw + 3, fbd0[fw], fbd0[fw+1], fbd0[fw+2], fbd0[fw+3]);
                            }
                        }
                        /* NO omitir Job 1: permitir que libmali ejecute su cadena completa */
                        if (0 && type == 9 && fj0[4] == 0 && (fj0[5] & 0x3) == 1 && fj0[3] > 0x10000) {
                            uint64_t real_fj = fj0[3];
                            uint64_t *fj1 = (uint64_t *)(uintptr_t)real_fj;
                            *(uint64_t *)&atoms72[i * 72 + 8] = real_fj;
                            fj1[2] &= ~0x0000ffff00000000ULL; // dep1 = 0
                            fj1[2] |= (1ULL << 8); // job_barrier = 1
                            flush_dcache_range(real_fj, 64);
                            GPU_LOG("  [FRAGMENT_FIX] Omitido dummy SFBD job @ 0x%llx! Átomo[%u] ahora inicia en MFBD @ 0x%llx (dep1=0, bar=1, creq=0x%x)",
                                    (unsigned long long)fjc, i, (unsigned long long)real_fj, *creq_ptr);
                        }
                    }
                }
            }

            for (uint32_t i = 0; i < out_nr_atoms; i++) {
                uint32_t creq = *(const uint32_t *)&atoms72[i * 72 + 52];
                uint64_t sjc = *(const uint64_t *)&atoms72[i * 72 + 8];
                if (!(creq & 0x200) && sjc > 0x10000 && (sjc >> 32) == 0x7b) {
                    uint64_t cur = sjc;
                    int step = 0;
                    while (cur > 0x10000 && (cur >> 32) == 0x7b && step < 16) {
                        const uint64_t *p = (const uint64_t *)(uintptr_t)cur;
                        uint32_t type = (uint32_t)((p[2] >> 1) & 0x7f);
                        uint32_t jidx = (uint32_t)(p[2] >> 16) & 0xffff;
                        uint32_t dep1 = (uint32_t)(p[2] >> 32) & 0xffff;
                        uint32_t dep2 = (uint32_t)(p[2] >> 48) & 0xffff;
                        uint32_t bar = (uint32_t)(p[2] >> 8) & 0x1;

                        /* Valhall v9 Job Header 128-byte Alignment Fix:
                         * En Valhall v9, el Job Header y el campo Next DEBEN estar alineados a 128 bytes.
                         * Si un job apunta a un siguiente job que NO está alineado a 128 bytes (p[3] & 0x7f != 0),
                         * copiamos el/los siguientes jobs a ranuras alineadas en 0x7befe74000 (+0x300, +0x400)
                         * y corregimos los punteros Next.
                         */
                        if (p[3] != 0 && (p[3] & 0x7f) != 0) {
                            uint64_t unaligned_next = p[3];
                            uint64_t *p_rw = (uint64_t *)p;
                            
                            // Usamos ranuras alineadas a 128B en la página 0x7befe74000 (+0x300, +0x400)
                            uint64_t page_base = (cur & ~0xfffULL);
                            uint64_t slot1 = page_base + 0x300; // alineado a 256B (y 128B)
                            uint64_t slot2 = page_base + 0x400; // alineado a 256B (y 128B)
                            
                            const uint64_t *src_job1 = (const uint64_t *)(uintptr_t)unaligned_next;
                            uint64_t *dst_job1 = (uint64_t *)(uintptr_t)slot1;
                            
                            GPU_LOG("  [VALHALL_ALIGN_128] Job @ 0x%llx tiene next=0x%llx desalineado (mod 128 = 0x%x)",
                                    (unsigned long long)cur, (unsigned long long)unaligned_next, (unsigned int)(unaligned_next & 0x7f));
                            
                            // Copiar job 1 (256 bytes = 32 palabras de 64 bits)
                            memcpy(dst_job1, src_job1, 256);
                            p_rw[3] = slot1; // Actualizar next del job actual

                            // Si el job 1 era type=5 (MALI_JOB_TYPE_VERTEX, no soportado en Valhall),
                            // convertir a type=4 (MALI_JOB_TYPE_COMPUTE)
                            uint32_t j1_type = (uint32_t)((dst_job1[2] >> 1) & 0x7f);
                            if (j1_type == 5) {
                                dst_job1[2] = (dst_job1[2] & ~0xfeULL) | (4ULL << 1);
                                GPU_LOG("  [VALHALL_JOB_FIX] Convertido JC[1.4] type 5 (VERTEX) -> 4 (COMPUTE)");
                            }
                            
                            // Si el job 1 también tiene un next desalineado, copiarlo a slot2
                            if (dst_job1[3] != 0 && (dst_job1[3] & 0x7f) != 0) {
                                const uint64_t *src_job2 = (const uint64_t *)(uintptr_t)dst_job1[3];
                                uint64_t *dst_job2 = (uint64_t *)(uintptr_t)slot2;
                                GPU_LOG("  [VALHALL_ALIGN_128] Siguiente job @ 0x%llx desalineado -> copiando a 0x%llx",
                                        (unsigned long long)dst_job1[3], (unsigned long long)slot2);
                                memcpy(dst_job2, src_job2, 256);
                                dst_job1[3] = slot2;
                                flush_dcache_range((uintptr_t)dst_job2, 256);
                            }
                            
                            flush_dcache_range((uintptr_t)p_rw, 64);
                            flush_dcache_range((uintptr_t)dst_job1, 256);
                            GPU_LOG("  [VALHALL_ALIGN_128] Re-alineado: next ahora apunta a 0x%llx (128B aligned)", (unsigned long long)slot1);
                        }
                        if (type == 9 && step == 0 && p[4] == 0 && p[3] != 0) {
                            uint64_t next_job = p[3];
                            const uint64_t *next_p = (const uint64_t *)(uintptr_t)next_job;
                            uint64_t *cur_rw = (uint64_t *)(uintptr_t)cur;
                            GPU_LOG("  [VALHALL_MERGE_FRAGMENT] Fusionando fragment job @ 0x%llx sobre pre-frame @ 0x%llx (128B aligned)",
                                    (unsigned long long)next_job, (unsigned long long)cur);
                            cur_rw[4] = next_p[4]; /* Bound Min/Max X/Y */
                            cur_rw[5] = next_p[5]; /* Framebuffer Pointer con tag 3 */
                            cur_rw[6] = next_p[6]; /* Tile Enable Map */
                            cur_rw[7] = next_p[7]; /* Tile Enable Map Row Stride */
                            cur_rw[2] &= 0x00000000ffffffffULL; /* Limpiar dep1 y dep2 */
                            cur_rw[3] = 0;                       /* next = 0 */
                            flush_dcache_range((uintptr_t)cur_rw, 128);
                            p = cur_rw;
                        }

                        if (type == 9 && (p[5] & 0x3) == 1) {
                            uint64_t *p_rw = (uint64_t *)p;
                            GPU_LOG("  [FBD_TAG_FIX] Corrigiendo FBD tag en JC[%u.%d]: 0x%llx -> tag 3 (MFBD para Valhall)",
                                    i, step, (unsigned long long)p[5]);
                            p_rw[5] = (p_rw[5] & ~0x3ULL) | 0x3ULL;
                            flush_dcache_range((uintptr_t)&p_rw[5], 64);
                        }
                        GPU_LOG("  [BEFORE_SUBMIT] ATOM[%u] JC[%u.%d] @ 0x%llx: type=%u, idx=0x%x, dep1=0x%x, dep2=0x%x, bar=%u, next=0x%llx",
                                i, i, step, (unsigned long long)cur, type, jidx, dep1, dep2, bar, (unsigned long long)p[3]);
                        for (int w = 0; w < 32; w += 4) {
                            GPU_LOG("    w[%02d..%02d]: 0x%016llx 0x%016llx 0x%016llx 0x%016llx",
                                    w, w + 3,
                                    (unsigned long long)p[w], (unsigned long long)p[w + 1],
                                    (unsigned long long)p[w + 2], (unsigned long long)p[w + 3]);
                        }
                        if (type == 9 && (p[5] >> 32) == 0x7b) {
                            uint64_t fb_ptr = p[5] & ~0x3ULL;
                            flush_dcache_range((uintptr_t)fb_ptr, 256);
                            const uint64_t *fb = (const uint64_t *)(uintptr_t)fb_ptr;
                            GPU_LOG("    [FB_DUMP] w5=0x%llx (tag=%u, base=0x%llx):",
                                    (unsigned long long)p[5], (unsigned int)(p[5] & 0x3), (unsigned long long)fb_ptr);
                            for (int fw = 0; fw < 32; fw += 4) {
                                GPU_LOG("      fb[%02d..%02d]: 0x%016llx 0x%016llx 0x%016llx 0x%016llx",
                                        fw, fw + 3,
                                        (unsigned long long)fb[fw], (unsigned long long)fb[fw + 1],
                                        (unsigned long long)fb[fw + 2], (unsigned long long)fb[fw + 3]);
                            }
                            if (fb[2] > 0x10000 && (fb[2] >> 32) == 0x7b) {
                                const uint64_t *sub_ptr = (const uint64_t *)(uintptr_t)(fb[2] & ~0x3ULL);
                                GPU_LOG("      [FB_SUB fb[2]] @ 0x%llx: 0x%016llx 0x%016llx 0x%016llx 0x%016llx",
                                        (unsigned long long)fb[2], sub_ptr[0], sub_ptr[1], sub_ptr[2], sub_ptr[3]);
                            }
                            if (fb[3] > 0x10000 && (fb[3] >> 32) == 0x7b) {
                                const uint64_t *sub_ptr = (const uint64_t *)(uintptr_t)(fb[3] & ~0x3ULL);
                                GPU_LOG("      [FB_SUB fb[3]] @ 0x%llx: 0x%016llx 0x%016llx 0x%016llx 0x%016llx",
                                        (unsigned long long)fb[3], sub_ptr[0], sub_ptr[1], sub_ptr[2], sub_ptr[3]);
                            }
                            if (fb[7] > 0x10000 && (fb[7] >> 32) == 0x7b) {
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
            usleep(25000);

            // Log de estado AFTER submit para todas las cadenas ejecutadas (solo hardware jobs)
            for (uint32_t i = 0; i < out_nr_atoms; i++) {
                const uint64_t *a72 = (const uint64_t *)&atoms72[i * 72];
                uint32_t core_req = *(const uint32_t *)&atoms72[i * 72 + 52];
                uint64_t jc_start = a72[1];
                if (!(core_req & 0x200) && jc_start > 0x10000 && (jc_start >> 32) == 0x7b) {
                    uint64_t cur = jc_start;
                    int step = 0;
                    while (cur > 0x10000 && (cur >> 32) == 0x7b && step < 16) {
                        const uint64_t *jc_ptr = (const uint64_t *)(uintptr_t)cur;
                        GPU_LOG("    ATOM[%u] JC[%u.%d] AFTER @ 0x%llx: status=0x%016llx fault_ptr=0x%016llx w2=0x%016llx next=0x%016llx w4=0x%016llx w5=0x%016llx",
                                i, i, step, (unsigned long long)cur,
                                (unsigned long long)jc_ptr[0], (unsigned long long)jc_ptr[1],
                                (unsigned long long)jc_ptr[2], (unsigned long long)jc_ptr[3],
                                (unsigned long long)jc_ptr[4], (unsigned long long)jc_ptr[5]);
                        cur = jc_ptr[3];
                        step++;
                    }
                }
            }
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
                        if (*v == 0x9093) {
                            GPU_LOG("ioctl GET_GPUPROPS: Parcheado prop_id=1 0x%x -> 0x9091 (Mali-G57 Valhall)", *v);
                            *v = 0x9091;
                        }
                    } else if (prop_id == 55 && val_size == 4) {
                        uint32_t *v = (uint32_t *)(buf + pos);
                        if (*v == 0x90930010) {
                            GPU_LOG("ioctl GET_GPUPROPS: Parcheado prop_id=55 0x%x -> 0x90910010 (Mali-G57 Valhall)", *v);
                            *v = 0x90910010;
                        }
                    }
                    pos += val_size;
                }
            }
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
                             uint32_t format, uint64_t size) {
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
            pthread_mutex_unlock(&g_dumb_mutex);
            GPU_LOG("Registrado dumb bo %p (slot %d, handle %u, prime_fd %d, %ux%u, stride %u)",
                    (void*)bo, i, dumb_handle, prime_fd, width, height, stride);
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
        kmsg_log("[GPU-GBM] ERROR: No se detectó descriptor de card0 para crear dumb buffer\n");
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

    register_dumb_bo(bo, creq.handle, card_fd, saved_prime_fd, width, height, creq.pitch, format, creq.size);
    kmsg_log("[GPU-GBM] Dumb BO registrado OK: bo=%p, handle=%u, prime_fd=%d, pitch=%u, size=%llu\n",
             (void*)bo, creq.handle, saved_prime_fd, creq.pitch, (unsigned long long)creq.size);
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
        kmsg_log("[GPU-GBM] gbm_bo_get_handle(bo=%p) -> DUMB handle=%u\n", (void*)bo, h.u32);
        return h;
    }
    pthread_mutex_unlock(&g_dumb_mutex);
    if (real_gbm_bo_get_handle) {
        union gbm_bo_handle h = real_gbm_bo_get_handle(bo);
        kmsg_log("[GPU-GBM] gbm_bo_get_handle(bo=%p) -> REAL handle=%u\n", (void*)bo, h.u32);
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
        void *mali = dlopen("/usr/lib/libmali-bifrost-patched.so", RTLD_LAZY | RTLD_GLOBAL);
        if (!mali) mali = dlopen("/usr/lib/libmali-bifrost.so", RTLD_LAZY | RTLD_GLOBAL);
        if (mali) real_glFlush = dlsym(mali, "glFlush");
        if (!real_glFlush) real_glFlush = dlsym(RTLD_NEXT, "glFlush");
    }
    if (real_glFlush) real_glFlush();
}

void glFinish(void) {
    if (!real_glFinish) {
        void *mali = dlopen("/usr/lib/libmali-bifrost-patched.so", RTLD_LAZY | RTLD_GLOBAL);
        if (!mali) mali = dlopen("/usr/lib/libmali-bifrost.so", RTLD_LAZY | RTLD_GLOBAL);
        if (mali) real_glFinish = dlsym(mali, "glFinish");
        if (!real_glFinish) real_glFinish = dlsym(RTLD_NEXT, "glFinish");
    }
    if (real_glFinish) real_glFinish();
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
        void *mali = dlopen("/usr/lib/libmali-bifrost-patched.so", RTLD_LAZY | RTLD_GLOBAL);
        if (!mali) mali = dlopen("/usr/lib/libmali-bifrost.so", RTLD_LAZY | RTLD_GLOBAL);
        if (mali) real_glDrawArrays = dlsym(mali, "glDrawArrays");
        if (!real_glDrawArrays) real_glDrawArrays = dlsym(RTLD_NEXT, "glDrawArrays");
    }
    kmsg_log("[GPU-GL] >>> glDrawArrays: mode=0x%x, first=%d, count=%d\n", mode, first, count);
    if (real_glDrawArrays) {
        real_glDrawArrays(mode, first, count);
    }
    glFinish();
    kmsg_log("[GPU-GL] <<< glDrawArrays RET\n");
}

__attribute__((visibility("default")))
void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices) {
    if (!real_glDrawElements) {
        void *mali = dlopen("/usr/lib/libmali-bifrost-patched.so", RTLD_LAZY | RTLD_GLOBAL);
        if (!mali) mali = dlopen("/usr/lib/libmali-bifrost.so", RTLD_LAZY | RTLD_GLOBAL);
        if (mali) real_glDrawElements = dlsym(mali, "glDrawElements");
        if (!real_glDrawElements) real_glDrawElements = dlsym(RTLD_NEXT, "glDrawElements");
    }
    kmsg_log("[GPU-GL] >>> glDrawElements: mode=0x%x, count=%d, type=0x%x\n", mode, count, type);
    if (real_glDrawElements) {
        real_glDrawElements(mode, count, type, indices);
    }
    glFinish();
    kmsg_log("[GPU-GL] <<< glDrawElements RET\n");
}

static void (*real_glClear)(unsigned int) = NULL;
__attribute__((visibility("default")))
void glClear(unsigned int mask) {
    if (!real_glClear) {
        void *mali = dlopen("/usr/lib/libmali-bifrost-patched.so", RTLD_LAZY | RTLD_GLOBAL);
        if (!mali) mali = dlopen("/usr/lib/libmali-bifrost.so", RTLD_LAZY | RTLD_GLOBAL);
        if (mali) real_glClear = dlsym(mali, "glClear");
        if (!real_glClear) real_glClear = dlsym(RTLD_NEXT, "glClear");
    }
    kmsg_log("[GPU-GL] >>> glClear: mask=0x%x\n", mask);
    if (real_glClear) {
        real_glClear(mask);
    }
    kmsg_log("[GPU-GL] <<< glClear RET\n");
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
    glFinish();
    kmsg_log("[GPU-EGL] >>> eglSwapBuffers: dpy=%p, surf=%p\n", dpy, surface);
    EGLBoolean ret = real_eglSwapBuffers ? real_eglSwapBuffers(dpy, surface) : 0;
    glFinish();
    kmsg_log("[GPU-EGL] <<< eglSwapBuffers RET=%u\n", ret);
    return ret;
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

        /* Si el dma_fd recibido no es válido o está cerrado, buscar un prime_fd válido en dumb table */
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
        if (matched_entry && fd_idx >= 0 && fcntl(dma_fd, F_GETFD) < 0 && matched_entry->prime_fd >= 0) {
            patched_attribs[fd_idx] = matched_entry->prime_fd;
            dma_fd = matched_entry->prime_fd;
        }
        pthread_mutex_unlock(&g_dumb_mutex);

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

        kmsg_log("[GPU-EGL] <<< eglCreateImageKHR(DMA_BUF) RET=%p\n", (void*)img);
        return img;
    }

    kmsg_log("[GPU-EGL] >>> eglCreateImageKHR: target=0x%x, buf=%p\n", target, (void*)buffer);
    EGLImageKHR img = real_eglCreateImageKHR ? real_eglCreateImageKHR(dpy, ctx, target, buffer, attrib_list) : NULL;
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

int drmModeSetCrtc(int fd, uint32_t crtcId, uint32_t bufferId,
                   uint32_t x, uint32_t y, uint32_t *connectors, int count,
                   drmModeModeInfoPtr mode)
{
    if (fd >= 0) g_kms_fd = fd;
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

int drmModePageFlip(int fd, uint32_t crtc_id, uint32_t fb_id,
                    uint32_t flags, void *user_data)
{
    if (fd >= 0) g_kms_fd = fd;
    static int (*real_drmModePageFlip)(int, uint32_t, uint32_t, uint32_t, void *) = NULL;
    if (!real_drmModePageFlip) {
        real_drmModePageFlip = dlsym(RTLD_NEXT, "drmModePageFlip");
        if (!real_drmModePageFlip) {
            void *ldrm = dlopen("/usr/lib/libdrm.so.2", RTLD_LAZY | RTLD_GLOBAL);
            if (ldrm) real_drmModePageFlip = dlsym(ldrm, "drmModePageFlip");
        }
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
    kmsg_log("[GPU-KMS] drmModeSetCursor(crtc=%u, handle=%u, %ux%u) -> forzar software cursor (-EINVAL)\n",
             crtcId, bo_handle, width, height);
    errno = EINVAL;
    return -1;
}

__attribute__((visibility("default")))
int drmModeSetCursor2(int fd, uint32_t crtcId, uint32_t bo_handle,
                      uint32_t width, uint32_t height, int32_t hot_x, int32_t hot_y)
{
    kmsg_log("[GPU-KMS] drmModeSetCursor2(crtc=%u, handle=%u, %ux%u) -> forzar software cursor (-EINVAL)\n",
             crtcId, bo_handle, width, height);
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
    GPU_LOG(">>> DRIVER DE INTERFAZ GPU MALI-G57 MC2 INICIALIZADO <<<");
}
