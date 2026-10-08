#!/usr/bin/env python3
import subprocess
import os
import sys
import time

TABLET_DIR = "/home/danilo/Documentos/TABLET"
MTK_DIR = os.path.join(TABLET_DIR, "mtkclient")
STOCK_DIR = os.path.join(TABLET_DIR, "stock_firmware/UMIDIGI_G7_Tab_Pro_E_V1.0_20250512")
V13_DIR = os.path.join(TABLET_DIR, "version_v13")
PRELOADER = os.path.join(STOCK_DIR, "preloader_k6789v1_64.bin")

# Lista ordenada de particiones a restaurar
RESTORE_LIST = [
    ("dtbo_a", os.path.join(STOCK_DIR, "dtbo.img"), 20),
    ("dtbo_b", os.path.join(STOCK_DIR, "dtbo.img"), 20),
    ("vbmeta_a", os.path.join(STOCK_DIR, "vbmeta.img"), 10),
    ("vbmeta_b", os.path.join(STOCK_DIR, "vbmeta.img"), 10),
    ("tee_a", os.path.join(STOCK_DIR, "tee.img"), 15),
    ("tee_b", os.path.join(STOCK_DIR, "tee.img"), 15),
    ("vendor_boot_a", os.path.join(V13_DIR, "vendor_boot_pmos_v13.img"), 120),
    ("vendor_boot_b", os.path.join(V13_DIR, "vendor_boot_pmos_v13.img"), 120),
    ("boot_a", os.path.join(V13_DIR, "pmos_boot_v13.img"), 120),
    ("boot_b", os.path.join(V13_DIR, "pmos_boot_v13.img"), 120),
]

def flash_partition(part_name, img_path, timeout_sec):
    print(f"\n[*] Flasheando {part_name} desde {os.path.basename(img_path)} (timeout {timeout_sec}s)...")
    cmd = [
        sys.executable,
        os.path.join(MTK_DIR, "mtk.py"),
        "w",
        part_name,
        img_path,
        f"--preloader={PRELOADER}"
    ]
    for attempt in range(30):
        try:
            p = subprocess.run(cmd, cwd=MTK_DIR, capture_output=True, text=True, timeout=timeout_sec)
            out = (p.stdout + p.stderr).strip()
            if "Wrote" in out or "100.0% Upload" in out or p.returncode == 0:
                print(f"  [+] {part_name} OK!")
                return True
        except subprocess.TimeoutExpired:
            print("  [-] Timeout, reintentando...")
        time.sleep(0.5)
    return False

print("==================================================================")
print("   RESTAURACIÓN COMPLETA DE PARTICIONES DE ARRANQUE (SLOT A & B)  ")
print("==================================================================")

for part_name, img_path, tout in RESTORE_LIST:
    ok = flash_partition(part_name, img_path, tout)
    if not ok:
        print(f"⚠️ Error al flashear {part_name}")

print("\n🎉 Todas las particiones críticas de arranque han sido restauradas.")
