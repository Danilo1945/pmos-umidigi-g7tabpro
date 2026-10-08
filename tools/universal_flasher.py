#!/usr/bin/env python3
import subprocess
import socket
import time
import sys
import os

print("==========================================================")
print("  CENTINELA UNIVERSAL DE RESCATE Y FLASHEO PERMANENTE    ")
print("==========================================================")
print("[*] Esperando a la tablet (Modo Fastboot o Modo Kernel)...")

TABLET_DIR = "/home/danilo/Documentos/TABLET"
BOOT_IMG = f"{TABLET_DIR}/version_v13/pmos_boot_v13.img"
VENDOR_BOOT_IMG = f"{TABLET_DIR}/version_v13/vendor_boot_pmos_v13.img"
DTBO_IMG = f"{TABLET_DIR}/dtbo.img"
VBMETA_IMG = f"{TABLET_DIR}/vbmeta_disabled_official.img"
MISC_IMG = f"{TABLET_DIR}/misc_slot_b_verified.img"

for f in [BOOT_IMG, VENDOR_BOOT_IMG, DTBO_IMG, VBMETA_IMG, MISC_IMG]:
    if not os.path.exists(f):
        print(f"[-] ERROR: Archivo faltante: {f}")
        sys.exit(1)

while True:
    # 1. Comprobar si está en FASTBOOT
    fb_out = subprocess.run("fastboot devices", shell=True, capture_output=True, text=True).stdout
    if "fastboot" in fb_out:
        print("\n🎉 ¡TABLET DETECTADA EN FASTBOOT!")
        print("[*] Flasheando particiones en Slot B con configuración verificada...")
        subprocess.run(f"fastboot flash misc {MISC_IMG}", shell=True, check=True)
        subprocess.run(f"fastboot flash vbmeta_b {VBMETA_IMG}", shell=True, check=True)
        subprocess.run(f"fastboot flash vbmeta_a {VBMETA_IMG}", shell=True, check=True)
        subprocess.run(f"fastboot flash dtbo_b {DTBO_IMG}", shell=True, check=True)
        subprocess.run(f"fastboot flash boot_b {BOOT_IMG}", shell=True, check=True)
        subprocess.run(f"fastboot flash vendor_boot_b {VENDOR_BOOT_IMG}", shell=True, check=True)
        subprocess.run(f"fastboot flash boot_a {BOOT_IMG}", shell=True, check=True)
        subprocess.run(f"fastboot flash vendor_boot_a {VENDOR_BOOT_IMG}", shell=True, check=True)
        subprocess.run("fastboot --set-active=b 2>/dev/null || true", shell=True)
        print("🎉 ¡FLASHEO FASTBOOT EXITOSO EN AMBOS SLOTS!")
        print("[*] Reiniciando tablet en postmarketOS v13...")
        subprocess.run("fastboot reboot", shell=True)
        sys.exit(0)

    # 2. Comprobar si está en MODO KERNEL (USB Gadget)
    links = subprocess.run("ip -br link", shell=True, capture_output=True, text=True).stdout
    for line in links.splitlines():
        if line.startswith("enx") or "usb" in line:
            ifname = line.split()[0]
            subprocess.run(f"sudo ip addr add 172.16.42.2/24 dev {ifname} 2>/dev/null || true", shell=True)
            subprocess.run(f"sudo ip link set {ifname} up 2>/dev/null || true", shell=True)
            
            for port in [2323, 23]:
                try:
                    s = socket.socket()
                    s.settimeout(0.5)
                    s.connect(("172.16.42.1", port))
                    print(f"\n🎉 ¡TABLET DETECTADA EN RED KERNEL (puerto {port})!")
                    cmd = (
                        "wget -O /tmp/boot_v13.img http://172.16.42.2:8081/pmos_boot_v13.img && "
                        "dd if=/tmp/boot_v13.img of=/dev/sdc28 bs=4M conv=fsync && "
                        "dd if=/tmp/boot_v13.img of=/dev/sdc51 bs=4M conv=fsync && "
                        "sync && echo __FLASH_SUCCESS__\n"
                    )
                    s.sendall(cmd.encode())
                    buf = b""
                    t0 = time.time()
                    while time.time() - t0 < 10:
                        chunk = s.recv(4096)
                        if not chunk: break
                        buf += chunk
                        if b"__FLASH_SUCCESS__" in buf:
                            print("🎉 ¡IMAGEN GRABADA POR RED EN AMBOS SLOTS!")
                            s.sendall(b"reboot -f\n")
                            sys.exit(0)
                except Exception:
                    pass
    time.sleep(0.5)
