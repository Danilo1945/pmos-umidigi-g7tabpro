#!/usr/bin/env python3
import dbus, dbus.service, dbus.mainloop.glib, glob, sys, time, os, subprocess, signal
from gi.repository import GLib

signal.signal(signal.SIGHUP, signal.SIG_IGN)

BUS_NAME = 'org.freedesktop.UPower'
DAEMON_IFACE = 'org.freedesktop.UPower'
DEV_IFACE = 'org.freedesktop.UPower.Device'
PROPS_IFACE = 'org.freedesktop.DBus.Properties'
INTRO_IFACE = 'org.freedesktop.DBus.Introspectable'

CHG_DIR = '/sys/class/power_supply/11017000.i2c:mt6375@34:chg'
IIO_MT6375 = '/sys/bus/iio/devices/iio:device0'
IIO_AUXADC = '/sys/bus/iio/devices/iio:device1'

OCV_TABLE = [
    (4350, 100), (4200, 95), (4100, 88), (4000, 78), (3900, 65),
    (3800, 48), (3700, 28), (3600, 12), (3500, 5), (3400, 1)
]

_smoothed_soc = None
_tick_counter = 0

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

def ensure_charger_modules():
    if os.path.exists(CHG_DIR):
        return
    krel = '5.10.185-android12-9-gcc7d44e69095-dirty'
    for mod in ['mt635x_auxadc', 'charger_class', 'mt6375-charger']:
        subprocess.call(['modprobe', mod], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for prefix in ['', '/sysroot']:
            ko_path = f'{prefix}/lib/modules/{krel}/{mod}.ko'
            if os.path.exists(ko_path):
                subprocess.call(['insmod', ko_path], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

def read_vbus_mv():
    # 1. Leer VBUS desde ADC del Sub-PMIC MT6375 (escala = 6.25 mV)
    raw = read_val(f'{IIO_MT6375}/in_voltage1_raw')
    if raw and raw.lstrip('-').isdigit():
        return int(int(raw) * 6.25)
    # 2. Alternativa: mtk-master-charger voltage_now (mV)
    v_mst = read_val('/sys/class/power_supply/mtk-master-charger/voltage_now')
    if v_mst and v_mst.lstrip('-').isdigit():
        return int(v_mst)
    # 3. Alternativa: divisor VCDT en AUXADC (538 mV ~ 5.0V VBUS)
    vcdt = read_val(f'{IIO_AUXADC}/in_voltage2_VCDT_input')
    if vcdt and vcdt.lstrip('-').isdigit():
        return int(int(vcdt) * 9.4)
    return 0

def read_ibat_ma():
    # Corriente de carga entrante medida por el ADC del MT6375 (escala = 2.5 mA)
    raw = read_val(f'{IIO_MT6375}/in_current6_raw')
    if raw and raw.lstrip('-').isdigit():
        return int(int(raw) * 2.5)
    return 0

def manage_hardware_charging():
    global _tick_counter
    _tick_counter += 1
    ensure_charger_modules()

    vbus_mv = read_vbus_mv()
    usb_present = (vbus_mv >= 4100)

    if os.path.exists(CHG_DIR):
        online_str = read_val(f'{CHG_DIR}/online') or '0'
        status_str = (read_val(f'{CHG_DIR}/status') or '').lower()

        if usb_present:
            need_kick = (
                online_str != '1' or
                'charging' not in status_str or
                'not' in status_str or
                (_tick_counter % 10 == 1)
            )
            if need_kick:
                write_val(f'{CHG_DIR}/online', 1)
                write_val(f'{CHG_DIR}/constant_charge_voltage', 4350)
                write_val(f'{CHG_DIR}/input_current_limit', 1500)
                write_val(f'{CHG_DIR}/constant_charge_current', 1500)
                write_val(f'{CHG_DIR}/status', 1)
            return True
        else:
            if online_str != '0' or ('charging' in status_str and 'not' not in status_str):
                write_val(f'{CHG_DIR}/status', 0)
                write_val(f'{CHG_DIR}/online', 0)
            return False

    return usb_present

def compute_telemetry(charging):
    global _smoothed_soc
    v_mv = None

    # 1. Leer canal ADC real de la celda (BATADC en MT6366)
    for adc_path in [
        f'{IIO_AUXADC}/in_voltage0_BATADC_input',
        '/sys/devices/platform/soc/10026000.pwrap/10026000.pwrap:mt6366/mt635x-auxadc/iio:device1/in_voltage0_BATADC_input'
    ]:
        v = read_val(adc_path)
        if v and v.isdigit():
            val = int(v)
            cand = val // 1000 if val > 100000 else val
            if 3000 <= cand <= 4550:
                v_mv = cand
                break

    # 2. Alternativa desde el ADC VBAT del MT6375 (escala = 1.25 mV)
    if v_mv is None:
        raw_vbat = read_val(f'{IIO_MT6375}/in_voltage4_raw')
        if raw_vbat and raw_vbat.isdigit():
            cand = int(int(raw_vbat) * 1.25)
            if 3000 <= cand <= 4550:
                v_mv = cand

    if v_mv is None:
        v_mv = 4000

    ibat_ma = read_ibat_ma() if charging else 0

    # Compensación IR durante la carga (R_eff ~ 175 mOhm entre conector, trazas y celda)
    if charging and ibat_ma > 20:
        v_ocv = max(3400, v_mv - int(ibat_ma * 0.175))
    else:
        v_ocv = v_mv

    raw_soc = calc_soc(v_ocv)
    if _smoothed_soc is None:
        _smoothed_soc = float(raw_soc)
    else:
        _smoothed_soc = 0.25 * float(raw_soc) + 0.75 * _smoothed_soc

    soc = max(1, min(100, int(_smoothed_soc + 0.5)))
    return soc, v_mv / 1000.0, ibat_ma

class BatteryDevice(dbus.service.Object):
    def __init__(self, bus, path):
        self.path = path
        self._p = {}
        self._refresh()
        super().__init__(bus, path)

    def _refresh(self):
        charging = manage_hardware_charging()
        soc, volts, ibat_ma = compute_telemetry(charging)

        # Estado UPower: 1=Charging, 2=Discharging, 4=Fully charged
        if charging:
            state = 4 if (soc >= 99 and ibat_ma < 150) else 1
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
        energy_rate = round((ibat_ma * volts) / 1000.0, 2) if charging else 2.5

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
            'WarningLevel': dbus.UInt32(1),         # UP_DEVICE_LEVEL_NONE = 1
            'BatteryLevel': dbus.UInt32(1),         # UP_DEVICE_LEVEL_NONE = 1 (usa Percentage continuo)
            'Percentage': dbus.Double(float(soc)),
            'Energy': dbus.Double(energy_now),
            'EnergyEmpty': dbus.Double(0.0),
            'EnergyFull': dbus.Double(energy_full),
            'EnergyFullDesign': dbus.Double(energy_full),
            'EnergyRate': dbus.Double(energy_rate),
            'Voltage': dbus.Double(round(volts, 3)),
            'TimeToEmpty': dbus.Int64(0),
            'TimeToFull': dbus.Int64(0),
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
                        'IconName': d._p['IconName'],
                        'Voltage': d._p['Voltage'],
                        'Energy': d._p['Energy'],
                        'EnergyRate': d._p['EnergyRate'],
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
