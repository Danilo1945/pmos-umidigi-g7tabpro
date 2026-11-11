#!/usr/bin/env python3
import os
import sys
import time
import subprocess

TABLET_DIR = "/home/danilo/Documentos/TABLET"
MTK_DIR = os.path.join(TABLET_DIR, "mtkclient")
STOCK_DIR = os.path.join(TABLET_DIR, "stock_firmware/UMIDIGI_G7_Tab_Pro_E_V1.0_20250512")
PRELOADER = os.path.join(STOCK_DIR, "preloader_k6789v1_64.bin")

sys.path.insert(0, MTK_DIR)

from mtkclient.Library.mtk_main import ArgHandler
from mtkclient.Library.mtk_class import Mtk
from mtkclient.Library.DA.mtk_da_handler import DaHandler
from mtkclient.config.mtk_config import MtkConfig
import logging

class DummyArgs:
    def __init__(self):
        self.cmd = "printgpt"
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

def check_battery():
    print("=======================================================")
    print("   ESPERANDO CONEXIÓN DE LA TABLET (USB)              ")
    print("   Conecte el cable USB para leer el estado/batería    ")
    print("=======================================================")
    
    args = DummyArgs()
    config = MtkConfig(loglevel=logging.INFO, gui=None, guiprogress=None)
    ArgHandler(args, config)
    config.preloader_filename = args.preloader
    
    while True:
        try:
            mtk = Mtk(config=config, loglevel=logging.INFO, serialportname=None)
            da_handler = DaHandler(mtk, logging.INFO)
            mtk = da_handler.connect(mtk, ".")
            if mtk is None:
                time.sleep(0.5)
                continue
            
            print("[+] Dispositivo MediaTek conectado a nivel Preloader / DA.")
            # Al configurar DA, la tablet reporta en XML la información de hardware: ram, ufs, battery_voltage
            mtk = da_handler.configure_da(mtk)
            if mtk is not None:
                print("[+] DA inicializado.")
                # Obtener voltage si fue guardado en config o variables
                break
        except Exception as e:
            time.sleep(0.5)

if __name__ == "__main__":
    check_battery()
