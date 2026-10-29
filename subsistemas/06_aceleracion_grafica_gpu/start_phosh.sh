#!/bin/sh
# ==============================================================================
# Script Maestro Supervisor Phoc + Phosh Estable
# Renderizador: Pixman (Software CPU Octa-Core Helio G99) + DRM Legacy
# Plataforma: UMIDIGI G7 Tab Pro (TG2403GBA / MT6789)
# postmarketOS Edge / Linux 5.10.185-android12 aarch64 (musl libc)
# ==============================================================================

# 1. Sanitización de procesos residuales y liberación de DRM Master
mkdir -p /var/log /sysroot/var/log 2>/dev/null || true
killall -9 drm_splash panel_pwm_force 2>/dev/null || true

# 2. Blindaje anti-UVLO de brillo LCD permanentemente en 15/255 con escudo de bind-mount
echo 15 > /sys/devices/platform/soc/soc:mtk_leds/leds/lcd-backlight/brightness 2>/dev/null || true
mkdir -p /run /sysroot/run 2>/dev/null || true
echo 15 > /run/dummy_brightness 2>/dev/null || true
echo 15 > /sysroot/run/dummy_brightness 2>/dev/null || true
chmod 666 /run/dummy_brightness /sysroot/run/dummy_brightness 2>/dev/null || true
mount --bind /run/dummy_brightness /sys/devices/platform/soc/soc:mtk_leds/leds/lcd-backlight/brightness 2>/dev/null || true
mount --bind /sysroot/run/dummy_brightness /sysroot/sys/devices/platform/soc/soc:mtk_leds/leds/lcd-backlight/brightness 2>/dev/null || true

if ! pgrep -f "backlight_guard" >/dev/null 2>&1; then
    (
        while true; do
            echo 15 > /run/dummy_brightness 2>/dev/null || true
            echo 15 > /sysroot/run/dummy_brightness 2>/dev/null || true
            sleep 2
        done
    ) &
fi

# 3. Watchdog de hardware activo
if ! pgrep -f wdt_pet >/dev/null 2>&1; then
    [ -x /bin/wdt_pet ] && /bin/wdt_pet >/dev/null 2>&1 &
    [ -x /sysroot/bin/wdt_pet ] && /sysroot/bin/wdt_pet >/dev/null 2>&1 &
fi

# 4. Montaje de /dev/shm tmpfs (128M) (Requerido por Wayland/wlroots xkb keymaps)
mkdir -p /dev/shm /sysroot/dev/shm 2>/dev/null || true
if ! grep -q ' /dev/shm ' /proc/mounts 2>/dev/null; then
    mount -t tmpfs -o mode=1777,size=128M shm /dev/shm 2>/dev/null || true
fi
if ! grep -q ' /sysroot/dev/shm ' /proc/mounts 2>/dev/null; then
    mount -t tmpfs -o mode=1777,size=128M shm /sysroot/dev/shm 2>/dev/null || true
fi
chmod 1777 /dev/shm /sysroot/dev/shm 2>/dev/null || true

# 5. Permisos de dispositivos
chmod 666 /dev/dri/* /sysroot/dev/dri/* 2>/dev/null || true
chmod 666 /dev/input/* /sysroot/dev/input/* 2>/dev/null || true

# 6. Configuración de phoc.ini (rotación 180° y deshabilitar Writeback)
mkdir -p /etc /sysroot/etc
cat << 'PHOCEOF' > /sysroot/etc/phoc.ini
[core]
xwayland=false

[output:DSI-1]
enable=true
scale=1.0
rotate=180
mode=1200x1920@60.000Hz

[output:Writeback-1]
enable=false

[output:Writeback-2]
enable=false
PHOCEOF
cp -f /sysroot/etc/phoc.ini /etc/phoc.ini 2>/dev/null || true

# 7. Creación de phosh_wrapper.sh con entorno Wayland completo y renderizado por software seguro
mkdir -p /usr/bin /sysroot/usr/bin
cat << 'WRAPPEREOF' > /sysroot/usr/bin/phosh_wrapper.sh
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
export NO_AT_BRIDGE=1

# Forzar renderizado Cairo por software para GTK3/Phosh (evita tocar Mali)
export GDK_RENDERING=similar
export GDK_GL=never
export GSK_RENDERER=cairo

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

# 3. Configuraciones de energia seguras para GNOME
gsettings set org.gnome.settings-daemon.plugins.power active false 2>/dev/null || true
gsettings set org.gnome.settings-daemon.plugins.color active false 2>/dev/null || true
gsettings set org.gnome.settings-daemon.plugins.power idle-dim false 2>/dev/null || true
gsettings set org.gnome.desktop.session idle-delay 0 2>/dev/null || true

echo "[wrapper] Lanzando sesion GNOME Mobile Phosh..." >> /var/log/phosh_user.log
exec /usr/libexec/phosh -U >> /var/log/phosh_user.log 2>&1
WRAPPEREOF
chmod 755 /sysroot/usr/bin/phosh_wrapper.sh
cp -f /sysroot/usr/bin/phosh_wrapper.sh /usr/bin/phosh_wrapper.sh 2>/dev/null || true

# 8. Autoejecución en entorno chroot de postmarketOS si se invoca desde initramfs
if [ ! -f /usr/bin/phoc ] && [ -f /sysroot/usr/bin/phoc ]; then
    grep -q ' /sysroot/proc ' /proc/mounts || mount -t proc proc /sysroot/proc 2>/dev/null || true
    grep -q ' /sysroot/sys ' /proc/mounts || mount -t sysfs sysfs /sysroot/sys 2>/dev/null || true
    grep -q ' /sysroot/dev ' /proc/mounts || mount --bind /dev /sysroot/dev 2>/dev/null || true
    grep -q ' /sysroot/dev/pts ' /proc/mounts || mount -t devpts devpts /sysroot/dev/pts 2>/dev/null || true
    grep -q ' /sysroot/run ' /proc/mounts || mount -t tmpfs tmpfs /sysroot/run 2>/dev/null || true
    mkdir -p /sysroot/run/udev 2>/dev/null || true
    grep -q ' /sysroot/run/udev ' /proc/mounts || mount --bind /run/udev /sysroot/run/udev 2>/dev/null || true
    grep -q ' /sysroot/tmp ' /proc/mounts || mount -t tmpfs tmpfs /sysroot/tmp 2>/dev/null || true
    grep -q ' /sysroot/dev/shm ' /proc/mounts || mount -t tmpfs -o mode=1777,size=128M shm /sysroot/dev/shm 2>/dev/null || true
    chmod 755 /dev/input /sysroot/dev/input 2>/dev/null || true
    chmod 666 /dev/input/* /sysroot/dev/input/* 2>/dev/null || true
    exec chroot /sysroot /usr/bin/start_phosh.sh "$@"
fi

# 9. Entorno Wayland y variables de ejecución dentro de chroot
chmod 755 /dev/input 2>/dev/null || true
chmod 666 /dev/input/* 2>/dev/null || true

mkdir -p /tmp/runtime-root
chmod 700 /tmp/runtime-root 2>/dev/null || true

export HOME=/root
export USER=root
export XDG_RUNTIME_DIR=/tmp/runtime-root
export DBUS_SYSTEM_BUS_ADDRESS=unix:path=/run/dbus/system_bus_socket

# 10. Renderizador Pixman sobre DRM Dumb Buffers (Software CPU Octa-Core)
export WLR_RENDERER=pixman
export WLR_RENDERER_ALLOW_SOFTWARE=1
export WLR_BACKENDS=drm,libinput
export WLR_DRM_DEVICES=/dev/dri/card0
export WLR_LIBINPUT_NO_DEVICES=1
export WLR_NO_HARDWARE_CURSORS=1
export WLR_DRM_NO_MODIFIERS=1
export WLR_DRM_NO_ATOMIC=1

# Asegurar que udev esté activo para DRM/KMS y dispositivos de entrada
if ! pgrep -x udevd >/dev/null 2>&1; then
    /sbin/udevd --daemon 2>/dev/null || /usr/sbin/udevd --daemon 2>/dev/null || true
fi

# 11. Variables de integración para clientes Wayland
export GDK_BACKEND=wayland
export QT_QPA_PLATFORM=wayland
export SDL_VIDEODRIVER=wayland
export XDG_SESSION_TYPE=wayland
export XDG_CURRENT_DESKTOP=GNOME:Phosh
export XDG_SESSION_DESKTOP=phosh
export MOZ_ENABLE_WAYLAND=1

# 12. D-Bus de sistema
mkdir -p /run/dbus /var/run/dbus
if [ ! -e /run/dbus/system_bus_socket ]; then
    dbus-daemon --system --fork 2>/dev/null || true
fi

# Desactivar plugins de energia de GNOME para blindaje absoluto de backlight
gsettings set org.gnome.settings-daemon.plugins.power active false 2>/dev/null || true
gsettings set org.gnome.settings-daemon.plugins.color active false 2>/dev/null || true
gsettings set org.gnome.settings-daemon.plugins.power idle-dim false 2>/dev/null || true
gsettings set org.gnome.desktop.session idle-delay 0 2>/dev/null || true

# 13. Cadena LD_PRELOAD limpia (solo libseat_direct y jemalloc)
PHOC_PRELOAD="/usr/lib/libjemalloc.so.2 /usr/lib/libseat_direct.so"
unset LD_PRELOAD

# 14. Supervisor de Phoc + Phosh
echo "[supervisor] Iniciando Phoc con Pixman + Phosh en pantalla DSI-1..." >> /var/log/phosh_supervisor.log
rm -f $XDG_RUNTIME_DIR/wayland-* 2>/dev/null || true
echo 15 > /run/dummy_brightness 2>/dev/null || true
echo 15 > /sysroot/run/dummy_brightness 2>/dev/null || true

exec dbus-run-session -- env LD_PRELOAD="$PHOC_PRELOAD" /usr/bin/phoc -v -S -C /etc/phoc.ini -E "/usr/bin/phosh_wrapper.sh" >> /var/log/phosh_pixman.log 2>&1
