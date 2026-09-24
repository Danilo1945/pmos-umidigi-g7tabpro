#!/usr/bin/env python3
import sys
import socket

def run_remote(cmd, ip='172.16.42.1', port=2323, timeout=10.0):
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
        res = out.replace(token + b'\n', b'').replace(token, b'')
        return res.decode('utf-8', errors='replace')
    except Exception as e:
        return f"ERROR: {e}\n"
    finally:
        s.close()

if __name__ == '__main__':
    if len(sys.argv) > 1:
        command = " ".join(sys.argv[1:])
    else:
        command = sys.stdin.read()
    output = run_remote(command)
    sys.stdout.write(output)
