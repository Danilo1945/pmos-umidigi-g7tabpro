# DOCUMENTACIÓN TÉCNICA COMPLETA Y DEFINITIVA
## UMIDIGI G7 Tab Pro (`TG2403GBA`) - SoC MediaTek Helio G99 (`MT6789`)
### postmarketOS Edge (Kernel 5.10.185-pmos aarch64 - Alpine Linux musl)

**Versión:** v16.0 Master Unificada  
**Fecha de Actualización:** 2026-11-05  
**Estado General:** 100% Operativo (Auto-boot en frío, Phosh Wayland acelerado por hardware GPU Mali-G57 MC2 a 60 FPS (`WLR_RENDERER=gles2`, `GSK_RENDERER=gl`, `libgpu_interface_driver.so`), Touch 1:1 `tddi_9551_patched.ko`, Audio 4x AW87359 + Jack 3.5mm, WiFi WMT, Batería 2A con telemetría UPower en vivo, Botón físico de bloqueo/energía y Menú de suspensión D-Bus login1).

---

## 1. FICHA TÉCNICA Y HARDWARE BASE

| Componente | Especificación Técnica | Controlador / Nodo del Sistema |
| :--- | :--- | :--- |
| **SoC** | MediaTek Helio G99 (`MT6789` / `MT8781`), 6nm | Octa-Core: 2x Cortex-A76 @ 2.2 GHz + 6x Cortex-A55 @ 2.0 GHz |
| **PMIC Principal** | MediaTek MT6366 / MT6358 | Audio DAC, Fuel Gauge (`mt6358-gauge`), RTC, Buck/LDO |
| **Sub-PMIC / Cargador** | MediaTek MT6375 | USB Type-C, negociación USB-PD / PE+, carga rápida 2A |
| **Pantalla** | LCD IPS 11" (1200x1920), MIPI-DSI | `mediatek_drm` (`/dev/dri/card0`), salida `DSI-1` rotada 180° |
| **Digitalizador Táctil** | Chipone TDDI I2C (`tddi_9551_patched.ko`) | `/dev/input/event1` (`chipone_tddi`, matriz `-1 0 1 0 -1 1`) y `event2` (Stylus pen) |
| **Audio Analógico** | 4x Awinic AW87359 (Clase K analógica) | Buses I2C 6 y 7 (`0x58`, `0x59`) alimentados por DAC MT6366 |
| **Wi-Fi / BT** | MediaTek MT6631 / MT6635 (802.11ac dual band) | Bus WMT / STP, interfaz `wlan0` gestionada por NetworkManager |
| **GPU 3D** | ARM Mali-G57 MC2 (Valhall v9 JM r32p1) | `/dev/mali0`, `mali_kbase_mt6789_patched.ko`, `libgpu_interface_driver.so`, OPP 15 (812MHz) / OPP 37 (390MHz) |
| **Batería** | Celda Li-Po 7500 mAh (3.85V nom, 4.40V máx) | Curva OCV $R_{int}=85\text{ m}\Omega$, telemetría ADC en vivo + `upower_battery_provider.py` |
| **Kernel & SO** | Kernel 5.10.185-android12-9-dirty | postmarketOS Edge (Alpine Linux aarch64 musl libc) |
| **Entorno Gráfico** | Phosh Mobile Desktop sobre Phoc compositor | Wayland / wlroots (`WLR_RENDERER=gles2`, `GSK_RENDERER=gl`, SIMD NEON x8) |

---

## 2. ARQUITECTURA DE ARRANQUE INCONDICIONAL (AUTO-BOOT v15)

La tablet inicia en frío automáticamente en **~12 segundos** directo al entorno de escritorio Phosh sin requerir intervención manual ni comandos externos.

```
       [ POWER-ON (Silicio MT6789) ]
                     │
                     ▼
       [ Little Kernel (LK / BROM) ]
                     │
                     ▼
  [ Particiones boot_a/b + vendor_boot_a/b (v15) ]
                     │
                     ▼
 [ Host Initramfs: Busybox ash /init (PID 1) ]
   ├─ Inicia Watchdog de silicio: /bin/wdt_pet (latido cada 500ms)
   ├─ Monta /dev/shm como tmpfs (128M mode 1777)
   ├─ Configura USB Gadget CDC-NCM (172.16.42.1)
   ├─ Carga módulos de kernel (WiFi WMT, Audio, Pantalla, Mali GPU)
   ├─ Monta partición rootfs en /sysroot
   ├─ Sincroniza scripts maestros a /sysroot/bin/
   └─ Ejecuta: /bin/start_phosh_service.sh
                     │
                     ▼
   [ Entorno Chroot /sysroot: start_phosh.sh ]
     ├─ Configura memoria compartida /dev/shm y carga cadena Mali GPU (812 MHz)
     ├─ Inicializa D-Bus System Bus (/run/dbus/system_bus_socket)
     ├─ Inicia wmt_wifi_start.sh + wpa_supplicant + NetworkManager
     ├─ Inicia upower_battery_provider.py + tablet_battery_daemon.py
     ├─ Inicia pipewire + wireplumber + pipewire-pulse + tablet_audio_daemon.py
     ├─ Inicia tablet_power_daemon.py (D-Bus login1 + captura evdev + brillo seguro)
     ├─ Espera confirmación D-Bus de org.freedesktop.login1
     └─ Lanza compositor gráfico supervisado:
        WLR_RENDERER=gles2 \
        LD_PRELOAD="/usr/lib/libjemalloc.so.2 /usr/lib/libseat_direct.so /usr/lib/libgpu_interface_driver.so" \
        dbus-run-session phoc -C /etc/phoc.ini -E "/usr/bin/phosh_wrapper.sh"
        (phosh_wrapper.sh exporta GSK_RENDERER=gl, activa animaciones y lanza squeekboard + phosh)
```

### Imágenes de Arranque Flasheadas Persistentes
Las imágenes compiladas se encuentran grabadas de forma idéntica en ambos slots A y B para evitar cualquier caída por rollback:
- **`pmos_boot_v15.img`** (Flasheado en `/dev/sdc28` [boot_a] y `/dev/sdc51` [boot_b])  
  *MD5:* `94c30b53a81497230bf510862905f692`  
  *SHA-256:* `d2562264a7b2fc363d179093f5578bd3a4b4177caf75091c2e214272dc605eb7`
- **`vendor_boot_pmos_v15.img`** (Flasheado en `/dev/sdc29` [vendor_boot_a] y `/dev/sdc52` [vendor_boot_b])  
  *MD5:* `3598d7eb387379b8f4abf8c3247646b6`  
  *SHA-256:* `9d7fcee90b3f3ecadde36a522d15aeac0135d26f047b83cd463aa65189ef8dee`
- **Respaldo de Configuración Rootfs:** `sysroot_v15_config_backup.tar.gz` (201.5 MB) en `version_v15_estable/`

Para regenerar las imágenes en cualquier momento:
```bash
python3 subsistemas/05_arranque_kernel/build_pmos_boot_v15.py
```

---

## 3. GESTIÓN DE ENERGÍA, BOTÓN FÍSICO POWER Y SUSPENSIÓN D-BUS

El control de energía del sistema está centralizado en el demonio de hardware de alta prioridad:  
`/sysroot/usr/bin/tablet_power_daemon.py` (también versionado en `subsistemas/03_pantalla_rotacion_touch/tablet_power_daemon.py`).

### 3.1 Captura Exclusiva de Eventos de Hardware (`EVIOCGRAB`)
- **`/dev/input/event3` (`mtk-pmic-keys`)**: Captura exclusiva de la tecla física de encendido (`KEY_POWER`). Evita que Phoc o libinput capturen el evento y apaguen la salida DRM de forma irreversible sin poder encenderla.
- **`/dev/input/event0` (`mtk-kpd`)**: Captura exclusiva de teclas físicas de volumen (`KEY_VOLUMEUP`, `KEY_VOLUMEDOWN`).

### 3.2 Interfaces D-Bus Exportadas por el Demonio
1. **`org.freedesktop.login1.Manager` (System Bus)**:
   - **Métodos**: `Suspend(b)`, `CanSuspend() -> 'yes'`, `PowerOff(b)`, `CanPowerOff() -> 'yes'`, `Reboot(b)`, `CanReboot() -> 'yes'`.
   - **Señales**: `PrepareForSleep(b)` (emitida con `true` al entrar en suspensión y con `false` al despertar), `PrepareForShutdown(b)`.
   - **Propiedades**: `PreparingForSleep`, `PreparingForShutdown`.
   - **Sesión**: `org.freedesktop.login1.Session` con método `SetBrightness(s, s, u)`.
2. **`org.gnome.SessionManager` (Session Bus)**:
   - **Métodos**: `Shutdown()`, `Reboot()`, `CanShutdown() -> true`, `IsSessionRunning() -> true`, `Logout()`.
3. **`net.hadess.SensorProxy` (System Bus)**:
   - Emula el proxy de acelerómetro para auto-rotación en Phosh Quick Settings (Portrait 180° / Landscape).

### 3.3 Ciclos de Reposo y Despertar

#### A. Ciclo de Suspensión / Reposo Profundo (Botón Físico o Menú Táctil "Suspender")
1. Se emite la señal D-Bus `PrepareForSleep(true)` a Phosh.
2. Se bloquea la sesión gráfica mediante `org.gnome.ScreenSaver.Lock()`.
3. Se apaga la retroiluminación a 0 nits (`/sys/class/leds/lcd-backlight/brightness = 0`).
4. Se conmutan los gobernadores de todas las CPUs (Cores 0 a 7) a `powersave`.
5. El consumo total de corriente cae a menos de **80 mA**.

#### B. Ciclo de Despertar (Pulsación del Botón Físico Power)
1. Se conmutan los gobernadores de CPU a `schedutil` (rendimiento nominal instantáneo).
2. Se fuerza la reactivación de la salida DRM DSI-1 con rotación garantizada a 180°:
   ```bash
   wlr-randr --output DSI-1 --on --transform 180
   ```
3. Se restaura el brillo de la pantalla a 200 nits.
4. Se despierta la pantalla de bloqueo con `org.gnome.ScreenSaver.SetActive(false)`.
5. Se emite la señal D-Bus `PrepareForSleep(false)`.
6. La pantalla se ilumina instantáneamente con el reloj y el widget "Desliza para desbloquear".

---

## 4. ESTADO TÉCNICO Y OPERATIVO POR SUBSISTEMA

| Subsistema | Estado | Implementación y Verificación |
| :--- | :--- | :--- |
| **Pantalla e Interfaz** | ✅ **100% Operativo** | Phoc (`WLR_RENDERER=gles2`) + Phosh (`GSK_RENDERER=gl`) 1200x1920 rotación 180°, acelerado con `libseat_direct.so` y `libgpu_interface_driver.so` (SIMD ARM NEON x8 a 60 FPS). |
| **Touch y Entrada** | ✅ **100% Operativo** | Chipone TDDI multitáctil (`tddi_9551_patched.ko`, matriz 1:1 `-1 0 1 0 -1 1`), Stylus pen (`event2`), teclado virtual `squeekboard` auto-iniciado. |
| **Botón Power y Suspensión** | ✅ **100% Operativo** | Captura exclusiva `EVIOCGRAB` en teclas físicas, suspensión D-Bus `login1.Manager`, escalado dinámico CPU/GPU, protección de brillo `[15, 100]` / `0` y wake instantáneo a 180°. |
| **Audio y Volumen** | ✅ **100% Operativo** | ALSA MT6789 AFE + 4x amplificadores AW87359 (I2C 6/7) vía DAC MT6366, detección de Jack 3.5mm, PipeWire configurado en S16LE/48kHz/RW_INTERLEAVED, control de volumen físico y OSD gráfico. |
| **Batería y Carga Rápida** | ✅ **100% Operativo** | Carga rápida 2A activa en PMIC MT6375, telemetría ADC en vivo (`BATADC`), porcentaje OCV con compensación IR calibrada para 7500mAh, servicio `upower_battery_provider.py` en Phosh. |
| **Wi-Fi 802.11ac** | ✅ **100% Operativo** | Chip MT6631 inicializado en boot mediante `wmt_init_complete` sobre protocolo WMT/STP, interfaz `wlan0` gestionada automáticamente por `wpa_supplicant` + NetworkManager. |
| **Arranque y Watchdog** | ✅ **100% Operativo** | Auto-boot v15/v16 incondicional en 12s, memoria compartida `/dev/shm` tmpfs, watchdog de silicio alimentado cada 500ms por binario estático `wdt_pet`. |
| **GPU Mali-G57 MC2** | ✅ **100% Operativo** | Silicio Valhall activo (`/dev/mali0`, `mali_kbase_mt6789_patched.ko`), OPP 15 (812 MHz) en uso activo y OPP 37 (390 MHz) en reposo, DDK ARM r32p1 + `libgpu_interface_driver.so`. |

---

## 5. CREDENCIALES Y ACCESO AL SISTEMA

- **Contraseña Unificada de Sistema:** **`1234`** (fijada tanto para usuario `danilo` como para `root`).
- **Desbloqueo de Pantalla (Phosh Lockscreen):** Deslizar hacia arriba e ingresar PIN **`1234`**.
- **Acceso por Red USB Gadget (CDC-NCM):**
  - IP de la tablet: **`172.16.42.1`**
  - IP del PC anfitrión: `172.16.42.2` (asignada por DHCP automático)
- **Conexión SSH:**
  ```bash
  ssh root@172.16.42.1
  # Contraseña: 1234
  ```
- **Shell de Control Rápido por Socket TCP (Puerto 2323):**
  ```bash
  python3 tools/tablet_cmd.py "<comando>"
  ```

---

## 6. COMANDOS OPERATIVOS Y HERRAMIENTAS CLI

### 6.1 Telemetría y Control de Energía (`tools/tablet_power.py`)
```bash
# Ver estado en tiempo real (batería, brillo, GPU, Phosh):
python3 tools/tablet_power.py status

# Poner la tablet en reposo profundo (apagar pantalla, CPUs a powersave):
python3 tools/tablet_power.py sleep

# Despertar la tablet (encender pantalla a 180°, CPUs a schedutil):
python3 tools/tablet_power.py wake
```

### 6.2 Comandos Útiles del Sistema (Vía `tablet_cmd.py` o SSH)
```bash
# Comprobar procesos activos de interfaz y servicios:
python3 tools/tablet_cmd.py "ps aux | grep -E 'phoc|phosh|pipewire|tablet_|wmt' | grep -v grep"

# Ver telemetría de batería en milivoltios:
python3 tools/tablet_cmd.py "cat /sys/devices/platform/soc/10026000.pwrap/10026000.pwrap:mt6366/mt6358-gauge/in_voltage0_BATADC_input"

# Ver estado y conexión de Wi-Fi:
python3 tools/tablet_cmd.py "chroot /sysroot /usr/bin/nmcli -t -f GENERAL.STATE,GENERAL.CONNECTION device show wlan0"

# Escanear y conectar a una red Wi-Fi:
python3 tools/tablet_cmd.py "chroot /sysroot /usr/bin/nmcli device wifi connect 'NOMBRE_RED' password 'CLAVE'"

# Reiniciar la tablet de forma limpia:
python3 tools/tablet_cmd.py "sync; reboot -f"
```

### 6.3 Restauración Inmediata y Punto de Retorno (1-Click Restore v15.0)
Si en el futuro se experimenta con algún paquete, kernel o configuración y se requiere restaurar la tablet completa (imágenes de arranque + estado y configuraciones del sistema en `/sysroot`):
```bash
# Restauración completa (flasheo de boot v15 + restauración de configs de sysroot):
python3 tools/restaurar_tablet_v15.py

# Restauración rápida en caliente solo de daemons:
python3 tools/restore_live_tablet.py
```

---

## 7. ESTRUCTURA CANÓNICA DEL REPOSITORIO

```
FUNCIONALIDADES_FINALES/
├── build_all.sh                       # Script de compilación de todos los binarios C
├── README.md                          # Guía rápida ejecutiva del repositorio
├── DOCUMENTACION_COMPLETA.md          # [ESTE ARCHIVO] Especificación técnica maestra
│
├── subsistemas/
│   ├── 01_audio/                      # Audio ALSA MT6789 AFE + 4x AW87359 + PipeWire
│   │   └── README_AUDIO.md            # Documentación exhaustiva de audio y ganancias
│   ├── 02_bateria_energia/            # Batería PMIC MT6366 / MT6375 y telemetría ADC
│   │   └── README_BATERIA.md          # Manual de curva OCV y compensación IR
│   ├── 03_pantalla_rotacion_touch/    # Pantalla DSI, Phoc/Phosh, libseat_direct y Power Daemon
│   │   ├── tablet_power_daemon.py     # Demonio maestro de botón Power, D-Bus login1 y volumen
│   │   ├── README_PANTALLA.md         # Documentación de DRM, libseat y gestión de energía
│   │   └── INFORME_GPU_HARDWARE.md    # Estudio técnico de ingeniería inversa de GPU Mali
│   ├── 04_wifi_conectividad/          # Wi-Fi 802.11ac MediaTek MT6631 / WMT
│   │   └── README_WIFI.md             # Documentación de protocolo STP y bus WMT
│   ├── 05_arranque_kernel/            # Arranque Android v4, Initramfs y Watchdog
│   │   ├── build_pmos_boot_v13.py     # Empaquetador de imágenes de arranque v13
│   │   └── README_BOOT.md             # Documentación de particiones y wdt_pet
│   └── 06_aceleracion_grafica_gpu/    # GPU ARM Mali-G57 MC2 Valhall
│       └── README.md                  # Documentación completa de parches y DDK Valhall
│
├── firmware/                          # Microcódigos binarios validados (AW87xxx, TDDI, WMT)
└── tools/                             # Herramientas de diagnóstico y control remoto (CLI)
    ├── tablet_cmd.py                  # Shell interactiva por socket 2323
    ├── tablet_power.py                # Monitor y controlador de energía / sleep / wake
    └── restore_live_tablet.py         # Restaurador automático 1-click
```

---
*Documentación técnica consolidada, determinista y verificada en hardware real.*