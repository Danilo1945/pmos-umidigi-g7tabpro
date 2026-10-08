#!/usr/bin/env python3
import os
import sys
import time
import socket
import urllib.request

V15_DIR = "/home/danilo/Documentos/TABLET/version_v15_estable"
TOOLS_DIR = "/home/danilo/Documentos/TABLET/FUNCIONALIDADES_FINALES/tools"
sys.path.insert(0, TOOLS_DIR)
import tablet_cmd

print("[*] Iniciando generación de respaldo de configuraciones y usuarios de sysroot...")

# Empaquetar configuraciones en la tablet
tar_cmd = (
    "tar -czf /tmp/backup_v15.tar.gz -C /sysroot "
    "etc root home usr/local "
    "usr/bin/tablet_power_daemon.py "
    "usr/bin/tablet_audio_daemon.py "
    "usr/bin/tablet_battery_daemon.py "
    "usr/bin/upower_battery_provider.py "
    "usr/lib/libseat_direct.so 2>/dev/null && "
    "ls -lh /tmp/backup_v15.tar.gz"
)

res = tablet_cmd.run_remote(tar_cmd, timeout=30)
print(f"[+] Archivo generado en la tablet:\n{res.strip()}")

# Descargar usando nc o levantando un netcat listener
out_path = os.path.join(V15_DIR, "sysroot_v15_config_backup.tar.gz")

# Usar base64 o netcat directo sobre puerto 2324
print("[*] Descargando respaldo hacia la laptop...")
s_listen = socket.socket()
s_listen.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s_listen.bind(("0.0.0.0", 8092))
s_listen.listen(1)

# Enviar comando a la tablet para mandar el archivo por nc
stream_cmd = "nc 172.16.42.2 8092 < /tmp/backup_v15.tar.gz && rm -f /tmp/backup_v15.tar.gz"
import threading
t = threading.Thread(target=lambda: tablet_cmd.run_remote(stream_cmd, timeout=30), daemon=True)
t.start()

conn, addr = s_listen.accept()
with open(out_path, "wb") as f:
    while chunk := conn.recv(65536):
        f.write(chunk)
conn.close()
s_listen.close()

size_mb = os.path.getsize(out_path) / (1024 * 1024)
print(f"🎉 ¡Respaldo guardado exitosamente en: {out_path} ({size_mb:.2f} MB)!")
