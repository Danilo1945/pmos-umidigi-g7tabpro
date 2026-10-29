import socket
import select
import sys

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
target = ("172.16.42.2", 9999)

with open("/dev/kmsg", "rb", buffering=0) as f:
    while True:
        try:
            line = f.readline()
            if line:
                s.sendto(line, target)
        except Exception:
            pass
