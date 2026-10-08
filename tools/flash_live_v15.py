#!/usr/bin/env python3
import os
import sys
import time
import socket
import hashlib
import threading
from http.server import SimpleHTTPRequestHandler, HTTPServer

V15_DIR = "/home/danilo/Documentos/TABLET/version_v15_estable"
TOOLS_DIR = "/home/danilo/Documentos/TABLET/FUNCIONALIDADES_FINALES/tools"
sys.path.insert(0, TOOLS_DIR)
import tablet_cmd

BOOT_IMG = os.path.join(V15_DIR, "pmos_boot_v15.img")
VB_IMG = os.path.join(V15_DIR, "vendor_boot_pmos_v15.img")

def md5_file(filepath):
    h = hashlib.md5()
    with open(filepath, "rb") as f:
        while chunk := f.read(4 * 1024 * 1024):
            h.update(chunk)
    return h.hexdigest()

boot_md5 = md5_file(BOOT_IMG)
vb_md5 = md5_file(VB_IMG)
print(f"[*] Boot v15 MD5:        {boot_md5}")
print(f"[*] Vendor Boot v15 MD5: {vb_md5}")

PORT = 8089
class QuietHandler(SimpleHTTPRequestHandler):
    def log_message(self, format, *args):
        pass

def run_server():
    os.chdir(V15_DIR)
    httpd = HTTPServer(("0.0.0.0", PORT), QuietHandler)
    httpd.serve_forever()

t = threading.Thread(target=run_server, daemon=True)
t.start()
time.sleep(1)

print("[*] Servidor HTTP local iniciado en puerto 8089...")
print("[*] Descargando y flasheando pmos_boot_v15.img en la tablet...")

cmd_boot = (
    f"wget -q -O /tmp/boot_v15.img http://172.16.42.2:{PORT}/pmos_boot_v15.img && "
    f"dd if=/tmp/boot_v15.img of=/dev/sdc28 bs=4M conv=fsync && "
    f"dd if=/tmp/boot_v15.img of=/dev/sdc51 bs=4M conv=fsync && "
    f"rm -f /tmp/boot_v15.img && sync && echo BOOT_FLASH_OK"
)
res = tablet_cmd.run_remote(cmd_boot, timeout=60)
print(f"[+] Resultado Boot: {res.strip()}")

print("[*] Descargando y flasheando vendor_boot_pmos_v15.img en la tablet...")
cmd_vb = (
    f"wget -q -O /tmp/vb_v15.img http://172.16.42.2:{PORT}/vendor_boot_pmos_v15.img && "
    f"dd if=/tmp/vb_v15.img of=/dev/sdc29 bs=4M conv=fsync && "
    f"dd if=/tmp/vb_v15.img of=/dev/sdc52 bs=4M conv=fsync && "
    f"rm -f /tmp/vb_v15.img && sync && echo VB_FLASH_OK"
)
res = tablet_cmd.run_remote(cmd_vb, timeout=60)
print(f"[+] Resultado Vendor Boot: {res.strip()}")

print("[*] Verificando hashes MD5 en las particiones de la tablet...")
verify_cmd = (
    f"head -c 67108864 /dev/sdc28 | md5sum; "
    f"head -c 67108864 /dev/sdc51 | md5sum; "
    f"head -c 67108864 /dev/sdc29 | md5sum; "
    f"head -c 67108864 /dev/sdc52 | md5sum"
)
res = tablet_cmd.run_remote(verify_cmd, timeout=40)
print("\nHashes leídos desde los bloques de la tablet:")
lines = [l.strip() for l in res.strip().split("\n") if l.strip()]
for l in lines:
    print(f"  {l}")

ok_boot = any(boot_md5 in l for l in lines)
ok_vb = any(vb_md5 in l for l in lines)

if ok_boot and ok_vb:
    print("\n🎉 ¡FLASHEO EN VIVO v15.0 ESTABLE COMPLETADO Y VERIFICADO AL 100%!")
else:
    print("\n⚠️ Advertencia en verificación de hashes. Revisar salida.")
