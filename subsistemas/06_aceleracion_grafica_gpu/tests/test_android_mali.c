#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

/* Test mínimo Bionic/linker64 para verificar renderizado 100% GPU con libGLES_mali.so real */
typedef void* EGLDisplay;
typedef void* EGLConfig;
typedef void* EGLContext;
typedef void* EGLSurface;
typedef void* EGLImageKHR;
typedef int EGLint;
typedef unsigned int EGLBoolean;
typedef unsigned int EGLenum;
typedef uint32_t GLuint;
typedef int32_t GLint;
typedef int32_t GLsizei;
typedef unsigned int GLenum;
typedef float GLfloat;

#define EGL_NO_DISPLAY ((EGLDisplay)0)
#define EGL_NO_CONTEXT ((EGLContext)0)
#define EGL_NO_SURFACE ((EGLSurface)0)
#define EGL_DEFAULT_DISPLAY ((void*)0)
#define EGL_NONE 0x3038
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_OPENGL_ES2_BIT 0x0004
#define EGL_SURFACE_TYPE 0x3033
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_OPENGL_ES_API 0x30A0

#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_TRIANGLES 0x0004
#define GL_FLOAT 0x1406
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_FRAMEBUFFER 0x8D40
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_RGBA8 0x8058

int main(void) {
    printf("=== TEST ANDROID NATIVO libGLES_mali.so (Valhall r32p1) ===\n");
    fflush(stdout);

    void *mali = dlopen("libGLES_mali.so", RTLD_NOW | RTLD_GLOBAL);
    if (!mali) {
        printf("[!] dlopen(libGLES_mali.so) fallo: %s\n", dlerror());
        return 1;
    }
    printf("[1] libGLES_mali.so cargada con exito!\n");

    EGLDisplay (*p_eglGetDisplay)(void*) = dlsym(mali, "eglGetDisplay");
    EGLBoolean (*p_eglInitialize)(EGLDisplay, EGLint*, EGLint*) = dlsym(mali, "eglInitialize");
    EGLBoolean (*p_eglBindAPI)(EGLenum) = dlsym(mali, "eglBindAPI");
    EGLBoolean (*p_eglChooseConfig)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*) = dlsym(mali, "eglChooseConfig");
    EGLContext (*p_eglCreateContext)(EGLDisplay, EGLConfig, EGLContext, const EGLint*) = dlsym(mali, "eglCreateContext");
    EGLBoolean (*p_eglMakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext) = dlsym(mali, "eglMakeCurrent");
    const char* (*p_eglQueryString)(EGLDisplay, EGLint) = dlsym(mali, "eglQueryString");
    EGLint (*p_eglGetError)(void) = dlsym(mali, "eglGetError");

    const unsigned char* (*p_glGetString)(GLenum) = dlsym(mali, "glGetString");
    GLuint (*p_glCreateShader)(GLenum) = dlsym(mali, "glCreateShader");
    void (*p_glShaderSource)(GLuint, GLsizei, const char**, const GLint*) = dlsym(mali, "glShaderSource");
    void (*p_glCompileShader)(GLuint) = dlsym(mali, "glCompileShader");
    void (*p_glGetShaderiv)(GLuint, GLenum, GLint*) = dlsym(mali, "glGetShaderiv");
    GLuint (*p_glCreateProgram)(void) = dlsym(mali, "glCreateProgram");
    void (*p_glAttachShader)(GLuint, GLuint) = dlsym(mali, "glAttachShader");
    void (*p_glBindAttribLocation)(GLuint, GLuint, const char*) = dlsym(mali, "glBindAttribLocation");
    void (*p_glLinkProgram)(GLuint) = dlsym(mali, "glLinkProgram");
    void (*p_glUseProgram)(GLuint) = dlsym(mali, "glUseProgram");
    void (*p_glGenFramebuffers)(GLsizei, GLuint*) = dlsym(mali, "glGenFramebuffers");
    void (*p_glBindFramebuffer)(GLenum, GLuint) = dlsym(mali, "glBindFramebuffer");
    void (*p_glGenRenderbuffers)(GLsizei, GLuint*) = dlsym(mali, "glGenRenderbuffers");
    void (*p_glBindRenderbuffer)(GLenum, GLuint) = dlsym(mali, "glBindRenderbuffer");
    void (*p_glRenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei) = dlsym(mali, "glRenderbufferStorage");
    void (*p_glFramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint) = dlsym(mali, "glFramebufferRenderbuffer");
    GLenum (*p_glCheckFramebufferStatus)(GLenum) = dlsym(mali, "glCheckFramebufferStatus");
    void (*p_glViewport)(GLint, GLint, GLsizei, GLsizei) = dlsym(mali, "glViewport");
    void (*p_glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat) = dlsym(mali, "glClearColor");
    void (*p_glClear)(GLuint) = dlsym(mali, "glClear");
    void (*p_glVertexAttribPointer)(GLuint, GLint, GLenum, unsigned char, GLsizei, const void*) = dlsym(mali, "glVertexAttribPointer");
    void (*p_glEnableVertexAttribArray)(GLuint) = dlsym(mali, "glEnableVertexAttribArray");
    void (*p_glDrawArrays)(GLenum, GLint, GLsizei) = dlsym(mali, "glDrawArrays");
    void (*p_glFinish)(void) = dlsym(mali, "glFinish");
    void (*p_glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = dlsym(mali, "glReadPixels");
    GLenum (*p_glGetError)(void) = dlsym(mali, "glGetError");

    EGLDisplay dpy = p_eglGetDisplay(EGL_DEFAULT_DISPLAY);
    printf("[2] eglGetDisplay=%p (err=0x%x)\n", dpy, p_eglGetError());
    EGLint maj = 0, min = 0;
    if (!p_eglInitialize(dpy, &maj, &min)) {
        printf("[!] eglInitialize fallo: 0x%x\n", p_eglGetError());
        return 1;
    }
    printf("[3] EGL %d.%d inicializado! Vendor=%s\n", maj, min, p_eglQueryString(dpy, 0x3053));

    p_eglBindAPI(EGL_OPENGL_ES_API);
    EGLint cfg_attr[] = { EGL_SURFACE_TYPE, 0x0004, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    EGLConfig cfg = NULL;
    EGLint ncfg = 0;
    p_eglChooseConfig(dpy, cfg_attr, &cfg, 1, &ncfg);
    printf("[4] eglChooseConfig ncfg=%d cfg=%p\n", ncfg, cfg);

    EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = p_eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
    printf("[5] eglCreateContext ctx=%p (err=0x%x)\n", ctx, p_eglGetError());

    if (!p_eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
        printf("[!] eglMakeCurrent(surfaceless) fallo: 0x%x\n", p_eglGetError());
        return 1;
    }
    printf("[6] GL_VENDOR=%s | GL_RENDERER=%s | GL_VERSION=%s\n",
           p_glGetString(GL_VENDOR), p_glGetString(GL_RENDERER), p_glGetString(GL_VERSION));

    GLuint fbo = 0, rbo = 0;
    p_glGenRenderbuffers(1, &rbo);
    p_glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    p_glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 64, 64);
    p_glGenFramebuffers(1, &fbo);
    p_glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    p_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbo);
    printf("[7] FBO status=0x%x (0x8CD5=COMPLETE)\n", p_glCheckFramebufferStatus(GL_FRAMEBUFFER));

    const char *vs_s = "attribute vec4 p; void main(){ gl_Position = p; }";
    const char *fs_s = "precision mediump float; void main(){ gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); }";
    GLuint vs = p_glCreateShader(GL_VERTEX_SHADER);
    p_glShaderSource(vs, 1, &vs_s, NULL);
    p_glCompileShader(vs);
    GLuint fs = p_glCreateShader(GL_FRAGMENT_SHADER);
    p_glShaderSource(fs, 1, &fs_s, NULL);
    p_glCompileShader(fs);
    GLuint prog = p_glCreateProgram();
    p_glAttachShader(prog, vs);
    p_glAttachShader(prog, fs);
    p_glBindAttribLocation(prog, 0, "p");
    p_glLinkProgram(prog);
    p_glUseProgram(prog);

    p_glViewport(0, 0, 64, 64);
    p_glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    p_glClear(GL_COLOR_BUFFER_BIT);

    GLfloat verts[] = { 0.0f, 0.8f, 0.0f,  -0.8f, -0.8f, 0.0f,  0.8f, -0.8f, 0.0f };
    p_glVertexAttribPointer(0, 3, GL_FLOAT, 0, 0, verts);
    p_glEnableVertexAttribArray(0);
    p_glDrawArrays(GL_TRIANGLES, 0, 3);
    p_glFinish();
    printf("[8] glDrawArrays + glFinish() COMPLETADO! glGetError=0x%x\n", p_glGetError());

    uint32_t px_corner = 0, px_center = 0;
    p_glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &px_corner);
    p_glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &px_center);
    printf("[9] Pixel esquina (esperado azul 0xff0000ff ABGR?): 0x%08x\n", px_corner);
    printf("[10] Pixel centro (esperado rojo 0xff0000ff): 0x%08x\n", px_center);
    return 0;
}
