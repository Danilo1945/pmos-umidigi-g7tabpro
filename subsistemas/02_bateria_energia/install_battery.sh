#!/bin/bash
# ==============================================================================
# Script de Instalación: Subsistema de Batería y Carga MT6366/MT6358/MT6375
# UMIDIGI G7 Tab Pro (postmarketOS Edge aarch64)
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "=== [1/3] Instalando Demonios de Batería ==="
cp -v "$SCRIPT_DIR/tablet_battery_daemon.py" /usr/bin/tablet_battery_daemon.py
chmod +x /usr/bin/tablet_battery_daemon.py

if [ -f "$SCRIPT_DIR/battery_daemon" ]; then
    cp -v "$SCRIPT_DIR/battery_daemon" /usr/local/bin/battery_daemon
    chmod +x /usr/local/bin/battery_daemon
fi

echo "=== [2/3] Configurando Servicio OpenRC tablet-battery ==="
cat << 'SERVICE_EOF' > /etc/init.d/tablet-battery
#!/sbin/openrc-run
description="Monitor Inteligente de Bateria y Cargador UMIDIGI G7 Tab Pro"
command="/usr/bin/python3"
command_args="-u /usr/bin/tablet_battery_daemon.py"
command_background=true
pidfile="/run/tablet-battery.pid"

depend() {
    need localmount
    after bootmisc
}
SERVICE_EOF
chmod +x /etc/init.d/tablet-battery
rc-update add tablet-battery default 2>/dev/null || true

echo "=== [3/3] Reiniciando servicio de Batería ==="
/etc/init.d/tablet-battery restart 2>/dev/null || true

echo ">>> ¡Subsistema de Batería y Cargador restaurado con éxito!"
