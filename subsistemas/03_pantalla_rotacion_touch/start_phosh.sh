#!/bin/sh
# Montar /dev/shm tmpfs si no existe (CRÍTICO para Wayland/wlroots xkb keymaps)
mkdir -p /dev/shm 2>/dev/null || true
if ! grep -q '/dev/shm' /proc/mounts 2>/dev/null; then
    mount -t tmpfs -o mode=1777,size=128M shm /dev/shm 2>/dev/null || true
fi
# Carga módulos batería/PMIC y carga rápida
for m in rtc-mt6397 pd_dbg_info adapter_class charger_class \
         mtk_charger_algorithm_class tcpc_class tcpc_mt6375 \
         mtk_charger_framework mt6375-charger extcon-mtk-usb \
         mtk_pep20 mtk_pep40 mtk_pd_charging mtk_chg_type_det \
         led_ctrl mt6358_battery mt635x_auxadc; do
    modprobe $m 2>/dev/null || \
    insmod /lib/modules/$m.ko 2>/dev/null || \
    insmod /usr/lib/modules/5.10.185-android12-9-gcc7d44e69095-dirty/$m.ko 2>/dev/null || \
    insmod /lib/modules/5.10.185-android12-9-gcc7d44e69095-dirty/$m.ko 2>/dev/null || true
done

# Activar carga rápida a nivel de hardware (2A)
echo 2000000 > /sys/devices/platform/charger/input_current 2>/dev/null || true
echo 4 > /sys/devices/platform/charger/chr_type 2>/dev/null || true
CHG_DEV="/sys/devices/platform/soc/11017000.i2c/i2c-5/5-0034/11017000.i2c:mt6375@34:chg/power_supply/11017000.i2c:mt6375@34:chg"
if [ -d "$CHG_DEV" ]; then
    echo 1 > $CHG_DEV/online 2>/dev/null || true
    echo "Charging" > $CHG_DEV/status 2>/dev/null || true
    echo 2000 > $CHG_DEV/input_current_limit 2>/dev/null || true
    echo 2000 > $CHG_DEV/constant_charge_current 2>/dev/null || true
fi

# Runtime dirs, D-Bus, NetworkManager, UPower provider, daemons, PipeWire
# Brillo 200
echo 200 > /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true

# phoc.ini con rotate=180
cat << 'PHOCEOF' > /etc/phoc.ini
[core]
xwayland=false

[output:DSI-1]
scale=1.0
rotate=180
PHOCEOF

# Entorno: root, XDG_RUNTIME_DIR=/tmp/runtime-root, Pixman
mkdir -p /tmp/runtime-root
chmod 700 /tmp/runtime-root

export HOME=/root
export USER=root
export XDG_RUNTIME_DIR=/tmp/runtime-root
export DBUS_SYSTEM_BUS_ADDRESS=unix:path=/run/dbus/system_bus_socket
export WLR_BACKENDS=drm,libinput
export WLR_NO_HARDWARE_CURSORS=1
export WLR_DRM_NO_MODIFIERS=1
export WLR_RENDERER=pixman
export WLR_RENDERER_ALLOW_SOFTWARE=1
export LP_NUM_THREADS=4
export LD_PRELOAD=/usr/lib/libseat_direct.so
export GDK_BACKEND=wayland
export QT_QPA_PLATFORM=wayland
export SDL_VIDEODRIVER=wayland
export XDG_SESSION_TYPE=wayland
export XDG_CURRENT_DESKTOP=GNOME:Phosh
export XDG_SESSION_DESKTOP=phosh
export MOZ_ENABLE_WAYLAND=1

# Iniciar D-Bus system si no existe
mkdir -p /run/dbus /var/run/dbus
if [ ! -e /run/dbus/system_bus_socket ]; then
    dbus-daemon --system --fork 2>/dev/null || true
fi

# NetworkManager
if ! pgrep -x NetworkManager >/dev/null; then
    NetworkManager 2>/dev/null || true
fi

# Iniciar proveedor UPower fake (NO upowerd real - compite)
if ! pgrep -f upower_battery_provider.py >/dev/null; then
    python3 -u /usr/bin/upower_battery_provider.py > /var/log/upower_provider.log 2>&1 &
fi

# Iniciar PipeWire con config null-sink
if ! pgrep -x pipewire >/dev/null; then
    rm -f /tmp/runtime-root/pipewire-0*.lock /tmp/runtime-root/pulse/native*
    XDG_RUNTIME_DIR=/tmp/runtime-root DBUS_SYSTEM_BUS_ADDRESS=unix:path=/run/dbus/system_bus_socket pipewire -c /tmp/custom_pipewire.conf > /var/log/pipewire.log 2>&1 &
fi
if ! pgrep -x pipewire-pulse >/dev/null; then
    XDG_RUNTIME_DIR=/tmp/runtime-root DBUS_SYSTEM_BUS_ADDRESS=unix:path=/run/dbus/system_bus_socket pipewire-pulse > /var/log/pipewire-pulse.log 2>&1 &
fi

# Iniciar squeekboard
if ! pgrep -x squeekboard >/dev/null; then
    squeekboard > /var/log/squeekboard.log 2>&1 &
fi

# Habilitar botón de Suspender en el menú de Phosh
HOME=/root dconf write /sm/puri/phosh/enable-suspend true 2>/dev/null || true

# Iniciar daemon de botón Power (bloqueo/reposo), volumen y rotación
if ! pgrep -f tablet_power_daemon.py >/dev/null; then
    python3 -u /usr/bin/tablet_power_daemon.py > /var/log/tablet_power.log 2>&1 &
fi

# Supervisor loop con dbus-run-session (session bus para compositor y shell)
while true; do
    rm -f $XDG_RUNTIME_DIR/wayland-*
    dbus-run-session -- /usr/bin/phoc -v -S -C /etc/phoc.ini -E "/usr/libexec/phosh -U" > /var/log/phosh.log 2>&1
    sleep 3
done