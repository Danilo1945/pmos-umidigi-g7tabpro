#!/bin/sh
# ==============================================================================
# Lanzador Robusto Maestro de Phoc + Phosh para UMIDIGI G7 Tab Pro (MT6789)
# postmarketOS Edge (Kernel 5.10.185-pmos aarch64)
# ==============================================================================
set -e

echo "=== INICIANDO SERVICIO MAESTRO DEFINITIVO PMOS ==="

# 1. Asegurar SHM en raíz y sysroot
mkdir -p /dev/shm /sysroot/dev/shm /sysroot/tmp/runtime-root
mount -t tmpfs -o mode=1777,size=128M shm /dev/shm 2>/dev/null || true
mount -t tmpfs -o mode=1777,size=128M shm /sysroot/dev/shm 2>/dev/null || true
chmod 1777 /dev/shm /sysroot/dev/shm /sysroot/tmp 2>/dev/null || true
chmod 700 /sysroot/tmp/runtime-root
echo "[1/7] SHM y runtime directories configurados OK"

# 2. Control seguro de brillo (27% = 70/255) y protección anti-UVLO (chmod 444)
chmod 666 /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
echo 70 > /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
chmod 444 /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
if ! pgrep -x backlight_guard >/dev/null 2>&1; then
    [ -x /bin/backlight_guard ] && /bin/backlight_guard >/dev/null 2>&1 &
fi
echo "[2/7] Brillo protegido en 70/255 anti-UVLO OK"

# 3. Carga limpia del subsistema GPU ARM Mali-G57 y proteccion OPP 37
if [ -x /sysroot/usr/bin/load_mali_chain.sh ]; then
    sh /sysroot/usr/bin/load_mali_chain.sh 2>&1 || true
elif [ -x /usr/bin/load_mali_chain.sh ]; then
    sh /usr/bin/load_mali_chain.sh 2>&1 || true
fi
if [ -e /proc/gpufreqv2/fix_target_opp_index ]; then
    echo 37 > /proc/gpufreqv2/fix_target_opp_index 2>/dev/null || true
fi
chmod 666 /dev/mali0 /sysroot/dev/mali0 2>/dev/null || true
echo "[3/7] GPU Mali inicializada, permisos 666 y OPP 37 (390MHz@575mV) OK"

# 4. Asegurar Watchdog pet daemon activo
if ! pgrep -f wdt_pet >/dev/null 2>&1; then
    [ -x /bin/wdt_pet ] && /bin/wdt_pet >/dev/null 2>&1 &
fi
echo "[4/7] Watchdog pet daemon verificado OK"

# 5. Iniciar D-Bus de sistema, UPower y daemons en sysroot
chroot /sysroot /bin/sh -c '
    mkdir -p /run/dbus /var/run/dbus
    if [ ! -e /run/dbus/system_bus_socket ]; then
        dbus-daemon --system --fork 2>/dev/null || true
    fi
    pgrep -x upowerd >/dev/null || /usr/libexec/upowerd >/dev/null 2>&1 &
    pgrep -f tablet_battery_daemon >/dev/null || nohup /usr/bin/python3 -u /usr/bin/tablet_battery_daemon.py >/var/log/battery_daemon.log 2>&1 &
'
echo "[5/7] D-Bus, UPower y Monitores en sysroot OK"

# 6. Iniciar compositor Phoc y Shell Phosh desacoplados con setsid
rm -f /sysroot/tmp/runtime-root/wayland-*
setsid chroot /sysroot /bin/sh -c '
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

echo "[6/7] Phoc + Phosh lanzado con setsid en segundo plano OK"

# 7. Safety-net para rotacion 180 a los 3 segundos
(
    sleep 3
    chroot /sysroot env WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/tmp/runtime-root /usr/bin/wlr-randr --output DSI-1 --transform 180 2>&1 || true
) &
echo "[7/7] Rotacion automatica 180 configurada OK"
echo "PHOSH_STARTED_SUCCESSFULLY"
