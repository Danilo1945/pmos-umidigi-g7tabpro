#!/usr/bin/env python3
# ==============================================================================
# DAEMON DE AUDIO INTELIGENTE ALSA + 4x AW87359 + PIPEWIRE + JACK 3.5mm
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
import signal

signal.signal(signal.SIGHUP, signal.SIG_IGN)

EVENT_FORMAT = 'llHHI'
EVENT_SIZE = struct.calcsize(EVENT_FORMAT)
EV_SW = 5
SW_HEADPHONE_INSERT = 2

# IOCTL para leer estado de switches en Linux: EVIOCGSW(8)
EVIOCGSW_8 = 0x8008451b

headphones_plugged_state = False
is_hardware_muted = False
last_known_volume = 0.50

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

def get_jack_devices():
    """Abre únicamente dispositivos de switch de audio (Headset Jack) sin bloquear teclas."""
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
            if "jack" in dname.lower() or "headset" in dname.lower() or "accdet" in dname.lower():
                fd = os.open(ev, os.O_RDONLY | os.O_NONBLOCK)
                devs[fd] = (ev, dname)
        except Exception:
            pass
    return devs

def check_jack_state(fd):
    """Lee el estado real del switch de hardware del jack mediante ioctl EVIOCGSW."""
    if fd is None:
        return False
    try:
        buf = array.array('B', [0] * 8)
        fcntl.ioctl(fd, EVIOCGSW_8, buf)
        return bool(buf[0] & (1 << SW_HEADPHONE_INSERT))
    except Exception:
        return False

def configure_gain_levels():
    """Configura ganancia calibrada de hardware a fidelidad Hi-Fi óptima sin saturación ni siseo, y activa captura de micrófono."""
    run_mixer("cset numid=27 32768")    # ADDA_DL_GAIN = 32768 (nivel nominal perfecto sin sobrecarga AGC)
    run_mixer("cset numid=299 12")      # Headset Volume (12/18 = ganancia analógica limpia sin ruido blanco)
    run_mixer("cset numid=300 12")      # Lineout Volume (12/18 = ganancia analógica limpia)
    run_mixer("cset numid=324 0")       # LOL Mux -> Open (evita diafonía)
    run_mixer("cset numid=179 0")       # I2S3_CH1 OFF (evita inyección de ruido digital en el bus)
    run_mixer("cset numid=192 0")       # I2S3_CH2 OFF
    # Micrófono Integrado (ADDA Uplink -> UL1 / Capture_1 plughw:0,9)
    run_mixer("cset numid=29 1")        # UL1_CH1 ADDA_UL_CH1 = on
    run_mixer("cset numid=32 1")        # UL1_CH2 ADDA_UL_CH2 = on
    run_mixer("cset numid=243 0")       # ADDA_UL_Mux = MTKAIF
    run_mixer("cset numid=329 1")       # Mic Type Mux = ACC
    run_mixer("cset numid=330 2")       # ADC L Mux = Left Preamplifier
    run_mixer("cset numid=331 2")       # ADC R Mux = Right Preamplifier
    run_mixer("cset numid=332 1")       # PGA L Mux = AIN0
    run_mixer("cset numid=333 1")       # PGA R Mux = AIN0
    run_mixer("cset numid=302 3")       # PGA1 Volume = +18dB
    run_mixer("cset numid=303 3")       # PGA2 Volume = +18dB

def get_current_volume():
    global last_known_volume
    try:
        cmd = (
            "XDG_RUNTIME_DIR=/tmp/runtime-root wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null || "
            "chroot /sysroot env XDG_RUNTIME_DIR=/tmp/runtime-root wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null || "
            "su danilo -c 'XDG_RUNTIME_DIR=/tmp/runtime-danilo wpctl get-volume @DEFAULT_AUDIO_SINK@' 2>/dev/null"
        )
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

def sync_hardware_state(force=False):
    global is_hardware_muted, headphones_plugged_state
    vol, is_muted = get_current_volume()
    if vol > 1.0:
        run_cmd(
            "XDG_RUNTIME_DIR=/tmp/runtime-root wpctl set-volume @DEFAULT_AUDIO_SINK@ 1.0 2>/dev/null || "
            "chroot /sysroot env XDG_RUNTIME_DIR=/tmp/runtime-root wpctl set-volume @DEFAULT_AUDIO_SINK@ 1.0 2>/dev/null"
        )
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

def show_volume_osd(vol, is_muted=False, label="Volumen", custom_icon=None):
    try:
        ensure_dbus_env()
        from gi.repository import Gio, GLib
        bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
        proxy = Gio.DBusProxy.new_sync(
            bus, Gio.DBusProxyFlags.NONE, None,
            'org.gnome.Shell', '/org/gnome/Shell', 'org.gnome.Shell', None
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

def ensure_snd_and_pipewire():
    try:
        if os.path.exists("/sys/class/sound/pcmC0D0p") and not os.path.exists("/dev/snd/pcmC0D0p"):
            os.makedirs("/dev/snd", exist_ok=True)
            for d in glob.glob("/sys/class/sound/*"):
                sname = os.path.basename(d)
                dev_file = os.path.join(d, "dev")
                dst_node = os.path.join("/dev/snd", sname)
                if os.path.isfile(dev_file) and not os.path.exists(dst_node):
                    with open(dev_file, "r") as f:
                        sdev = f.read().strip()
                    if ":" in sdev:
                        smaj, smin = sdev.split(":", 1)
                        subprocess.run(f"mknod {dst_node} c {smaj} {smin}", shell=True, stderr=subprocess.DEVNULL)
            subprocess.run("chmod 755 /dev/snd && chmod 666 /dev/snd/*", shell=True, stderr=subprocess.DEVNULL)

        if os.path.exists("/dev/snd/pcmC0D0p"):
            res = subprocess.run("pgrep -x pipewire", shell=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            if res.returncode != 0:
                print("[*] /dev/snd/pcmC0D0p listo; levantando PipeWire + WirePlumber...", flush=True)
                subprocess.run(
                    "rm -f /etc/pipewire/pipewire.conf.d/00-disable-portal.conf /etc/pipewire/pipewire.conf.d/10-null.conf "
                    "/tmp/runtime-root/pipewire-0*.lock /tmp/runtime-root/pulse/native* 2>/dev/null || true; "
                    "XDG_RUNTIME_DIR=/tmp/runtime-root DBUS_SYSTEM_BUS_ADDRESS=unix:path=/run/dbus/system_bus_socket /usr/bin/pipewire >/var/log/pipewire.log 2>&1 & "
                    "sleep 0.5; "
                    "XDG_RUNTIME_DIR=/tmp/runtime-root DBUS_SYSTEM_BUS_ADDRESS=unix:path=/run/dbus/system_bus_socket /usr/bin/wireplumber >/var/log/wireplumber.log 2>&1 & "
                    "if ! pgrep -x pipewire-pulse >/dev/null 2>&1; then "
                    "XDG_RUNTIME_DIR=/tmp/runtime-root DBUS_SYSTEM_BUS_ADDRESS=unix:path=/run/dbus/system_bus_socket /usr/bin/pipewire-pulse >/var/log/pipewire-pulse.log 2>&1 & "
                    "fi",
                    shell=True
                )
    except Exception:
        pass

def main():
    global headphones_plugged_state
    print("[+] tablet_audio_daemon v11 (ALSA 4x AW87359 + PipeWire + Jack 3.5mm) iniciado...", flush=True)

    ensure_snd_and_pipewire()

    devs = get_jack_devices()
    print(f"[*] Dispositivos Jack detectados: {[(ev, name) for ev, name in devs.values()]}", flush=True)

    jack_fd = next(iter(devs.keys()), None)
    if jack_fd is not None:
        headphones_plugged_state = check_jack_state(jack_fd)
        print(f"[*] Estado fisico inicial del Jack 3.5mm: {'Conectado' if headphones_plugged_state else 'Desconectado'}", flush=True)

    sync_hardware_state(force=True)

    while True:
        ensure_snd_and_pipewire()
        # Sincronizar estado de mute/altavoces cada ciclo con el volumen de PipeWire
        sync_hardware_state()

        if not devs:
            time.sleep(1.0)
            devs = get_jack_devices()
            jack_fd = next(iter(devs.keys()), None)
            continue

        try:
            r, _, _ = select.select(list(devs.keys()), [], [], 0.5)
        except Exception:
            devs = get_jack_devices()
            jack_fd = next(iter(devs.keys()), None)
            continue

        for fd in r:
            try:
                data = os.read(fd, EVENT_SIZE)
                if len(data) < EVENT_SIZE:
                    continue
                sec, usec, etype, code, val = struct.unpack(EVENT_FORMAT, data)

                if etype == EV_SW and (code == SW_HEADPHONE_INSERT or fd == jack_fd):
                    headphones_plugged_state = bool(val == 1) if code == SW_HEADPHONE_INSERT else check_jack_state(jack_fd)
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
