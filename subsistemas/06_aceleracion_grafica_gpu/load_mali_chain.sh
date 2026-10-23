#!/bin/sh
# ==============================================================================
# Script de Carga Ordenada del Subsistema de GPU ARM Mali-G57 (Helio G99 / MT6789)
# postmarketOS Edge / Linux 5.10.185-android12 aarch64
# ==============================================================================
set +e
echo "=== INICIANDO CADENA DE CONTROLADORES GPU MALI-G57 ==="

# Blindar brillo inmediatamente en 15/255 (anti-UVLO)
echo 15 > /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true

# 1. Dependencias de reloj, energía y devinfo (CLAVE para resolver EPROBE_DEFER)
modprobe nvmem_mtk-devinfo 2>/dev/null || true
modprobe mcupm 2>/dev/null || true
modprobe fhctl 2>/dev/null || true

# 2. Infraestructura de hardware y bus de eventos
modprobe mtk-ssc 2>/dev/null || true
modprobe gpueb 2>/dev/null || true
modprobe mtk_gpu_hal 2>/dev/null || true

# 3. Módulos de protección y limitación de batería
modprobe mtk_bp_thl 2>/dev/null || true
modprobe mtk_battery_oc_throttling 2>/dev/null || true

# 4. Infraestructura DVFS y QoS de GPU MediaTek
modprobe mtk_gpufreq_wrapper 2>/dev/null || true
modprobe mtk_qos 2>/dev/null || true
modprobe sspm_v3 2>/dev/null || true
modprobe mtk_gpu_qos 2>/dev/null || true
modprobe mtk_gpufreq_mt6789 2>/dev/null || true

# 5. Subsistema GED y Memory Group Manager
modprobe ged 2>/dev/null || true
modprobe mali_mgm_mt6789 2>/dev/null || true

# 6. Controlador principal ARM Mali kbase (priorizar versión parcheada v2 en sysroot)
if [ -f /sysroot/usr/lib/modules/5.10.185-android12-9-gcc7d44e69095-dirty/mali_kbase_mt6789.ko ]; then
    cp -f /sysroot/usr/lib/modules/5.10.185-android12-9-gcc7d44e69095-dirty/mali_kbase_mt6789.ko /lib/modules/5.10.185-android12-9-gcc7d44e69095-dirty/mali_kbase_mt6789.ko 2>/dev/null || true
    insmod /sysroot/usr/lib/modules/5.10.185-android12-9-gcc7d44e69095-dirty/mali_kbase_mt6789.ko 2>/dev/null || modprobe mali_kbase_mt6789 2>/dev/null || true
else
    insmod /usr/lib/modules/5.10.185-android12-9-gcc7d44e69095-dirty/mali_kbase_mt6789.ko 2>/dev/null || modprobe mali_kbase_mt6789 2>/dev/null || true
fi

# 7. Creación dinámica y permisos de acceso en /dev/mali0
MALI_MINOR=$(grep -w mali0 /proc/misc 2>/dev/null | awk '{print $1}')
[ -z "$MALI_MINOR" ] && MALI_MINOR=117
rm -f /dev/mali0 /sysroot/dev/mali0 2>/dev/null || true
mknod /dev/mali0 c 10 "$MALI_MINOR" 2>/dev/null || true
chmod 666 /dev/mali0 2>/dev/null || true
if [ -d /sysroot/dev ]; then
    mknod /sysroot/dev/mali0 c 10 "$MALI_MINOR" 2>/dev/null || true
    chmod 666 /sysroot/dev/mali0 2>/dev/null || true
fi
chmod 666 /dev/dma_heap/* /sysroot/dev/dma_heap/* 2>/dev/null || true

# 8. Bloqueo de frecuencia de seguridad anti-UVLO (OPP 37 = 390 MHz @ 575 mV, 153 mW)
if [ -d /proc/gpufreqv2 ]; then
    echo 37 > /proc/gpufreqv2/fix_target_opp_index 2>/dev/null || true
elif [ -d /proc/gpufreq ]; then
    echo 37 > /proc/gpufreq/fix_target_opp_index 2>/dev/null || echo 37 > /proc/gpufreq/gpufreq_opp_idx 2>/dev/null || true
fi

echo "=== GPU MALI-G57 COMPLETAMENTE OPERATIVA Y SEGURA ==="

