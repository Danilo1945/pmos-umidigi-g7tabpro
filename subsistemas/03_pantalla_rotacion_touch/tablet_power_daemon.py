#!/usr/bin/env python3
# ==============================================================================
# DAEMON DE GESTIÓN DE HARDWARE, ENERGÍA, BLOQUEO Y ROTACIÓN
# UMIDIGI G7 Tab Pro (MT6789 Helio G99) - postmarketOS Phosh
# ==============================================================================
import os
import sys
import glob
import time
import select
import struct
import fcntl
import array
import subprocess
import threading

EVENT_FORMAT = 'llHHI'
EVENT_SIZE = struct.calcsize(EVENT_FORMAT)
EV_KEY = 1
EV_SW = 5

KEY_VOLUMEDOWN = 114
KEY_VOLUMEUP   = 115
KEY_POWER      = 116

EVIOCGRAB = 0x40044590

is_screen_on = True
saved_backlight_level = 15
last_power_press_time = 0.0

current_sensor_orientation = "bottom-up"
sensor_dbus_conn = None
login1_dbus_conn = None

def get_session_dbus_address():
    if "DBUS_SESSION_BUS_ADDRESS" in os.environ and os.environ["DBUS_SESSION_BUS_ADDRESS"]:
        return os.environ["DBUS_SESSION_BUS_ADDRESS"]
    for p in glob.glob('/proc/[0-9]*/environ'):
        try:
            with open(p, 'rb') as f:
                env = f.read().split(b'\0')
            for item in env:
                if item.startswith(b'DBUS_SESSION_BUS_ADDRESS='):
                    addr = item.split(b'=', 1)[1].decode('utf-8')
                    os.environ['DBUS_SESSION_BUS_ADDRESS'] = addr
                    return addr
        except Exception:
            pass
    return None

def get_current_backlight():
    try:
        with open("/sys/class/leds/lcd-backlight/brightness", "r") as f:
            val = int(f.read().strip())
            return val
    except Exception:
        return 15

def set_backlight_hardware(level):
    if level < 15 and level != 0:
        level = 15
    try:
        with open("/sys/class/leds/lcd-backlight/brightness", "w") as f:
            f.write(f"{int(level)}\n")
    except Exception:
        pass

def set_cpu_governors(governor="schedutil"):
    try:
        for g in glob.glob("/sys/devices/system/cpu/cpu*/cpufreq/scaling_governor"):
            try:
                with open(g, "w") as f:
                    f.write(f"{governor}\n")
            except Exception:
                pass
    except Exception:
        pass

def lock_screen():
    try:
        get_session_dbus_address()
        from gi.repository import Gio
        bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
        proxy = Gio.DBusProxy.new_sync(
            bus, Gio.DBusProxyFlags.NONE, None,
            'org.gnome.ScreenSaver', '/org/gnome/ScreenSaver',
            'org.gnome.ScreenSaver', None
        )
        proxy.Lock()
    except Exception as e:
        print(f"[-] Error en ScreenSaver.Lock: {e}", flush=True)

def wake_screen():
    try:
        get_session_dbus_address()
        from gi.repository import Gio
        bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
        proxy = Gio.DBusProxy.new_sync(
            bus, Gio.DBusProxyFlags.NONE, None,
            'org.gnome.ScreenSaver', '/org/gnome/ScreenSaver',
            'org.gnome.ScreenSaver', None
        )
        proxy.SetActive('(b)', False)
    except Exception as e:
        print(f"[-] Error en ScreenSaver.SetActive(False): {e}", flush=True)

def ensure_desired_orientation():
    global current_sensor_orientation
    target_transform = "180" if current_sensor_orientation == "bottom-up" else ("90" if current_sensor_orientation == "left-up" else "normal")
    try:
        cmd = f"WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/tmp/runtime-root /usr/bin/wlr-randr --output DSI-1 --on --transform {target_transform} 2>/dev/null || WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/tmp/runtime-root wlr-randr --output DSI-1 --on --transform {target_transform} 2>/dev/null || true"
        subprocess.call(cmd, shell=True)
    except Exception:
        pass

def sleep_tablet():
    global is_screen_on, saved_backlight_level, login1_dbus_conn
    print("[*] Reposo/Bloqueo activado: apagando pantalla y ahorrando energía...", flush=True)
    cur = get_current_backlight()
    if cur > 10:
        saved_backlight_level = cur

    if login1_dbus_conn:
        try:
            from gi.repository import GLib
            login1_dbus_conn.emit_signal(
                None, '/org/freedesktop/login1', 'org.freedesktop.login1.Manager',
                'PrepareForSleep', GLib.Variant('(b)', (True,))
            )
        except Exception:
            pass

    # 1. Bloquear sesión de Phosh
    lock_screen()

    # 2. Apagar backlight físico a 0 (cero emisión de luz)
    set_backlight_hardware(0)

    # 3. Gobernadores de CPU en powersave (mínimo consumo)
    set_cpu_governors("powersave")

    is_screen_on = False
    print("[*] Tablet en reposo: pantalla 0 nits, CPUs en powersave.", flush=True)

def wake_tablet():
    global is_screen_on, saved_backlight_level, login1_dbus_conn
    print("[*] Despertando tablet: reactivando pantalla y rendimiento...", flush=True)
    # 1. Restaurar CPUs a schedutil
    set_cpu_governors("schedutil")

    # 2. Reactivar salida DRM con rotación al derecho 180°
    ensure_desired_orientation()

    # 3. Restaurar brillo LCD
    target_b = saved_backlight_level if saved_backlight_level >= 15 else 15
    set_backlight_hardware(target_b)

    # 4. Despertar lockscreen
    wake_screen()

    if login1_dbus_conn:
        try:
            from gi.repository import GLib
            login1_dbus_conn.emit_signal(
                None, '/org/freedesktop/login1', 'org.freedesktop.login1.Manager',
                'PrepareForSleep', GLib.Variant('(b)', (False,))
            )
        except Exception:
            pass

    is_screen_on = True
    print(f"[*] Tablet activa: pantalla a brillo {target_b}, Phosh lockscreen mostrado.", flush=True)

def toggle_screen_power():
    global last_power_press_time, is_screen_on
    now = time.time()
    if now - last_power_press_time < 0.35:
        return
    last_power_press_time = now

    if is_screen_on:
        sleep_tablet()
    else:
        wake_tablet()

def show_volume_osd(vol, is_muted=False):
    try:
        get_session_dbus_address()
        from gi.repository import Gio, GLib
        bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
        proxy = Gio.DBusProxy.new_sync(
            bus, Gio.DBusProxyFlags.NONE, None,
            'org.gnome.Shell', '/org/gnome/Shell', 'org.gnome.Shell', None
        )
        level = max(0.0, min(1.0, float(vol)))
        icon_name = 'audio-volume-muted-symbolic' if (is_muted or level <= 0.03) else ('audio-volume-high-symbolic' if level > 0.6 else 'audio-volume-medium-symbolic')
        params = {
            'icon': GLib.Variant('s', icon_name),
            'label': GLib.Variant('s', 'Volumen'),
            'level': GLib.Variant('d', level),
            'max-level': GLib.Variant('d', 1.0)
        }
        proxy.ShowOSD('(a{sv})', params)
    except Exception:
        pass

def adjust_volume(up=True):
    try:
        delta = "5%+" if up else "5%-"
        cmd = f"XDG_RUNTIME_DIR=/tmp/runtime-root wpctl set-volume -l 1.0 @DEFAULT_AUDIO_SINK@ {delta} 2>/dev/null || wpctl set-volume -l 1.0 @DEFAULT_AUDIO_SINK@ {delta} 2>/dev/null || amixer set Master {delta} 2>/dev/null"
        subprocess.run(cmd, shell=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        res = subprocess.run("XDG_RUNTIME_DIR=/tmp/runtime-root wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null", shell=True, capture_output=True, text=True)
        out = res.stdout.strip()
        parts = out.split()
        if len(parts) >= 2:
            vol = float(parts[1])
            is_muted = "[MUTED]" in out
            show_volume_osd(vol, is_muted)
    except Exception as e:
        print(f"[-] Error ajustando volumen: {e}", flush=True)

def start_dbus_services():
    global login1_dbus_conn
    get_session_dbus_address()
    from gi.repository import Gio, GLib

    LOGIN1_XML = '''
    <node>
      <interface name="org.freedesktop.login1.Session">
        <method name="SetBrightness">
          <arg type="s" name="subsystem" direction="in"/>
          <arg type="s" name="device" direction="in"/>
          <arg type="u" name="value" direction="in"/>
        </method>
      </interface>
      <interface name="org.freedesktop.login1.Manager">
        <method name="GetSession">
          <arg type="s" name="session_id" direction="in"/>
          <arg type="o" name="session" direction="out"/>
        </method>
        <method name="CanSuspend">
          <arg type="s" name="result" direction="out"/>
        </method>
        <method name="CanPowerOff">
          <arg type="s" name="result" direction="out"/>
        </method>
        <method name="CanReboot">
          <arg type="s" name="result" direction="out"/>
        </method>
        <method name="Suspend">
          <arg type="b" name="interactive" direction="in"/>
        </method>
        <method name="PowerOff">
          <arg type="b" name="interactive" direction="in"/>
        </method>
        <method name="Reboot">
          <arg type="b" name="interactive" direction="in"/>
        </method>
        <signal name="PrepareForSleep">
          <arg type="b" name="start"/>
        </signal>
        <signal name="PrepareForShutdown">
          <arg type="b" name="start"/>
        </signal>
        <property name="PreparingForSleep" type="b" access="read"/>
        <property name="PreparingForShutdown" type="b" access="read"/>
      </interface>
    </node>
    '''
    node_info = Gio.DBusNodeInfo.new_for_xml(LOGIN1_XML)

    def handle_login1_method(conn, sender, path, iface, method, params, invocation):
        global saved_backlight_level
        if method == "SetBrightness":
            sub, dev, val = params.unpack()
            val_int = int(val)
            set_backlight_hardware(val_int)
            if val_int > 5:
                saved_backlight_level = val_int
            invocation.return_value(None)
        elif method == "GetSession":
            invocation.return_value(GLib.Variant('(o)', ('/org/freedesktop/login1/session/auto',)))
        elif method == "CanSuspend":
            invocation.return_value(GLib.Variant('(s)', ('yes',)))
        elif method == "CanPowerOff":
            invocation.return_value(GLib.Variant('(s)', ('yes',)))
        elif method == "CanReboot":
            invocation.return_value(GLib.Variant('(s)', ('yes',)))
        elif method == "Suspend":
            print("[*] DBus login1.Manager.Suspend llamado desde la GUI de Phosh!", flush=True)
            sleep_tablet()
            invocation.return_value(None)
        elif method == "PowerOff":
            print("[*] DBus login1.Manager.PowerOff llamado (ignorado para estabilidad de depuración)", flush=True)
            invocation.return_value(None)
        elif method == "Reboot":
            print("[*] DBus login1.Manager.Reboot llamado (ignorado para estabilidad de depuración)", flush=True)
            invocation.return_value(None)
        else:
            invocation.return_value(None)

    def handle_login1_get_prop(conn, sender, path, iface, prop):
        if prop == "PreparingForSleep":
            return GLib.Variant('b', not is_screen_on)
        elif prop == "PreparingForShutdown":
            return GLib.Variant('b', False)
        return None

    def on_system_bus_acquired(conn, name):
        global login1_dbus_conn
        login1_dbus_conn = conn
        conn.register_object(
            '/org/freedesktop/login1/session/auto',
            node_info.interfaces[0],
            handle_login1_method, None, None
        )
        conn.register_object(
            '/org/freedesktop/login1/session/self',
            node_info.interfaces[0],
            handle_login1_method, None, None
        )
        conn.register_object(
            '/org/freedesktop/login1',
            node_info.interfaces[1],
            handle_login1_method,
            handle_login1_get_prop,
            None
        )

    # 2. net.hadess.SensorProxy para rotación en Phosh
    SENSOR_XML = '''
    <node>
      <interface name="net.hadess.SensorProxy">
        <method name="ClaimAccelerometer"/>
        <method name="ReleaseAccelerometer"/>
        <method name="ClaimLight"/>
        <method name="ReleaseLight"/>
        <method name="SetOrientation">
          <arg type="s" name="orientation" direction="in"/>
        </method>
        <property name="HasAccelerometer" type="b" access="read"/>
        <property name="AccelerometerOrientation" type="s" access="read"/>
        <property name="HasAmbientLight" type="b" access="read"/>
      </interface>
    </node>
    '''
    sensor_node_info = Gio.DBusNodeInfo.new_for_xml(SENSOR_XML)

    def set_sensor_orientation(new_orientation):
        global current_sensor_orientation, sensor_dbus_conn
        current_sensor_orientation = new_orientation
        target_transform = "180" if new_orientation == "bottom-up" else ("90" if new_orientation == "left-up" else "normal")
        print(f"[*] Cambio de orientación a: {new_orientation} (transform={target_transform})", flush=True)
        if sensor_dbus_conn:
            try:
                changed_props = GLib.Variant('a{sv}', {'AccelerometerOrientation': GLib.Variant('s', new_orientation)})
                sensor_dbus_conn.emit_signal(
                    None, '/net/hadess/SensorProxy', 'org.freedesktop.DBus.Properties', 'PropertiesChanged',
                    GLib.Variant('(sa{sv}as)', ('net.hadess.SensorProxy', changed_props, GLib.Variant('as', [])))
                )
            except Exception:
                pass
        try:
            cmd = f"WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/tmp/runtime-root wlr-randr --output DSI-1 --on --transform {target_transform} 2>/dev/null || true"
            subprocess.call(cmd, shell=True)
        except Exception:
            pass

    def handle_sensor_method(conn, sender, path, iface, method, params, invocation):
        if method == "SetOrientation":
            orient = params.unpack()[0]
            set_sensor_orientation(orient)
        invocation.return_value(None)

    def handle_sensor_get_prop(conn, sender, path, iface, prop):
        if prop == 'HasAccelerometer':
            return GLib.Variant('b', True)
        elif prop == 'AccelerometerOrientation':
            return GLib.Variant('s', current_sensor_orientation)
        elif prop == 'HasAmbientLight':
            return GLib.Variant('b', False)
        return None

    def on_sensor_bus_acquired(conn, name):
        global sensor_dbus_conn
        sensor_dbus_conn = conn
        conn.register_object(
            '/net/hadess/SensorProxy',
            sensor_node_info.interfaces[0],
            handle_sensor_method,
            handle_sensor_get_prop,
            None
        )

    try:
        Gio.bus_own_name(Gio.BusType.SYSTEM, 'org.freedesktop.login1', Gio.BusNameOwnerFlags.NONE, on_system_bus_acquired, None, None)
    except Exception as e:
        print(f"[-] Error registrando org.freedesktop.login1: {e}", flush=True)

    try:
        Gio.bus_own_name(Gio.BusType.SYSTEM, 'net.hadess.SensorProxy', Gio.BusNameOwnerFlags.NONE, on_sensor_bus_acquired, None, None)
    except Exception as e:
        print(f"[-] Error registrando net.hadess.SensorProxy: {e}", flush=True)

    # 3. org.gnome.SessionManager en el Session Bus
    SESSION_XML = '''
    <node>
      <interface name="org.gnome.SessionManager">
        <method name="Shutdown"/>
        <method name="Reboot"/>
        <method name="CanShutdown">
          <arg type="b" name="result" direction="out"/>
        </method>
        <method name="IsSessionRunning">
          <arg type="b" name="result" direction="out"/>
        </method>
        <method name="Logout">
          <arg type="u" name="mode" direction="in"/>
        </method>
      </interface>
    </node>
    '''
    session_node_info = Gio.DBusNodeInfo.new_for_xml(SESSION_XML)

    def handle_session_method(conn, sender, path, iface, method, params, invocation):
        if method == "Shutdown":
            print("[*] SessionManager.Shutdown recibido (ignorado para estabilidad)", flush=True)
            invocation.return_value(None)
        elif method == "Reboot":
            print("[*] SessionManager.Reboot recibido (ignorado para estabilidad)", flush=True)
            invocation.return_value(None)
        elif method == "CanShutdown":
            invocation.return_value(GLib.Variant('(b)', (True,)))
        elif method == "IsSessionRunning":
            invocation.return_value(GLib.Variant('(b)', (True,)))
        else:
            invocation.return_value(None)

    def on_session_bus_acquired(conn, name):
        conn.register_object(
            '/org/gnome/SessionManager',
            session_node_info.interfaces[0],
            handle_session_method, None, None
        )

    try:
        Gio.bus_own_name(Gio.BusType.SESSION, 'org.gnome.SessionManager', Gio.BusNameOwnerFlags.NONE, on_session_bus_acquired, None, None)
    except Exception as e:
        print(f"[-] Error registrando org.gnome.SessionManager: {e}", flush=True)

    # 4. Listener de desbloqueo para mantener 180°
    def on_screensaver_signal(conn, sender, path, iface, signal, params):
        if signal == "ActiveChanged":
            is_active = params.unpack()[0]
            if not is_active:
                ensure_desired_orientation()

    try:
        session_bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
        session_bus.signal_subscribe(
            'org.gnome.ScreenSaver', 'org.gnome.ScreenSaver', 'ActiveChanged',
            '/org/gnome/ScreenSaver', None, Gio.DBusSignalFlags.NONE,
            on_screensaver_signal
        )
    except Exception:
        pass

    loop = GLib.MainLoop()
    loop.run()

def get_grabbed_devices():
    devs = {}
    for ev in sorted(glob.glob('/dev/input/event*')):
        try:
            ev_name = os.path.basename(ev)
            name_file = f"/sys/class/input/{ev_name}/device/name"
            dname = ""
            if os.path.exists(name_file):
                with open(name_file, "r") as f:
                    dname = f.read().strip()
            if "mtk-pmic-keys" in dname or "mtk-kpd" in dname:
                fd = os.open(ev, os.O_RDONLY | os.O_NONBLOCK)
                try:
                    fcntl.ioctl(fd, EVIOCGRAB, 1)
                    print(f"[+] EVIOCGRAB activo en {dname} ({ev})", flush=True)
                except Exception as e:
                    print(f"[-] Error aplicando EVIOCGRAB a {dname}: {e}", flush=True)
                devs[fd] = (ev, dname)
        except Exception:
            pass
    return devs

def main():
    print("[+] tablet_power_daemon: Iniciando controlador de botón Power y suspensión...", flush=True)
    try:
        subprocess.call("HOME=/root dconf write /sm/puri/phosh/enable-suspend true 2>/dev/null", shell=True)
    except Exception:
        pass

    t = threading.Thread(target=start_dbus_services, daemon=True)
    t.start()

    devs = get_grabbed_devices()
    ensure_desired_orientation()

    last_check_orient = time.time()

    while True:
        now = time.time()
        if now - last_check_orient > 3.0:
            last_check_orient = now
            if is_screen_on:
                ensure_desired_orientation()

        if not devs:
            time.sleep(1)
            devs = get_grabbed_devices()
            continue

        try:
            r, _, _ = select.select(list(devs.keys()), [], [], 0.5)
        except Exception:
            devs = get_grabbed_devices()
            continue

        for fd in r:
            try:
                data = os.read(fd, EVENT_SIZE)
                if len(data) < EVENT_SIZE:
                    continue
                sec, usec, etype, code, val = struct.unpack(EVENT_FORMAT, data)

                if etype == EV_KEY and val == 1:
                    if code == KEY_POWER:
                        toggle_screen_power()
                    elif code == KEY_VOLUMEUP:
                        adjust_volume(up=True)
                    elif code == KEY_VOLUMEDOWN:
                        adjust_volume(up=False)

            except Exception:
                try:
                    os.close(fd)
                except Exception:
                    pass
                devs.pop(fd, None)

if __name__ == "__main__":
    main()
