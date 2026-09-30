#!/bin/bash
# ==============================================================================
# Script Maestro de Compilación - Repositorio de Funcionalidades Finales
# UMIDIGI G7 Tab Pro (MT6789 / Helio G99)
# ==============================================================================
set -e

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
echo "=== Iniciando Compilación Global de Todos los Subsistemas (AArch64) ==="

echo "[1/4] Compilando Subsistema 02: Batería y Energía..."
make -C "$ROOT_DIR/subsistemas/02_bateria_energia" clean
make -C "$ROOT_DIR/subsistemas/02_bateria_energia"

echo "[2/4] Compilando Subsistema 03: Pantalla y libseat direct..."
make -C "$ROOT_DIR/subsistemas/03_pantalla_rotacion_touch" clean
make -C "$ROOT_DIR/subsistemas/03_pantalla_rotacion_touch"

echo "[3/4] Compilando Subsistema 04: Wi-Fi y Conectividad WMT..."
make -C "$ROOT_DIR/subsistemas/04_wifi_conectividad" clean
make -C "$ROOT_DIR/subsistemas/04_wifi_conectividad"

echo "[4/5] Compilando Subsistema 05: Arranque y Watchdog Petter..."
make -C "$ROOT_DIR/subsistemas/05_arranque_kernel" clean
make -C "$ROOT_DIR/subsistemas/05_arranque_kernel"

echo "[5/5] Compilando Subsistema 06: Aceleración Gráfica GPU ARM Mali-G57 (libmali_shim.so)..."
make -C "$ROOT_DIR/subsistemas/06_aceleracion_grafica_gpu" clean
make -C "$ROOT_DIR/subsistemas/06_aceleracion_grafica_gpu"

echo ""
echo "=========================================================="
echo " 🎉 ¡TODOS LOS SUBSISTEMAS HAN SIDO COMPILADOS CON ÉXITO! "
echo "=========================================================="

