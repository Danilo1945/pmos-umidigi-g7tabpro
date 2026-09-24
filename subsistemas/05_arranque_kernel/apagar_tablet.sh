#!/bin/bash
# ==============================================================================
# Script de Apagado Limpio y Seguro para la Tablet UMIDIGI G7 Tab Pro
# ==============================================================================

echo "=========================================================="
echo " 🔌 Apagado Seguro de la Tablet (postmarketOS / Fastboot)  "
echo "=========================================================="

if nc -z -w 1 172.16.42.1 2323 2>/dev/null; then
    echo "[*] Tablet conectada en red (172.16.42.1). Enviando señal de apagado del sistema..."
    echo 'poweroff' | nc -w 1 172.16.42.1 2323 2>/dev/null || true
    echo "✅ Comando de apagado enviado. La tablet se apagará en unos segundos."
    exit 0
fi

if fastboot devices | grep -q "fastboot"; then
    echo "[*] Tablet en modo Fastboot. Enviando orden de apagado..."
    fastboot oem poweroff 2>/dev/null || fastboot reboot-bootloader 2>/dev/null || true
    echo "ℹ️ En Fastboot, mantén pulsado el botón de encendido 10 segundos para apagar completamente."
    exit 0
fi

if adb devices | grep -q "device$"; then
    echo "[*] Tablet en ADB. Apagando..."
    adb reboot -p
    echo "✅ Tablet apagándose..."
    exit 0
fi

echo "[-] No se detectó conexión activa con la tablet por USB o Red."
echo "Para apagarla manualmente de forma segura: mantén pulsado el botón POWER durante 10 segundos hasta que la pantalla se apague."
