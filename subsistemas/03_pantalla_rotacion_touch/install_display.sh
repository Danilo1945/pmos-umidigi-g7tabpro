#!/bin/bash
# ==============================================================================
# Script de Instalación: Pantalla DSI, Touchscreen, Orientación y Phosh Wayland
# UMIDIGI G7 Tab Pro (postmarketOS Edge aarch64)
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIRMWARE_DIR="$SCRIPT_DIR/../../firmware"

echo "=== [1/4] Verificando e instalando Firmware Touch Chipone ==="
if [ -f "$FIRMWARE_DIR/chipone_firmware.bin" ]; then
    mkdir -p /lib/firmware
    cp -v "$FIRMWARE_DIR/chipone_firmware.bin" /lib/firmware/
    cp -v "$FIRMWARE_DIR/chipone_firmware_ld.bin" /lib/firmware/ 2>/dev/null || true
fi

echo "=== [2/4] Instalando shim de bypass libseat_direct.so ==="
cp -v "$SCRIPT_DIR/libseat_direct.so" /usr/lib/libseat_direct.so
chmod 755 /usr/lib/libseat_direct.so

echo "=== [3/4] Instalando perfil de pantalla y compositor Phoc ==="
mkdir -p /etc
cp -v "$SCRIPT_DIR/phoc.ini" /etc/phoc.ini

echo "=== [4/4] Instalando script de inicio gráfico de Phosh ==="
cp -v "$SCRIPT_DIR/start_phosh.sh" /usr/local/bin/start_phosh.sh
chmod +x /usr/local/bin/start_phosh.sh

echo ">>> ¡Subsistema de Pantalla, Touch y Phosh restaurado con éxito!"
