#!/usr/bin/env python3
"""
Control y Diagnóstico de Periféricos Adicionales — UMIDIGI G7 Tab Pro (MT6789)
Uso:
  python3 tablet_peripherals.py status         # Estado integral de periféricos
  python3 tablet_peripherals.py torch on       # Encender linterna trasera AW36515
  python3 tablet_peripherals.py torch off      # Apagar linterna trasera AW36515
  python3 tablet_peripherals.py vibrate [ms]   # Activar motor de vibración (def: 250 ms)
  python3 tablet_peripherals.py mic-test       # Probar captura de micrófono integrado (1s)
"""
import subprocess
import sys
import os

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
CMD_SCRIPT = os.path.join(SCRIPT_DIR, "tablet_cmd.py")


def run_remote(cmd):
    res = subprocess.run(
        ["python3", CMD_SCRIPT, cmd],
        capture_output=True,
        text=True,
        timeout=15,
    )
    return res.stdout.strip()


def main():
    if len(sys.argv) < 2:
        print(__doc__.strip())
        sys.exit(1)

    action = sys.argv[1].lower()
    if action == "torch":
        sub = sys.argv[2].lower() if len(sys.argv) > 2 else "status"
        if sub in ("on", "1"):
            out = run_remote(
                "echo '0 0 0 1' > /sys/class/flashlight_core/flashlight/flashlight_torch; "
                "cat /sys/class/flashlight_core/flashlight/flashlight_torch"
            )
            print(f"[+] Linterna AW36515 ENCENDIDA:\n{out}")
        elif sub in ("off", "0"):
            out = run_remote(
                "echo '0 0 0 0' > /sys/class/flashlight_core/flashlight/flashlight_torch; "
                "cat /sys/class/flashlight_core/flashlight/flashlight_torch"
            )
            print(f"[+] Linterna AW36515 APAGADA:\n{out}")
        else:
            out = run_remote("cat /sys/class/flashlight_core/flashlight/flashlight_torch")
            print(f"[*] Estado de linterna AW36515:\n{out}")
    elif action == "vibrate":
        ms = int(sys.argv[2]) if len(sys.argv) > 2 else 250
        sec = max(0.05, min(ms / 1000.0, 2.0))
        out = run_remote(
            f"echo 1 > /sys/class/leds/vibrator/brightness; sleep {sec:.2f}; "
            f"echo 0 > /sys/class/leds/vibrator/brightness; "
            f"dmesg | grep -i vibrat | tail -n 2"
        )
        print(f"[+] Vibración ejecutada ({ms} ms):\n{out}")
    elif action == "mic-test":
        out = run_remote(
            "chroot /sysroot arecord -D plughw:0,9 -f S16_LE -r 48000 -c 2 -d 1 /tmp/mic_check.wav 2>&1; "
            "ls -lh /sysroot/tmp/mic_check.wav"
        )
        print(f"[+] Prueba de micrófono integrado (Capture_1 plughw:0,9):\n{out}")
    elif action == "status":
        out = run_remote(
            "echo '=== LINTERNA AW36515 ==='; cat /sys/class/flashlight_core/flashlight/flashlight_capability 2>/dev/null; "
            "echo '=== VIBRADOR ==='; ls -d /sys/class/leds/vibrator 2>/dev/null; "
            "echo '=== MICROSD MMC0 ==='; ls -d /sys/class/mmc_host/mmc0 2>/dev/null; "
            "echo '=== CONNSYS & SENSORES ==='; ls -la /dev/stpwmt /dev/stpbt /dev/stpgps /dev/fm /dev/hf_manager /dev/ttyC0 2>/dev/null; "
            "echo '=== PIPEWIRE AUDIO ==='; chroot /sysroot env XDG_RUNTIME_DIR=/tmp/runtime-root wpctl status 2>/dev/null | grep -A 6 'Audio'"
        )
        print(out)
    else:
        print(__doc__.strip())
        sys.exit(1)


if __name__ == "__main__":
    main()
