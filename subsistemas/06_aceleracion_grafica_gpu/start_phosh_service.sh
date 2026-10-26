#!/bin/sh
echo 15 > /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
/bin/backlight_guard >/dev/null 2>&1 &
exec chroot /sysroot /usr/bin/start_phosh.sh
