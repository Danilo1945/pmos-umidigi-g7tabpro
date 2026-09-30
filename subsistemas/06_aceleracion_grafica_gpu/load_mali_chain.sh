#!/bin/sh
# ==============================================================================
# Script de Carga Ordenada del Subsistema de GPU ARM Mali-G57 (Helio G99 / MT6789)
# postmarketOS Edge / Linux 5.10.185-android12 aarch64
# ==============================================================================
set -e

MODPATH="/lib/modules/$(uname -r)"
[ -d "$MODPATH" ] || MODPATH="/lib/modules"

echo "=== INICIANDO CADENA DE CONTROLADORES GPU MALI-G57 ==="

# 1. Cargar dependencias de reloj, micro-controlador de energía y devinfo
modprobe mcupm 2>/dev/null || insmod $MODPATH/mcupm.ko 2>/dev/null || true
modprobe fhctl 2>/dev/null || insmod $MODPATH/fhctl.ko 2>/dev/null || true
modprobe nvmem_mtk-devinfo 2>/dev/null || insmod $MODPATH/nvmem_mtk-devinfo.ko 2>/dev/null || true

# 2. Cargar infraestructura de DVFS y QoS de GPU MediaTek
modprobe mtk_gpufreq_wrapper 2>/dev/null || insmod $MODPATH/mtk_gpufreq_wrapper.ko 2>/dev/null || true
modprobe mtk_gpu_qos 2>/dev/null || insmod $MODPATH/mtk_gpu_qos.ko 2>/dev/null || true
modprobe mtk_gpufreq_mt6789 2>/dev/null || insmod $MODPATH/mtk_gpufreq_mt6789.ko 2>/dev/null || true

# 3. Cargar subsistema GED (GPU Extension Device) y Memory Group Manager
modprobe ged 2>/dev/null || insmod $MODPATH/ged.ko 2>/dev/null || true
modprobe mali_mgm_mt6789 2>/dev/null || insmod $MODPATH/mali_mgm_mt6789.ko 2>/dev/null || true

# 4. Cargar controlador principal de GPU Mali Kbase parcheado
insmod $MODPATH/mali_kbase_mt6789.ko 2>/dev/null || insmod /lib/modules/mali_kbase_mt6789.ko 2>/dev/null || true

# 5. Asegurar permisos de acceso a usuarios en /dev/mali0
if [ -e /dev/mali0 ]; then
    chmod 666 /dev/mali0
    [ -d /sysroot/dev ] && cp -a /dev/mali0 /sysroot/dev/mali0 2>/dev/null || true
    echo "[✓] /dev/mali0 inicializado con permisos 666"
else
    echo "[!] Error: /dev/mali0 no fue creado por el kernel"
    exit 1
fi

# 6. Fijar punto de operación seguro (OPP 37 = 390 MHz @ 575 mV, 153 mW)
# Evita picos de sobrecorriente instantánea y caídas de tensión (PMIC UVLO)
if [ -e /proc/gpufreqv2/fix_target_opp_index ]; then
    echo 37 > /proc/gpufreqv2/fix_target_opp_index
    echo "[✓] Frecuencia de GPU fijada en modo de bajo consumo seguro (390 MHz @ 575 mV)"
fi

echo "=== GPU MALI-G57 COMPLETAMENTE OPERATIVA Y SEGURA ==="
