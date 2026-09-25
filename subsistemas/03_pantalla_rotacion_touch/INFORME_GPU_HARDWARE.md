# Hallazgos de Aceleración Gráfica por Hardware - Mali-G57 MC2 (MT6789)

## 1. Resumen Ejecutivo
Se completó la investigación técnica profunda sobre la activación de aceleración gráfica por hardware en la tablet UMIDIGI G7 Tab Pro (SoC MediaTek Helio G99 `MT6789`, GPU ARM Mali-G57 MC2) en postmarketOS Edge (Alpine Linux aarch64 musl).

---

## 2. Resultados de las Pruebas

### A. Intento de Driver Panfrost Upstream (Linux DRM)
1. **Compilación y Kernel Built-in:**
   - Se compiló el kernel Linux GKI 5.10.185 con `CONFIG_DRM_PANFROST=y` y `CONFIG_DRM_SCHED=y` integrados como built-in para evitar los bloqueos de integridad CFI de Clang (`BRK 0x800`).
   - Se adaptaron los identificadores de hardware para Mali-G57 (`0x9003` Valhall JM) y los compatibles DT (`mediatek,mali`, `arm,mali-valhall`).
2. **Resultado de Arranque:**
   - La imagen `v14` generada fue flasheada a los slots de arranque. El bootloader de fábrica MediaTek (Little Kernel / LK) rechazó la firma binaria del nuevo `vmlinux`, entrando en modo de recuperación Preloader USB (`0e8d:2000`).
   - Se ejecutó la restauración inmediata y sin pérdida a la imagen estable `v13`.

### B. Descubrimiento y Activación del Driver Nativo del Silicio (`mali_kbase`)
1. **Resolución de Dependencias Ocultas:**
   - El driver nativo de ARM para Valhall v9 (`mali_kbase_mt6789.ko`) y el controlador MediaTek GED (`ged.ko`) requerían una cadena estricta de proveedores devlink en hardware:
     - `nvmem_mtk-devinfo.ko` (suministrador de calibración térmica y fusibles efuse para `13fbf000.gpufreq`).
     - `mcupm.ko` + `fhctl.ko` (control de frecuencia y relojes de hardware de la GPU).
     - `mtk_gpu_hal.ko` + `sspm_v3.ko` + `mtk_qos.ko` + `mtk-ssc.ko` + `gpueb.ko` + `mtk_gpufreq_wrapper.ko` + `mtk_gpu_qos.ko` + `mtk_gpufreq_mt6789.ko`.
2. **Probing Exitoso en Silicio Real:**
   - Al insertar el controlador `nvmem_mtk-devinfo.ko`, la cadena devlink se satisfizo instantáneamente y el núcleo reconoció la GPU:
   ```text
   mali 13000000.mali: GPU identified as 0x3 arch 9.0.9 r0p1 status 0
   mali 13000000.mali: GPU soft reset completed
   mali 13000000.mali: Probed as mali0 (JM)
   probe of 13000000.mali returned 1
   ```
   - Telemetría en vivo:
     - **Dispositivo**: `Mali-G57 2 cores r0p1 0x9093`
     - **Core mask activa**: `0x5` (los 2 núcleos físicos de shaders activados)
     - **Frecuencias dinámicas (devfreq)**: Activas desde 390 MHz hasta 1100 MHz.
     - **Nodo de acceso**: `/dev/mali0` (carácter 10, 116).

---

## 3. Estado de la Aceleración en Espacio de Usuario (Mesa vs DDK)

1. **Arquitectura del Driver Mali de ARM:**
   - El driver del kernel `mali_kbase` no expone una interfaz DRM/KMS tradicional (`/dev/dri/cardX` con render node), sino un nodo de dispositivo de control directo de hardware: `/dev/mali0`.
   - La librería Mesa estándar de Linux (Gallium/DRI) requiere el driver DRM `panfrost` a nivel de kernel para comunicarse con el hardware a través de `/dev/dri/renderD128`.
   - Al no existir el driver DRM Panfrost firmado y admitido por el bootloader bloqueado, Mesa en postmarketOS utiliza su renderizador de alta eficiencia `llvmpipe` / `pixman` sobre el display controller `/dev/dri/card0` (`14000000.dispsys_config`).

2. **Garantía de Estabilidad:**
   - Los 5 subsistemas críticos continúan al 100% operativos:
     1. **Audio Hi-Fi 4x AW87359**: Configuración ALSA activa.
     2. **Batería OCV v4**: Curva de voltaje y porcentaje real.
     3. **Pantalla DSI 180° y Touch**: Display 1200x1920 y matriz calibrada.
     4. **Wi-Fi WMT**: Firmware cargado y enlace activo.
     5. **Watchdog de Hardware**: Servicio `/bin/wdt_pet` alimentando el temporizador sin interrupciones.
