#!/usr/bin/env python3
"""
Monitor en tiempo real del arranque de postmarketOS v16.
Detecta conexión por USB Ethernet (172.16.42.1) o puerto serie (/dev/ttyACM0).
"""
import os
import sys
import time
import subprocess

print("[*] Iniciando monitor de arranque postmarketOS v16...")
sys.stdout.flush()

for attempt in range(120):
    # 1. Comprobar ttyACM0
    if os.path.exists("/dev/ttyACM0"):
        print(f"\n[+] Puerto /dev/ttyACM0 detectado en intento {attempt}!")
        sys.stdout.flush()
        break
        
    # 2. Comprobar ping
    res = subprocess.run(["ping", "-c", "1", "-W", "1", "172.16.42.1"], capture_output=True)
    if res.returncode == 0:
        print(f"\n[+] Red USB activa en 172.16.42.1 en intento {attempt}!")
        sys.stdout.flush()
        break
        
    time.sleep(1)
    if attempt % 5 == 0:
        print(".", end="", flush=True)
