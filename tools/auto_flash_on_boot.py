#!/usr/bin/env python3
import socket
import subprocess
import time
import sys

print("[*] Iniciando centinela de flasheo automático...")
print("[*] Esperando a que la tablet inicie el kernel y aparezca en red...")

while True:
    # 1. Detectar dispositivo Gadget USB o interfaz
    links = subprocess.run("ip -br link", shell=True, capture_output=True, text=True).stdout
    iface = None
    for line in links.splitlines():
        if line.startswith("enx") or "usb" in line:
            iface = line.split()[0]
            break
    
    if iface:
        # Configurar IP si no la tiene
        subprocess.run(f"sudo ip addr add 172.16.42.2/24 dev {iface} 2>/dev/null || true", shell=True)
        subprocess.run(f"sudo ip link set {iface} up 2>/dev/null || true", shell=True)
        
        # Probar conexión al puerto 2323
        try:
            s = socket.socket()
            s.settimeout(1.0)
            s.connect(("172.16.42.1", 2323))
            print("🎉 ¡CONECTADO A LA TABLET EN PUERTO 2323!")
            
            # Enviar comando de descarga y flasheo ultra-rápido
            cmd = (
                "wget -O /tmp/boot_v13.img http://172.16.42.2:8081/pmos_boot_v13.img && "
                "dd if=/tmp/boot_v13.img of=/dev/sdc28 bs=4M conv=fsync && "
                "dd if=/tmp/boot_v13.img of=/dev/sdc51 bs=4M conv=fsync && "
                "sync && echo __FLASH_OK__\n"
            )
            print("[*] Enviando comando de flasheo en caliente...")
            s.sendall(cmd.encode("utf-8"))
            
            buf = b""
            start_t = time.time()
            while time.time() - start_t < 15:
                chunk = s.recv(4096)
                if not chunk:
                    break
                buf += chunk
                print(chunk.decode(errors="replace"), end="", flush=True)
                if b"__FLASH_OK__" in buf:
                    print("\n\n🎉 ¡IMAGEN PERMANENTE FLASHEADA EXITOSAMENTE EN AMBOS SLOTS (sdc28 y sdc51)!")
                    print("[*] Reiniciando la tablet para boot 100% definitivo...")
                    s.sendall(b"reboot -f\n")
                    s.close()
                    sys.exit(0)
            s.close()
        except Exception:
            pass
    time.sleep(0.5)
