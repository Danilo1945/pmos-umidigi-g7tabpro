#!/bin/sh
# ==============================================================================
# Lanzador de Sesion Phoc + Phosh Acelerada por GPU
# ==============================================================================
mkdir -p /tmp/runtime-root /var/log /run/dbus /var/run/dbus
chmod 700 /tmp/runtime-root
rm -rf /tmp/runtime-root/* 2>/dev/null || true

export HOME=/root
export USER=root
export XDG_RUNTIME_DIR=/tmp/runtime-root

export WLR_RENDERER=gles2
export WLR_BACKENDS=drm,libinput
export WLR_DRM_DEVICES=/dev/dri/card0
export WLR_DRM_NO_ATOMIC=1
export WLR_DRM_NO_MODIFIERS=1
export WLR_NO_HARDWARE_CURSORS=1
export WLR_LIBINPUT_NO_DEVICES=1

export EGL_PLATFORM=wayland
export GDK_BACKEND=wayland
export QT_QPA_PLATFORM=wayland
export XDG_SESSION_TYPE=wayland
export XDG_CURRENT_DESKTOP=GNOME:Phosh
export XDG_SESSION_DESKTOP=phosh

if [ ! -e /run/dbus/system_bus_socket ]; then
    dbus-daemon --system --fork 2>/dev/null || true
fi

PHOC_PRELOAD="/usr/lib/libjemalloc.so.2 /usr/lib/libstdc++.so.6 /usr/lib/libseat_direct.so /usr/lib/libgpu_interface_driver.so /usr/lib/libmali-bifrost-patched.so"

echo "[session] Lanzando Phoc + Phosh acelerado por GPU..." > /var/log/phosh_session.log
LD_PRELOAD="$PHOC_PRELOAD" exec dbus-run-session -- /usr/bin/phoc -v -S -C /etc/phoc.ini -E "/usr/bin/phosh_wrapper.sh" >> /var/log/phosh_session.log 2>&1
