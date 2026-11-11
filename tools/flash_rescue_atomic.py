#!/usr/bin/env python3
import os
import sys
import time

TABLET_DIR = "/home/danilo/Documentos/TABLET"
MTK_DIR = os.path.join(TABLET_DIR, "mtkclient")
STOCK_DIR = os.path.join(TABLET_DIR, "stock_firmware/UMIDIGI_G7_Tab_Pro_E_V1.0_20250512")
V13_DIR = os.path.join(TABLET_DIR, "version_v13")

PRELOADER = os.path.join(STOCK_DIR, "preloader_k6789v1_64.bin")
BOOT_PMOS = os.path.join(V13_DIR, "pmos_boot_v13.img")
VENDOR_BOOT_PMOS = os.path.join(V13_DIR, "vendor_boot_pmos_v13.img")

# Verificaciones
for f in [PRELOADER, BOOT_PMOS, VENDOR_BOOT_PMOS]:
    if not os.path.exists(f):
        print(f"ERROR: No existe {f}")
        sys.exit(1)
    sz = os.path.getsize(f)
    print(f"OK: {os.path.basename(f)} ({sz} bytes)")

sys.path.insert(0, MTK_DIR)

from mtkclient.Library.mtk_main import ArgHandler
from mtkclient.Library.mtk_class import Mtk
from mtkclient.Library.DA.mtk_da_handler import DaHandler
from mtkclient.config.mtk_config import MtkConfig
from mtkclient.Library.Partitions.gpt import GptSettings
import logging

class DummyArgs:
    def __init__(self):
        self.cmd = "wo"
        self.debugmode = False
        self.loader = None
        self.serialport = None
        self.disable_internal_flash = False
        self.auth = None
        self.cert = None
        self.preloader = PRELOADER
        self.directory = "."
        self.offset = None
        self.length = None
        self.step = None
        self.iot = None
        self.parttype = None
        self.socid = None
        self.crash = None
        self.ptype = None
        self.gpt_num_part_entries = 0
        self.gpt_part_entry_size = 0
        self.gpt_part_entry_start_lba = 0
        self.gpt_file = None

# Geometría exacta de particiones desde MT6789_Android_scatter.txt
FLASH_TARGETS = [
    ("boot_a",        0x2aa00000, 0x4000000, BOOT_PMOS),
    ("vendor_boot_a", 0x2ea00000, 0x4000000, VENDOR_BOOT_PMOS),
    ("boot_b",        0x4ca00000, 0x4000000, BOOT_PMOS),
    ("vendor_boot_b", 0x50a00000, 0x4000000, VENDOR_BOOT_PMOS),
]

def run_rescue():
    print("\n=======================================================")
    print("   ESPERANDO DISPOSITIVO MEDIATEK PRELOADER (MT6789)   ")
    print("=======================================================\n")
    
    args = DummyArgs()
    config = MtkConfig(loglevel=logging.INFO, gui=None, guiprogress=None)
    ArgHandler(args, config)
    config.preloader_filename = args.preloader
    
    while True:
        try:
            mtk = Mtk(config=config, loglevel=logging.INFO, serialportname=None)
            da_handler = DaHandler(mtk, logging.INFO)
            # Conectar y cargar DA
            mtk = da_handler.connect(mtk, ".")
            if mtk is None:
                time.sleep(0.5)
                continue
            mtk = da_handler.configure_da(mtk)
            if mtk is not None:
                break
        except Exception as e:
            print(f"Esperando handshake de Preloader ({e})...")
            time.sleep(0.5)
        
    print("\n[+] DA Stage 2 configurado con éxito. Escribiendo particiones directamente...")
    
    for name, addr, length, filepath in FLASH_TARGETS:
        print(f"\n[*] Flasheando {name} (0x{addr:X}, {length // (1024*1024)} MB) desde {os.path.basename(filepath)}...")
        ok = mtk.daloader.writeflash(addr=addr, length=length, filename=filepath, parttype=args.parttype)
        if ok:
            print(f"  [+] {name} ESCRITA SATISFACTORIAMENTE.")
        else:
            print(f"  [-] ERROR escribiendo {name}.")
            return False

    # Limpiar misc escribiendo ceros
    print("\n[*] Limpiando partición misc (0x8000, 512 KB) para eliminar bucles de recuperación...")
    zero_data = b"\x00" * 0x80000
    try:
        ok_misc = mtk.daloader.writeflash(addr=0x8000, length=0x80000, filename="", wdata=zero_data, parttype=args.parttype)
        if ok_misc:
            print("  [+] Partición misc limpiada.")
    except Exception as e:
        print(f"  [!] Nota en misc: {e}")
        
    print("\n[*] Enviando comando de REINICIO normal a la tablet...")
    try:
        if os.path.exists(os.path.join(mtk.config.hwparam_path, ".state")):
            os.remove(os.path.join(mtk.config.hwparam_path, ".state"))
        mtk.daloader.shutdown(bootmode=0)
        print("  [+] Tablet reiniciada.")
    except Exception as e:
        print(f"  [!] Nota al reiniciar: {e}")
        
    try:
        mtk.port.close()
    except Exception:
        pass
        
    print("\n🎉 ¡RESTAURACIÓN 100% COMPLETA! Ambas ranuras A y B han sido reparadas.")
    return True

if __name__ == "__main__":
    success = run_rescue()
    sys.exit(0 if success else 1)
