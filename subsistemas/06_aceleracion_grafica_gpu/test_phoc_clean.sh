#!/bin/sh
set -e
export HOME=/root
export USER=root
export XDG_RUNTIME_DIR=/tmp/runtime-root
mkdir -p $XDG_RUNTIME_DIR
chmod 700 $XDG_RUNTIME_DIR
rm -f $XDG_RUNTIME_DIR/wayland-*

export WLR_RENDERER=gles2
export WLR_RENDERER_ALLOW_SOFTWARE=0
export WLR_BACKENDS=drm,libinput
export WLR_DRM_DEVICES=/dev/dri/card0
export WLR_LIBINPUT_NO_DEVICES=1
export WLR_NO_HARDWARE_CURSORS=1
export WLR_DRM_NO_MODIFIERS=1
export WLR_DRM_NO_ATOMIC=1

export PHOC_PRELOAD="/usr/lib/libjemalloc.so.2 /usr/lib/libstdc++.so.6 /usr/lib/libseat_direct.so /usr/lib/libgpu_interface_driver.so /usr/lib/libmali-bifrost-patched.so"

echo "[clean_test] Launching phoc..."
env LD_PRELOAD="$PHOC_PRELOAD" /usr/bin/phoc -v -S -C /etc/phoc.ini > /var/log/phoc_clean.log 2>&1 &
PHOC_PID=$!
echo "[clean_test] phoc PID=$PHOC_PID"

cnt=0
while [ ! -S /tmp/runtime-root/wayland-0 ] && [ $cnt -lt 40 ]; do
    sleep 0.1
    cnt=$((cnt + 1))
done

if [ -S /tmp/runtime-root/wayland-0 ]; then
    echo "[clean_test] SUCCESS: wayland-0 socket is ACTIVE! (${cnt}00 ms)"
    ls -l /tmp/runtime-root/wayland-0
else
    echo "[clean_test] ERROR: wayland-0 socket did NOT appear!"
fi

echo "[clean_test] Checking phoc process:"
ps | grep -E "phoc" | grep -v grep || true

echo "[clean_test] Checking phoc log head:"
head -n 25 /var/log/phoc_clean.log 2>/dev/null || true

echo "[clean_test] Checking phoc log tail:"
tail -n 25 /var/log/phoc_clean.log 2>/dev/null || true
