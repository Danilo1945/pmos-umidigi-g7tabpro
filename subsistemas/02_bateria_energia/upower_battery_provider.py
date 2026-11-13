#!/usr/bin/env python3
# ==============================================================================
# Proveedor Maestro UPower D-Bus y Gestor de Carga Rápida (MT6375 + MT6366)
# UMIDIGI G7 Tab Pro (TG2403GBA / MediaTek Helio G99 MT6789 - Celda 7500 mAh)
# ==============================================================================
import dbus, dbus.service, dbus.mainloop.glib, glob, sys, time, os, subprocess, signal
from gi.repository import GLib

signal.signal(signal.SIGHUP, signal.SIG_IGN)

BUS_NAME = 'org.freedesktop.UPower'
DAEMON_IFACE = 'org.freedesktop.UPower'
DEV_IFACE = 'org.freedesktop.UPower.Device'
PROPS_IFACE = 'org.freedesktop.DBus.Properties'
INTRO_IFACE = 'org.freedesktop.DBus.Introspectable'

CHG_DIR = '/sys/class/power_supply/11017000.i2c:mt6375@34:chg'
PATH_INIT = '/sys/class/power_supply/battery/init_done'
PATH_UI_SOC = '/sys/devices/platform/soc/10026000.pwrap/10026000.pwrap:mt6366/mt6358-gauge/UI_SOC'
PATH_FG_CURRENT = '/sys/devices/platform/soc/10026000.pwrap/10026000.pwrap:mt6366/mt6358-gauge/FG_Battery_CurrentConsumption'

SOUND_PLUG = '/usr/share/sounds/oxygen/stereo/power-plug.ogg'
SOUND_UNPLUG = '/usr/share/sounds/oxygen/stereo/power-unplug.ogg'

# Tabla OCV oficial de 100 puntos (mV * 10, SoC %) para la celda Li-Po de 7500 mAh
OCV_TABLE_MV10 = [
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

_smoothed_soc = None
_tick_counter = 0
_last_charging = None

def read_val(path):
    try:
        with open(path, 'r') as f:
            return f.read().strip()
    except Exception:
        return None

def write_val(path, val):
    try:
        with open(path, 'w') as f:
            f.write(str(val) + '\n')
        return True
    except Exception:
        return False

def find_iio_mt6375():
    # 1. Ruta física directa en I2C-5 del Sub-PMIC MT6375
    matches = glob.glob('/sys/devices/platform/soc/11017000.i2c/i2c-5/5-0034/11017000.i2c:mt6375@34:adc/iio:device*')
    if matches:
        return matches[0]
    # 2. Búsqueda por atributo name o canales específicos de MT6375
    for d in sorted(glob.glob('/sys/bus/iio/devices/iio:device*')):
        name = (read_val(f'{d}/name') or '').lower()
        if 'mt6375' in name or os.path.exists(f'{d}/in_current6_raw'):
            return d
    return '/sys/bus/iio/devices/iio:device0'

def find_iio_auxadc():
    # 1. Ruta física directa en PWRAP del PMIC principal MT6366 (mt635x-auxadc)
    matches = glob.glob('/sys/devices/platform/soc/10026000.pwrap/10026000.pwrap:mt6366/mt635x-auxadc/iio:device*')
    if matches:
        return matches[0]
    # 2. Búsqueda por presencia del canal BATADC
    for d in sorted(glob.glob('/sys/bus/iio/devices/iio:device*')):
        if os.path.exists(f'{d}/in_voltage0_BATADC_input'):
            return d
    return '/sys/bus/iio/devices/iio:device1'

def calc_soc_mv(v_mv):
    vocv_mv10 = int(round(v_mv * 10.0))
    if vocv_mv10 >= OCV_TABLE_MV10[0][0]:
        return 100.0
    if vocv_mv10 <= OCV_TABLE_MV10[-1][0]:
        return 1.0
    for i in range(len(OCV_TABLE_MV10) - 1):
        v_high, s_high = OCV_TABLE_MV10[i]
        v_low, s_low = OCV_TABLE_MV10[i + 1]
        if v_low <= vocv_mv10 <= v_high:
            frac = (vocv_mv10 - v_low) / float(v_high - v_low) if v_high != v_low else 0.0
            return s_low + frac * (s_high - s_low)
    return 50.0

def play_plug_sound(charging):
    snd = SOUND_PLUG if charging else SOUND_UNPLUG
    if os.path.exists(snd):
        cmd = f"XDG_RUNTIME_DIR=/tmp/runtime-root pw-play {snd} >/dev/null 2>&1 &"
        subprocess.Popen(cmd, shell=True)

def ensure_charger_modules():
    if os.path.exists(CHG_DIR):
        return
    krel = '5.10.185-android12-9-gcc7d44e69095-dirty'
    for mod in ['mt635x_auxadc', 'mt6375-auxadc', 'charger_class', 'mt6375-charger', 'mt6358_battery']:
        subprocess.call(['modprobe', mod], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for prefix in ['', '/sysroot']:
            ko_path = f'{prefix}/lib/modules/{krel}/{mod}.ko'
            if os.path.exists(ko_path):
                subprocess.call(['insmod', ko_path], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

def read_vbus_mv():
    iio_mt6375 = find_iio_mt6375()
    iio_auxadc = find_iio_auxadc()
    # 1. Leer VBUS desde ADC del Sub-PMIC MT6375 (escala = 6.25 mV)
    raw = read_val(f'{iio_mt6375}/in_voltage1_raw')
    if raw and raw.lstrip('-').isdigit():
        val = int(int(raw) * 6.25)
        if val > 1000:
            return val
    # 2. Alternativa: mtk-master-charger voltage_now (mV)
    v_mst = read_val('/sys/class/power_supply/mtk-master-charger/voltage_now')
    if v_mst and v_mst.lstrip('-').isdigit():
        val = int(v_mst)
        if val > 1000:
            return val // 1000 if val > 100000 else val
    # 3. Alternativa: divisor VCDT en AUXADC (538 mV ~ 5.0V VBUS)
    vcdt = read_val(f'{iio_auxadc}/in_voltage2_VCDT_input')
    if vcdt and vcdt.lstrip('-').isdigit():
        return int(int(vcdt) * 9.4)
    return 0

def read_ibat_ma(charging):
    iio_mt6375 = find_iio_mt6375()
    # 1. Corriente de carga entrante medida por el ADC del MT6375 (escala = 2.5 mA)
    if charging:
        raw = read_val(f'{iio_mt6375}/in_current6_raw')
        if raw and raw.lstrip('-').isdigit():
            val = int(int(raw) * 2.5)
            if val > 0:
                return val
    # 2. Lectura del Fuel Gauge MT6358/MT6366 (en uA)
    fg_raw = read_val(PATH_FG_CURRENT)
    if fg_raw and fg_raw.lstrip('-').isdigit():
        fg_ma = abs(int(fg_raw)) // 1000
        if 10 <= fg_ma <= 4000:
            return fg_ma
    return 0 if charging else 450

def manage_hardware_charging():
    global _tick_counter, _last_charging
    _tick_counter += 1
    ensure_charger_modules()
    write_val(PATH_INIT, 1)

    vbus_mv = read_vbus_mv()
    usb_present = (vbus_mv >= 4100)

    if os.path.exists(CHG_DIR):
        online_str = read_val(f'{CHG_DIR}/online') or '0'
        status_str = (read_val(f'{CHG_DIR}/status') or '').lower()

        if usb_present:
            need_kick = (
                online_str != '1' or
                ('charging' not in status_str and 'full' not in status_str) or
                'not' in status_str or
                (_tick_counter % 10 == 1)
            )
            if need_kick:
                write_val(f'{CHG_DIR}/online', 1)
                write_val(f'{CHG_DIR}/constant_charge_voltage', 4350)
                write_val(f'{CHG_DIR}/input_current_limit', 2000)
                write_val(f'{CHG_DIR}/constant_charge_current', 2000)
                write_val(f'{CHG_DIR}/status', 1)
            charging = True
        else:
            if online_str != '0' or ('charging' in status_str and 'not' not in status_str):
                write_val(f'{CHG_DIR}/status', 0)
                write_val(f'{CHG_DIR}/online', 0)
            charging = False
    else:
        charging = usb_present

    if _last_charging is not None and charging != _last_charging:
        play_plug_sound(charging)
    _last_charging = charging
    return charging

def read_vbat_mv():
    iio_auxadc = find_iio_auxadc()
    iio_mt6375 = find_iio_mt6375()

    # 1. Leer canal ADC real de la celda (BATADC en MT6366)
    for adc_path in [
        f'{iio_auxadc}/in_voltage0_BATADC_input',
        '/sys/devices/platform/soc/10026000.pwrap/10026000.pwrap:mt6366/mt635x-auxadc/iio:device1/in_voltage0_BATADC_input',
        '/sys/devices/platform/soc/10026000.pwrap/10026000.pwrap:mt6366/mt635x-auxadc/iio:device0/in_voltage0_BATADC_input',
        '/sys/class/power_supply/battery/voltage_now',
    ]:
        v = read_val(adc_path)
        if v and v.lstrip('-').isdigit():
            val = int(v)
            cand = val // 1000 if val > 100000 else val
            if 2850 <= cand <= 4550:
                return cand

    # 2. Alternativa desde el ADC VBAT del MT6375 (escala = 1.25 mV)
    raw_vbat = read_val(f'{iio_mt6375}/in_voltage4_raw')
    if raw_vbat and raw_vbat.lstrip('-').isdigit():
        cand = int(int(raw_vbat) * 1.25)
        if 2850 <= cand <= 4550:
            return cand

    return None

def compute_telemetry(charging):
    global _smoothed_soc
    v_mv = read_vbat_mv()
    if v_mv is None:
        v_mv = 3850

    ibat_ma = read_ibat_ma(charging)
    chg_status = (read_val(f'{CHG_DIR}/status') or '').lower() if os.path.exists(CHG_DIR) else ''
    is_full_hw = charging and (v_mv >= 4320) and ('full' in chg_status or ibat_ma < 180)

    # Compensación bidireccional de resistencia interna (R_cell ~ 85 mOhm, R_chg_path ~ 120 mOhm)
    if is_full_hw:
        v_ocv = max(float(v_mv), 4371.2)
    elif charging and ibat_ma > 20:
        v_ocv = max(3000.0, float(v_mv) - (ibat_ma * 0.120))
    elif not charging:
        load_ma = min(1500, max(250, ibat_ma))
        v_ocv = min(4371.2, float(v_mv) + (load_ma * 0.085))
    else:
        v_ocv = float(v_mv)

    raw_soc = calc_soc_mv(v_ocv)
    if _smoothed_soc is None:
        _smoothed_soc = float(raw_soc)
    else:
        _smoothed_soc = 0.25 * float(raw_soc) + 0.75 * _smoothed_soc

    soc = max(1, min(100, int(_smoothed_soc + 0.5)))
    write_val(PATH_UI_SOC, soc)
    return soc, v_mv / 1000.0, ibat_ma, is_full_hw

class BatteryDevice(dbus.service.Object):
    def __init__(self, bus, path):
        self.path = path
        self._p = {}
        self._refresh()
        super().__init__(bus, path)

    def _refresh(self):
        charging = manage_hardware_charging()
        soc, volts, ibat_ma, is_full_hw = compute_telemetry(charging)

        # Estado UPower: 1=Charging, 2=Discharging, 4=Fully charged
        if charging:
            state = 4 if (is_full_hw or (soc >= 99 and ibat_ma < 180)) else 1
            icon_suffix = '-charging-symbolic' if state == 1 else '-charged-symbolic'
        else:
            state = 2
            icon_suffix = '-symbolic'

        if soc >= 90:
            level_str = 'full'
        elif soc >= 60:
            level_str = 'good'
        elif soc >= 30:
            level_str = 'medium'
        elif soc >= 15:
            level_str = 'low'
        else:
            level_str = 'caution'

        icon_name = f'battery-{level_str}{icon_suffix}'
        now = int(time.time())
        energy_full = 28.5  # 7500 mAh * 3.8V = 28.5 Wh
        energy_now = round((soc / 100.0) * energy_full, 2)
        if charging:
            eff_ma = max(100, ibat_ma) if state == 1 else 0
            energy_rate = round((eff_ma * volts) / 1000.0, 2)
        else:
            eff_ma = max(250, ibat_ma)
            energy_rate = round((eff_ma * volts) / 1000.0, 2)

        time_to_empty = 0
        time_to_full = 0
        if state == 2 and energy_rate > 0.1:
            time_to_empty = int((energy_now / energy_rate) * 3600)
        elif state == 1 and energy_rate > 0.1:
            time_to_full = int(((energy_full - energy_now) / energy_rate) * 3600)

        if soc <= 5 and not charging:
            warn_level = 4  # Critical
        elif soc <= 12 and not charging:
            warn_level = 3  # Low
        else:
            warn_level = 1  # None

        self._p = {
            'NativePath': dbus.String('mt6375-charger'),
            'Vendor': dbus.String('Mediatek'),
            'Model': dbus.String('UMIDIGI G7 Tab Pro'),
            'Serial': dbus.String('TG2403GBA'),
            'Type': dbus.UInt32(2),                 # UP_DEVICE_KIND_BATTERY = 2
            'PowerSupply': dbus.Boolean(True),
            'IsPresent': dbus.Boolean(True),
            'IsRechargeable': dbus.Boolean(True),
            'HasHistory': dbus.Boolean(False),
            'HasStatistics': dbus.Boolean(False),
            'Online': dbus.Boolean(charging),
            'State': dbus.UInt32(state),
            'Technology': dbus.UInt32(2),           # Lithium-polymer
            'WarningLevel': dbus.UInt32(warn_level),
            'BatteryLevel': dbus.UInt32(1),         # UP_DEVICE_LEVEL_NONE = 1 (usa Percentage continuo)
            'Percentage': dbus.Double(float(soc)),
            'Energy': dbus.Double(energy_now),
            'EnergyEmpty': dbus.Double(0.0),
            'EnergyFull': dbus.Double(energy_full),
            'EnergyFullDesign': dbus.Double(energy_full),
            'EnergyRate': dbus.Double(energy_rate),
            'Voltage': dbus.Double(round(volts, 3)),
            'TimeToEmpty': dbus.Int64(time_to_empty),
            'TimeToFull': dbus.Int64(time_to_full),
            'IconName': dbus.String(icon_name),
            'UpdateTime': dbus.UInt64(now),
        }

    @dbus.service.method(PROPS_IFACE, in_signature='s', out_signature='a{sv}')
    def GetAll(self, iface):
        if iface != DEV_IFACE:
            return dbus.Dictionary(signature='sv')
        self._refresh()
        return dbus.Dictionary(self._p, signature='sv')

    @dbus.service.method(PROPS_IFACE, in_signature='ss', out_signature='v')
    def Get(self, iface, prop):
        if iface != DEV_IFACE:
            return dbus.String('')
        self._refresh()
        return self._p.get(prop, dbus.String(''))

    @dbus.service.method(DEV_IFACE, in_signature='', out_signature='')
    def Refresh(self):
        self._refresh()

    @dbus.service.signal(PROPS_IFACE, signature='sa{sv}as')
    def PropertiesChanged(self, iface, changed, invalidated):
        pass

    @dbus.service.method(INTRO_IFACE, out_signature='s')
    def Introspect(self):
        return '''<node>
  <interface name="org.freedesktop.UPower.Device">
    <method name="Refresh"/>
    <property name="NativePath" type="s" access="read"/>
    <property name="Vendor" type="s" access="read"/>
    <property name="Model" type="s" access="read"/>
    <property name="Serial" type="s" access="read"/>
    <property name="Type" type="u" access="read"/>
    <property name="PowerSupply" type="b" access="read"/>
    <property name="IsPresent" type="b" access="read"/>
    <property name="IsRechargeable" type="b" access="read"/>
    <property name="HasHistory" type="b" access="read"/>
    <property name="HasStatistics" type="b" access="read"/>
    <property name="Online" type="b" access="read"/>
    <property name="State" type="u" access="read"/>
    <property name="Technology" type="u" access="read"/>
    <property name="WarningLevel" type="u" access="read"/>
    <property name="BatteryLevel" type="u" access="read"/>
    <property name="Percentage" type="d" access="read"/>
    <property name="Energy" type="d" access="read"/>
    <property name="EnergyEmpty" type="d" access="read"/>
    <property name="EnergyFull" type="d" access="read"/>
    <property name="EnergyFullDesign" type="d" access="read"/>
    <property name="EnergyRate" type="d" access="read"/>
    <property name="Voltage" type="d" access="read"/>
    <property name="TimeToEmpty" type="x" access="read"/>
    <property name="TimeToFull" type="x" access="read"/>
    <property name="IconName" type="s" access="read"/>
    <property name="UpdateTime" type="t" access="read"/>
  </interface>
  <interface name="org.freedesktop.DBus.Properties">
    <method name="Get">
      <arg name="interface_name" type="s" direction="in"/>
      <arg name="property_name" type="s" direction="in"/>
      <arg name="value" type="v" direction="out"/>
    </method>
    <method name="GetAll">
      <arg name="interface_name" type="s" direction="in"/>
      <arg name="properties" type="a{sv}" direction="out"/>
    </method>
  </interface>
  <interface name="org.freedesktop.DBus.Introspectable">
    <method name="Introspect">
      <arg name="xml_data" type="s" direction="out"/>
    </method>
  </interface>
</node>'''

class UpowerDaemon(dbus.service.Object):
    def __init__(self, bus, dev_paths):
        super().__init__(bus, '/org/freedesktop/UPower')
        self.dev_paths = dev_paths

    @dbus.service.method(DAEMON_IFACE, in_signature='', out_signature='ao')
    def EnumerateDevices(self):
        return dbus.Array([dbus.ObjectPath(p) for p in self.dev_paths])

    @dbus.service.method(DAEMON_IFACE, in_signature='', out_signature='o')
    def GetDisplayDevice(self):
        return dbus.ObjectPath('/org/freedesktop/UPower/devices/DisplayDevice')

    @dbus.service.method(DAEMON_IFACE, in_signature='', out_signature='b')
    def OnBattery(self):
        return not (read_vbus_mv() >= 4100)

    @dbus.service.method(DAEMON_IFACE, in_signature='', out_signature='b')
    def LidIsClosed(self):
        return False

    @dbus.service.method(DAEMON_IFACE, in_signature='', out_signature='b')
    def LidIsPresent(self):
        return False

    @dbus.service.method(DAEMON_IFACE, in_signature='', out_signature='s')
    def GetCriticalAction(self):
        return 'PowerOff'

    @dbus.service.method(DAEMON_IFACE, in_signature='', out_signature='s')
    def DaemonVersion(self):
        return '1.91.0'

    @dbus.service.method(INTRO_IFACE, out_signature='s')
    def Introspect(self):
        return '<node name=/></node>'

def main():
    # Asegurar que no haya demonios conflictivos intentando relanzar upowerd
    subprocess.call(['pkill', '-9', '-f', 'tablet_battery_daemon.py'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.call(['killall', '-9', 'battery_daemon', 'upowerd'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    ensure_charger_modules()
    manage_hardware_charging()

    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    bus = dbus.SystemBus()
    try:
        bus.get_object('org.freedesktop.DBus', '/org/freedesktop/DBus').ReleaseName(BUS_NAME)
    except Exception:
        pass
    time.sleep(0.3)
    try:
        name = dbus.service.BusName(BUS_NAME, bus, do_not_queue=True)
        print('Claimed', BUS_NAME, flush=True)
    except dbus.exceptions.NameExistsException:
        sys.stderr.write('Name still busy\n')
        sys.exit(2)

    paths = [
        '/org/freedesktop/UPower/devices/battery_battery',
        '/org/freedesktop/UPower/devices/battery_fake0',
        '/org/freedesktop/UPower/devices/DisplayDevice',
    ]
    devs = [BatteryDevice(bus, p) for p in paths]
    daemon = UpowerDaemon(bus, paths)
    print('Ready, devices:', paths, flush=True)

    def tick():
        for d in devs:
            try:
                d._refresh()
                d.PropertiesChanged(
                    DEV_IFACE,
                    {
                        'Percentage': d._p['Percentage'],
                        'State': d._p['State'],
                        'WarningLevel': d._p['WarningLevel'],
                        'IconName': d._p['IconName'],
                        'Voltage': d._p['Voltage'],
                        'Energy': d._p['Energy'],
                        'EnergyRate': d._p['EnergyRate'],
                        'TimeToEmpty': d._p['TimeToEmpty'],
                        'TimeToFull': d._p['TimeToFull'],
                        'Online': d._p['Online'],
                        'UpdateTime': d._p['UpdateTime'],
                    },
                    []
                )
            except Exception as e:
                print('tick err', e, flush=True)
        return True

    GLib.timeout_add_seconds(3, tick)
    loop = GLib.MainLoop()
    loop.run()

if __name__ == '__main__':
    main()
