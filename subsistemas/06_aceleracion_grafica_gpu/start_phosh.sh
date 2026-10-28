#!/bin/sh
# ==============================================================================
# Script Maestro Supervisor Phoc + Phosh Acelerado por GPU ARM Mali-G57 MC2
# Plataforma: UMIDIGI G7 Tab Pro (MT6789 - Helio G99)
# postmarketOS Edge / Linux 5.10.185-android12 aarch64 (musl libc)
# ==============================================================================

# 1. Sanitización de procesos residuales y liberación de DRM Master
mkdir -p /var/log /sysroot/var/log 2>/dev/null || true
killall -9 drm_splash panel_pwm_force 2>/dev/null || true

# 2. Panel LCD Novatek activo nativamente


# 3. Blindaje anti-UVLO de brillo LCD permanentemente en 15/255 con centinela
chmod 666 /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
echo 15 > /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
chmod 444 /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
if ! pgrep -f backlight_guard >/dev/null 2>&1; then
    (
        while true; do
            chmod 666 /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
            echo 15 > /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
            chmod 444 /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
            sleep 2
        done
    ) &
fi

# 4. Watchdog de hardware activo
if ! pgrep -f wdt_pet >/dev/null 2>&1; then
    [ -x /bin/wdt_pet ] && /bin/wdt_pet >/dev/null 2>&1 &
    [ -x /sysroot/bin/wdt_pet ] && /sysroot/bin/wdt_pet >/dev/null 2>&1 &
fi

# 5. Montaje de /dev/shm tmpfs (128M) (Requerido por Wayland/wlroots xkb keymaps)
mkdir -p /dev/shm /sysroot/dev/shm 2>/dev/null || true
if ! grep -q ' /dev/shm ' /proc/mounts 2>/dev/null; then
    mount -t tmpfs -o mode=1777,size=128M shm /dev/shm 2>/dev/null || true
fi
if ! grep -q ' /sysroot/dev/shm ' /proc/mounts 2>/dev/null; then
    mount -t tmpfs -o mode=1777,size=128M shm /sysroot/dev/shm 2>/dev/null || true
fi
chmod 1777 /dev/shm /sysroot/dev/shm 2>/dev/null || true

# 6. Invocación de load_mali_chain.sh / OPP 37 y permisos 666 en /dev/mali0
if [ -x /usr/bin/load_mali_chain.sh ]; then
    /usr/bin/load_mali_chain.sh >/var/log/load_mali.log 2>&1 || true
elif [ -x /sysroot/usr/bin/load_mali_chain.sh ]; then
    /sysroot/usr/bin/load_mali_chain.sh >/var/log/load_mali.log 2>&1 || true
fi
chmod 666 /dev/mali0 /sysroot/dev/mali0 /dev/dma_heap/* /sysroot/dev/dma_heap/* /dev/dri/* /sysroot/dev/dri/* 2>/dev/null || true

# 7. Configuración de phoc.ini (rotación 180° y deshabilitar Writeback)
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

# 8. Creación de phosh_wrapper.sh con entorno Wayland completo
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

count=0
while [ ! -S /tmp/runtime-root/wayland-0 ] && [ $count -lt 50 ]; do
    sleep 0.1
    count=$((count + 1))
done

# Esperar a que el compositor Phoc complete la inicializacion de KMS
sleep 1.5

echo "[wrapper] Lanzando sesion GNOME Mobile Phosh..." >> /var/log/phosh_user.log
exec /usr/libexec/phosh -U >> /var/log/phosh_user.log 2>&1
WRAPPEREOF
chmod 755 /sysroot/usr/bin/phosh_wrapper.sh
cp -f /sysroot/usr/bin/phosh_wrapper.sh /usr/bin/phosh_wrapper.sh 2>/dev/null || true

# Autoejecución en entorno chroot de postmarketOS si se invoca desde initramfs
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

# 10. Aceleración GPU ARM Mali-G57 MC2 (GLES2 + DRM Legacy Estable)
export WLR_RENDERER=gles2
export WLR_RENDERER_ALLOW_SOFTWARE=0
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
export EGL_PLATFORM=wayland
export GDK_BACKEND=wayland
export QT_QPA_PLATFORM=wayland
export SDL_VIDEODRIVER=wayland
export XDG_SESSION_TYPE=wayland
export XDG_CURRENT_DESKTOP=GNOME:Phosh
export XDG_SESSION_DESKTOP=phosh
export MOZ_ENABLE_WAYLAND=1

# 12. D-Bus de sistema (iniciado limpiamente sin LD_PRELOAD)
mkdir -p /run/dbus /var/run/dbus
if [ ! -e /run/dbus/system_bus_socket ]; then
    dbus-daemon --system --fork 2>/dev/null || true
fi

# 13. Cadena estricta LD_PRELOAD exclusivamente para el compositor Phoc
PHOC_PRELOAD="/usr/lib/libjemalloc.so.2 /usr/lib/libstdc++.so.6 /usr/lib/libseat_direct.so /usr/lib/libgpu_interface_driver.so /usr/lib/libmali-bifrost-patched.so"
unset LD_PRELOAD

# 14. Bucle Supervisor de Phoc + Phosh
while true; do
    rm -f $XDG_RUNTIME_DIR/wayland-* 2>/dev/null || true
    chmod 666 /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
    echo 15 > /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
    chmod 444 /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
    dbus-run-session -- env LD_PRELOAD="$PHOC_PRELOAD" /usr/bin/phoc -v -S -C /etc/phoc.ini -E "/usr/bin/phosh_wrapper.sh" >> /var/log/phosh_gpu.log 2>&1
    sleep 2
done