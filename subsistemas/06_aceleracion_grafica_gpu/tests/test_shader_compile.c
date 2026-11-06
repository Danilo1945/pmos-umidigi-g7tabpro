#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>
#include <stdint.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

const char *vert_src = 
    "attribute vec4 position;\n"
    "void main() {\n"
    "    gl_Position = position;\n"
    "}\n";

const char *frag_src = 
    "precision mediump float;\n"
    "void main() {\n"
    "    gl_FragColor = vec4(0.0, 1.0, 0.5, 1.0);\n"
    "}\n";

int main() {
    setbuf(stdout, NULL);
    printf("=== TEST COMPILACION Y ENLACE DE SHADERS EN SILICIO MALI-G57 ===\n");

    void *h_gbm = dlopen("/usr/lib/libgbm.so.1", RTLD_LAZY | RTLD_GLOBAL);
    void *h_egl = dlopen("/usr/lib/libEGL.so.1", RTLD_LAZY | RTLD_GLOBAL);
    void *h_gles = dlopen("/usr/lib/libGLESv2.so.2", RTLD_LAZY | RTLD_GLOBAL);
    if (!h_gbm || !h_egl || !h_gles) {
        printf("[!] Error dlopen: gbm=%p, egl=%p, gles=%p\n", h_gbm, h_egl, h_gles);
        return 1;
    }

    struct gbm_device *(*fn_gbm_create_device)(int) = dlsym(h_gbm, "gbm_create_device");
    void *(*fn_eglGetProcAddress)(const char *) = dlsym(h_egl, "eglGetProcAddress");
    EGLBoolean (*fn_eglInitialize)(EGLDisplay, EGLint *, EGLint *) = dlsym(h_egl, "eglInitialize");
    const char *(*fn_eglQueryString)(EGLDisplay, EGLint) = dlsym(h_egl, "eglQueryString");
    EGLBoolean (*fn_eglChooseConfig)(EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *) = dlsym(h_egl, "eglChooseConfig");
    EGLContext (*fn_eglCreateContext)(EGLDisplay, EGLConfig, EGLContext, const EGLint *) = dlsym(h_egl, "eglCreateContext");
    EGLSurface (*fn_eglCreatePbufferSurface)(EGLDisplay, EGLConfig, const EGLint *) = dlsym(h_egl, "eglCreatePbufferSurface");
    EGLBoolean (*fn_eglMakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext) = dlsym(h_egl, "eglMakeCurrent");

    const GLubyte *(*fn_glGetString)(GLenum) = dlsym(h_gles, "glGetString");
    GLuint (*fn_glCreateShader)(GLenum) = dlsym(h_gles, "glCreateShader");
    void (*fn_glShaderSource)(GLuint, GLsizei, const GLchar *const*, const GLint *) = dlsym(h_gles, "glShaderSource");
    void (*fn_glCompileShader)(GLuint) = dlsym(h_gles, "glCompileShader");
    void (*fn_glGetShaderiv)(GLuint, GLenum, GLint *) = dlsym(h_gles, "glGetShaderiv");
    GLuint (*fn_glCreateProgram)(void) = dlsym(h_gles, "glCreateProgram");
    void (*fn_glAttachShader)(GLuint, GLuint) = dlsym(h_gles, "glAttachShader");
    void (*fn_glLinkProgram)(GLuint) = dlsym(h_gles, "glLinkProgram");
    void (*fn_glGetProgramiv)(GLuint, GLenum, GLint *) = dlsym(h_gles, "glGetProgramiv");
    void (*fn_glGetProgramInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *) = dlsym(h_gles, "glGetProgramInfoLog");
    void (*fn_glUseProgram)(GLuint) = dlsym(h_gles, "glUseProgram");
    void (*fn_glClearColor)(GLclampf, GLclampf, GLclampf, GLclampf) = dlsym(h_gles, "glClearColor");
    void (*fn_glClear)(GLbitfield) = dlsym(h_gles, "glClear");
    void (*fn_glFlush)(void) = dlsym(h_gles, "glFlush");

    int fd = open("/dev/dri/card0", O_RDWR);
    if (fd < 0) { perror("open card0"); return 1; }

    struct gbm_device *gbm = fn_gbm_create_device(fd);
    PFNEGLGETPLATFORMDISPLAYEXTPROC my_get_plat = (PFNEGLGETPLATFORMDISPLAYEXTPROC)fn_eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay dpy = my_get_plat ? my_get_plat(0x31D7, gbm, NULL) : NULL;

    EGLint maj = 0, min = 0;
    fn_eglInitialize(dpy, &maj, &min);
    printf("[1] EGL Inicializado: %d.%d, Vendor: %s\n", maj, min, fn_eglQueryString(dpy, EGL_VENDOR));

    EGLint cfg_attribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_NONE
    };
    EGLConfig cfg;
    EGLint num;
    fn_eglChooseConfig(dpy, cfg_attribs, &cfg, 1, &num);

    EGLint ctx_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = fn_eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attribs);

    EGLint pbuf_attribs[] = { EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE };
    EGLSurface pbuf = fn_eglCreatePbufferSurface(dpy, cfg, pbuf_attribs);

    fn_eglMakeCurrent(dpy, pbuf, pbuf, ctx);
    printf("[2] Contexto GL Creado: Vendor=%s, Renderer=%s, Version=%s\n",
           fn_glGetString(GL_VENDOR), fn_glGetString(GL_RENDERER), fn_glGetString(GL_VERSION));

    printf("[3] Compilando Vertex Shader...\n");
    GLuint vs = fn_glCreateShader(GL_VERTEX_SHADER);
    fn_glShaderSource(vs, 1, &vert_src, NULL);
    fn_glCompileShader(vs);
    GLint status = 0;
    fn_glGetShaderiv(vs, GL_COMPILE_STATUS, &status);
    printf("    Vertex Shader Compile Status: %d\n", status);

    printf("[4] Compilando Fragment Shader...\n");
    GLuint fs = fn_glCreateShader(GL_FRAGMENT_SHADER);
    fn_glShaderSource(fs, 1, &frag_src, NULL);
    fn_glCompileShader(fs);
    status = 0;
    fn_glGetShaderiv(fs, GL_COMPILE_STATUS, &status);
    printf("    Fragment Shader Compile Status: %d\n", status);

    printf("[5] Enlazando Programa GL (dispara compilador JIT mali-compiler cmpbe_v2)...\n");
    GLuint prog = fn_glCreateProgram();
    fn_glAttachShader(prog, vs);
    fn_glAttachShader(prog, fs);
    fn_glLinkProgram(prog);
    status = 0;
    fn_glGetProgramiv(prog, GL_LINK_STATUS, &status);
    printf("    Program Link Status: %d\n", status);

    if (status) {
        printf("[✓] ¡EXITO ROTUNDO! mali-compiler compilo y enlazo los shaders sin SIGSEGV.\n");
        fn_glUseProgram(prog);
        fn_glClearColor(0.2f, 0.7f, 0.3f, 1.0f);
        fn_glClear(GL_COLOR_BUFFER_BIT);
        fn_glFlush();
        printf("[✓] Pipeline de renderizado ejecutado en silicio ARM Mali.\n");
    } else {
        char log[512];
        fn_glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        printf("[!] Fallo de enlace: %s\n", log);
    }

    _exit(0);
}
