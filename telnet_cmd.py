#!/usr/bin/env python3
import socket
import sys

def telnet_cmd(cmds):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(6)
    s.connect(('172.16.42.1', 23))
    
    buf = b''
    while True:
        try:
            chunk = s.recv(1024)
            if not chunk: break
            buf += chunk
            if buf.endswith(b'# ') or buf.endswith(b'$ '): break
        except socket.timeout:
            break
            
    results = []
    for cmd in cmds:
        s.sendall(cmd.encode() + b'\n')
        buf = b''
        while True:
            try:
                chunk = s.recv(4096)
                if not chunk: break
                buf += chunk
                if buf.endswith(b'# ') or buf.endswith(b'$ '): break
            except socket.timeout:
                break
        results.append(buf.decode(errors='replace'))
    s.close()
    return results

if __name__ == '__main__':
    commands = sys.argv[1:]
    res = telnet_cmd(commands)
    for r in res:
        print(r)
