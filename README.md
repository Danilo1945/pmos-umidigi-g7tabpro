# pmos-umidigi-g7tabpro
## postmarketOS & Linux Bringup para UMIDIGI G7 Tab Pro (`TG2403GBA`)
### SoC MediaTek Helio G99 (`MT6789`) | postmarketOS Edge (Alpine Linux aarch64 - Kernel 5.10.185-pmos)

[![postmarketOS](https://img.shields.io/badge/postmarketOS-Edge-green.svg)](https://postmarketos.org/)
[![SoC](https://img.shields.io/badge/SoC-MediaTek%20Helio%20G99%20(MT6789)-orange.svg)](https://www.mediatek.com/)
[![GPU](https://img.shields.io/badge/GPU-Mali--G57%20MC2%20(Acelerada)-blue.svg)]()
[![VPU](https://img.shields.io/badge/VPU%2FVDEC-H.264%2FHEVC%2FVP9%20Hardware-brightgreen.svg)]()
[![Battery](https://img.shields.io/badge/Power-Android--Style%20Deep%20Standby-purple.svg)]()
[![i18n](https://img.shields.io/badge/i18n-Espa%C3%B1ol%20%2F%20English-yellow.svg)]()

**Versión:** v16.0 Master Unificada (GPU Wayland + Hardware VDEC + Gestión de Energía Avanzada) | **Documentación Técnica Principal:** [`DOCUMENTACION_COMPLETA.md`](./DOCUMENTACION_COMPLETA.md)

---

## 📌 Propósito de este Repositorio
Este repositorio consolida **todas las funcionalidades estables y probadas en silicio real** de la tablet UMIDIGI G7 Tab Pro:
- **Auto-boot incondicional en frío** a escritorio Phosh en ~12 segundos.
- **Interfaz Wayland/Phosh acelerada al 100% por hardware GPU ARM Mali-G57 MC2** (`WLR_RENDERER=gles2`, `libgpu_interface_driver.so`, SIMD ARM NEON x8, 60 FPS fluidos).
- **Aceleración por Hardware de Video (VPU/VDEC `/dev/video0`):** Driver MediaTek VDEC reconstruido mediante ingeniería inversa, con soporte nativo V4L2 M2M para decodificación fluida 1080p/4K de **H.264, HEVC (H.265), VP9 y VP8** en Firefox, MPV y Chromium.
- **Gestión de Energía Estilo Android (Deep Standby):**
  - Gobernador inteligente **`schedutil`** (EAS) en uso activo (escala de 500 MHz a 2.2 GHz).
  - Escalado dinámico devfreq de la GPU Mali (`simple_ondemand`, 390 MHz a 812 MHz).
  - Reposo profundo con **CPU Hotplug**: apaga 7 de los 8 núcleos de CPU en reposo para eliminar el consumo estático, congelador de apps de fondo (`killall -STOP`) y suspensión de pantalla a 0 nits.
  - Botón de **Suspender / Reposo** habilitado directamente en el panel de control de Phosh y vinculado al botón físico de encendido.
- **Soporte Bilingüe Completo (Español / Inglés):** Selector de idioma del sistema operativo (`set_language es/en`, `toggle_language`), teclado táctil Squeekboard con botón de alternancia rápida entre distribución en Español (`ñ`) e Inglés (US), y traducciones completas en Phosh y GTK.
- **Pantalla y Táctil:** Novatek NT36672E a 1200x1920 @ 60Hz rotada a 180° y táctil multitouch 1:1 de precisión con `tddi_9551_patched.ko` y `libseat_direct.so`.
- **Audio Cuádruple:** Amplificadores inteligentes AW87359 con enrutamiento ALSA/PipeWire y detección de Jack 3.5mm.
- **Batería y Carga Rápida:** Carga a 2A en PMIC MT6375 con telemetría OCV e indicador UPower D-Bus nativo en Phosh.
- **Conectividad:** Wi-Fi 802.11ac y Bluetooth gestionados por NetworkManager y WMT.

Sirve como **código fuente canónico y punto de restauración determinista**: si en el desarrollo futuro se altera alguna configuración, cualquier subsistema puede restaurarse instantáneamente con un comando.

---

## 🗂️ Arquitectura del Repositorio

```
FUNCIONALIDADES_FINALES/
├── build_all.sh                       # Script maestro de compilación de todos los binarios C
├── README.md                          # Este documento ejecutivo
├── DOCUMENTACION_COMPLETA.md          # 📖 Manual técnico maestro y especificación exhaustiva
│
├── subsistemas/
│   ├── 01_audio/                      # 🔊 Subsistema de Audio MT6789 AFE + 4x AW87359
│   │   ├── tablet_audio_daemon.py     # Demonio de enrutamiento ALSA, mute HW y Jack 3.5mm
│   │   ├── 10-alsa.conf               # Configuración PipeWire S16LE / 48kHz / disable-mmap
│   │   ├── 50-alsa-fix.conf           # Configuración WirePlumber
│   │   ├── volume_keys_daemon.py      # Utilidad auxiliar de teclas de volumen
│   │   └── README_AUDIO.md            # Documentación exhaustiva del subsistema de audio
│   │
│   ├── 02_bateria_energia/            # 🔋 Subsistema de Batería PMIC MT6366 / MT6375
│   │   ├── upower_battery_provider.py # Proveedor D-Bus UPower nativo para Phosh
│   │   ├── tablet_battery_daemon.py   # Demonio de monitoreo OCV + telemetría ADC en vivo
│   │   ├── battery_daemon.c           # Fuente en C de alta eficiencia
│   │   ├── battery_table.h            # Tabla OCV calibrada para celda 7500mAh
│   │   └── README_BATERIA.md          # Documentación técnica de energía y carga rápida
│   │
│   ├── 03_pantalla_rotacion_touch/    # 🖥️ Pantalla DSI, Touch, Phosh y Gestión de Energía
│   │   ├── tablet_power_daemon.py     # ⚡ Demonio unificado Power, D-Bus login1, s2idle, rotación, brillo y volumen
│   │   ├── phosh_backlight_shim.c     # Shim libgudev en espacio de usuario para slider de brillo en Phosh
│   │   ├── libphosh_backlight_shim.so # Biblioteca compartida AArch64 inyectada en Phosh
│   │   ├── libseat_direct.c           # Shim en C con soporte DRM_IOCTL_SET_MASTER
│   │   ├── libseat_direct.so          # Biblioteca compartida compilada AArch64
│   │   ├── tddi_9551_patched.ko       # Driver táctil Chipone TDDI parcheado (sin colisión GPIO)
│   │   ├── phoc.ini                   # Geometría, rotación 180° y escala de compositor
│   │   ├── phosh_wrapper.sh           # Lanzador Phosh con GSK_RENDERER=gl, animaciones y squeekboard
│   │   ├── start_phosh.sh             # Script integral unificado de arranque de sesión y demonios
│   │   ├── README_PANTALLA.md         # Documentación de DRM/KMS, suspensión y bloqueo
│   │   └── INFORME_GPU_HARDWARE.md    # Estudio técnico de ingeniería inversa de GPU Mali
│   │
│   ├── 04_wifi_conectividad/          # 📶 Wi-Fi 802.11ac MediaTek MT6631 / WMT
│   │   ├── wmt_init_complete.c        # Fuente en C de inicialización WMT
│   │   ├── wmt_wifi_start.sh          # Secuencia de encendido y registro udev/NM
│   │   └── README_WIFI.md             # Documentación de protocolo STP/WMT
│   │
│   ├── 05_arranque_kernel/            # 🚀 Arranque Android v4, Kernel y Watchdog
│   │   ├── wdt_pet.c                  # Fuente en C del alimentador de watchdog de silicio
│   │   ├── build_pmos_boot_v15.py     # Empaquetador maestro de imágenes boot.img y vendor_boot.img
│   │   ├── flash_pmos_v13_mtkclient.sh# Flasheador BROM / Preloader automático
│   │   └── README_BOOT.md             # Documentación de particiones y watchdog
│   │
│   └── 06_aceleracion_grafica_gpu/    # ⚡ GPU ARM Mali-G57 MC2 (Valhall v9) - Aceleración Wayland HW
│       ├── README.md                  # Documentación técnica de arquitectura DDK y parches
│       ├── gpu_interface_driver.c     # Driver puente GPU FBO <-> KMS Dumb Buffer (SIMD NEON x8)
│       ├── libgpu_interface_driver.so # Librería compartida AArch64 inyectada en Phoc
│       ├── mali_kbase_mt6789_patched.ko# Driver kernel Mali parcheado (OPP 15 @ 812 MHz / OPP 37)
│       ├── libmali_shim.c             # Shim C de compatibilidad musl/glibc
│       ├── load_mali_chain.sh         # Carga de la cadena devlink de 16 módulos
│       └── tests/                     # Suite de pruebas unitarias de renderizado e inspección KMS
│
├── firmware/                          # 💾 Firmwares y microcódigos esenciales (AW87xxx, TDDI, WMT)
└── tools/                             # 🛠️ Herramientas de soporte y diagnóstico
    ├── tablet_power.py                # Monitor y control de energía (status, sleep, wake)
    ├── tablet_cmd.py                  # Shell interactiva remota por socket 2323
    ├── telnet_cmd.py                  # Cliente auxiliar de comandos por telnet
    └── restore_live_tablet.py         # Restaurador automático 1-click hacia la tablet viva
```

---

## 🔑 Credenciales y Conexión

| Acceso | Detalle |
| :--- | :--- |
| **Usuario estándar** | `danilo` (contraseña: **`1234`**) |
| **Superusuario** | `root` (contraseña: **`1234`**) |
| **PIN Desbloqueo Phosh** | **`1234`** (deslizar pantalla hacia arriba e ingresar código) |
| **Red USB Gadget** | IP tablet: `172.16.42.1` \| IP host PC: `172.16.42.2` |
| **Acceso SSH** | `ssh root@172.16.42.1` |
| **Shell Rápida CLI** | `python3 tools/tablet_cmd.py "<comando>"` |

---

## 🚀 Guía Rápida de Operaciones

### 1. Control de Energía y Estado en Tiempo Real
```bash
# Ver estado del sistema (batería, brillo, GPU, interfaz):
python3 tools/tablet_power.py status

# Poner en reposo profundo (apagar pantalla, CPUs a powersave):
python3 tools/tablet_power.py sleep

# Despertar (encender pantalla a 180°, CPUs a schedutil):
python3 tools/tablet_power.py wake
```

### 2. Compilar Todos los Binarios C desde Cero
```bash
./build_all.sh
```

### 3. Restaurar la Tablet en Vivo con un Solo Comando (1-Click Restore)
Con la tablet encendida y conectada vía USB (`172.16.42.1`):
```bash
python3 tools/restore_live_tablet.py
```

### 4. Probar Aceleración 3D por Hardware (kmscube)
```bash
LD_PRELOAD="/usr/lib/libmali-valhall-g57-wayland.so /usr/lib/libmali_shim.so" kmscube -c 60
```

---

## 📊 Matriz de Estado y Funcionalidades Validadas

| Subsistema | Componente Clave | Estado | Verificación Técnica |
| :--- | :--- | :--- | :--- |
| **Auto-boot** | Initramfs v15/v16 + tmpfs `/dev/shm` | ✅ **100% Funcional** | Arranque en frío en ~12 segundos directo a interfaz Phosh acelerada por GPU sin intervención manual. |
| **Botón Power y Suspensión**| `tablet_power_daemon.py` + `libphosh_backlight_shim.so` | ✅ **100% Funcional** | Suspensión real `s2idle` (`/sys/power/state = mem`) con apagado de `DSI-1`/Wi-Fi/BT, brillo completo `[10, 255]` con slider nativo en Phosh, `reboot -f`/`poweroff -f` y wake inmediato por `KEY_POWER`. |
| **Audio** | ALSA MT6789 AFE + 4x AW87359 | ✅ **100% Funcional** | Sonido nítido en 4 altavoces, detección Jack 3.5mm, volumen sincronizado 1:1 vía `pactl` (escala cúbica PulseAudio/Phosh) y control físico con OSD. |
| **Batería** | PMIC MT6366 + MT6375 chg | ✅ **100% Funcional** | Carga rápida a 2A, curva OCV 7500mAh con compensación IR ($R_{int}=85\text{ m}\Omega$), `is_full_hw` validado (`>= 4320 mV`) y servicio `upower_battery_provider.py` en Phosh. |
| **Pantalla, Rotación y Touch** | MIPI-DSI 1200x1920 + TDDI + `gpu_interface_driver.c` | ✅ **100% Funcional** | Sesión GNOME/Phosh en español (`es_EC.UTF-8`), rotación vertical (`180°`) y horizontal (`90°`) con mapeo afín 2D sin distorsión, `tddi_9551_patched.ko` y `gsd-rfkill` para Bluetooth UI. |
| **Wi-Fi, BT y Reloj NTP** | MediaTek MT6631 / WMT + `chronyd` | ✅ **100% Funcional** | `wlan0` (NetworkManager), Bluetooth (`tablet_bt_daemon.py` + `gsd-rfkill`) y sincronización automática de hora por NTP (`chronyd`) con respaldo `/var/lib/last_valid_time`. |
| **Kernel / Boot**| Boot v4 + musb Gadget + WDT | ✅ **100% Funcional** | Kernel 5.10.185-pmos estable con `wdt_pet` (sin reinicios watchdog), red fija `172.16.42.1`. |
| **Aceleración GPU**| ARM Mali-G57 MC2 + `libgpu_interface_driver.so` | ✅ **100% Funcional** | Compositor Phoc (`WLR_RENDERER=gles2`) y apps GTK4 (`GSK_RENDERER=gl`) acelerados por hardware a 60 FPS (812 MHz OPP 15 en uso, SIMD ARM NEON x8). |

---

## 🛡️ Política de Versionamiento
- Cualquier experimento futuro debe realizarse en una rama separada para mantener protegida la rama `master`.
- Ante cualquier eventualidad, `python3 tools/restore_live_tablet.py` restaura la tablet al estado 100% operativo en caliente.
