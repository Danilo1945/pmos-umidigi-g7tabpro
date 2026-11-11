#!/bin/sh
# ==============================================================================
# phosh_wrapper.sh - Lanzador de GNOME Mobile Shell (Phosh) con Aceleración GPU
# ==============================================================================
unset LD_PRELOAD
export HOME=/root
export USER=root
export XDG_RUNTIME_DIR=/tmp/runtime-root
export WAYLAND_DISPLAY=wayland-0
export GDK_BACKEND=wayland
export QT_QPA_PLATFORM=wayland
export QT_QUICK_BACKEND=software
export QTWEBENGINE_DISABLE_SANDBOX=1
export XDG_SESSION_TYPE=wayland
export XDG_CURRENT_DESKTOP=GNOME:Phosh
export XDG_SESSION_DESKTOP=phosh
export NO_AT_BRIDGE=1

# Clientes GTK4 y Qt6 renderizan en SHM Wayland y Phoc compone por GPU Mali-G57
export GSK_RENDERER=cairo
unset GDK_DEBUG
unset GDK_GL
unset GDK_RENDERING
unset GDK_DISABLE

# 1. Esperar disponibilidad del socket Wayland de Phoc
count=0
while [ ! -S /tmp/runtime-root/wayland-0 ] && [ $count -lt 80 ]; do
    sleep 0.1
    count=$((count + 1))
done

# 2. Esperar a que el compositor Phoc complete la inicializacion de KMS y registre DSI-1
count=0
while [ $count -lt 80 ]; do
    if /usr/bin/wlr-randr 2>/dev/null | grep -q "DSI-1"; then
        echo "[wrapper] Salida DSI-1 detectada y activa en Wayland compositor!" >> /var/log/phosh_user.log
        break
    fi
    sleep 0.2
    count=$((count + 1))
done
sleep 0.5

# 3. Propagar entorno completo a D-Bus de sesión para activación de servicios y GNOME Settings
cat << 'EOF_ENV' > /etc/profile.d/phosh-cairo.sh
export GSK_RENDERER=cairo
export QT_QUICK_BACKEND=software
export QTWEBENGINE_DISABLE_SANDBOX=1
export NO_AT_BRIDGE=1
EOF_ENV
mkdir -p /usr/local/bin /etc/mpv /tmp/runtime-danilo 2>/dev/null || true
chown 10000:10000 /tmp/runtime-danilo 2>/dev/null || true
chmod 700 /tmp/runtime-danilo 2>/dev/null || true
chmod 755 /tmp/runtime-root 2>/dev/null || true
chmod 777 /tmp/runtime-root/wayland-* 2>/dev/null || true
ln -sf /tmp/runtime-root/wayland-0 /tmp/runtime-danilo/wayland-0 2>/dev/null || true
cat << 'EOF_MPV' > /etc/mpv/mpv.conf
vo=wlshm
ao=pipewire,alsa
EOF_MPV
cat << 'EOF_FF' > /usr/lib/firefox/firefox
#!/bin/sh
export HOME=/root
export USER=root
export XDG_RUNTIME_DIR=/tmp/runtime-root
export WAYLAND_DISPLAY=wayland-0
export MOZ_ENABLE_WAYLAND=1
export GDK_BACKEND=wayland
export GSK_RENDERER=cairo
export LIBGL_ALWAYS_SOFTWARE=1
exec /usr/lib/firefox/firefox-real "$@"
EOF_FF
chmod 755 /usr/lib/firefox/firefox 2>/dev/null || true
cat << 'EOF_GCC' > /usr/local/bin/gnome-control-center
#!/bin/sh
export XDG_CURRENT_DESKTOP=GNOME
export GSK_RENDERER=cairo
exec /usr/bin/gnome-control-center "$@"
EOF_GCC
cat << 'EOF_GSW' > /usr/local/bin/gnome-software
#!/bin/sh
if [ "$(id -u)" = "0" ]; then
    mkdir -p /tmp/runtime-danilo
    chown 10000:10000 /tmp/runtime-danilo
    chmod 700 /tmp/runtime-danilo
    chmod 755 /tmp/runtime-root 2>/dev/null || true
    chmod 777 /tmp/runtime-root/wayland-* 2>/dev/null || true
    ln -sf /tmp/runtime-root/wayland-0 /tmp/runtime-danilo/wayland-0
    exec su -s /bin/sh danilo -c "
        export HOME=/home/danilo
        export USER=danilo
        export XDG_RUNTIME_DIR=/tmp/runtime-danilo
        export WAYLAND_DISPLAY=wayland-0
        export GDK_BACKEND=wayland
        export GSK_RENDERER=cairo
        export NO_AT_BRIDGE=1
        exec dbus-run-session -- /usr/bin/gnome-software \"\$@\"
    " -- "$@"
fi
exec /usr/bin/gnome-software "$@"
EOF_GSW
chmod 755 /usr/local/bin/gnome-control-center /usr/local/bin/gnome-software 2>/dev/null || true
dbus-update-activation-environment --all 2>/dev/null || true
dbus-update-activation-environment --systemd GSK_RENDERER=cairo QTWEBENGINE_DISABLE_SANDBOX=1 GDK_BACKEND=wayland WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/tmp/runtime-root XDG_CURRENT_DESKTOP=GNOME:Phosh XDG_SESSION_DESKTOP=phosh NO_AT_BRIDGE=1 2>/dev/null || true
gdbus call --session --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus --method org.freedesktop.DBus.UpdateActivationEnvironment "{'GSK_RENDERER': 'cairo', 'QTWEBENGINE_DISABLE_SANDBOX': '1', 'GDK_BACKEND': 'wayland', 'WAYLAND_DISPLAY': 'wayland-0', 'XDG_RUNTIME_DIR': '/tmp/runtime-root', 'XDG_CURRENT_DESKTOP': 'GNOME:Phosh', 'XDG_SESSION_DESKTOP': 'phosh', 'NO_AT_BRIDGE': '1'}" >/dev/null 2>&1 || true

# 4. Iniciar demonio de RFKILL y sincronización automática de zona horaria y hora NTP
cp -f /usr/share/zoneinfo/America/Guayaquil /etc/localtime 2>/dev/null || true
echo "America/Guayaquil" > /etc/timezone 2>/dev/null || true
grep -q "8.8.8.8" /etc/resolv.conf 2>/dev/null || printf "nameserver 8.8.8.8\nnameserver 1.1.1.1\n" >> /etc/resolv.conf 2>/dev/null || true
if ! pgrep -x ntpd >/dev/null 2>&1; then
    ntpd -p 216.239.35.0 -p 216.239.35.4 -p pool.ntp.org 2>/dev/null || true
fi
if ! pgrep -x gsd-rfkill >/dev/null 2>&1; then
    /usr/libexec/gsd-rfkill >/var/log/gsd-rfkill.log 2>&1 &
fi

# 5. Configuraciones de energia seguras para GNOME y animaciones fluidas a 60 FPS
gsettings set org.gnome.settings-daemon.plugins.power active false 2>/dev/null || true
gsettings set org.gnome.settings-daemon.plugins.color active false 2>/dev/null || true
gsettings set org.gnome.settings-daemon.plugins.power idle-dim false 2>/dev/null || true
gsettings set org.gnome.desktop.session idle-delay 0 2>/dev/null || true
gsettings set org.gnome.desktop.interface enable-animations true 2>/dev/null || true
gsettings set org.gnome.desktop.interface show-battery-percentage true 2>/dev/null || true
dconf write /org/gnome/desktop/interface/show-battery-percentage true 2>/dev/null || true
dconf write /sm/puri/phosh/enable-suspend true 2>/dev/null || true

# 6. Iniciar teclado en pantalla squeekboard dentro de la sesion Wayland activa
if ! pgrep -x squeekboard >/dev/null 2>&1; then
    /usr/bin/squeekboard >/var/log/squeekboard.log 2>&1 &
fi

echo "[wrapper] Lanzando sesion GNOME Mobile Phosh con aceleracion GPU completa..." >> /var/log/phosh_user.log
exec /usr/libexec/phosh -U >> /var/log/phosh_user.log 2>&1
