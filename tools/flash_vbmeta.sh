#!/bin/bash
TABLET_DIR="/home/danilo/Documentos/TABLET"
MTK_DIR="$TABLET_DIR/mtkclient"
PRELOADER="$TABLET_DIR/stock_firmware/UMIDIGI_G7_Tab_Pro_E_V1.0_20250512/preloader_k6789v1_64.bin"
VBMETA="$TABLET_DIR/vbmeta_disabled_official.img"

echo "=========================================================="
echo " Flasheando vbmeta_b y vbmeta_a (AVB Deshabilitado)       "
echo "=========================================================="

python3 "$MTK_DIR/mtk.py" --preloader="$PRELOADER" w vbmeta_b "$VBMETA"
python3 "$MTK_DIR/mtk.py" --preloader="$PRELOADER" w vbmeta_a "$VBMETA"
