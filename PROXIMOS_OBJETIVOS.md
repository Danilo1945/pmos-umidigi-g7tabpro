# PRÓXIMOS OBJETIVOS - SESIÓN SIGUIENTE

## 📋 RESUMEN EJECUTIVO
La tablet está en **logo UMIDIGI con brillo mínimo** tras reboot. No ha montado rootfs aún. Todos los scripts de boot están corregidos en `/bin/`. Al encender, debería auto-arrancar Phosh + servicios.

---

## ✅ YA HECHO (LISTO PARA AUTO-BOOT)

| Item | Estado | Ubicación |
|------|--------|-----------|
| `start_phosh_service.sh` | ✅ FIXED | `/bin/start_phosh_service.sh` |
| `start_phosh.sh` (chroot) | ✅ CORRECTO | `/sysroot/usr/bin/start_phosh.sh` + `/bin/start_phosh.sh` |
| `backlight_guard` | ✅ SCRIPT SEGURO | `/bin/backlight_guard` |
| `phoc.ini` | ✅ rotate=180 | `/sysroot/etc/phoc.ini` (se regenera en boot) |
| SSH server | ✅ INSTALADO | Puerto 22, `sshd` corriendo |
| WiFi | ✅ CONECTADO | FAMILIA_TAPIA_2 guardado |
| UPower provider | ✅ FUNCIONA | 80% batería, DisplayDevice expuesto |
| squeekboard | ✅ AUTO-START | Agregado a `start_phosh.sh` |
| Phoc/Phosh | ✅ FUNCIONA | Pixman, rotate=180°, supervisor loop |

---

## 🔴 PRÓXIMA SESIÓN - PRIORIDADES INMEDIATAS

### 1. VERIFICAR AUTO-BOOT REAL (PRIORIDAD #1)
```bash
# Esperar USB gadget (1-2 min tras power on)
ping 172.16.42.1

# Verificar UI
python3 tablet_cmd.py "ps aux | grep -E 'phoc|phosh' | grep -v grep"
python3 tablet_cmd.py "cat /sys/class/leds/lcd-backlight/brightness"
```
**Si NO aparece UI:** `python3 tablet_cmd.py "chroot /sysroot /usr/bin/start_phosh.sh"`

### 2. FIX POST-BOOT INMEDIATOS (ejecutar en orden)
```bash
# 1. Matar upowerd zombie (compite con provider fake)
pkill -9 upowerd

# 2. Fix PipeWire (audio)
pkill -9 pipewire pipewire-pulse
rm -f /tmp/runtime-root/pipewire-0*.lock /tmp/runtime-root/pulse/native*
XDG_RUNTIME_DIR=/tmp/runtime-root DBUS_SYSTEM_BUS_ADDRESS=unix:path=/run/dbus/system_bus_socket pipewire -c /tmp/custom_pipewire.conf &
XDG_RUNTIME_DIR=/tmp/runtime-root DBUS_SYSTEM_BUS_ADDRESS=unix:path=/run/dbus/system_bus_socket pipewire-pulse &

# 3. Verificar WiFi auto-conectado
chroot /sysroot /usr/bin/nmcli -t -f GENERAL.STATE,GENERAL.CONNECTION device show wlan0
```

### 3. CONECTAR SSH Y TRABAJAR REMOTO
```bash
ssh root@172.16.42.1
# Trabajar desde PC, mucho más rápido
```

---

## 🟡 TAREAS SECUNDARIAS (CUANDO HAYA TIEMPO)

| Tarea | Comando | Bloqueante |
|-------|---------|------------|
| Limpiar KDE residual | `apk del plasma-* kde-*` | apk db libre |
| Desinstalar upowerd real | `apk del upower` | apk db libre |
| Fix mkinitfs error | Investigar kernel flavor | No bloquea boot |
| Audio real (AW87359) | Firmware SCP + alsa | Requiere blobs vendor |

---

## 📁 ARCHIVOS CLAVE A VERIFICAR TRAS REBOOT

| Archivo | Debe existir | Contenido clave |
|---------|--------------|-----------------|
| `/bin/start_phosh_service.sh` | ✅ | Brillo 200, wmt_wifi_start.sh, exec chroot start_phosh.sh |
| `/bin/start_phosh.sh` | ✅ | Pixman, rotate=180, root, dbus-launch, pipewire, squeekboard |
| `/bin/backlight_guard` | ✅ | Script seguro (restore 0→200 only) |
| `/sysroot/usr/bin/start_phosh.sh` | ✅ | Copia de `/bin/start_phosh.sh` (initramfs la copia) |
| `/bin/backlight_guard` | ✅ | Script seguro (restore 0→200 only) |
| `/sysroot/usr/bin/start_phosh.sh` | ✅ | Debe tener pipewire, upower provider, squeekboard |

---

## 🚨 SI EL AUTO-BOOT FALLA

### Síntoma: Logo UMIDIGI + brillo mínimo > 2 min
```bash
# 1. Verificar si USB gadget responde
ping 172.16.42.1

# 2. Si responde, iniciar manual
python3 tablet_cmd.py "chroot /sysroot /usr/bin/start_phosh.sh"

# 3. Si NO responde, hard reset (power 10s) y reintentar
```

### Síntoma: UI aparece pero brillo 0/70
```bash
# Fix brillo
echo 200 > /sys/class/leds/lcd-backlight/brightness
pkill -9 backlight_guard
nohup sh -c 'while true; do cur=$(cat /sys/class/leds/lcd-backlight/brightness 2>/dev/null || echo 0); [ "$cur" -eq 0 ] && echo 200 > /sys/class/leds/lcd-backlight/brightness; sleep 2; done' &
```

### Síntoma: No hay WiFi
```bash
# Verificar firmware cargado
lsmod | grep -E 'mt6631|wmt|connac'
# Si no, cargar manual
/bin/wmt_wifi_start.sh
# Conectar
nmcli device wifi connect JESSY-RED-2 password 'TU_PASS'
```

---

## 📞 COMANDOS RÁPIDOS PARA COPY-PASTE

```bash
# Estado completo
python3 tablet_cmd.py "ps aux | grep -E 'phoc|phosh|pipewire|NetworkManager|upower|squeekboard|sshd' | grep -v grep; cat /sys/class/leds/lcd-backlight/brightness; chroot /sysroot /usr/bin/busctl --system get-property org.freedesktop.UPower /org/freedesktop/UPower/devices/DisplayDevice org.freedesktop.UPower.Device Percentage; chroot /sysroot /usr/bin/nmcli -t -f GENERAL.STATE,GENERAL.CONNECTION device show wlan0"

# Fix completo post-boot
pkill -9 upowerd; pkill -9 pipewire pipewire-pulse; rm -f /tmp/runtime-root/pipewire-0*.lock /tmp/runtime-root/pulse/native*; XDG_RUNTIME_DIR=/tmp/runtime-root DBUS_SYSTEM_BUS_ADDRESS=unix:path=/run/dbus/system_bus_socket pipewire -c /tmp/custom_pipewire.conf & sleep 2; XDG_RUNTIME_DIR=/tmp/runtime-root DBUS_SYSTEM_BUS_ADDRESS=unix:path=/run/dbus/system_bus_socket pipewire-pulse & sleep 2; echo "DONE"

# SSH
ssh root@172.16.42.1
```

---

## 📌 NOTAS FINALES

- **Tablet en logo UMIDIGI ahora** = normal, esperando rootfs mount
- **Brillo mínimo en logo** = normal (initramfs splash)
- **Auto-boot debería funcionar** con scripts corregidos en `/bin/`
- **SSH es la forma más rápida** de trabajar tras boot
- **Documentación completa** en `DOCUMENTACION_COMPLETA.md`

---

**¡Siguiente sesión: verificar auto-boot y aplicar fixes post-boot!** 🚀