# Documento de Progreso y Arquitectura Técnica: Aceleración GPU ARM Mali-G57 MC2 en Interfaz Gráfica

**Dispositivo:** UMIDIGI G7 Tab Pro (`TG2403GBA`)  
**SoC:** MediaTek MT6789 (Helio G99) - Octa-Core (2x Cortex-A76 @ 2.2GHz + 6x Cortex-A55 @ 2.0GHz)  
**GPU Física:** ARM Mali-G57 MC2 (Arquitectura Valhall/Bifrost v1.g24p0-00eac0)  
**Controlador Display:** MediaTek DDP / DSI CMDQ (`14000000.dispsys_config` / `14013000.dsi`)  
**Panel LCD:** 1200x1920 @ 60 Hz (`panel-l0a9w006c-dsi-vdo` / controlador Novatek `NT36672E`)  
**Sistema Operativo:** postmarketOS Edge (Alpine Linux aarch64 musl libc / Kernel 5.10.185-android12)  
**Entorno Gráfico:** Phoc (wlroots compositor Wayland) + Phosh (GNOME Mobile Shell)  
**Rama Git:** `feature-gpu-interface-driver`  
**Fecha de corte:** Octubre 2026  

---

## 1. Resumen Ejecutivo y Estado Actual

- **Panel Físico LCD y Backlight Operativos**:
  - Resuelto el problema de la pantalla en negro. Se identificó que la retroiluminación física del panel LCD depende de la línea **GPIO 12 (`lcm_led_en1`)**.
  - El canal PWM de hardware (`disp_pwm0`, `period=39385 ns`, `duty=35419 ns`) y el chip de alimentación de display **AW37503** (I2C bus 3, `0x3e`) están energizados y confirmados.
  - El daemon de hardware `/usr/bin/panel_pwm_force` mantiene las líneas en `HIGH` y el brillo en `255/255`.
- **Modo Actual de la Interfaz**:
  - `Phosh` y `Phoc` están corriendo de forma estable en el panel físico a resolución completa (1200x1920) y orientados correctamente a 180°.
  - Actualmente está configurado en **`WLR_RENDERER=pixman`** (renderizado por software en CPU) para garantizar estabilidad del modeset mientras se depuraba el display.
- **GPU Mali-G57 MC2 Lista y Validada**:
  - Módulos del kernel cargados (`mali_kbase_mt6789.ko` en `/dev/mali0`).
  - Driver shim `libgpu_interface_driver.so` valida inicialización nativa de **EGL 1.5 y GLES 3.2 de ARM Mali**.
  - Validada la importación transparente de dumb buffers asignados por `card0` en Mali vía `gbm_bo_import`.

---

## 2. Investigación y Diagnóstico Detallado

### A. La Causa Raíz de la Pantalla Negra (Hardware LCD / Backlight)
Al realizar el modeset KMS (`drmModeSetCrtc` devolviendo `ret=0`), el CRTC y el plano `plane-0 [34]` estaban enviando datos de píxeles, pero el panel no emitía luz. Mediante la inspección del device tree (`/proc/device-tree/`):
1. **Controlador del Panel**: El kernel usa el driver `panel-l0a9w006c-dsi-vdo.ko` (compatible con `l0a9w006c_dsi` y chip Novatek `NT36672E`).
2. **Pines de Alimentación y Control**:
   - `reset-gpios`: Pin 85 (GPIO85) -> Puesto en `HIGH` por el kernel.
   - `bias-gpios`: Pin 103 (GPIO103) -> Puesto en `HIGH` por el kernel.
   - `vdd1v8-gpios`: Pin 106 (GPIO106) -> Puesto en `HIGH` por el kernel.
   - **`lcm_led_en1_gpio`**: **Pin 12 (GPIO12)**. Este pin controla el Enable del convertidor elevador de voltaje de los LEDs de la retroiluminación. Al arrancar el sistema estaba desreclamado (`UNCLAIMED`) en estado bajo (`LOW`).
   - Al forzar `GPIO12` a `HIGH` mediante `ioctl(gfd, GPIO_GET_LINEHANDLE_IOCTL, &req)`, el circuito elevador se activó y el panel físico encendió su luz de fondo inmediatamente.

### B. Conflicto de Arquitectura de Memoria entre Mali y MediaTek Display (KMS)
Durante las pruebas previas con `WLR_RENDERER=gles2`, el sistema experimentaba un reinicio de hardware (*watchdog hard reset*). La investigación arrojó el motivo exacto:
1. **GPU Desacoplada**: En el SoC Helio G99, la GPU Mali-G57 es un bloque IP independiente de ARM con su propio paginado y MMU. El controlador de pantalla (`mediatek-drm` / `cmdq`) lee desde el IOMMU multimedia de MediaTek (`mtk_iommu` / `mtk_smi`).
2. **Fallo de Scanout Directo de Mali**:
   - Cuando Phoc solicita `gbm_bo_create` con flag `GBM_BO_USE_SCANOUT` utilizando el asignador propietario de `libmali`, la memoria asignada no cuenta con mapeo contiguo CMA ni registro en el IOMMU de pantalla de MediaTek.
   - Al ejecutar `drmModeSetCrtc` o un commit atómico KMS con ese buffer, el DMA de MediaTek provoca una violación de seguridad (`device_apc_mt6789`) o pánico de IOMMU, disparando el reinicio del SoC.
3. **Solución Comprobada**:
   - La asignación de buffers dumb directamente en el kernel mediante `DRM_IOCTL_MODE_CREATE_DUMB` sobre `/dev/dri/card0` es 100% segura para MediaTek KMS.
   - Probamos exitosamente la importación de dicho buffer dumb de `card0` hacia el allocator de Mali mediante **`gbm_bo_import(gbm, GBM_BO_IMPORT_FD, &import_data, ...)`** (DMA-BUF prime import).
   - Esto permite que **Mali dibuje por aceleración OpenGL ES 2.0 / 3.2 sobre el buffer importado, mientras que el hardware de MediaTek lo escanea hacia el panel LCD sin fallos ni reinicios**.

---

## 3. Componentes Implementados en el Sistema

### 1. Driver Interceptor: `libgpu_interface_driver.so`
- **Ubicación fuente:** `/home/danilo/Documentos/TABLET/FUNCIONALIDADES_FINALES/subsistemas/06_aceleracion_grafica_gpu/gpu_interface_driver.c`
- **Ubicación binaria instalada:** `/sysroot/usr/lib/libgpu_interface_driver.so`
- **Capacidades integradas:**
  - **Puente ABI Musl <-> Glibc**: Emula `fcntl64`, `pthread_atfork`, `mallinfo`, `strtol_l`, y funciones con fortificación `__chk`.
  - **Traductor KBASE IOCTL**: Traduce las llamadas de usuariospace `0xc0048034` a la versión de kernel MT6789 `0xc0048000` con `kver = {11, 38}`, y filtra las banderas seguras de `SET_FLAGS` (`0x7a`).
  - **GBM Shim**: Proporciona soporte para modificadores lineales (`DRM_FORMAT_MOD_LINEAR`) y stubs multi-plano requeridos por `wlroots`.
  - **EGL Extensions**: Intercepta `eglBindWaylandDisplayWL`.

### 2. Daemon de Retroiluminación: `panel_pwm_force`
- **Ubicación fuente:** `/sysroot/usr/bin/panel_pwm_force.c`
- **Ubicación binaria:** `/sysroot/usr/bin/panel_pwm_force`
- **Función:** Mantiene en nivel alto permanente el pin GPIO 12 (`lcm_led_en1`) y fija el brillo en `/sys/class/leds/lcd-backlight/brightness` a `255`.

### 3. Script de Inicio de la Sesión: `start_phosh.sh`
- **Ubicación:** `/sysroot/usr/bin/start_phosh.sh`
- **Configuración actual:**
  ```sh
  export WLR_BACKENDS=drm,libinput
  export WLR_NO_HARDWARE_CURSORS=1
  export WLR_DRM_NO_MODIFIERS=1
  export WLR_RENDERER=pixman
  export WLR_RENDERER_ALLOW_SOFTWARE=1
  export LD_PRELOAD=/usr/lib/libseat_direct.so
  ```

---

## 4. Plan de Acción y Hoja de Ruta para la Próxima Sesión

### Objetivo Central
> **Conectar y ejecutar la interfaz gráfica Phoc/Phosh acelerada por la GPU ARM Mali-G57 MC2 (`WLR_RENDERER=gles2`) con presentación directa y fluida en la pantalla LCD de la tablet.**

### Pasos Técnicos Precisos:

1. **Ajuste en `gpu_interface_driver.c` para Intercepción Inteligente de GBM**:
   - En `gbm_bo_create`:
     - Si los flags contienen `GBM_BO_USE_SCANOUT`, crear el buffer primario como dumb buffer en `card0` (`DRM_IOCTL_MODE_CREATE_DUMB`), exportarlo vía `drmPrimeHandleToFD` e importarlo automáticamente a Mali con `gbm_bo_import`.
     - Si es solo para renderizado interno (`GBM_BO_USE_RENDERING` sin scanout), invocar directamente el `gbm_bo_create` nativo de Mali.
   - De este modo, `wlroots` recibe un `gbm_bo` que tanto Mali (vía EGL/GLES) como MediaTek KMS (vía DRM FB) aceptan sin fricción.

2. **Compilar e Implementar la Nueva Versión de `libgpu_interface_driver.so`**:
   - Compilar dentro del chroot:
     ```sh
     gcc -shared -fPIC -O2 -I/usr/include/libdrm \
         /home/danilo/Documentos/TABLET/FUNCIONALIDADES_FINALES/subsistemas/06_aceleracion_grafica_gpu/gpu_interface_driver.c \
         -ldl -lpthread -ldrm -lgbm -o /sysroot/usr/lib/libgpu_interface_driver.so
     ```

3. **Modificar `start_phosh.sh` para Activar la GPU**:
   - Cambiar variables en `/sysroot/usr/bin/start_phosh.sh`:
     ```sh
     export WLR_RENDERER=gles2
     export LD_PRELOAD=/usr/lib/libgpu_interface_driver.so:/usr/lib/libseat_direct.so
     # Desactivar software fallback
     unset WLR_RENDERER_ALLOW_SOFTWARE
     ```
   - Asegurar que antes de ejecutar Phoc, se ejecute `/usr/bin/panel_pwm_force &`.

4. **Verificación y Validación**:
   - Iniciar Phoc/Phosh.
   - Inspeccionar `/var/log/phosh.log` para confirmar:
     - `Creating gles2 renderer`
     - `Using EGL 1.5 Bifrost-g24p0-00eac0 / ARM Mali-G57`
     - `GL_RENDERER: Mali-G57 MC2`
   - Comprobar visualmente fluidez, animaciones aceleradas y 60 FPS en el panel LCD de la tablet.
   - Guardar commit en Git sobre la rama `feature-gpu-interface-driver`.

---
*Documento generado y archivado para continuación inmediata en la siguiente sesión.*
