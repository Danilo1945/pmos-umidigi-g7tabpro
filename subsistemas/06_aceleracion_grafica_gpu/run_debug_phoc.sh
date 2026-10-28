#!/bin/sh
dmesg -c > /dev/null
rm -f /sysroot/tmp/runtime-root/wayland-*
chroot /sysroot sh -c '
  export HOME=/root USER=root XDG_RUNTIME_DIR=/tmp/runtime-root
  export WLR_RENDERER=gles2 WLR_RENDERER_ALLOW_SOFTWARE=0
  export WLR_BACKENDS=drm,libinput WLR_DRM_DEVICES=/dev/dri/card0
  export WLR_LIBINPUT_NO_DEVICES=1 WLR_NO_HARDWARE_CURSORS=1
  export WLR_DRM_NO_MODIFIERS=1 WLR_DRM_NO_ATOMIC=1
  export MALI_DEBUG=1
  export LD_PRELOAD="/usr/lib/libjemalloc.so.2 /usr/lib/libstdc++.so.6 /usr/lib/libseat_direct.so /usr/lib/libgpu_interface_driver.so /usr/lib/libmali-bifrost-patched.so"
  /usr/bin/phoc -v -S -C /etc/phoc.ini > /var/log/phoc_debug.log 2>&1 &
  PID=$!
  sleep 3
  kill -TERM $PID 2>/dev/null || true
  sleep 0.5
'
dmesg | grep -E "VALHALL|FRAGMENT|BOUNCE|fault|error detected|STATUS" | head -n 40
