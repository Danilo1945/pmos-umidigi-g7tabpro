#!/usr/bin/env python3
import time
import subprocess
import socket
import sys

print("==================================================================")
print("  MONITOR DE ARRANQUE: ESPERANDO POSTMARKETOS EN LA TABLET  ")
print("==================================================================")
print("[*] Desconecta y reconecta el cable USB en la tablet si aún no lo has hecho.")
print("[*] Esperando que la interfaz de red USB aparezca...")

while True:
    links = subprocess.run("ip -br link", shell=True, capture_output=True, text=True).stdout
    iface = None
    for line in links.splitlines():
        if line.startswith("enx") or "usb" in line:
            iface = line.split()[0]
            break
            
    if iface:
        print(f"\n🎉 ¡INTERFAZ DE RED DETECTADA: {iface}!")
        subprocess.run(f"sudo ip addr add 172.16.42.2/24 dev {iface} 2>/dev/null || true", shell=True)
        subprocess.run(f"sudo ip link set {iface} up 2>/dev/null || true", shell=True)
        print("[*] IP 172.16.42.2 asignada. Probando conexión al puerto 2323 de la tablet...")
        
        for _ in range(30):
            try:
                s = socket.socket()
                s.settimeout(1.0)
                s.connect(("172.16.42.1", 2323))
                s.sendall(b"uptime; ps aux | grep -E 'phoc|phosh'\n")
                res = s.recv(4096).decode(errors="replace")
                s.close()
                print("\n" + "=" * 65)
                print("🎉🎉🎉 ¡TABLET INICIADA Y CONECTADA EXITOSAMENTE! 🎉🎉🎉")
                print("=" * 65)
                print(res)
                sys.exit(0)
            except Exception:
                time.sleep(0.5)
    time.sleep(0.5)
