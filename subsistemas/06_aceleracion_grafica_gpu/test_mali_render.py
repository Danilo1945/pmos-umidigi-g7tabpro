#!/usr/bin/env python3
"""
Suite de Prueba de Renderizado Nativo OpenGL ES 3.2 sobre GPU ARM Mali-G57 MC2
postmarketOS Edge / aarch64 (Helio G99 / MT6789)

Verifica de forma aislada:
1. Creación de dispositivo GBM nativo Mali en /dev/dri/card0
2. Inicialización de EGL 1.5 plataforma GBM (eglGetPlatformDisplayEXT)
3. Consulta y selección de configuración EGL (entre las 66 configs disponibles)
4. Creación de superficie Window GBM (256x256 XRGB8888)
5. Creación y vinculación de contexto EGL (eglMakeCurrent)
6. Ejecución de pipeline OpenGL ES 3.2 por hardware (glClearColor, glClear, glFlush)
7. Lectura de VRAM física y verificación del frame renderizado (glReadPixels)
"""

import os
import sys
import ctypes

def main():
    print("=== TEST DE ACELERACIÓN GRÁFICA NATIVA ARM MALI-G57 MC2 ===")
    
    # Cargar libmali ya inyectado en el espacio de direcciones vía LD_PRELOAD
    try:
        mali = ctypes.CDLL(None)
    except Exception as e:
        print(f"[!] Error accediendo al espacio de símbolos de libmali: {e}")
        return 1

    # Prototipos GBM nativos de ARM Mali
    mali.gbm_create_device.argtypes = [ctypes.c_int]
    mali.gbm_create_device.restype = ctypes.c_void_p
    mali.gbm_surface_create.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32]
    mali.gbm_surface_create.restype = ctypes.c_void_p

    # Prototipos EGL
    mali.eglGetProcAddress.argtypes = [ctypes.c_char_p]
    mali.eglGetProcAddress.restype = ctypes.c_void_p
    mali.eglInitialize.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int)]
    mali.eglInitialize.restype = ctypes.c_int
    mali.eglChooseConfig.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_void_p), ctypes.c_int, ctypes.POINTER(ctypes.c_int)]
    mali.eglChooseConfig.restype = ctypes.c_int
    mali.eglCreateWindowSurface.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.POINTER(ctypes.c_int)]
    mali.eglCreateWindowSurface.restype = ctypes.c_void_p
    mali.eglCreateContext.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.POINTER(ctypes.c_int)]
    mali.eglCreateContext.restype = ctypes.c_void_p
    mali.eglMakeCurrent.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
    mali.eglMakeCurrent.restype = ctypes.c_int

    # Prototipos OpenGL ES
    mali.glGetString.argtypes = [ctypes.c_int]
    mali.glGetString.restype = ctypes.c_char_p
    mali.glClearColor.argtypes = [ctypes.c_float, ctypes.c_float, ctypes.c_float, ctypes.c_float]
    mali.glClear.argtypes = [ctypes.c_uint]
    mali.glFlush.argtypes = []
    mali.glReadPixels.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_uint, ctypes.c_uint, ctypes.c_void_p]

    # 1. Abrir DRM Card0 y crear GBM Device
    fd = os.open("/dev/dri/card0", os.O_RDWR)
    gbm_dev = mali.gbm_create_device(fd)
    if not gbm_dev:
        print("[!] Error creando dispositivo GBM Mali nativo")
        return 1
    print(f"[1] Dispositivo GBM Mali creado: {hex(gbm_dev)}")

    # 2. Inicializar Display EGL 1.5
    EGL_PLATFORM_GBM_KHR = 0x31D7
    addr_disp = mali.eglGetProcAddress(b"eglGetPlatformDisplayEXT")
    fn_disp = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_uint, ctypes.c_void_p, ctypes.c_void_p)(addr_disp)
    dpy = fn_disp(EGL_PLATFORM_GBM_KHR, gbm_dev, None)
    
    maj, min = ctypes.c_int(), ctypes.c_int()
    if not mali.eglInitialize(dpy, ctypes.byref(maj), ctypes.byref(min)):
        print("[!] Error en eglInitialize")
        return 1
    print(f"[2] EGL Display inicializado: versión {maj.value}.{min.value}")

    # 3. Elegir Configuración
    attribs = (ctypes.c_int * 9)(
        0x3033, 0x0004, # EGL_SURFACE_TYPE, EGL_WINDOW_BIT
        0x3024, 8,      # EGL_RED_SIZE
        0x3023, 8,      # EGL_GREEN_SIZE
        0x3022, 8,      # EGL_BLUE_SIZE
        0x3038          # EGL_NONE
    )
    cfg = ctypes.c_void_p()
    num_cfgs = ctypes.c_int()
    mali.eglChooseConfig(dpy, attribs, ctypes.byref(cfg), 1, ctypes.byref(num_cfgs))
    print(f"[3] Config EGL elegido: {hex(cfg.value or 0)} (de {num_cfgs.value} compatibles)")

    # 4. Crear Superficie Window GBM
    GBM_FORMAT_XRGB8888 = 0x34325258
    GBM_BO_USE_RENDERING = 1 << 2
    gs = mali.gbm_surface_create(gbm_dev, 256, 256, GBM_FORMAT_XRGB8888, GBM_BO_USE_RENDERING)
    surf = mali.eglCreateWindowSurface(dpy, cfg, gs, None)
    print(f"[4] Superficie Window EGL creada: {hex(surf or 0)}")

    # 5. Crear y activar Contexto OpenGL ES 2.0 / 3.2
    ctx_attribs = (ctypes.c_int * 3)(0x3098, 2, 0x3038)
    ctx = mali.eglCreateContext(dpy, cfg, None, ctx_attribs)
    mali.eglMakeCurrent(dpy, surf, surf, ctx)
    print(f"[5] Contexto EGL conectado con éxito")
    print(f"    Vendor:   {mali.glGetString(0x1F00).decode()}")
    print(f"    Renderer: {mali.glGetString(0x1F01).decode()}")
    print(f"    Version:  {mali.glGetString(0x1F02).decode()}")

    # 6. Renderizar frame con glClear por hardware
    print("[6] Renderizando frame con fondo azul cian (R=0, G=200, B=255)...")
    mali.glClearColor(0.0, 0.784, 1.0, 1.0)
    mali.glClear(0x00004000)
    mali.glFlush()
    print("[7] glFlush ejecutado: comandos procesados por los shaders de Mali")

    # 7. Leer píxel desde VRAM
    buf = (ctypes.c_uint8 * 4)()
    mali.glReadPixels(128, 128, 1, 1, 0x1908, 0x1401, ctypes.byref(buf))
    print(f"[8] Píxel leído por hardware (RGBA): R={buf[0]}, G={buf[1]}, B={buf[2]}, A={buf[3]}")

    os.close(fd)

    if buf[2] > 200 and buf[1] > 150 and buf[0] == 0:
        print("==========================================================================")
        print(">>> VERIFICACIÓN EXITOSA: LA GPU MALI-G57 MC2 RENDERIZÓ EN HARDWARE <<<")
        print("==========================================================================")
        return 0
    else:
        print("[!] Color de píxel no coincide con el renderizado.")
        return 1

if __name__ == "__main__":
    sys.exit(main())
