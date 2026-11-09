#!/usr/bin/env python3
"""
Puente HCI H4 Userspace para Bluetooth MediaTek ConnSys (MT6631 / MT6789)
Conecta /dev/stpbt (c 192 0, bt_drv_connac1x.ko) con un par PTY + btattach (N_HCI H4)
y levanta automáticamente hci0 + bluetoothd (BlueZ) para Phosh / GNOME Mobile.
"""
import os
import pty
import select
import signal
import subprocess
import sys
import threading
import time
import tty

STPBT_DEV = "/dev/stpbt"


def ensure_stpbt_node():
    if not os.path.exists(STPBT_DEV):
        try:
            os.mknod(STPBT_DEV, 0o666 | 0o020000, os.makedev(192, 0))
        except Exception:
            pass


def bring_up_bluetooth():
    """Espera a que aparezca hci0, desbloquea rfkill, levanta hci0 e inicia bluetoothd."""
    time.sleep(1.5)
    subprocess.run(["/usr/sbin/rfkill", "unblock", "bluetooth"], stderr=subprocess.DEVNULL)
    for _ in range(20):
        res = subprocess.run(
            ["/usr/bin/hciconfig", "hci0"],
            capture_output=True,
            text=True,
        )
        if res.returncode == 0 and "hci0" in res.stdout:
            break
        time.sleep(0.3)

    # Iniciar bluetoothd si no está corriendo
    btd_check = subprocess.run(["pgrep", "-x", "bluetoothd"], capture_output=True)
    if btd_check.returncode != 0:
        for btd_path in ["/usr/libexec/bluetooth/bluetoothd", "/usr/lib/bluetooth/bluetoothd", "/usr/sbin/bluetoothd"]:
            if os.path.exists(btd_path):
                subprocess.Popen(
                    [btd_path, "-n"],
                    stdout=open("/var/log/bluetoothd.log", "a"),
                    stderr=subprocess.STDOUT,
                )
                break
        time.sleep(0.8)

    subprocess.run(["/usr/bin/hciconfig", "hci0", "up"], stderr=subprocess.DEVNULL)
    subprocess.run(["/usr/bin/bluetoothctl", "power", "on"], stderr=subprocess.DEVNULL)
    print("[bt-daemon] Interfaz hci0 activa y bluetoothd en ejecución.", flush=True)


def run_bridge():
    ensure_stpbt_node()
    while True:
        stp_fd = None
        master_fd = None
        slave_fd = None
        btattach_proc = None
        try:
            stp_fd = os.open(STPBT_DEV, os.O_RDWR | os.O_NONBLOCK)
            master_fd, slave_fd = pty.openpty()
            tty.setraw(master_fd)
            tty.setraw(slave_fd)
            slave_name = os.ttyname(slave_fd)
            print(f"[bt-daemon] /dev/stpbt abierto (fd={stp_fd}) <-> PTY {slave_name}", flush=True)

            btattach_proc = subprocess.Popen(
                ["/usr/bin/btattach", "-B", slave_name, "-P", "h4"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )

            threading.Thread(target=bring_up_bluetooth, daemon=True).start()

            tx_buf = bytearray()
            while btattach_proc.poll() is None:
                r, _, _ = select.select([stp_fd, master_fd], [], [], 0.5)
                if stp_fd in r:
                    try:
                        data = os.read(stp_fd, 4096)
                        if data:
                            # Fix MT6631/ConnSys HCI_Read_Flow_Control_Mode (0x100c) & HCI_Read_Data_Block_Size (0x0c77)
                            if data.startswith(b"\x04\x0f\x04\x01\x01\x77\x0c"):
                                data = b"\x04\x0e\x0a\x01\x77\x0c\x00\xfd\x03\xfd\x03\x06\x00"
                            elif data.startswith(b"\x04\x0e\x06\x01\x0c\x10\x00"):
                                data = b"\x04\x0e\x05\x01\x0c\x10\x00\x00"
                            os.write(master_fd, data)
                    except BlockingIOError:
                        pass
                if master_fd in r:
                    try:
                        data = os.read(master_fd, 4096)
                        if data:
                            tx_buf.extend(data)
                            while len(tx_buf) >= 4:
                                ptype = tx_buf[0]
                                if ptype == 1:
                                    plen = 4 + tx_buf[3]
                                elif ptype == 2:
                                    if len(tx_buf) < 5:
                                        break
                                    plen = 5 + (tx_buf[3] | (tx_buf[4] << 8))
                                elif ptype == 3:
                                    plen = 4 + tx_buf[3]
                                else:
                                    tx_buf.pop(0)
                                    continue
                                if len(tx_buf) < plen:
                                    break
                                pkt = bytes(tx_buf[:plen])
                                del tx_buf[:plen]
                                os.write(stp_fd, pkt)
                    except BlockingIOError:
                        pass
        except Exception as e:
            print(f"[bt-daemon] Error en puente H4: {e}. Reintentando en 3s...", flush=True)
        finally:
            if btattach_proc and btattach_proc.poll() is None:
                btattach_proc.terminate()
            for fd in (master_fd, slave_fd, stp_fd):
                if fd is not None:
                    try:
                        os.close(fd)
                    except Exception:
                        pass
        time.sleep(3)


if __name__ == "__main__":
    signal.signal(signal.SIGTERM, lambda s, f: sys.exit(0))
    run_bridge()
