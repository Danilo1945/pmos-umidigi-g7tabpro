#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

/* ============================================================================
 * Shim mínimo de librerías Android para cargar libGLES_mali.so (Valhall r32p1)
 * bajo linker64 en postmarketOS.
 * ============================================================================ */

/* --- 1. zlib stubs (usados solo si hay cache de shaders comprimidos) --- */
int deflateInit_(void *strm, int level, const char *version, int stream_size) { (void)strm; (void)level; (void)version; (void)stream_size; return -1; }
int deflate(void *strm, int flush) { (void)strm; (void)flush; return -1; }
int deflateEnd(void *strm) { (void)strm; return 0; }
unsigned long deflateBound(void *strm, unsigned long sourceLen) { (void)strm; return sourceLen + 64; }
int inflateInit_(void *strm, const char *version, int stream_size) { (void)strm; (void)version; (void)stream_size; return -1; }
int inflate(void *strm, int flush) { (void)strm; (void)flush; return -1; }
int inflateEnd(void *strm) { (void)strm; return 0; }

/* --- 2. libgpud.so (MediaTek GPU Debug/Dump - deshabilitado en producción) --- */
void *g_gpud_context = NULL;
void gpudInitialize(void) {}
int gpudIsEnabled(void) { return 0; }
int gpudIsGlBindFramebufferDumpEnabled(void) { return 0; }
int gpudIsGlForceRGB8888(void) { return 0; }
void gpudAuxShowConversionStatus(void) {}
void gpudEglHackChooseConfig(void) {}
void gpudEglHackCreateWindowSurface(void) {}
void gpudGlDumpBindFramebuffer(void) {}
void gpudGlDumpCompressedTexImage2D(void) {}
void gpudGlDumpCompressedTexImage3D(void) {}
void gpudGlDumpCompressedTexSubImage2D(void) {}
void gpudGlDumpCompressedTexSubImage3D(void) {}
void gpudGlDumpCopyTexImage2D(void) {}
void gpudGlDumpCopyTexSubImage2D(void) {}
void gpudGlDumpCopyTexSubImage3D(void) {}
void gpudGlDumpEGLImageTargetTexture2DOES(void) {}
void gpudGlDumpPresentFrame(void) {}
void gpudGlDumpReadPixels(void) {}
void gpudGlDumpStateError(void) {}
void gpudGlDumpTexImage2D(void) {}
void gpudGlDumpTexImage3D(void) {}
void gpudGlDumpTexSubImage2D(void) {}
void gpudGlDumpTexSubImage3D(void) {}
void gpudGlHackClear(void) {}
void gpudGlHackClearColor(void) {}
void gpudGlHackClearDepthf(void) {}
void gpudGlHackClearStencil(void) {}
void gpudGlHackGetString(void) {}
void gpudGlHackOrDumpShaderSource(void) {}
void gpudGlHackPolygonOffset(void) {}
void gpudGlHackSamplerParameterf(void) {}
void gpudGlHackSamplerParameteri(void) {}
void gpudGlHackScissor(void) {}
void gpudGlHackTexParameterf(void) {}
void gpudGlHackTexParameteri(void) {}
void gpudGlHackUseProgram(void) {}
void gpudGlHackViewport(void) {}
void gpudGlShowCurrentContext(void) {}
void gpudVkDumpAllocateMemory(void) {}
void gpudVkDumpMapMemory(void) {}
void gpudVkHackCreateDevice(void) {}
void gpudVkHackCreateInstance(void) {}
void gpudVkHackGetPhysicalDeviceProperties(void) {}
void gpudVkHackGetPhysicalDeviceProperties2(void) {}
void gpudVkHackGetPhysicalDeviceQueueFamilyProperties(void) {}
void gpudVkHackGetPhysicalDeviceQueueFamilyProperties2(void) {}

/* --- 3. libgpu_aux.so --- */
int GpuAuxCheckAuxPath(void) { return 0; }
void *GpuAuxCreateContext(void) { return NULL; }
void GpuAuxDestoryContext(void *ctx) { (void)ctx; }
int GpuAuxDoConversionIfNeedV2(void) { return 0; }
int GpuAuxGetAuxOutFormat(void) { return 0; }
void *GpuAuxGetCurrentAuxBuffer(void) { return NULL; }
int GpuAuxGetCurrentIdx(void) { return 0; }
int GpuAuxIsSupportFormat(void) { return 0; }
int GpuAuxPrepareBuffer(void) { return 0; }

/* --- 4. libged.so (MediaTek GPU Event Driver) --- */
void *ged_create(void) { return (void *)0x1; }
void ged_destroy(void *h) { (void)h; }
int ged_gpu_hint_cpu(void *h, int a, int b) { (void)h; (void)a; (void)b; return 0; }
int ged_hint_force_mdp(void *h, int a, int b) { (void)h; (void)a; (void)b; return 0; }
void *ged_swd_create(void) { return (void *)0x1; }
void ged_swd_destroy(void *h) { (void)h; }
int ged_swd_push_fence(void *h, int fd) { (void)h; (void)fd; return 0; }

/* --- 5. libdmabufheap.so (Implementación real sobre /dev/dma_heap/system) --- */
struct dma_heap_allocation_data {
    uint64_t len;
    uint32_t fd;
    uint32_t fd_flags;
    uint64_t heap_flags;
};
#define DMA_HEAP_IOC_MAGIC 'H'
#define DMA_HEAP_IOCTL_ALLOC _IOWR(DMA_HEAP_IOC_MAGIC, 0x0, struct dma_heap_allocation_data)

void *CreateDmabufHeapBufferAllocator(void) {
    int fd = open("/dev/dma_heap/system", O_RDONLY | O_CLOEXEC);
    if (fd < 0) fd = open("/dev/dma_heap/mtk_mm", O_RDONLY | O_CLOEXEC);
    return (void *)(intptr_t)(fd >= 0 ? fd + 1 : 0);
}
void FreeDmabufHeapBufferAllocator(void *alloc) {
    int fd = (int)(intptr_t)alloc - 1;
    if (fd >= 0) close(fd);
}
int DmabufHeapAlloc(void *alloc, const char *heap_name, size_t len, unsigned int heap_flags, size_t legacy_align) {
    (void)heap_name; (void)legacy_align;
    int hfd = (int)(intptr_t)alloc - 1;
    int close_after = 0;
    if (hfd < 0) {
        hfd = open("/dev/dma_heap/system", O_RDONLY | O_CLOEXEC);
        if (hfd < 0) return -1;
        close_after = 1;
    }
    struct dma_heap_allocation_data data = {
        .len = len,
        .fd = 0,
        .fd_flags = O_RDWR | O_CLOEXEC,
        .heap_flags = heap_flags
    };
    int ret = ioctl(hfd, DMA_HEAP_IOCTL_ALLOC, &data);
    if (close_after) close(hfd);
    return (ret == 0) ? (int)data.fd : -1;
}
int MapDmabufHeapNameToIonHeap(void *alloc, const char *heap_name, const char *ion_heap_name,
                               unsigned int ion_heap_mask, unsigned int ion_flags, unsigned int legacy_align) {
    (void)alloc; (void)heap_name; (void)ion_heap_name; (void)ion_heap_mask; (void)ion_flags; (void)legacy_align;
    return 0;
}

/* --- 6. libgralloc_extra.so --- */
int gralloc_extra_query(const void *handle, int attr, void *out) { (void)handle; (void)attr; (void)out; return -1; }
int gralloc_extra_perform(const void *handle, int op, void *in, void *out) { (void)handle; (void)op; (void)in; (void)out; return -1; }
int gralloc_extra_sf_set_status(void *sf_info, uint64_t mask, uint64_t val) { (void)sf_info; (void)mask; (void)val; return 0; }

/* --- 7. libnativewindow.so (ANativeWindow & AHardwareBuffer stubs para EGL surfaceless/FBO) --- */
void ANativeWindow_acquire(void *w) { (void)w; }
void ANativeWindow_release(void *w) { (void)w; }
int32_t ANativeWindow_getWidth(void *w) { (void)w; return 1200; }
int32_t ANativeWindow_getHeight(void *w) { (void)w; return 1920; }
int32_t ANativeWindow_getFormat(void *w) { (void)w; return 1; }
int ANativeWindow_setBuffersDimensions(void *w, int32_t wi, int32_t h) { (void)w; (void)wi; (void)h; return 0; }
int ANativeWindow_setBuffersFormat(void *w, int32_t f) { (void)w; (void)f; return 0; }
int ANativeWindow_setBuffersTransform(void *w, int32_t t) { (void)w; (void)t; return 0; }
int ANativeWindow_setSharedBufferMode(void *w, int m) { (void)w; (void)m; return 0; }
int ANativeWindow_setSwapInterval(void *w, int i) { (void)w; (void)i; return 0; }
int ANativeWindow_setUsage(void *w, uint64_t u) { (void)w; (void)u; return 0; }
int ANativeWindow_query(void *w, int what, int *val) { (void)w; (void)what; if (val) *val = 0; return -1; }
int ANativeWindow_queryf(void *w, int what, float *val) { (void)w; (void)what; if (val) *val = 0.0f; return -1; }
int ANativeWindow_dequeueBuffer(void *w, void **buf, int *fence) { (void)w; (void)buf; (void)fence; return -1; }
int ANativeWindow_queueBuffer(void *w, void *buf, int fence) { (void)w; (void)buf; (void)fence; return -1; }
int ANativeWindow_cancelBuffer(void *w, void *buf, int fence) { (void)w; (void)buf; (void)fence; return 0; }

void AHardwareBuffer_acquire(void *b) { (void)b; }
void AHardwareBuffer_release(void *b) { (void)b; }
int AHardwareBuffer_allocate(const void *desc, void **out) { (void)desc; (void)out; return -1; }
void AHardwareBuffer_describe(const void *b, void *out) { (void)b; (void)out; }
const void *AHardwareBuffer_getNativeHandle(const void *b) { (void)b; return NULL; }
int AHardwareBuffer_getId(const void *b, uint64_t *id) { (void)b; if (id) *id = 0; return -1; }

/* --- 8. C++ HIDL / gralloc4 / RefBase stubs --- */
void _ZNK7android7RefBase9decStrongEPKv(void *this_ptr, const void *id) { (void)this_ptr; (void)id; }
void _ZN7android8hardware11hidl_stringC1EPKc(void *this_ptr, const char *s) { (void)this_ptr; (void)s; }
void _ZN7android8hardware11hidl_stringC1ERKS1_(void *this_ptr, const void *o) { (void)this_ptr; (void)o; }
void _ZN7android8hardware11hidl_stringD1Ev(void *this_ptr) { (void)this_ptr; }
void _ZN7android8hardware7details13return_statusD2Ev(void *this_ptr) { (void)this_ptr; }
void *_ZN7android8hardware8graphics6mapper4V4_07IMapper10getServiceERKNSt3__112basic_stringIcNS5_11char_traitsIcEENS5_9allocatorIcEEEEb(void *out, const void *name, int retry) {
    (void)name; (void)retry;
    if (out) { ((void**)out)[0] = NULL; ((void**)out)[1] = NULL; }
    return out;
}
int _ZN7android8gralloc410decodeCropERKNS_8hardware8hidl_vecIhEEPNSt3__16vectorIN4aidl7android8hardware8graphics6common4RectENS6_9allocatorISD_EEEE(const void *a, void *b) { (void)a; (void)b; return -1; }
int _ZN7android8gralloc411decodeUsageERKNS_8hardware8hidl_vecIhEEPm(const void *a, uint64_t *b) { (void)a; (void)b; return -1; }
int _ZN7android8gralloc411decodeWidthERKNS_8hardware8hidl_vecIhEEPm(const void *a, uint64_t *b) { (void)a; (void)b; return -1; }
int _ZN7android8gralloc412decodeHeightERKNS_8hardware8hidl_vecIhEEPm(const void *a, uint64_t *b) { (void)a; (void)b; return -1; }
int _ZN7android8gralloc415decodeDataspaceERKNS_8hardware8hidl_vecIhEEPN4aidl7android8hardware8graphics6common9DataspaceE(const void *a, void *b) { (void)a; (void)b; return -1; }
int _ZN7android8gralloc415encodeDataspaceERKN4aidl7android8hardware8graphics6common9DataspaceEPNS_8hardware8hidl_vecIhEE(const void *a, void *b) { (void)a; (void)b; return -1; }
int _ZN7android8gralloc416decodeLayerCountERKNS_8hardware8hidl_vecIhEEPm(const void *a, uint64_t *b) { (void)a; (void)b; return -1; }
int _ZN7android8gralloc418decodePlaneLayoutsERKNS_8hardware8hidl_vecIhEEPNSt3__16vectorIN4aidl7android8hardware8graphics6common11PlaneLayoutENS6_9allocatorISD_EEEE(const void *a, void *b) { (void)a; (void)b; return -1; }
int _ZN7android8gralloc423decodePixelFormatFourCCERKNS_8hardware8hidl_vecIhEEPj(const void *a, uint32_t *b) { (void)a; (void)b; return -1; }
int _ZN7android8gralloc425decodePixelFormatModifierERKNS_8hardware8hidl_vecIhEEPm(const void *a, uint64_t *b) { (void)a; (void)b; return -1; }

/* --- 9. libui_compat_layer.so (libhybris GRALLOC_COMPAT sobre /dev/dma_heap/system) --- */
#include <stdlib.h>

typedef struct {
    int version; /* sizeof(native_handle_t) = 12 */
    int numFds;
    int numInts;
    int data[5]; /* data[0] = dmabuf_fd, data[1]=w, data[2]=h, data[3]=stride, data[4]=fmt */
} shim_native_handle_t;

int graphic_buffer_allocator_allocate(uint32_t w, uint32_t h, int32_t format, uint32_t layerCount,
                                      uint64_t usage, const void **handle, uint32_t *stride,
                                      uint64_t graphicBufferId, const char *requestorName) {
    (void)layerCount; (void)usage; (void)graphicBufferId; (void)requestorName;
    uint32_t st = (w + 15) & ~15u;
    size_t size = (size_t)st * (size_t)h * 4;
    if (size == 0) size = 4096;
    int fd = DmabufHeapAlloc(NULL, "system", size, 0, 0);
    if (fd < 0) return -12; /* ENOMEM */
    shim_native_handle_t *nh = (shim_native_handle_t *)calloc(1, sizeof(shim_native_handle_t));
    if (!nh) { close(fd); return -12; }
    nh->version = 12;
    nh->numFds = 1;
    nh->numInts = 4;
    nh->data[0] = fd;
    nh->data[1] = (int)w;
    nh->data[2] = (int)h;
    nh->data[3] = (int)st;
    nh->data[4] = format;
    if (handle) *handle = nh;
    if (stride) *stride = st;
    return 0;
}

int graphic_buffer_allocator_free(const void *handle) {
    shim_native_handle_t *nh = (shim_native_handle_t *)handle;
    if (nh) {
        if (nh->numFds > 0 && nh->data[0] >= 0) close(nh->data[0]);
        free(nh);
    }
    return 0;
}

int graphic_buffer_mapper_import_buffer(const void *raw_handle, uint32_t w, uint32_t h, uint32_t layers,
                                        int32_t fmt, uint64_t usage, uint32_t stride, const void **out_handle) {
    (void)w; (void)h; (void)layers; (void)fmt; (void)usage; (void)stride;
    if (out_handle) *out_handle = raw_handle;
    return 0;
}

int graphic_buffer_mapper_import_buffer_no_size(const void *raw_handle, const void **out_handle) {
    if (out_handle) *out_handle = raw_handle;
    return 0;
}

int graphic_buffer_mapper_free_buffer(const void *handle) {
    (void)handle;
    return 0;
}

int graphic_buffer_mapper_lock(const void *handle, uint32_t usage, const void *bounds,
                               void **vaddr, int32_t *outBytesPerPixel, int32_t *outBytesPerStride) {
    (void)usage; (void)bounds;
    const shim_native_handle_t *nh = (const shim_native_handle_t *)handle;
    if (!nh || nh->numFds < 1 || nh->data[0] < 0) return -22;
    size_t sz = (size_t)nh->data[3] * (size_t)nh->data[2] * 4;
    if (sz == 0) sz = 4096;
    void *p = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, nh->data[0], 0);
    if (p == MAP_FAILED) return -12;
    if (vaddr) *vaddr = p;
    if (outBytesPerPixel) *outBytesPerPixel = 4;
    if (outBytesPerStride) *outBytesPerStride = nh->data[3] * 4;
    return 0;
}

int graphic_buffer_mapper_unlock(const void *handle) {
    (void)handle;
    return 0;
}
