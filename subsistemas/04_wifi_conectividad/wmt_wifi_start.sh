#!/bin/sh
# ==============================================================================
# Secuencia de Inicialización Wi-Fi MediaTek MT6631 (WMT + STP + gen4m)
# UMIDIGI G7 Tab Pro (MT6789) - postmarketOS Edge aarch64
# ==============================================================================
echo "=========================================================="
echo "  Inicializando Wi-Fi MediaTek MT6631 (WMT + STP + gen4m) "
echo "=========================================================="

# 1. Nodos de comunicacion estaticos
[ ! -c /dev/stpwmt ] && mknod -m 666 /dev/stpwmt c 190 0
[ ! -c /dev/wmtdetect ] && mknod -m 666 /dev/wmtdetect c 154 0
[ ! -c /dev/wmtWifi ] && mknod -m 666 /dev/wmtWifi c 508 0
[ ! -c /dev/stpbt ] && mknod -m 666 /dev/stpbt c 191 0
[ ! -c /dev/stpgps ] && mknod -m 666 /dev/stpgps c 192 0
[ ! -c /dev/fm ] && mknod -m 666 /dev/fm c 193 0
chmod 666 /dev/stpwmt /dev/wmtdetect /dev/wmtWifi /dev/stpbt /dev/stpgps /dev/fm 2>/dev/null || true

# 2. Enlaces esperados por el firmware
mkdir -p /vendor/firmware /system/etc/firmware /system/vendor/firmware /data/vendor/stp_dump /data/vendor/connsyslog /proc/driver 2>/dev/null || true
for fw in /lib/firmware/*; do
    [ -e "$fw" ] || continue
    fname=$(basename "$fw")
    [ ! -e "/vendor/firmware/$fname" ] && ln -sf "$fw" "/vendor/firmware/$fname" 2>/dev/null || true
    [ ! -e "/system/etc/firmware/$fname" ] && ln -sf "$fw" "/system/etc/firmware/$fname" 2>/dev/null || true
    [ ! -e "/system/vendor/firmware/$fname" ] && ln -sf "$fw" "/system/vendor/firmware/$fname" 2>/dev/null || true
done
[ ! -e /lib/firmware/WMT.cfg ] && ln -sf /lib/firmware/WMT_SOC.cfg /lib/firmware/WMT.cfg 2>/dev/null || true
[ ! -e /vendor/firmware/WMT.cfg ] && ln -sf /lib/firmware/WMT_SOC.cfg /vendor/firmware/WMT.cfg 2>/dev/null || true

# 3. Asegurar driver base WMT cargado
load_m() {
    local m="$1"
    local base="${m%.ko}"
    base="${base//-/_}"
    /sbin/modprobe "$base" 2>/dev/null || \
    insmod "/lib/modules/$m" 2>/dev/null || \
    insmod "/lib/modules/5.10.185-android12-9-gcc7d44e69095-dirty/$m" 2>/dev/null || true
}
load_m cfg80211.ko
load_m connadp.ko
load_m btif_drv.ko
load_m wmt_drv.ko

# 4. Localizar bundle android_compat si existe
COMPAT="/android_compat"
[ ! -d "$COMPAT" ] && [ -d "/sysroot/android_compat" ] && COMPAT="/sysroot/android_compat"

# 4. Inicializacion de hardware y deteccion de chip MT6631
if [ -x /usr/bin/wmt_init_complete ] || [ -x /bin/wmt_init_complete ]; then
    echo "[1/4] Ejecutando wmt_init_complete (MT6631 / MT6789)..."
    wmt_init_cmd="/usr/bin/wmt_init_complete"
    [ ! -x "$wmt_init_cmd" ] && wmt_init_cmd="/bin/wmt_init_complete"
    $wmt_init_cmd 0x6631 >/tmp/wmt_init_complete.log 2>&1 || true
    echo "    -> wmt_init_complete finalizado (ret=$?)"
fi

# 5. Lanzar wmt_launcher oficial si no está ya en ejecución (NUNCA matarlo si ya está activo para evitar reset de EMI)
if [ -x "$COMPAT/bin/linker64" ] && [ -x "$COMPAT/bin/wmt_launcher" ]; then
    if ! pgrep -f wmt_launcher >/dev/null 2>&1; then
        echo "[2/4] Lanzando daemon wmt_launcher..."
        LD_LIBRARY_PATH="$COMPAT/lib64" "$COMPAT/bin/linker64" "$COMPAT/bin/wmt_launcher" -p /lib/firmware/ -o 1 >/tmp/wmt_launcher.log 2>&1 &
        sleep 2
    else
        echo "[2/4] Daemon wmt_launcher ya está activo y protegido."
    fi
fi

# 6. Cargar controladores de interfaz Wi-Fi
echo "[3/4] Cargando módulos de red Wi-Fi (wmt_chrdev_wifi + wlan_drv)..."
load_m wmt_chrdev_wifi.ko
sleep 0.5

# Asegurar creacion de nodo /dev/wmtWifi con major dinamico real
if [ -f /sys/class/wmtWifi/wmtWifi/dev ]; then
    maj_min=$(cat /sys/class/wmtWifi/wmtWifi/dev 2>/dev/null)
    rm -f /dev/wmtWifi
    mknod -m 666 /dev/wmtWifi c ${maj_min%:*} ${maj_min#*:} 2>/dev/null || true
else
    [ ! -c /dev/wmtWifi ] && mknod -m 666 /dev/wmtWifi c 504 0 2>/dev/null || true
fi
chmod 666 /dev/wmtWifi 2>/dev/null || true

load_m wlan_drv_gen4m_6789.ko
sleep 1.5

# 7. Encender radio Wi-Fi vía CharDev
echo "[4/4] Encendiendo radio Wi-Fi (/dev/wmtWifi)..."
echo 1 > /dev/wmtWifi 2>/dev/null || true
sleep 2.5

# 8. Verificar interfaz wlan0 e integrar con NetworkManager / udev
if ip link show wlan0 >/dev/null 2>&1; then
    echo "🎉 ¡INTERFAZ wlan0 ACTIVA Y DISPONIBLE!"
    ip link set wlan0 up 2>/dev/null || true
    ip link show wlan0

    # Auto-registro en udev para que NetworkManager lo administre
    if [ -f /sys/class/net/wlan0/ifindex ]; then
        wlan_ifidx=$(cat /sys/class/net/wlan0/ifindex 2>/dev/null)
        for udir in /run/udev/data /sysroot/run/udev/data; do
            mkdir -p "$udir" 2>/dev/null || true
            cat << EOF_UDEV_N > "$udir/n$wlan_ifidx"
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
        done
    fi

    # Configuración de wpa_supplicant y NetworkManager en sysroot si aplica
    mkdir -p /etc/wpa_supplicant /run/wpa_supplicant 2>/dev/null || true
    if [ ! -f /etc/wpa_supplicant/wpa_supplicant.conf ]; then
        cat << 'EOF_WPA' > /etc/wpa_supplicant/wpa_supplicant.conf
ctrl_interface=/run/wpa_supplicant
ctrl_interface_group=0
update_config=1
EOF_WPA
    fi

    mkdir -p /etc/NetworkManager/conf.d 2>/dev/null || true
    cat << 'EOF_NM' > /etc/NetworkManager/conf.d/10-manage-wifi.conf
[device-wifi]
match-device=interface-name:wlan*
managed=1
EOF_NM

    if ! pgrep -x NetworkManager >/dev/null 2>&1; then
        NetworkManager 2>/dev/null &
    fi
else
    echo "[-] wlan0 no aparecio de inmediato. Revisando dmesg:"
    dmesg | grep -i -E "wmt|wlan|6631" | tail -n 20
fi
