#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>

/* El controlador mediatek_drm del kernel MT6789 provoca un reset hardware si se
 * invoca DRM_IOCTL_MODE_CURSOR (0xa3) o CURSOR2 (0xbb), incluso al desactivar
 * el cursor hardware durante el modesetting de wlroots. */
int drmIoctl(int fd, unsigned long request, void *arg) {
    static int (*real_drmIoctl)(int, unsigned long, void *) = NULL;
    if (!real_drmIoctl) real_drmIoctl = dlsym(RTLD_NEXT, "drmIoctl");
    unsigned int nr = (unsigned int)(request & 0xff);
    if (nr == 0xa3 || nr == 0xbb) {
        return 0;
    }
    return real_drmIoctl ? real_drmIoctl(fd, request, arg) : -1;
}

/* ============================================================================
 * hybris_gbm_bridge.c - Conector intermedio GBM/DRM <-> libhybris (libGLES_mali)
 *
 * Permite que aplicaciones y compositores Linux (kmscube, Phoc/wlroots)
 * rendericen 100% por hardware en la GPU Mali-G57 MC2 (Valhall r32p1) usando
 * el driver nativo Android libGLES_mali.so a través de libhybris + DMA-BUF.
 * ============================================================================ */

#define EGL_NO_DISPLAY ((void *)0)
#define EGL_NO_CONTEXT ((void *)0)
#define EGL_NO_SURFACE ((void *)0)
#define EGL_DEFAULT_DISPLAY ((void *)0)
#define EGL_TRUE 1
#define EGL_FALSE 0
#define EGL_NONE 0x3038
#define EGL_WIDTH 0x3057
#define EGL_HEIGHT 0x3056
#define EGL_NATIVE_PIXMAP_KHR 0x30B0
#define EGL_PLATFORM_GBM_KHR 0x31D7
#define EGL_PLATFORM_GBM_MESA 0x31D7
#define EGL_PLATFORM_WAYLAND_KHR 0x31D8
#define EGL_PLATFORM_WAYLAND_EXT 0x31D8
#define EGL_LINUX_DMA_BUF_EXT 0x3270
#define EGL_LINUX_DRM_FOURCC_EXT 0x3271
#define EGL_DMA_BUF_PLANE0_FD_EXT 0x3272
#define EGL_DMA_BUF_PLANE0_OFFSET_EXT 0x3273
#define EGL_DMA_BUF_PLANE0_PITCH_EXT 0x3274
#define EGL_SURFACE_TYPE 0x3033
#define EGL_WINDOW_BIT 0x0004
#define EGL_PBUFFER_BIT 0x0001
#define EGL_OPENGL_ES2_BIT 0x0004
#define EGL_RENDERABLE_TYPE 0x3040

#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_STENCIL_ATTACHMENT 0x8D20
#define GL_DEPTH24_STENCIL8_OES 0x88F0

#define GBM_FORMAT_XRGB8888 0x34325258
#define GBM_FORMAT_ARGB8888 0x34325241
#define GBM_FORMAT_XBGR8888 0x34324258
#define GBM_FORMAT_ABGR8888 0x34324241
#define GBM_BO_IMPORT_FD 0x5503
#define GBM_BO_IMPORT_FD_MODIFIER 0x5504

#define GBM_MAGIC 0x47424D44U /* 'GBMD' */
#define GBM_SURF_MAGIC 0x47424D53U /* 'GBMS' */
#define NUM_SWAP_BUFFERS 3

struct gbm_device {
    uint32_t magic;
    int fd;
};

struct gbm_bo {
    struct gbm_device *gbm;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
    uint32_t handle;
    int prime_fd;
    uint64_t size;
    uint64_t modifier;
    void *user_data;
    void (*destroy_user_data)(struct gbm_bo *, void *);
    int locked;
    int is_imported;
};

struct gbm_surface {
    uint32_t magic;
    struct gbm_device *gbm;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t flags;
    struct gbm_bo *bos[NUM_SWAP_BUFFERS];
    void *egl_images[NUM_SWAP_BUFFERS];
    uint32_t rbos[NUM_SWAP_BUFFERS];
    uint32_t depth_rbo;
    uint32_t fbos[NUM_SWAP_BUFFERS];
    int back_idx;
    int front_idx;
    void *egl_dpy;
};

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

struct gbm_import_fd_modifier_data {
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t num_fds;
    int fds[4];
    int strides[4];
    int offsets[4];
    uint64_t modifier;
};

/* Punteros a libhybris (libEGL.so.1 y libGLESv2.so.2) */
static void *h_egl = NULL;
static void *h_gles2 = NULL;

static const char *(*p_eglQueryString)(void *, int) = NULL;
static unsigned int (*p_eglSwapBuffersWithDamageEXT)(void *, void *, const int *, int) = NULL;
static void *(*p_eglGetDisplay)(void *) = NULL;
static unsigned int (*p_eglInitialize)(void *, int *, int *) = NULL;
static void *(*p_eglGetProcAddress)(const char *) = NULL;
static unsigned int (*p_eglMakeCurrent)(void *, void *, void *, void *) = NULL;
static unsigned int (*p_eglSwapBuffers)(void *, void *) = NULL;
static unsigned int (*p_eglDestroySurface)(void *, void *) = NULL;
static void *(*p_eglCreateImageKHR)(void *, void *, unsigned int, void *, const int *) = NULL;
static unsigned int (*p_eglDestroyImageKHR)(void *, void *) = NULL;
static void *(*p_eglCreateWindowSurface)(void *, void *, void *, const int *) = NULL;
static unsigned int (*p_eglQuerySurface)(void *, void *, int, int *) = NULL;
static unsigned int (*p_eglChooseConfig)(void *, const int *, void **, int, int *) = NULL;
static unsigned int (*p_eglGetConfigAttrib)(void *, void *, int, int *) = NULL;

static void (*p_glGenFramebuffers)(int, uint32_t *) = NULL;
static void (*p_glDeleteFramebuffers)(int, const uint32_t *) = NULL;
static void (*p_glBindFramebuffer)(unsigned int, uint32_t) = NULL;
static void (*p_glGenRenderbuffers)(int, uint32_t *) = NULL;
static void (*p_glDeleteRenderbuffers)(int, const uint32_t *) = NULL;
static void (*p_glBindRenderbuffer)(unsigned int, uint32_t) = NULL;
static void (*p_glRenderbufferStorage)(unsigned int, unsigned int, int, int) = NULL;
static void (*p_glFramebufferRenderbuffer)(unsigned int, unsigned int, unsigned int, uint32_t) = NULL;
static void (*p_glEGLImageTargetRenderbufferStorageOES)(unsigned int, void *) = NULL;
static void (*p_glFinish)(void) = NULL;

static pthread_once_t g_init_once = PTHREAD_ONCE_INIT;
static __thread void *tls_current_surf = NULL;
static __thread uint32_t tls_current_fbo = 0;

static void init_hybris_syms(void) {
    setenv("HYBRIS_EGLPLATFORM", "null", 1);
    setenv("HYBRIS_LD_LIBRARY_PATH", "/usr/lib/mali_android", 0);
    setenv("HYBRIS_ANDROID_SDK_VERSION", "30", 0);
    h_egl = dlopen("/usr/lib/libhybris_mali/libhybris_EGL.so.1", RTLD_NOW | RTLD_GLOBAL);
    if (!h_egl) h_egl = dlopen("/usr/lib/libhybris_mali/libEGL.so.1.0.0", RTLD_NOW | RTLD_GLOBAL);
    if (!h_egl) h_egl = dlopen("libEGL.so.1", RTLD_LAZY | RTLD_GLOBAL);
    h_gles2 = dlopen("/usr/lib/libhybris_mali/libGLESv2.so.2", RTLD_NOW | RTLD_GLOBAL);
    if (!h_gles2) h_gles2 = dlopen("libGLESv2.so.2", RTLD_LAZY | RTLD_GLOBAL);

    if (h_egl) {
        p_eglQueryString = dlsym(h_egl, "eglQueryString");
        p_eglGetDisplay = dlsym(h_egl, "eglGetDisplay");
        p_eglInitialize = dlsym(h_egl, "eglInitialize");
        p_eglGetProcAddress = dlsym(h_egl, "eglGetProcAddress");
        p_eglMakeCurrent = dlsym(h_egl, "eglMakeCurrent");
        p_eglSwapBuffers = dlsym(h_egl, "eglSwapBuffers");
        p_eglDestroySurface = dlsym(h_egl, "eglDestroySurface");
        p_eglCreateWindowSurface = dlsym(h_egl, "eglCreateWindowSurface");
        p_eglQuerySurface = dlsym(h_egl, "eglQuerySurface");
        p_eglChooseConfig = dlsym(h_egl, "eglChooseConfig");
        p_eglGetConfigAttrib = dlsym(h_egl, "eglGetConfigAttrib");
        if (p_eglGetProcAddress) {
            p_eglCreateImageKHR = p_eglGetProcAddress("eglCreateImageKHR");
            p_eglDestroyImageKHR = p_eglGetProcAddress("eglDestroyImageKHR");
            p_eglSwapBuffersWithDamageEXT = p_eglGetProcAddress("eglSwapBuffersWithDamageEXT");
            p_glEGLImageTargetRenderbufferStorageOES = p_eglGetProcAddress("glEGLImageTargetRenderbufferStorageOES");
        }
    }
    if (h_gles2) {
        p_glGenFramebuffers = dlsym(h_gles2, "glGenFramebuffers");
        p_glDeleteFramebuffers = dlsym(h_gles2, "glDeleteFramebuffers");
        p_glBindFramebuffer = dlsym(h_gles2, "glBindFramebuffer");
        p_glGenRenderbuffers = dlsym(h_gles2, "glGenRenderbuffers");
        p_glDeleteRenderbuffers = dlsym(h_gles2, "glDeleteRenderbuffers");
        p_glBindRenderbuffer = dlsym(h_gles2, "glBindRenderbuffer");
        p_glRenderbufferStorage = dlsym(h_gles2, "glRenderbufferStorage");
        p_glFramebufferRenderbuffer = dlsym(h_gles2, "glFramebufferRenderbuffer");
        p_glFinish = dlsym(h_gles2, "glFinish");
    }
}

static void ensure_init(void) {
    pthread_once(&g_init_once, init_hybris_syms);
}

/* Mapea formatos GBM (XRGB8888/ARGB8888) a FourCC compatibles con Mali DMA-BUF */
static uint32_t gbm_to_mali_fourcc(uint32_t fmt) {
    switch (fmt) {
        case GBM_FORMAT_XRGB8888:
        case GBM_FORMAT_ARGB8888:
            /* Mali DMA-BUF acepta ABGR8888 / XBGR8888 directamente o ARGB8888 */
            return fmt;
        default:
            return fmt;
    }
}

/* ============================================================================
 * Implementación completa de libgbm.so.1 sobre DRM Dumb + PRIME DMA-BUF
 * ============================================================================ */

struct gbm_device *gbm_create_device(int fd) {
    ensure_init();
    struct gbm_device *gbm = calloc(1, sizeof(struct gbm_device));
    if (!gbm) return NULL;
    gbm->magic = GBM_MAGIC;
    gbm->fd = fd;
    return gbm;
}

void gbm_device_destroy(struct gbm_device *gbm) {
    free(gbm);
}

int gbm_device_get_fd(struct gbm_device *gbm) {
    return gbm ? gbm->fd : -1;
}

const char *gbm_device_get_backend_name(struct gbm_device *gbm) {
    (void)gbm;
    return "drm_hybris_mali";
}

int gbm_device_is_format_supported(struct gbm_device *gbm, uint32_t format, uint32_t usage) {
    (void)gbm; (void)usage;
    return (format == GBM_FORMAT_XRGB8888 || format == GBM_FORMAT_ARGB8888 ||
            format == GBM_FORMAT_XBGR8888 || format == GBM_FORMAT_ABGR8888);
}

int gbm_device_get_format_modifier_plane_count(struct gbm_device *gbm, uint32_t format, uint64_t modifier) {
    (void)gbm; (void)format; (void)modifier;
    return 1;
}

struct gbm_bo *gbm_bo_create(struct gbm_device *gbm, uint32_t width, uint32_t height,
                             uint32_t format, uint32_t flags) {
    (void)flags;
    if (!gbm || width == 0 || height == 0) return NULL;

    struct drm_mode_create_dumb creq = {
        .width = width,
        .height = height,
        .bpp = 32
    };
    if (ioctl(gbm->fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
        return NULL;
    }

    struct drm_prime_handle prime = {
        .handle = creq.handle,
        .flags = DRM_CLOEXEC | DRM_RDWR,
        .fd = -1
    };
    if (ioctl(gbm->fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &prime) < 0) {
        struct drm_mode_destroy_dumb dreq = { .handle = creq.handle };
        ioctl(gbm->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
        return NULL;
    }

    struct gbm_bo *bo = calloc(1, sizeof(struct gbm_bo));
    if (!bo) {
        close(prime.fd);
        struct drm_mode_destroy_dumb dreq = { .handle = creq.handle };
        ioctl(gbm->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
        return NULL;
    }
    bo->gbm = gbm;
    bo->width = width;
    bo->height = height;
    bo->stride = creq.pitch;
    bo->format = format;
    bo->handle = creq.handle;
    bo->prime_fd = prime.fd;
    bo->size = creq.size;
    bo->modifier = 0; /* DRM_FORMAT_MOD_LINEAR */
    return bo;
}

struct gbm_bo *gbm_bo_create_with_modifiers(struct gbm_device *gbm, uint32_t width, uint32_t height,
                                            uint32_t format, const uint64_t *modifiers, unsigned int count) {
    (void)modifiers; (void)count;
    return gbm_bo_create(gbm, width, height, format, 0);
}

struct gbm_bo *gbm_bo_create_with_modifiers2(struct gbm_device *gbm, uint32_t width, uint32_t height,
                                             uint32_t format, const uint64_t *modifiers, unsigned int count, uint32_t flags) {
    (void)modifiers; (void)count;
    return gbm_bo_create(gbm, width, height, format, flags);
}

struct gbm_bo *gbm_bo_import(struct gbm_device *gbm, uint32_t type, void *buffer, uint32_t usage) {
    (void)usage;
    if (!gbm || !buffer) return NULL;
    int fd = -1;
    uint32_t w = 0, h = 0, stride = 0, fmt = GBM_FORMAT_ARGB8888;
    uint64_t mod = 0;
    if (type == GBM_BO_IMPORT_FD) {
        struct gbm_import_fd_data *d = buffer;
        fd = d->fd; w = d->width; h = d->height; stride = d->stride; fmt = d->format;
    } else if (type == GBM_BO_IMPORT_FD_MODIFIER) {
        struct gbm_import_fd_modifier_data *d = buffer;
        fd = d->fds[0]; w = d->width; h = d->height; stride = (uint32_t)d->strides[0]; fmt = d->format; mod = d->modifier;
    } else {
        return NULL;
    }

    struct drm_prime_handle prime = { .fd = fd, .handle = 0 };
    if (ioctl(gbm->fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &prime) < 0) {
        return NULL;
    }

    struct gbm_bo *bo = calloc(1, sizeof(struct gbm_bo));
    if (!bo) return NULL;
    bo->gbm = gbm;
    bo->width = w;
    bo->height = h;
    bo->stride = stride;
    bo->format = fmt;
    bo->handle = prime.handle;
    bo->prime_fd = dup(fd);
    bo->modifier = mod;
    bo->is_imported = 1;
    return bo;
}

void gbm_bo_destroy(struct gbm_bo *bo) {
    if (!bo) return;
    if (bo->destroy_user_data) bo->destroy_user_data(bo, bo->user_data);
    if (bo->prime_fd >= 0) close(bo->prime_fd);
    if (bo->gbm && bo->handle && !bo->is_imported) {
        struct drm_mode_destroy_dumb dreq = { .handle = bo->handle };
        ioctl(bo->gbm->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
    }
    free(bo);
}

uint32_t gbm_bo_get_width(struct gbm_bo *bo) { return bo ? bo->width : 0; }
uint32_t gbm_bo_get_height(struct gbm_bo *bo) { return bo ? bo->height : 0; }
uint32_t gbm_bo_get_stride(struct gbm_bo *bo) { return bo ? bo->stride : 0; }
uint32_t gbm_bo_get_format(struct gbm_bo *bo) { return bo ? bo->format : 0; }
union gbm_bo_handle gbm_bo_get_handle(struct gbm_bo *bo) {
    union gbm_bo_handle h = {0};
    if (bo) h.u32 = bo->handle;
    return h;
}
int gbm_bo_get_fd(struct gbm_bo *bo) { return bo ? dup(bo->prime_fd) : -1; }
uint64_t gbm_bo_get_modifier(struct gbm_bo *bo) { return bo ? bo->modifier : 0; }
int gbm_bo_get_plane_count(struct gbm_bo *bo) { (void)bo; return 1; }
uint32_t gbm_bo_get_offset(struct gbm_bo *bo, int plane) { (void)bo; (void)plane; return 0; }
uint32_t gbm_bo_get_stride_for_plane(struct gbm_bo *bo, int plane) { return (plane == 0 && bo) ? bo->stride : 0; }
int gbm_bo_get_fd_for_plane(struct gbm_bo *bo, int plane) { return (plane == 0) ? gbm_bo_get_fd(bo) : -1; }
union gbm_bo_handle gbm_bo_get_handle_for_plane(struct gbm_bo *bo, int plane) {
    union gbm_bo_handle h = {0};
    if (plane == 0 && bo) h.u32 = bo->handle;
    return h;
}
void gbm_bo_set_user_data(struct gbm_bo *bo, void *data, void (*destroy_user_data)(struct gbm_bo *, void *)) {
    if (!bo) return;
    bo->user_data = data;
    bo->destroy_user_data = destroy_user_data;
}
void *gbm_bo_get_user_data(struct gbm_bo *bo) { return bo ? bo->user_data : NULL; }

struct gbm_device *gbm_bo_get_device(struct gbm_bo *bo) { return bo ? bo->gbm : NULL; }

void *gbm_bo_map(struct gbm_bo *bo, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                 uint32_t flags, uint32_t *stride, void **map_data) {
    (void)x; (void)y; (void)width; (void)height; (void)flags;
    if (!bo || bo->prime_fd < 0) return NULL;
    size_t sz = bo->size ? bo->size : (size_t)bo->stride * bo->height;
    void *p = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, bo->prime_fd, 0);
    if (p == MAP_FAILED) return NULL;
    if (stride) *stride = bo->stride;
    if (map_data) *map_data = p;
    return p;
}

void gbm_bo_unmap(struct gbm_bo *bo, void *map_data) {
    if (!bo || !map_data) return;
    size_t sz = bo->size ? bo->size : (size_t)bo->stride * bo->height;
    munmap(map_data, sz);
}

struct gbm_surface *gbm_surface_create(struct gbm_device *gbm, uint32_t width, uint32_t height,
                                       uint32_t format, uint32_t flags) {
    if (!gbm || width == 0 || height == 0) return NULL;
    struct gbm_surface *surf = calloc(1, sizeof(struct gbm_surface));
    if (!surf) return NULL;
    surf->magic = GBM_SURF_MAGIC;
    surf->gbm = gbm;
    surf->width = width;
    surf->height = height;
    surf->format = format;
    surf->flags = flags;
    for (int i = 0; i < NUM_SWAP_BUFFERS; i++) {
        surf->bos[i] = gbm_bo_create(gbm, width, height, format, flags);
        if (!surf->bos[i]) {
            for (int j = 0; j < i; j++) gbm_bo_destroy(surf->bos[j]);
            free(surf);
            return NULL;
        }
    }
    surf->back_idx = 0;
    surf->front_idx = 0;
    return surf;
}

struct gbm_surface *gbm_surface_create_with_modifiers(struct gbm_device *gbm, uint32_t width, uint32_t height,
                                                      uint32_t format, const uint64_t *modifiers, unsigned int count) {
    (void)modifiers; (void)count;
    return gbm_surface_create(gbm, width, height, format, 0);
}

struct gbm_surface *gbm_surface_create_with_modifiers2(struct gbm_device *gbm, uint32_t width, uint32_t height,
                                                       uint32_t format, const uint64_t *modifiers, unsigned int count, uint32_t flags) {
    (void)modifiers; (void)count;
    return gbm_surface_create(gbm, width, height, format, flags);
}

void gbm_surface_destroy(struct gbm_surface *surf) {
    if (!surf) return;
    for (int i = 0; i < NUM_SWAP_BUFFERS; i++) {
        if (surf->fbos[i] && p_glDeleteFramebuffers) p_glDeleteFramebuffers(1, &surf->fbos[i]);
        if (surf->rbos[i] && p_glDeleteRenderbuffers) p_glDeleteRenderbuffers(1, &surf->rbos[i]);
        if (surf->egl_images[i] && p_eglDestroyImageKHR && surf->egl_dpy)
            p_eglDestroyImageKHR(surf->egl_dpy, surf->egl_images[i]);
        if (surf->bos[i]) gbm_bo_destroy(surf->bos[i]);
    }
    if (surf->depth_rbo && p_glDeleteRenderbuffers) p_glDeleteRenderbuffers(1, &surf->depth_rbo);
    free(surf);
}

struct gbm_bo *gbm_surface_lock_front_buffer(struct gbm_surface *surf) {
    if (!surf) return NULL;
    struct gbm_bo *bo = surf->bos[surf->front_idx];
    bo->locked = 1;
    return bo;
}

void gbm_surface_release_buffer(struct gbm_surface *surf, struct gbm_bo *bo) {
    (void)surf;
    if (bo) bo->locked = 0;
}

int gbm_surface_has_free_buffers(struct gbm_surface *surf) {
    if (!surf) return 0;
    for (int i = 0; i < NUM_SWAP_BUFFERS; i++) {
        if (!surf->bos[i]->locked) return 1;
    }
    return 0;
}

/* ============================================================================
 * Puente EGL / GLES2 para GBM y Wayland DMA-BUF <-> libhybris (libGLES_mali.so)
 * ============================================================================ */

#define WL_SURF_MAGIC 0x574C5352U /* 'WLSR' */
#define WL_EGL_WINDOW_VERSION 3
#define WL_MARSHAL_FLAG_DESTROY (1 << 0)

struct wl_egl_window {
    const intptr_t version;
    int width;
    int height;
    int dx;
    int dy;
    int attached_width;
    int attached_height;
    void *driver_private;
    void (*resize_callback)(struct wl_egl_window *, void *);
    void (*destroy_window_callback)(void *);
    void *surface;
};

struct wl_message_mini {
    const char *name;
    const char *signature;
    const void **types;
};

struct wl_interface_mini {
    const char *name;
    int version;
    int method_count;
    const struct wl_message_mini *methods;
    int event_count;
    const struct wl_message_mini *events;
};

struct dma_heap_alloc_data {
    uint64_t len;
    uint32_t fd;
    uint32_t fd_flags;
    uint64_t heap_flags;
};
#define DMA_HEAP_IOCTL_ALLOC _IOWR('H', 0x0, struct dma_heap_alloc_data)

static void *h_wl = NULL;
static const void *sym_wl_display_iface = NULL;
static const void *sym_wl_registry_iface = NULL;
static const void *sym_wl_surface_iface = NULL;
static const void *sym_wl_buffer_iface = NULL;

static void *(*p_wl_display_create_queue)(void *) = NULL;
static void (*p_wl_event_queue_destroy)(void *) = NULL;
static int (*p_wl_display_roundtrip_queue)(void *, void *) = NULL;
static int (*p_wl_display_dispatch_queue_pending)(void *, void *) = NULL;
static int (*p_wl_display_flush)(void *) = NULL;
static void (*p_wl_proxy_set_queue)(void *, void *) = NULL;
static void (*p_wl_proxy_destroy)(void *) = NULL;
static uint32_t (*p_wl_proxy_get_version)(void *) = NULL;
static int (*p_wl_proxy_add_listener)(void *, void (**)(void), void *) = NULL;
static void *(*p_wl_proxy_marshal_flags)(void *, uint32_t, const void *, uint32_t, uint32_t, ...) = NULL;

static struct wl_interface_mini wl_dmabuf_iface;
static struct wl_interface_mini wl_params_iface;
static const void *wl_dmabuf_types[16] = {0};
static struct wl_message_mini wl_dmabuf_reqs[4] = {
    { "destroy", "", wl_dmabuf_types },
    { "create_params", "n", wl_dmabuf_types + 6 },
    { "get_default_feedback", "4n", wl_dmabuf_types + 7 },
    { "get_surface_feedback", "4no", wl_dmabuf_types + 8 },
};
static struct wl_message_mini wl_dmabuf_evts[2] = {
    { "format", "u", wl_dmabuf_types },
    { "modifier", "3uuu", wl_dmabuf_types },
};
static struct wl_message_mini wl_params_reqs[4] = {
    { "destroy", "", wl_dmabuf_types },
    { "add", "huuuuu", wl_dmabuf_types },
    { "create", "iiuu", wl_dmabuf_types },
    { "create_immed", "2niiuu", wl_dmabuf_types + 10 },
};
static struct wl_message_mini wl_params_evts[2] = {
    { "created", "n", wl_dmabuf_types + 15 },
    { "failed", "", wl_dmabuf_types },
};

static int init_wl_syms(void) {
    if (p_wl_proxy_marshal_flags) return 1;
    h_wl = dlopen("libwayland-client.so.0", RTLD_LAZY | RTLD_GLOBAL);
    if (!h_wl) return 0;
    sym_wl_display_iface = dlsym(h_wl, "wl_display_interface");
    sym_wl_registry_iface = dlsym(h_wl, "wl_registry_interface");
    sym_wl_surface_iface = dlsym(h_wl, "wl_surface_interface");
    sym_wl_buffer_iface = dlsym(h_wl, "wl_buffer_interface");
    p_wl_display_create_queue = dlsym(h_wl, "wl_display_create_queue");
    p_wl_event_queue_destroy = dlsym(h_wl, "wl_event_queue_destroy");
    p_wl_display_roundtrip_queue = dlsym(h_wl, "wl_display_roundtrip_queue");
    p_wl_display_dispatch_queue_pending = dlsym(h_wl, "wl_display_dispatch_queue_pending");
    p_wl_display_flush = dlsym(h_wl, "wl_display_flush");
    p_wl_proxy_set_queue = dlsym(h_wl, "wl_proxy_set_queue");
    p_wl_proxy_destroy = dlsym(h_wl, "wl_proxy_destroy");
    p_wl_proxy_get_version = dlsym(h_wl, "wl_proxy_get_version");
    p_wl_proxy_add_listener = dlsym(h_wl, "wl_proxy_add_listener");
    p_wl_proxy_marshal_flags = dlsym(h_wl, "wl_proxy_marshal_flags");

    wl_dmabuf_types[6] = &wl_params_iface;
    wl_dmabuf_types[9] = sym_wl_surface_iface;
    wl_dmabuf_types[10] = sym_wl_buffer_iface;
    wl_dmabuf_types[15] = sym_wl_buffer_iface;

    wl_dmabuf_iface = (struct wl_interface_mini){ "zwp_linux_dmabuf_v1", 4, 4, wl_dmabuf_reqs, 2, wl_dmabuf_evts };
    wl_params_iface = (struct wl_interface_mini){ "zwp_linux_buffer_params_v1", 4, 4, wl_params_reqs, 2, wl_params_evts };

    return (sym_wl_display_iface && p_wl_proxy_marshal_flags && p_wl_display_create_queue);
}

struct wl_dpy_map {
    void *egl_dpy;
    void *wl_dpy;
    void *wl_queue;
    void *dmabuf;
    uint32_t dmabuf_ver;
};

#define MAX_WL_DPYS 8
static struct wl_dpy_map g_wl_dpys[MAX_WL_DPYS];
static pthread_mutex_t g_wl_lock = PTHREAD_MUTEX_INITIALIZER;

static void wl_reg_global(void *data, void *registry, uint32_t name, const char *iface, uint32_t ver) {
    struct wl_dpy_map *m = (struct wl_dpy_map *)data;
    if (strcmp(iface, "zwp_linux_dmabuf_v1") == 0) {
        uint32_t v = ver < 3 ? ver : 3;
        m->dmabuf = p_wl_proxy_marshal_flags(registry, 0 /* bind */, &wl_dmabuf_iface, v, 0,
                                             name, wl_dmabuf_iface.name, v, NULL);
        m->dmabuf_ver = v;
    }
}
static void wl_reg_global_remove(void *data, void *registry, uint32_t name) {
    (void)data; (void)registry; (void)name;
}
static void (*wl_reg_listener[2])(void) = {
    (void (*)(void))wl_reg_global,
    (void (*)(void))wl_reg_global_remove
};

static int is_wl_display(void *ptr) {
    if (!ptr || (uintptr_t)ptr < 0x10000) return 0;
    if (!init_wl_syms()) return 0;
    return (*(void **)ptr == sym_wl_display_iface);
}

static struct wl_dpy_map *find_wl_map_by_egl(void *egl_dpy) {
    for (int i = 0; i < MAX_WL_DPYS; i++) {
        if (g_wl_dpys[i].egl_dpy == egl_dpy) return &g_wl_dpys[i];
    }
    return NULL;
}

static void register_wl_dpy(void *egl_dpy, void *wl_dpy) {
    pthread_mutex_lock(&g_wl_lock);
    for (int i = 0; i < MAX_WL_DPYS; i++) {
        if (g_wl_dpys[i].egl_dpy == egl_dpy || !g_wl_dpys[i].egl_dpy) {
            g_wl_dpys[i].egl_dpy = egl_dpy;
            g_wl_dpys[i].wl_dpy = wl_dpy;
            break;
        }
    }
    pthread_mutex_unlock(&g_wl_lock);
}

static int ensure_wl_dmabuf(struct wl_dpy_map *m) {
    if (!m || !m->wl_dpy) return 0;
    if (!init_wl_syms()) return 0;
    if (m->dmabuf) return 1;
    m->wl_queue = p_wl_display_create_queue(m->wl_dpy);
    void *reg = p_wl_proxy_marshal_flags(m->wl_dpy, 1 /* get_registry */, sym_wl_registry_iface,
                                         p_wl_proxy_get_version(m->wl_dpy), 0, NULL);
    if (!reg) return 0;
    if (m->wl_queue) p_wl_proxy_set_queue(reg, m->wl_queue);
    p_wl_proxy_add_listener(reg, wl_reg_listener, m);
    p_wl_display_roundtrip_queue(m->wl_dpy, m->wl_queue);
    p_wl_proxy_destroy(reg);
    return (m->dmabuf != NULL);
}

struct wl_egl_bridge_surface {
    uint32_t magic;
    struct wl_egl_window *win;
    struct wl_dpy_map *map;
    void *egl_dpy;
    int width;
    int height;
    int dmabuf_fds[NUM_SWAP_BUFFERS];
    uint32_t strides[NUM_SWAP_BUFFERS];
    void *wl_bufs[NUM_SWAP_BUFFERS];
    int busy[NUM_SWAP_BUFFERS];
    void *egl_images[NUM_SWAP_BUFFERS];
    uint32_t rbos[NUM_SWAP_BUFFERS];
    uint32_t depth_rbo;
    uint32_t fbos[NUM_SWAP_BUFFERS];
    int back_idx;
};

static void wl_buf_release(void *data, void *wl_buffer) {
    struct wl_egl_bridge_surface *surf = (struct wl_egl_bridge_surface *)data;
    if (!surf || surf->magic != WL_SURF_MAGIC) return;
    for (int i = 0; i < NUM_SWAP_BUFFERS; i++) {
        if (surf->wl_bufs[i] == wl_buffer) {
            surf->busy[i] = 0;
            break;
        }
    }
}
static void (*wl_buf_listener[1])(void) = {
    (void (*)(void))wl_buf_release
};

static int is_gbm_device(void *ptr) {
    if (!ptr || (uintptr_t)ptr < 0x10000) return 0;
    return ((struct gbm_device *)ptr)->magic == GBM_MAGIC;
}

static int is_gbm_surface(void *ptr) {
    if (!ptr || (uintptr_t)ptr < 0x10000) return 0;
    return ((struct gbm_surface *)ptr)->magic == GBM_SURF_MAGIC;
}

static int is_wl_bridge_surface(void *ptr) {
    if (!ptr || (uintptr_t)ptr < 0x10000) return 0;
    return ((struct wl_egl_bridge_surface *)ptr)->magic == WL_SURF_MAGIC;
}

static int is_wl_egl_window(void *ptr) {
    if (!ptr || (uintptr_t)ptr < 0x10000) return 0;
    struct wl_egl_window *w = (struct wl_egl_window *)ptr;
    return (w->version == WL_EGL_WINDOW_VERSION && w->surface != NULL);
}

static void build_dmabuf_fbo(void *egl_dpy, int width, int height, uint32_t fourcc,
                             int fd, uint32_t stride, void **out_img, uint32_t *out_rbo,
                             uint32_t *depth_rbo, uint32_t *out_fbo) {
    int attrs[] = {
        EGL_WIDTH, width,
        EGL_HEIGHT, height,
        EGL_LINUX_DRM_FOURCC_EXT, (int)fourcc,
        EGL_DMA_BUF_PLANE0_FD_EXT, fd,
        EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
        EGL_DMA_BUF_PLANE0_PITCH_EXT, (int)stride,
        EGL_NONE
    };
    *out_img = p_eglCreateImageKHR(egl_dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, attrs);
    p_glGenRenderbuffers(1, out_rbo);
    p_glBindRenderbuffer(GL_RENDERBUFFER, *out_rbo);
    p_glEGLImageTargetRenderbufferStorageOES(GL_RENDERBUFFER, *out_img);

    if (!*depth_rbo) {
        p_glGenRenderbuffers(1, depth_rbo);
        p_glBindRenderbuffer(GL_RENDERBUFFER, *depth_rbo);
        p_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8_OES, width, height);
    }

    p_glGenFramebuffers(1, out_fbo);
    p_glBindFramebuffer(GL_FRAMEBUFFER, *out_fbo);
    p_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, *out_rbo);
    p_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, *depth_rbo);
    p_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, *depth_rbo);
}

static void ensure_gbm_fbo(struct gbm_surface *surf, int idx) {
    if (surf->fbos[idx]) return;
    struct gbm_bo *bo = surf->bos[idx];
    build_dmabuf_fbo(surf->egl_dpy, (int)bo->width, (int)bo->height,
                     gbm_to_mali_fourcc(bo->format), bo->prime_fd, bo->stride,
                     &surf->egl_images[idx], &surf->rbos[idx], &surf->depth_rbo, &surf->fbos[idx]);
}

static void free_wl_buffers(struct wl_egl_bridge_surface *surf) {
    for (int i = 0; i < NUM_SWAP_BUFFERS; i++) {
        if (surf->fbos[i] && p_glDeleteFramebuffers) p_glDeleteFramebuffers(1, &surf->fbos[i]);
        if (surf->rbos[i] && p_glDeleteRenderbuffers) p_glDeleteRenderbuffers(1, &surf->rbos[i]);
        if (surf->egl_images[i] && p_eglDestroyImageKHR && surf->egl_dpy)
            p_eglDestroyImageKHR(surf->egl_dpy, surf->egl_images[i]);
        if (surf->wl_bufs[i] && p_wl_proxy_marshal_flags)
            p_wl_proxy_marshal_flags(surf->wl_bufs[i], 0 /* destroy */, NULL,
                                     p_wl_proxy_get_version(surf->wl_bufs[i]), WL_MARSHAL_FLAG_DESTROY);
        if (surf->dmabuf_fds[i] >= 0) close(surf->dmabuf_fds[i]);
        surf->fbos[i] = 0;
        surf->rbos[i] = 0;
        surf->egl_images[i] = NULL;
        surf->wl_bufs[i] = NULL;
        surf->dmabuf_fds[i] = -1;
        surf->busy[i] = 0;
    }
    if (surf->depth_rbo && p_glDeleteRenderbuffers) {
        p_glDeleteRenderbuffers(1, &surf->depth_rbo);
        surf->depth_rbo = 0;
    }
}

static int alloc_wl_buffers(struct wl_egl_bridge_surface *surf, int w, int h) {
    if (w <= 0 || h <= 0) return 0;
    if (!ensure_wl_dmabuf(surf->map)) return 0;
    int hfd = open("/dev/dma_heap/system", O_RDONLY | O_CLOEXEC);
    if (hfd < 0) return 0;

    uint32_t stride = ((uint32_t)w * 4U + 255U) & ~255U;
    uint64_t size = (uint64_t)stride * (uint64_t)h;
    surf->width = w;
    surf->height = h;

    for (int i = 0; i < NUM_SWAP_BUFFERS; i++) {
        struct dma_heap_alloc_data data = {
            .len = size,
            .fd_flags = O_RDWR | O_CLOEXEC,
            .heap_flags = 0
        };
        if (ioctl(hfd, DMA_HEAP_IOCTL_ALLOC, &data) < 0) {
            close(hfd);
            free_wl_buffers(surf);
            return 0;
        }
        surf->dmabuf_fds[i] = (int)data.fd;
        surf->strides[i] = stride;

        void *params = p_wl_proxy_marshal_flags(surf->map->dmabuf, 1 /* create_params */,
                                                &wl_params_iface, surf->map->dmabuf_ver, 0, NULL);
        p_wl_proxy_marshal_flags(params, 1 /* add */, NULL, surf->map->dmabuf_ver, 0,
                                 surf->dmabuf_fds[i], 0U, 0U, stride, 0U, 0U);
        void *wbuf = p_wl_proxy_marshal_flags(params, 3 /* create_immed */,
                                              sym_wl_buffer_iface, surf->map->dmabuf_ver, 0,
                                              NULL, w, h, GBM_FORMAT_ARGB8888, 0U);
        p_wl_proxy_marshal_flags(params, 0 /* destroy */, NULL, surf->map->dmabuf_ver, WL_MARSHAL_FLAG_DESTROY);
        if (wbuf && surf->map->wl_queue) {
            p_wl_proxy_set_queue(wbuf, surf->map->wl_queue);
            p_wl_proxy_add_listener(wbuf, wl_buf_listener, surf);
        }
        surf->wl_bufs[i] = wbuf;
        surf->busy[i] = 0;
    }
    close(hfd);
    surf->back_idx = 0;
    return 1;
}

static void ensure_wl_fbo(struct wl_egl_bridge_surface *surf, int idx) {
    if (surf->fbos[idx]) return;
    build_dmabuf_fbo(surf->egl_dpy, surf->width, surf->height,
                     GBM_FORMAT_ARGB8888, surf->dmabuf_fds[idx], surf->strides[idx],
                     &surf->egl_images[idx], &surf->rbos[idx], &surf->depth_rbo, &surf->fbos[idx]);
}

void *eglGetDisplay(void *display_id) {
    ensure_init();
    if (is_gbm_device(display_id)) {
        return p_eglGetDisplay(EGL_DEFAULT_DISPLAY);
    }
    if (is_wl_display(display_id)) {
        void *dpy = p_eglGetDisplay(EGL_DEFAULT_DISPLAY);
        register_wl_dpy(dpy, display_id);
        return dpy;
    }
    return p_eglGetDisplay(display_id);
}

void *eglGetPlatformDisplay(unsigned int platform, void *native_display, const intptr_t *attrib_list) {
    (void)attrib_list;
    ensure_init();
    if (platform == EGL_PLATFORM_GBM_KHR || is_gbm_device(native_display)) {
        return p_eglGetDisplay(EGL_DEFAULT_DISPLAY);
    }
    if (platform == EGL_PLATFORM_WAYLAND_KHR || is_wl_display(native_display)) {
        void *dpy = p_eglGetDisplay(EGL_DEFAULT_DISPLAY);
        register_wl_dpy(dpy, native_display);
        return dpy;
    }
    return p_eglGetDisplay(native_display);
}

void *eglGetPlatformDisplayEXT(unsigned int platform, void *native_display, const int *attrib_list) {
    (void)attrib_list;
    return eglGetPlatformDisplay(platform, native_display, NULL);
}

unsigned int eglChooseConfig(void *dpy, const int *attrib_list, void **configs, int config_size, int *num_config) {
    ensure_init();
    if (!attrib_list) return p_eglChooseConfig(dpy, attrib_list, configs, config_size, num_config);
    int clean[64];
    int j = 0;
    for (int i = 0; attrib_list[i] != EGL_NONE && j < 60; i += 2) {
        int attr = attrib_list[i];
        int val = attrib_list[i + 1];
        if (attr == 0x302E /* EGL_NATIVE_VISUAL_ID */ || attr == 0x3142 /* EGL_MATCH_NATIVE_PIXMAP */) continue;
        if (attr == 0x3040 /* EGL_RENDERABLE_TYPE */ && (val & 0x0008 /* EGL_OPENGL_BIT */)) {
            val = (val & ~0x0008) | 0x0004 /* EGL_OPENGL_ES2_BIT */;
        }
        clean[j++] = attr;
        clean[j++] = val;
    }
    clean[j] = EGL_NONE;
    return p_eglChooseConfig(dpy, clean, configs, config_size, num_config);
}

unsigned int eglGetConfigAttrib(void *dpy, void *config, int attribute, int *value) {
    ensure_init();
    unsigned int ret = p_eglGetConfigAttrib ? p_eglGetConfigAttrib(dpy, config, attribute, value) : 0;
    if (ret && attribute == 0x302E /* EGL_NATIVE_VISUAL_ID */ && value) {
        *value = GBM_FORMAT_XRGB8888;
    }
    return ret;
}

void *eglCreateWindowSurface(void *dpy, void *config, void *win, const int *attrib_list) {
    ensure_init();
    if (is_gbm_surface(win)) {
        struct gbm_surface *surf = (struct gbm_surface *)win;
        surf->egl_dpy = dpy;
        return surf;
    }
    if (is_wl_egl_window(win)) {
        struct wl_dpy_map *m = find_wl_map_by_egl(dpy);
        if (!m) return EGL_NO_SURFACE;
        struct wl_egl_bridge_surface *surf = calloc(1, sizeof(struct wl_egl_bridge_surface));
        if (!surf) return EGL_NO_SURFACE;
        surf->magic = WL_SURF_MAGIC;
        surf->win = (struct wl_egl_window *)win;
        surf->map = m;
        surf->egl_dpy = dpy;
        for (int i = 0; i < NUM_SWAP_BUFFERS; i++) surf->dmabuf_fds[i] = -1;
        if (!alloc_wl_buffers(surf, surf->win->width, surf->win->height)) {
            free(surf);
            return EGL_NO_SURFACE;
        }
        return surf;
    }
    return p_eglCreateWindowSurface(dpy, config, win, attrib_list);
}

void *eglCreatePlatformWindowSurfaceEXT(void *dpy, void *config, void *native_window, const int *attrib_list) {
    return eglCreateWindowSurface(dpy, config, native_window, attrib_list);
}

void *eglCreatePlatformWindowSurface(void *dpy, void *config, void *native_window, const intptr_t *attrib_list) {
    (void)attrib_list;
    return eglCreateWindowSurface(dpy, config, native_window, NULL);
}

unsigned int eglDestroySurface(void *dpy, void *surface) {
    ensure_init();
    if (is_gbm_surface(surface)) return EGL_TRUE;
    if (is_wl_bridge_surface(surface)) {
        struct wl_egl_bridge_surface *surf = (struct wl_egl_bridge_surface *)surface;
        free_wl_buffers(surf);
        free(surf);
        return EGL_TRUE;
    }
    return p_eglDestroySurface(dpy, surface);
}

unsigned int eglQuerySurface(void *dpy, void *surface, int attribute, int *value) {
    ensure_init();
    if (is_gbm_surface(surface)) {
        struct gbm_surface *surf = (struct gbm_surface *)surface;
        if (attribute == EGL_WIDTH && value) { *value = (int)surf->width; return EGL_TRUE; }
        if (attribute == EGL_HEIGHT && value) { *value = (int)surf->height; return EGL_TRUE; }
        return EGL_TRUE;
    }
    if (is_wl_bridge_surface(surface)) {
        struct wl_egl_bridge_surface *surf = (struct wl_egl_bridge_surface *)surface;
        if (attribute == EGL_WIDTH && value) { *value = surf->win ? surf->win->width : surf->width; return EGL_TRUE; }
        if (attribute == EGL_HEIGHT && value) { *value = surf->win ? surf->win->height : surf->height; return EGL_TRUE; }
        return EGL_TRUE;
    }
    return p_eglQuerySurface(dpy, surface, attribute, value);
}

unsigned int eglMakeCurrent(void *dpy, void *draw, void *read, void *ctx) {
    ensure_init();
    if (is_gbm_surface(draw)) {
        struct gbm_surface *surf = (struct gbm_surface *)draw;
        surf->egl_dpy = dpy;
        unsigned int ok = p_eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
        if (!ok) return EGL_FALSE;
        ensure_gbm_fbo(surf, surf->back_idx);
        tls_current_surf = surf;
        tls_current_fbo = surf->fbos[surf->back_idx];
        p_glBindFramebuffer(GL_FRAMEBUFFER, tls_current_fbo);
        return EGL_TRUE;
    }
    if (is_wl_bridge_surface(draw)) {
        struct wl_egl_bridge_surface *surf = (struct wl_egl_bridge_surface *)draw;
        surf->egl_dpy = dpy;
        unsigned int ok = p_eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
        if (!ok) return EGL_FALSE;
        ensure_wl_fbo(surf, surf->back_idx);
        tls_current_surf = surf;
        tls_current_fbo = surf->fbos[surf->back_idx];
        p_glBindFramebuffer(GL_FRAMEBUFFER, tls_current_fbo);
        return EGL_TRUE;
    }
    tls_current_surf = NULL;
    tls_current_fbo = 0;
    return p_eglMakeCurrent(dpy, draw, read, ctx);
}

unsigned int eglSwapBuffers(void *dpy, void *surface) {
    ensure_init();
    if (is_gbm_surface(surface)) {
        struct gbm_surface *surf = (struct gbm_surface *)surface;
        if (p_glFinish) p_glFinish();
        surf->front_idx = surf->back_idx;
        for (int i = 1; i <= NUM_SWAP_BUFFERS; i++) {
            int cand = (surf->back_idx + i) % NUM_SWAP_BUFFERS;
            if (!surf->bos[cand]->locked) {
                surf->back_idx = cand;
                break;
            }
        }
        if (tls_current_surf == surf) {
            ensure_gbm_fbo(surf, surf->back_idx);
            tls_current_fbo = surf->fbos[surf->back_idx];
            p_glBindFramebuffer(GL_FRAMEBUFFER, tls_current_fbo);
        }
        return EGL_TRUE;
    }
    if (is_wl_bridge_surface(surface)) {
        struct wl_egl_bridge_surface *surf = (struct wl_egl_bridge_surface *)surface;
        struct wl_egl_window *win = surf->win;
        if (!win || !win->surface) return EGL_FALSE;
        if (p_glFinish) p_glFinish();

        int idx = surf->back_idx;
        uint32_t sver = p_wl_proxy_get_version(win->surface);
        if (sver >= 2) {
            p_wl_proxy_marshal_flags(win->surface, 5 /* set_buffer_transform */, NULL, sver, 0,
                                     6 /* WL_OUTPUT_TRANSFORM_FLIPPED_180 */);
        }
        p_wl_proxy_marshal_flags(win->surface, 1 /* attach */, NULL, sver, 0,
                                 surf->wl_bufs[idx], win->dx, win->dy);
        p_wl_proxy_marshal_flags(win->surface, 2 /* damage */, NULL, sver, 0,
                                 0, 0, surf->width, surf->height);
        p_wl_proxy_marshal_flags(win->surface, 6 /* commit */, NULL, sver, 0);
        p_wl_display_flush(surf->map->wl_dpy);

        surf->busy[idx] = 1;
        win->dx = 0;
        win->dy = 0;
        win->attached_width = surf->width;
        win->attached_height = surf->height;

        if (surf->map->wl_queue) {
            p_wl_display_dispatch_queue_pending(surf->map->wl_dpy, surf->map->wl_queue);
        }

        if (win->width != surf->width || win->height != surf->height) {
            free_wl_buffers(surf);
            alloc_wl_buffers(surf, win->width, win->height);
        } else {
            for (int i = 1; i <= NUM_SWAP_BUFFERS; i++) {
                int cand = (idx + i) % NUM_SWAP_BUFFERS;
                if (!surf->busy[cand]) {
                    surf->back_idx = cand;
                    break;
                }
            }
        }
        if (tls_current_surf == surf) {
            ensure_wl_fbo(surf, surf->back_idx);
            tls_current_fbo = surf->fbos[surf->back_idx];
            p_glBindFramebuffer(GL_FRAMEBUFFER, tls_current_fbo);
        }
        return EGL_TRUE;
    }
    return p_eglSwapBuffers(dpy, surface);
}

void glBindFramebuffer(unsigned int target, uint32_t framebuffer) {
    ensure_init();
    if (framebuffer == 0 && tls_current_fbo != 0 &&
        (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER)) {
        p_glBindFramebuffer(target, tls_current_fbo);
        return;
    }
    p_glBindFramebuffer(target, framebuffer);
}

void *eglCreateImageKHR(void *dpy, void *ctx, unsigned int target, void *buffer, const int *attrib_list) {
    ensure_init();
    if (target == EGL_NATIVE_PIXMAP_KHR && buffer) {
        struct gbm_bo *bo = (struct gbm_bo *)buffer;
        int attrs[] = {
            EGL_WIDTH, (int)bo->width,
            EGL_HEIGHT, (int)bo->height,
            EGL_LINUX_DRM_FOURCC_EXT, (int)gbm_to_mali_fourcc(bo->format),
            EGL_DMA_BUF_PLANE0_FD_EXT, bo->prime_fd,
            EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
            EGL_DMA_BUF_PLANE0_PITCH_EXT, (int)bo->stride,
            EGL_NONE
        };
        return p_eglCreateImageKHR(dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, attrs);
    }
    void *res = p_eglCreateImageKHR(dpy, ctx, target, buffer, attrib_list);
    if (!res && target == EGL_LINUX_DMA_BUF_EXT && attrib_list) {
        int clean[64];
        int j = 0;
        for (int i = 0; attrib_list[i] != EGL_NONE && j < 60; i += 2) {
            int k = attrib_list[i];
            int v = attrib_list[i + 1];
            if (k == 0x3443 /* MODIFIER_LO */ || k == 0x3444 /* MODIFIER_HI */ || k == 0x30D2 /* PRESERVED */) {
                continue;
            }
            clean[j++] = k;
            clean[j++] = v;
        }
        clean[j] = EGL_NONE;
        res = p_eglCreateImageKHR(dpy, ctx, target, buffer, clean);
    }
    return res;
}

const char *eglQueryString(void *dpy, int name) {
    ensure_init();
    const char *base = p_eglQueryString ? p_eglQueryString(dpy, name) : NULL;
    if (name == 0x3055 /* EGL_EXTENSIONS */) {
        static char ext_buf_nodpy[2048];
        static char ext_buf_dpy[4096];
        char *dst = (dpy == EGL_NO_DISPLAY) ? ext_buf_nodpy : ext_buf_dpy;
        size_t cap = (dpy == EGL_NO_DISPLAY) ? sizeof(ext_buf_nodpy) : sizeof(ext_buf_dpy);
        snprintf(dst, cap, "%s%sEGL_KHR_platform_gbm EGL_MESA_platform_gbm EGL_KHR_platform_wayland EGL_EXT_platform_wayland EGL_KHR_image_base EGL_EXT_image_dma_buf_import",
                 base ? base : "", (base && base[0]) ? " " : "");
        return dst;
    }
    return base;
}

unsigned int eglSwapBuffersWithDamageEXT(void *dpy, void *surface, const int *rects, int n_rects) {
    ensure_init();
    if (is_gbm_surface(surface) || is_wl_bridge_surface(surface)) {
        return eglSwapBuffers(dpy, surface);
    }
    return p_eglSwapBuffersWithDamageEXT ? p_eglSwapBuffersWithDamageEXT(dpy, surface, rects, n_rects)
                                         : eglSwapBuffers(dpy, surface);
}

unsigned int eglSwapBuffersWithDamageKHR(void *dpy, void *surface, const int *rects, int n_rects) {
    return eglSwapBuffersWithDamageEXT(dpy, surface, rects, n_rects);
}

unsigned int eglBindAPI(unsigned int api);
void *eglCreateContext(void *dpy, void *config, void *share_context, const int *attrib_list);

void *eglGetProcAddress(const char *procname) {
    ensure_init();
    if (!procname) return NULL;
    if (strcmp(procname, "eglGetDisplay") == 0) return (void *)eglGetDisplay;
    if (strcmp(procname, "eglGetPlatformDisplayEXT") == 0) return (void *)eglGetPlatformDisplayEXT;
    if (strcmp(procname, "eglGetPlatformDisplay") == 0) return (void *)eglGetPlatformDisplay;
    if (strcmp(procname, "eglChooseConfig") == 0) return (void *)eglChooseConfig;
    if (strcmp(procname, "eglGetConfigAttrib") == 0) return (void *)eglGetConfigAttrib;
    if (strcmp(procname, "eglCreateWindowSurface") == 0) return (void *)eglCreateWindowSurface;
    if (strcmp(procname, "eglCreatePlatformWindowSurfaceEXT") == 0) return (void *)eglCreatePlatformWindowSurfaceEXT;
    if (strcmp(procname, "eglCreatePlatformWindowSurface") == 0) return (void *)eglCreatePlatformWindowSurface;
    if (strcmp(procname, "eglDestroySurface") == 0) return (void *)eglDestroySurface;
    if (strcmp(procname, "eglQuerySurface") == 0) return (void *)eglQuerySurface;
    if (strcmp(procname, "eglMakeCurrent") == 0) return (void *)eglMakeCurrent;
    if (strcmp(procname, "eglSwapBuffers") == 0) return (void *)eglSwapBuffers;
    if (strcmp(procname, "eglSwapBuffersWithDamageEXT") == 0) return (void *)eglSwapBuffersWithDamageEXT;
    if (strcmp(procname, "eglSwapBuffersWithDamageKHR") == 0) return (void *)eglSwapBuffersWithDamageKHR;
    if (strcmp(procname, "eglQueryString") == 0) return (void *)eglQueryString;
    if (strcmp(procname, "eglCreateImageKHR") == 0) return (void *)eglCreateImageKHR;
    if (strcmp(procname, "eglBindAPI") == 0) return (void *)eglBindAPI;
    if (strcmp(procname, "eglCreateContext") == 0) return (void *)eglCreateContext;
    if (strcmp(procname, "glBindFramebuffer") == 0) return (void *)glBindFramebuffer;
    return p_eglGetProcAddress ? p_eglGetProcAddress(procname) : NULL;
}

#define EGL_FORWARD(ret, name, params, args) \
    ret name params { \
        ensure_init(); \
        static ret (*fn) params = NULL; \
        if (!fn && h_egl) fn = dlsym(h_egl, #name); \
        return fn ? fn args : (ret)0; \
    }

EGL_FORWARD(unsigned int, eglInitialize, (void *dpy, int *major, int *minor), (dpy, major, minor))
EGL_FORWARD(unsigned int, eglTerminate, (void *dpy), (dpy))
EGL_FORWARD(int, eglGetError, (void), ())
EGL_FORWARD(unsigned int, eglQueryAPI, (void), ())

unsigned int eglBindAPI(unsigned int api) {
    ensure_init();
    static unsigned int (*fn)(unsigned int) = NULL;
    if (!fn && h_egl) fn = dlsym(h_egl, "eglBindAPI");
    if (api == 0x30A2 /* EGL_OPENGL_API */) api = 0x30A0 /* EGL_OPENGL_ES_API */;
    return fn ? fn(api) : 0;
}

void *eglCreateContext(void *dpy, void *config, void *share_context, const int *attrib_list) {
    ensure_init();
    static void *(*fn)(void *, void *, void *, const int *) = NULL;
    if (!fn && h_egl) fn = dlsym(h_egl, "eglCreateContext");
    if (!fn) return EGL_NO_CONTEXT;
    int clean[32];
    int j = 0;
    int has_ver = 0;
    if (attrib_list) {
        for (int i = 0; attrib_list[i] != EGL_NONE && j < 26; i += 2) {
            int attr = attrib_list[i];
            int val = attrib_list[i + 1];
            if (attr == 0x30FD /* PROFILE_MASK */ || attr == 0x30FB /* MINOR_VERSION */ || attr == 0x30FC /* FLAGS */) continue;
            if (attr == 0x3098 /* MAJOR_VERSION / CLIENT_VERSION */) {
                has_ver = 1;
                if (val < 2) val = 2;
                if (val > 3) val = 3;
            }
            clean[j++] = attr;
            clean[j++] = val;
        }
    }
    if (!has_ver) {
        clean[j++] = 0x3098; /* EGL_CONTEXT_CLIENT_VERSION */
        clean[j++] = 2;
    }
    clean[j] = EGL_NONE;
    return fn(dpy, config, share_context, clean);
}
EGL_FORWARD(unsigned int, eglDestroyContext, (void *dpy, void *ctx), (dpy, ctx))
EGL_FORWARD(void *, eglGetCurrentContext, (void), ())
EGL_FORWARD(void *, eglGetCurrentDisplay, (void), ())
EGL_FORWARD(unsigned int, eglQueryContext, (void *dpy, void *ctx, int attribute, int *value), (dpy, ctx, attribute, value))
EGL_FORWARD(unsigned int, eglWaitClient, (void), ())

unsigned int eglSwapInterval(void *dpy, int interval) {
    ensure_init();
    if (tls_current_surf) return EGL_TRUE;
    static unsigned int (*fn)(void *, int) = NULL;
    if (!fn && h_egl) fn = dlsym(h_egl, "eglSwapInterval");
    return fn ? fn(dpy, interval) : EGL_TRUE;
}
EGL_FORWARD(unsigned int, eglWaitGL, (void), ())
EGL_FORWARD(unsigned int, eglWaitNative, (int engine), (engine))
EGL_FORWARD(unsigned int, eglGetConfigs, (void *dpy, void **configs, int config_size, int *num_config), (dpy, configs, config_size, num_config))
EGL_FORWARD(void *, eglCreatePbufferSurface, (void *dpy, void *config, const int *attrib_list), (dpy, config, attrib_list))
EGL_FORWARD(void *, eglCreatePixmapSurface, (void *dpy, void *config, void *pixmap, const int *attrib_list), (dpy, config, pixmap, attrib_list))
EGL_FORWARD(unsigned int, eglCopyBuffers, (void *dpy, void *surface, void *target), (dpy, surface, target))
EGL_FORWARD(unsigned int, eglReleaseThread, (void), ())

void *eglGetCurrentSurface(int readdraw) {
    ensure_init();
    if (tls_current_surf) return tls_current_surf;
    static void *(*fn)(int) = NULL;
    if (!fn && h_egl) fn = dlsym(h_egl, "eglGetCurrentSurface");
    return fn ? fn(readdraw) : EGL_NO_SURFACE;
}
