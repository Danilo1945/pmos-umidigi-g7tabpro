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
export XDG_SESSION_TYPE=wayland
export XDG_CURRENT_DESKTOP=GNOME:Phosh
export XDG_SESSION_DESKTOP=phosh
export NO_AT_BRIDGE=1
export LANG=es_EC.UTF-8
export LC_ALL=es_EC.UTF-8
export LANGUAGE=es_EC:es

# Aceleración GPU Mali nativa para clientes GTK / GNOME Mobile
export GSK_RENDERER=gl
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

# 3. Configuraciones de energia seguras para GNOME y animaciones fluidas a 60 FPS
gsettings set org.gnome.settings-daemon.plugins.power active false 2>/dev/null || true
gsettings set org.gnome.settings-daemon.plugins.color active false 2>/dev/null || true
gsettings set org.gnome.settings-daemon.plugins.power idle-dim false 2>/dev/null || true
gsettings set org.gnome.desktop.session idle-delay 0 2>/dev/null || true
gsettings set org.gnome.desktop.interface enable-animations true 2>/dev/null || true
dconf write /sm/puri/phosh/enable-suspend true 2>/dev/null || true

# 4. Propagar entorno Wayland/GNOME al bus de sesion D-Bus y lanzar squeekboard + gsd-rfkill
dbus-update-activation-environment WAYLAND_DISPLAY XDG_CURRENT_DESKTOP XDG_SESSION_TYPE XDG_RUNTIME_DIR GSK_RENDERER GDK_BACKEND HOME LANG LC_ALL LANGUAGE 2>/dev/null || true
if ! pgrep -x squeekboard >/dev/null 2>&1; then
    /usr/bin/squeekboard >/var/log/squeekboard.log 2>&1 &
fi
chmod 666 /dev/rfkill 2>/dev/null || true
if [ -x /usr/libexec/gsd-rfkill ] && ! pgrep -x gsd-rfkill >/dev/null 2>&1; then
    /usr/libexec/gsd-rfkill >/var/log/gsd-rfkill.log 2>&1 &
fi

echo "[wrapper] Lanzando sesion GNOME Mobile Phosh con aceleracion GPU completa..." >> /var/log/phosh_user.log
if [ -f /usr/lib/libphosh_backlight_shim.so ]; then
    exec env LD_PRELOAD=/usr/lib/libphosh_backlight_shim.so /usr/libexec/phosh -U >> /var/log/phosh_user.log 2>&1
fi
exec /usr/libexec/phosh -U >> /var/log/phosh_user.log 2>&1
