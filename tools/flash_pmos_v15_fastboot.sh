#!/bin/bash
set -e

TABLET_DIR="/home/danilo/Documentos/TABLET"
V15_DIR="$TABLET_DIR/version_v15_estable"

BOOT_IMG="$V15_DIR/pmos_boot_v15.img"
VENDOR_BOOT_IMG="$V15_DIR/vendor_boot_pmos_v15.img"
DTBO_IMG="$TABLET_DIR/stock_firmware/UMIDIGI_G7_Tab_Pro_E_V1.0_20250512/dtbo.img"
VBMETA_IMG="$TABLET_DIR/vbmeta_disabled_official.img"
MISC_IMG="$TABLET_DIR/misc_slot_b_verified.img"

echo "=========================================================="
echo " Flasheo postmarketOS Versión 15.0 Estable (Boot + Power) "
echo "=========================================================="

if [ ! -f "$BOOT_IMG" ] || [ ! -f "$VENDOR_BOOT_IMG" ]; then
    echo "[-] Error: Faltan imágenes de arranque en $V15_DIR"
    exit 1
fi

echo "[*] Esperando a que el dispositivo entre en modo Fastboot..."
while ! fastboot devices 2>/dev/null | grep -q "fastboot"; do
    sleep 0.5
done

DEV=$(fastboot devices 2>/dev/null | awk '{print $1}')
echo "✅ Dispositivo Fastboot detectado: $DEV"

echo "[1/6] Flasheando dtbo_a y dtbo_b..."
fastboot flash dtbo_a "$DTBO_IMG"
fastboot flash dtbo_b "$DTBO_IMG"

echo "[2/6] Flasheando vbmeta_a y vbmeta_b (AVB deshabilitado)..."
fastboot flash vbmeta_a "$VBMETA_IMG"
fastboot flash vbmeta_b "$VBMETA_IMG"

echo "[3/6] Flasheando vendor_boot_a y vendor_boot_b (v15)..."
fastboot flash vendor_boot_a "$VENDOR_BOOT_IMG"
fastboot flash vendor_boot_b "$VENDOR_BOOT_IMG"

echo "[4/6] Flasheando boot_a y boot_b (v15 - Daemon + Phosh + Power)..."
fastboot flash boot_a "$BOOT_IMG"
fastboot flash boot_b "$BOOT_IMG"

echo "[5/6] Fijando Slot B en partición misc..."
fastboot flash misc "$MISC_IMG"

echo ""
echo "🎉 ¡FLASHEO DE POSTMARKETOS V15.0 COMPLETADO CON ÉXITO!"
echo "[*] Reiniciando tablet hacia postmarketOS..."
fastboot reboot
