#!/bin/sh
# ==============================================================================
# Lanzador de Sesion Phoc + Phosh Acelerada por GPU
# ==============================================================================
mkdir -p /tmp/runtime-root /var/log /run/dbus /var/run/dbus /run/udev/data
chmod 700 /tmp/runtime-root
rm -rf /tmp/runtime-root/* 2>/dev/null || true

# Asegurar que /dev/mali0 tenga el minor dinámico correcto de /proc/misc
MALI_MINOR=$(grep -w mali0 /proc/misc 2>/dev/null | awk '{print $1}')
if [ -n "$MALI_MINOR" ]; then
    rm -f /dev/mali0 2>/dev/null || true
    mknod /dev/mali0 c 10 "$MALI_MINOR" 2>/dev/null || true
    chmod 666 /dev/mali0 2>/dev/null || true
fi
chmod 666 /dev/dri/* /dev/dma_heap/* 2>/dev/null || true

# Asegurar descriptores udev para libinput (touchscreen y teclas)
for ev in /sys/class/input/event*; do
    [ ! -e "$ev" ] && continue
    devname=$(basename "$ev")
    devnum=$(cat "$ev/dev" 2>/dev/null)
    [ -z "$devnum" ] && continue
    sysname=$(cat "$ev/device/name" 2>/dev/null || echo "")
    datafile="/run/udev/data/c$devnum"
    echo "I:559115511" > "$datafile"
    echo "E:DEVNAME=/dev/input/$devname" >> "$datafile"
    echo "E:MAJOR=13" >> "$datafile"
    echo "E:ID_INPUT=1" >> "$datafile"
    if echo "$sysname" | grep -i -q "chipone_tddi"; then
        echo "E:ID_INPUT_TOUCHSCREEN=1" >> "$datafile"
        echo "E:ID_BUS=i2c" >> "$datafile"
        echo "E:LIBINPUT_CALIBRATION_MATRIX=-1 0 1 0 -1 1" >> "$datafile"
    elif echo "$sysname" | grep -i -q "key\|kpd"; then
        echo "E:ID_INPUT_KEY=1" >> "$datafile"
        echo "E:ID_INPUT_KEYBOARD=1" >> "$datafile"
    fi
done

export HOME=/root
export USER=root
export XDG_RUNTIME_DIR=/tmp/runtime-root

export WLR_RENDERER=gles2
export WLR_RENDERER_ALLOW_SOFTWARE=0
export WLR_BACKENDS=drm,libinput
export WLR_DRM_DEVICES=/dev/dri/card0
export WLR_DRM_NO_ATOMIC=1
export WLR_DRM_NO_MODIFIERS=1
export WLR_NO_HARDWARE_CURSORS=1
# export WLR_LIBINPUT_NO_DEVICES=1

export EGL_PLATFORM=wayland
export GDK_BACKEND=wayland
export QT_QPA_PLATFORM=wayland
export XDG_SESSION_TYPE=wayland
export XDG_CURRENT_DESKTOP=GNOME:Phosh
export XDG_SESSION_DESKTOP=phosh

if [ ! -e /run/dbus/system_bus_socket ]; then
    dbus-daemon --system --fork 2>/dev/null || true
fi

PHOC_PRELOAD="/usr/lib/libjemalloc.so.2 /usr/lib/libseat_direct.so /usr/lib/libgpu_interface_driver.so"

echo "[session] Lanzando Phoc + Phosh acelerado por GPU..." > /var/log/phosh_session.log
exec dbus-run-session -- env LD_PRELOAD="$PHOC_PRELOAD" /usr/bin/phoc -v -S -C /etc/phoc.ini -E "/usr/bin/phosh_wrapper.sh" >> /var/log/phosh_session.log 2>&1
