#!/bin/bash
# ==============================================================================
# Script de Instalación: Subsistema Wi-Fi MediaTek MT6631 / WMT
# UMIDIGI G7 Tab Pro (postmarketOS Edge aarch64)
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIRMWARE_DIR="$SCRIPT_DIR/../../firmware"

echo "=== [1/4] Verificando e instalando Firmwares WMT y Wi-Fi ==="
mkdir -p /lib/firmware /vendor/firmware
for fw in WIFI_RAM_CODE_soc2_2_1d_1.bin WIFI_RAM_CODE_soc2_2_1e_1.bin soc2_2_ram_wifi_1d_1_hdr.bin soc2_2_ram_wifi_1e_1_hdr.bin WMT_SOC.cfg WMT.cfg; do
    if [ -f "$FIRMWARE_DIR/$fw" ]; then
        cp -v "$FIRMWARE_DIR/$fw" /lib/firmware/
        ln -sf "/lib/firmware/$fw" "/vendor/firmware/$fw" 2>/dev/null || true
    fi
done

echo "=== [2/4] Instalando binarios de inicialización WMT ==="
if [ -f "$SCRIPT_DIR/wmt_init_complete" ]; then
    cp -v "$SCRIPT_DIR/wmt_init_complete" /usr/bin/wmt_init_complete
    chmod +x /usr/bin/wmt_init_complete
fi

if [ -f "$SCRIPT_DIR/wmt_loader" ]; then
    cp -v "$SCRIPT_DIR/wmt_loader" /usr/bin/wmt_loader
    chmod +x /usr/bin/wmt_loader
fi

if [ -f "$SCRIPT_DIR/wmt_launcher" ]; then
    cp -v "$SCRIPT_DIR/wmt_launcher" /usr/bin/wmt_launcher
    chmod +x /usr/bin/wmt_launcher
fi

echo "=== [3/4] Instalando script de arranque Wi-Fi ==="
cp -v "$SCRIPT_DIR/wmt_wifi_start.sh" /usr/bin/wmt_wifi_start.sh
chmod +x /usr/bin/wmt_wifi_start.sh

echo "=== [4/4] Configurando Servicio OpenRC mtk-wifi ==="
cat << 'SERVICE_EOF' > /etc/init.d/mtk-wifi
#!/sbin/openrc-run
description="Inicializador Wi-Fi MediaTek WMT MT6631"
command="/usr/bin/wmt_wifi_start.sh"
command_background=true
pidfile="/run/mtk-wifi.pid"

depend() {
    need localmount
    before net
    after bootmisc
}
SERVICE_EOF
chmod +x /etc/init.d/mtk-wifi
rc-update add mtk-wifi default 2>/dev/null || true

echo "=== [5/5] Levantando Wi-Fi ==="
/usr/bin/wmt_wifi_start.sh

echo ">>> ¡Subsistema Wi-Fi restaurado con éxito!"
