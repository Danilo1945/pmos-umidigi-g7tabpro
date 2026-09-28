#!/bin/sh
set -e

# ==============================================================================
# Servicio de Inicio de Phoc + Phosh y Subsistemas de Hardware
# UMIDIGI G7 Tab Pro (Helio G99 / MT6789) - postmarketOS Edge
# ==============================================================================

# 1. Elevar de inmediato wdt_pet a máxima prioridad de tiempo real (SCHED_FIFO 99)
for p in $(pgrep wdt_pet); do
    chroot /sysroot /usr/bin/chrt -f -p 99 "$p" 2>/dev/null || true
done

# 2. Asegurar SHM en raíz y sysroot (modo 1777 para wlroots y shm keymaps)
mkdir -p /dev/shm /sysroot/dev/shm
mount -t tmpfs -o rw,nosuid,nodev,mode=1777 tmpfs /dev/shm 2>/dev/null || true
mount -t tmpfs -o rw,nosuid,nodev,mode=1777 tmpfs /sysroot/dev/shm 2>/dev/null || true
chmod 1777 /dev/shm /sysroot/dev/shm 2>/dev/null || true

# 3. Control seguro de brillo (70/255) y activación de supervisor Anti-UVLO
echo 70 > /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
chmod 444 /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
if [ -x /sysroot/bin/backlight_guard ]; then
    /sysroot/bin/backlight_guard &
elif [ -x /bin/backlight_guard ]; then
    /bin/backlight_guard &
fi

# 4. Cargar cadena devlink óptima GPU Mali-G57 MC2 (Silicio Valhall v9 JM)
MODDIR=/sysroot/usr/lib/modules/5.10.185-android12-9-gcc7d44e69095-dirty
for mod in nvmem_mtk-devinfo.ko mcupm.ko sspm_v3.ko mtk_tinysys_ipi.ko mtk-ssc.ko gpueb.ko fhctl.ko mtk_gpu_hal.ko mtk_qos.ko mtk_bp_thl.ko mtk_battery_oc_throttling.ko mtk_gpufreq_wrapper.ko mtk_gpu_qos.ko mtk_gpufreq_mt6789.ko ged.ko mali_mgm_mt6789.ko mali_kbase_mt6789.ko; do
    p="/usr/lib/modules/$mod"
    [ ! -f "$p" ] && p="$MODDIR/$mod"
    insmod "$p" 2>/dev/null || true
done
mknod /dev/mali0 c 10 116 2>/dev/null || true
mknod /sysroot/dev/mali0 c 10 116 2>/dev/null || true
chmod 666 /dev/mali0 /sysroot/dev/mali0 2>/dev/null || true

# 5. Cargar módulos de Batería AUXADC (PMIC MT6366)
insmod "$MODDIR/mt635x-auxadc.ko" 2>/dev/null || true

# 6. Iniciar D-Bus de sistema y UPower en sysroot
chroot /sysroot /bin/sh -c '
    mkdir -p /run/dbus /var/run/dbus
    if [ ! -e /run/dbus/system_bus_socket ]; then
        dbus-daemon --system --fork 2>/dev/null
    fi
    pgrep -x upowerd >/dev/null || /usr/libexec/upowerd >/dev/null 2>&1 &
'

# 7. Iniciar daemon de batería inteligente en sysroot
if ! chroot /sysroot pgrep -f tablet_battery_daemon.py >/dev/null 2>&1; then
    nohup chroot /sysroot /usr/bin/python3 -u /usr/bin/tablet_battery_daemon.py </dev/null >/sysroot/var/log/battery_daemon.log 2>&1 &
fi

# 8. Lanzar compositor Phoc y Shell Phosh
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

echo 'SERVICE_STARTED'
