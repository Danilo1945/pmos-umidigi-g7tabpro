#!/bin/sh
# launch_phosh_user.sh - Lanzador de la sesión de usuario de Phosh bajo Wayland acelerado por GPU
while [ ! -S /tmp/runtime-danilo/wayland-0 ]; do
    sleep 0.2
done
chmod 777 /tmp/runtime-danilo /tmp/runtime-danilo/wayland-* 2>/dev/null || true
mkdir -p /tmp/runtime-danilo/dconf
chown -R danilo:danilo /tmp/runtime-danilo /home/danilo 2>/dev/null || true

exec su -s /bin/sh danilo -c '
    export HOME=/home/danilo
    export USER=danilo
    export XDG_RUNTIME_DIR=/tmp/runtime-danilo
    export WAYLAND_DISPLAY=wayland-0
    export DBUS_SYSTEM_BUS_ADDRESS="unix:path=/run/dbus/system_bus_socket"
    eval $(dbus-launch --sh-syntax)
    export GDK_BACKEND=wayland
    export CLUTTER_BACKEND=wayland
    export QT_QPA_PLATFORM=wayland
    export XDG_SESSION_TYPE=wayland
    export GSK_RENDERER=gl
    export QT_QUICK_BACKEND=gl
    export MOZ_ENABLE_WAYLAND=1
    # Iniciar PipeWire y WirePlumber de usuario
    nohup pipewire </dev/null >/dev/null 2>&1 &
    nohup wireplumber </dev/null >/dev/null 2>&1 &

    exec /usr/libexec/phosh -U
'
