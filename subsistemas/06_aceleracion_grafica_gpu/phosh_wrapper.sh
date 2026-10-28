#!/bin/sh
unset LD_PRELOAD
export HOME=/root
export USER=root
export XDG_RUNTIME_DIR=/tmp/runtime-root
export WAYLAND_DISPLAY=wayland-0
export GDK_BACKEND=wayland
export QT_QPA_PLATFORM=wayland
export XDG_SESSION_TYPE=wayland
export XDG_CURRENT_DESKTOP=GNOME:Phosh
export XDG_SESSION_DESKTOP=phosh

count=0
while [ ! -S /tmp/runtime-root/wayland-0 ] && [ $count -lt 50 ]; do
    sleep 0.1
    count=$((count + 1))
done

sleep 1.5

echo "[wrapper] Lanzando sesion GNOME Mobile Phosh..." >> /var/log/phosh_user.log
exec /usr/libexec/phosh -U >> /var/log/phosh_user.log 2>&1
