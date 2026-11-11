#!/usr/bin/env python3
# ==============================================================================
# DAEMON MAESTRO DE GESTIÓN DE ENERGÍA, BOTÓN POWER, VOLUMEN, BRILLO Y SUSPENSIÓN
# UMIDIGI G7 Tab Pro (MT6789 Helio G99) - postmarketOS Phosh + GPU Mali-G57
# ==============================================================================
import os
import sys
import glob
import time
import select
import struct
import fcntl
import subprocess
import threading
import signal

signal.signal(signal.SIGHUP, signal.SIG_IGN)

EVENT_FORMAT = 'llHHI'
EVENT_SIZE = struct.calcsize(EVENT_FORMAT)
EV_KEY = 1

KEY_VOLUMEDOWN = 114
KEY_VOLUMEUP   = 115
KEY_POWER      = 116

EVIOCGRAB = 0x40044590

MIN_SAFE_BRIGHTNESS = 15
MAX_SAFE_BRIGHTNESS = 100

is_screen_on = True
saved_backlight_level = 35
last_power_press_time = 0.0
touch_grab_fds = []

current_sensor_orientation = "bottom-up"
sensor_dbus_conn = None
login1_dbus_conn = None
_cached_session_addr = None
_cached_session_conn = None

BACKLIGHT_PATHS = [
    "/sys/devices/platform/soc/soc:mtk_leds/leds/lcd-backlight/brightness",
    "/sys/class/leds/lcd-backlight/brightness",
]

def release_static_backlight_locks():
    """Libera bloqueos estáticos de depuración para permitir que este daemon apague la pantalla en reposo."""
    subprocess.run("killall -9 backlight_lock backlight_guard 2>/dev/null || true", shell=True)
    for bp in BACKLIGHT_PATHS:
        subprocess.run(f"umount -l {bp} 2>/dev/null || true", shell=True)
        subprocess.run(f"chmod 666 {bp} 2>/dev/null || true", shell=True)

def get_session_dbus_address():
    """Obtiene siempre la dirección D-Bus de la sesión activa de Phosh en tiempo real."""
    # Priorizar el proceso phosh activo
    try:
        out = subprocess.check_output(["pidof", "phosh"], stderr=subprocess.DEVNULL).decode().strip()
        for pid in out.split():
            with open(f"/proc/{pid}/environ", "rb") as f:
                for item in f.read().split(b"\0"):
                    if item.startswith(b"DBUS_SESSION_BUS_ADDRESS="):
                        addr = item.split(b"=", 1)[1].decode("utf-8")
                        os.environ["DBUS_SESSION_BUS_ADDRESS"] = addr
                        return addr
    except Exception:
        pass

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
    return os.environ.get("DBUS_SESSION_BUS_ADDRESS")

def get_phosh_session_bus():
    """Crea o reutiliza una conexión Gio.DBusConnection directa al bus de sesión actual de Phosh."""
    global _cached_session_addr, _cached_session_conn
    addr = get_session_dbus_address()
    if not addr:
        return None
    from gi.repository import Gio
    if _cached_session_conn is not None and _cached_session_addr == addr and not _cached_session_conn.is_closed():
        return _cached_session_conn
    try:
        flags = Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION
        _cached_session_conn = Gio.DBusConnection.new_for_address_sync(addr, flags, None, None)
        _cached_session_addr = addr
        return _cached_session_conn
    except Exception as e:
        print(f"[-] Error conectando al bus de sesión ({addr}): {e}", flush=True)
        _cached_session_conn = None
        return None

def grab_touchscreen(grab=True):
    """Bloquea el panel táctil mientras la pantalla está apagada para evitar toques fantasma."""
    global touch_grab_fds
    if not grab:
        for fd in touch_grab_fds:
            try:
                fcntl.ioctl(fd, EVIOCGRAB, 0)
            except Exception:
                pass
            try:
                os.close(fd)
            except Exception:
                pass
        touch_grab_fds = []
        return

    if touch_grab_fds:
        return
    for ev in sorted(glob.glob('/dev/input/event*')):
        try:
            ev_name = os.path.basename(ev)
            name_file = f"/sys/class/input/{ev_name}/device/name"
            if os.path.exists(name_file):
                with open(name_file, "r") as f:
                    dname = f.read().strip().lower()
                if "chipone" in dname or "tddi" in dname or "fts" in dname:
                    fd = os.open(ev, os.O_RDONLY | os.O_NONBLOCK)
                    fcntl.ioctl(fd, EVIOCGRAB, 1)
                    touch_grab_fds.append(fd)
        except Exception:
            pass

def get_current_backlight():
    for bp in BACKLIGHT_PATHS:
        try:
            if os.path.exists(bp):
                with open(bp, "r") as f:
                    return int(f.read().strip())
        except Exception:
            pass
    return saved_backlight_level

def set_backlight_hardware(level):
    level = int(level)
    if level != 0:
        level = max(MIN_SAFE_BRIGHTNESS, min(MAX_SAFE_BRIGHTNESS, level))
    for bp in BACKLIGHT_PATHS:
        try:
            if os.path.exists(bp):
                os.chmod(bp, 0o666)
                with open(bp, "w") as f:
                    f.write(f"{level}\n")
        except Exception:
            pass
    for dp in ["/run/dummy_brightness", "/sysroot/run/dummy_brightness"]:
        try:
            if os.path.exists(dp):
                with open(dp, "w") as f:
                    f.write(f"{level}\n")
        except Exception:
            pass

def set_cpu_and_gpu_power(performance=True):
    gov = "performance" if performance else "powersave"
    for g in glob.glob("/sys/devices/system/cpu/cpu*/cpufreq/scaling_governor"):
        try:
            with open(g, "w") as f:
                f.write(f"{gov}\n")
        except Exception:
            pass

    # Escalado de GPU ARM Mali-G57 MC2
    try:
        if performance:
            if os.path.exists("/proc/gpufreqv2/fix_target_opp_index"):
                with open("/proc/gpufreqv2/fix_target_opp_index", "w") as f:
                    f.write("15\n")  # 812 MHz
            for mg in glob.glob("/sys/class/devfreq/*.mali/governor"):
                with open(mg, "w") as f:
                    f.write("performance\n")
            for mf in glob.glob("/sys/class/devfreq/*.mali/min_freq"):
                with open(mf, "w") as f:
                    f.write("812000000\n")
        else:
            if os.path.exists("/proc/gpufreqv2/fix_target_opp_index"):
                with open("/proc/gpufreqv2/fix_target_opp_index", "w") as f:
                    f.write("37\n")  # 390 MHz (mínimo consumo en reposo)
            for mf in glob.glob("/sys/class/devfreq/*.mali/min_freq"):
                with open(mf, "w") as f:
                    f.write("390000000\n")
    except Exception:
        pass

def lock_screen():
    """Activa el bloqueo de pantalla de Phosh (org.gnome.ScreenSaver.Lock) de forma asíncrona e instantánea."""
    try:
        from gi.repository import Gio, GLib
        bus = get_phosh_session_bus()
        if not bus:
            return
        bus.call(
            'org.gnome.ScreenSaver',
            '/org/gnome/ScreenSaver',
            'org.gnome.ScreenSaver',
            'SetActive',
            GLib.Variant('(b)', (True,)),
            None,
            Gio.DBusCallFlags.NONE,
            500,
            None,
            None
        )
        bus.call(
            'org.gnome.ScreenSaver',
            '/org/gnome/ScreenSaver',
            'org.gnome.ScreenSaver',
            'Lock',
            None,
            None,
            Gio.DBusCallFlags.NONE,
            500,
            None,
            None
        )
        print("[+] Pantalla bloqueada en Phosh (ScreenSaver.Lock).", flush=True)
    except Exception as e:
        print(f"[-] Error en ScreenSaver.Lock: {e}", flush=True)

def ensure_desired_orientation(force=False, async_exec=False):
    global current_sensor_orientation
    target_transform = "180" if current_sensor_orientation == "bottom-up" else ("90" if current_sensor_orientation == "left-up" else "normal")
    wlr_bin = "/usr/bin/wlr-randr" if os.path.exists("/usr/bin/wlr-randr") else "chroot /sysroot /usr/bin/wlr-randr"
    try:
        if not force:
            cmd_check = f"WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/tmp/runtime-root {wlr_bin} 2>/dev/null || true"
            out = subprocess.check_output(cmd_check, shell=True, stderr=subprocess.DEVNULL).decode().strip()
            if "Enabled: yes" in out and f"Transform: {target_transform}" in out:
                return
        cmd = f"WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/tmp/runtime-root {wlr_bin} --output DSI-1 --on --transform {target_transform} 2>/dev/null || true"
        if async_exec:
            subprocess.Popen(cmd, shell=True)
        else:
            subprocess.call(cmd, shell=True)
    except Exception:
        pass

def sleep_tablet():
    global is_screen_on, saved_backlight_level, login1_dbus_conn
    print("[*] Reposo/Bloqueo activado: bloqueando Phosh, apagando pantalla y reduciendo consumo CPU/GPU...", flush=True)
    cur = get_current_backlight()
    if cur >= MIN_SAFE_BRIGHTNESS:
        saved_backlight_level = min(cur, MAX_SAFE_BRIGHTNESS)

    is_screen_on = False

    # 1. Apagar backlight físico a 0 de inmediato para respuesta visual instantánea
    set_backlight_hardware(0)

    # 2. Bloquear toques en el panel táctil mientras la pantalla está apagada
    grab_touchscreen(grab=True)

    # 3. Bloquear sesión de Phosh para que al encender aparezca el Lockscreen
    lock_screen()

    if login1_dbus_conn:
        try:
            from gi.repository import GLib
            login1_dbus_conn.emit_signal(
                None, '/org/freedesktop/login1', 'org.freedesktop.login1.Manager',
                'PrepareForSleep', GLib.Variant('(b)', (True,))
            )
        except Exception:
            pass

    # 4. Gobernadores de CPU en powersave y GPU en 390 MHz
    set_cpu_and_gpu_power(performance=False)

    print("[*] Tablet en reposo: pantalla bloqueada a 0 nits, táctil inhibido, CPU en powersave, GPU en 390 MHz.", flush=True)

def _post_wake_verify():
    for delay in (0.12, 0.25, 0.50):
        time.sleep(delay)
        if not is_screen_on:
            return
        ensure_desired_orientation(force=False, async_exec=False)

def wake_tablet():
    global is_screen_on, saved_backlight_level, login1_dbus_conn
    print("[*] Despertando tablet: reactivando aceleración GPU/CPU, salida DSI-1 en 180° y Lockscreen...", flush=True)
    is_screen_on = True

    # 1. Restaurar CPUs a performance (2.2 GHz) y GPU Mali a 812 MHz ANTES de despertar Wayland (cero lag)
    set_cpu_and_gpu_power(performance=True)

    # 2. Reactivar salida DSI-1 (--on) y forzar Transform: 180 síncronamente ANTES de liberar el touch
    ensure_desired_orientation(force=True, async_exec=False)

    # 3. Liberar bloqueo del panel táctil (ya sincronizado con DSI-1 en Transform: 180)
    grab_touchscreen(grab=False)

    # 4. Restaurar brillo LCD seguro de inmediato
    target_b = max(MIN_SAFE_BRIGHTNESS, min(MAX_SAFE_BRIGHTNESS, saved_backlight_level))
    set_backlight_hardware(target_b)

    if login1_dbus_conn:
        try:
            from gi.repository import GLib
            login1_dbus_conn.emit_signal(
                None, '/org/freedesktop/login1', 'org.freedesktop.login1.Manager',
                'PrepareForSleep', GLib.Variant('(b)', (False,))
            )
        except Exception:
            pass

    # 5. Verificación post-wake en segundo plano por si Phosh reconfigura el monitor al despertar
    threading.Thread(target=_post_wake_verify, daemon=True).start()

    print(f"[*] Tablet activa en pantalla de bloqueo: brillo {target_b}, DSI-1 180°, GPU Mali a 812 MHz.", flush=True)

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
        from gi.repository import Gio, GLib
        bus = get_phosh_session_bus()
        if not bus:
            return
        level = max(0.0, min(1.0, float(vol)))
        if is_muted or level <= 0.03:
            icon_name = 'audio-volume-muted-symbolic'
        elif level < 0.35:
            icon_name = 'audio-volume-low-symbolic'
        elif level < 0.70:
            icon_name = 'audio-volume-medium-symbolic'
        else:
            icon_name = 'audio-volume-high-symbolic'
        params = {
            'icon': GLib.Variant('s', icon_name),
            'label': GLib.Variant('s', f'Volumen ({int(level * 100)}%)'),
            'level': GLib.Variant('d', level),
            'max-level': GLib.Variant('d', 1.0)
        }
        bus.call(
            'org.gnome.Shell',
            '/org/gnome/Shell',
            'org.gnome.Shell',
            'ShowOSD',
            GLib.Variant('(a{sv})', (params,)),
            None,
            Gio.DBusCallFlags.NONE,
            500,
            None,
            None
        )
    except Exception as e:
        print(f"[-] Error mostrando OSD de volumen: {e}", flush=True)

def get_pipewire_volume():
    try:
        res = subprocess.run(
            "XDG_RUNTIME_DIR=/tmp/runtime-root wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null",
            shell=True, capture_output=True, text=True
        )
        out = res.stdout.strip()
        is_muted = "[MUTED]" in out
        parts = out.split()
        if len(parts) >= 2:
            return float(parts[1]), is_muted
    except Exception:
        pass
    return 0.5, False

def adjust_volume(up=True):
    try:
        vol, is_muted = get_pipewire_volume()
        if up:
            subprocess.run("XDG_RUNTIME_DIR=/tmp/runtime-root wpctl set-mute @DEFAULT_AUDIO_SINK@ 0 2>/dev/null", shell=True)
            if vol <= 0.05 or is_muted:
                subprocess.run("XDG_RUNTIME_DIR=/tmp/runtime-root wpctl set-volume @DEFAULT_AUDIO_SINK@ 0.10 2>/dev/null", shell=True)
            else:
                subprocess.run("XDG_RUNTIME_DIR=/tmp/runtime-root wpctl set-volume -l 1.0 @DEFAULT_AUDIO_SINK@ 5%+ 2>/dev/null", shell=True)
        else:
            if vol <= 0.06:
                subprocess.run("XDG_RUNTIME_DIR=/tmp/runtime-root wpctl set-volume @DEFAULT_AUDIO_SINK@ 0.0 2>/dev/null", shell=True)
                subprocess.run("XDG_RUNTIME_DIR=/tmp/runtime-root wpctl set-mute @DEFAULT_AUDIO_SINK@ 1 2>/dev/null", shell=True)
            else:
                subprocess.run("XDG_RUNTIME_DIR=/tmp/runtime-root wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%- 2>/dev/null", shell=True)
        vol, is_muted = get_pipewire_volume()
        print(f"[+] Volumen ajustado: {int(vol * 100)}% (muted={is_muted})", flush=True)
        show_volume_osd(vol, is_muted)
    except Exception as e:
        print(f"[-] Error ajustando volumen: {e}", flush=True)

def start_dbus_services():
    global login1_dbus_conn
    from gi.repository import Gio, GLib

    hostname_policy = '''<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-BUS Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <policy user="root">
    <allow own="org.freedesktop.hostname1"/>
    <allow send_destination="org.freedesktop.hostname1"/>
    <allow receive_sender="org.freedesktop.hostname1"/>
  </policy>
  <policy context="default">
    <allow send_destination="org.freedesktop.hostname1"/>
    <allow receive_sender="org.freedesktop.hostname1"/>
  </policy>
</busconfig>
'''
    for pdir in ["/etc/dbus-1/system.d", "/sysroot/etc/dbus-1/system.d"]:
        try:
            if os.path.isdir(pdir):
                with open(os.path.join(pdir, "org.freedesktop.hostname1.conf"), "w") as f:
                    f.write(hostname_policy)
        except Exception:
            pass
    subprocess.call("killall -HUP dbus-daemon 2>/dev/null || true", shell=True)

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
        <method name="Wake"/>
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
            val_int = max(MIN_SAFE_BRIGHTNESS, min(MAX_SAFE_BRIGHTNESS, int(val)))
            saved_backlight_level = val_int
            if is_screen_on:
                set_backlight_hardware(val_int)
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
        elif method == "Wake":
            print("[*] DBus login1.Manager.Wake llamado!", flush=True)
            wake_tablet()
            invocation.return_value(None)
        elif method == "PowerOff":
            print("[*] DBus login1.Manager.PowerOff llamado!", flush=True)
            subprocess.call("sync; poweroff", shell=True)
            invocation.return_value(None)
        elif method == "Reboot":
            print("[*] DBus login1.Manager.Reboot llamado!", flush=True)
            subprocess.call("sync; reboot", shell=True)
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
        ensure_desired_orientation(force=True)

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

    HOSTNAME_XML = '''
    <node>
      <interface name="org.freedesktop.hostname1">
        <property name="Hostname" type="s" access="read"/>
        <property name="StaticHostname" type="s" access="read"/>
        <property name="PrettyHostname" type="s" access="read"/>
        <property name="IconName" type="s" access="read"/>
        <property name="Chassis" type="s" access="read"/>
        <property name="OperatingSystemPrettyName" type="s" access="read"/>
      </interface>
    </node>
    '''
    hostname_node_info = Gio.DBusNodeInfo.new_for_xml(HOSTNAME_XML)

    def handle_hostname_get_prop(conn, sender, path, iface, prop):
        if prop in ('Hostname', 'StaticHostname'):
            return GLib.Variant('s', 'umidigi-g7-tab-pro')
        elif prop == 'PrettyHostname':
            return GLib.Variant('s', 'UMIDIGI G7 Tab Pro')
        elif prop == 'IconName':
            return GLib.Variant('s', 'computer-tablet')
        elif prop == 'Chassis':
            return GLib.Variant('s', 'tablet')
        elif prop == 'OperatingSystemPrettyName':
            return GLib.Variant('s', 'postmarketOS v24.12 (GKI 5.10)')
        return None

    def on_hostname_bus_acquired(conn, name):
        conn.register_object(
            '/org/freedesktop/hostname1',
            hostname_node_info.interfaces[0],
            None,
            handle_hostname_get_prop,
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

    try:
        Gio.bus_own_name(Gio.BusType.SYSTEM, 'org.freedesktop.hostname1', Gio.BusNameOwnerFlags.NONE, on_hostname_bus_acquired, None, None)
    except Exception as e:
        print(f"[-] Error registrando org.freedesktop.hostname1: {e}", flush=True)

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
    registered_session_addr = [None]

    def handle_session_method(conn, sender, path, iface, method, params, invocation):
        if method == "Shutdown":
            print("[*] SessionManager.Shutdown llamado!", flush=True)
            subprocess.call("sync; poweroff", shell=True)
            invocation.return_value(None)
        elif method == "Reboot":
            print("[*] SessionManager.Reboot llamado!", flush=True)
            subprocess.call("sync; reboot", shell=True)
            invocation.return_value(None)
        elif method == "CanShutdown":
            invocation.return_value(GLib.Variant('(b)', (True,)))
        elif method == "IsSessionRunning":
            invocation.return_value(GLib.Variant('(b)', (True,)))
        else:
            invocation.return_value(None)

    def on_screensaver_signal(conn, sender, path, iface, signal, params):
        if signal == "ActiveChanged":
            if is_screen_on:
                ensure_desired_orientation(force=False)
                threading.Thread(target=_post_wake_verify, daemon=True).start()

    def ensure_session_bus_registered():
        addr = get_session_dbus_address()
        if not addr or addr == registered_session_addr[0]:
            return True
        sbus = get_phosh_session_bus()
        if not sbus:
            return True
        try:
            sbus.register_object(
                '/org/gnome/SessionManager',
                session_node_info.interfaces[0],
                handle_session_method, None, None
            )
            Gio.bus_own_name_on_connection(sbus, 'org.gnome.SessionManager', Gio.BusNameOwnerFlags.NONE, None, None)
            sbus.signal_subscribe(
                'org.gnome.ScreenSaver', 'org.gnome.ScreenSaver', 'ActiveChanged',
                '/org/gnome/ScreenSaver', None, Gio.DBusSignalFlags.NONE,
                on_screensaver_signal
            )
            registered_session_addr[0] = addr
            print(f"[+] Conectado al bus de sesión de Phosh ({addr})", flush=True)
        except Exception as e:
            print(f"[-] Aviso registrando en sesión Phosh: {e}", flush=True)
        return True

    GLib.timeout_add_seconds(2, ensure_session_bus_registered)

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
    print("[+] tablet_power_daemon v3: Iniciando controlador de botón Power, Volumen, Brillo y Suspensión...", flush=True)
    release_static_backlight_locks()
    set_backlight_hardware(saved_backlight_level)

    try:
        subprocess.call("HOME=/root dconf write /sm/puri/phosh/enable-suspend true 2>/dev/null", shell=True)
    except Exception:
        pass

    t = threading.Thread(target=start_dbus_services, daemon=True)
    t.start()

    devs = get_grabbed_devices()

    last_check_hw = time.time()

    while True:
        now = time.time()
        if now - last_check_hw > 2.0:
            last_check_hw = now
            if is_screen_on:
                cur_b = get_current_backlight()
                if cur_b < MIN_SAFE_BRIGHTNESS or cur_b > MAX_SAFE_BRIGHTNESS:
                    set_backlight_hardware(saved_backlight_level)
                ensure_desired_orientation(force=False)

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
                raw = os.read(fd, EVENT_SIZE * 16)
                if len(raw) < EVENT_SIZE:
                    continue
                for offset in range(0, len(raw) - (len(raw) % EVENT_SIZE), EVENT_SIZE):
                    sec, usec, etype, code, val = struct.unpack(EVENT_FORMAT, raw[offset:offset + EVENT_SIZE])
                    if etype == EV_KEY and val in (1, 2):
                        if code == KEY_POWER and val == 1:
                            toggle_screen_power()
                        elif code == KEY_VOLUMEUP:
                            adjust_volume(up=True)
                        elif code == KEY_VOLUMEDOWN:
                            adjust_volume(up=False)

            except BlockingIOError:
                pass
            except Exception:
                try:
                    os.close(fd)
                except Exception:
                    pass
                devs.pop(fd, None)

if __name__ == "__main__":
    main()
