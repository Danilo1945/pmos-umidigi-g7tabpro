#!/usr/bin/env python3
import dbus, dbus.service, dbus.mainloop.glib, glob, sys, time, os
from gi.repository import GLib

BUS_NAME = 'org.freedesktop.UPower'
DAEMON_IFACE = 'org.freedesktop.UPower'
DEV_IFACE = 'org.freedesktop.UPower.Device'
PROPS_IFACE = 'org.freedesktop.DBus.Properties'
INTRO_IFACE = 'org.freedesktop.DBus.Introspectable'

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

def read_val(path):
    try:
        with open(path) as f:
            return f.read().strip()
    except Exception:
        return None

def compute_soc():
    # 1. Leer canal ADC real de la celda
    for adc_path in [
        '/sys/bus/iio/devices/iio:device1/in_voltage0_BATADC_input',
        '/sys/devices/platform/soc/10026000.pwrap/10026000.pwrap:mt6366/mt635x-auxadc/iio:device1/in_voltage0_BATADC_input'
    ]:
        v = read_val(adc_path)
        if v and v.isdigit():
            val = int(v)
            v_mv = val // 1000 if val > 100000 else val
            if 3000 <= v_mv <= 4500:
                return calc_soc(v_mv), v_mv / 1000.0

    # 2. Alternativa por sysfs power_supply
    for p in glob.glob('/sys/class/power_supply/*/voltage_now'):
        try:
            val = int(read_val(p))
            v_mv = val // 1000 if val > 100000 else val
            if 3000 <= v_mv <= 4500:
                return calc_soc(v_mv), v_mv / 1000.0
        except Exception:
            pass
    return 95, 4.20

class BatteryDevice(dbus.service.Object):
    def __init__(self, bus, path):
        self.path = path
        self._p = {}
        self._refresh()
        super().__init__(bus, path)

    def _refresh(self):
        soc, volts = compute_soc()
        charging = False
        
        # Verificar estado del cargador MT6375 o master charger
        for chg_path in [
            '/sys/class/power_supply/11017000.i2c:mt6375@34:chg/status',
            '/sys/class/power_supply/mtk-master-charger/status',
            '/sys/class/power_supply/battery/status'
        ]:
            st = read_val(chg_path)
            if st and ('harg' in st.lower() or st.lower() == 'charging'):
                charging = True
                break

        # Estado UPower: 1=Charging, 2=Discharging, 4=Fully charged
        if charging:
            state = 4 if soc >= 99 else 1
            icon_suffix = '-charging-symbolic'
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
        self._p = {
            'NativePath': dbus.String('mtk-master-charger'),
            'Vendor': dbus.String('Mediatek'),
            'Model': dbus.String('UMIDIGI G7 Tab Pro'),
            'Serial': dbus.String('TG2403GBA'),
            'PowerSupply': dbus.Boolean(True),
            'HasHistory': dbus.Boolean(False),
            'HasStatistics': dbus.Boolean(False),
            'Online': dbus.Boolean(charging),
            'WarningLevel': dbus.UInt32(0),
            'State': dbus.UInt32(state),
            'Percentage': dbus.Double(float(soc)),
            'IconName': dbus.String(icon_name),
            'TimeToEmpty': dbus.Int64(0),
            'TimeToFull': dbus.Int64(0),
            'Voltage': dbus.Double(float(volts)),
            'UpdateTime': dbus.UInt64(now),
            'BatteryLevel': dbus.UInt32(0),
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

    @dbus.service.signal(PROPS_IFACE, signature='sa{sv}as')
    def PropertiesChanged(self, iface, changed, invalidated):
        pass

    @dbus.service.method(INTRO_IFACE, out_signature='s')
    def Introspect(self):
        return '''<node>
  <interface name="org.freedesktop.UPower.Device">
    <property name="NativePath" type="s" access="read"/>
    <property name="Vendor" type="s" access="read"/>
    <property name="Model" type="s" access="read"/>
    <property name="Serial" type="s" access="read"/>
    <property name="PowerSupply" type="b" access="read"/>
    <property name="HasHistory" type="b" access="read"/>
    <property name="HasStatistics" type="b" access="read"/>
    <property name="Online" type="b" access="read"/>
    <property name="WarningLevel" type="u" access="read"/>
    <property name="State" type="u" access="read"/>
    <property name="Percentage" type="d" access="read"/>
    <property name="IconName" type="s" access="read"/>
    <property name="TimeToEmpty" type="x" access="read"/>
    <property name="TimeToFull" type="x" access="read"/>
    <property name="Voltage" type="d" access="read"/>
    <property name="UpdateTime" type="t" access="read"/>
    <property name="BatteryLevel" type="u" access="read"/>
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
        # False si el cargador está online
        for chg in ['/sys/class/power_supply/11017000.i2c:mt6375@34:chg/status', '/sys/class/power_supply/mtk-master-charger/status']:
            st = read_val(chg)
            if st and ('harg' in st.lower() or st.lower() == 'charging'):
                return False
        return True

    @dbus.service.method(DAEMON_IFACE, in_signature='', out_signature='b')
    def LidIsClosed(self):
        return False

    @dbus.service.method(DAEMON_IFACE, in_signature='', out_signature='b')
    def LidIsPresent(self):
        return False

    @dbus.service.method(DAEMON_IFACE, in_signature='', out_signature='u')
    def CriticalAction(self):
        return 0

    @dbus.service.method(DAEMON_IFACE, in_signature='', out_signature='s')
    def DaemonVersion(self):
        return '1.91.0'

    @dbus.service.method(INTRO_IFACE, out_signature='s')
    def Introspect(self):
        return '<node name=/></node>'

def main():
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
                        'Online': d._p['Online']
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
