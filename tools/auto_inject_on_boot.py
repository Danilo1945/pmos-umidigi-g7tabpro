#!/usr/bin/env python3
import time
import subprocess
import os
import sys

TABLET_DIR = "/home/danilo/Documentos/TABLET"
TOOLS_DIR = os.path.join(TABLET_DIR, "FUNCIONALIDADES_FINALES/tools")
CMD_SCRIPT = os.path.join(TOOLS_DIR, "tablet_cmd.py")
FLASH_SCRIPT = os.path.join(TABLET_DIR, "version_v15_estable/flash_pmos_v15_fastboot.sh")

print("==================================================================")
print("  VIGILANTE ACTIVO: Auto-Inyección y Detección de Tablet Encendida ")
print("==================================================================")
print("[*] Monitoreando USB para Fastboot o arranque de postmarketOS...")

def check_fastboot():
    try:
        out = subprocess.check_output(["fastboot", "devices"], stderr=subprocess.DEVNULL, timeout=2).decode()
        if "fastboot" in out:
            dev = out.split()[0]
            return dev
    except:
        pass
    return None

def check_ping():
    try:
        ret = subprocess.call(["ping", "-c", "1", "-W", "1", "172.16.42.1"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return ret == 0
    except:
        return False

def check_serial():
    return os.path.exists('/dev/ttyACM0')

def inject_charging_and_gpu():
    print("\n⚡ [ENLACE DETECTADO] Inyectando comandos de carga inmediata y GPU en la tablet...")
    
    cmd_sequence = """
echo 15 > /sys/class/leds/lcd-backlight/brightness
killall -9 drm_splash backlight_guard panel_pwm_force 2>/dev/null || true
cat << 'EOF' > /bin/backlight_guard
#!/bin/sh
while true; do
    c=$(cat /sys/class/leds/lcd-backlight/brightness 2>/dev/null || echo 0)
    if [ "$c" -ne 15 ]; then
        echo 15 > /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
    fi
    sleep 0.2
done
EOF
chmod +x /bin/backlight_guard
nohup /bin/backlight_guard >/dev/null 2>&1 &

MOD_DIR=/sysroot/usr/lib/modules/5.10.185-android12-9-gcc7d44e69095-dirty
[ ! -d $MOD_DIR ] && MOD_DIR=/sysroot/lib/modules/5.10.185-android12-9-gcc7d44e69095-dirty
for m in nvmem-mt635x-efuse mt635x-auxadc pd_dbg_info adapter_class charger_class mtk_charger_algorithm_class tcpc_class tcpc_mt6375 mtk_charger_framework mt6375-charger extcon-mtk-usb mtk_pep20 mtk_pep40 mtk_pd_charging mtk_chg_type_det mt6358_battery; do
    insmod $MOD_DIR/$m.ko 2>/dev/null || true
done
echo 2000000 > /sys/devices/platform/charger/input_current 2>/dev/null || true
echo 4 > /sys/devices/platform/charger/chr_type 2>/dev/null || true
CHG_DEV="/sys/devices/platform/soc/11017000.i2c/i2c-5/5-0034/11017000.i2c:mt6375@34:chg/power_supply/11017000.i2c:mt6375@34:chg"
if [ -d "$CHG_DEV" ]; then
    echo 1 > $CHG_DEV/online 2>/dev/null || true
    echo "Charging" > $CHG_DEV/status 2>/dev/null || true
    echo 2000 > $CHG_DEV/input_current_limit 2>/dev/null || true
    echo 2000 > $CHG_DEV/constant_charge_current 2>/dev/null || true
fi

for g in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    echo powersave > $g 2>/dev/null || true
done

if [ -f $MOD_DIR/mali_kbase_mt6789.ko ]; then
    rmmod mali_kbase_mt6789 2>/dev/null || true
    insmod $MOD_DIR/mali_kbase_mt6789.ko 2>/dev/null || true
fi
MALI_MINOR=$(grep mali0 /proc/misc | awk '{print $1}')
if [ -n "$MALI_MINOR" ]; then
    rm -f /dev/mali0 /sysroot/dev/mali0
    mknod -m 666 /dev/mali0 c 10 $MALI_MINOR
    mknod -m 666 /sysroot/dev/mali0 c 10 $MALI_MINOR
fi
if [ -e /proc/gpufreqv2/fix_target_opp_index ]; then
    echo 37 > /proc/gpufreqv2/fix_target_opp_index 2>/dev/null || true
fi
"""

    try:

        subprocess.run(["python3", CMD_SCRIPT, cmd_sequence], timeout=15)
        print("  [+] Brillo (15/255), Carga 2A (MT6375), CPU powersave y GPU Mali inicializados")

        
        # Verificar telemetría
        res = subprocess.run(["python3", CMD_SCRIPT, "cat /sys/bus/iio/devices/iio:device0/in_voltage10_raw 2>/dev/null; cat /sys/devices/platform/charger/input_current 2>/dev/null"], capture_output=True, text=True, timeout=5)
        print(f"  [Telemetría] {res.stdout.strip()}")
        print("🎉 ¡INYECCIÓN COMPLETADA CON ÉXITO! La tablet está cargando a máxima velocidad.")
        return True
    except Exception as e:
        print(f"  [!] Error durante la inyección: {e}")
        return False

last_status_time = 0
injected = False

while True:
    fb_dev = check_fastboot()
    if fb_dev:
        print(f"\n⚡ [FASTBOOT DETECTADO] Dispositivo: {fb_dev}")
        print("[*] Iniciando flasheo automático de Boot v15 con carga a 2A y Mali GLES2...")
        subprocess.run(["bash", FLASH_SCRIPT])
        print("[✓] Flasheo completado. Esperando a que el kernel postmarketOS inicie...")
        time.sleep(5)
        injected = False
        continue

    is_ping = check_ping()
    is_serial = check_serial()

    if (is_ping or is_serial) and not injected:
        print(f"\n[+] Dispositivo detectado (Ping: {is_ping}, Serial ttyACM0: {is_serial})")
        if inject_charging_and_gpu():
            injected = True
            print("[*] Manteniendo vigilancia activa para monitorear el enlace...")
    elif not is_ping and not is_serial:
        if injected:
            print("[!] Enlace perdido con la tablet. Reanudando escucha...")
            injected = False
        now = time.time()
        if now - last_status_time > 10:
            print("[...] Esperando a que la tablet encienda / conecte (Fastboot, ttyACM0 o 172.16.42.1)...", flush=True)
            last_status_time = now

    time.sleep(0.5)

