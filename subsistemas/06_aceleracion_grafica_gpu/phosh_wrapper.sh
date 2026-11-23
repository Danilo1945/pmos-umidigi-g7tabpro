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

# Renderizador Cairo SHM para clientes GTK4 (compositor Phoc acelerado por GPU Mali)
export GSK_RENDERER=cairo
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
gsettings set sm.puri.phosh favorites "['org.gnome.Calls.desktop', 'sm.puri.Chatty.desktop', 'org.gnome.Contacts.desktop', 'firefox.desktop', 'org.gnome.Nautilus.desktop', 'org.gnome.Calculator.desktop', 'org.gnome.Console.desktop', 'org.gnome.Settings.desktop']" 2>/dev/null || true
dconf write /sm/puri/phosh/enable-suspend true 2>/dev/null || true

# 4. Iniciar wireplumber, squeekboard y gsd-rfkill, y propagar entorno Wayland/GNOME al bus D-Bus
if [ -x /usr/bin/wireplumber ]; then
    pkill -x wireplumber 2>/dev/null || true
    sleep 0.1
    /usr/bin/wireplumber >/var/log/wireplumber.log 2>&1 &
fi
if ! pgrep -x squeekboard >/dev/null 2>&1; then
    /usr/bin/squeekboard >/var/log/squeekboard.log 2>&1 &
fi
chmod 666 /dev/rfkill 2>/dev/null || true
if [ -x /usr/libexec/gsd-rfkill ] && ! pgrep -x gsd-rfkill >/dev/null 2>&1; then
    /usr/libexec/gsd-rfkill >/var/log/gsd-rfkill.log 2>&1 &
fi

export SDL_VIDEODRIVER=wayland
export SDL_RENDER_DRIVER=opengles2
export LIBGL_ES=2
export PULSE_LATENCY_MSEC=60
export LD_LIBRARY_PATH="/usr/lib/libhybris_mali"
export HYBRIS_LD_LIBRARY_PATH="/usr/lib/mali_android"
export HYBRIS_ANDROID_SDK_VERSION=30
export HYBRIS_EGLPLATFORM=null
export LD_PRELOAD="/usr/lib/libhybris_gbm_bridge.so /usr/lib/libhybris_mali/libEGL.so.1 /usr/lib/libhybris_mali/libGLESv2.so.2 /usr/lib/libphosh_backlight_shim.so"
dbus-update-activation-environment WAYLAND_DISPLAY XDG_CURRENT_DESKTOP XDG_SESSION_DESKTOP XDG_SESSION_TYPE XDG_RUNTIME_DIR GSK_RENDERER GDK_BACKEND SDL_VIDEODRIVER SDL_RENDER_DRIVER LIBGL_ES PULSE_LATENCY_MSEC HOME USER LANG LC_ALL LANGUAGE LD_LIBRARY_PATH HYBRIS_LD_LIBRARY_PATH HYBRIS_ANDROID_SDK_VERSION HYBRIS_EGLPLATFORM LD_PRELOAD 2>/dev/null || true

echo "[wrapper] Lanzando sesion GNOME Mobile Phosh con aceleracion GPU completa..." >> /var/log/phosh_user.log
exec /usr/libexec/phosh -U >> /var/log/phosh_user.log 2>&1
