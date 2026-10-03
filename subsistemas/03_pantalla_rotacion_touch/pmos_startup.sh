#!/bin/sh
# ==============================================================================
# Script de Inicio Permanente, Autónomo y Supervisado de la Interfaz y Servicios
# UMIDIGI G7 Tab Pro (MT6789 / Helio G99) - postmarketOS Edge
# ==============================================================================

echo "=== [pmOS Startup] INICIANDO SISTEMA INTEGRAL Y SERVICIOS PERMANENTES ==="

# 1. Asegurar SHM, proc, sys, dev y Watchdog alimentado en prioridad de tiempo real máxima (SCHED_FIFO 99)
mkdir -p /dev/shm /sysroot/dev/shm /sysroot/tmp/runtime-danilo /sysroot/proc /sysroot/sys /sysroot/dev /sysroot/dev/pts /sysroot/run /sysroot/tmp
mount -t proc proc /sysroot/proc 2>/dev/null || true
mount -t sysfs sysfs /sysroot/sys 2>/dev/null || true
mount --bind /dev /sysroot/dev 2>/dev/null || true
mount -t devpts devpts /sysroot/dev/pts 2>/dev/null || true
mount -t tmpfs tmpfs /sysroot/run 2>/dev/null || true
mount -t tmpfs tmpfs /sysroot/tmp 2>/dev/null || true
mount -t tmpfs -o rw,nosuid,nodev,mode=1777 tmpfs /dev/shm 2>/dev/null || true
mount -t tmpfs -o rw,nosuid,nodev,mode=1777 tmpfs /sysroot/dev/shm 2>/dev/null || true
chmod 1777 /dev/shm /sysroot/dev/shm /sysroot/tmp 2>/dev/null || true
pgrep -x wdt_pet >/dev/null || /bin/wdt_pet &
for p in $(pidof wdt_pet); do
    chroot /sysroot /usr/bin/chrt -f -p 99 "$p" 2>/dev/null || true
done

# 2. Supervisor de brillo Anti-UVLO: Fijar nivel nominal seguro 70/255 (27%)
chmod 666 /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
echo 70 > /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
chmod 444 /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
if ! pgrep -x backlight_guard >/dev/null; then
    if [ -x /sysroot/bin/backlight_guard ]; then
        /sysroot/bin/backlight_guard &
    elif [ -x /bin/backlight_guard ]; then
        /bin/backlight_guard &
    fi
fi

# Bind mounts de backlight para que GNOME / Phosh lo reconozca
mkdir -p /tmp/fake_backlight/lcd-backlight
ln -sf /sys/class/leds/lcd-backlight/brightness /tmp/fake_backlight/lcd-backlight/brightness 2>/dev/null || true
ln -sf /sys/class/leds/lcd-backlight/brightness /tmp/fake_backlight/lcd-backlight/actual_brightness 2>/dev/null || true
ln -sf /sys/class/leds/lcd-backlight/max_brightness /tmp/fake_backlight/lcd-backlight/max_brightness 2>/dev/null || true
echo raw > /tmp/fake_backlight/lcd-backlight/type 2>/dev/null || true
mount --bind /tmp/fake_backlight /sys/class/backlight 2>/dev/null || true
mount --bind /tmp/fake_backlight /sysroot/sys/class/backlight 2>/dev/null || true

# 3. Cargar subsistema GPU Mali-G57 MC2 y fijar OPP 37 (Seguridad eléctrica Anti-UVLO: 390MHz @ 575mV)
if [ -x /sysroot/bin/load_mali_chain.sh ]; then
    /sysroot/bin/load_mali_chain.sh
elif [ -x /bin/load_mali_chain.sh ]; then
    /bin/load_mali_chain.sh
fi
echo 37 > /proc/gpufreqv2/fix_target_opp_index 2>/dev/null || true
chmod 666 /dev/mali0 /sysroot/dev/mali0 2>/dev/null || true

# 4. Cargar subsistema de Audio ALSA y 4x AW87359 de forma segura con modprobe
chroot /sysroot modprobe nvmem-mt635x-efuse 2>/dev/null || true
chroot /sysroot modprobe mt635x-auxadc 2>/dev/null || true
chroot /sysroot modprobe snd-soc-mtk-common 2>/dev/null || true
chroot /sysroot modprobe snd-soc-mt6789-afe 2>/dev/null || true
chroot /sysroot modprobe snd-soc-mt6366 2>/dev/null || true
chroot /sysroot modprobe mt6789-mt6366 2>/dev/null || true
chroot /sysroot modprobe snd-soc-aw87xxx 2>/dev/null || true
mkdir -p /dev/snd /sysroot/dev/snd
for d in /sys/class/sound/*; do
    if [ -f "$d/dev" ]; then
        sname=$(basename "$d")
        sdev=$(cat "$d/dev")
        mknod "/dev/snd/$sname" c ${sdev%%:*} ${sdev##*:} 2>/dev/null || true
        mknod "/sysroot/dev/snd/$sname" c ${sdev%%:*} ${sdev##*:} 2>/dev/null || true
    fi
done
chmod 755 /dev/snd /sysroot/dev/snd 2>/dev/null || true
chmod 666 /dev/snd/* /sysroot/dev/snd/* 2>/dev/null || true

# Calibración de Audio AFE y amplificadores AW87359 (0 dB nominal sin distorsión)
amixer -c 0 cset numid=211 1 2>/dev/null || true
amixer -c 0 cset numid=226 1 2>/dev/null || true
amixer -c 0 cset numid=27 32768 2>/dev/null || true
amixer -c 0 cset numid=299 12 2>/dev/null || true
amixer -c 0 cset numid=300 12 2>/dev/null || true
amixer -c 0 cset numid=325 2 2>/dev/null || true
amixer -c 0 cset numid=326 2 2>/dev/null || true
amixer -c 0 cset numid=319 1 2>/dev/null || true
for nid in 282 279 276 273; do
    amixer -c 0 cset numid=$nid 0 2>/dev/null || true
done

# Daemon de audio y eventos de teclas
if ! chroot /sysroot pgrep -f tablet_audio_daemon.py >/dev/null; then
    if [ -f /sysroot/usr/local/bin/tablet_audio_daemon.py ]; then
        nohup chroot /sysroot /usr/bin/python3 -u /usr/local/bin/tablet_audio_daemon.py >/sysroot/var/log/tablet_audio.log 2>&1 &
    elif [ -f /sysroot/usr/bin/tablet_audio_daemon.py ]; then
        nohup chroot /sysroot /usr/bin/python3 -u /usr/bin/tablet_audio_daemon.py >/sysroot/var/log/tablet_audio.log 2>&1 &
    fi
fi

# 5. Subsistema Wi-Fi MediaTek MT6631
echo 1 > /dev/wmtWifi 2>/dev/null || true
ip link set wlan0 up 2>/dev/null || true

# 6. Subsistema de Batería y AUXADC (Telemetría MT6366 OCV v4)
chroot /sysroot modprobe rtc-mt6397 2>/dev/null || insmod /lib/modules/rtc-mt6397.ko 2>/dev/null || true
chroot /sysroot modprobe mt635x-auxadc 2>/dev/null || insmod /lib/modules/mt635x-auxadc.ko 2>/dev/null || true
if ! chroot /sysroot pgrep -f tablet_battery_daemon.py >/dev/null; then
    nohup chroot /sysroot /usr/bin/python3 -u /usr/bin/tablet_battery_daemon.py >/sysroot/var/log/battery_daemon.log 2>&1 &
fi

# 7. D-Bus y UPower en sysroot
chroot /sysroot /bin/sh -c '
    mkdir -p /run/dbus /var/run/dbus
    if [ ! -e /run/dbus/system_bus_socket ]; then
        dbus-daemon --system --fork 2>/dev/null || true
    fi
    pgrep -x upowerd >/dev/null || /usr/libexec/upowerd >/dev/null 2>&1 &
'

# 8. Memoria compartida SHM crítica (Evita SIGSEGV en wlr_keyboard)
mkdir -p /dev/shm /sysroot/dev/shm
mount -t tmpfs -o rw,nosuid,nodev,mode=1777 tmpfs /dev/shm 2>/dev/null || true
mount -t tmpfs -o rw,nosuid,nodev,mode=1777 tmpfs /sysroot/dev/shm 2>/dev/null || true
chmod 1777 /dev/shm /sysroot/dev/shm 2>/dev/null || true

# 9. Directorio de runtime para usuario danilo
mkdir -p /sysroot/tmp/runtime-danilo /sysroot/var/log
chroot /sysroot chown -R danilo:danilo /tmp/runtime-danilo /home/danilo 2>/dev/null || true
chmod 777 /sysroot/tmp/runtime-danilo

# Perfil de pantalla Phoc con rotación nativa 180°
cat << 'PHOCEOF' > /sysroot/etc/phoc.ini
[core]
xwayland=false

[output:DSI-1]
scale=1.0
transform=180
rotate=180
PHOCEOF
cp -f /sysroot/etc/phoc.ini /etc/phoc.ini 2>/dev/null || true

# Script lanzador de Phosh de usuario
cat << 'LAUNCHEOF' > /sysroot/bin/launch_phosh_user.sh
#!/bin/sh
while [ ! -S /tmp/runtime-danilo/wayland-0 ]; do
    sleep 0.2
done
chmod 777 /tmp/runtime-danilo /tmp/runtime-danilo/wayland-* 2>/dev/null || true
mkdir -p /tmp/runtime-danilo/dconf
chown -R danilo:danilo /tmp/runtime-danilo 2>/dev/null || true

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
    # Iniciar PipeWire y WirePlumber de usuario de forma unica
    pgrep -u danilo -x pipewire >/dev/null || nohup pipewire </dev/null >/dev/null 2>&1 &
    pgrep -u danilo -x wireplumber >/dev/null || nohup wireplumber </dev/null >/dev/null 2>&1 &

    pgrep -u danilo -x phosh >/dev/null || exec /usr/libexec/phosh -U
'
LAUNCHEOF
chmod +x /sysroot/bin/launch_phosh_user.sh

# 10. Lanzamiento Ordenado y Limpio de Phoc + Phosh UI (Una sola instancia)
rm -f /sysroot/tmp/runtime-danilo/wayland-*

# Proteger siempre brillo y frecuencia antes de compositor
chmod 666 /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
echo 70 > /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
chmod 444 /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
echo 37 > /proc/gpufreqv2/fix_target_opp_index 2>/dev/null || true

# Lanzar lanzador de Phosh de usuario (esperará al socket wayland-0)
if ! chroot /sysroot pgrep -f launch_phosh_user >/dev/null 2>&1; then
    nohup chroot /sysroot /bin/sh /bin/launch_phosh_user.sh </dev/null >/sysroot/var/log/phosh_user.log 2>&1 &
fi

# Lanzar compositor Phoc desacoplado con nohup
if ! chroot /sysroot pgrep -x phoc >/dev/null 2>&1; then
    nohup chroot /sysroot /bin/sh -c '
        export HOME=/home/danilo
        export USER=danilo
        export XDG_RUNTIME_DIR=/tmp/runtime-danilo
        export WLR_BACKENDS=drm,libinput
        export WLR_NO_HARDWARE_CURSORS=1
        export WLR_DRM_NO_MODIFIERS=1
        export WLR_RENDERER=pixman
        export WLR_RENDERER_ALLOW_SOFTWARE=1
        export LP_NUM_THREADS=4
        LD_PRELOAD=/usr/lib/libseat_direct.so /usr/bin/phoc -C /etc/phoc.ini
    ' </dev/null >>/sysroot/var/log/phoc_live.log 2>&1 &
fi

echo "=== [pmOS Startup] PIPELINE DE INICIO PERMANENTE COMPLETADO ==="
