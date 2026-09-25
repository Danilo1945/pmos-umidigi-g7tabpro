# Repositorio Maestro de Funcionalidades Finales y Versionamiento
## UMIDIGI G7 Tab Pro (`TG2403GBA`) - SoC MediaTek Helio G99 (`MT6789`)
### postmarketOS Edge (Alpine Linux aarch64 - Kernel 5.10.185-pmos)

---

## 📌 Propósito de este Repositorio
Este repositorio consolida **todas las funcionalidades funcionales y probadas** que han sido estabilizadas en la tablet UMIDIGI G7 Tab Pro. 
Su objetivo es servir como **código fuente canónico y punto de restauración determinista**: si al implementar una nueva característica (por ejemplo, aceleración de video GPU / VPU) se altera alguna configuración o driver, cualquier subsistema puede ser recompilado o restaurado instantáneamente con un solo comando.

---

## 🗂️ Arquitectura del Repositorio

```
FUNCIONALIDADES_FINALES/
├── build_all.sh                       # Script maestro de compilación de todos los binarios C
├── README.md                          # Este documento técnico maestro
├── .gitignore                         # Control de versiones Git optimizado
│
├── subsistemas/
│   ├── 01_audio/                      # 🔊 Subsistema de Audio MT6789 AFE + 4x AW87359
│   │   ├── tablet_audio_daemon.py     # Demonio de calibración ALSA y control de ganancias
│   │   ├── 10-alsa.conf               # Configuración PipeWire S16LE / 48kHz / disable-mmap
│   │   ├── 50-alsa-fix.conf           # Configuración WirePlumber
│   │   ├── analog-output-headphones.conf # Mapeo de rutas ALSA/PulseAudio
│   │   ├── volume_keys_daemon.py      # Captura de teclas físicas de volumen
│   │   ├── install_audio.sh           # Instalador / servicio OpenRC tablet-audio
│   │   └── README_AUDIO.md            # Documentación exhaustiva del audio
│   │
│   ├── 02_bateria_energia/            # 🔋 Subsistema de Batería PMIC MT6366 / MT6375
│   │   ├── tablet_battery_daemon.py   # Demonio de monitoreo OCV + bind mount en vivo
│   │   ├── battery_daemon.c           # Fuente en C de alta eficiencia
│   │   ├── battery_table.h            # Tabla OCV calibrada para celda 7500mAh
│   │   ├── battery_daemon             # Binario ejecutable estático AArch64
│   │   ├── Makefile                   # Compilación cruzada para AArch64
│   │   ├── install_battery.sh         # Instalador / servicio OpenRC tablet-battery
│   │   └── README_BATERIA.md          # Documentación técnica de energía y carga
│   │
│   ├── 03_pantalla_rotacion_touch/    # 🖥️ Pantalla DSI, Touch Chipone y Wayland Phosh
│   │   ├── libseat_direct.c           # Shim en C directo a syscalls sin dependencias
│   │   ├── libseat_direct.so          # Biblioteca compartida compilada AArch64
│   │   ├── phoc.ini                   # Geometría, rotación 180° y escala de compositor
│   │   ├── start_phosh.sh             # Script integral de arranque de sesión GNOME/Phosh
│   │   ├── Makefile                   # Compilación de libseat_direct.so
│   │   ├── install_display.sh         # Instalador gráfico
│   │   └── README_PANTALLA.md         # Documentación de DRM/KMS y libseat
│   │
│   ├── 04_wifi_conectividad/          # 📶 Wi-Fi 802.11ac MediaTek MT6631 / WMT
│   │   ├── wmt_init_complete.c        # Fuente en C de inicialización WMT
│   │   ├── wmt_init_complete          # Binario estático AArch64
│   │   ├── wmt_launcher & wmt_loader  # Binarios auxiliares de firmware
│   │   ├── wmt_wifi_start.sh          # Secuencia de encendido y registro udev/NM
│   │   ├── Makefile                   # Compilación de wmt_init_complete
│   │   ├── install_wifi.sh            # Instalador y servicio mtk-wifi
│   │   └── README_WIFI.md             # Documentación de protocolo STP/WMT
│   │
│   └── 05_arranque_kernel/            # 🚀 Arranque Android v4, Kernel y Watchdog
│       ├── wdt_pet.c                  # Fuente en C del alimentador de watchdog
│       ├── wdt_pet                    # Binario estático AArch64
│       ├── pmos_usb_peripheral.dtb    # Árbol de dispositivos con USB Gadget forzado
│       ├── pmos_usb_peripheral.dts    # Fuente DTS descompilada
│       ├── build_pmos_boot_v13.py     # Empaquetador de imágenes boot.img y vendor_boot.img
│       ├── flash_pmos_v13_mtkclient.sh# Flasheador BROM / Preloader automático
│       ├── flash_pmos_v13.sh          # Flasheador Fastboot
│       ├── activar_slot_b.sh          # Switch de slot A/B
│       ├── apagar_tablet.sh           # Apagado remoto vía mtkclient
│       ├── Makefile                   # Compilación de wdt_pet
│       └── README_BOOT.md             # Documentación de particiones y watchdog
│
├── firmware/                          # 💾 Firmwares y microcódigos esenciales
│   ├── aw87xxx_acf.bin                # Configuración de 4 amplificadores AW87359
│   ├── chipone_firmware.bin           # Microcódigo del digitalizador táctil TDDI
│   ├── WIFI_RAM_CODE_soc2_2_*.bin     # Firmware Wi-Fi RAM SoC MediaTek
│   ├── soc2_2_ram_wifi_*.bin          # Cabeceras de microcódigo de red
│   └── WMT_SOC.cfg / WMT.cfg          # Configuración del bus WMT
│
└── tools/                             # 🛠️ Herramientas de soporte y diagnóstico
    ├── tablet_cmd.py                  # Shell interactiva remota por socket 2323
    └── restore_live_tablet.py         # Restaurador automático 1-click hacia la tablet viva
```

---

## 🚀 Guía Rápida de Operaciones

### 1. Compilar Todos los Binarios C desde Cero
Si se dispone de `aarch64-linux-gnu-gcc` (o dentro de Alpine aarch64):
```bash
cd /home/danilo/Documentos/TABLET/FUNCIONALIDADES_FINALES
./build_all.sh
```

### 2. Restaurar la Tablet en Vivo con un Solo Comando (1-Click Restore)
Con la tablet encendida y conectada vía USB (`172.16.42.1`):
```bash
python3 tools/restore_live_tablet.py
```
Este comando verificará el socket en el puerto `2323`, re-instalará los scripts y configuraciones de audio, batería, pantalla y Wi-Fi, y reiniciará los daemons automáticamente.

### 3. Diagnóstico Remoto Instantáneo
```bash
# Ver estado del sistema:
python3 tools/tablet_cmd.py "uptime; free -m; ip a show wlan0"

# Ver procesos de los daemons clave:
python3 tools/tablet_cmd.py "ps aux | grep -E 'battery|audio|phosh|wmt'"
```

### 4. Reconstruir y Flashear Imágenes de Arranque
Si se requiere reinstalar el kernel o el initramfs:
```bash
cd subsistemas/05_arranque_kernel
python3 build_pmos_boot_v13.py
./flash_pmos_v13_mtkclient.sh
```

---

## 📊 Matriz de Estado y Funcionalidades Validadas

| Subsistema | Componente Clave | Estado | Verificación Técnica |
| :--- | :--- | :--- | :--- |
| **Audio** | ALSA MT6789 AFE + 4x AW87359 | ✅ **100% Funcional** | Sonido nítido, buen volumen, sin siseo ni distorsión (ganancias calibradas a 32768 y 12, I2S3 off, DMA S16LE en PipeWire). |
| **Batería** | PMIC MT6366 + MT6375 chg | ✅ **100% Funcional** | Curva OCV 7500mAh ($R_{int}=85\text{ m}\Omega$), Daemon v4 con bind mount persistente, notificación D-Bus Refresh y uevent sin matar upowerd, rayo de carga y porcentaje real en Phosh. |
| **Pantalla** | MIPI-DSI 1200x1920 + TDDI | ✅ **100% Funcional** | Sesión GNOME/Phosh acelerada levantada con bypass `libseat_direct.so`, rotación 180° y touch fluido. |
| **Wi-Fi** | MediaTek MT6631 / WMT | ✅ **100% Funcional** | Interfaz `wlan0` activa, gestionada por NetworkManager con registro udev dinámico. |
| **Kernel / Boot**| Boot v4 + musb Gadget + WDT | ✅ **100% Funcional** | Kernel 5.10.185-pmos arranca de manera estable sin reboot a los 30s gracias a `wdt_pet`. Red USB fija `172.16.42.1`. |

---

## 🛡️ Política de Versionamiento
- Cualquier cambio o experimento futuro (ej. aceleración por hardware con Mali G57 y VPU) **debe crearse en una rama separada** o probarse sin sobreescribir este repositorio.
- Si una prueba compromete el audio o el sistema gráfico, basta con ejecutar `python3 tools/restore_live_tablet.py` para regresar al estado 100% operativo.
