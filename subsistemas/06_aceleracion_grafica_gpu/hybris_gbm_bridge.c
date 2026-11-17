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
static __thread struct gbm_surface *tls_current_surf = NULL;
static __thread uint32_t tls_current_fbo = 0;

static void init_hybris_syms(void) {
    h_egl = dlopen("/usr/lib/libhybris_mali/libEGL.so.1", RTLD_NOW | RTLD_GLOBAL);
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
 * Puente EGL / GLES2 para GBM <-> libhybris (libGLES_mali.so)
 * ============================================================================ */

static int is_gbm_device(void *ptr) {
    if (!ptr || (uintptr_t)ptr < 0x10000) return 0;
    return ((struct gbm_device *)ptr)->magic == GBM_MAGIC;
}

static int is_gbm_surface(void *ptr) {
    if (!ptr || (uintptr_t)ptr < 0x10000) return 0;
    return ((struct gbm_surface *)ptr)->magic == GBM_SURF_MAGIC;
}

static void ensure_gbm_fbo(struct gbm_surface *surf, int idx) {
    if (surf->fbos[idx]) return;
    struct gbm_bo *bo = surf->bos[idx];
    int attrs[] = {
        EGL_WIDTH, (int)bo->width,
        EGL_HEIGHT, (int)bo->height,
        EGL_LINUX_DRM_FOURCC_EXT, (int)gbm_to_mali_fourcc(bo->format),
        EGL_DMA_BUF_PLANE0_FD_EXT, bo->prime_fd,
        EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
        EGL_DMA_BUF_PLANE0_PITCH_EXT, (int)bo->stride,
        EGL_NONE
    };
    surf->egl_images[idx] = p_eglCreateImageKHR(surf->egl_dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, attrs);
    p_glGenRenderbuffers(1, &surf->rbos[idx]);
    p_glBindRenderbuffer(GL_RENDERBUFFER, surf->rbos[idx]);
    p_glEGLImageTargetRenderbufferStorageOES(GL_RENDERBUFFER, surf->egl_images[idx]);

    if (!surf->depth_rbo) {
        p_glGenRenderbuffers(1, &surf->depth_rbo);
        p_glBindRenderbuffer(GL_RENDERBUFFER, surf->depth_rbo);
        p_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8_OES, (int)bo->width, (int)bo->height);
    }

    p_glGenFramebuffers(1, &surf->fbos[idx]);
    p_glBindFramebuffer(GL_FRAMEBUFFER, surf->fbos[idx]);
    p_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, surf->rbos[idx]);
    p_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, surf->depth_rbo);
    p_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, surf->depth_rbo);
}

void *eglGetDisplay(void *display_id) {
    ensure_init();
    if (is_gbm_device(display_id)) {
        return p_eglGetDisplay(EGL_DEFAULT_DISPLAY);
    }
    return p_eglGetDisplay(display_id);
}

void *eglGetPlatformDisplay(unsigned int platform, void *native_display, const intptr_t *attrib_list) {
    (void)attrib_list;
    ensure_init();
    if (platform == EGL_PLATFORM_GBM_KHR || is_gbm_device(native_display)) {
        return p_eglGetDisplay(EGL_DEFAULT_DISPLAY);
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
        fprintf(stderr, "[bridge] eglCreateImageKHR bo=%p w=%u h=%u stride=%u fmt=0x%x fd=%d\n",
                bo, bo->width, bo->height, bo->stride, bo->format, bo->prime_fd);
        int attrs[] = {
            EGL_WIDTH, (int)bo->width,
            EGL_HEIGHT, (int)bo->height,
            EGL_LINUX_DRM_FOURCC_EXT, (int)gbm_to_mali_fourcc(bo->format),
            EGL_DMA_BUF_PLANE0_FD_EXT, bo->prime_fd,
            EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
            EGL_DMA_BUF_PLANE0_PITCH_EXT, (int)bo->stride,
            EGL_NONE
        };
        void *res = p_eglCreateImageKHR(dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, attrs);
        fprintf(stderr, "[bridge] eglCreateImageKHR -> %p\n", res);
        return res;
    }
    return p_eglCreateImageKHR(dpy, ctx, target, buffer, attrib_list);
}

const char *eglQueryString(void *dpy, int name) {
    ensure_init();
    const char *base = p_eglQueryString ? p_eglQueryString(dpy, name) : NULL;
    if (name == 0x3055 /* EGL_EXTENSIONS */) {
        static char ext_buf_nodpy[2048];
        static char ext_buf_dpy[4096];
        char *dst = (dpy == EGL_NO_DISPLAY) ? ext_buf_nodpy : ext_buf_dpy;
        size_t cap = (dpy == EGL_NO_DISPLAY) ? sizeof(ext_buf_nodpy) : sizeof(ext_buf_dpy);
        snprintf(dst, cap, "%s%sEGL_KHR_platform_gbm EGL_MESA_platform_gbm EGL_KHR_image_base EGL_EXT_image_dma_buf_import",
                 base ? base : "", (base && base[0]) ? " " : "");
        return dst;
    }
    return base;
}

unsigned int eglSwapBuffersWithDamageEXT(void *dpy, void *surface, const int *rects, int n_rects) {
    ensure_init();
    if (is_gbm_surface(surface)) {
        return eglSwapBuffers(dpy, surface);
    }
    return p_eglSwapBuffersWithDamageEXT ? p_eglSwapBuffersWithDamageEXT(dpy, surface, rects, n_rects)
                                         : eglSwapBuffers(dpy, surface);
}

unsigned int eglSwapBuffersWithDamageKHR(void *dpy, void *surface, const int *rects, int n_rects) {
    return eglSwapBuffersWithDamageEXT(dpy, surface, rects, n_rects);
}

void *eglGetProcAddress(const char *procname) {
    ensure_init();
    if (!procname) return NULL;
    if (strcmp(procname, "eglGetPlatformDisplayEXT") == 0) return (void *)eglGetPlatformDisplayEXT;
    if (strcmp(procname, "eglGetPlatformDisplay") == 0) return (void *)eglGetPlatformDisplay;
    if (strcmp(procname, "eglCreatePlatformWindowSurfaceEXT") == 0) return (void *)eglCreatePlatformWindowSurfaceEXT;
    if (strcmp(procname, "eglCreatePlatformWindowSurface") == 0) return (void *)eglCreatePlatformWindowSurface;
    if (strcmp(procname, "eglSwapBuffersWithDamageEXT") == 0) return (void *)eglSwapBuffersWithDamageEXT;
    if (strcmp(procname, "eglSwapBuffersWithDamageKHR") == 0) return (void *)eglSwapBuffersWithDamageKHR;
    if (strcmp(procname, "eglQueryString") == 0) return (void *)eglQueryString;
    if (strcmp(procname, "eglCreateImageKHR") == 0) return (void *)eglCreateImageKHR;
    if (strcmp(procname, "glBindFramebuffer") == 0) return (void *)glBindFramebuffer;
    return p_eglGetProcAddress ? p_eglGetProcAddress(procname) : NULL;
}
