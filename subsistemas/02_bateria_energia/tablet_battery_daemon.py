#!/usr/bin/env python3
# ==============================================================================
# Monitor Inteligente de Batería y Cargadores - UMIDIGI G7 Tab Pro (MT6789)
# postmarketOS Edge (Kernel 5.10.185-pmos aarch64)
# ==============================================================================
import time
import os
import subprocess

PATH_VOLTAGE     = "/sys/class/power_supply/battery/voltage_now"
PATH_RAW_CURRENT = "/sys/devices/platform/soc/10026000.pwrap/10026000.pwrap:mt6366/mt6358-gauge/FG_Battery_CurrentConsumption"
PATH_ONLINE_CHG  = "/sys/devices/platform/soc/11017000.i2c/i2c-5/5-0034/11017000.i2c:mt6375@34:chg/power_supply/11017000.i2c:mt6375@34:chg/online"
PATH_STATUS_CHG  = "/sys/devices/platform/soc/11017000.i2c/i2c-5/5-0034/11017000.i2c:mt6375@34:chg/power_supply/11017000.i2c:mt6375@34:chg/status"
PATH_INIT        = "/sys/class/power_supply/battery/init_done"
PATH_UI_SOC      = "/sys/devices/platform/soc/10026000.pwrap/10026000.pwrap:mt6366/mt6358-gauge/UI_SOC"

PATH_STATUS_BAT  = "/sys/devices/platform/soc/10026000.pwrap/10026000.pwrap:mt6366/mt6358-gauge/power_supply/battery/status"
PATH_CURR_BAT    = "/sys/devices/platform/soc/10026000.pwrap/10026000.pwrap:mt6366/mt6358-gauge/power_supply/battery/current_now"
PATH_MST_CHG     = "/sys/class/power_supply/mtk-master-charger/online"
PATH_SLV_CHG     = "/sys/class/power_supply/mtk-slave-charger/online"

PATH_IN_LIMIT    = "/sys/class/power_supply/11017000.i2c:mt6375@34:chg/input_current_limit"
PATH_CHG_LIMIT   = "/sys/class/power_supply/11017000.i2c:mt6375@34:chg/constant_charge_current"

FAKE_STATUS_FILE  = "/tmp/battery_fake_status"
FAKE_CURRENT_FILE = "/tmp/battery_fake_current"
FAKE_CHG_FILE     = "/tmp/battery_fake_chg"

SOUND_PLUG   = "/usr/share/sounds/oxygen/stereo/power-plug.ogg"
SOUND_UNPLUG = "/usr/share/sounds/oxygen/stereo/power-unplug.ogg"

# OCV Table (mV*10, SoC%) para celda Li-Po 7500mAh
OCV_TABLE = [
    (43712, 100), (43586, 99), (43473, 98), (43362, 97), (43254, 96),
    (43140, 95),  (43029, 94), (42912, 93), (42798, 92), (42682, 91),
    (42567, 90),  (42450, 89), (42337, 88), (42222, 87), (42106, 86),
    (41991, 85),  (41876, 84), (41765, 83), (41650, 82), (41536, 81),
    (41425, 80),  (41317, 79), (41212, 78), (41108, 77), (41006, 76),
    (40908, 75),  (40810, 74), (40713, 73), (40616, 72), (40520, 71),
    (40422, 70),  (40324, 69), (40224, 68), (40124, 67), (40023, 66),
    (39922, 65),  (39820, 64), (39719, 63), (39618, 62), (39517, 61),
    (39417, 60),  (39317, 59), (39218, 58), (39119, 57), (39020, 56),
    (38923, 55),  (38827, 54), (38732, 53), (38637, 52), (38544, 51),
    (38452, 50),  (38361, 49), (38271, 48), (38183, 47), (38096, 46),
    (38010, 45),  (37926, 44), (37843, 43), (37762, 42), (37682, 41),
    (37604, 40),  (37527, 39), (37452, 38), (37378, 37), (37306, 36),
    (37236, 35),  (37167, 34), (37100, 33), (37035, 32), (36972, 31),
    (36911, 30),  (36852, 29), (36795, 28), (36739, 27), (36686, 26),
    (36635, 25),  (36585, 24), (36537, 23), (36490, 22), (36444, 21),
    (36399, 20),  (36354, 19), (36308, 18), (36260, 17), (36209, 16),
    (36153, 15),  (36090, 14), (36017, 13), (35930, 12), (35824, 11),
    (35692, 10),  (35522, 9),  (35300, 8),  (35002, 7),  (34586, 6),
    (33990, 5),   (33100, 4),  (32000, 3),  (31000, 2),  (30000, 1)
]

def read_int(path, default=0):
    try:
        with open(path, "r") as f:
            return int(f.read().strip())
    except:
        return default

def read_str(path, default=""):
    try:
        with open(path, "r") as f:
            return f.read().strip()
    except:
        return default

def write_file(path, val):
    try:
        with open(path, "w") as f:
            f.write(str(val) + "\n")
    except:
        pass

def ensure_charger_limits():
    write_file(PATH_IN_LIMIT, 2000)
    write_file(PATH_CHG_LIMIT, 2000)

def calc_soc(vbat_uv, ibat_ua):
    vbat_mv = vbat_uv / 1000.0
    ibat_ma = ibat_ua / 1000.0
    r_mohm = 85.0
    vocv_mv = vbat_mv - (ibat_ma * r_mohm) / 1000.0
    vocv_mv10 = int(vocv_mv * 10)
    
    if vocv_mv10 >= OCV_TABLE[0][0]:
        return 100
    if vocv_mv10 <= OCV_TABLE[-1][0]:
        return 1
        
    for i in range(len(OCV_TABLE) - 1):
        v_high, soc_high = OCV_TABLE[i]
        v_low, soc_low = OCV_TABLE[i+1]
        if vocv_mv10 <= v_high and vocv_mv10 >= v_low:
            frac = (vocv_mv10 - v_low) / float(v_high - v_low) if v_high != v_low else 0
            return int(soc_low + frac * (soc_high - soc_low) + 0.5)
    return 50

def play_sound(snd_path):
    cmd = f"XDG_RUNTIME_DIR=/tmp/runtime-root pw-play {snd_path} >/dev/null 2>&1 || su danilo -c 'XDG_RUNTIME_DIR=/tmp/runtime-danilo pw-play {snd_path}' >/dev/null 2>&1"
    subprocess.Popen(cmd, shell=True)

def bind_mount_if_needed(target, source):
    try:
        mounts = open("/proc/mounts").read()
        real_target = os.path.realpath(target)
        if real_target not in mounts and target not in mounts:
            subprocess.call(["mount", "--bind", source, target], stderr=subprocess.DEVNULL)
    except:
        pass

def setup_bind_mounts():
    if not os.path.exists(FAKE_STATUS_FILE):
        write_file(FAKE_STATUS_FILE, "Discharging")
    if not os.path.exists(FAKE_CURRENT_FILE):
        write_file(FAKE_CURRENT_FILE, "-500000")
    if not os.path.exists(FAKE_CHG_FILE):
        write_file(FAKE_CHG_FILE, "0")

    targets = [
        PATH_STATUS_BAT,
        "/sysroot" + PATH_STATUS_BAT if os.path.exists("/sysroot" + PATH_STATUS_BAT) else None,
        "/sys/class/power_supply/battery/status",
        "/sysroot/sys/class/power_supply/battery/status" if os.path.exists("/sysroot/sys/class/power_supply/battery/status") else None
    ]
    for t in targets:
        if t and os.path.exists(t):
            bind_mount_if_needed(t, FAKE_STATUS_FILE)

    curr_targets = [
        PATH_CURR_BAT,
        "/sysroot" + PATH_CURR_BAT if os.path.exists("/sysroot" + PATH_CURR_BAT) else None,
        "/sys/class/power_supply/battery/current_now",
        "/sysroot/sys/class/power_supply/battery/current_now" if os.path.exists("/sysroot/sys/class/power_supply/battery/current_now") else None
    ]
    for ct in curr_targets:
        if ct and os.path.exists(ct):
            bind_mount_if_needed(ct, FAKE_CURRENT_FILE)

    if os.path.exists(PATH_MST_CHG):
        bind_mount_if_needed(PATH_MST_CHG, FAKE_CHG_FILE)
    if os.path.exists(PATH_SLV_CHG):
        bind_mount_if_needed(PATH_SLV_CHG, FAKE_CHG_FILE)

def ensure_upowerd_running():
    # Verifica que upowerd esté vivo; si no, lo arranca
    res = subprocess.call(["pgrep", "-x", "upowerd"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if res != 0:
        for p in ["/usr/libexec/upowerd", "/usr/lib/upower/upowerd"]:
            if os.path.exists(p):
                subprocess.Popen([p], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                break

def notify_upower():
    # 1. Notificar a udev mediante sintetización de uevent
    uevent_paths = [
        "/sys/class/power_supply/battery/uevent",
        "/sysroot/sys/class/power_supply/battery/uevent",
        "/sys/devices/platform/soc/10026000.pwrap/10026000.pwrap:mt6366/mt6358-gauge/power_supply/battery/uevent"
    ]
    for up in uevent_paths:
        if os.path.exists(up):
            write_file(up, "change")

    # 2. Llamada directa por D-Bus a Refresh() en UPower
    # Forzar actualización inmediata en UPower y Phosh sin reiniciar ningún proceso
    busctl_cmd = [
        "busctl", "call", "org.freedesktop.UPower",
        "/org/freedesktop/UPower/devices/battery_battery",
        "org.freedesktop.UPower.Device", "Refresh"
    ]
    subprocess.call(busctl_cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

def set_charging_state(charging):
    setup_bind_mounts()
    if charging:
        write_file(FAKE_STATUS_FILE, "Charging")
        write_file(FAKE_CURRENT_FILE, "1500000")
        write_file(FAKE_CHG_FILE, "1")
        play_sound(SOUND_PLUG)
        print("[BatteryDaemon] Cargador CONECTADO -> Sonido plug emitido, icono de rayo activado.")
    else:
        write_file(FAKE_STATUS_FILE, "Discharging")
        write_file(FAKE_CURRENT_FILE, "-500000")
        write_file(FAKE_CHG_FILE, "0")
        play_sound(SOUND_UNPLUG)
        print("[BatteryDaemon] Cargador DESCONECTADO -> Sonido unplug emitido, icono normal.")

    # Asegurar que upowerd esté activo y notificarle el cambio inmediatamente SIN matarlo
    ensure_upowerd_running()
    notify_upower()

def main():
    print(">> [BatteryDaemon v4] Monitor inteligente de Batería y Cargadores iniciado.")
    write_file(PATH_INIT, 1)
    setup_bind_mounts()
    ensure_upowerd_running()
    
    last_charging = None
    debounce_candidate = None
    debounce_count = 0
    REQUIRED_STABLE_CYCLES = 2
    
    last_soc = -1
    smoothed_soc = -1.0
    counter = 0

    while True:
        try:
            write_file(PATH_INIT, 1)
            ensure_charger_limits()
            vbat = read_int(PATH_VOLTAGE, 0)
            if vbat < 2500000:
                # Fallback al ADC de hardware del PMIC MT6366 (AUXADC BATADC en mV)
                for adc_path in [
                    "/sys/bus/iio/devices/iio:device1/in_voltage0_BATADC_input",
                    "/sys/devices/platform/soc/10026000.pwrap/10026000.pwrap:mt6366/mt635x-auxadc/iio:device1/in_voltage0_BATADC_input"
                ]:
                    adc_mv = read_int(adc_path, 0)
                    if adc_mv > 2500:
                        vbat = adc_mv * 1000
                        break

            raw_curr = read_int(PATH_RAW_CURRENT, 0)
            online = read_int(PATH_ONLINE_CHG, 0)
            chg_status = read_str(PATH_STATUS_CHG, "")

            raw_is_charging = (online > 0) or ("Charging" in chg_status) or (raw_curr > 30000)

            # Debounce para evitar falsos positivos
            if raw_is_charging != debounce_candidate:
                debounce_candidate = raw_is_charging
                debounce_count = 1
            else:
                debounce_count += 1

            if debounce_count >= REQUIRED_STABLE_CYCLES:
                confirmed_charging = debounce_candidate
                if confirmed_charging != last_charging:
                    set_charging_state(confirmed_charging)
                    last_charging = confirmed_charging

            if counter % 5 == 0 and vbat > 2500000:
                raw_soc = calc_soc(vbat, raw_curr)
                if smoothed_soc < 0:
                    smoothed_soc = float(raw_soc)
                else:
                    smoothed_soc = 0.2 * raw_soc + 0.8 * smoothed_soc
                soc = int(smoothed_soc + 0.5)
                soc = max(1, min(100, soc))
                if soc != last_soc:
                    write_file(PATH_UI_SOC, soc)
                    last_soc = soc
                    notify_upower()
                    print(f"[BatteryDaemon] V={vbat//1000}mV I={raw_curr//1000}mA SOC={soc}% Charging={last_charging}")
        except Exception as e:
            print("[BatteryDaemon] Excepción en ciclo:", e)

        counter += 1
        time.sleep(1)

if __name__ == "__main__":
    main()
