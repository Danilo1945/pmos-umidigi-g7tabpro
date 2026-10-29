#!/bin/sh
# ==============================================================================
# Lanzador Maestro de Phoc + Phosh con Pixman
# ==============================================================================

# 1. Blindaje de Brillo anti-UVLO (15/255)
mkdir -p /run /sysroot/run /sysroot/var/log /sysroot/tmp/runtime-root
echo 15 > /sys/devices/platform/soc/soc:mtk_leds/leds/lcd-backlight/brightness 2>/dev/null || true
echo 15 > /run/dummy_brightness
echo 15 > /sysroot/run/dummy_brightness
chmod 666 /run/dummy_brightness /sysroot/run/dummy_brightness
mount --bind /run/dummy_brightness /sys/devices/platform/soc/soc:mtk_leds/leds/lcd-backlight/brightness 2>/dev/null || true
mount --bind /sysroot/run/dummy_brightness /sysroot/sys/devices/platform/soc/soc:mtk_leds/leds/lcd-backlight/brightness 2>/dev/null || true

# 2. Centinela de brillo en segundo plano
if ! pgrep -f "dummy_brightness" >/dev/null 2>&1; then
    (
        while true; do
            echo 15 > /run/dummy_brightness 2>/dev/null || true
            echo 15 > /sysroot/run/dummy_brightness 2>/dev/null || true
            sleep 2
        done
    ) &
fi

# 3. Permisos y runtime
chmod 700 /sysroot/tmp/runtime-root 2>/dev/null || true
rm -rf /sysroot/tmp/runtime-root/* 2>/dev/null || true

# 4. Lanzar Phoc + Phosh en chroot
chroot /sysroot sh -c '
    export HOME=/root
    export USER=root
    export XDG_RUNTIME_DIR=/tmp/runtime-root
    export WLR_RENDERER=pixman
    export WLR_BACKENDS=drm,libinput
    export WLR_DRM_DEVICES=/dev/dri/card0
    export WLR_DRM_NO_ATOMIC=1
    export WLR_DRM_NO_MODIFIERS=1
    export WLR_NO_HARDWARE_CURSORS=1

    mkdir -p /run/dbus
    if [ ! -e /run/dbus/system_bus_socket ]; then
        dbus-daemon --system --fork 2>/dev/null || true
    fi

    echo "[launcher] Lanzando Phoc (Pixman) + Phosh..." > /var/log/phoc_pixman.log
    LD_PRELOAD="/usr/lib/libjemalloc.so.2 /usr/lib/libseat_direct.so" \
    exec dbus-run-session -- /usr/bin/phoc -v -S -C /etc/phoc.ini -E "/usr/bin/phosh_wrapper.sh" >> /var/log/phoc_pixman.log 2>&1
'
