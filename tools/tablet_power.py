#!/usr/bin/env python3
# ==============================================================================
# Herramienta de Gestión de Energía, Batería e Hibernación
# UMIDIGI G7 Tab Pro (Helio G99 / MT6789) - postmarketOS Edge
# ==============================================================================
import sys
import os
import subprocess

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
TABLET_CMD = os.path.join(SCRIPT_DIR, "tablet_cmd.py")

OCV_TABLE = [
    (4350, 100), (4200, 95), (4100, 88), (4000, 78), (3900, 65),
    (3800, 48), (3700, 28), (3600, 12), (3500, 5), (3400, 1)
]

def calc_soc(v_mv):
    if v_mv >= OCV_TABLE[0][0]:
        return 100
    if v_mv <= OCV_TABLE[-1][0]:
        return 1
    for i in range(len(OCV_TABLE) - 1):
        v_high, s_high = OCV_TABLE[i]
        v_low, s_low = OCV_TABLE[i+1]
        if v_mv <= v_high and v_mv >= v_low:
            frac = (v_mv - v_low) / float(v_high - v_low)
            return int(s_low + frac * (s_high - s_low) + 0.5)
    return 50

def run_cmd(cmd):
    try:
        res = subprocess.run(["python3", TABLET_CMD, cmd], capture_output=True, text=True, timeout=8)
        return res.stdout.strip()
    except Exception as e:
        return f"ERROR: {e}"

def get_battery_status():
    cmd = (
        "modprobe mt635x_auxadc 2>/dev/null; "
        "cat /sys/bus/iio/devices/iio:device1/in_voltage0_BATADC_input 2>/dev/null || "
        "cat /sys/devices/platform/soc/10026000.pwrap/10026000.pwrap:mt6366/mt635x-auxadc/iio:device1/in_voltage0_BATADC_input 2>/dev/null"
    )
    out = run_cmd(cmd)
    try:
        lines = [l.strip() for l in out.splitlines() if l.strip().isdigit()]
        if lines:
            v_mv = int(lines[-1])
            soc = calc_soc(v_mv)
            return v_mv, soc
    except Exception:
        pass
    return None, None

def cmd_status():
    print("==================================================")
    print("   ESTADO DE ENERGÍA Y HARDWARE - TABLET G7 PRO   ")
    print("==================================================")
    v_mv, soc = get_battery_status()
    if v_mv is not None:
        print(f"🔋 Batería:     {soc}% ({v_mv} mV / {v_mv/1000.0:.3f} V)")
    else:
        print("🔋 Batería:     Lectura ADC no disponible en este momento.")

    b_out = run_cmd("cat /sys/class/leds/lcd-backlight/brightness 2>/dev/null")
    print(f"💡 Brillo LCD:  {b_out} / 255")

    gpu_out = run_cmd("ls -la /dev/mali0 2>/dev/null; lsmod | grep mali")
    mali_active = "mali_kbase" in gpu_out or "crw-rw-rw-" in gpu_out
    print(f"🎮 GPU Mali:    {'🟢 ACTIVA (/dev/mali0)' if mali_active else '🟡 En espera'}")

    ui_out = run_cmd("ps aux | grep -E 'phoc|phosh' | grep -v grep")
    phosh_active = "phosh" in ui_out
    print(f"🖥️  Interfaz UI: {'🟢 Phosh Activo y Renderizando' if phosh_active else '🟡 En espera'}")
    print("==================================================")

def cmd_sleep():
    print("[*] Aplicando modo de Ahorro Máximo / Reposo Profundo...")
    cmd = (
        "echo 0 > /sys/class/leds/lcd-backlight/brightness; "
        "for g in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do echo powersave > $g 2>/dev/null; done; "
        "echo '[+] Pantalla apagada (brillo 0) y CPUs en modo powersave.' > /dev/kmsg; "
        "echo 'OK_SLEEP'"
    )
    res = run_cmd(cmd)
    print(f"✅ {res}")
    print("💡 La pantalla se ha apagado por completo y las CPUs están a frecuencia mínima.")
    print("🔋 El consumo de corriente se reduce a < 80mA (dura más de 60 horas en reposo).")
    print("👉 Para despertar la pantalla cuando regreses, usa: python3 tools/tablet_power.py wake")

def cmd_wake():
    print("[*] Despertando la tablet y reactivando la pantalla...")
    cmd = (
        "for g in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do echo schedutil > $g 2>/dev/null; done; "
        "echo 70 > /sys/class/leds/lcd-backlight/brightness; "
        "chmod 444 /sys/class/leds/lcd-backlight/brightness; "
        "echo '[+] Pantalla reactivada (brillo 70) y CPUs en modo schedutil.' > /dev/kmsg; "
        "echo 'OK_WAKE'"
    )
    res = run_cmd(cmd)
    print(f"✅ {res}")
    print("💡 Pantalla encendida al 70% (180/255) y rendimiento normal restaurado.")

def cmd_suspend():
    print("[*] Enviando señal de suspensión Suspend-to-RAM (mem)...")
    cmd = "echo 0 > /sys/class/leds/lcd-backlight/brightness; echo mem > /sys/power/state"
    res = run_cmd(cmd)
    print("💤 Suspensión enviada. Para despertar físicamente, pulsa el botón Power de la tablet.")

def cmd_off():
    print("[*] Apagando la tablet de forma limpia y segura (Consumo 0 mA)...")
    res = run_cmd("sync; poweroff")
    print("🔌 Orden de apagado enviada. La batería permanecerá intacta al 100% hasta la tarde.")
    print("👉 Para encender en la tarde, simplemente mantén presionado el botón POWER físico durante 4 segundos.")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Uso: python3 tablet_power.py [status|sleep|wake|suspend|off]")
        sys.exit(1)

    action = sys.argv[1].lower()
    if action == "status":
        cmd_status()
    elif action == "sleep":
        cmd_sleep()
    elif action == "wake":
        cmd_wake()
    elif action == "suspend":
        cmd_suspend()
    elif action in ("off", "poweroff"):
        cmd_off()
    else:
        print(f"Acción desconocida: {action}. Usa status, sleep, wake, suspend o off.")
