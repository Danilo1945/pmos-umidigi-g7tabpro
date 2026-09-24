#!/bin/bash
set -e

TABLET_DIR="/home/danilo/Documentos/TABLET"
V13_DIR="$TABLET_DIR/version_v13"

BOOT_IMG="$V13_DIR/pmos_boot_v13.img"
VENDOR_BOOT_IMG="$V13_DIR/vendor_boot_pmos_v13.img"
DTBO_IMG="$TABLET_DIR/stock_firmware/UMIDIGI_G7_Tab_Pro_E_V1.0_20250512/dtbo.img"
VBMETA_IMG="$TABLET_DIR/vbmeta_disabled_official.img"
MISC_IMG="$TABLET_DIR/misc_slot_b_verified.img"

echo "=========================================================="
echo " Flasheo postmarketOS Versión 13 (Audio + Power + Rotación) "
echo "=========================================================="

if [ ! -f "$BOOT_IMG" ]; then
    echo "[-] Error: No se encontró $BOOT_IMG"
    exit 1
fi

if [ ! -f "$VENDOR_BOOT_IMG" ]; then
    echo "[-] Error: No se encontró $VENDOR_BOOT_IMG"
    exit 1
fi

echo "[*] Esperando a que el dispositivo entre en modo Fastboot..."
while ! fastboot devices 2>/dev/null | grep -q "fastboot"; do
    sleep 0.5
done

DEV=$(fastboot devices 2>/dev/null | awk '{print $1}')
echo "✅ Dispositivo Fastboot detectado: $DEV"

echo "[1/5] Flasheando dtbo_b..."
fastboot flash dtbo_b "$DTBO_IMG"

echo "[2/5] Flasheando vbmeta_b (AVB deshabilitado)..."
fastboot flash vbmeta_b "$VBMETA_IMG"

echo "[3/5] Flasheando vendor_boot_b (v13)..."
fastboot flash vendor_boot_b "$VENDOR_BOOT_IMG"

echo "[4/5] Flasheando boot_b (v13 - Daemon + Phosh + Power/Rotación)..."
fastboot flash boot_b "$BOOT_IMG"

echo "[5/5] Fijando Slot B en partición misc..."
fastboot flash misc "$MISC_IMG"

echo ""
echo "🎉 ¡FLASHEO DE POSTMARKETOS V13 COMPLETADO CON ÉXITO!"
echo "[*] Reiniciando tablet hacia postmarketOS..."
fastboot reboot
