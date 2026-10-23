#!/bin/bash
set -e

TABLET_DIR="/home/danilo/Documentos/TABLET"
MTK_DIR="$TABLET_DIR/mtkclient"
PRELOADER="$TABLET_DIR/stock_firmware/UMIDIGI_G7_Tab_Pro_E_V1.0_20250512/preloader_k6789v1_64.bin"
V15_DIR="$TABLET_DIR/version_v15_estable"

MISC_CLEAN="$TABLET_DIR/misc_clean.bin"
BOOT_IMG="$V15_DIR/pmos_boot_v15.img"
VB_IMG="$V15_DIR/vendor_boot_pmos_v15.img"

echo "=================================================================="
echo "    FLASHEO COMPLETO SENTINEL DE RECUPERACION (MTKCLIENT)         "
echo "=================================================================="
echo "[*] Limpieza misc:       $MISC_CLEAN"
echo "[*] Boot Centinela:      $BOOT_IMG"
echo "[*] Vendor Boot DTB:     $VB_IMG"
echo ""

sudo systemctl stop ModemManager 2>/dev/null || true

attempt=0
while [ $attempt -lt 15 ]; do
    attempt=$((attempt + 1))
    sudo rm -f "$MTK_DIR/.state"* 2>/dev/null || true
    echo "[Intento #$attempt] Esperando handshake en Preloader..."
    
    set +e
    sudo /home/danilo/anaconda3/bin/python3 -u "$MTK_DIR/mtk.py" --serialport DETECT \
        w misc,boot_a,boot_b,vendor_boot_a,vendor_boot_b \
        "$MISC_CLEAN,$BOOT_IMG,$BOOT_IMG,$VB_IMG,$VB_IMG" \
        --preloader="$PRELOADER"
    rc=$?
    set -e
    
    sudo rm -f "$MTK_DIR/.state"* 2>/dev/null || true
    
    if [ $rc -eq 0 ]; then
        echo ""
        echo "=================================================================="
        echo "🎉🎉🎉 ¡FLASHEO COMPLETO COMPLETADO CON ÉXITO ABSOLUTO! 🎉🎉🎉"
        echo "=================================================================="
        echo "[*] Reiniciando la tablet a postmarketOS..."
        sudo /home/danilo/anaconda3/bin/python3 "$MTK_DIR/mtk.py" --serialport DETECT reset 2>/dev/null || true
        exit 0
    fi
    
    echo "[-] Intento #$attempt falló (código $rc). Reintentando en 0.2s..."
    sleep 0.2
done

echo "[!] Se superaron los intentos máximos."
exit 1
