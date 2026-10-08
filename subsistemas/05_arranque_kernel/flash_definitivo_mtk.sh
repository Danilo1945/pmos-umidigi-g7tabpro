#!/bin/bash

TABLET_DIR="/home/danilo/Documentos/TABLET"
V13_DIR="$TABLET_DIR/version_v13"
MTK_DIR="$TABLET_DIR/mtkclient"

BOOT_IMG="$V13_DIR/pmos_boot_v13.img"
PRELOADER="$TABLET_DIR/stock_firmware/UMIDIGI_G7_Tab_Pro_E_V1.0_20250512/preloader_k6789v1_64.bin"

echo "=========================================================="
echo " Flasheo Maestro Definitivo (Slots A y B) vía mtkclient   "
echo "=========================================================="
echo "[*] Esperando a que el dispositivo sea conectado o reiniciado..."
echo "[*] Si está conectado, mantén presionado el botón POWER 8-10s."

while true; do
    res=$(python3 "$MTK_DIR/mtk.py" w boot_a,boot_b "$BOOT_IMG,$BOOT_IMG" --preloader="$PRELOADER" 2>&1)
    echo "$res"
    if echo "$res" | grep -q "Progress:"; then
        echo "🎉 ¡FLASHEO DEFINITIVO COMPLETADO EN AMBOS SLOTS!"
        break
    fi
    sleep 0.5
done
