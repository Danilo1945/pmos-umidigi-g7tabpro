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

def run_remote(cmd, ip='172.16.42.1', port=2323, timeout=None):
    import time
    if timeout is None:
        timeout = float(os.environ.get("TABLET_TIMEOUT", 10.0))
    token = b'__CMD_FINISHED_MARKER__'
    for p in [port, 23]:
        try:
            s = socket.socket()
            s.settimeout(timeout)
            s.connect((ip, p))
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
            import re
            ansi_regex = re.compile(r'\x1b\[[0-9;?]*[a-zA-Z]')
            lines = []
            for l in out.decode('utf-8', errors='replace').splitlines():
                cleaned = ansi_regex.sub('', l)
                if token.decode() not in cleaned and not cleaned.startswith('export PAGER=cat') and not cleaned.startswith('(') and not cleaned.endswith(f'echo {token.decode()}') and not cleaned.startswith('~ #'):
                    lines.append(cleaned)
            return '\n'.join(lines).strip() + '\n'
        except Exception:
            continue
    return "ERROR: Connection failed to TCP ports\n"

if __name__ == '__main__':
    if len(sys.argv) > 1:
        command = " ".join(sys.argv[1:])
    else:
        command = sys.stdin.read()
    output = run_remote(command)
    sys.stdout.write(output)
