#!/bin/bash
echo "=== MTKClient Auto-Flasher Iniciado ==="
echo "Esperando que el usuario reconecte el cable USB..."
while true; do
    OUT=$(echo danilo | sudo -S /home/danilo/anaconda3/bin/python3 /home/danilo/Documentos/TABLET/mtkclient/mtk.py script /home/danilo/Documentos/TABLET/mtk_flash_script.txt 2>&1 || true)
    if echo "$OUT" | grep -q "Wrote"; then
        echo "$OUT"
        echo ""
        echo "🎉 ¡FLASHEO COMPLETO CON ÉXITO VÍA MTKCLIENT!"
        break
    fi
    sleep 0.3
done
