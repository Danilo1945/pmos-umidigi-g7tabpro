#!/bin/bash
TABLET_DIR="/home/danilo/Documentos/TABLET"
MTK_DIR="$TABLET_DIR/mtkclient"
PRELOADER="$TABLET_DIR/stock_firmware/UMIDIGI_G7_Tab_Pro_E_V1.0_20250512/preloader_k6789v1_64.bin"
OUT_FILE="/home/danilo/Documentos/TABLET/misc_dump.bin"

echo "=================================================================="
echo "      CAPTURA DE PARTICION MISC VIA PRELOADER (MTKCLIENT)         "
echo "=================================================================="

sudo systemctl stop ModemManager 2>/dev/null || true

attempt=0
while [ $attempt -lt 10 ]; do
    attempt=$((attempt + 1))
    sudo rm -f "$MTK_DIR/.state"* 2>/dev/null || true
    echo ""
    echo "[Intento #$attempt] Esperando handshake en Preloader..."
    
    # Leer partición misc (primeros 512 KB o completa)
    sudo /home/danilo/anaconda3/bin/python3 -u "$MTK_DIR/mtk.py" --serialport DETECT r misc "$OUT_FILE" --preloader="$PRELOADER"
    rc=$?
    
    sudo rm -f "$MTK_DIR/.state"* 2>/dev/null || true
    
    if [ $rc -eq 0 ]; then
        echo ""
        echo "=================================================================="
        echo "🎉 ¡PARTICION MISC LEIDA CON EXITO! ($OUT_FILE) 🎉"
        echo "=================================================================="
        exit 0
    fi
    
    echo "[-] Intento #$attempt falló (código $rc). Reintentando en 0.2s..."
    sleep 0.2
done
