#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

static const char *vs_src =
    "attribute vec4 vPosition;\n"
    "void main() {\n"
    "  gl_Position = vPosition;\n"
    "}\n";

static const char *fs_src =
    "precision mediump float;\n"
    "void main() {\n"
    "  gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);\n"
    "}\n";

static GLuint load_shader(GLenum type, const char *shader_src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &shader_src, NULL);
    glCompileShader(shader);
    GLint compiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char info[512];
        glGetShaderInfoLog(shader, sizeof(info), NULL, info);
        printf("[!] Shader compile error: %s\n", info);
        return 0;
    }
    return shader;
}

int main() {
    setbuf(stdout, NULL);
    printf("=== TEST DRAW ARRAYS CON FBO + MALI GPU ===\n");
    int fd = open("/dev/dri/card0", O_RDWR);
    if (fd < 0) { perror("open card0"); return 1; }

    struct gbm_device *gbm = gbm_create_device(fd);
    PFNEGLGETPLATFORMDISPLAYEXTPROC my_get_plat = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay dpy = my_get_plat(0x31D7, gbm, NULL);

    EGLint maj, min;
    eglInitialize(dpy, &maj, &min);
    printf("[1] EGL %d.%d, Vendor: %s\n", maj, min, eglQueryString(dpy, EGL_VENDOR));

    EGLint cfg_attribs[] = {
        EGL_SURFACE_TYPE, EGL_DONT_CARE,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_NONE
    };
    EGLConfig cfg;
    EGLint num;
    eglChooseConfig(dpy, cfg_attribs, &cfg, 1, &num);

    EGLint ctx_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attribs);

    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
    printf("[2] Context OK: GL_VENDOR: %s, GL_RENDERER: %s\n",
           glGetString(GL_VENDOR), glGetString(GL_RENDERER));

    printf("[3] Creando BO GBM y EGLImage FBO...\n");
    struct gbm_bo *bo = gbm_bo_create(gbm, 64, 64, GBM_FORMAT_XRGB8888, GBM_BO_USE_RENDERING);
    if (!bo) {
        printf("[!] gbm_bo_create retorno NULL\n");
        return 1;
    }

    PFNEGLCREATEIMAGEKHRPROC my_eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC my_glEGLImageTargetRenderbufferStorageOES = (PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC)eglGetProcAddress("glEGLImageTargetRenderbufferStorageOES");

    EGLImageKHR img = my_eglCreateImageKHR(dpy, EGL_NO_CONTEXT, EGL_NATIVE_PIXMAP_KHR, (EGLClientBuffer)bo, NULL);
    if (!img) {
        printf("[!] eglCreateImageKHR retorno NULL\n");
        return 1;
    }

    GLuint rbo = 0, fbo = 0;
    glGenRenderbuffers(1, &rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    my_glEGLImageTargetRenderbufferStorageOES(GL_RENDERBUFFER, img);

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbo);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    printf("[4] Framebuffer status: 0x%x (0x8CD5 = COMPLETE)\n", status);

    printf("[5] Compilando shaders...\n");
    GLuint vs = load_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = load_shader(GL_FRAGMENT_SHADER, fs_src);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "vPosition");
    glLinkProgram(prog);
    GLint linked;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        char info[512];
        glGetProgramInfoLog(prog, sizeof(info), NULL, info);
        printf("[!] Program link error: %s\n", info);
        return 1;
    }
    printf("[6] Shaders linkeados OK!\n");

    glViewport(0, 0, 64, 64);
    glClearColor(0.0, 0.0, 0.5, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(prog);
    GLfloat vertices[] = {
         0.0f,  0.5f, 0.0f,
        -0.5f, -0.5f, 0.0f,
         0.5f, -0.5f, 0.0f
    };
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, vertices);
    glEnableVertexAttribArray(0);

    printf("[7] Ejecutando glDrawArrays...\n");
    glDrawArrays(GL_TRIANGLES, 0, 3);
    printf("[8] glDrawArrays retorno! Ejecutando glFinish...\n");
    glFinish();
    printf("[9] glFinish retorno con exito!\n");

    printf("=== TEST DRAW ARRAYS CON FBO COMPLETADO EXITOSAMENTE ===\n");
    _exit(0);
}
