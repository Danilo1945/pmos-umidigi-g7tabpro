# 📋 BITÁCORA DE PROGRESO Y CONTROL DE SESIONES
## Repositorio: `FUNCIONALIDADES_FINALES` - UMIDIGI G7 Tab Pro (`TG2403GBA`)
### postmarketOS Edge (Kernel 5.10.185-pmos aarch64 - Alpine Linux musl)

---

## 🚀 INSTRUCCIONES DIRECTAS PARA LA SESIÓN DE LA TARDE (SESIÓN 10)

> [!IMPORTANT]
> **ORDEN ESTRICTA PARA EL ASISTENTE AL INICIAR LA SESIÓN EN LA TARDE:**
> 1. Lee este archivo [`PROGRESO.md`](file:///home/danilo/Documentos/TABLET/FUNCIONALIDADES_FINALES/PROGRESO.md), [`DOCUMENTACION_GENERAL.md`](file:///home/danilo/Documentos/TABLET/FUNCIONALIDADES_FINALES/DOCUMENTACION_GENERAL.md) y [`INFORME_GPU_HARDWARE.md`](file:///home/danilo/Documentos/TABLET/FUNCIONALIDADES_FINALES/subsistemas/03_pantalla_rotacion_touch/INFORME_GPU_HARDWARE.md).
> 2. Mantén congelada la rama `master` (etiqueta `v1.0-estable`). Trabaja en `feature-gpu-hardware-acceleration`.
> 3. No alteres bajo ninguna circunstancia las configuraciones ya estabilizadas de Audio (4x AW87359), Batería OCV v4 ni Wi-Fi WMT.
> 4. Si la tablet estuvo en modo reposo (`sleep`), despiértala con:
>    ```bash
>    python3 tools/tablet_power.py wake
>    ```
>    Si estuvo apagada (`poweroff`), enciéndela con el botón físico POWER (4s) y verifica conexión con:
>    ```bash
>    python3 tools/tablet_power.py status
>    ```

---

### 🎯 OBJETIVOS DE LA SESIÓN DE LA TARDE (SESIÓN 10):

#### 1. Aceleración Gráfica en Espacio de Usuario para `/dev/mali0`
- **Estado Actual**:
  - Silicio GPU ARM Mali-G57 MC2 100% activo en el kernel (`mali_kbase_mt6789`, `ged`, `mtk_gpu_hal`).
  - Nodo `/dev/mali0` creado con permisos `0666`.
  - Phoc/Phosh actualmente renderiza con Pixman NEON (8 hilos CPU) de forma fluida y estable.
- **Misión de la Tarde**:
  - Enlazar `/dev/mali0` con el stack de usuario mediante `libMali-valhall-g57-wayland` o shim EGL/GBM para delegar el renderizado 3D directamente a la GPU física.

#### 2. Consolidación de UPower con Telemetría AUXADC
- El canal ADC `in_voltage0_BATADC_input` reporta la lectura analógica real del PMIC MT6366 en milivoltios.
- Mantener activo el monitor OCV para sincronizar el porcentaje de carga en la barra superior de Phosh.

---

### 📝 Procedimiento de Reactivación Paso a Paso:
```bash
# 1. Comprobar detección USB de la tablet (BROM o Preloader):
lsusb | grep -i mediatek

# 2. Si la tablet sigue en modo Preloader/BROM y ya cargó suficiente:
cd /home/danilo/Documentos/TABLET
# Limpiar misc para salir de recovery:
python3 mtkclient/mtk.py e misc
# Resetear la tablet:
python3 mtkclient/mtk.py reset

# 3. Al iniciar postmarketOS, comprobar la red gadget:
ping -c 3 172.16.42.1
python3 tools/tablet_cmd.py "uptime; cat /sys/class/power_supply/battery/capacity"

# 4. Verificar que el silicio Mali esté activo:
python3 tools/tablet_cmd.py "ls -l /dev/mali0; dmesg | grep -i 'mali 13000000'"

# 5. Protocolo de Rescate en Caliente:
python3 tools/restore_live_tablet.py
```

---

## 📊 ESTADO ACTUAL DE SUBSISTEMAS (Línea Base: `v1.0-estable`)

| Subsistema | Componente | Estado | Versión / Commit | Notas |
| :--- | :--- | :---: | :---: | :--- |
| **01. Audio** | MT6789 AFE + 4x AW87359 | 🟢 **100% OK** | `v1.0-estable` | Ganancias: 32768 (0 dB), Headset: 12 (+2 dB). PipeWire: S16LE, 48kHz, disable-mmap, period-size 6000. Sonido limpio, nítido y sin siseo en los 4 altavoces. |
| **02. Batería** | PMIC MT6366 + MT6375 | 🟢 **100% OK** | `v1.0-estable` | Algoritmo OCV 7500mAh ($R_{int}=85\text{ m}\Omega$), Daemon v4 sin killall upowerd. Notificación D-Bus Refresh y uevent. Rayo de carga y porcentaje real en Phosh. |
| **03. Pantalla/Touch**| MIPI-DSI + TDDI Chipone | 🟢 **100% OK** | `v1.0-estable` | Bypass de sesión `libseat_direct.so` inyectado en Phoc. Orientación invertida 180° aplicada sin latencia a 1200x1920. Touch fluido. |
| **04. Wi-Fi** | MediaTek MT6631 / WMT | 🟢 **100% OK** | `v1.0-estable` | Bus WMT inicializado por `wmt_init_complete.c`. Interfaz `wlan0` administrada por NetworkManager con registro udev dinámico. |
| **05. Arranque** | Boot v4 + WDT Petter | 🟢 **100% OK** | `v1.0-estable` | Kernel 5.10.185-pmos estable sin reinicio de 30s gracias a `wdt_pet`. Red USB gadget fija en `172.16.42.1`. |
| **06. Brillo Pantalla**| `lcd-backlight` (PWM) | 🟡 **En Integración**| `v1.0-estable` | Hardware responde (0..255). Pendiente vincular slider Phosh con `/sys/class/backlight`. |
| **07. GPU Silicio** | ARM Mali-G57 MC2 | 🟢 **Silicio OK** | `feature-gpu-hardware-acceleration` | **Silicio físico activado exitosamente**: `0x9093` Valhall v9 JM r0p1, 2 núcleos activos (`core_mask = 0x5`), devfreq 390-1100 MHz, `/dev/mali0`. |
| **08. GPU Espacio Usuario**| Mesa / EGL / Wayland | 🟡 **En Investigación**| `feature-gpu-hardware-acceleration` | Mesa usa `llvmpipe` / `pixman` (8 núcleos CPU) por falta de render node DRM `/dev/dri/renderD128` (bloqueado por firmas OEM en LK). Evaluando shim EGL / libhybris. |

---

## 📜 HISTORIAL DE SESIONES Y HITOS COMPLETADOS

### Sesión 9: Superación del Logo UMIDIGI, Arranque de Phosh (1200x1920 @ 60Hz), GPU Mali-G57 Activa y Gestión de Hibernación
- **Hitos Técnicos**:
  1. **Diagnóstico y Desbloqueo del Logo de Arranque**:
     - Se determinó que la tablet no estaba colgada; el Little Kernel retenía el framebuffer del logo porque el initramfs entraba en modo interactivo sin invocar al compositor.
     - Se integró `libseat-direct` con `dbus-run-session` y memoria compartida `/dev/shm` (tmpfs 1777).
     - Phoc + Phosh levantaron en **0.80 segundos** (`Phosh ready after 0.80s`), tomando el control de la salida **DSI-1** a su resolución nativa de **1200x1920 @ 60 Hz** con rotación 180°.
  2. **Silicio Mali-G57 MC2 Integrado en Caliente**:
     - Driver oficial `mali_kbase_mt6789.ko` cargado con `ged.ko` y `mtk_gpu_hal.ko`.
     - Nodos `/dev/mali0` y `/sysroot/dev/mali0` creados con permisos `0666` de lectura y escritura.
  3. **Control de Brillo Físico Calibrado**:
     - Brillo activo al 70% (180/255) en `/sys/class/leds/lcd-backlight/brightness`, con soporte de `brightnessctl`.
  4. **Telemetría de Batería por ADC y Herramienta de Energía**:
     - Lectura ADC directa del PMIC MT6366 en `/sys/bus/iio/devices/iio:device1/in_voltage0_BATADC_input` (~4120 mV, ~88-92% de carga real).
     - Creación de [`tools/tablet_power.py`](file:///home/danilo/Documentos/TABLET/FUNCIONALIDADES_FINALES/tools/tablet_power.py) con comandos:
       - `status`: Muestra telemetría en tiempo real de batería, brillo, GPU y UI.
       - `sleep`: Apaga la pantalla a 0 (cero emisión de luz) y baja las CPUs a `powersave` para ultrabajo consumo (< 80mA).
       - `wake`: Reactiva la pantalla al 70% (180) y restaura las CPUs a `schedutil`.
       - `off`: Apagado completo y limpio del sistema (`poweroff`) para preservar la batería al 100% hasta la tarde.

### Sesión 8: Intento Panfrost Upstream, Descubrimiento Devlink y Activación Real del Silicio Mali-G57 MC2
- **Hitos Técnicos**:
  1. **Compilación de Panfrost Built-in**: Se compiló Linux GKI 5.10.185 con `CONFIG_DRM_PANFROST=y` y `CONFIG_DRM_SCHED=y` integrados para eludir las trampas de Clang CFI (`vmlinuz-panfrost-g57`).
  2. **Diagnóstico del Bootloader Little Kernel (LK)**: Se descubrió que el Little Kernel de MediaTek realiza una verificación criptográfica obligatoria de la firma del fabricante en la partición `boot`. Al flashear una imagen no firmada (`pmos_boot_v14.img`), el LK aborta el arranque y cae en modo de rescate Preloader USB (`0e8d:2000`). Se restauró con éxito `pmos_boot_v13.img` mediante `mtkclient`.
  3. **Descubrimiento de la Cadena Devlink de la GPU**:
     - Se identificó la causa exacta por la cual `mali_kbase_mt6789.ko` y `ged.ko` arrojaban `-517` (`EPROBE_DEFER`): el gobernador `13fbf000.gpufreq` requería acceder a los fusibles de calibración térmica grabados en los efuses del SoC (`11c10000.efuse`).
     - Se extrajo e insertó el módulo `nvmem_mtk-devinfo.ko` (`mediatek,devinfo`) junto con `mcupm.ko` y `fhctl.ko`.
  4. **Silicio Mali-G57 MC2 Encendido en Vivo**:
     - El hardware respondió de inmediato:
       ```text
       mali 13000000.mali: GPU identified as 0x3 arch 9.0.9 r0p1 status 0
       mali 13000000.mali: GPU soft reset completed
       mali 13000000.mali: Probed as mali0 (JM)
       ```
     - Telemetría: 2 núcleos de shaders (`core_mask = 0x5`), devfreq 390 a 1100 MHz, nodo `/dev/mali0` operativo.
  5. **Gestión de Descarga Profunda de Batería (0%)**:
     - Al agotarse la batería durante los ensayos, el kernel PMOS se apagaba de emergencia para proteger la celda de litio (al carecer del daemon `kpoc` de Android).
     - Se preparó el slot B con boot stock y se inyectó el comando BCB `boot-recovery` en `misc` (`misc_recovery.bin`) mediante el script por lotes `mtk_batch.txt`, permitiendo que la tablet cargue de manera segura en modo recovery sin apagarse.

### Sesión 7: Silicio GPU Mali-G57 Activado y Diagnóstico de Mesa Panfrost vs kbase
- **Hito**: Mapeo topológico de la GPU e investigación preliminar de dependencias de MediaTek (`tinysys_scmi`, `sspm_v3`, `mtk_gpu_qos`).

### Sesión 6: Solución Definitiva de Batería en Phosh (Daemon v4 sin killall upowerd)
- **Hito**: Eliminación de `killall upowerd`, actualización atómica con D-Bus Refresh y uevent, logrando porcentaje real y rayo de carga estables.

### Sesión 5: Calibración Definitiva de Audio, Repositorio Central y Diagnóstico de Brillo
- **Hito**: Eliminación del siseo en 4 amplificadores AW87359 (0 dB nominal), PipeWire S16LE 48kHz, repositorio `FUNCIONALIDADES_FINALES` y etiquetado `v1.0-estable`.

### Sesión 4: Medidor OCV de Batería y Cargador
- **Hito**: Algoritmo OCV de 100 puntos y bind mounts sobre sysfs.

### Sesión 3: Pantalla DSI, Touch y Bypass Libseat
- **Hito**: Creación de `libseat_direct.c` en ensamblador AArch64 para levantar Phoc/Phosh sin logind. Orientación 180°.

### Sesión 2: Subsistema Wi-Fi MediaTek WMT
- **Hito**: Inicializador C `wmt_init_complete.c` para MT6631 sobre bus STP.

### Sesión 1: Núcleo de Arranque y Control de Watchdog
- **Hito**: Creación de `wdt_pet.c` para alimentar el watchdog MediaTek de 30s y forzar USB Gadget `172.16.42.1`.

---

## 🗺️ HOJA DE RUTA (BACKLOG DE FUTURAS SESIONES)

- [ ] **Fase 1 (Próxima Sesión - Sesión 9)**:
  - Reactivación tras carga de batería (salida de recovery a slot A).
  - Automatización de `nvmem_mtk-devinfo.ko` en `build_pmos_boot_v13.py`.
  - Estrategia de aceleración en espacio de usuario para `/dev/mali0`.
  - Restauración definitiva del slider de brillo de pantalla en Phosh.
- [ ] **Fase 2**: Decodificación de video por hardware VPU (MediaTek Vcodec / V4L2 M2M para 1080p/4K a 0% de CPU).
- [ ] **Fase 3**: Gestión avanzada de energía, suspensión profunda (Deep Sleep) y reactivación por botón físico Power.
- [ ] **Fase 4**: Soporte de cámaras frontal y trasera (MIPI-CSI) y sensores de orientación física por hardware.
