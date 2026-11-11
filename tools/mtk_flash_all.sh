#!/bin/bash
set -e

TABLET_DIR="/home/danilo/Documentos/TABLET"
V15_DIR="$TABLET_DIR/version_v15_estable"
MTK_PY="/home/danilo/Documentos/TABLET/mtkclient/mtk.py"
PYTHON="/home/danilo/anaconda3/bin/python3"

BOOT_IMG="$V15_DIR/pmos_boot_v15.img"
VB_IMG="$V15_DIR/vendor_boot_pmos_v15.img"
DTBO_IMG="$TABLET_DIR/stock_firmware/UMIDIGI_G7_Tab_Pro_E_V1.0_20250512/dtbo.img"
VBMETA_IMG="$TABLET_DIR/vbmeta_disabled_official.img"
MISC_IMG="$TABLET_DIR/misc_slot_b_verified.img"

PARTS="misc,boot_a,boot_b,vendor_boot_a,vendor_boot_b,vbmeta_a,vbmeta_b,dtbo_a,dtbo_b"
FILES="$MISC_IMG,$BOOT_IMG,$BOOT_IMG,$VB_IMG,$VB_IMG,$VBMETA_IMG,$VBMETA_IMG,$DTBO_IMG,$DTBO_IMG"

echo "=========================================================="
echo " Flasheo Integral vía MediaTek Download Agent (mtkclient) "
echo "=========================================================="
echo "Particiones a escribir: $PARTS"
echo ""
echo "Conectando con Preloader / DA..."

echo danilo | sudo -S $PYTHON $MTK_PY w "$PARTS" "$FILES"

echo ""
echo "🎉 ¡FLASHEO COMPLETO CON MTKCLIENT EXITOSO!"
echo "Reiniciando dispositivo..."
echo danilo | sudo -S $PYTHON $MTK_PY reset 2>/dev/null || true
