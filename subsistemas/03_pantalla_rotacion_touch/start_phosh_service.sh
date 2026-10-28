#!/bin/sh
chmod 666 /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
echo 15 > /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
chmod 444 /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
if ! pgrep -f backlight_guard >/dev/null 2>&1; then
    /bin/backlight_guard >/dev/null 2>&1 &
fi
exec chroot /sysroot /usr/bin/start_phosh.sh
