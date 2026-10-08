# Repositorio Maestro de Funcionalidades Finales y Versionamiento
## UMIDIGI G7 Tab Pro (`TG2403GBA`) - SoC MediaTek Helio G99 (`MT6789`)
### postmarketOS Edge (Alpine Linux aarch64 - Kernel 5.10.185-pmos)

**Versión:** v15.0 Estable | **Documentación Técnica Principal:** [`DOCUMENTACION_COMPLETA.md`](./DOCUMENTACION_COMPLETA.md)

---

## 📌 Propósito de este Repositorio
Este repositorio consolida **todas las funcionalidades estables y probadas en hardware real** de la tablet UMIDIGI G7 Tab Pro:
- Auto-boot incondicional en frío a escritorio Phosh en ~12 segundos.
- Control de energía por botón físico Power y menú táctil "Suspender" vía D-Bus `login1`.
- Bloqueo de pantalla (Lockscreen) protegido con PIN `1234`.
- Pantalla rotada 180° y táctil fluido con bypass `libseat_direct.so`.
- Cuádruple amplificador inteligente de audio AW87359 con PipeWire.
- Carga rápida 2A en PMIC MT6375 con telemetría OCV e indicador en Phosh.
- Conectividad Wi-Fi 802.11ac gestionada por NetworkManager.
- Silicio GPU ARM Mali-G57 MC2 activo con frecuencia segura anti-brownout (OPP 37).

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
│   │   ├── tablet_audio_daemon.py     # Demonio de calibración ALSA y control de ganancias
│   │   ├── 10-alsa.conf               # Configuración PipeWire S16LE / 48kHz / disable-mmap
│   │   ├── 50-alsa-fix.conf           # Configuración WirePlumber
│   │   ├── volume_keys_daemon.py      # Captura de teclas físicas de volumen
│   │   └── README_AUDIO.md            # Documentación exhaustiva del subsistema de audio
│   │
│   ├── 02_bateria_energia/            # 🔋 Subsistema de Batería PMIC MT6366 / MT6375
│   │   ├── tablet_battery_daemon.py   # Demonio de monitoreo OCV + telemetría ADC en vivo
│   │   ├── battery_daemon.c           # Fuente en C de alta eficiencia
│   │   ├── battery_table.h            # Tabla OCV calibrada para celda 7500mAh
│   │   └── README_BATERIA.md          # Documentación técnica de energía y carga rápida
│   │
│   ├── 03_pantalla_rotacion_touch/    # 🖥️ Pantalla DSI, Touch, Phosh y Gestión de Energía
│   │   ├── tablet_power_daemon.py     # ⚡ Demonio de botón Power, D-Bus login1, reposo y volumen
│   │   ├── libseat_direct.c           # Shim en C con soporte DRM_IOCTL_SET_MASTER
│   │   ├── libseat_direct.so          # Biblioteca compartida compilada AArch64
│   │   ├── phoc.ini                   # Geometría, rotación 180° y escala de compositor
│   │   ├── start_phosh.sh             # Script integral de arranque de sesión Wayland/Phosh
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
│   │   ├── build_pmos_boot_v13.py     # Empaquetador de imágenes boot.img y vendor_boot.img
│   │   ├── flash_pmos_v13_mtkclient.sh# Flasheador BROM / Preloader automático
│   │   └── README_BOOT.md             # Documentación de particiones y watchdog
│   │
│   └── 06_aceleracion_grafica_gpu/    # ⚡ GPU ARM Mali-G57 MC2 (Valhall v9) - Aceleración 3D HW
│       ├── README.md                  # Documentación técnica de arquitectura DDK y parches
│       ├── libmali_shim.c             # Shim C de compatibilidad musl/glibc
│       ├── load_mali_chain.sh         # Carga de la cadena devlink de 16 módulos
│       └── test_mali_render.py        # Test standalone de renderizado EGL 1.5 + GLES 3.2
│
├── firmware/                          # 💾 Firmwares y microcódigos esenciales (AW87xxx, TDDI, WMT)
└── tools/                             # 🛠️ Herramientas de soporte y diagnóstico
    ├── tablet_power.py                # Monitor y control de energía (status, sleep, wake)
    ├── tablet_cmd.py                  # Shell interactiva remota por socket 2323
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
| **Auto-boot** | Initramfs v13 + tmpfs `/dev/shm` | ✅ **100% Funcional** | Arranque en frío en ~12 segundos directo a interfaz Phosh sin intervención manual. |
| **Botón Power y Suspensión**| `tablet_power_daemon.py` | ✅ **100% Funcional** | Captura `EVIOCGRAB` en `event3`, suspensión táctil vía D-Bus `org.freedesktop.login1`, reposo profundo (< 80 mA) y wake inmediato a 180°. |
| **Audio** | ALSA MT6789 AFE + 4x AW87359 | ✅ **100% Funcional** | Sonido nítido en 4 altavoces, volumen calibrado sin distorsión ni siseo, control de volumen físico y OSD. |
| **Batería** | PMIC MT6366 + MT6375 chg | ✅ **100% Funcional** | Carga rápida a 2A, curva OCV 7500mAh con compensación IR ($R_{int}=85\text{ m}\Omega$), telemetría ADC y rayo de carga en Phosh. |
| **Pantalla y Touch** | MIPI-DSI 1200x1920 + TDDI | ✅ **100% Funcional** | Sesión GNOME/Phosh acelerada con `libseat_direct.so`, rotación fija 180° y táctil multitouch fluido. |
| **Wi-Fi** | MediaTek MT6631 / WMT | ✅ **100% Funcional** | Interfaz `wlan0` activa y gestionada por NetworkManager en cada arranque. |
| **Kernel / Boot**| Boot v4 + musb Gadget + WDT | ✅ **100% Funcional** | Kernel 5.10.185-pmos estable con `wdt_pet` (sin reinicios watchdog), red fija `172.16.42.1`. |
| **Aceleración GPU**| ARM Mali-G57 MC2 (Valhall) | ✅ **100% Funcional** | Silicio activo (`/dev/mali0`), OPP 37 anti-brownout (390 MHz @ 575 mV, 153 mW), 66 configs EGL nativas, `kmscube` a 60 fps. |

---

## 🛡️ Política de Versionamiento
- Cualquier experimento futuro debe realizarse en una rama separada para mantener protegida la rama `master`.
- Ante cualquier eventualidad, `python3 tools/restore_live_tablet.py` restaura la tablet al estado 100% operativo en caliente.
