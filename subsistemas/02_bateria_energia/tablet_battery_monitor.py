#!/usr/bin/env python3
# ==============================================================================
# Monitor de Batería OCV para UPower y Phosh - UMIDIGI G7 Tab Pro (MT6789)
# ==============================================================================
import time
import os
import subprocess

PATH_BAT_ADC = "/sys/bus/iio/devices/iio:device1/in_voltage0_BATADC_input"
FAKE_BAT_DIR = "/tmp/fake_battery"

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

def setup_fake_battery():
    os.makedirs(FAKE_BAT_DIR, exist_ok=True)
    with open(f"{FAKE_BAT_DIR}/type", "w") as f: f.write("Battery\n")
    with open(f"{FAKE_BAT_DIR}/present", "w") as f: f.write("1\n")
    with open(f"{FAKE_BAT_DIR}/technology", "w") as f: f.write("Li-poly\n")
    with open(f"{FAKE_BAT_DIR}/charge_full", "w") as f: f.write("7500000\n")
    with open(f"{FAKE_BAT_DIR}/charge_full_design", "w") as f: f.write("7500000\n")

    for path in ["/sys/class/power_supply/battery", "/sysroot/sys/class/power_supply/battery"]:
        os.makedirs(path, exist_ok=True)
        try:
            mounts = open("/proc/mounts").read()
            if path not in mounts:
                subprocess.call(["mount", "--bind", FAKE_BAT_DIR, path], stderr=subprocess.DEVNULL)
        except Exception:
            pass

def update_battery(v_mv, soc, is_charging=False):
    status = "Charging" if is_charging else "Discharging"
    current = 1500000 if is_charging else -450000
    charge_now = int(soc * 75000)
    
    with open(f"{FAKE_BAT_DIR}/status", "w") as f: f.write(f"{status}\n")
    with open(f"{FAKE_BAT_DIR}/capacity", "w") as f: f.write(f"{soc}\n")
    with open(f"{FAKE_BAT_DIR}/voltage_now", "w") as f: f.write(f"{v_mv * 1000}\n")
    with open(f"{FAKE_BAT_DIR}/current_now", "w") as f: f.write(f"{current}\n")
    with open(f"{FAKE_BAT_DIR}/charge_now", "w") as f: f.write(f"{charge_now}\n")
    
    with open(f"{FAKE_BAT_DIR}/uevent", "w") as f:
        f.write(f"POWER_SUPPLY_NAME=battery\n"
                f"POWER_SUPPLY_TYPE=Battery\n"
                f"POWER_SUPPLY_STATUS={status}\n"
                f"POWER_SUPPLY_PRESENT=1\n"
                f"POWER_SUPPLY_TECHNOLOGY=Li-poly\n"
                f"POWER_SUPPLY_VOLTAGE_NOW={v_mv * 1000}\n"
                f"POWER_SUPPLY_CURRENT_NOW={current}\n"
                f"POWER_SUPPLY_CAPACITY={soc}\n"
                f"POWER_SUPPLY_CHARGE_FULL=7500000\n"
                f"POWER_SUPPLY_CHARGE_NOW={charge_now}\n")

    # Notificar a UPower vía D-Bus
    subprocess.call(["busctl", "call", "org.freedesktop.UPower",
                     "/org/freedesktop/UPower/devices/battery_battery",
                     "org.freedesktop.UPower.Device", "Refresh"],
                    stderr=subprocess.DEVNULL)

def main():
    setup_fake_battery()
    print(">> [BatteryMonitor] Monitor de batería OCV iniciado...")
    while True:
        try:
            if not os.path.exists(PATH_BAT_ADC):
                subprocess.call(["modprobe", "mt635x_auxadc"], stderr=subprocess.DEVNULL)
            
            v_mv = 4120
            if os.path.exists(PATH_BAT_ADC):
                with open(PATH_BAT_ADC, "r") as f:
                    v_mv = int(f.read().strip())
            
            soc = calc_soc(v_mv)
            update_battery(v_mv, soc, is_charging=False)
        except Exception as e:
            pass
        time.sleep(5)

if __name__ == "__main__":
    main()
