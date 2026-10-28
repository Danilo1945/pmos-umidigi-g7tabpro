#!/bin/bash
echo "[*] Monitoreando conexión de la tablet por USB..."
for i in {1..30}; do
    # 1. Comprobar serial ttyACM0
    if [ -e /dev/ttyACM0 ]; then
        echo "✅ Detectado /dev/ttyACM0"
        python3 -c "
import serial, time
s = serial.Serial('/dev/ttyACM0', 115200, timeout=2)
s.write(b'\nroot\n')
time.sleep(0.5)
s.write(b'cat /sys/class/power_supply/*/capacity /sys/class/power_supply/*/voltage_now /sys/class/power_supply/*/status 2>/dev/null\n')
time.sleep(1)
print(s.read(s.in_waiting).decode('utf-8', errors='ignore'))
s.close()
" 2>/dev/null
        exit 0
    fi

    # 2. Comprobar red USB (172.16.42.1)
    if ping -c 1 -W 1 172.16.42.1 >/dev/null 2>&1; then
        echo "✅ Detectada red en 172.16.42.1"
        ssh -o ConnectTimeout=2 -o StrictHostKeyChecking=no user@172.16.42.1 "cat /sys/class/power_supply/*/capacity /sys/class/power_supply/*/voltage_now /sys/class/power_supply/*/status 2>/dev/null" 2>/dev/null
        exit 0
    fi

    # 3. Comprobar si está en BROM / Preloader
    if lsusb | grep -qiE "0e8d|MediaTek"; then
        echo "⚡ Detectada en modo MediaTek (BROM / Preloader):"
        lsusb | grep -iE "0e8d|MediaTek"
        exit 0
    fi

    # 4. Comprobar Fastboot
    if fastboot devices 2>/dev/null | grep -q "fastboot"; then
        echo "⚡ Detectada en modo Fastboot:"
        fastboot getvar battery-voltage 2>&1
        fastboot getvar battery-soc-ok 2>&1
        exit 0
    fi

    sleep 1
done
echo "[-] Aún no detectada."
