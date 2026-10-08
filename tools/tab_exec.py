import socket
import time
import sys

def run_cmd(cmd_str, timeout=10):
    for port in [23, 2323]:
        try:
            s = socket.socket()
            s.settimeout(timeout)
            s.connect(('172.16.42.1', port))
            time.sleep(0.3)
            s.sendall(cmd_str.encode('utf-8') + b'\necho ---DONE---\n')
            resp = b''
            t0 = time.time()
            while time.time() - t0 < timeout:
                try:
                    chunk = s.recv(4096)
                    if not chunk:
                        break
                    resp += chunk
                    if b'---DONE---' in resp:
                        break
                except socket.timeout:
                    break
            s.close()
            if resp:
                return resp.decode('utf-8', errors='ignore')
        except Exception:
            continue
    return "ERROR: Connection failed on ports 23 and 2323"

if __name__ == '__main__':
    if len(sys.argv) > 1:
        cmd = ' '.join(sys.argv[1:])
    else:
        cmd = 'uptime'
    print(run_cmd(cmd))
