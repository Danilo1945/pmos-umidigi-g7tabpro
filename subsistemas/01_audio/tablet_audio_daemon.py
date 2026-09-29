#!/usr/bin/env python3
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

# Botones físicos en UMIDIGI G7 Tab Pro:
# 115 = KEY_VOLUMEUP   (Tecla física superior para SUBIR volumen)
# 114 = KEY_VOLUMEDOWN (Tecla física inferior para BAJAR volumen)
# 116 = KEY_POWER      (Tecla física de encendido / bloqueo de pantalla)
KEY_VOLUMEUP = 115
KEY_VOLUMEDOWN = 114
KEY_POWER = 116

# IOCTL para leer estado de switches en Linux: EVIOCGSW(8)
EVIOCGSW_8 = 0x8008451b

headphones_plugged_state = False
is_hardware_muted = False
last_known_volume = 0.50

is_screen_on = True
saved_backlight_level = 70
last_power_press_time = 0.0

# Orientacion del sensor: "bottom-up" = 180 grados (al derecho para la tablet del usuario)
current_sensor_orientation = "bottom-up"
sensor_dbus_conn = None

def set_sensor_orientation(new_orientation):
    global current_sensor_orientation, sensor_dbus_conn
    current_sensor_orientation = new_orientation
    target_transform = "180" if new_orientation == "bottom-up" else ("90" if new_orientation == "left-up" else ("270" if new_orientation == "right-up" else "normal"))
    print(f"[*] Cambiando orientacion del sensor a: {new_orientation} (transform={target_transform})", flush=True)
    if sensor_dbus_conn:
        try:
            from gi.repository import GLib
            changed_props = GLib.Variant('a{sv}', {
                'AccelerometerOrientation': GLib.Variant('s', new_orientation)
            })
            invalidated_props = GLib.Variant('as', [])
            sensor_dbus_conn.emit_signal(
                None,
                '/net/hadess/SensorProxy',
                'org.freedesktop.DBus.Properties',
                'PropertiesChanged',
                GLib.Variant('(sa{sv}as)', ('net.hadess.SensorProxy', changed_props, invalidated_props))
            )
        except Exception as e:
            print(f"[-] Error emitiendo cambio de orientacion por DBus: {e}", flush=True)

    try:
        cmd = f"WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/tmp/runtime-danilo /usr/bin/wlr-randr --output DSI-1 --on --transform {target_transform} 2>/dev/null || WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/tmp/runtime-danilo chroot /sysroot /usr/bin/wlr-randr --output DSI-1 --on --transform {target_transform} 2>/dev/null || true"
        subprocess.call(cmd, shell=True)
    except Exception:
        pass

def run_cmd(cmd):
    try:
        subprocess.run(cmd, shell=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except Exception:
        pass

def run_mixer(arg):
    run_cmd(f"amixer -c 0 {arg} 2>/dev/null || chroot /sysroot amixer -c 0 {arg} 2>/dev/null")

def ensure_input_nodes():
    os.makedirs('/dev/input', exist_ok=True)
    for e in glob.glob('/sys/class/input/event*'):
        ename = os.path.basename(e)
        dev_file = f"{e}/dev"
        if os.path.exists(dev_file):
            try:
                with open(dev_file) as f:
                    maj, min_ = [int(x) for x in f.read().strip().split(':')]
                nodepath = f"/dev/input/{ename}"
                if not os.path.exists(nodepath):
                    os.mknod(nodepath, 0o666 | 0o020000, os.makedev(maj, min_))
                    os.chmod(nodepath, 0o666)
            except Exception:
                pass

EVIOCGRAB = 0x40044590

def get_input_devices():
    ensure_input_nodes()
    devs = {}
    for ev in sorted(glob.glob('/dev/input/event*')):
        try:
            ev_name = os.path.basename(ev)
            name_path = f"/sys/class/input/{ev_name}/device/name"
            dname = ""
            if os.path.exists(name_path):
                with open(name_path, "r") as f:
                    dname = f.read().strip()
            fd = os.open(ev, os.O_RDONLY | os.O_NONBLOCK)
            if "mtk-pmic-keys" in dname or "mtk-kpd" in dname:
                try:
                    fcntl.ioctl(fd, EVIOCGRAB, 1)
                    print(f"[*] Captura exclusiva EVIOCGRAB aplicada a {dname} ({ev})", flush=True)
                except Exception as e:
                    print(f"[-] No se pudo aplicar EVIOCGRAB a {dname}: {e}", flush=True)
            devs[fd] = (ev, dname)
        except Exception:
            pass
    return devs

SW_HEADPHONE_INSERT = 2

def check_jack_state(fd):
    """Lee el estado real del switch de hardware del jack mediante ioctl EVIOCGSW."""
    if fd is None:
        return False
    try:
        buf = array.array('B', [0] * 8)
        fcntl.ioctl(fd, EVIOCGSW_8, buf)
        # Bit 2 (SW_HEADPHONE_INSERT = 2) del byte 0
        return bool(buf[0] & (1 << SW_HEADPHONE_INSERT))
    except Exception:
        return False

def configure_gain_levels():
    """Configura ganancia calibrada de hardware a fidelidad Hi-Fi óptima sin saturación ni siseo."""
    run_mixer("cset numid=27 32768")    # ADDA_DL_GAIN = 32768 (nivel nominal perfecto sin sobrecarga AGC)
    run_mixer("cset numid=299 12")      # Headset Volume (12/18 = ganancia analógica limpia sin ruido blanco)
    run_mixer("cset numid=300 12")      # Lineout Volume (12/18 = ganancia analógica limpia)
    run_mixer("cset numid=324 0")       # LOL Mux -> Open (evita diafonía)
    run_mixer("cset numid=179 0")       # I2S3_CH1 OFF (evita inyección de ruido digital en el bus)
    run_mixer("cset numid=192 0")       # I2S3_CH2 OFF

def sync_hardware_state(force=False):
    global is_hardware_muted, headphones_plugged_state
    vol, is_muted = get_current_volume()
    if vol > 1.0:
        run_cmd("su danilo -c 'XDG_RUNTIME_DIR=/tmp/runtime-danilo wpctl set-volume @DEFAULT_AUDIO_SINK@ 1.0' 2>/dev/null || chroot /sysroot su danilo -c 'XDG_RUNTIME_DIR=/tmp/runtime-danilo wpctl set-volume @DEFAULT_AUDIO_SINK@ 1.0' 2>/dev/null")
        vol = 1.0
    should_mute = (vol <= 0.01 or is_muted)

    if not force and is_hardware_muted == should_mute:
        return

    is_hardware_muted = should_mute

    if should_mute:
        # Silencio físico total
        run_mixer("cset numid=319 0")       # Ext_Speaker_Amp Switch OFF
        for numid in [282, 279, 276, 273]: # AW87359 4x Smart PA OFF (modo 2)
            run_mixer(f"cset numid={numid} 2")
        run_mixer("cset numid=325 0")       # HPL Mux Open
        run_mixer("cset numid=326 0")       # HPR Mux Open
        run_mixer("cset numid=324 0")       # LOL Mux Open
        run_mixer("cset numid=211 0")       # ADDA_DL_CH1 OFF
        run_mixer("cset numid=226 0")       # ADDA_DL_CH2 OFF
        run_mixer("cset numid=179 0")       # I2S3_CH1 OFF
        run_mixer("cset numid=192 0")       # I2S3_CH2 OFF
        print("[*] SILENCIO FISICO TOTAL APLICADO", flush=True)
    else:
        # Reactivar audio limpio en DAC analógico MT6366
        run_mixer("cset numid=211 1")       # ADDA_DL_CH1 ON
        run_mixer("cset numid=226 1")       # ADDA_DL_CH2 ON
        run_mixer("cset numid=179 0")       # I2S3_CH1 DL1_CH1 OFF
        run_mixer("cset numid=192 0")       # I2S3_CH2 DL1_CH2 OFF
        configure_gain_levels()
        if headphones_plugged_state:
            # Auriculares conectados por jack 3.5mm
            run_mixer("cset numid=325 2")   # HPL Mux -> Audio Playback
            run_mixer("cset numid=326 2")   # HPR Mux -> Audio Playback
            run_mixer("cset numid=324 0")   # LOL Mux Open
            run_mixer("cset numid=287 1")   # Headphone Plugged In = On
            run_mixer("cset numid=319 0")   # Ext_Speaker_Amp Switch = off
            for numid in [282, 279, 276, 273]:
                run_mixer(f"cset numid={numid} 2") # Apagar altavoces exteriores
            print("[*] AUDIO ACTIVO EN AURICULARES (Jack 3.5mm)", flush=True)
        else:
            # Altavoces exteriores (4 parlantes con Smart PA AW87359 alimentados por DAC analógico HPL/HPR)
            run_mixer("cset numid=325 2")   # HPL Mux -> Audio Playback
            run_mixer("cset numid=326 2")   # HPR Mux -> Audio Playback
            run_mixer("cset numid=324 0")   # LOL Mux Open
            run_mixer("cset numid=287 0")   # Headphone Plugged In = Off
            run_mixer("cset numid=319 1")   # Ext_Speaker_Amp Switch = on
            for numid in [282, 279, 276, 273]:
                run_mixer(f"cset numid={numid} 0") # Perfil 0: Music
            print("[*] AUDIO ACTIVO EN 4 ALTAVOCES EXTERIORES (AW87359 Music)", flush=True)

def get_current_volume():
    global last_known_volume
    try:
        cmd = "su danilo -c 'XDG_RUNTIME_DIR=/tmp/runtime-danilo wpctl get-volume @DEFAULT_AUDIO_SINK@' 2>/dev/null || chroot /sysroot su danilo -c 'XDG_RUNTIME_DIR=/tmp/runtime-danilo wpctl get-volume @DEFAULT_AUDIO_SINK@' 2>/dev/null"
        res = subprocess.run(cmd, shell=True, capture_output=True, text=True)
        out = res.stdout.strip()
        is_muted = "[MUTED]" in out
        parts = out.split()
        if len(parts) >= 2:
            vol = float(parts[1])
            last_known_volume = vol
            return vol, is_muted
    except Exception:
        pass
    return last_known_volume, False

def ensure_dbus_env():
    if "DBUS_SESSION_BUS_ADDRESS" not in os.environ:
        for p in glob.glob('/proc/[0-9]*/environ'):
            try:
                with open(p, 'rb') as f:
                    env = f.read().split(b'\0')
                for item in env:
                    if item.startswith(b'DBUS_SESSION_BUS_ADDRESS='):
                        os.environ['DBUS_SESSION_BUS_ADDRESS'] = item.split(b'=', 1)[1].decode('utf-8')
                        break
                if "DBUS_SESSION_BUS_ADDRESS" in os.environ:
                    break
            except Exception:
                pass

def get_current_backlight():
    try:
        with open("/sys/class/leds/lcd-backlight/brightness", "r") as f:
            return int(f.read().strip())
    except Exception:
        return 70

def set_backlight_hardware(level):
    try:
        val = int(level)
        if val <= 0:
            safe_level = 0
        elif val < 10:
            safe_level = 10
        elif val > 75:
            safe_level = 75
        else:
            safe_level = val
        with open("/sys/class/leds/lcd-backlight/brightness", "w") as f:
            f.write(f"{safe_level}\n")
    except Exception:
        pass

def ensure_desired_orientation(force=False):
    global current_sensor_orientation
    try:
        target_transform = "180" if current_sensor_orientation == "bottom-up" else ("90" if current_sensor_orientation == "left-up" else "270")
        cmd_check = "WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/tmp/runtime-danilo /usr/bin/wlr-randr 2>/dev/null || WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/tmp/runtime-danilo chroot /sysroot /usr/bin/wlr-randr 2>/dev/null || true"
        if not force:
            out = subprocess.check_output(cmd_check, shell=True, stderr=subprocess.DEVNULL).decode().strip()
            # Si esta en Transform: normal (patas arriba), corregirla de inmediato
            if "Transform: normal" not in out:
                return
        print(f"[*] Sincronizando orientacion correcta de pantalla ({target_transform})...", flush=True)
        cmd_set = f"WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/tmp/runtime-danilo /usr/bin/wlr-randr --output DSI-1 --on --transform {target_transform} 2>/dev/null || WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/tmp/runtime-danilo chroot /sysroot /usr/bin/wlr-randr --output DSI-1 --on --transform {target_transform} 2>/dev/null || true"
        subprocess.call(cmd_set, shell=True)
    except Exception:
        pass

def start_dbus_services():
    ensure_dbus_env()
    from gi.repository import Gio, GLib

    # 1. Escuchar senal de desbloqueo de ScreenSaver en session bus
    def on_screensaver_signal(conn, sender, path, iface, signal, params):
        if signal == "ActiveChanged":
            is_active = params.unpack()[0]
            if not is_active:
                print("[*] Desbloqueo detectado por DBus -> Sincronizando orientacion...", flush=True)
                ensure_desired_orientation(force=True)

    try:
        session_bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
        session_bus.signal_subscribe(
            'org.gnome.ScreenSaver', 'org.gnome.ScreenSaver', 'ActiveChanged',
            '/org/gnome/ScreenSaver', None, Gio.DBusSignalFlags.NONE,
            on_screensaver_signal
        )
    except Exception as e:
        print(f"[-] Error suscribiendo a senales de ScreenSaver: {e}", flush=True)

    # 2. Servicio login1 en system bus para el slider de brillo de Phosh
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
      </interface>
    </node>
    '''
    node_info = Gio.DBusNodeInfo.new_for_xml(LOGIN1_XML)

    def handle_login1_method(conn, sender, path, iface, method, params, invocation):
        global saved_backlight_level
        if method == "SetBrightness":
            sub, dev, val = params.unpack()
            val_int = int(val)
            print(f"[*] DBus login1 SetBrightness: {sub}/{dev} -> {val_int}", flush=True)
            set_backlight_hardware(val_int)
            if val_int > 5:
                saved_backlight_level = val_int
            invocation.return_value(None)
        elif method == "GetSession":
            invocation.return_value(GLib.Variant('(o)', ('/org/freedesktop/login1/session/auto',)))
        else:
            invocation.return_value(None)

    def on_system_bus_acquired(conn, name):
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
            handle_login1_method, None, None
        )

    # 3. Servicio net.hadess.SensorProxy en system bus para Quick Settings y Auto-rotacion
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

    def handle_sensor_method(conn, sender, path, iface, method, params, invocation):
        print(f"[*] SensorProxy method called: {method} by {sender}", flush=True)
        if method == "SetOrientation":
            orient = params.unpack()[0]
            set_sensor_orientation(orient)
        invocation.return_value(None)

    def handle_sensor_get_prop(conn, sender, path, iface, prop):
        global current_sensor_orientation
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
        Gio.bus_own_name(
            Gio.BusType.SYSTEM,
            'org.freedesktop.login1',
            Gio.BusNameOwnerFlags.NONE,
            on_system_bus_acquired,
            None, None
        )
    except Exception as e:
        print(f"[-] Error registrando servicio org.freedesktop.login1: {e}", flush=True)

    try:
        Gio.bus_own_name(
            Gio.BusType.SYSTEM,
            'net.hadess.SensorProxy',
            Gio.BusNameOwnerFlags.NONE,
            on_sensor_bus_acquired,
            None, None
        )
    except Exception as e:
        print(f"[-] Error registrando servicio net.hadess.SensorProxy: {e}", flush=True)

    # 4. Escuchar cambios en el toggle de rotacion del panel de control (Quick Settings)
    try:
        touch_settings = Gio.Settings.new('org.gnome.settings-daemon.peripherals.touchscreen')
        def on_orientation_lock_changed(s, key):
            locked = s.get_boolean('orientation-lock')
            print(f"[*] Quick Setting rotacion cambio: locked={locked}", flush=True)
            if not locked:
                # El usuario toco el toggle para desbloquear y rotar -> conmuta entre Portrait (180) y Landscape (90)
                next_orient = "left-up" if current_sensor_orientation == "bottom-up" else "bottom-up"
                set_sensor_orientation(next_orient)
        touch_settings.connect('changed::orientation-lock', on_orientation_lock_changed)
    except Exception as e:
        print(f"[-] Error conectando a settings de orientation-lock: {e}", flush=True)

    loop = GLib.MainLoop()
    loop.run()

def lock_screen():
    try:
        ensure_dbus_env()
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
        ensure_dbus_env()
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

    ensure_desired_orientation(force=True)

def toggle_screen_power():
    global is_screen_on, saved_backlight_level, last_power_press_time
    now = time.time()
    if now - last_power_press_time < 0.35:  # Debounce 350ms
        return
    last_power_press_time = now

    if is_screen_on:
        print("[*] Boton fisico POWER: APAGANDO PANTALLA...", flush=True)
        cur = get_current_backlight()
        if cur > 10:
            saved_backlight_level = cur

        # 1. Bloquear Phosh
        lock_screen()

        # 2. Apagar físicamente el backlight a 0 (cero emisión de luz)
        set_backlight_hardware(0)
        is_screen_on = False
        print("[*] Pantalla apagada exitosamente.", flush=True)
    else:
        print("[*] Boton fisico POWER: ENCENDIENDO PANTALLA...", flush=True)
        # 1. Despertar la sesión en Phosh (unblank y reactivar señal DRM)
        wake_screen()

        # 2. Restaurar nivel de brillo previo
        set_backlight_hardware(saved_backlight_level)
        is_screen_on = True
        print(f"[*] Pantalla encendida exitosamente (Brillo: {saved_backlight_level}).", flush=True)

def show_volume_osd(vol, is_muted=False, label="Volumen", custom_icon=None):
    try:
        ensure_dbus_env()
        from gi.repository import Gio, GLib
        bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
        proxy = Gio.DBusProxy.new_sync(
            bus,
            Gio.DBusProxyFlags.NONE,
            None,
            'org.gnome.Shell',
            '/org/gnome/Shell',
            'org.gnome.Shell',
            None
        )

        level = max(0.0, min(1.0, float(vol)))
        if custom_icon:
            icon_name = custom_icon
        elif is_muted or level <= 0.03:
            icon_name = 'audio-volume-muted-symbolic'
        elif level < 0.35:
            icon_name = 'audio-volume-low-symbolic'
        elif level < 0.70:
            icon_name = 'audio-volume-medium-symbolic'
        else:
            icon_name = 'audio-volume-high-symbolic'

        params = {
            'icon': GLib.Variant('s', icon_name),
            'label': GLib.Variant('s', label),
            'level': GLib.Variant('d', level),
            'max-level': GLib.Variant('d', 1.0)
        }
        proxy.ShowOSD('(a{sv})', params)
    except Exception:
        pass

def adjust_volume(up=True):
    vol, is_muted = get_current_volume()
    if up:
        print("[*] Boton fisico: SUBIR volumen", flush=True)
        # 1. Desmutear siempre en PipeWire
        run_cmd("su danilo -c 'XDG_RUNTIME_DIR=/tmp/runtime-danilo wpctl set-mute @DEFAULT_AUDIO_SINK@ 0' 2>/dev/null || chroot /sysroot su danilo -c 'XDG_RUNTIME_DIR=/tmp/runtime-danilo wpctl set-mute @DEFAULT_AUDIO_SINK@ 0' 2>/dev/null")
        # 2. Si venía de silencio total, poner a 15% para que sea audible al instante
        if vol <= 0.05 or is_muted:
            run_cmd("su danilo -c 'XDG_RUNTIME_DIR=/tmp/runtime-danilo wpctl set-volume @DEFAULT_AUDIO_SINK@ 0.15' 2>/dev/null || chroot /sysroot su danilo -c 'XDG_RUNTIME_DIR=/tmp/runtime-danilo wpctl set-volume @DEFAULT_AUDIO_SINK@ 0.15' 2>/dev/null")
        else:
            run_cmd("su danilo -c 'XDG_RUNTIME_DIR=/tmp/runtime-danilo wpctl set-volume -l 1.0 @DEFAULT_AUDIO_SINK@ 5%+' 2>/dev/null || chroot /sysroot su danilo -c 'XDG_RUNTIME_DIR=/tmp/runtime-danilo wpctl set-volume -l 1.0 @DEFAULT_AUDIO_SINK@ 5%+' 2>/dev/null")
        vol, is_muted = get_current_volume()
        pct = int(round(vol * 100))
        print(f"[*] Volumen actual: {pct}%", flush=True)
        sync_hardware_state(force=True)
        show_volume_osd(vol, is_muted)
    else:
        print("[*] Boton fisico: BAJAR volumen", flush=True)
        if vol <= 0.08:
            print("[*] Umbral minimo alcanzado -> Aplicando SILENCIO ABSOLUTO (0%)", flush=True)
            run_cmd("su danilo -c 'XDG_RUNTIME_DIR=/tmp/runtime-danilo wpctl set-volume @DEFAULT_AUDIO_SINK@ 0.0' 2>/dev/null || chroot /sysroot su danilo -c 'XDG_RUNTIME_DIR=/tmp/runtime-danilo wpctl set-volume @DEFAULT_AUDIO_SINK@ 0.0' 2>/dev/null")
            run_cmd("su danilo -c 'XDG_RUNTIME_DIR=/tmp/runtime-danilo wpctl set-mute @DEFAULT_AUDIO_SINK@ 1' 2>/dev/null || chroot /sysroot su danilo -c 'XDG_RUNTIME_DIR=/tmp/runtime-danilo wpctl set-mute @DEFAULT_AUDIO_SINK@ 1' 2>/dev/null")
        else:
            run_cmd("su danilo -c 'XDG_RUNTIME_DIR=/tmp/runtime-danilo wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%-' 2>/dev/null || chroot /sysroot su danilo -c 'XDG_RUNTIME_DIR=/tmp/runtime-danilo wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%-' 2>/dev/null")
        vol, is_muted = get_current_volume()
        pct = int(round(vol * 100))
        print(f"[*] Volumen actual: {pct}%", flush=True)
        sync_hardware_state(force=True)
        show_volume_osd(vol, is_muted)

def main():
    global headphones_plugged_state
    print("[+] tablet_audio_daemon v10 (Audio, Power/Lockscreen y Rotacion 180 continua) iniciado...", flush=True)

    # Iniciar servicios DBus (login1 SetBrightness y listener de desbloqueo) en background
    dbus_thread = threading.Thread(target=start_dbus_services, daemon=True)
    dbus_thread.start()

    devs = get_input_devices()
    print(f"[*] Dispositivos de entrada: {[(ev, name) for ev, name in devs.values()]}", flush=True)

    jack_fd = None
    for fd, (path, name) in devs.items():
        if "Headset Jack" in name or "jack" in name.lower():
            jack_fd = fd
            break

    if jack_fd is not None:
        headphones_plugged_state = check_jack_state(jack_fd)
        print(f"[*] Estado fisico inicial del Jack 3.5mm: {'Conectado' if headphones_plugged_state else 'Desconectado'}", flush=True)

    sync_hardware_state(force=True)
    ensure_desired_orientation(force=True)
    last_orient_check = 0.0

    while True:
        now = time.time()
        if now - last_orient_check > 2.0:
            last_orient_check = now
            ensure_desired_orientation()

        if not devs:
            time.sleep(1)
            devs = get_input_devices()
            continue

        try:
            r, _, _ = select.select(list(devs.keys()), [], [], 0.5)
        except Exception:
            devs = get_input_devices()
            continue

        # Sincronización continua con el control de volumen del panel superior de Phosh
        sync_hardware_state()

        for fd in r:
            try:
                data = os.read(fd, EVENT_SIZE)
                if len(data) < EVENT_SIZE:
                    continue
                sec, usec, etype, code, val = struct.unpack(EVENT_FORMAT, data)

                # Teclas físicas (Pulsado=1, Mantenido=2)
                if etype == EV_KEY and val in (1, 2):
                    if code == KEY_VOLUMEUP:
                        adjust_volume(up=True)
                    elif code == KEY_VOLUMEDOWN:
                        adjust_volume(up=False)
                    elif code == KEY_POWER and val == 1:
                        toggle_screen_power()

                # Evento de switch (Jack 3.5mm: SW_HEADPHONE_INSERT = 2)
                elif etype == EV_SW and (code == 2 or fd == jack_fd):
                    headphones_plugged_state = bool(val == 1) if code == 2 else check_jack_state(jack_fd)
                    print(f"[*] Cambio de switch Jack 3.5mm: {'Conectado' if headphones_plugged_state else 'Desconectado'}", flush=True)
                    sync_hardware_state(force=True)
                    vol, is_muted = get_current_volume()
                    if headphones_plugged_state:
                        show_volume_osd(vol, is_muted, label="Auriculares", custom_icon="audio-headphones-symbolic")
                    else:
                        show_volume_osd(vol, is_muted, label="Altavoces", custom_icon="audio-volume-high-symbolic")

            except Exception:
                try:
                    os.close(fd)
                except Exception:
                    pass
                devs.pop(fd, None)

if __name__ == "__main__":
    main()
