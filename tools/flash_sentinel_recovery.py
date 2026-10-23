#!/usr/bin/env python3
import os
import sys
import time
import socket
import subprocess
import threading
from http.server import SimpleHTTPRequestHandler, HTTPServer

V15_DIR = "/home/danilo/Documentos/TABLET/version_v15_estable"
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
time.sleep(0.5)

print("[*] Centinela de recuperación ultra-rápido activo...")

while True:
    links = subprocess.run("ip -br link", shell=True, capture_output=True, text=True).stdout
    iface = None
    for line in links.splitlines():
        if line.startswith("enx") or "usb" in line:
            iface = line.split()[0]
            break
    
    if iface:
        subprocess.run(f"sudo ip addr add 172.16.42.2/24 dev {iface} 2>/dev/null || true", shell=True)
        subprocess.run(f"sudo ip link set {iface} up 2>/dev/null || true", shell=True)
        
        try:
            s = socket.socket()
            s.settimeout(0.5)
            s.connect(("172.16.42.1", 2323))
            print("\n🎉 ¡CONEXIÓN ESTABLECIDA CON LA TABLET EN PUERTO 2323!")
            
            # 1. DETENER INMEDIATAMENTE EL CRASH LOOP MATANDO PHOSH ANTES DE QUE SUENE EL RELOJ
            stop_cmd = "killall -9 start_phosh_service.sh start_phosh.sh phoc phosh 2>/dev/null; pkill -9 -f start_phosh 2>/dev/null; echo CRASH_DETENIDO\n"
            s.sendall(stop_cmd.encode("utf-8"))
            time.sleep(0.2)
            
            # 2. Descargar y flashear boot limpio
            flash_cmd = (
                f"wget -q -O /tmp/boot_v15.img http://172.16.42.2:{PORT}/pmos_boot_v15.img && "
                f"dd if=/tmp/boot_v15.img of=/dev/sdc28 bs=4M conv=fsync && "
                f"dd if=/tmp/boot_v15.img of=/dev/sdc51 bs=4M conv=fsync && "
                f"sync && echo FLASH_BOOT_RECOVERED && reboot -f\n"
            )
            print("[*] Transfiriendo y escribiendo pmos_boot_v15.img...")
            s.sendall(flash_cmd.encode("utf-8"))
            
            buf = b""
            start_t = time.time()
            while time.time() - start_t < 25:
                chunk = s.recv(4096)
                if not chunk:
                    break
                buf += chunk
                text = chunk.decode(errors="replace")
                print(text, end="", flush=True)
                if "FLASH_BOOT_RECOVERED" in text:
                    print("\n\n🎉🎉🎉 ¡TABLET RECUPERADA Y FLASHEADA CON ÉXITO ABSOLUTO!")
                    s.close()
                    sys.exit(0)
            s.close()
        except Exception:
            pass
    time.sleep(0.15)
