#!/usr/bin/env python3
"""
restaurar_tablet_v15.py - Restaurador Maestro Deterministico v15.0 Estable
Restaura tanto las imágenes de arranque (boot_a/b, vendor_boot_a/b)
como el respaldo completo de configuraciones de sysroot (usuarios, etc, daemons, dconf).
"""
import os
import sys
import time
import subprocess

V15_DIR = "/home/danilo/Documentos/TABLET/version_v15_estable"
TOOLS_DIR = "/home/danilo/Documentos/TABLET/FUNCIONALIDADES_FINALES/tools"
sys.path.insert(0, TOOLS_DIR)
import tablet_cmd

print("==========================================================")
print("  RESTAURADOR MAESTRO postmarketOS v15.0 ESTABLE          ")
print("==========================================================")

# Paso 1: Flashear boot_a/b y vendor_boot_a/b
print("\n[Paso 1/2] Verificando y flasheando particiones de arranque...")
subprocess.run([sys.executable, os.path.join(V15_DIR, "flash_live_v15.py")], check=True)

# Paso 2: Restaurar configuraciones de sysroot
backup_tar = os.path.join(V15_DIR, "sysroot_v15_config_backup.tar.gz")
if os.path.exists(backup_tar):
    print("\n[Paso 2/2] Restaurando configuraciones de sysroot (/etc, /root, /home, daemons)...")
    import threading
    from http.server import SimpleHTTPRequestHandler, HTTPServer
    
    PORT = 8093
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

    restore_cmd = (
        f"wget -q -O /tmp/restore_v15.tar.gz http://172.16.42.2:{PORT}/sysroot_v15_config_backup.tar.gz && "
        f"tar -xzf /tmp/restore_v15.tar.gz -C /sysroot && "
        f"rm -f /tmp/restore_v15.tar.gz && sync && echo RESTORE_OK"
    )
    res = tablet_cmd.run_remote(restore_cmd, timeout=40)
    print(f"[+] Resultado restauración sysroot: {res.strip()}")
else:
    print("[-] No se encontró sysroot_v15_config_backup.tar.gz")

print("\n🎉 ¡RESTAURACIÓN V15.0 COMPLETADA!")
print("Para aplicar cualquier cambio fresco, puedes reiniciar con:")
print("  python3 tools/tablet_cmd.py 'sync; reboot -f'")
