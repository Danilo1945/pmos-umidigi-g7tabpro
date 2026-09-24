#!/bin/bash
# ==============================================================================
# Script de Instalación / Restauración: Subsistema de Audio MT6789 + AW87359
# UMIDIGI G7 Tab Pro (Helio G99 / MT6789 - postmarketOS Edge aarch64)
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIRMWARE_DIR="$SCRIPT_DIR/../../firmware"

echo "=== [1/5] Verificando e instalando Firmware AW87359 ==="
if [ -f "$FIRMWARE_DIR/aw87xxx_acf.bin" ]; then
    mkdir -p /lib/firmware
    cp -v "$FIRMWARE_DIR/aw87xxx_acf.bin" /lib/firmware/
fi

echo "=== [2/5] Instalando configuraciones de PipeWire y WirePlumber ==="
mkdir -p /etc/pipewire/pipewire.conf.d
cp -v "$SCRIPT_DIR/10-alsa.conf" /etc/pipewire/pipewire.conf.d/10-alsa.conf

if [ -d /etc/wireplumber/main.lua.d ]; then
    cp -v "$SCRIPT_DIR/50-alsa-fix.conf" /etc/wireplumber/main.lua.d/50-alsa-fix.conf 2>/dev/null || true
fi

if [ -d /usr/share/pulseaudio/alsa-mixer/paths ]; then
    cp -v "$SCRIPT_DIR/analog-output-headphones.conf" /usr/share/pulseaudio/alsa-mixer/paths/analog-output-headphones.conf 2>/dev/null || true
fi

echo "=== [3/5] Instalando Demonios de Audio y Teclas de Volumen ==="
cp -v "$SCRIPT_DIR/tablet_audio_daemon.py" /usr/local/bin/tablet_audio_daemon.py
chmod +x /usr/local/bin/tablet_audio_daemon.py

if [ -f "$SCRIPT_DIR/volume_keys_daemon.py" ]; then
    cp -v "$SCRIPT_DIR/volume_keys_daemon.py" /usr/local/bin/volume_keys_daemon.py
    chmod +x /usr/local/bin/volume_keys_daemon.py
fi

echo "=== [4/5] Configurando Servicio OpenRC tablet-audio ==="
cat << 'SERVICE_EOF' > /etc/init.d/tablet-audio
#!/sbin/openrc-run
description="Daemon de Audio y Calibracion de Amplificadores UMIDIGI G7 Tab Pro"
command="/usr/local/bin/tablet_audio_daemon.py"
command_background=true
pidfile="/run/tablet-audio.pid"

depend() {
    need localmount
    after alsasound pipewire
}
SERVICE_EOF
chmod +x /etc/init.d/tablet-audio
rc-update add tablet-audio default 2>/dev/null || true

echo "=== [5/5] Reiniciando servicios de audio ==="
/etc/init.d/tablet-audio restart 2>/dev/null || true

echo ">>> ¡Subsistema de Audio restaurado con éxito!"
