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
echo "   ESPERANDO FASTBOOT PARA ACTIVAR SLOT B (POSTMARKETOS)  "
echo "=========================================================="

while ! fastboot devices 2>/dev/null | grep -q "fastboot"; do
    sleep 0.5
done

DEV=$(fastboot devices 2>/dev/null | awk '{print $1}')
echo "✅ Tablet detectada en Fastboot: $DEV"

echo "[1/6] Flasheando particion misc (activando Slot B verificado)..."
fastboot flash misc "$MISC_IMG"

echo "[2/6] Flasheando vbmeta_b (AVB deshabilitado)..."
fastboot flash vbmeta_b "$VBMETA_IMG"

echo "[3/6] Flasheando dtbo_b..."
fastboot flash dtbo_b "$DTBO_IMG"

echo "[4/6] Flasheando boot_b (postmarketOS v13)..."
fastboot flash boot_b "$BOOT_IMG"

echo "[5/6] Flasheando vendor_boot_b (postmarketOS v13)..."
fastboot flash vendor_boot_b "$VENDOR_BOOT_IMG"

echo "[6/6] Estableciendo slot activo B..."
fastboot --set-active=b 2>/dev/null || true

echo ""
echo "🎉 ¡SLOT B CONFIGURADO Y ACTIVADO CON ÉXITO!"
echo "[*] Reiniciando tablet en postmarketOS..."
fastboot reboot
