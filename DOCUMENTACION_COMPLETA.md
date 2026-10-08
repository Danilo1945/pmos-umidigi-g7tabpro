# DOCUMENTACIÓN TÉCNICA COMPLETA Y DEFINITIVA
## UMIDIGI G7 Tab Pro (`TG2403GBA`) - SoC MediaTek Helio G99 (`MT6789`)
### postmarketOS Edge (Kernel 5.10.185-pmos aarch64 - Alpine Linux musl)

**Versión:** v13.1 Estable  
**Fecha de Actualización:** 2026-10-08  
**Estado General:** 100% Operativo (Auto-boot en frío, Phosh Wayland, Touch, Audio 4x AW87359, WiFi WMT, Batería 2A con telemetría en vivo, Botón físico de bloqueo/energía, Menú de suspensión D-Bus login1 y GPU Mali-G57 activa).

---

## 1. FICHA TÉCNICA Y HARDWARE BASE

| Componente | Especificación Técnica | Controlador / Nodo del Sistema |
| :--- | :--- | :--- |
| **SoC** | MediaTek Helio G99 (`MT6789` / `MT8781`), 6nm | Octa-Core: 2x Cortex-A76 @ 2.2 GHz + 6x Cortex-A55 @ 2.0 GHz |
| **PMIC Principal** | MediaTek MT6366 / MT6358 | Audio DAC, Fuel Gauge (`mt6358-gauge`), RTC, Buck/LDO |
| **Sub-PMIC / Cargador** | MediaTek MT6375 | USB Type-C, negociación USB-PD / PE+, carga rápida 2A |
| **Pantalla** | LCD IPS 11" (1200x1920), MIPI-DSI | `mediatek_drm` (`/dev/dri/card0`), salida `DSI-1` rotada 180° |
| **Digitalizador Táctil** | Chipone TDDI I2C (`tddi_9551`) | `/dev/input/event1` (`chipone_tddi`) y `event2` (Stylus pen) |
| **Audio Analógico** | 4x Awinic AW87359 (Clase K analógica) | Buses I2C 6 y 7 (`0x58`, `0x59`) alimentados por DAC MT6366 |
| **Wi-Fi / BT** | MediaTek MT6631 / MT6635 (802.11ac dual band) | Bus WMT / STP, interfaz `wlan0` gestionada por NetworkManager |
| **GPU 3D** | ARM Mali-G57 MC2 (Valhall v9 JM r32p1) | `/dev/mali0`, `mali_kbase_mt6789.ko`, OPP 37 (390MHz @ 575mV) |
| **Batería** | Celda Li-Po 7500 mAh (3.85V nom, 4.40V máx) | Curva OCV $R_{int}=85\text{ m}\Omega$, telemetría ADC en vivo |
| **Kernel & SO** | Kernel 5.10.185-android12-9-dirty | postmarketOS Edge (Alpine Linux aarch64 musl libc) |
| **Entorno Gráfico** | Phosh Mobile Desktop sobre Phoc compositor | Wayland / wlroots, acelerado con bypass `libseat_direct.so` |

---

## 2. ARQUITECTURA DE ARRANQUE INCONDICIONAL (AUTO-BOOT v13)

La tablet inicia en frío automáticamente en **~12 segundos** directo al entorno de escritorio Phosh sin requerir intervención manual ni comandos externos.

```
       [ POWER-ON (Silicio MT6789) ]
                     │
                     ▼
       [ Little Kernel (LK / BROM) ]
                     │
                     ▼
  [ Particiones boot_a/b + vendor_boot_a/b (v13) ]
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
     ├─ Configura memoria compartida /dev/shm en chroot
     ├─ Inicializa D-Bus System Bus (/run/dbus/system_bus_socket)
     ├─ Inicia tablet_power_daemon.py (D-Bus login1 + captura evdev)
     ├─ Inicia tablet_battery_daemon.py (telemetría ADC + UPower)
     ├─ Inicia tablet_audio_daemon.py (calibración ALSA + PipeWire)
     ├─ Espera confirmación D-Bus de org.freedesktop.login1
     ├─ Inicia teclado en pantalla squeekboard
     └─ Lanza compositor gráfico:
        LD_PRELOAD=/usr/lib/libseat_direct.so dbus-run-session \
        phoc -v -S -C /etc/phoc.ini -E "/usr/libexec/phosh -U"
```

### Imágenes de Arranque Flasheadas Persistentes
Las imágenes compiladas se encuentran grabadas de forma idéntica en ambos slots A y B para evitar cualquier caída por rollback:
- **`pmos_boot_v13.img`** (Flasheado en `/dev/sdc28` [boot_a] y `/dev/sdc51` [boot_b])  
  *SHA-256:* `bb20688ae8f55fe49d7d9a180855aea67709360ad7837032b726a348c92f9ea1`
- **`vendor_boot_pmos_v13.img`** (Flasheado en `/dev/sdc29` [vendor_boot_a] y `/dev/sdc52` [vendor_boot_b])  
  *SHA-256:* `e3f8b9f390b0ec04d1a0faf991896c26ba413b1dbc54b5fe49f7fba5419d8500`

Para regenerar las imágenes en cualquier momento:
```bash
python3 subsistemas/05_arranque_kernel/build_pmos_boot_v13.py
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
| **Pantalla e Interfaz** | ✅ **100% Operativo** | Phoc + Phosh Wayland 1200x1920 rotación 180°, escala 2, compositor acelerado con bypass `libseat_direct.so` (`DRM_IOCTL_SET_MASTER`). |
| **Touch y Entrada** | ✅ **100% Operativo** | Chipone TDDI multitáctil (`/dev/input/event1`), Stylus pen (`event2`), teclado virtual `squeekboard` auto-iniciado. |
| **Botón Power y Suspensión** | ✅ **100% Operativo** | Captura exclusiva `EVIOCGRAB` en `event3`, suspensión táctil D-Bus `login1.Manager`, reposo profundo < 80 mA y wake instantáneo a 180°. |
| **Audio y Volumen** | ✅ **100% Operativo** | ALSA MT6789 AFE + 4x amplificadores AW87359 (I2C 6/7) vía DAC MT6366, PipeWire configurado en S16LE/48kHz/RW_INTERLEAVED, ganancias nominales calibradas a 32768/12 sin siseo, control de volumen físico y OSD gráfico. |
| **Batería y Carga Rápida** | ✅ **100% Operativo** | Carga rápida 2A activa en PMIC MT6375, telemetría ADC en vivo (`BATADC`), porcentaje OCV con compensación IR calibrada para 7500mAh, rayo de carga (`battery-full-charging-symbolic`) en Phosh. |
| **Wi-Fi 802.11ac** | ✅ **100% Operativo** | Chip MT6631 inicializado en boot mediante `wmt_init_complete` sobre protocolo WMT/STP, interfaz `wlan0` gestionada automáticamente por NetworkManager. |
| **Arranque y Watchdog** | ✅ **100% Operativo** | Auto-boot v13 incondicional en 12s, memoria compartida `/dev/shm` tmpfs, watchdog de silicio alimentado cada 500ms por binario estático `wdt_pet`. |
| **GPU Mali-G57 MC2** | ✅ **100% Operativo** | Silicio Valhall activo (`/dev/mali0`), OPP 37 fijado anti-brownout (390MHz @ 575mV, 153 mW), DDK ARM r32p1 con shim de compatibilidad musl libc. |

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

### 6.3 Restauración Inmediata en Caliente (1-Click Restore)
Si en el futuro se experimenta con algún paquete o configuración y se requiere restaurar los daemons y configuraciones estables de audio, pantalla, batería y Wi-Fi en caliente:
```bash
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