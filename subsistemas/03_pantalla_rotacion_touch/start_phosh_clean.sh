#!/bin/sh
set -e

echo "=== INICIANDO PHOSH LIMPIO (USUARIO DANILO + LIBSEAT + SHM 1777) ==="

# 0. Asegurar módulos de GPU Mali-G57 y reloj OPP 37
if [ -x /sysroot/usr/bin/load_mali_chain.sh ]; then
    /sysroot/usr/bin/load_mali_chain.sh 2>/dev/null || true
elif [ -f /bin/load_mali_chain.sh ]; then
    sh /bin/load_mali_chain.sh 2>/dev/null || true
fi
echo 37 > /proc/gpufreqv2/fix_target_opp_index 2>/dev/null || true
chmod 666 /dev/mali0 /sysroot/dev/mali0 2>/dev/null || true

# 1. Elevar wdt_pet a SCHED_FIFO 99
for p in $(pgrep wdt_pet); do
    chroot /sysroot /usr/bin/chrt -f -p 99 "$p" 2>/dev/null || true
done
echo "[1/8] Watchdog SCHED_FIFO 99 verificado"

# 2. Control de brillo nominal (70/255 = 27%) y supervisor Anti-UVLO
echo 70 > /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
chmod 444 /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
if ! pgrep -x backlight_guard >/dev/null; then
    if [ -x /sysroot/bin/backlight_guard ]; then
        /sysroot/bin/backlight_guard &
    elif [ -x /sysroot/usr/bin/backlight_guard ]; then
        /sysroot/usr/bin/backlight_guard &
    fi
fi
echo "[2/8] Brillo blindado en 70 (444) y backlight_guard activo"

# 3. Montar /dev/shm en raiz y sysroot (tmpfs 1777)
mkdir -p /dev/shm /sysroot/dev/shm
mount -t tmpfs -o rw,nosuid,nodev,mode=1777 tmpfs /dev/shm 2>/dev/null || true
mount -t tmpfs -o rw,nosuid,nodev,mode=1777 tmpfs /sysroot/dev/shm 2>/dev/null || true
chmod 1777 /dev/shm /sysroot/dev/shm 2>/dev/null || true
echo "[3/8] Memoria compartida tmpfs 1777 lista"

# 4. Directorio de runtime para usuario danilo
mkdir -p /sysroot/tmp/runtime-danilo /sysroot/var/log
chroot /sysroot chown -R danilo:danilo /tmp/runtime-danilo /home/danilo 2>/dev/null || true
chmod 777 /sysroot/tmp/runtime-danilo
rm -f /sysroot/tmp/runtime-danilo/wayland-*
echo "[4/8] Runtime dir /tmp/runtime-danilo preparado (777)"

# 5. D-Bus y UPower en sysroot
chroot /sysroot /bin/sh -c '
    mkdir -p /run/dbus /var/run/dbus
    if [ ! -e /run/dbus/system_bus_socket ]; then
        dbus-daemon --system --fork 2>/dev/null || true
    fi
    pgrep -x upowerd >/dev/null || /usr/libexec/upowerd >/dev/null 2>&1 &
'
echo "[5/8] D-Bus y UPower en sysroot iniciados"

# 6. Monitor de bateria OCV
if ! chroot /sysroot pgrep -f tablet_battery_daemon.py >/dev/null 2>&1; then
    nohup chroot /sysroot /usr/bin/python3 -u /usr/bin/tablet_battery_daemon.py </dev/null >/sysroot/var/log/battery_daemon.log 2>&1 &
fi
echo "[6/8] tablet_battery_daemon v4 activo"

# 7. Lanzador Phosh de usuario (espera socket Wayland y levanta phosh con sesion danilo)
chroot /sysroot /bin/sh /bin/launch_phosh_user.sh >/sysroot/var/log/phosh_user.log 2>&1 &
echo "[7/8] launch_phosh_user.sh iniciado en espera del socket wayland-0"

# 8. Transformación 180° retardada
(
    sleep 4
    chroot /sysroot /bin/sh -c 'WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/tmp/runtime-danilo /usr/bin/wlr-randr --output DSI-1 --on --mode 1200x1920 --transform 180 2>/dev/null || true'
) &

# 9. Iniciar Phoc compositor con escaneo lineal seguro (Pixman dumb buffer) y aceleración Wayland
nohup chroot /sysroot /bin/sh -c '
    export HOME=/home/danilo
    export USER=danilo
    export XDG_RUNTIME_DIR=/tmp/runtime-danilo
    export WLR_BACKENDS=drm,libinput
    export WLR_NO_HARDWARE_CURSORS=1
    export WLR_DRM_NO_MODIFIERS=1
    export WLR_RENDERER=pixman
    export WLR_RENDERER_ALLOW_SOFTWARE=1
    export LP_NUM_THREADS=4
    LD_PRELOAD=/usr/lib/libseat_direct.so /usr/bin/phoc -C /etc/phoc.ini
' </dev/null >/sysroot/var/log/phoc_live.log 2>&1 &

echo "[8/8] Phoc compositor lanzado con scanout lineal estable"
echo "PHOSH_PIPELINE_COMPLETE"
