#!/bin/sh
mkdir -p /sysroot/dev/shm
mount -t tmpfs -o mode=1777 none /sysroot/dev/shm 2>/dev/null || true
chmod 1777 /sysroot/dev/shm 2>/dev/null || true

chmod 666 /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
echo 180 > /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true

# Cargar GPU Mali-G57 MC2
modprobe mali_kbase_mt6789 2>/dev/null || true
mknod /dev/mali0 c 10 116 2>/dev/null || true
mknod /sysroot/dev/mali0 c 10 116 2>/dev/null || true
chmod 666 /dev/mali0 /sysroot/dev/mali0 2>/dev/null || true

nohup chroot /sysroot /bin/sh -c '
    mkdir -p /tmp/runtime-danilo /run/dbus /var/run/dbus
    chmod 700 /tmp/runtime-danilo
    chown danilo:danilo /tmp/runtime-danilo
    [ ! -e /run/dbus/system_bus_socket ] && dbus-daemon --system --fork 2>/dev/null
    pgrep -x upowerd >/dev/null || /usr/libexec/upowerd >/dev/null 2>&1 &
    
    export HOME=/home/danilo
    export USER=danilo
    export XDG_RUNTIME_DIR=/tmp/runtime-danilo
    export WLR_BACKENDS=drm,libinput
    export WLR_NO_HARDWARE_CURSORS=1
    export WLR_DRM_NO_MODIFIERS=1
    export WLR_RENDERER=pixman
    export WLR_RENDERER_ALLOW_SOFTWARE=1
    export LP_NUM_THREADS=8
    export GDK_BACKEND=wayland
    export QT_QPA_PLATFORM=wayland
    export SDL_VIDEODRIVER=wayland
    export XDG_SESSION_TYPE=wayland
    export XDG_CURRENT_DESKTOP=GNOME:Phosh
    export XDG_SESSION_DESKTOP=phosh
    export MOZ_ENABLE_WAYLAND=1

    (sleep 4; WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/tmp/runtime-danilo /usr/bin/wlr-randr --output DSI-1 --on --mode 1200x1920 --transform 180 2>/dev/null || true) &

    while true; do
        rm -f /tmp/runtime-danilo/wayland-*
        LD_PRELOAD=/usr/lib/libseat_direct.so dbus-run-session -- /usr/bin/phoc -C /etc/phoc.ini -E "/usr/libexec/phosh -U"
        sleep 2
    done
' >/sysroot/var/log/phosh_service.log 2>&1 </dev/null &
echo 'SERVICE_STARTED'
