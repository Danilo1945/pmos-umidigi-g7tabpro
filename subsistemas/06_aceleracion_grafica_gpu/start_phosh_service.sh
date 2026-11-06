#!/bin/sh
chmod 666 /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
echo 15 > /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
if [ -x /sysroot/usr/bin/start_phosh.sh ]; then
    exec /sysroot/usr/bin/start_phosh.sh "$@"
else
    exec /usr/bin/start_phosh.sh "$@"
fi
