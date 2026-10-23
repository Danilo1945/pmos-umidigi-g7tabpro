#!/usr/bin/env python3
import sys
import socket
import os

def run_remote_serial(cmd, tty_path='/dev/ttyACM0', timeout=4.0):
    import os, time
    token = '__CMD_FINISHED_MARKER__'
    try:
        fd = os.open(tty_path, os.O_RDWR | os.O_NONBLOCK)
        wrapped = f"\nexport PAGER=cat; ({cmd})\necho {token}\n".encode('utf-8')
        os.write(fd, wrapped)
        out = b''
        start = time.time()
        while time.time() - start < timeout:
            try:
                chunk = os.read(fd, 4096)
                if chunk:
                    out += chunk
                    if token.encode() in out:
                        break
            except BlockingIOError:
                pass
            except Exception:
                pass
            time.sleep(0.05)
        os.close(fd)
        text = out.decode('utf-8', errors='replace')
        lines = [l for l in text.splitlines() if token not in l and not l.startswith('export PAGER=cat') and not l.startswith('(') and not l.endswith(f'echo {token}') and not l.startswith('~ #')]
        return '\n'.join(lines).strip() + '\n'
    except Exception as e:
        return f"ERROR SERIAL: {e}\n"

def run_remote(cmd, ip='172.16.42.1', port=23, timeout=8.0):
    import time
    token = b'__CMD_FINISHED_MARKER__'
    try:
        s = socket.socket()
        s.settimeout(timeout)
        s.connect((ip, port))
        time.sleep(0.1)
        try:
            s.recv(1024)
        except Exception:
            pass
        wrapped_cmd = f"\nexport PAGER=cat; ({cmd})\necho {token.decode()}\n"
        s.sendall(wrapped_cmd.encode('utf-8'))
        out = b''
        start = time.time()
        while time.time() - start < timeout:
            try:
                chunk = s.recv(4096)
                if not chunk:
                    break
                out += chunk
                if token in out:
                    break
            except socket.timeout:
                break
        s.close()
        text = out.decode('utf-8', errors='replace')
        lines = [l for l in text.splitlines() if token.decode() not in l and not l.startswith('export PAGER=cat') and not l.startswith('(') and not l.endswith(f'echo {token.decode()}') and not l.startswith('~ #') and not '\x1b[' in l]
        return '\n'.join(lines).strip() + '\n'
    except Exception:
        # Fallback a consola serie USB /dev/ttyACM0
        if os.path.exists('/dev/ttyACM0'):
            return run_remote_serial(cmd)
        return "ERROR: Connection failed (both TCP and Serial unavailable)\n"

if __name__ == '__main__':
    if len(sys.argv) > 1:
        command = " ".join(sys.argv[1:])
    else:
        command = sys.stdin.read()
    output = run_remote(command)
    sys.stdout.write(output)
