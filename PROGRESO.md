# 📋 BITÁCORA DE PROGRESO Y CONTROL DE SESIONES
## Repositorio: `FUNCIONALIDADES_FINALES` - UMIDIGI G7 Tab Pro (`TG2403GBA`)
### postmarketOS Edge (Kernel 5.10.185-pmos aarch64 - Alpine Linux musl)

---

## 🚀 INSTRUCCIONES DIRECTAS PARA LA PRÓXIMA SESIÓN (SESIÓN 9)

> [!IMPORTANT]
> **ORDEN ESTRICTA PARA EL ASISTENTE AL INICIAR LA PRÓXIMA SESIÓN:**
> 1. Lee este archivo [`PROGRESO.md`](file:///home/danilo/Documentos/TABLET/FUNCIONALIDADES_FINALES/PROGRESO.md), [`DOCUMENTACION_GENERAL.md`](file:///home/danilo/Documentos/TABLET/FUNCIONALIDADES_FINALES/DOCUMENTACION_GENERAL.md) y [`INFORME_GPU_HARDWARE.md`](file:///home/danilo/Documentos/TABLET/FUNCIONALIDADES_FINALES/subsistemas/03_pantalla_rotacion_touch/INFORME_GPU_HARDWARE.md).
> 2. Mantén congelada la rama `master` (etiqueta `v1.0-estable`). Trabaja en `feature-gpu-hardware-acceleration`.
> 3. No alteres bajo ninguna circunstancia las configuraciones ya estabilizadas de Audio (4x AW87359), Batería OCV v4 ni Wi-Fi WMT.
> 4. Sigue la secuencia operativa de reactivación y arranque detallada a continuación.

---

### 🎯 OBJETIVOS DE LA PRÓXIMA SESIÓN:

#### 1. Reactivación del Arranque postmarketOS (Slot A) tras Carga de Batería
- **Estado Inicial de la Tablet**: Al finalizar la Sesión 8, la batería se agotó (0%). Se configuró el arranque en modo Recovery / Stock en `boot_b` mediante el comando BCB `misc_recovery.bin` para permitir que el hardware cargue sin apagarse por software.
- **Acción Inmediata**:
  1. Verificar que la tablet haya alcanzado al menos 20-30% de carga.
  2. Limpiar la partición `misc` para cancelar el bucle de recovery (`python3 mtkclient/mtk.py e misc`).
  3. Asegurar que el slot activo sea el slot A (`boot_a` con `pmos_boot_v13.img`).
  4. Encender la tablet presionando el botón `POWER` durante 5 segundos y verificar ping a `172.16.42.1`.

#### 2. Carga Autónoma del Silicio Mali-G57 en el Ramdisk (`v13`)
- **Problema Resuelto en Sesión 8**: Se descubrió que el driver oficial ARM `mali_kbase_mt6789.ko` dependía de `nvmem_mtk-devinfo.ko` (`mediatek,devinfo`) para leer los fusibles efuse térmicos y de voltaje de la GPU.
- **Tarea**:
  - Incorporar de forma permanente `nvmem_mtk-devinfo.ko` en `version_v13/build_pmos_boot_v13.py`.
  - Asegurar que al arrancar, el initramfs o el servicio OpenRC inicialice la cadena de los 16 módulos de MediaTek de forma 100% desatendida, creando automáticamente `/dev/mali0`.

#### 3. Aceleración Gráfica en Espacio de Usuario (Mesa vs ARM DDK)
- **Diagnóstico Arquitectónico**:
  - El driver `mali_kbase` provee el nodo `/dev/mali0` con su protocolo ioctl propietario para el DDK ARM de Android Bionic (`libGLES_mali.so`).
  - Mesa Gallium en Alpine musl espera un nodo DRM del kernel (`/dev/dri/renderD128`).
  - El Little Kernel (LK) de MediaTek verifica firmas criptográficas de la imagen del kernel y bloquea kernels modificados con Panfrost upstream (`vmlinuz-panfrost-g57`).
- **Líneas de Acción para la Sesión 9**:
  - **Vía A (Recomendada/Práctica)**: Optimizar el renderizado CPU de Mesa con `LP_NUM_THREADS=8` y `pixman`, manteniendo 60 fps estables en Phosh mientras se investiga un puente de compatibilidad.
  - **Vía B (Aceleración Real vía DDK)**: Evaluar el puente de bibliotecas binarias Bionic (`libhybris` o shim wrapper EGL/GBM) que conecte las llamadas OpenGL ES / Vulkan de Wayland directamente al nodo `/dev/mali0`.

#### 4. Restauración del Control de Brillo de Pantalla (Slider de Phosh)
- **Problema**: El hardware regula el brillo en `/sys/class/leds/lcd-backlight/brightness` (0..255). Phosh busca en `/sys/class/backlight/*`.
- **Tarea**: Finalizar el enlace D-Bus o proxy udev para que el slider de los ajustes rápidos de Phosh ajuste el brillo del panel físico.

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
