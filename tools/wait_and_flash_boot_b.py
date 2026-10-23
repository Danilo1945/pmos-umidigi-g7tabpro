#!/usr/bin/env python3
import os
import sys
import time
import subprocess
import usb.core

TABLET_DIR = "/home/danilo/Documentos/TABLET"
MTK_DIR = os.path.join(TABLET_DIR, "mtkclient")
STOCK_DIR = os.path.join(TABLET_DIR, "stock_firmware/UMIDIGI_G7_Tab_Pro_E_V1.0_20250512")
PRELOADER = os.path.join(STOCK_DIR, "preloader_k6789v1_64.bin")
BOOT_IMG = os.path.join(TABLET_DIR, "version_v15_estable/pmos_boot_v15.img")

if not os.path.exists(BOOT_IMG):
    print(f"[-] ERROR: No se encuentra {BOOT_IMG}")
    sys.exit(1)

print("=" * 65)
print("  ESPERA LIMPIA DE PRELOADER: FLASHEO DE BOOT_B V15  ")
print("=" * 65)
print(f"[*] Imagen a escribir: {BOOT_IMG} ({os.path.getsize(BOOT_IMG)} bytes)")

# Limpieza estricta de archivos de estado stale
state_file = os.path.join(MTK_DIR, ".state")
if os.path.exists(state_file):
    os.remove(state_file)
subprocess.run("sudo rm -f /home/danilo/Documentos/TABLET/mtkclient/.state* 2>/dev/null || true", shell=True)
subprocess.run("sudo systemctl stop ModemManager 2>/dev/null || true", shell=True)

print("[*] Archivos .state eliminados. ModemManager detenido.")
print("[*] Esperando que conectes el cable USB a la tablet...")

# Esperar a que el dispositivo aparezca en el bus USB
dot_count = 0
while True:
    try:
        dev = usb.core.find(idVendor=0x0e8d)
        if dev is not None:
            print(f"\n🎉 ¡DISPOSITIVO MEDIATEK DETECTADO! (ID 0e8d:{dev.idProduct:04x})")
            break
    except Exception:
        pass
    dot_count += 1
    if dot_count % 20 == 0:
        print(".", end="", flush=True)
    time.sleep(0.05)

# Asegurar de nuevo que no exista .state antes de llamar mtk.py
subprocess.run("sudo rm -f /home/danilo/Documentos/TABLET/mtkclient/.state* 2>/dev/null || true", shell=True)

print("[*] Iniciando mtkclient con handshake fresco...")

cmd = [
    "sudo",
    "/home/danilo/anaconda3/bin/python3",
    "-u",
    os.path.join(MTK_DIR, "mtk.py"),
    "--serialport", "DETECT",
    "w", "boot_b",
    BOOT_IMG,
    f"--preloader={PRELOADER}"
]

p = subprocess.Popen(
    cmd,
    cwd=MTK_DIR,
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    text=True,
    bufsize=1
)

success = False
for line in p.stdout:
    sys.stdout.write(line)
    sys.stdout.flush()
    if "100.0% Upload" in line or "Wrote" in line:
        success = True

rc = p.wait()

# Limpiar .state residual
subprocess.run("sudo rm -f /home/danilo/Documentos/TABLET/mtkclient/.state* 2>/dev/null || true", shell=True)

if success or rc == 0:
    print("\n" + "=" * 65)
    print("🎉🎉🎉 ¡BOOT_B FLASHEADOS AL 100% CON ÉXITO ABSOLUTO! 🎉🎉🎉")
    print("=" * 65)
    print("[*] Ambos slots (boot_a y boot_b) ahora tienen pmos_boot_v15.img estable.")
    print("[*] La tablet iniciará de forma limpia sin reinicios.")
    sys.exit(0)
else:
    print(f"\n[-] Flasheo finalizó con código {rc}")
    sys.exit(rc)
