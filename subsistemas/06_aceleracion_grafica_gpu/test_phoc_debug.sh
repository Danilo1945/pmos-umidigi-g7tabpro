#!/bin/sh
dmesg -c > /dev/null
rm -f /sysroot/tmp/runtime-root/wayland-*

chroot /sysroot env HOME=/root USER=root XDG_RUNTIME_DIR=/tmp/runtime-root \
  DBUS_SYSTEM_BUS_ADDRESS=unix:path=/run/dbus/system_bus_socket \
  WLR_RENDERER=gles2 WLR_RENDERER_ALLOW_SOFTWARE=0 \
  WLR_BACKENDS=drm,libinput WLR_DRM_DEVICES=/dev/dri/card0 \
  WLR_LIBINPUT_NO_DEVICES=1 WLR_NO_HARDWARE_CURSORS=1 \
  WLR_DRM_NO_MODIFIERS=1 WLR_DRM_NO_ATOMIC=1 \
  MALI_DEBUG=1 \
  LD_PRELOAD="/usr/lib/libjemalloc.so.2 /usr/lib/libstdc++.so.6 /usr/lib/libseat_direct.so /usr/lib/libgpu_interface_driver.so /usr/lib/libmali-bifrost-patched.so" \
  /usr/bin/phoc -v -S -C /etc/phoc.ini > /sysroot/var/log/phoc_debug.log 2>&1 &
PHOC_PID=$!

sleep 2

if [ -S /sysroot/tmp/runtime-root/wayland-0 ]; then
    echo "wayland-0 ready, launching phosh..."
    chroot /sysroot env HOME=/root USER=root XDG_RUNTIME_DIR=/tmp/runtime-root \
      DBUS_SYSTEM_BUS_ADDRESS=unix:path=/run/dbus/system_bus_socket \
      WAYLAND_DISPLAY=wayland-0 GDK_BACKEND=wayland \
      /usr/libexec/phosh -U > /sysroot/var/log/phosh_debug.log 2>&1 &
    PHOSH_PID=$!
    sleep 3
    kill -TERM $PHOSH_PID 2>/dev/null
fi

sleep 1
kill -TERM $PHOC_PID 2>/dev/null
wait $PHOC_PID 2>/dev/null
echo "Test done."
