# Hallazgos de Aceleración Gráfica por Hardware - Mali-G57 MC2 (MT6789)
## UMIDIGI G7 Tab Pro (`TG2403GBA`) - SoC MediaTek Helio G99
### postmarketOS Edge (Kernel 5.10.185-pmos aarch64 - Alpine Linux musl)

---

## 1. Resumen Ejecutivo y Hitos Alcanzados
En esta fase de investigación e ingeniería de bajo nivel, se alcanzó un hito histórico para el SoC MediaTek Helio G99 en entornos Linux puros (postmarketOS / Alpine musl):
1. **Activación Física Real del Silicio GPU**: Se descubrió la causa raíz que mantenía inoperante al driver nativo de ARM `mali_kbase_mt6789.ko` (error de enlace `devlink` diferido `EPROBE_DEFER` `-517`) y se encendió el silicio gráfico Mali-G57 MC2 (Valhall v9 JM r32p1) con sus 2 núcleos de shaders (`core_mask = 0x5`) y escalado dinámico de reloj (390 MHz - 1100 MHz), creando con éxito el nodo `/dev/mali0`.
2. **Diagnóstico del Bootloader MediaTek Little Kernel (LK)**: Se probó la compilación de Linux GKI con el driver DRM upstream `panfrost` como `built-in` (`vmlinuz-panfrost-g57`). Se determinó que el bootloader LK de fábrica aplica verificación estricta de firmas criptográficas OEM sobre el binario del kernel, bloqueando imágenes no firmadas hacia el modo de emergencia Preloader BROM.
3. **Análisis de Capas de Espacio de Usuario (Mesa vs ARM DDK)**: Se identificó la discrepancia arquitectónica entre Mesa Gallium (que requiere el subsistema DRM `/dev/dri/renderD128`) y el driver `mali_kbase` de ARM (diseñado para ioctls directos hacia el DDK de Android Bionic `libGLES_mali.so`).
4. **Rescate de Batería Crítica (0%) y Carga de Emergencia**: Al agotarse la batería durante pruebas continuas, se implementó una solución de arranque en modo Recovery/Stock en el slot B mediante comandos BCB (`misc_recovery.bin`) y scripts BROM por lotes (`mtk_batch.txt`), permitiendo la recarga segura de la celda de litio sin interrupción por apagado de emergencia del kernel.

---

## 2. Investigación Técnica Detallada

### A. Intento de Driver Upstream Panfrost (DRM Linux Kernel)
- **Objetivo**: Integrar el driver DRM libre de ARM Mali (`panfrost`) en el kernel Linux GKI 5.10.185 para generar de manera nativa el nodo `/dev/dri/renderD128`, permitiendo que Mesa 26.1 (`panfrost_dri.so`) lo consuma directamente sin intermediarios.
- **Compilación**:
  - Se configuraron `CONFIG_DRM_PANFROST=y` y `CONFIG_DRM_SCHED=y` como `built-in` (`=y`) en el kernel, eludiendo las restricciones de Clang CFI (`Control Flow Integrity`, trampa `BRK 0x800`).
  - Se incorporaron las tablas de compatibilidad para Valhall v9 JM (`0x9003`) y compatibles del Device Tree (`mediatek,mali`, `arm,mali-valhall`).
  - Se generó el kernel `vmlinuz-panfrost-g57` (14 MB) y la imagen `pmos_boot_v14.img`.
- **Comportamiento del Hardware al Flashear**:
  - Al programar `pmos_boot_v14.img` en `boot_a` y `boot_b`, el dispositivo no inició el kernel.
  - *Diagnóstico*: El Little Kernel (LK) de MediaTek verifica la firma RSA/ECDSA de la partición `boot`. Al detectar que el binario `vmlinux` fue modificado y no contiene la firma privada de UMIDIGI/MediaTek, el bootloader aborta la secuencia de arranque de inmediato y delega el control al Preloader USB (`0e8d:2000`).
  - *Acción correctiva*: Se utilizó `mtkclient` para restaurar instantáneamente la imagen `pmos_boot_v13.img` (que conserva el kernel firmado por el OEM), restableciendo la operatividad del sistema sin pérdida de datos.

---

### B. Descubrimiento y Activación del Silicio con Drivers del Fabricante (`mali_kbase`)
- **Problema de Fondo**:
  Tanto `mali_kbase_mt6789.ko` como el daemon de gestión energética MediaTek GED (`ged.ko`) fallaban sistemáticamente al intentar registrarse en el sistema con el código de error `-517` (`EPROBE_DEFER`):
  ```text
  platform 13000000.mali: Driver mali-kbase requests probe deferral
  ```
- **Análisis de Enlace Topológico de Hardware (`devlink`)**:
  Mediante inspección del árbol de dispositivos (DTS) y del subsistema `sysfs`, se rastreó la cadena jerárquica de dependencias de energía, relojes y calibración térmica:
  $$\text{13000000.mali (GPU)} \longrightarrow \text{soc:ged} \longrightarrow \text{13fbf000.gpufreq} \longrightarrow \begin{cases} \text{11c10000.efuse (mediatek,devinfo)} \\ \text{1000ce00.fhctl (control de frecuencias)} \end{cases}$$
  El componente `13fbf000.gpufreq` requería leer los fusibles efuse térmicos y de voltaje grabados en fábrica para calibrar el regulador de la GPU. Al faltar el driver de calibración efuse, todo el árbol de sondeo se posponía indefinidamente.

- **Solución al Bloqueo**:
  Se extrajo del firmware stock el módulo `nvmem_mtk-devinfo.ko` (`mediatek,devinfo`) junto con los módulos de soporte `mcupm.ko` y `fhctl.ko`.

- **Cadena Determinista de Carga de Módulos (16 Módulos)**:
  Para inicializar el silicio Mali-G57 de forma determinista, los módulos deben cargarse en el orden estricto siguiente:
  1. `nvmem_mtk-devinfo.ko` *(efuse y calibración térmica - CLAVE)*
  2. `mcupm.ko` *(microcontrolador de gestión de energía)*
  3. `fhctl.ko` *(control de salto de frecuencia de reloj)*
  4. `mtk_gpu_hal.ko` *(abstracción de hardware de MediaTek)*
  5. `sspm_v3.ko` *(procesador seguro de MediaTek)*
  6. `mtk_qos.ko` *(calidad de servicio del bus)*
  7. `mtk-ssc.ko` *(espectro ensanchado de reloj)*
  8. `gpueb.ko` *(puente de eventos de GPU)*
  9. `mtk_bp_thl.ko` *(throttling de presión de batería)*
  10. `mtk_battery_oc_throttling.ko` *(protección contra sobrecorriente)*
  11. `mtk_gpufreq_wrapper.ko` *(envoltorio de frecuencia)*
  12. `mtk_gpu_qos.ko` *(calidad de servicio GPU)*
  13. `mtk_gpufreq_mt6789.ko` *(gobernador devfreq GPU)*
  14. `ged.ko` *(MediaTek Graphics Execution Daemon)*
  15. `mali_mgm_mt6789.ko` *(gestor de memoria Mali)*
  16. `mali_kbase_mt6789.ko` *(driver principal ARM Mali kbase)*

- **Telemetría Confirmada en Silicio Real (dmesg)**:
  Al ejecutar la secuencia en la tablet en vivo, el kernel reportó:
  ```text
  mali 13000000.mali: GPU identified as 0x3 arch 9.0.9 r0p1 status 0
  mali 13000000.mali: GPU soft reset completed
  mali 13000000.mali: Probed as mali0 (JM)
  probe of 13000000.mali returned 1
  ```
  - **Identificador de Silicio**: Mali-G57 2 cores r0p1 (Arquitectura Valhall v9, ID `0x9093`).
  - **Núcleos de Cómputo Activos**: `core_mask = 0x5` (los 2 núcleos físicos de shaders operando simultáneamente).
  - **Gobernador de Frecuencia**: `devfreq` escalando dinámicamente entre 390 MHz (reposo) y 1100 MHz (carga máxima).
  - **Dispositivo de Control Creado**: `/dev/mali0` (carácter major 10, minor 116) con permisos `0666` en raíz y `/sysroot/dev/mali0`.

---

## 3. Arquitectura del Espacio de Usuario: Mesa vs DDK Propietario

| Componente | Driver Upstream Linux (Panfrost) | Driver Oficial ARM (mali_kbase) |
| :--- | :--- | :--- |
| **Interfaz en Kernel** | DRM/KMS (`/dev/dri/renderD128`) | Dispositivo caracter ioctl (`/dev/mali0`) |
| **Soporte en Mesa DRI** | Nativo (`panfrost_dri.so`) | No implementado (Mesa no soporta ioctls de kbase) |
| **Espacio de Usuario Nativo**| Librerías libres Mesa (EGL/GLES2/Vulkan) | DDK ARM Bionic de Android (`libGLES_mali.so`, `vulkan.mali.so`) |
| **Compositor Wayland (Phoc)**| Abre `/dev/dri/renderD128` | Requiere shim GBM/EGL o puente `libhybris` |
| **Estado Actual en PMOS** | Bloqueado por firma LK del bootloader | **Silicio Físicamente Operativo en `/dev/mali0`** |

### Situación del Renderizado en Phosh / Phoc:
- El display controller físico (`14000000.dispsys_config`) está expuesto en `/dev/dri/card0` por `mediatek_drm` a 1200x1920 @ 60 Hz.
- Dado que Mesa no cuenta actualmente con un traductor directo de `/dev/mali0` a Gallium DRI, Mesa selecciona el pipeline de software optimizado `llvmpipe` / `pixman` utilizando los 8 núcleos de CPU (Cortex-A76 + Cortex-A55) distribuidos con `LP_NUM_THREADS=8`.
- El entorno Phosh opera de manera fluida y responsiva a 60 fps con orientación corregida a 180° y escala 1.0.

---

## 4. Incidente de Batería Crítica (0%) y Solución de Carga de Emergencia

### A. Causa Raíz
Durante las pruebas intensivas de compilación y benchmarking, la batería de 7500 mAh se descargó por completo hasta alcanzar el umbral de 0% (voltaje de corte por hardware $\approx 3.4\text{ V}$).
- En Android de fábrica, el bootloader transfiere el control al subsistema `kpoc` (Kernel Power-Off Charging), que mantiene la pantalla apagada o con animación de batería y permite que el PMIC MT6375 cargue la celda.
- En postmarketOS Edge, el daemon `kpoc` no está integrado en el espacio de usuario. Al arrancar el kernel de postmarketOS con 0% de batería, el subsistema de gestión de energía del kernel detecta voltaje bajo y ejecuta un apagado de emergencia inmediato (`shutdown`) para salvaguardar la química de la celda de litio.
- Esto impedía que la tablet se cargara mientras intentara arrancar postmarketOS.

### B. Solución de Ingeniería Implementada
Para garantizar la carga continua y segura desde 0%:
1. **Configuración de Slot Alterno (`boot_b`)**:
   Se preparó la partición `boot_b` con la imagen de fábrica stock (`stock_firmware/.../boot.img`), la cual contiene el ramdisk y controladores de carga originales.
2. **Inyección de Bloque de Control de Arranque (BCB en `misc`)**:
   Se generó un binario de partición `misc` (`misc_recovery.bin`, 2048 bytes) con la estructura BCB de Android:
   - `command`: `boot-recovery\0`
   - `recovery`: `recovery\n`
   Al detectar este comando en la partición `misc`, el Little Kernel desvía el arranque automáticamente hacia el modo Recovery / Carga apagada, evitando que el sistema se apague y permitiendo que el PMIC cargue la batería al 100%.
3. **Automatización con `mtkclient` por Lotes**:
   Se creó el script de procesamiento por lotes `/home/danilo/Documentos/TABLET/mtk_batch.txt`:
   ```text
   w boot_b /home/danilo/Documentos/TABLET/stock_firmware/UMIDIGI_G7_Tab_Pro_E_V1.0_20250512/boot.img
   w misc /home/danilo/Documentos/TABLET/misc_recovery.bin
   reset
   ```
   Y el ejecutable lanzador `/home/danilo/Documentos/TABLET/flash_recovery_slot_b.sh`.

---

## 5. Hoja de Ruta e Instrucciones para la Próxima Sesión

1. **Paso 1: Verificación de Carga y Encendido**:
   - Conectar la tablet al cargador de pared de alta potencia (5V/2A o USB-PD) hasta alcanzar al menos un 20-30% de carga.
   - Encender la tablet presionando el botón `POWER` durante 5 segundos continuos.
   - Verificar la aparición de la interfaz de red USB gadget mediante:
     ```bash
     ip a show usb0  # o verificar ping 172.16.42.1
     ```
2. **Paso 2: Conmutar al Arranque de postmarketOS (Slot A)**:
   - Limpiar la partición `misc` para cancelar el bucle de recovery:
     ```bash
     python3 /home/danilo/Documentos/TABLET/mtkclient/mtk.py e misc
     ```
   - Asegurar que el slot activo sea el slot A (`boot_a` con `pmos_boot_v13.img`).
3. **Paso 3: Automatizar `nvmem_mtk-devinfo.ko` en el Ramdisk (`v13`)**:
   - Incorporar `nvmem_mtk-devinfo.ko` directamente en el script `version_v13/build_pmos_boot_v13.py` para que el silicio Mali-G57 se encienda de manera completamente desatendida durante el arranque en `initramfs`.
4. **Paso 4: Exploración de Aceleración de Usuario**:
   - Evaluar el puente de aceleración EGL mediante `libhybris` o un shim DRM sobre `/dev/mali0`, permitiendo vincular los blobs binarios de ARM Mali hacia wlroots/Phoc.
5. **Paso 5: Regulación de Brillo de Pantalla**:
   - Finalizar el enlace D-Bus para que el slider de brillo de Phosh actúe directamente sobre `/sys/class/leds/lcd-backlight/brightness`.
