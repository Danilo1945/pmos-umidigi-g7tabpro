#!/bin/sh
# Brillo 200 (NO 70)
echo 200 > /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
pkill -9 backlight_guard 2>/dev/null || true

# Cargar firmware WiFi ANTES de chroot
if [ -x /bin/wmt_wifi_start.sh ]; then
    /bin/wmt_wifi_start.sh >/tmp/wmt_wifi.log 2>&1 &
    sleep 3
fi

# Lanzar stack completo dentro del chroot
exec chroot /sysroot /usr/bin/start_phosh.sh