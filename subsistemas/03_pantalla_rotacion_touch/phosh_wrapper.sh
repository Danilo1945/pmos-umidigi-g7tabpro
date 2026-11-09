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
export XDG_SESSION_TYPE=wayland
export XDG_CURRENT_DESKTOP=GNOME:Phosh
export XDG_SESSION_DESKTOP=phosh
export NO_AT_BRIDGE=1

# Clientes GTK4 y Qt6 renderizan en SHM Wayland y Phoc compone por GPU Mali-G57
export GSK_RENDERER=cairo
export GDK_DEBUG=gl-disable

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
dbus-update-activation-environment --all 2>/dev/null || true

# 4. Iniciar demonio de RFKILL para control y conmutación de Bluetooth y modo avión
if ! pgrep -x gsd-rfkill >/dev/null 2>&1; then
    /usr/libexec/gsd-rfkill >/var/log/gsd-rfkill.log 2>&1 &
fi

# 5. Configuraciones de energia seguras para GNOME y animaciones fluidas a 60 FPS
gsettings set org.gnome.settings-daemon.plugins.power active false 2>/dev/null || true
gsettings set org.gnome.settings-daemon.plugins.color active false 2>/dev/null || true
gsettings set org.gnome.settings-daemon.plugins.power idle-dim false 2>/dev/null || true
gsettings set org.gnome.desktop.session idle-delay 0 2>/dev/null || true
gsettings set org.gnome.desktop.interface enable-animations true 2>/dev/null || true
dconf write /sm/puri/phosh/enable-suspend true 2>/dev/null || true

# 6. Iniciar teclado en pantalla squeekboard dentro de la sesion Wayland activa
if ! pgrep -x squeekboard >/dev/null 2>&1; then
    /usr/bin/squeekboard >/var/log/squeekboard.log 2>&1 &
fi

echo "[wrapper] Lanzando sesion GNOME Mobile Phosh con aceleracion GPU completa..." >> /var/log/phosh_user.log
exec /usr/libexec/phosh -U >> /var/log/phosh_user.log 2>&1
