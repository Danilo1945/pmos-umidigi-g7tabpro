#!/usr/bin/env python3
import os
import sys
import time
import subprocess

TABLET_DIR = "/home/danilo/Documentos/TABLET"
MTK_DIR = os.path.join(TABLET_DIR, "mtkclient")
STOCK_DIR = os.path.join(TABLET_DIR, "stock_firmware/UMIDIGI_G7_Tab_Pro_E_V1.0_20250512")
V13_DIR = os.path.join(TABLET_DIR, "version_v13")
PRELOADER = os.path.join(STOCK_DIR, "preloader_k6789v1_64.bin")

BOOT_PMOS = os.path.join(V13_DIR, "pmos_boot_v13.img")
VENDOR_BOOT_PMOS = os.path.join(V13_DIR, "vendor_boot_pmos_v13.img")

print(f"[*] BOOT:   {BOOT_PMOS} ({os.path.getsize(BOOT_PMOS)} bytes)")
print(f"[*] VENDOR: {VENDOR_BOOT_PMOS} ({os.path.getsize(VENDOR_BOOT_PMOS)} bytes)")

STEPS = [
    ("w", "boot_b", BOOT_PMOS),
    ("w", "vendor_boot_b", VENDOR_BOOT_PMOS),
    ("w", "boot_a", BOOT_PMOS),
    ("w", "vendor_boot_a", VENDOR_BOOT_PMOS),
    ("e", "misc", None),
]

for op, part, path in STEPS:
    print(f"\n>>> INICIANDO {op.upper()} {part} <<<")
    if op == "w":
        cmd = [sys.executable, os.path.join(MTK_DIR, "mtk.py"), f"--preloader={PRELOADER}", "w", part, path]
    else:
        cmd = [sys.executable, os.path.join(MTK_DIR, "mtk.py"), f"--preloader={PRELOADER}", "e", part]

    for attempt in range(1, 30):
        print(f"[{part}] Intento {attempt}...")
        p = subprocess.run(cmd, cwd=MTK_DIR, capture_output=True, text=True, timeout=60)
        out = (p.stdout + p.stderr).strip()
        if "Wrote" in out or "100.0% Upload" in out or "Erased" in out or p.returncode == 0:
            print(f"✅ {part} COMPLETADO EXITOSAMENTE!")
            break
        print(f"  [-] Reintentando... ({out[-80:] if out else 'vacio'})")
        time.sleep(0.5)
    time.sleep(1)

print("\n🎉 FLASHEO DE AMBOS SLOTS TERMINADO.")
print("[*] Enviando comando de reset...")
reset_cmd = [sys.executable, os.path.join(MTK_DIR, "mtk.py"), f"--preloader={PRELOADER}", "reset"]
subprocess.run(reset_cmd, cwd=MTK_DIR)
print("🚀 Reset enviado.")
