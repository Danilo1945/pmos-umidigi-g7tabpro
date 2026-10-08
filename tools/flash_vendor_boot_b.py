#!/usr/bin/env python3
import os
import sys
import time
import subprocess

MTKCLIENT_DIR = "/home/danilo/Documentos/TABLET/mtkclient"
VENDOR_BOOT_IMG = "/home/danilo/Documentos/TABLET/version_v13/vendor_boot_pmos_v13.img"
BOOT_IMG = "/home/danilo/Documentos/TABLET/version_v13/pmos_boot_v13.img"
PRELOADER = "/home/danilo/Documentos/TABLET/stock_firmware/UMIDIGI_G7_Tab_Pro_E_V1.0_20250512/preloader_k6789v1_64.bin"

def flash_one(part, img):
    cmd = [
        sys.executable,
        os.path.join(MTKCLIENT_DIR, "mtk.py"),
        f"--preloader={PRELOADER}",
        "w",
        part,
        img
    ]
    print(f"\n[*] Flasheando {part} desde {os.path.basename(img)}...")
    while True:
        proc = subprocess.Popen(cmd, cwd=MTKCLIENT_DIR)
        ret = proc.wait()
        if ret == 0:
            print(f"🎉 ¡{part} ESCRITO EXITOSAMENTE!")
            break
        print(f"[!] Reintentando {part} en el siguiente ciclo...")
        time.sleep(1)

flash_one("vendor_boot_b", VENDOR_BOOT_IMG)
flash_one("boot_a", BOOT_IMG)
flash_one("vendor_boot_a", VENDOR_BOOT_IMG)

print("\n[*] Reiniciando la tablet...")
reset_cmd = [
    sys.executable,
    os.path.join(MTKCLIENT_DIR, "mtk.py"),
    f"--preloader={PRELOADER}",
    "reset"
]
subprocess.run(reset_cmd, cwd=MTKCLIENT_DIR)
print("🎉 ¡TODAS LAS PARTICIONES DE ARRANQUE ESTÁN 100% SINCRONIZADAS!")
