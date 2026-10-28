#!/bin/sh
set -e

echo "=== PASO 1: VERIFICAR Y ASEGURAR WATCHDOG Y BACKLIGHT ==="
chmod 666 /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
echo 15 > /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
if ! pgrep -f backlight_lock >/dev/null 2>&1; then
    /bin/backlight_lock >/dev/null 2>&1 &
fi
if ! pgrep -f wdt_pet >/dev/null 2>&1; then
    /bin/wdt_pet >/dev/null 2>&1 &
fi

echo "=== PASO 2: SANITIZAR PROCESOS PREVIOS Y SOCKETS ==="
killall -TERM phoc phosh 2>/dev/null || true
sleep 1
rm -f /sysroot/tmp/runtime-root/wayland-* /tmp/runtime-root/wayland-* 2>/dev/null || true
mkdir -p /sysroot/tmp/runtime-root /tmp/runtime-root
chmod 700 /sysroot/tmp/runtime-root /tmp/runtime-root

# Asegurar tmpfs en /dev/shm dentro de chroot
mkdir -p /sysroot/dev/shm
if ! grep -q ' /sysroot/dev/shm ' /proc/mounts 2>/dev/null; then
    mount -t tmpfs -o mode=1777,size=128M shm /sysroot/dev/shm 2>/dev/null || true
fi

# Permisos para Mali y DRM
chmod 666 /sysroot/dev/mali0 /sysroot/dev/dri/* /sysroot/dev/dma_heap/* 2>/dev/null || true

echo "=== PASO 3: CONFIGURAR PHOC.INI ==="
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

echo "=== PASO 4: LANZAR PHOC Y PHOSH DENTRO DE CHROOT ==="
chroot /sysroot sh -c '
  # Sesión D-Bus requerida por Phosh
  eval $(dbus-launch --sh-syntax)
  echo "[supervisor] D-Bus session bus: $DBUS_SESSION_BUS_ADDRESS"

  export HOME=/root
  export USER=root
  export XDG_RUNTIME_DIR=/tmp/runtime-root
  export DBUS_SYSTEM_BUS_ADDRESS=unix:path=/run/dbus/system_bus_socket

  # Renderizado KMS / EGL con Mali GPU
  export WLR_RENDERER=gles2
  export WLR_RENDERER_ALLOW_SOFTWARE=0
  export WLR_BACKENDS=drm,libinput
  export WLR_DRM_DEVICES=/dev/dri/card0
  export WLR_LIBINPUT_NO_DEVICES=1
  export WLR_NO_HARDWARE_CURSORS=1
  export WLR_DRM_NO_MODIFIERS=1
  export WLR_DRM_NO_ATOMIC=1

  export PHOC_PRELOAD="/usr/lib/libjemalloc.so.2 /usr/lib/libstdc++.so.6 /usr/lib/libseat_direct.so /usr/lib/libgpu_interface_driver.so /usr/lib/libmali-bifrost-patched.so"

  echo "[supervisor] Arrancando Phoc compositor con aceleracion GPU Mali-G57..."
  env LD_PRELOAD="$PHOC_PRELOAD" /usr/bin/phoc -v -S -C /etc/phoc.ini > /var/log/phoc_live.log 2>&1 &
  PHOC_PID=$!
  echo "[supervisor] Phoc iniciado con PID=$PHOC_PID"

  # Esperar socket Wayland
  cnt=0
  while [ ! -S /tmp/runtime-root/wayland-0 ] && [ $cnt -lt 50 ]; do
      sleep 0.1
      cnt=$((cnt + 1))
  done

  if [ ! -S /tmp/runtime-root/wayland-0 ]; then
      echo "[supervisor] ERROR CRITICO: socket wayland-0 no aparecio!"
      cat /var/log/phoc_live.log
      kill -TERM $PHOC_PID 2>/dev/null || true
      exit 1
  fi
  echo "[supervisor] Socket wayland-0 ACTIVO tras ${cnt}00 ms!"

  # Estabilizacion de KMS modeset
  sleep 1.5

  # Consultar output KMS con wlr-randr
  export WAYLAND_DISPLAY=wayland-0
  echo "[supervisor] Estado de salida Wayland (wlr-randr):"
  wlr-randr 2>/dev/null || echo "[supervisor] wlr-randr no disponible"

  echo "[supervisor] Lanzando GNOME Mobile Phosh UI..."
  export GDK_BACKEND=wayland
  export QT_QPA_PLATFORM=wayland
  export XDG_SESSION_TYPE=wayland
  export XDG_CURRENT_DESKTOP=GNOME:Phosh
  export XDG_SESSION_DESKTOP=phosh
  unset LD_PRELOAD

  /usr/libexec/phosh -U > /var/log/phosh_live.log 2>&1 &
  PHOSH_PID=$!
  echo "[supervisor] Phosh iniciado con PID=$PHOSH_PID"

  echo "[supervisor] Esperando 6 segundos de renderizado..."
  sleep 6

  echo "[supervisor] Verificando estado de procesos:"
  ps | grep -E "phoc|phosh" | grep -v grep || true

  echo "[supervisor] Ultimas 20 lineas de /var/log/phosh_live.log:"
  tail -n 20 /var/log/phosh_live.log 2>/dev/null || true

  echo "[supervisor] Ultimas 20 lineas de /var/log/phoc_live.log:"
  tail -n 20 /var/log/phoc_live.log 2>/dev/null || true
'
