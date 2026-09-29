#!/bin/sh
# ==============================================================================
# Lanzador Robusto de Phoc + Phosh para UMIDIGI G7 Tab Pro (MT6789)
# postmarketOS Edge (Kernel 5.10.185-pmos aarch64)
# ==============================================================================
set -e

echo "=== INICIANDO SERVICIO DEFINITIVO DE PHOC + PHOSH ==="

# 1. Asegurar SHM en raíz y sysroot
mkdir -p /dev/shm /sysroot/dev/shm /sysroot/tmp/runtime-root
mount -t tmpfs -o mode=1777 none /dev/shm 2>/dev/null || true
mount -t tmpfs -o mode=1777 none /sysroot/dev/shm 2>/dev/null || true
chmod 1777 /dev/shm /sysroot/dev/shm 2>/dev/null || true
chmod 700 /sysroot/tmp/runtime-root
echo "[1/6] SHM y runtime directories configurados OK"

# 2. Control seguro de brillo (27% = 70/255) y protección anti-UVLO (chmod 444)
killall backlight_guard 2>/dev/null || true
chmod 666 /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
echo 70 > /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
chmod 444 /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
echo "[2/6] Brillo protegido en 70/255 anti-UVLO OK"

# 3. Permisos de GPU Mali-G57 MC2
chmod 666 /dev/mali0 /sysroot/dev/mali0 2>/dev/null || true
echo "[3/6] Permisos GPU Mali /dev/mali0 OK"

# 4. Iniciar D-Bus de sistema y UPower en sysroot
chroot /sysroot /bin/sh -c '
    mkdir -p /run/dbus /var/run/dbus
    if [ ! -e /run/dbus/system_bus_socket ]; then
        dbus-daemon --system --fork 2>/dev/null || true
    fi
    pgrep -x upowerd >/dev/null || /usr/libexec/upowerd >/dev/null 2>&1 &
'
echo "[4/6] D-Bus y UPower en sysroot OK"

# 5. Iniciar tablet_battery_daemon en sysroot si no está corriendo
if ! chroot /sysroot pgrep -f tablet_battery_daemon.py >/dev/null 2>&1; then
    nohup chroot /sysroot /usr/bin/python3 -u /usr/bin/tablet_battery_daemon.py >/sysroot/var/log/battery_daemon.log 2>&1 &
fi
echo "[5/6] Monitor de bateria OCV v4 OK"

# 6. Lanzar compositor Phoc y Shell Phosh desacoplados con setsid
rm -f /sysroot/tmp/runtime-root/wayland-*
setsid chroot /sysroot /bin/sh -c '
    export HOME=/root
    export USER=root
    export XDG_RUNTIME_DIR=/tmp/runtime-root
    export DBUS_SYSTEM_BUS_ADDRESS="unix:path=/run/dbus/system_bus_socket"
    export WLR_BACKENDS=drm
    export WLR_NO_HARDWARE_CURSORS=1
    export WLR_DRM_NO_MODIFIERS=1
    export WLR_RENDERER=pixman
    export WLR_RENDERER_ALLOW_SOFTWARE=1
    export LP_NUM_THREADS=2
    export LD_PRELOAD=/usr/lib/libseat_direct.so
    export GDK_BACKEND=wayland
    export QT_QPA_PLATFORM=wayland
    export SDL_VIDEODRIVER=wayland
    export XDG_SESSION_TYPE=wayland
    export XDG_CURRENT_DESKTOP=GNOME:Phosh
    export XDG_SESSION_DESKTOP=phosh
    export MOZ_ENABLE_WAYLAND=1

    exec dbus-run-session -- /usr/bin/phoc -v -S -C /etc/phoc.ini -E "/usr/libexec/phosh -U"
' </dev/null >/sysroot/var/log/phosh_service.log 2>&1 &

echo "[6/6] Phoc + Phosh lanzado con setsid en segundo plano OK"
echo "PHOSH_STARTED_SUCCESSFULLY"
