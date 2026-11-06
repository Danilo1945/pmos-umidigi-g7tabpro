#!/usr/bin/env python3
# ==============================================================================
# Script Maestro de Restauración y Sincronización Rápida a Tablet en Vivo
# UMIDIGI G7 Tab Pro (MT6789) - Vía Socket USB Gadget (172.16.42.1:2323 / 23)
# ==============================================================================
import os
import sys
import time
import socket
import base64

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

def run_remote(cmd, ip='172.16.42.1', timeout=20.0):
    for port in (2323, 23):
        s = socket.socket()
        s.settimeout(timeout)
        token = b'__CMD_FINISHED_MARKER__'
        try:
            s.connect((ip, port))
            if port == 23:
                time.sleep(0.2)
                try:
                    s.recv(1024)
                except Exception:
                    pass
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
            res = out.replace(token + b'\r\n', b'').replace(token + b'\n', b'').replace(token, b'')
            return res.decode('utf-8', errors='replace')
        except Exception as e:
            last_err = e
        finally:
            s.close()
    return f"ERROR: {last_err}\n"

def send_file(local_path, remote_path, mode="755"):
    print(f"[+] Enviando {os.path.basename(local_path)} -> {remote_path}...")
    with open(local_path, "rb") as f:
        raw_bytes = f.read()

    remote_dir = os.path.dirname(remote_path)
    run_remote(f"mkdir -p {remote_dir} && rm -f {remote_path}.b64")

    b64_data = base64.b64encode(raw_bytes).decode('ascii')
    chunk_size = 32768
    for i in range(0, len(b64_data), chunk_size):
        chunk = b64_data[i:i+chunk_size]
        run_remote(f"printf '%s' '{chunk}' >> {remote_path}.b64")

    res = run_remote(
        f"base64 -d {remote_path}.b64 > {remote_path}.tmp && "
        f"mv -f {remote_path}.tmp {remote_path} && "
        f"rm -f {remote_path}.b64 && "
        f"chmod {mode} {remote_path}"
    )
    if "ERROR" in res:
        print(f"    [-] Error enviando {local_path}: {res}")
    else:
        print(f"    [OK] Instalado {remote_path} ({len(raw_bytes):,} bytes)")

def main():
    print("==========================================================")
    print("  Sincronizando Todas las Piezas a UMIDIGI G7 Tab Pro     ")
    print("==========================================================")

    res = run_remote("uptime")
    if "ERROR" in res:
        print(f"[-] No se pudo conectar a la tablet en 172.16.42.1: {res}")
        sys.exit(1)
    print(f"[+] Conexión establecida con éxito: {res.strip()}")

    # 1. Subsistema 06: Aceleración Gráfica GPU Mali-G57
    print("\n--- [1/5] Sincronizando Driver de Interfaz GPU Mali-G57 ---")
    gpu_dir = os.path.join(ROOT_DIR, "subsistemas", "06_aceleracion_grafica_gpu")
    send_file(os.path.join(gpu_dir, "libgpu_interface_driver.so"), "/sysroot/usr/lib/libgpu_interface_driver.so")
    run_remote("cp -f /sysroot/usr/lib/libgpu_interface_driver.so /usr/lib/libgpu_interface_driver.so 2>/dev/null || true")
    send_file(os.path.join(gpu_dir, "load_mali_chain.sh"), "/sysroot/usr/bin/load_mali_chain.sh")
    run_remote("cp -f /sysroot/usr/bin/load_mali_chain.sh /usr/bin/load_mali_chain.sh 2>/dev/null || true")

    # 2. Subsistema 01: Audio (PipeWire + 4x AW87359)
    print("\n--- [2/5] Sincronizando Subsistema de Audio ---")
    audio_dir = os.path.join(ROOT_DIR, "subsistemas", "01_audio")
    run_remote("rm -f /sysroot/etc/pipewire/pipewire.conf.d/00-disable-portal.conf /sysroot/etc/pipewire/pipewire.conf.d/10-null.conf 2>/dev/null || true")
    send_file(os.path.join(audio_dir, "tablet_audio_daemon.py"), "/sysroot/usr/bin/tablet_audio_daemon.py")
    run_remote("cp -f /sysroot/usr/bin/tablet_audio_daemon.py /sysroot/usr/local/bin/tablet_audio_daemon.py 2>/dev/null || true")
    run_remote("cp -f /sysroot/usr/bin/tablet_audio_daemon.py /usr/bin/tablet_audio_daemon.py 2>/dev/null || true")
    send_file(os.path.join(audio_dir, "10-alsa.conf"), "/sysroot/etc/pipewire/pipewire.conf.d/10-alsa.conf", mode="644")
    send_file(os.path.join(audio_dir, "50-alsa-fix.conf"), "/sysroot/etc/wireplumber/wireplumber.conf.d/50-alsa-fix.conf", mode="644")

    # 3. Subsistema 02: Batería y UPower
    print("\n--- [3/5] Sincronizando Subsistema de Batería ---")
    bat_dir = os.path.join(ROOT_DIR, "subsistemas", "02_bateria_energia")
    send_file(os.path.join(bat_dir, "upower_battery_provider.py"), "/sysroot/usr/bin/upower_battery_provider.py")
    send_file(os.path.join(bat_dir, "tablet_battery_daemon.py"), "/sysroot/usr/bin/tablet_battery_daemon.py")

    # 4. Subsistema 03: Pantalla, Touch, Power Daemon y Scripts de Phosh
    print("\n--- [4/5] Sincronizando Pantalla, Touch, Power Daemon y Phosh ---")
    disp_dir = os.path.join(ROOT_DIR, "subsistemas", "03_pantalla_rotacion_touch")
    send_file(os.path.join(disp_dir, "libseat_direct.so"), "/sysroot/usr/lib/libseat_direct.so")
    send_file(os.path.join(disp_dir, "phoc.ini"), "/sysroot/etc/phoc.ini", mode="644")
    send_file(os.path.join(disp_dir, "tablet_power_daemon.py"), "/sysroot/usr/bin/tablet_power_daemon.py")
    send_file(os.path.join(disp_dir, "phosh_wrapper.sh"), "/sysroot/usr/bin/phosh_wrapper.sh")
    send_file(os.path.join(disp_dir, "start_phosh.sh"), "/sysroot/usr/bin/start_phosh.sh")
    send_file(os.path.join(disp_dir, "start_phosh_service.sh"), "/sysroot/usr/bin/start_phosh_service.sh")
    run_remote(
        "cp -f /sysroot/etc/phoc.ini /etc/phoc.ini 2>/dev/null || true; "
        "cp -f /sysroot/usr/bin/phosh_wrapper.sh /usr/bin/phosh_wrapper.sh 2>/dev/null || true; "
        "cp -f /sysroot/usr/bin/start_phosh.sh /usr/bin/start_phosh.sh 2>/dev/null || true; "
        "cp -f /sysroot/usr/bin/start_phosh.sh /bin/start_phosh.sh 2>/dev/null || true; "
        "cp -f /sysroot/usr/bin/start_phosh_service.sh /bin/start_phosh_service.sh 2>/dev/null || true"
    )

    # 5. Subsistema 04: Wi-Fi WMT
    print("\n--- [5/5] Sincronizando Wi-Fi WMT ---")
    wifi_dir = os.path.join(ROOT_DIR, "subsistemas", "04_wifi_conectividad")
    send_file(os.path.join(wifi_dir, "wmt_wifi_start.sh"), "/sysroot/usr/bin/wmt_wifi_start.sh")
    run_remote("cp -f /sysroot/usr/bin/wmt_wifi_start.sh /bin/wmt_wifi_start.sh 2>/dev/null || true")

    run_remote("sync")
    print("\n==========================================================")
    print(" 🎉 ¡SINCRONIZACIÓN COMPLETA EN LA TABLET FINALIZADA!     ")
    print("==========================================================")

if __name__ == "__main__":
    main()
