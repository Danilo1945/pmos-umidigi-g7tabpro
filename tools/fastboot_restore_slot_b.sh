#!/bin/bash
set -e
TABLET_DIR="/home/danilo/Documentos/TABLET"
V15_DIR="$TABLET_DIR/version_v15_estable"

echo "Esperando que la tablet entre en Fastboot..."
while ! fastboot devices 2>/dev/null | grep -q "fastboot"; do
    sleep 0.2
done

DEV=$(fastboot devices 2>/dev/null | awk '{print $1}')
echo "✅ Dispositivo Fastboot detectado: $DEV"

echo "[1/6] Flasheando dtbo_b..."
fastboot flash dtbo_b "$TABLET_DIR/stock_firmware/UMIDIGI_G7_Tab_Pro_E_V1.0_20250512/dtbo.img"

echo "[2/6] Flasheando vbmeta_b con AVB desactivado..."
fastboot flash vbmeta_b "$TABLET_DIR/vbmeta_disabled_official.img"

echo "[3/6] Flasheando vendor_boot_b..."
fastboot flash vendor_boot_b "$V15_DIR/vendor_boot_pmos_v15.img"

echo "[4/6] Flasheando boot_b..."
fastboot flash boot_b "$V15_DIR/pmos_boot_v15.img"

echo "[5/6] Restaurando BCB oficial Slot B en misc..."
fastboot flash misc "$TABLET_DIR/misc_slot_b_verified.img"

echo "[6/6] Fijando Slot B activo..."
fastboot set_active b

echo "[*] Arrancando imagen directamente en memoria RAM con 'fastboot boot'..."
fastboot boot "$V15_DIR/pmos_boot_v15.img"

echo "🎉 ¡Comando fastboot boot ejecutado! Monitoreando ping a 172.16.42.1..."
for i in $(seq 1 40); do
    if ping -c 1 -W 1 172.16.42.1 >/dev/null 2>&1; then
        echo "🎉 ¡TABLET ONLINE! Ping respondido en intento $i"
        exit 0
    fi
    sleep 1.5
done
echo "[-] Timeout esperando ping."
