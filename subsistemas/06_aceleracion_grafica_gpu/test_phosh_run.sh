#!/bin/sh
set -e

echo "=== PREPARING ENVIRONMENT ==="
# 1. Watchdog & Backlight
if ! pgrep -f wdt_pet >/dev/null 2>&1; then
    /bin/wdt_pet >/dev/null 2>&1 &
fi
echo 15 > /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true

# 2. Cleanup old sockets & master
rm -f /sysroot/tmp/runtime-root/wayland-* 2>/dev/null || true
mkdir -p /sysroot/tmp/runtime-root /tmp/runtime-root
chmod 700 /sysroot/tmp/runtime-root /tmp/runtime-root

# 3. Phoc config
cat << 'EOF' > /sysroot/etc/phoc.ini
[core]
xwayland=false

[output:DSI-1]
enable=true
scale=1.0
rotate=180

[output:Writeback-1]
enable=false

[output:Writeback-2]
enable=false
EOF

# 4. Launch in chroot
echo "=== LAUNCHING PHOC + PHOSH ==="
chroot /sysroot sh -c '
  eval $(dbus-launch --sh-syntax)
  export HOME=/root USER=root XDG_RUNTIME_DIR=/tmp/runtime-root
  export DBUS_SYSTEM_BUS_ADDRESS=unix:path=/run/dbus/system_bus_socket
  export WLR_RENDERER=gles2 WLR_RENDERER_ALLOW_SOFTWARE=0
  export WLR_BACKENDS=drm,libinput WLR_DRM_DEVICES=/dev/dri/card0
  export WLR_LIBINPUT_NO_DEVICES=1 WLR_NO_HARDWARE_CURSORS=1
  export WLR_DRM_NO_MODIFIERS=1 WLR_DRM_NO_ATOMIC=1
  export PHOC_PRELOAD="/usr/lib/libjemalloc.so.2 /usr/lib/libstdc++.so.6 /usr/lib/libseat_direct.so /usr/lib/libgpu_interface_driver.so /usr/lib/libmali-bifrost-patched.so"

  echo "[test] Starting phoc..."
  env LD_PRELOAD="$PHOC_PRELOAD" /usr/bin/phoc -v -S -C /etc/phoc.ini > /var/log/phoc_live.log 2>&1 &
  PHOC_PID=$!
  echo "[test] phoc PID=$PHOC_PID"

  # Wait for wayland-0
  cnt=0
  while [ ! -S /tmp/runtime-root/wayland-0 ] && [ $cnt -lt 30 ]; do
    sleep 0.1
    cnt=$((cnt + 1))
  done

  if [ ! -S /tmp/runtime-root/wayland-0 ]; then
    echo "[test] ERROR: wayland-0 socket did not appear!"
    kill -TERM $PHOC_PID 2>/dev/null || true
    exit 1
  fi
  echo "[test] wayland-0 socket ready after ${cnt}00 ms!"

  # Wait for KMS modeset to complete
  sleep 1.5

  echo "[test] Starting phosh..."
  export WAYLAND_DISPLAY=wayland-0
  export GDK_BACKEND=wayland
  export XDG_SESSION_TYPE=wayland
  export XDG_CURRENT_DESKTOP=GNOME:Phosh
  export XDG_SESSION_DESKTOP=phosh
  /usr/libexec/phosh -U > /var/log/phosh_user.log 2>&1 &
  PHOSH_PID=$!
  echo "[test] phosh PID=$PHOSH_PID"

  echo "[test] Waiting 8 seconds for UI to render..."
  sleep 8

  echo "[test] Checking status..."
  ps -o pid,comm,args | grep -E "phoc|phosh" || true

  echo "[test] Screen check completed."
'
