#!/bin/sh
# ==============================================================================
# Script Maestro Supervisor Integral: Phoc + Phosh + Todos los Subsistemas
# Renderizador: GPU ARM Mali-G57 MC2 (GLES2 + libgpu_interface_driver.so + NEON)
# Plataforma: UMIDIGI G7 Tab Pro (TG2403GBA / MediaTek Helio G99 MT6789)
# postmarketOS Edge / Linux 5.10.185-android12 aarch64 (musl libc)
# ==============================================================================

# 1. Sanitización de procesos residuales y liberación de DRM Master
mkdir -p /var/log /sysroot/var/log 2>/dev/null || true
killall -9 drm_splash panel_pwm_force feedbackd callaudiod dconf-service at-spi-bus-launcher at-spi2-registryd backlight_lock backlight_guard 2>/dev/null || true
pkill -f "capture_touch" 2>/dev/null || true

# Inhabilitar feedbackd permanentemente para evitar cuelgues de D-Bus en eventos táctiles
chmod 000 /usr/libexec/feedbackd /sysroot/usr/libexec/feedbackd 2>/dev/null || true
rm -f /sysroot/usr/share/dbus-1/services/org.sigxcpu.Feedback.service /usr/share/dbus-1/services/org.sigxcpu.Feedback.service 2>/dev/null || true

# Fijar CPU en performance en todos los núcleos (Cortex-A76 @ 2.2 GHz + Cortex-A55 @ 2.0 GHz)
for gov in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    echo performance > "$gov" 2>/dev/null || true
done

# 2. Liberar bind-mounts estáticos de brillo y fijar nivel inicial seguro anti-UVLO (15/255)
umount -l /sys/devices/platform/soc/soc:mtk_leds/leds/lcd-backlight/brightness 2>/dev/null || true
umount -l /sysroot/sys/devices/platform/soc/soc:mtk_leds/leds/lcd-backlight/brightness 2>/dev/null || true
umount -l /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
chmod 666 /sys/devices/platform/soc/soc:mtk_leds/leds/lcd-backlight/brightness /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
echo 15 > /sys/devices/platform/soc/soc:mtk_leds/leds/lcd-backlight/brightness 2>/dev/null || true
echo 15 > /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true

# 3. Watchdog de silicio activo
if ! pgrep -f wdt_pet >/dev/null 2>&1; then
    [ -x /bin/wdt_pet ] && /bin/wdt_pet >/dev/null 2>&1 &
    [ -x /usr/bin/wdt_pet ] && /usr/bin/wdt_pet >/dev/null 2>&1 &
    [ -x /sysroot/usr/bin/wdt_pet ] && /sysroot/usr/bin/wdt_pet >/dev/null 2>&1 &
fi

# 3.1 Cargar cadena de GPU Mali-G57 si el módulo mali_kbase no está cargado
if ! grep -q "mali_kbase" /proc/modules 2>/dev/null; then
    [ -x /sysroot/usr/bin/load_mali_chain.sh ] && /sysroot/usr/bin/load_mali_chain.sh || true
    [ -x /usr/bin/load_mali_chain.sh ] && /usr/bin/load_mali_chain.sh || true
fi
chmod 666 /dev/mali0 /sysroot/dev/mali0 /dev/dma_heap/* /sysroot/dev/dma_heap/* 2>/dev/null || true

# Configuración de alto rendimiento para GPU Mali-G57 (812 MHz - 1.1 GHz / OPP 15)
if [ -d /proc/gpufreqv2 ]; then
    echo 15 > /proc/gpufreqv2/fix_target_opp_index 2>/dev/null || true
fi
for mg in /sys/class/devfreq/*.mali/governor; do
    [ -f "$mg" ] && echo performance > "$mg" 2>/dev/null || true
done
for mf in /sys/class/devfreq/*.mali/min_freq; do
    [ -f "$mf" ] && echo 812000000 > "$mf" 2>/dev/null || true
done

# 3.2 Cargar driver táctil Chipone TDDI parcheado
if [ ! -e /dev/input/event2 ]; then
    rmmod tddi_9551 2>/dev/null || true
    insmod /sysroot/lib/modules/tddi_9551_patched.ko 2>/dev/null || \
    insmod /lib/modules/tddi_9551_patched.ko 2>/dev/null || \
    insmod /lib/modules/5.10.185-android12-9-gcc7d44e69095-dirty/tddi_9551.ko 2>/dev/null || true
    mdev -s 2>/dev/null || true
fi

# 3.3 Cargar cadena de módulos ALSA Audio (MT6789 AFE + MT6366 + 4x AW87359) si no hay tarjeta registrada
if ! grep -q "mt6789" /proc/asound/cards 2>/dev/null; then
    for am in nvmem-mt635x-efuse mt635x_auxadc mt6358_battery mtk-afe-external scp adsp \
              mtk-scp-audiocommon audio_ipi snd-soc-audiodsp-common mtk-scp-audio \
              mtk-sp-spk-amp snd-soc-mtk-common mtk-scp-ultra snd-soc-mtk-scp-ultra \
              snd-soc-aw87xxx snd-soc-mt6789-afe mt6358-accdet mtk-btcvsd \
              snd-soc-mt6366 mt6789-mt6366; do
        modprobe "$am" 2>/dev/null || \
        insmod "/lib/modules/5.10.185-android12-9-gcc7d44e69095-dirty/$am.ko" 2>/dev/null || \
        insmod "/sysroot/lib/modules/5.10.185-android12-9-gcc7d44e69095-dirty/$am.ko" 2>/dev/null || \
        insmod "/lib/modules/$am.ko" 2>/dev/null || true
    done
    for i in $(seq 1 30); do
        grep -q "mt6789" /proc/asound/cards 2>/dev/null && break
        sleep 0.1
    done
fi
mkdir -p /dev/snd /sysroot/dev/snd 2>/dev/null || true
for d in /sys/class/sound/*; do
    [ -e "$d" ] || continue
    sname=$(basename "$d")
    if [ -f "$d/dev" ]; then
        sdev=$(cat "$d/dev")
        smaj=${sdev%%:*}
        smin=${sdev##*:}
        mknod "/dev/snd/$sname" c "$smaj" "$smin" 2>/dev/null || true
        mknod "/sysroot/dev/snd/$sname" c "$smaj" "$smin" 2>/dev/null || true
    fi
done
chmod 755 /dev/snd /sysroot/dev/snd 2>/dev/null || true
chmod 666 /dev/snd/* /sysroot/dev/snd/* 2>/dev/null || true

# 3.4 Cargar periféricos adicionales seguros (MicroSD mmc0, Linterna AW36515, Vibrador, BT/GPS/FM, AUXADC)
for pm in mt6577_auxadc mt6375-auxadc \
          cqhci rpmb-mtk mtk-mmc-autok mtk-mmc-dbg mtk-mmc \
          regulator-vibrator mtk-pwm \
          flashlight aw36515 \
          bt_drv_connac1x gps_pwr gps_drv_stp fmradio_drv_mt6631_6635; do
    modprobe "$pm" 2>/dev/null || \
    insmod "/lib/modules/5.10.185-android12-9-gcc7d44e69095-dirty/$pm.ko" 2>/dev/null || \
    insmod "/sysroot/lib/modules/5.10.185-android12-9-gcc7d44e69095-dirty/$pm.ko" 2>/dev/null || \
    insmod "/lib/modules/$pm.ko" 2>/dev/null || true
done
rm -f /dev/stpbt /dev/stpgps /sysroot/dev/stpbt /sysroot/dev/stpgps 2>/dev/null || true
mknod -m 666 /dev/stpgps c 191 0 2>/dev/null || true
mknod -m 666 /dev/stpbt c 192 0 2>/dev/null || true
mknod -m 666 /sysroot/dev/stpgps c 191 0 2>/dev/null || true
mknod -m 666 /sysroot/dev/stpbt c 192 0 2>/dev/null || true
fm_maj=$(awk '$2=="fm" {print $1}' /proc/devices 2>/dev/null)
if [ -n "$fm_maj" ]; then
    rm -f /dev/fm /sysroot/dev/fm 2>/dev/null || true
    mknod -m 666 /dev/fm c "$fm_maj" 0 2>/dev/null || true
    mknod -m 666 /sysroot/dev/fm c "$fm_maj" 0 2>/dev/null || true
fi
chmod 666 /sys/class/flashlight_core/flashlight/flashlight_torch /sys/class/leds/vibrator/brightness 2>/dev/null || true

# 4. Montaje de /dev/shm tmpfs (128M) (Requerido por Wayland/wlroots xkb keymaps)
mkdir -p /dev/shm /sysroot/dev/shm 2>/dev/null || true
if ! grep -q ' /dev/shm ' /proc/mounts 2>/dev/null; then
    mount -t tmpfs -o mode=1777,size=128M shm /dev/shm 2>/dev/null || true
fi
if [ -d /sysroot/dev/shm ] && ! grep -q ' /sysroot/dev/shm ' /proc/mounts 2>/dev/null; then
    mount -t tmpfs -o mode=1777,size=128M shm /sysroot/dev/shm 2>/dev/null || true
fi
chmod 1777 /dev/shm /sysroot/dev/shm 2>/dev/null || true

# 5. Permisos y metadatos udev de dispositivos de entrada (calibración 1:1 para rotación 180°)
chmod 666 /dev/dri/* /sysroot/dev/dri/* 2>/dev/null || true
chmod 666 /dev/input/* /sysroot/dev/input/* 2>/dev/null || true

mkdir -p /run/udev/data /sysroot/run/udev/data /etc/udev/rules.d /sysroot/etc/udev/rules.d 2>/dev/null || true
cat << 'UDEVEOF' > /run/udev/data/c13:66
I:3249649026
E:DEVNAME=/dev/input/event2
E:DEVPATH=/devices/platform/soc/11e00000.i2c/i2c-0/0-0048/input/input2/event2
E:MAJOR=13
E:MINOR=66
E:SUBSYSTEM=input
E:ID_INPUT=1
E:ID_INPUT_TOUCHSCREEN=1
E:ID_BUS=i2c
E:LIBINPUT_CALIBRATION_MATRIX=-1 0 1 0 -1 1
UDEVEOF
cp -f /run/udev/data/c13:66 /sysroot/run/udev/data/c13:66 2>/dev/null || true

cat << 'UDEVEOF' > /run/udev/data/c13:67
I:3249649027
E:DEVNAME=/dev/input/event3
E:DEVPATH=/devices/platform/soc/11e00000.i2c/i2c-0/0-0048/input/input3/event3
E:MAJOR=13
E:MINOR=67
E:SUBSYSTEM=input
E:ID_INPUT=1
E:ID_INPUT_TABLET=1
E:ID_BUS=i2c
E:LIBINPUT_CALIBRATION_MATRIX=-1 0 1 0 -1 1
UDEVEOF
cp -f /run/udev/data/c13:67 /sysroot/run/udev/data/c13:67 2>/dev/null || true

cat << 'UDEVEOF' > /etc/udev/rules.d/99-touchscreen.rules
ACTION=="add|change", KERNEL=="event[0-9]*", ATTRS{name}=="chipone_tddi", ENV{LIBINPUT_CALIBRATION_MATRIX}="-1 0 1 0 -1 1", ENV{ID_INPUT_TOUCHSCREEN}="1"
ACTION=="add|change", KERNEL=="event[0-9]*", ATTRS{name}=="chipone-tddi,pen", ENV{LIBINPUT_CALIBRATION_MATRIX}="-1 0 1 0 -1 1", ENV{ID_INPUT_TABLET}="1"
UDEVEOF
cp -f /etc/udev/rules.d/99-touchscreen.rules /sysroot/etc/udev/rules.d/99-touchscreen.rules 2>/dev/null || true

# 6. Configuración de phoc.ini (rotación 180° y deshabilitar Writeback)
mkdir -p /etc /sysroot/etc
PHOC_INI_TARGET="/etc/phoc.ini"
[ -d /sysroot/etc ] && PHOC_INI_TARGET="/sysroot/etc/phoc.ini"
cat << 'PHOCEOF' > "$PHOC_INI_TARGET"
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
[ -f /sysroot/etc/phoc.ini ] && cp -f /sysroot/etc/phoc.ini /etc/phoc.ini 2>/dev/null || true

# 7. Creación de phosh_wrapper.sh con aceleración GPU nativa (GSK_RENDERER=gl) y 60 FPS
mkdir -p /usr/bin /sysroot/usr/bin
WRAPPER_TARGET="/usr/bin/phosh_wrapper.sh"
[ -d /sysroot/usr/bin ] && WRAPPER_TARGET="/sysroot/usr/bin/phosh_wrapper.sh"
cat << 'WRAPPEREOF' > "$WRAPPER_TARGET"
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

# Compatibilidad estable para clientes GTK4 y Qt6/KDE en Wayland
export GSK_RENDERER=cairo
export QT_QUICK_BACKEND=software
export QTWEBENGINE_DISABLE_SANDBOX=1
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
WRAPPEREOF
chmod 755 "$WRAPPER_TARGET"
[ -f /sysroot/usr/bin/phosh_wrapper.sh ] && cp -f /sysroot/usr/bin/phosh_wrapper.sh /usr/bin/phosh_wrapper.sh 2>/dev/null || true

# 8. Autoejecución en entorno chroot de postmarketOS si se invoca desde initramfs
if [ ! -f /usr/bin/phoc ] && [ -f /sysroot/usr/bin/phoc ]; then
    # Si el Wi-Fi aún no se ha levantado desde el initramfs, dispararlo en segundo plano
    if ! ip link show wlan0 >/dev/null 2>&1; then
        [ -x /bin/wmt_wifi_start.sh ] && /bin/wmt_wifi_start.sh >/tmp/wmt_wifi_init.log 2>&1 &
    fi
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
    cp -f /bin/start_phosh.sh /sysroot/usr/bin/start_phosh.sh 2>/dev/null || true
    exec chroot /sysroot /usr/bin/start_phosh.sh "$@"
fi

# ==============================================================================
# DENTRO DEL CHROOT (/sysroot): INICIO DE TODOS LOS SUBSISTEMAS Y DEMONIOS
# ==============================================================================
chmod 755 /dev/input 2>/dev/null || true
chmod 666 /dev/input/* /dev/dri/* /dev/mali0 2>/dev/null || true

mkdir -p /tmp/runtime-root /var/log /run/dbus /var/run/dbus
chmod 700 /tmp/runtime-root 2>/dev/null || true

export HOME=/root
export USER=root
export XDG_RUNTIME_DIR=/tmp/runtime-root
export DBUS_SYSTEM_BUS_ADDRESS=unix:path=/run/dbus/system_bus_socket

# 9. Asegurar udevd (con regla para wlan0 MT6631) y D-Bus System Bus activos
mkdir -p /etc/udev/rules.d 2>/dev/null || true
cat << 'EOF_UDEV_RULE' > /etc/udev/rules.d/99-mtk-wifi.rules
SUBSYSTEM=="net", KERNEL=="wlan*", ENV{ID_NET_DRIVER}="wlan", ENV{DEVTYPE}="wlan", ENV{SYSTEMD_READY}="1", TAG+="systemd"
EOF_UDEV_RULE
if ! pgrep -x udevd >/dev/null 2>&1; then
    /usr/bin/udevd --daemon 2>/dev/null || /sbin/udevd --daemon 2>/dev/null || /usr/sbin/udevd --daemon 2>/dev/null || true
else
    udevadm control --reload-rules 2>/dev/null || true
fi
if ! pgrep -x dbus-daemon >/dev/null 2>&1; then
    rm -f /run/dbus/dbus.pid /var/run/dbus/dbus.pid 2>/dev/null || true
    dbus-daemon --system --fork 2>/dev/null || true
fi

# 10. Subsistema 04: Conectividad Wi-Fi (wpa_supplicant + NetworkManager) y Bluetooth (BlueZ)
if ip link show wlan0 >/dev/null 2>&1; then
    ip link set wlan0 up 2>/dev/null || true
    if [ -f /sys/class/net/wlan0/ifindex ]; then
        wlan_ifidx=$(cat /sys/class/net/wlan0/ifindex 2>/dev/null)
        mkdir -p /run/udev/data 2>/dev/null || true
        cat << EOF_UDEV_N > "/run/udev/data/n$wlan_ifidx"
I:243319448
E:DEVPATH=/devices/platform/soc/18000000.wifi/net/wlan0
E:DEVTYPE=wlan
E:ID_NET_DRIVER=wlan
E:IFINDEX=$wlan_ifidx
E:INTERFACE=wlan0
E:SUBSYSTEM=net
E:SYSTEMD_READY=1
E:USEC_INITIALIZED=243319448
TAGS=:systemd:
EOF_UDEV_N
    fi
fi
if ! pgrep -x wpa_supplicant >/dev/null 2>&1; then
    mkdir -p /etc/wpa_supplicant /run/wpa_supplicant 2>/dev/null || true
    [ ! -f /etc/wpa_supplicant/wpa_supplicant.conf ] && printf "ctrl_interface=/run/wpa_supplicant\nctrl_interface_group=0\nupdate_config=1\n" > /etc/wpa_supplicant/wpa_supplicant.conf
    /usr/sbin/wpa_supplicant -u -B -c /etc/wpa_supplicant/wpa_supplicant.conf 2>/dev/null || true
fi
if ! pgrep -x NetworkManager >/dev/null 2>&1; then
    /usr/sbin/NetworkManager 2>/dev/null &
fi
if [ -f /usr/bin/tablet_bt_daemon.py ] && ! pgrep -f tablet_bt_daemon.py >/dev/null 2>&1; then
    python3 -u /usr/bin/tablet_bt_daemon.py > /var/log/tablet_bt.log 2>&1 &
fi

# 11. Subsistema 02: Batería, Cargador Sub-PMIC MT6375, Telemetría ADC/OCV y Proveedor UPower
for bm in mt635x_auxadc mt6375-auxadc charger_class mt6375-charger mt6358_battery; do
    modprobe "$bm" 2>/dev/null || insmod "/lib/modules/5.10.185-android12-9-gcc7d44e69095-dirty/$bm.ko" 2>/dev/null || true
done
pkill -9 -f tablet_battery_daemon.py 2>/dev/null || true
killall -9 battery_daemon 2>/dev/null || true
if [ -f /usr/bin/upower_battery_provider.py ] && ! pgrep -f upower_battery_provider.py >/dev/null 2>&1; then
    killall -9 upowerd 2>/dev/null || true
    python3 -u /usr/bin/upower_battery_provider.py > /var/log/upower_provider.log 2>&1 &
fi

# 12. Subsistema 01: Audio (PipeWire + WirePlumber + 4x Amplificadores AW87359)
for i in $(seq 1 30); do
    grep -q "mt6789" /proc/asound/cards 2>/dev/null && break
    sleep 0.1
done
mkdir -p /dev/snd 2>/dev/null || true
for d in /sys/class/sound/*; do
    [ -e "$d" ] || continue
    sname=$(basename "$d")
    if [ -f "$d/dev" ]; then
        sdev=$(cat "$d/dev")
        smaj=${sdev%%:*}
        smin=${sdev##*:}
        mknod "/dev/snd/$sname" c "$smaj" "$smin" 2>/dev/null || true
    fi
done
chmod 755 /dev/snd 2>/dev/null || true
chmod 666 /dev/snd/* 2>/dev/null || true
rm -f /etc/pipewire/pipewire.conf.d/00-disable-portal.conf /etc/pipewire/pipewire.conf.d/10-null.conf 2>/dev/null || true
if ! pgrep -x pipewire >/dev/null 2>&1; then
    rm -f /tmp/runtime-root/pipewire-0*.lock /tmp/runtime-root/pulse/native* 2>/dev/null || true
    XDG_RUNTIME_DIR=/tmp/runtime-root DBUS_SYSTEM_BUS_ADDRESS=unix:path=/run/dbus/system_bus_socket /usr/bin/pipewire > /var/log/pipewire.log 2>&1 &
    sleep 0.3
fi
if ! pgrep -x wireplumber >/dev/null 2>&1; then
    XDG_RUNTIME_DIR=/tmp/runtime-root DBUS_SYSTEM_BUS_ADDRESS=unix:path=/run/dbus/system_bus_socket /usr/bin/wireplumber > /var/log/wireplumber.log 2>&1 &
fi
if ! pgrep -x pipewire-pulse >/dev/null 2>&1; then
    XDG_RUNTIME_DIR=/tmp/runtime-root DBUS_SYSTEM_BUS_ADDRESS=unix:path=/run/dbus/system_bus_socket /usr/bin/pipewire-pulse > /var/log/pipewire-pulse.log 2>&1 &
fi
if [ -f /usr/bin/tablet_audio_daemon.py ] && ! pgrep -f tablet_audio_daemon.py >/dev/null 2>&1; then
    python3 -u /usr/bin/tablet_audio_daemon.py > /var/log/tablet_audio.log 2>&1 &
elif [ -f /usr/local/bin/tablet_audio_daemon.py ] && ! pgrep -f tablet_audio_daemon.py >/dev/null 2>&1; then
    python3 -u /usr/local/bin/tablet_audio_daemon.py > /var/log/tablet_audio.log 2>&1 &
fi

# 13. Subsistema 03: Botón Físico Power, Suspensión D-Bus login1, Brillo y Volumen
HOME=/root dconf write /sm/puri/phosh/enable-suspend true 2>/dev/null || true
if [ -f /usr/bin/tablet_power_daemon.py ] && ! pgrep -f tablet_power_daemon.py >/dev/null 2>&1; then
    python3 -u /usr/bin/tablet_power_daemon.py > /var/log/tablet_power.log 2>&1 &
    for i in $(seq 1 25); do
        if gdbus call --system --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus --method org.freedesktop.DBus.GetNameOwner org.freedesktop.login1 >/dev/null 2>&1; then
            break
        fi
        sleep 0.1
    done
fi

# 14. Variables de entorno Wayland y Renderizador GPU GLES2 (ARM Mali-G57)
export WLR_RENDERER=gles2
export WLR_RENDERER_ALLOW_SOFTWARE=0
export WLR_BACKENDS=drm,libinput
export WLR_DRM_DEVICES=/dev/dri/card0
export WLR_NO_HARDWARE_CURSORS=1
export WLR_DRM_NO_MODIFIERS=1
export WLR_DRM_NO_ATOMIC=1

export GDK_BACKEND=wayland
export GSK_RENDERER=cairo
export QT_QPA_PLATFORM=wayland
export QT_QUICK_BACKEND=software
export SDL_VIDEODRIVER=wayland
export XDG_SESSION_TYPE=wayland
export XDG_CURRENT_DESKTOP=GNOME:Phosh
export XDG_SESSION_DESKTOP=phosh
export MOZ_ENABLE_WAYLAND=1
export NO_AT_BRIDGE=1
unset GDK_DEBUG

PHOC_PRELOAD="/usr/lib/libjemalloc.so.2 /usr/lib/libseat_direct.so /usr/lib/libgpu_interface_driver.so"
unset LD_PRELOAD

# 15. Bucle Supervisor de Alta Disponibilidad para Phoc + Phosh Acelerado por GPU
while true; do
    echo "[supervisor] Iniciando Phoc con GPU Mali-G57 + Phosh en pantalla DSI-1..." >> /var/log/phosh_supervisor.log
    rm -f $XDG_RUNTIME_DIR/wayland-* 2>/dev/null || true
    dbus-run-session -- env LD_PRELOAD="$PHOC_PRELOAD" /usr/bin/phoc -v -S -C /etc/phoc.ini -E "/usr/bin/phosh_wrapper.sh" >> /var/log/phosh_gpu.log 2>&1
    echo "[supervisor] Phoc finalizó (código $?). Reiniciando sesión en 2 segundos..." >> /var/log/phosh_supervisor.log
    sleep 2
done
