#!/usr/bin/env python3
import subprocess
import os
import sys
import time

TABLET_DIR = "/home/danilo/Documentos/TABLET"
MTK_DIR = os.path.join(TABLET_DIR, "mtkclient")
STOCK_DIR = os.path.join(TABLET_DIR, "stock_firmware/UMIDIGI_G7_Tab_Pro_E_V1.0_20250512")
PRELOADER = os.path.join(STOCK_DIR, "preloader_k6789v1_64.bin")
LK_IMG = os.path.join(STOCK_DIR, "lk.img")
GZ_IMG = os.path.join(STOCK_DIR, "gz.img")

print("===================================================================")
print("  RESCATE DE EMERGENCIA MTK: RESTAURACIÓN DE LK Y GZ (BOOTLOADER) ")
print("===================================================================")
print("[*] Esperando que desconectes y vuelvas a conectar el cable USB...")

# Comando directo para flashear LK (Little Kernel)
cmd_lk = [
    sys.executable,
    os.path.join(MTK_DIR, "mtk.py"),
    "w",
    "lk_a,lk_b",
    f"{LK_IMG},{LK_IMG}",
    f"--preloader={PRELOADER}"
]

attempt = 0
while True:
    attempt += 1
    p = subprocess.run(cmd_lk, cwd=MTK_DIR, capture_output=True, text=True)
    out = (p.stdout + p.stderr).strip()
    
    if "Please disconnect" in out:
        if attempt % 5 == 0:
            print("[*] Cable detectado sin handshake: Por favor DESCONECTA el cable USB y reconéctalo...")
    elif "Device detected" in out or "DA Stage 1" in out or "Progress:" in out or "100.0% Upload" in out:
        print("\n🎉 ¡DISPOSITIVO CAPTURADO! Flasheando Little Kernel...")
        print(out)
        if "100.0% Upload" in out or "Wrote" in out or p.returncode == 0:
            print("\n🎉🎉🎉 ¡LK_A Y LK_B RESTAURADOS EXITOSAMENTE!")
            break
    else:
        if attempt % 10 == 0:
            print(f"[{attempt}] Esperando conexión... ({out[:80]})")
    time.sleep(0.5)

print("\n[+] Paso 1/2 (LK) completado. El gestor de arranque ya tiene su núcleo restaurado.")
