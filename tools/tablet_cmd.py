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
    tok = f"__DONE_{int(time.time()*1000)}__"
    marker1 = f"\n__MARK_{tok}".encode('utf-8')
    marker2 = f"\r\n__MARK_{tok}".encode('utf-8')
    for p in [port, 23]:
        try:
            s = socket.create_connection((ip, p), timeout=3.0)
            s.settimeout(timeout)
            wrapped_cmd = f"export PAGER=cat; ({cmd}); echo __MARK_{tok}\nexit\n"
            s.sendall(wrapped_cmd.encode('utf-8'))
            out = b''
            start = time.time()
            while time.time() - start < timeout:
                try:
                    chunk = s.recv(8192)
                    if not chunk:
                        break
                    out += chunk
                    if marker1 in out or marker2 in out:
                        break
                except socket.timeout:
                    break
            s.close()
            import re
            ansi_regex = re.compile(r'\x1b\[[0-9;?]*[a-zA-Z]')
            lines = []
            for l in out.decode('utf-8', errors='replace').splitlines():
                cleaned = ansi_regex.sub('', l).rstrip('\r')
                if tok not in cleaned and not cleaned.startswith('export PAGER=cat') and not cleaned.startswith('~ #') and not cleaned.startswith('/ #'):
                    lines.append(cleaned)
            return '\n'.join(lines).strip() + '\n'
        except Exception:
            continue
    return "ERROR: Connection failed to TCP ports\n"

def push_file(local_path, remote_path):
    import base64
    with open(local_path, 'rb') as f:
        data = f.read()
    b64 = base64.b64encode(data).decode('ascii')
    lines = [f"rm -f {remote_path}.b64"]
    chunk_size = 768
    for i in range(0, len(b64), chunk_size):
        chunk = b64[i:i+chunk_size]
        lines.append(f"printf '%s' '{chunk}' >> {remote_path}.b64")
    lines.append(f"base64 -d {remote_path}.b64 > {remote_path}.tmp && mv -f {remote_path}.tmp {remote_path} && rm -f {remote_path}.b64 && md5sum {remote_path}")
    return run_remote(" && ".join(lines), timeout=30.0)

def pull_file(remote_path, local_path):
    import base64
    b64 = run_remote(f"base64 {remote_path}", timeout=30.0)
    clean_b64 = "".join(l.strip() for l in b64.splitlines() if not l.startswith("base64 ") and "->" not in l)
    data = base64.b64decode(clean_b64)
    with open(local_path, 'wb') as f:
        f.write(data)
    return f"Pulled {len(data)} bytes to {local_path}\n"

if __name__ == '__main__':
    if len(sys.argv) >= 4 and sys.argv[1] == 'push':
        sys.stdout.write(push_file(sys.argv[2], sys.argv[3]))
    elif len(sys.argv) >= 4 and sys.argv[1] == 'pull':
        sys.stdout.write(pull_file(sys.argv[2], sys.argv[3]))
    else:
        if len(sys.argv) > 1:
            command = " ".join(sys.argv[1:])
        else:
            command = sys.stdin.read()
        output = run_remote(command)
        sys.stdout.write(output)
