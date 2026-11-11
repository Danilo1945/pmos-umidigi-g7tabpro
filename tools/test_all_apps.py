#!/usr/bin/env python3
"""
Script de prueba automatizada de todas las aplicaciones instaladas en la tablet
UMIDIGI G7 Tab Pro (postmarketOS v15 / Phosh + Phoc Wayland).
"""
import subprocess
import sys
import os

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
TABLET_CMD = os.path.join(SCRIPT_DIR, "tablet_cmd.py")

REMOTE_TEST_PY = r'''
import subprocess, time, os, glob, signal

dbus_socks = sorted(glob.glob("/tmp/dbus-*"), key=os.path.getmtime, reverse=True)
dbus_addr = f"unix:path={dbus_socks[0]}" if dbus_socks else ""

env = os.environ.copy()
env.update({
    "HOME": "/root",
    "USER": "root",
    "XDG_RUNTIME_DIR": "/tmp/runtime-root",
    "WAYLAND_DISPLAY": "wayland-0",
    "GDK_BACKEND": "wayland",
    "GSK_RENDERER": "cairo",
    "QT_QPA_PLATFORM": "wayland",
    "QT_QUICK_BACKEND": "software",
    "QTWEBENGINE_DISABLE_SANDBOX": "1",
    "XDG_SESSION_TYPE": "wayland",
    "XDG_CURRENT_DESKTOP": "GNOME:Phosh",
    "XDG_SESSION_DESKTOP": "phosh",
    "MOZ_ENABLE_WAYLAND": "1",
    "NO_AT_BRIDGE": "1",
    "DBUS_SESSION_BUS_ADDRESS": dbus_addr,
})
env.pop("LD_PRELOAD", None)

apps = [
    ("Settings (GNOME)", ["/usr/local/bin/gnome-control-center"]),
    ("Amberol", ["amberol"]),
    ("Angelfish", ["angelfish"]),
    ("Arianna", ["arianna"]),
    ("Calculator (Kalk)", ["kalk"]),
    ("Clock (KClock)", ["/usr/bin/kclock"]),
    ("Kasts", ["kasts"]),
    ("KDE Itinerary", ["itinerary"]),
    ("Photos (Koko)", ["koko"]),
    ("Recorder (KRecorder)", ["krecorder"]),
    ("Software (GNOME)", ["/usr/local/bin/gnome-software"]),
    ("Terminal (QMLKonsole)", ["qmlkonsole"]),
    ("Tokodon", ["tokodon"]),
    ("Visual Voicemail", ["/usr/bin/vvmplayer"]),
    ("Weather (KWeather)", ["kweather"]),
    ("Firefox", ["/usr/lib/firefox/firefox"]),
    ("Foot Terminal", ["foot"]),
    ("mpv Media Player", ["mpv", "--player-operation-mode=pseudo-gui"]),
    ("Alligator", ["alligator"]),
    ("AudioTube", ["audiotube"]),
    ("Discover", ["plasma-discover"]),
    ("Dolphin", ["dolphin"]),
    ("Elisa", ["elisa"]),
    ("KAlgebra Mobile", ["kalgebramobile"]),
    ("Keysmith", ["keysmith"]),
    ("NeoChat", ["neochat"]),
    ("Okular Mobile", ["okularkirigami"]),
    ("Phonebook", ["plasma-phonebook"]),
    ("Spacebar", ["spacebar"]),
    ("Plasma Settings", ["plasma-settings"]),
    ("Parental Controls", ["malcontent-control"]),
]

subprocess.run("pkill -9 gnome-control-center gnome-software amberol firefox foot mpv", shell=True)
time.sleep(0.3)

print(f"{'APLICACION':<24} | {'ESTADO':<10} | {'PID':<7} | {'DETALLE'}")
print("-" * 100)

for name, cmd in apps:
    try:
        proc = subprocess.Popen(
            cmd,
            env=env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            start_new_session=True
        )
        time.sleep(1.8)
        ret = proc.poll()
        if ret is None:
            status = "OK (VIVA)"
            detail = "Ventana Wayland activa sin errores"
            try:
                os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
                time.sleep(0.2)
                if proc.poll() is None:
                    os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
            except Exception:
                pass
        else:
            err = proc.stderr.read().decode("utf-8", errors="ignore").strip().splitlines()
            err_filtered = [l for l in err if "QML" in l or "Error" in l or "error" in l or "ERROR" in l or "symbol" in l or "not found" in l]
            last_err = (err_filtered[-1] if err_filtered else (err[-1] if err else f"Exit code {ret}"))
            status = f"EXIT({ret})"
            detail = last_err[:90]
        print(f"{name:<24} | {status:<10} | {proc.pid:<7} | {detail}", flush=True)
    except Exception as e:
        print(f"{name:<24} | ERROR      | -       | {str(e)[:90]}", flush=True)
'''

def main():
    env = os.environ.copy()
    env["TABLET_TIMEOUT"] = "150"
    subprocess.run([sys.executable, TABLET_CMD, "cat << 'EOF' > /sysroot/tmp/run_app_tests.py\n" + REMOTE_TEST_PY + "\nEOF\n"], env=env, check=True)
    res = subprocess.run([sys.executable, TABLET_CMD, "chroot /sysroot python3 /tmp/run_app_tests.py"], env=env, text=True)
    sys.exit(res.returncode)

if __name__ == "__main__":
    main()
