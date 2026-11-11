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

LK_IMG = os.path.join(STOCK_DIR, "lk.img")
GZ_IMG = os.path.join(STOCK_DIR, "gz.img")
BOOT_PMOS = os.path.join(V13_DIR, "pmos_boot_v13.img")
VENDOR_BOOT_PMOS = os.path.join(V13_DIR, "vendor_boot_pmos_v13.img")

TASKS = [
    ("w", "lk_b", LK_IMG, 30),
    ("w", "lk_a", LK_IMG, 30),
    ("w", "gz_b", GZ_IMG, 30),
    ("w", "gz_a", GZ_IMG, 30),
    ("w", "boot_b", BOOT_PMOS, 90),
    ("w", "vendor_boot_b", VENDOR_BOOT_PMOS, 90),
    ("w", "boot_a", BOOT_PMOS, 90),
    ("w", "vendor_boot_a", VENDOR_BOOT_PMOS, 90),
    ("e", "misc", None, 30),
]

def execute_task(op, part, img_path, timeout_sec):
    print(f"\n=======================================================")
    if op == "w":
        print(f"[*] Operación: ESCRIBIR {part} <- {os.path.basename(img_path)}")
        cmd = [sys.executable, os.path.join(MTK_DIR, "mtk.py"), f"--preloader={PRELOADER}", "w", part, img_path]
    elif op == "e":
        print(f"[*] Operación: BORRAR {part}")
        cmd = [sys.executable, os.path.join(MTK_DIR, "mtk.py"), f"--preloader={PRELOADER}", "e", part]
    print(f"=======================================================")

    for attempt in range(1, 30):
        print(f"[Intento {attempt}] Esperando captura de Preloader...")
        try:
            p = subprocess.run(cmd, cwd=MTK_DIR, capture_output=True, text=True, timeout=timeout_sec)
            out = (p.stdout + p.stderr).strip()
            
            if "Wrote" in out or "100.0% Upload" in out or "Erased" in out or p.returncode == 0:
                print(f"  🎉 ¡{part} COMPLETADO EXITOSAMENTE!")
                return True
            else:
                if "DA Stage 1" in out:
                    print(f"  [i] Handshake en curso...")
                else:
                    print(f"  [-] Salida: {out[-120:] if out else 'sin salida'}")
        except subprocess.TimeoutExpired:
            print(f"  [-] Timeout ({timeout_sec}s), reintentando en próximo ciclo...")
        except Exception as e:
            print(f"  [-] Error: {e}")
        time.sleep(1)
    return False

def main():
    print("#################################################################")
    print("   RESCATE INTEGRAL Y AUTOMÁTICO DE BOOTLOOP MEDIA_TEK MT6789   ")
    print("#################################################################")
    print("Dispositivo detectado en bucle cada 5s.")
    print("Capturaremos el Preloader de forma secuencial y atómica.\n")

    for op, part, img_path, tout in TASKS:
        success = execute_task(op, part, img_path, tout)
        if not success:
            print(f"\n❌ ERROR CRÍTICO al procesar {part}. Abortando rescate.")
            sys.exit(1)
        time.sleep(1.5)

    print("\n#################################################################")
    print("   🎉 TODAS LAS PARTICIONES RESTAURADAS CON ÉXITO ABSOLUTO       ")
    print("#################################################################")
    print("[*] Reiniciando la tablet...")
    reset_cmd = [sys.executable, os.path.join(MTK_DIR, "mtk.py"), f"--preloader={PRELOADER}", "reset"]
    subprocess.run(reset_cmd, cwd=MTK_DIR)
    print("\n🚀 ¡Tablet reiniciada en modo normal con postmarketOS v13!")

if __name__ == "__main__":
    main()
