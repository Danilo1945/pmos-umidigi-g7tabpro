#!/usr/bin/env python3
import os
import sys
import glob
import select
import struct
import subprocess

EVENT_FORMAT = 'llHHI'
EVENT_SIZE = struct.calcsize(EVENT_FORMAT)
EV_KEY = 1
KEY_VOLUMEDOWN = 114
KEY_VOLUMEUP = 115

def get_input_fds():
    fds = {}
    for ev in glob.glob('/dev/input/event*'):
        try:
            fd = os.open(ev, os.O_RDONLY | os.O_NONBLOCK)
            fds[fd] = ev
        except Exception:
            pass
    return fds

def main():
    print("[+] volume_keys_daemon iniciado...")
    fds = get_input_fds()
    while True:
        if not fds:
            fds = get_input_fds()
        try:
            r, _, _ = select.select(list(fds.keys()), [], [], 2.0)
        except Exception:
            fds = get_input_fds()
            continue

        for fd in r:
            try:
                data = os.read(fd, EVENT_SIZE)
                if len(data) < EVENT_SIZE:
                    continue
                sec, usec, etype, code, val = struct.unpack(EVENT_FORMAT, data)
                if etype == EV_KEY and val == 1:
                    if code == KEY_VOLUMEUP:
                        subprocess.run("su - danilo -c 'XDG_RUNTIME_DIR=/tmp/runtime-danilo wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%+' >/dev/null 2>&1 || amixer set Master 5%+ >/dev/null 2>&1", shell=True)
                    elif code == KEY_VOLUMEDOWN:
                        subprocess.run("su - danilo -c 'XDG_RUNTIME_DIR=/tmp/runtime-danilo wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%-' >/dev/null 2>&1 || amixer set Master 5%- >/dev/null 2>&1", shell=True)
            except Exception:
                try:
                    os.close(fd)
                except Exception:
                    pass
                fds.pop(fd, None)

if __name__ == "__main__":
    main()
