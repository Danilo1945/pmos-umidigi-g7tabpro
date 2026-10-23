#!/bin/bash
TABLET_DIR="/home/danilo/Documentos/TABLET"
MTK_DIR="$TABLET_DIR/mtkclient"
PRELOADER="$TABLET_DIR/stock_firmware/UMIDIGI_G7_Tab_Pro_E_V1.0_20250512/preloader_k6789v1_64.bin"
BOOT_IMG="$TABLET_DIR/version_v15_estable/pmos_boot_v15.img"

echo "=================================================================="
echo "  BUCLE DE FLASHEO DEFINITIVO DE BOOT_B V15 (MTKCLIENT)  "
echo "=================================================================="
echo "[*] Imagen: $BOOT_IMG ($(stat -c%s "$BOOT_IMG") bytes)"
echo "[*] Si la tablet está en bucle o congelada, desconecta el cable y reconéctalo..."

sudo systemctl stop ModemManager 2>/dev/null || true

attempt=0
while true; do
    attempt=$((attempt + 1))
    sudo rm -f "$MTK_DIR/.state"* 2>/dev/null || true
    echo ""
    echo "[Intento #$attempt] Esperando handshake en Preloader..."
    
    sudo /home/danilo/anaconda3/bin/python3 -u "$MTK_DIR/mtk.py" --serialport DETECT w boot_b "$BOOT_IMG" --preloader="$PRELOADER"
    rc=$?
    
    sudo rm -f "$MTK_DIR/.state"* 2>/dev/null || true
    
    if [ $rc -eq 0 ]; then
        echo ""
        echo "=================================================================="
        echo "🎉🎉🎉 ¡BOOT_B FLASHEADOS AL 100% CON ÉXITO ABSOLUTO! 🎉🎉🎉"
        echo "=================================================================="
        echo "[*] Reiniciando la tablet..."
        sudo /home/danilo/anaconda3/bin/python3 "$MTK_DIR/mtk.py" --serialport DETECT reset 2>/dev/null || true
        exit 0
    fi
    
    echo "[-] Intento #$attempt falló (código $rc). Reintentando en 0.5s..."
    sleep 0.5
done
