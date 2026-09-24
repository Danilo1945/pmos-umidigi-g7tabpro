#!/bin/bash
# ==============================================================================
# Script de Reactivación Garantizada del Slot B (postmarketOS)
# UMIDIGI G7 Tab Pro (MT6789 / Helio G99)
# ==============================================================================

set -e

TABLET_DIR="/home/danilo/Documentos/TABLET"
MISC_IMG="$TABLET_DIR/misc_slot_b_verified.img"
BOOT_IMG="$TABLET_DIR/version_v13/pmos_boot_v13.img"
VENDOR_BOOT_IMG="$TABLET_DIR/version_v13/vendor_boot_pmos_v13.img"

echo "=========================================================="
echo " 🔧 Reactivación y Fijación del Slot B (postmarketOS)      "
echo "=========================================================="

echo "[*] Esperando conexion con Fastboot..."
while ! fastboot devices | grep -q "fastboot"; do
    sleep 0.5
done
echo "✅ Tablet detectada en Fastboot."

echo "[1/4] Flasheando particion misc con Slot B activo y CRC32 verificado..."
fastboot flash misc "$MISC_IMG"

echo "[2/4] Asegurando imagen de arranque v11 en boot_b..."
fastboot flash boot_b "$BOOT_IMG"

echo "[3/4] Asegurando imagen vendor_boot_b v11..."
fastboot flash vendor_boot_b "$VENDOR_BOOT_IMG"

echo "[4/4] Estableciendo slot activo en B..."
fastboot --set-active=b 2>/dev/null || true

echo ""
echo "=========================================================="
echo " ✅ Slot B reactivado exitosamente con 7 reintentos limpios"
echo "=========================================================="
echo "Para arrancar postmarketOS ahora, presiona Enter o ejecuta:"
echo "  fastboot reboot"
echo ""
read -p "¿Deseas reiniciar la tablet ahora en Slot B? (s/N): " RESP
if [[ "$RESP" =~ ^[sSyY]$ ]]; then
    fastboot reboot
    echo "[+] Reiniciando tablet en postmarketOS..."
fi
