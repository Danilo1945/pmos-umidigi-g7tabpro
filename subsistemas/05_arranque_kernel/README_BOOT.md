# Subsistema de Arranque, Kernel y Watchdog - UMIDIGI G7 Tab Pro
**SoC MediaTek Helio G99 (MT6789) - postmarketOS Edge aarch64**

---

## 1. Arquitectura de Arranque Android A/B y MediaTek BROM
- **Particiones de Arranque**:
  - `boot_a` / `boot_b`: Contiene cabecera Boot Image v4 de Android, kernel uncompressed o gzip (`Image.gz`), initramfs de postmarketOS empaquetado en cpio.gz.
  - `vendor_boot_a` / `vendor_boot_b`: Contiene el árbol de dispositivos compilado (`dtb`) inyectado y parámetros adicionales.
- **Árbol de Dispositivos (Device Tree - DTB)**:
  - `pmos_usb_peripheral.dtb`: Parcheado para forzar la controladora USB MediaTek (`musb_hdrc`) en modo `peripheral` permanente (Gadget CDC-NCM para red IP fija `172.16.42.1`).

---

## 2. El Perro Guardián del Kernel (`wdt_pet`)
- **Problema de Hardware**:
  - El SoC MediaTek MT6789 tiene un watchdog de silicio (`mtk_wdt`) con un timeout rígido de 30 segundos.
  - Si el usuario de espacio no alimenta periódicamente `/dev/watchdog` o `/dev/watchdog0`, el hardware resetea la tablet en bucle sin dejar rastros de kernel panic.
- **Solución `wdt_pet`**:
  - Binario ultra-ligero y estático compilado en C.
  - Escanea automáticamente los nodos dinámicos de watchdog en `/proc/devices`, `/sys/class/watchdog/` y majors dinámicos.
  - Envía el pulso de latido ioctl `WDIOC_KEEPALIVE` cada 500 ms directamente hacia el registro de silicio.
  - Es el primer proceso lanzado por el initramfs.

---

## 3. Scripts y Herramientas Incluidas
1. `build_pmos_boot_v13.py`: Genera las imágenes `pmos_boot.img` y `vendor_boot_pmos.img` empaquetando el initramfs actualizado, scripts de inicio, módulos y DTB.
2. `flash_pmos_v13_mtkclient.sh`: Flashea las particiones de arranque mediante `mtkclient` en modo BROM / Preloader sin necesidad de desbloqueo fastboot manual.
3. `flash_pmos_v13.sh`: Flashea usando fastboot estándar (`fastboot flash boot_a ...`).
4. `activar_slot_b.sh` / `apagar_tablet.sh`: Scripts utilitarios para gestión de particiones A/B y apagado seguro vía mtkclient.
5. `wdt_pet.c`: Código fuente en C del watchdog petter.
6. `pmos_usb_peripheral.dts` & `.dtb`: Código fuente y binario del Device Tree con USB periférico habilitado.

---

## 4. Reconstrucción y Flasheo de Imágenes
```bash
# Para reconstruir las imágenes de arranque completas:
python3 build_pmos_boot_v13.py

# Para flashear a la tablet con mtkclient (conectar tablet apagada presionando Vol- o por BROM):
./flash_pmos_v13_mtkclient.sh
```
