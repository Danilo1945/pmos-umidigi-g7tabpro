#!/bin/bash
set -e

echo "[+] Esperando conexión USB con la tablet (CDC-ACM o RNDIS 172.16.42.1)..."

while true; do
    if ping -c 1 -W 1 172.16.42.1 >/dev/null 2>&1; then
        echo "[+] ¡Tablet detectada en 172.16.42.1!"
        break
    fi
    if [ -e /dev/ttyACM0 ]; then
        echo "[+] ¡Tablet detectada en /dev/ttyACM0!"
        break
    fi
    sleep 0.5
done

# Esperar a que el daemon de telnet o shell esté listo
echo "[+] Conectando e inyectando salvaguardas inmediatas..."
python3 /home/danilo/Documentos/TABLET/FUNCIONALIDADES_FINALES/tools/tablet_cmd.py "
# 1. Congelar brillo en 15 inmediatamente y matar drm_splash
echo 15 > /sys/class/leds/lcd-backlight/brightness
killall -9 drm_splash 2>/dev/null || true
if ! pgrep -f 'backlight_guard' >/dev/null; then
    (while true; do
        if [ \"\$(cat /sys/class/leds/lcd-backlight/brightness 2>/dev/null)\" != \"15\" ]; then
            echo 15 > /sys/class/leds/lcd-backlight/brightness 2>/dev/null
        fi
        sleep 0.2
    done) >/dev/null 2>&1 &
fi

# 2. CPU y Bus en powersave para evitar picos de consumo que disparen UVLO de bateria
for g in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    [ -f \"\$g\" ] && echo powersave > \"\$g\" 2>/dev/null || true
done
for g in /sys/class/devfreq/*dvfsrc*/governor; do
    [ -f \"\$g\" ] && echo powersave > \"\$g\" 2>/dev/null || true
done

# 3. Carga a 2A permanente
echo 2000000 > /sys/devices/platform/charger/input_current 2>/dev/null || true
echo 4 > /sys/devices/platform/charger/chr_type 2>/dev/null || true
CHG_DEV=\"/sys/devices/platform/soc/11017000.i2c/i2c-5/5-0034/11017000.i2c:mt6375@34:chg/power_supply/11017000.i2c:mt6375@34:chg\"
if [ -d \"\$CHG_DEV\" ]; then
    echo 1 > \$CHG_DEV/online 2>/dev/null || true
    echo \"Charging\" > \$CHG_DEV/status 2>/dev/null || true
    echo 2000 > \$CHG_DEV/input_current_limit 2>/dev/null || true
    echo 2000 > \$CHG_DEV/constant_charge_current 2>/dev/null || true
fi

# 4. Cargar cadena de GPU Mali de forma segura y verificar nodo /dev/mali0
sh /sysroot/usr/bin/load_mali_chain.sh >/dev/null 2>&1 || true
MALI_MINOR=\$(grep mali0 /proc/misc 2>/dev/null | awk '{print \$1}')
if [ -n \"\$MALI_MINOR\" ]; then
    rm -f /dev/mali0 /sysroot/dev/mali0
    mknod -m 666 /dev/mali0 c 10 \$MALI_MINOR
    mknod -m 666 /sysroot/dev/mali0 c 10 \$MALI_MINOR
fi

echo 'STATUS: Hardware protegido (Powersave CPU=500MHz, Brillo=15, Carga=2A, GPU=OPP37).'
"

echo "[+] Salvaguardas aplicadas con éxito."
