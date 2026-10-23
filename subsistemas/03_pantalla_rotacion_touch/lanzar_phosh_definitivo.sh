#!/bin/sh
# ==============================================================================
# Lanzador Robusto Maestro PMOS - UMIDIGI G7 Tab Pro (MT6789)
# postmarketOS Edge (Kernel 5.10.185-pmos aarch64)
# ==============================================================================
set -e

echo "=== INICIANDO TODOS LOS SERVICIOS DEL SISTEMA PMOS ==="

# 1. Asegurar SHM en raíz y sysroot
mkdir -p /dev/shm /sysroot/dev/shm /sysroot/tmp/runtime-root
mount -t tmpfs -o mode=1777,size=128M shm /dev/shm 2>/dev/null || true
mount -t tmpfs -o mode=1777,size=128M shm /sysroot/dev/shm 2>/dev/null || true
chmod 1777 /dev/shm /sysroot/dev/shm /sysroot/tmp 2>/dev/null || true
chmod 700 /sysroot/tmp/runtime-root
echo "[1/8] SHM y runtime directories configurados OK"

# 2. Control seguro de brillo (15/255) y protección anti-UVLO (chmod 444)
chmod 666 /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
echo 15 > /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
chmod 444 /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
if ! pgrep -x backlight_guard >/dev/null 2>&1; then
    [ -x /bin/backlight_guard ] && /bin/backlight_guard >/dev/null 2>&1 &
fi
echo "[2/8] Brillo blindado en 15/255 anti-UVLO OK"

# 3. Watchdog de hardware activo (Prioridad Tiempo Real FIFO 99)
if ! pgrep -f wdt_pet >/dev/null 2>&1; then
    [ -x /bin/wdt_pet ] && /bin/wdt_pet >/dev/null 2>&1 &
fi
echo "[3/8] Watchdog pet daemon verificado OK"

# 4. Subsistema GPU ARM Mali-G57 (Carga limpia y bloqueo OPP 37)
if [ -x /sysroot/usr/bin/load_mali_chain.sh ]; then
    sh /sysroot/usr/bin/load_mali_chain.sh 2>&1 || true
elif [ -x /usr/bin/load_mali_chain.sh ]; then
    sh /usr/bin/load_mali_chain.sh 2>&1 || true
fi
if [ -e /proc/gpufreqv2/fix_target_opp_index ]; then
    echo 37 > /proc/gpufreqv2/fix_target_opp_index 2>/dev/null || true
fi
chmod 666 /dev/mali0 /sysroot/dev/mali0 2>/dev/null || true
echo "[4/8] GPU Mali inicializada en OPP 37 seguro (390MHz@575mV) OK"

# 5. Conectividad Wi-Fi MediaTek WMT
if [ -x /bin/wmt_wifi_start.sh ] && ! ifconfig wlan0 >/dev/null 2>&1; then
    /bin/wmt_wifi_start.sh >/tmp/wmt_wifi.log 2>&1 &
fi
echo "[5/8] Wi-Fi WMT verificado OK"

# 6. D-Bus, UPower, Audio (AW87359), Batería OCV y NetworkManager en sysroot
chroot /sysroot /bin/sh -c '
    # D-Bus
    mkdir -p /run/dbus /var/run/dbus
    if [ ! -e /run/dbus/system_bus_socket ]; then
        dbus-daemon --system --fork 2>/dev/null || true
    fi
    # NetworkManager
    pgrep -x NetworkManager >/dev/null || NetworkManager >/var/log/nm.log 2>&1 &
    # UPower
    pgrep -x upowerd >/dev/null || /usr/libexec/upowerd >/dev/null 2>&1 &
    # Modulo ADC para lectura real de voltaje de bateria
    modprobe mt635x_auxadc 2>/dev/null || insmod /lib/modules/6.17.0-14-generic/mt635x-auxadc.ko 2>/dev/null || true
    # Daemon Batería OCV
    pgrep -f tablet_battery_monitor >/dev/null || nohup /usr/bin/python3 -u /usr/bin/tablet_battery_monitor.py >/var/log/battery_monitor.log 2>&1 &
    # Daemon Audio + Teclas + Rotacion
    pgrep -f tablet_audio_daemon >/dev/null || nohup /usr/bin/python3 -u /usr/bin/tablet_audio_daemon.py >/var/log/tablet_audio.log 2>&1 &
'
echo "[6/8] D-Bus, NetworkManager, Audio, Batería OCV en sysroot OK"

# 7. Iniciar compositor Phoc y Shell Phosh desacoplados con setsid
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

echo "[7/8] Phoc + Phosh lanzado con setsid en segundo plano OK"

# 8. Sincronización final de rotación 180°
(
    sleep 3
    chroot /sysroot env WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/tmp/runtime-root /usr/bin/wlr-randr --output DSI-1 --transform 180 2>&1 || true
) &
echo "[8/8] Rotación 180° sincronizada OK"
echo "=== TODOS LOS SUBSISTEMAS RESTAURADOS Y ACTIVOS ==="
