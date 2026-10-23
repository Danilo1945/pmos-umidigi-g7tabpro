#!/usr/bin/env python3
import os
import sys
import time
import subprocess

TABLET_DIR = "/home/danilo/Documentos/TABLET"
MTK_DIR = os.path.join(TABLET_DIR, "mtkclient")
STOCK_DIR = os.path.join(TABLET_DIR, "stock_firmware/UMIDIGI_G7_Tab_Pro_E_V1.0_20250512")
PRELOADER = os.path.join(STOCK_DIR, "preloader_k6789v1_64.bin")
BOOT_IMG = os.path.join(TABLET_DIR, "version_v15_estable/pmos_boot_v15.img")

if not os.path.exists(BOOT_IMG):
    print(f"[-] ERROR: No se encuentra {BOOT_IMG}")
    sys.exit(1)

print("=" * 65)
print("  BUCLE DE RESCATE: FLASHEO DE BOOT_B V15 (MTK PRELOADER)  ")
print("=" * 65)
print(f"[*] Imagen a escribir: {BOOT_IMG} ({os.path.getsize(BOOT_IMG)} bytes)")
print("[*] Iniciando centinela de captura continua...")

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

attempt = 0
while True:
    attempt += 1
    subprocess.run("sudo systemctl stop ModemManager 2>/dev/null || true", shell=True)
    
    print(f"\n[Intento #{attempt}] Ejecutando mtkclient y esperando handshake...")
    p = subprocess.Popen(
        cmd,
        cwd=MTK_DIR,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1
    )

    success = False
    captured_da = False
    
    for line in p.stdout:
        sys.stdout.write(line)
        sys.stdout.flush()
        if "Device detected" in line or "Uploading stage" in line or "DAXML" in line:
            captured_da = True
        if "100.0% Upload" in line or "Wrote" in line:
            success = True

    rc = p.wait()
    
    if success or rc == 0:
        print("\n" + "=" * 65)
        print("🎉🎉🎉 ¡BOOT_B FLASHEADOS AL 100% CON ÉXITO ABSOLUTO! 🎉🎉🎉")
        print("=" * 65)
        print("[*] Ambas particiones boot_a y boot_b ahora tienen pmos_boot_v15.img")
        print("[*] La tablet debería iniciar ahora de forma estable.")
        sys.exit(0)
    else:
        print(f"[-] Intento #{attempt} finalizó (rc={rc}).")
        print("[*] Por favor: DESCONECTA el cable USB y VUELVE A CONECTARLO para nuevo handshake...")
        time.sleep(1)

