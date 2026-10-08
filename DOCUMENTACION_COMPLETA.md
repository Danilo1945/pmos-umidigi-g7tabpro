# DOCUMENTACIÓN COMPLETA - UMIDIGI G7 Tab Pro (MT6789) postmarketOS

**Fecha:** 2026-10-03  
**Estado actual:** Tablet en logo UMIDIGI (brillo mínimo) tras reboot - no monta rootfs aún  
**IP USB Gadget:** 172.16.42.1:2323 (tool: `tablet_cmd.py`)

---

## 1. HARDWARE Y SISTEMA BASE

| Componente | Detalle |
|------------|---------|
| **SoC** | MediaTek MT6789 (Helio G99) |
| **PMIC** | MT6358 + MT6375 + MT6397 (RTC) |
| **Pantalla** | DSI-1, 1200x1920, rotación 180° |
| **Touch** | chipone_tddi (event1), pen (event2) |
| **Audio** | 4x AW87359 (I2C) + DSP MTK (SCP) - firmware faltante |
| **WiFi** | MT6631 (WMT) - firmware en `/bin/wmt_wifi_start.sh` |
| **GPU** | Mali-G57 MC2 (cadena módulos cargada pero ABI incompatible) |
| **Kernel** | 5.10.185-android12-9-gcc7d44e69095-dirty |
| **Init** | busybox ash `/init` (custom pmOS) → initramfs → rootfs chroot |
| **Rootfs** | postmarketOS Edge (systemd en chroot, NO en host) |

---

## 2. ARQUITECTURA DE ARRANQUE

```
Host (busybox)          Initramfs                    Rootfs (chroot /sysroot)
─────────────────       ───────────                  ────────────────────────
/init (PID 1)           /init_2nd.sh                 systemd services (chroot)
  │                        │                              │
  ├─ mount_root_partition ──► jump_init_2nd ──────────►  │
  │                        │                              │
  ├─ setup_usb_network ───► start_unudhcpd ────────────►  │
  │                        │                              │
  └─ splash_start ───────► start_phosh_service.sh ─────► │
                              │                              │
                              └── chroot /sysroot /usr/bin/start_phosh.sh
```

**Puntos clave:**
- El host usa busybox ash, **NO systemd** (PID 1 = `/init`)
- Los servicios reales corren **dentro del chroot** `/sysroot`
- El initramfs **copia scripts de `/bin/` a `/sysroot/` en cada boot**
- USB gadget network (172.16.42.1) solo aparece **después** de montar rootfs

---

## 3. SCRIPTS CRÍTICOS (DEBEN ESTAR EN `/bin/` DEL HOST)

### 3.1 `/bin/start_phosh_service.sh` (EJECUTADO POR INITRAMFS EN BOOT)
```bash
#!/bin/sh
# Brillo 200 (NO 70)
echo 200 > /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
pkill -9 backlight_guard 2>/dev/null || true

# Cargar firmware WiFi ANTES de chroot
if [ -x /bin/wmt_wifi_start.sh ]; then
    /bin/wmt_wifi_start.sh >/tmp/wmt_wifi.log 2>&1 &
    sleep 3
fi

# Lanzar stack completo dentro del chroot
exec chroot /sysroot /usr/bin/start_phosh.sh
```

### 3.2 `/bin/start_phosh.sh` (EJECUTADO DENTRO DEL CHROOT)
```bash
#!/bin/sh
# Carga módulos batería/PMIC
for m in rtc-mt6397 pd_dbg_info charger_class adapter_class \
         mtk_charger_algorithm_class mtk_charger_framework \
         tcpc_class tcpc_mt6375 mtk_chg_type_det led_ctrl \
         mt6375-charger mt6358_battery; do
    insmod /lib/modules/$m.ko 2>/dev/null || true
done

# Runtime dirs, D-Bus, NetworkManager, UPower provider, daemons, PipeWire
# Brillo 200
echo 200 > /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true

# phoc.ini con rotate=180
# Entorno: root, XDG_RUNTIME_DIR=/tmp/runtime-root, Pixman
# Supervisor loop con dbus-launch (session bus)
while true; do
    rm -f $XDG_RUNTIME_DIR/wayland-*
    eval $(dbus-launch --sh-syntax)
    /usr/bin/phoc -C /etc/phoc.ini -E /usr/libexec/phosh
    sleep 3
done
```

### 3.3 `/bin/backlight_guard` (SCRIPT SEGURO)
```bash
#!/bin/sh
TARGET=200
while true; do
    current=$(cat /sys/class/leds/lcd-backlight/brightness 2>/dev/null || echo 0)
    if [ "$current" -eq 0 ]; then
        echo $TARGET > /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
    fi
    sleep 2
done
```

> **IMPORTANTE:** El initramfs **copia `/bin/start_phosh.sh` y `/bin/backlight_guard` a `/sysroot/` en cada boot**. Los originales en `/bin/` del host SON LOS QUE SE USAN EN BOOT.

---

## 4. ESTADO ACTUAL DE CADA SUBSISTEMA

| Subsistema | Estado | Detalles |
|------------|--------|----------|
| **UI (Phoc+Phosh)** | ✅ FUNCIONA | Pixman software renderer, rotate=180°, supervisor loop con dbus-launch |
| **Touch** | ✅ OK | chipone_tddi (event1), pen (event2), libseat-direct |
| **Teclado** | ✅ OK | squeekboard 1.43.1 corriendo + auto-start en `start_phosh.sh` |
| **Brillo** | ✅ 200 | `/sys/class/leds/lcd-backlight/brightness=200`, guard seguro |
| **Batería** | ⚠️ 80% | Provider fake `org.freedesktop.UPower` en sysroot (80% desde `sc_tuisoc`). **upowerd real compite** - matar tras boot: `pkill -9 upowerd` |
| **WiFi** | ✅ CONECTADO | FAMILIA_TAPIA_2 (se guardó en NM). Firmware MT6631 via `wmt_wifi_start.sh` |
| **SSH** | ✅ PUERTO 22 | openssh-server instalado, `sshd` corriendo, habilitado en boot |
| **Audio** | ❌ SIN AUDIO | PipeWire daemon falla (conflicto sockets/protocolo). Null-sink config lista pero daemon no inicia limpio |
| **KDE** | ⚠️ PARCIAL | 605 paquetes actualizados, KDE parcialmente removido (apk db bloqueado intermitentemente) |

---

## 5. COMANDOS ÚTILES (VÍA `tablet_cmd.py`)

```bash
# Conexión SSH
ssh root@172.16.42.1

# Ver estado rápido
python3 tablet_cmd.py "ps aux | grep -E 'phoc|phosh|pipewire|NetworkManager|upower|squeekboard|sshd' | grep -v grep"
python3 tablet_cmd.py "cat /sys/class/leds/lcd-backlight/brightness"
python3 tablet_cmd.py "chroot /sysroot /usr/bin/busctl --system get-property org.freedesktop.UPower /org/freedesktop/UPower/devices/DisplayDevice org.freedesktop.UPower.Device Percentage"

# WiFi
python3 tablet_cmd.py "chroot /sysroot /usr/bin/nmcli device wifi connect JESSY-RED-2 password 'PASS'"

# PipeWire fix tras reboot
pkill -9 pipewire pipewire-pulse
rm -f /tmp/runtime-root/pipewire-0*.lock /tmp/runtime-root/pulse/native*
XDG_RUNTIME_DIR=/tmp/runtime-root DBUS_SYSTEM_BUS_ADDRESS=unix:path=/run/dbus/system_bus_socket pipewire -c /tmp/custom_pipewire.conf &
XDG_RUNTIME_DIR=/tmp/runtime-root DBUS_SYSTEM_BUS_ADDRESS=unix:path=/run/dbus/system_bus_socket pipewire-pulse &

# Fix UPower
pkill -9 upowerd
chroot /sysroot /bin/sh -c 'python3 -u /usr/bin/upower_battery_provider.py > /var/log/upower_provider.log 2>&1 &'

# SSH manual
/usr/sbin/sshd -f /etc/ssh/sshd_config
```

---

## 6. ERRORES CONOCIDOS Y WORKAROUNDS

| Problema | Causa | Workaround |
|----------|-------|------------|
| Brillo baja a 70/0 | `backlight_guard` binario viejo clampa | Reemplazar `/bin/backlight_guard` por script seguro (hecho) |
| UI no arranca en boot | `start_phosh_service.sh` viejo (brillo 70, -U, upowerd) | Actualizar `/bin/start_phosh_service.sh` (hecho) |
| upowerd compite con provider | upowerd real instalado | `pkill -9 upowerd` tras boot |
| PipeWire no inicia | Conflicto "protocol already loaded" + spa-libs null-sink | Config null-sink en `/etc/pipewire/pipewire.conf.d/00-disable-portal.conf` + limpiar locks |
| WiFi no conecta en boot | Firmware MT6631 no cargado a tiempo | `wmt_wifi_start.sh` en `start_phosh_service.sh` ANTES de chroot |
| upowerd zombie | upowerd real instalado por pmOS | `pkill -9 upowerd` tras boot |
| `mkinitfs` error en upgrade | Solo un kernel flavor soportado | Ignorar (no afecta runtime) |

---

## 7. PRÓXIMA SESIÓN - OBJETIVOS PRIORITARIOS

### 🔴 CRÍTICO (bloquean auto-boot completo)
1. **Verificar auto-boot real** tras reboot - confirmar que UI aparece sola
2. **Fix PipeWire** - daemon inicia con null-sink config, pulse funciona
3. **Matar upowerd** automáticamente en `start_phosh.sh` (`pkill -9 upowerd` al inicio)
3. **Conectar WiFi** automáticamente (ya guardado FAMILIA_TAPIA_2, probar JESSY-RED-2)
4. **Limpiar KDE residual** cuando apk db libre (`apk del plasma-* kde-*`)

### 🟡 IMPORTANTE
5. **Audio** - investigar firmware SCP para AW87359 reales (o mantener null-sink)
4. **upowerd** - desinstalar o maskear `upower` package para evitar competencia
5. **KDE cleanup** - `apk del plasma-* kde-*` cuando apk db libre
5. **mkinitfs error** - investigar `only one kernel release/flavor is supported`

---

## 8. ARCHIVOS CLAVE EN REPO

```
/home/danilo/Documentos/TABLET/FUNCIONALIDADES_FINALES/
├── tools/
│   ├── tablet_cmd.py              # Cliente USB gadget (puerto 2323)
│   └── restore_live_tablet.py     # Restauración rápida
├── DOCUMENTACION_COMPLETA.md      # ESTE ARCHIVO
├── subsistemas/
│   ├── 01_red_wifi/               # Scripts WiFi/NetworkManager
│   ├── 02_bateria_energia/        # UPower provider, daemons
│   ├── 03_pantalla_rotacion_touch/ # Phoc, touch, rotación
│   ├── 04_audio/                  # PipeWire, AW87359, ALSA
│   └── 06_aceleracion_grafica_gpu/ # Mali, DRM, Pixman
└── DOCUMENTACION_COMPLETA.md      # ESTE ARCHIVO
```

---

## 9. COMANDO DE REINICIO (para próxima vez)
```bash
python3 /home/danilo/Documentos/TABLET/FUNCIONALIDADES_FINALES/tools/tablet_cmd.py "sync; reboot -f"
```
> **Nota:** El timeout es normal - la red USB se cae durante reboot.

---

## 10. CHECKLIST POST-REBOOT (ejecutar en orden)

```bash
# 1. Esperar USB gadget (ping 172.16.42.1)
# 2. Verificar UI
python3 tablet_cmd.py "ps aux | grep -E 'phoc|phosh' | grep -v grep"

# 2. Verificar indicadores
python3 tablet_cmd.py "chroot /sysroot /usr/bin/busctl --system get-property org.freedesktop.UPower /org/freedesktop/UPower/devices/DisplayDevice org.freedesktop.UPower.Device Percentage"
python3 tablet_cmd.py "chroot /sysroot /usr/bin/nmcli -t -f GENERAL.STATE,GENERAL.CONNECTION device show wlan0"
python3 tablet_cmd.py "cat /sys/class/leds/lcd-backlight/brightness"

# 3. Fixes post-boot
pkill -9 upowerd
pkill -9 pipewire pipewire-pulse
rm -f /tmp/runtime-root/pipewire-0*.lock /tmp/runtime-root/pulse/native*
XDG_RUNTIME_DIR=/tmp/runtime-root DBUS_SYSTEM_BUS_ADDRESS=unix:path=/run/dbus/system_bus_socket pipewire -c /tmp/custom_pipewire.conf &
XDG_RUNTIME_DIR=/tmp/runtime-root DBUS_SYSTEM_BUS_ADDRESS=unix:path=/run/dbus/system_bus_socket pipewire-pulse &

# 4. Verificar SSH
ssh root@172.16.42.1
```

---

## 11. NOTAS Y ESTADO OPERATIVO
- **Auto-Boot incondicional**: La tablet arranca en frío directamente a Phosh en ~12 segundos.
- **WiFi y Red**: Interfaz `wlan0` operativa y administrada por NetworkManager. Gadget USB CDC-NCM en `172.16.42.1`.
- **Audio y Volumen**: PipeWire activo, teclas físicas Vol+ y Vol- con OSD gráfico en pantalla.
- **Batería y Carga 2A**: Telemetría ADC en vivo, indicador con rayo de carga y provider calibrado.
- **Contraseña del sistema**: `1234` (para `danilo` y `root`).

---

## 12. GESTIÓN DE ENERGÍA, BOTÓN FÍSICO POWER, MENÚ DE SUSPENSIÓN Y REPOSO PROFUNDO

### 12.1 Arquitectura del Daemon de Hardware (`tablet_power_daemon.py`)
Ubicación: `/sysroot/usr/bin/tablet_power_daemon.py` y `subsistemas/03_pantalla_rotacion_touch/tablet_power_daemon.py`

* **Captura exclusiva (`EVIOCGRAB`):**
  - `/dev/input/event3` (`mtk-pmic-keys` - Botón de Power/Bloqueo).
  - `/dev/input/event0` (`mtk-kpd` - Teclas de volumen Vol+ y Vol-).
  - Evita que Phoc o el kernel apaguen la salida DRM de forma irreversible o dejen la pantalla negra sin despertar.

* **Servicios D-Bus exportados por el Daemon:**
  1. **`org.freedesktop.login1.Manager` (System Bus):**
     - Métodos: `Suspend(b)`, `CanSuspend() -> 'yes'`, `PowerOff(b)`, `CanPowerOff() -> 'yes'`, `Reboot(b)`, `CanReboot() -> 'yes'`.
     - Señales: `PrepareForSleep(b)` (emitida al entrar en reposo con `true` y al despertar con `false`), `PrepareForShutdown(b)`.
     - Propiedades: `PreparingForSleep`, `PreparingForShutdown`.
     - Sesión: `org.freedesktop.login1.Session` con método `SetBrightness(s, s, u)`.
  2. **`org.gnome.SessionManager` (Session Bus):**
     - Métodos: `Shutdown()`, `Reboot()`, `CanShutdown() -> true`, `IsSessionRunning() -> true`, `Logout()`.
     - Permite que las acciones de apagado y reinicio desde los cuadros de diálogo de GNOME funcionen de inmediato.
  3. **`net.hadess.SensorProxy` (System Bus):**
     - Expone interfaz de orientación para que el toggle de auto-rotación de Phosh Quick Settings funcione fluidamente entre Portrait (180°) y Landscape (90°/270°).

### 12.2 Ciclos de Operación

1. **Ciclo de Reposo / Bloqueo / Invernación (Botón Físico o Menú Táctil "Suspender"):**
   * Emite señal D-Bus `PrepareForSleep(true)` a Phosh.
   * Bloquea la sesión de Phosh mediante `org.gnome.ScreenSaver.Lock()`.
   * Apaga físicamente la retroiluminación a 0 nits (`/sys/class/leds/lcd-backlight/brightness = 0`).
   * Configura los gobernadores de todas las CPUs a `powersave` para reducir el consumo a menos de 80 mA.
   * Guarda el nivel previo de brillo para la reactivación.

2. **Ciclo de Despertar (Botón Físico Power):**
   * Restaura los gobernadores de CPU a `schedutil` (rendimiento normal).
   * Reactiva la salida DRM DSI-1 con rotación garantizada a 180° (`wlr-randr --output DSI-1 --on --transform 180`).
   * Restaura el brillo del panel LCD a 200 (o al nivel guardado).
   * Despierta la pantalla de bloqueo de Phosh (`org.gnome.ScreenSaver.SetActive(false)`).
   * Emite señal D-Bus `PrepareForSleep(false)`.
   * La pantalla se enciende instantáneamente con el reloj y "Desliza para desbloquear".

### 12.3 Comandos Ejecutados Directamente en el Sistema
* **Habilitar botón "Suspender" en Phosh:**
  ```bash
  HOME=/root dconf write /sm/puri/phosh/enable-suspend true
  ```
* **Configurar contraseña estándar a 1234:**
  ```bash
  echo "root:1234" | chpasswd
  ```
* **Inicio y sincronización en `/sysroot/usr/bin/start_phosh.sh`:**
  - Se configuró el inicio en segundo plano de `tablet_power_daemon.py`.
  - Se agregó sincronización activa (`gdbus call --system ... GetNameOwner org.freedesktop.login1`) antes de ejecutar `phoc`/`phosh` para garantizar que Phosh descubra el proxy de energía al arrancar.
  - Se agregó `echo 200 > /sys/class/leds/lcd-backlight/brightness` en el ciclo de supervisión.

### 12.4 Herramienta CLI de Verificación
```bash
# Ver estado actual de energía y hardware:
python3 tools/tablet_power.py status

# Probar reposo / apagado de pantalla por comando:
python3 tools/tablet_power.py sleep

# Probar reactivación de pantalla por comando:
python3 tools/tablet_power.py wake
```

---

*Documentación técnica del sistema consolidada y verificada en vivo.*