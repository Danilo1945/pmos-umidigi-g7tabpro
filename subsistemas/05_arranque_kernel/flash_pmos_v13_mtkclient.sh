#!/bin/bash
set -e

TABLET_DIR="/home/danilo/Documentos/TABLET"
V13_DIR="$TABLET_DIR/version_v13"
MTK_DIR="$TABLET_DIR/mtkclient"

BOOT_IMG="$V13_DIR/pmos_boot_v13.img"
VENDOR_BOOT_IMG="$V13_DIR/vendor_boot_pmos_v13.img"
PRELOADER="$TABLET_DIR/stock_firmware/UMIDIGI_G7_Tab_Pro_E_V1.0_20250512/preloader_k6789v1_64.bin"

echo "=========================================================="
echo " Flasheo postmarketOS Versión 13 vía mtkclient (BROM Mode) "
echo "=========================================================="
echo "[*] Particiones a escribir: boot_b y vendor_boot_b"
echo ""
echo "[*] Esperando a que el dispositivo sea conectado en modo BROM..."
echo ""

python3 "$MTK_DIR/mtk.py" w boot_b,vendor_boot_b "$BOOT_IMG,$VENDOR_BOOT_IMG" --preloader="$PRELOADER"

echo ""
echo "🎉 ¡FLASHEO DE POSTMARKETOS V13 COMPLETADO CON ÉXITO!"
