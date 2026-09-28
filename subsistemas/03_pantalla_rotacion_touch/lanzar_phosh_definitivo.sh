#!/bin/sh
set -e

echo "=== INICIANDO SERVICIO DEFINITIVO DE PHOSH ==="

# 1. Elevar wdt_pet a SCHED_FIFO 99
for p in $(pgrep wdt_pet); do
    chroot /sysroot /usr/bin/chrt -f -p 99 "$p" 2>/dev/null || true
done
echo "[1/6] Watchdog en tiempo real FIFO 99 OK"

# 2. Iniciar supervisor Anti-UVLO y blindar brillo
if [ -x /bin/backlight_guard ]; then
    /bin/backlight_guard &
elif [ -x /sysroot/bin/backlight_guard ]; then
    /sysroot/bin/backlight_guard &
fi
echo 70 > /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
chmod 444 /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
echo "[2/6] Supervisor Anti-UVLO activo y brillo protegido en 70 (27%)"

# 3. Montar SHM 1777
mkdir -p /dev/shm /sysroot/dev/shm
mount -t tmpfs -o rw,nosuid,nodev,mode=1777 tmpfs /dev/shm 2>/dev/null || true
mount -t tmpfs -o rw,nosuid,nodev,mode=1777 tmpfs /sysroot/dev/shm 2>/dev/null || true
chmod 1777 /dev/shm /sysroot/dev/shm 2>/dev/null || true
echo "[3/6] Memoria compartida tmpfs 1777 OK"

# 4. Iniciar D-Bus de sistema y UPower en sysroot
chroot /sysroot /bin/sh -c '
    mkdir -p /run/dbus /var/run/dbus
    if [ ! -e /run/dbus/system_bus_socket ]; then
        dbus-daemon --system --fork 2>/dev/null || true
    fi
    pgrep -x upowerd >/dev/null || /usr/libexec/upowerd >/dev/null 2>&1 &
'
echo "[4/6] D-Bus y UPower en sysroot OK"

# 5. Iniciar tablet_battery_daemon en sysroot
if ! chroot /sysroot pgrep -f tablet_battery_daemon.py >/dev/null 2>&1; then
    nohup chroot /sysroot /usr/bin/python3 -u /usr/bin/tablet_battery_daemon.py </dev/null >/sysroot/var/log/battery_daemon.log 2>&1 &
fi
echo "[5/6] Monitor de bateria OCV v4 OK"

# 6. Lanzar Phoc + Phosh con libseat_direct
nohup chroot /sysroot /bin/sh -c '
    mkdir -p /tmp/runtime-root
    chmod 700 /tmp/runtime-root
    export HOME=/root
    export USER=root
    export XDG_RUNTIME_DIR=/tmp/runtime-root
    export DBUS_SYSTEM_BUS_ADDRESS="unix:path=/run/dbus/system_bus_socket"
    export WLR_BACKENDS=drm,libinput
    export WLR_NO_HARDWARE_CURSORS=1
    export WLR_DRM_NO_MODIFIERS=1
    export WLR_RENDERER=pixman
    export WLR_RENDERER_ALLOW_SOFTWARE=1
    export LP_NUM_THREADS=4
    export GDK_BACKEND=wayland
    export QT_QPA_PLATFORM=wayland
    export SDL_VIDEODRIVER=wayland
    export XDG_SESSION_TYPE=wayland
    export XDG_CURRENT_DESKTOP=GNOME:Phosh
    export XDG_SESSION_DESKTOP=phosh
    export MOZ_ENABLE_WAYLAND=1

    rm -f /tmp/runtime-root/wayland-*
    while true; do
        LD_PRELOAD=/usr/lib/libseat_direct.so dbus-run-session -- /usr/bin/phoc -C /etc/phoc.ini -E "/usr/libexec/phosh -U"
        sleep 2
    done
' </dev/null >/sysroot/var/log/phosh_service.log 2>&1 &

echo "[6/6] Phoc + Phosh lanzado con libseat_direct en segundo plano"
echo "PHOSH_SUCCESSFULLY_STARTED"
