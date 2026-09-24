#!/usr/bin/env python3
# ==============================================================================
# Script de Restauración Rápida a Tablet en Vivo
# UMIDIGI G7 Tab Pro (MT6789) - Vía Socket USB Gadget (172.16.42.1:2323)
# ==============================================================================
import os
import sys
import socket

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

def run_remote(cmd, ip='172.16.42.1', port=2323, timeout=15.0):
    s = socket.socket()
    s.settimeout(timeout)
    token = b'__CMD_FINISHED_MARKER__'
    try:
        s.connect((ip, port))
        wrapped_cmd = f"({cmd})\necho {token.decode()}\n"
        s.sendall(wrapped_cmd.encode('utf-8'))
        out = b''
        while True:
            chunk = s.recv(4096)
            if not chunk:
                break
            out += chunk
            if token in out:
                break
            # En caso de que se demore
        res = out.replace(token + b'\n', b'').replace(token, b'')
        return res.decode('utf-8', errors='replace')
    except Exception as e:
        return f"ERROR: {e}\n"
    finally:
        s.close()

def send_file_content(local_path, remote_path):
    print(f"[+] Enviando {os.path.basename(local_path)} -> {remote_path}...")
    with open(local_path, "r", encoding="utf-8", errors="ignore") as f:
        content = f.read()
    
    # Crear carpeta remota
    remote_dir = os.path.dirname(remote_path)
    run_remote(f"mkdir -p {remote_dir}")
    
    # Escribir mediante heredoc en base64 para evitar caracteres conflictivos
    import base64
    b64_data = base64.b64encode(content.encode('utf-8')).decode('ascii')
    cmd = f"echo '{b64_data}' | base64 -d > {remote_path} && chmod +x {remote_path} 2>/dev/null || true"
    res = run_remote(cmd)
    if "ERROR" in res:
        print(f"    [-] Error enviando {local_path}: {res}")
    else:
        print(f"    [OK] Instalado {remote_path}")

def main():
    print("==========================================================")
    print("  Restaurando Funcionalidades a la Tablet UMIDIGI G7 Pro  ")
    print("==========================================================")
    
    # 1. Comprobar conectividad
    res = run_remote("uptime")
    if "ERROR" in res:
        print(f"[-] No se pudo conectar a la tablet en 172.16.42.1:2323: {res}")
        sys.exit(1)
    print(f"[+] Conexión establecida con éxito: {res.strip()}")
    
    # 2. Restaurar Subsistema de Audio
    print("\n--- [1/4] Restaurando Subsistema de Audio ---")
    audio_dir = os.path.join(ROOT_DIR, "subsistemas", "01_audio")
    send_file_content(os.path.join(audio_dir, "tablet_audio_daemon.py"), "/usr/local/bin/tablet_audio_daemon.py")
    send_file_content(os.path.join(audio_dir, "tablet_audio_daemon.py"), "/usr/bin/tablet_audio_daemon.py")
    send_file_content(os.path.join(audio_dir, "10-alsa.conf"), "/etc/pipewire/pipewire.conf.d/10-alsa.conf")
    
    # Reiniciar daemon de audio en la tablet
    run_remote("killall -9 tablet_audio_daemon.py 2>/dev/null; /usr/local/bin/tablet_audio_daemon.py &")
    print("    [OK] tablet_audio_daemon reiniciado y calibrado.")
    
    # 3. Restaurar Subsistema de Batería
    print("\n--- [2/4] Restaurando Subsistema de Batería ---")
    bat_dir = os.path.join(ROOT_DIR, "subsistemas", "02_bateria_energia")
    send_file_content(os.path.join(bat_dir, "tablet_battery_daemon.py"), "/usr/bin/tablet_battery_daemon.py")
    run_remote("killall -9 tablet_battery_daemon.py 2>/dev/null; /usr/bin/tablet_battery_daemon.py &")
    print("    [OK] tablet_battery_daemon reiniciado.")
    
    # 4. Restaurar Subsistema de Pantalla y Phosh
    print("\n--- [3/4] Restaurando Pantalla y Perfil Phoc ---")
    disp_dir = os.path.join(ROOT_DIR, "subsistemas", "03_pantalla_rotacion_touch")
    send_file_content(os.path.join(disp_dir, "phoc.ini"), "/etc/phoc.ini")
    
    # 5. Restaurar Subsistema de Wi-Fi
    print("\n--- [4/4] Restaurando Wi-Fi WMT ---")
    wifi_dir = os.path.join(ROOT_DIR, "subsistemas", "04_wifi_conectividad")
    send_file_content(os.path.join(wifi_dir, "wmt_wifi_start.sh"), "/usr/bin/wmt_wifi_start.sh")
    
    print("\n==========================================================")
    print(" 🎉 ¡RESTAURACIÓN FINALIZADA Y VALIDADA EN LA TABLET! ")
    print("==========================================================")

if __name__ == "__main__":
    main()
