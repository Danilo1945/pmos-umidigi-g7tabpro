#!/bin/sh
echo ">> [pmOS-v14] Iniciando entorno GNOME / Phosh con Aceleracion GLES2, Bateria, Audio y Brillo..."

MODDIR=/lib/modules/5.10.185-android12-9-gcc7d44e69095-dirty

# 1. Cargar Módulos de Batería PMIC MT6366
insmod /lib/modules/rtc-mt6397.ko 2>/dev/null || insmod $MODDIR/rtc-mt6397.ko 2>/dev/null || true
insmod /lib/modules/mt635x-auxadc.ko 2>/dev/null || insmod $MODDIR/mt635x-auxadc.ko 2>/dev/null || true
insmod /lib/modules/charger_class.ko 2>/dev/null || insmod $MODDIR/charger_class.ko 2>/dev/null || true
insmod /lib/modules/tcpc_mt6375.ko 2>/dev/null || insmod $MODDIR/tcpc_mt6375.ko 2>/dev/null || true
insmod /lib/modules/mt6375-charger.ko 2>/dev/null || insmod $MODDIR/mt6375-charger.ko 2>/dev/null || true
insmod /lib/modules/pd_dbg_info.ko 2>/dev/null || insmod $MODDIR/pd_dbg_info.ko 2>/dev/null || true
insmod /lib/modules/tcpc_class.ko 2>/dev/null || insmod $MODDIR/tcpc_class.ko 2>/dev/null || true
insmod /lib/modules/mtk_chg_type_det.ko 2>/dev/null || insmod $MODDIR/mtk_chg_type_det.ko 2>/dev/null || true
insmod /lib/modules/mt6358_battery.ko 2>/dev/null || insmod $MODDIR/mt6358_battery.ko 2>/dev/null || true
udevadm trigger --subsystem-match=platform 2>/dev/null || true

# Iniciar monitor de batería (preferir tablet_battery_daemon.py con audio, bind mount y OCV)
if ! pgrep -f tablet_battery_daemon.py >/dev/null; then
    if [ -f /usr/bin/tablet_battery_daemon.py ]; then
        python3 -u /usr/bin/tablet_battery_daemon.py > /var/log/battery_daemon.log 2>&1 &
    elif [ -x /usr/bin/battery_daemon ]; then
        /usr/bin/battery_daemon >/dev/null 2>&1 &
    fi
fi

# 2. Cargar Módulos de Audio y DSP
for m in nvmem-mt635x-efuse scp adsp mtk-afe-external mtk-scp-audiocommon audio_ipi \
         snd-soc-audiodsp-common mtk-scp-audio mtk-scp-ultra snd-soc-mtk-scp-ultra \
         snd-soc-mtk-common snd-soc-aw87xxx mtk-sp-spk-amp mt6358-accdet mtk-btcvsd \
         snd-soc-mt6789-afe snd-soc-mt6366 mt6789-mt6366; do
    insmod /lib/modules/$m.ko 2>/dev/null || insmod $MODDIR/$m.ko 2>/dev/null || true
done

# Crear nodos de audio /dev/snd
mkdir -p /dev/snd /sysroot/dev/snd
for d in /sys/class/sound/*; do
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

# Configurar Mixers ALSA para 4 altavoces AW87359
amixer -c 0 cset numid=211 1 2>/dev/null || true
amixer -c 0 cset numid=226 1 2>/dev/null || true
amixer -c 0 cset numid=27 63311 2>/dev/null || true
amixer -c 0 cset numid=299 18 2>/dev/null || true
amixer -c 0 cset numid=325 2 2>/dev/null || true
amixer -c 0 cset numid=326 2 2>/dev/null || true
amixer -c 0 cset numid=287 0 2>/dev/null || true
amixer -c 0 cset numid=319 1 2>/dev/null || true
for numid in 282 279 276 273; do
    amixer -c 0 cset numid=$numid 0 2>/dev/null || true
done

# 3. Cargar Módulos de GPU ARM Mali-G57 MC2 (Cadena de Dependencias Topológica Validada en Silicio)
for m in nvmem_mtk-devinfo mcupm fhctl mtk_gpu_hal sspm_v3 mtk_qos mtk-ssc gpueb \
         mtk_bp_thl mtk_battery_oc_throttling mtk_gpufreq_wrapper mtk_gpu_qos \
         mtk_gpufreq_mt6789 ged mali_mgm_mt6789 mali_kbase_mt6789; do
    insmod /lib/modules/$m.ko 2>/dev/null || insmod $MODDIR/$m.ko 2>/dev/null || true
done
modprobe mali_kbase_mt6789 2>/dev/null || true
mknod /dev/mali0 c 10 116 2>/dev/null || true
mknod /sysroot/dev/mali0 c 10 116 2>/dev/null || true
chmod 666 /dev/mali0 /sysroot/dev/mali0 2>/dev/null || true

# 4. Asegurar SHM y nodos de input
mkdir -p /dev/shm /sysroot/dev/shm /tmp/runtime-danilo
mount -t tmpfs none /dev/shm 2>/dev/null || true
mount -t tmpfs none /sysroot/dev/shm 2>/dev/null || true
chmod 1777 /dev/shm /sysroot/dev/shm 2>/dev/null || true

chmod -R 666 /dev/input /sysroot/dev/input 2>/dev/null || true
chown -R 10000:23 /sysroot/dev/input 2>/dev/null || true

# 5. Entorno de usuario danilo
export HOME=/home/danilo
export USER=danilo
export XDG_RUNTIME_DIR=/tmp/runtime-danilo
export PULSE_SERVER="unix:/tmp/runtime-danilo/pulse/native"
export PULSE_COOKIE="/home/danilo/.config/pulse/cookie"
chmod 777 $XDG_RUNTIME_DIR 2>/dev/null || true
chown -R danilo:danilo $XDG_RUNTIME_DIR /home/danilo 2>/dev/null || true

# Iniciar bus de sistema D-Bus, udevd y NetworkManager
mkdir -p /run/dbus /var/run/dbus
if [ ! -e /run/dbus/system_bus_socket ]; then
    dbus-daemon --system --fork 2>/dev/null || true
fi

if ! pgrep -x udevd >/dev/null; then
    /usr/sbin/udevd --daemon 2>/dev/null || true
    udevadm trigger --subsystem-match=net 2>/dev/null || true
    udevadm trigger --subsystem-match=sound 2>/dev/null || true
fi

if ! pgrep -x NetworkManager >/dev/null; then
    NetworkManager 2>/dev/null || true
fi

eval $(dbus-launch --sh-syntax)
export DBUS_SYSTEM_BUS_ADDRESS="unix:path=/run/dbus/system_bus_socket"

# Iniciar upowerd para reporte de batería a Phosh
if ! pgrep -x upowerd >/dev/null; then
    /usr/libexec/upowerd >/dev/null 2>&1 &
fi

# Iniciar PipeWire y WirePlumber
su danilo -c "
    export XDG_RUNTIME_DIR=/tmp/runtime-danilo
    export DBUS_SESSION_BUS_ADDRESS='$DBUS_SESSION_BUS_ADDRESS'
    export DBUS_SYSTEM_BUS_ADDRESS='unix:path=/run/dbus/system_bus_socket'
    if ! pgrep -x pipewire >/dev/null; then
        pipewire >/tmp/pipewire.log 2>&1 &
    fi
    if ! pgrep -x wireplumber >/dev/null; then
        wireplumber >/tmp/wireplumber.log 2>&1 &
    fi
    if ! pgrep -x pipewire-pulse >/dev/null; then
        pipewire-pulse >/tmp/pipewire-pulse.log 2>&1 &
    fi
" 2>/dev/null || true

# Iniciar tablet_audio_daemon (Audio 4x AW87359 Hi-Fi, Teclas Volumen, Jack 3.5mm, Botón Power y Rotación D-Bus)
if ! pgrep -f tablet_audio_daemon.py >/dev/null; then
    if [ -f /usr/bin/tablet_audio_daemon.py ]; then
        python3 -u /usr/bin/tablet_audio_daemon.py > /var/log/tablet_audio_daemon.log 2>&1 &
    elif [ -f /bin/tablet_audio_daemon.py ]; then
        python3 -u /bin/tablet_audio_daemon.py > /var/log/tablet_audio_daemon.log 2>&1 &
    fi
fi

# Configurar perfil de pantalla Phoc (Orientación 180 y escala 1.0)
cat << 'PHOCEOF' > /etc/phoc.ini
[core]
xwayland=false

[output:DSI-1]
scale=1.0
rotate=180
PHOCEOF


# Control de brillo: permisos y nivel óptimo (70% - 180/255)
chmod 666 /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
echo 180 > /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true

# Asegurar SHM en el chroot
mkdir -p /dev/shm
mount -t tmpfs none /dev/shm 2>/dev/null || true
chmod 1777 /dev/shm 2>/dev/null || true

export WLR_BACKENDS=drm,libinput
export WLR_NO_HARDWARE_CURSORS=1
export WLR_DRM_NO_MODIFIERS=1
export WLR_RENDERER=pixman
export WLR_RENDERER_ALLOW_SOFTWARE=1
export LP_NUM_THREADS=8
export GDK_BACKEND=wayland
export QT_QPA_PLATFORM=wayland
export SDL_VIDEODRIVER=wayland
export XDG_SESSION_TYPE=wayland
export XDG_CURRENT_DESKTOP=GNOME:Phosh
export XDG_SESSION_DESKTOP=phosh
export MOZ_ENABLE_WAYLAND=1

# Forzar orientación 180 manteniendo salida encendida una vez levantado Wayland
(sleep 4; WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/tmp/runtime-danilo /usr/bin/wlr-randr --output DSI-1 --on --mode 1200x1920 --transform 180 2>/dev/null || true) &

while true; do
    rm -f $XDG_RUNTIME_DIR/wayland-*
    echo "[+] Levantando Phoc + Phosh -U con dbus-run-session..."
    LD_PRELOAD=/usr/lib/libseat_direct.so dbus-run-session -- phoc -C /etc/phoc.ini -E "/usr/libexec/phosh -U" > /var/log/phosh_session.log 2>&1
    echo "[-] Phoc finalizo. Reiniciando en 3 segundos..."
    sleep 3
done
