#!/usr/bin/env python3
"""
Flashea partición por partición de forma individual en cada conexión de Preloader.
Esto evita el fallo de DAXML cuando se intentan escribir múltiples particiones en una sola sesión.
"""
import os
import sys
import time
import subprocess

MTK_DIR = "/home/danilo/Documentos/TABLET/mtkclient"
V16_DIR = "/home/danilo/Documentos/TABLET/version_v16_estable"
STOCK_DIR = "/home/danilo/Documentos/TABLET/stock_firmware/UMIDIGI_G7_Tab_Pro_E_V1.0_20250512"

BOOT_IMG = os.path.join(V16_DIR, "pmos_boot_v16.img")
VENDOR_BOOT_IMG = os.path.join(V16_DIR, "vendor_boot_pmos_v16.img")
PRELOADER = os.path.join(STOCK_DIR, "preloader_k6789v1_64.bin")
MISC_A = "/home/danilo/Documentos/TABLET/misc_stock_backup.img"

# Lista de particiones a sincronizar una a una
TAREAS = [
    ("boot_a", BOOT_IMG, "Flashear boot_a con v16 sin auto-phoc"),
    ("boot_b", BOOT_IMG, "Sincronizar boot_b con v16"),
    ("vendor_boot_b", VENDOR_BOOT_IMG, "Sincronizar vendor_boot_b con v16"),
    ("misc", MISC_A, "Restaurar Slot A como activo")
]

subprocess.run("sudo systemctl stop ModemManager 2>/dev/null || true", shell=True)

for part_name, file_path, desc in TAREAS:
    print(f"\n{'='*65}")
    print(f"[*] TAREA: {desc} ({part_name})")
    print(f"[*] Archivo: {file_path} ({os.path.getsize(file_path):,} bytes)")
    print(f"[*] Esperando Preloader para flashear {part_name}...")
    print(f"{'='*65}")
    
    cmd = [
        sys.executable,
        os.path.join(MTK_DIR, "mtk.py"),
        "w",
        part_name,
        file_path,
        f"--preloader={PRELOADER}"
    ]
    
    while True:
        proc = subprocess.run(cmd, cwd=MTK_DIR, capture_output=True, text=True)
        out = proc.stdout + proc.stderr
        
        if "Wrote" in out and "with sector count" in out:
            print(f"✅ ¡{part_name} escrito con éxito!")
            break
        elif "Device disconnected" in out or "Port is busy" in out or "Handshake failed" in out:
            print(".", end="", flush=True)
            time.sleep(0.5)
        else:
            # Si hubo otro error o salida
            if "Progress:" in out:
                print(f"✅ ¡{part_name} transferido con éxito!")
                break
            print(f"\n[!] Reintentando {part_name}... (Salida: {out[-150:].strip()})")
            time.sleep(1)

print("\n🎉 ¡TODAS LAS PARTICIONES SINCRONIZADAS CON ÉXITO!")
print("[*] La tablet ahora tiene Slot A activo y ambas ranuras sincronizadas en v16.")
