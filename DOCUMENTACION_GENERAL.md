# DOCUMENTACIÓN GENERAL DE FUNCIONALIDADES Y ARQUITECTURA
## UMIDIGI G7 Tab Pro (`TG2403GBA`) - SoC MediaTek Helio G99 (`MT6789`)
### postmarketOS Edge (Alpine Linux aarch64 - Kernel 5.10.185-pmos)

---

## 1. VISIÓN GENERAL Y OBJETIVO DEL REPOSITORIO
Este repositorio reúne y organiza **todas las soluciones de ingeniería, código fuente en C, scripts en Python/Shell, configuraciones probadas y microcódigos de firmware** que han llevado a estabilizar por completo la tablet UMIDIGI G7 Tab Pro corriendo postmarketOS Edge.

El objetivo central es proporcionar un **entorno determinista y seguro**: cuando en el futuro se trabaje en nuevas metas complejas (como aceleración por hardware con GPU Mali-G57 o decodificación de video VPU), si alguna modificación altera o corrompe un subsistema ya funcional, **no será necesario reiniciar la investigación desde cero**. Con este repositorio se puede leer la documentación técnica, recompilar los binarios con `./build_all.sh` o restaurar la tablet en caliente en 3 segundos con `python3 tools/restore_live_tablet.py`.

---

## 2. FICHA TÉCNICA DEL DISPOSITIVO
- **Modelo comercial**: UMIDIGI G7 Tab Pro (Identificador de placa: `TG2403GBA`).
- **Plataforma SoC**: MediaTek Helio G99 (`MT6789` / `MT8781`), 6nm.
- **CPU**: Octa-Core (2x Cortex-A76 @ 2.2 GHz + 6x Cortex-A55 @ 2.0 GHz).
- **GPU**: ARM Mali-G57 MC2 (Arquitectura Valhall 1ra gen).
- **Memoria RAM**: 8 GB LPDDR4X.
- **Almacenamiento**: 128 GB UFS 2.2.
- **Pantalla**: 11 pulgadas LCD IPS, resolución nativa 1200x1920 (relación de aspecto 16:10), MIPI-DSI, rotación física invertida a 180°.
- **Batería**: 7500 mAh (celda Li-Po nominal 3.85V, límite de carga 4.40V).
- **PMIC Principal**: MediaTek MT6366 / MT6358 (Audio DAC, Fuel Gauge, RTC, Regulación Buck/LDO).
- **Sub-PMIC / Cargador**: MediaTek MT6375 (Carga USB-PD, Tipo C, PE+).
- **Audio Analógico**: Cuádruple amplificador inteligente Awinic AW87359 (I2C buses 6 y 7, Clase K analógica).
- **Touch**: Digitalizador multitáctil TDDI Chipone I2C (`tddi_9551`).
- **Conectividad Inalámbrica**: MediaTek MT6631 / MT6635 (Wi-Fi 802.11ac dual band 2.4/5GHz + Bluetooth 5.2).
- **Sistema Operativo**: postmarketOS Edge (Alpine Linux musl libc, arquitectura aarch64).
- **Entorno Gráfico**: Phosh Mobile Desktop sobre compositor Phoc (Wayland / wlroots).
- **Servidor de Audio**: PipeWire con WirePlumber.
- **Inicializador del Sistema**: OpenRC.

---

## 3. COMPOSICIÓN Y ESTRUCTURA DEL REPOSITORIO

```
FUNCIONALIDADES_FINALES/
├── DOCUMENTACION_GENERAL.md           # [ESTE ARCHIVO] Documentación técnica y operativa completa
├── README.md                          # Resumen ejecutivo y guía rápida de operaciones
├── build_all.sh                       # Script maestro de compilación cruzada AArch64
├── .gitignore                         # Exclusión de temporales respetando código y firmwares
│
├── subsistemas/
│   ├── 01_audio/                      # 🔊 Subsistema de Audio MT6789 AFE + 4x AW87359
│   │   ├── tablet_audio_daemon.py     # Demonio en Python: control ALSA, ganancias calibradas, mute
│   │   ├── 10-alsa.conf               # Configuración PipeWire S16LE / 48kHz / disable-mmap / period-size 6000
│   │   ├── 50-alsa-fix.conf           # Configuración complementaria WirePlumber
│   │   ├── analog-output-headphones.conf # Mapeo de perfiles ALSA / PulseAudio
│   │   ├── volume_keys_daemon.py      # Captura de eventos físicos de teclas de volumen
│   │   ├── install_audio.sh           # Instalador / configuración del servicio OpenRC tablet-audio
│   │   └── README_AUDIO.md            # Manual técnico del subsistema de audio
│   │
│   ├── 02_bateria_energia/            # 🔋 Subsistema de Batería PMIC MT6366 / MT6375
│   │   ├── tablet_battery_daemon.py   # Demonio de monitoreo OCV con compensación IR (85 mΩ) y bind mount
│   │   ├── battery_daemon.c           # Código fuente en C de alto rendimiento sin sobrecarga
│   │   ├── battery_table.h            # Tabla OCV de 100 puntos calibrada para 7500mAh
│   │   ├── battery_daemon             # Binario ejecutable estático AArch64
│   │   ├── Makefile                   # Compilación con aarch64-linux-gnu-gcc
│   │   ├── install_battery.sh         # Instalador y servicio OpenRC tablet-battery
│   │   └── README_BATERIA.md          # Manual técnico de batería, carga y OCV
│   │
│   ├── 03_pantalla_rotacion_touch/    # 🖥️ Pantalla DSI, Touch y Phosh Wayland
│   │   ├── libseat_direct.c           # Shim en C directo a syscalls sin dependencias libc
│   │   ├── libseat_direct.so          # Biblioteca compartida compilada AArch64
│   │   ├── phoc.ini                   # Geometría, rotación 180° y escala de compositor Phoc
│   │   ├── start_phosh.sh             # Script maestro de arranque del entorno móvil Phosh
│   │   ├── Makefile                   # Compilación de libseat_direct.so (-nostdlib)
│   │   ├── install_display.sh         # Instalador gráfico y compositor
│   │   └── README_PANTALLA.md         # Manual de DRM/KMS, mediatek_drm y libseat direct
│   │
│   ├── 04_wifi_conectividad/          # 📶 Wi-Fi MediaTek MT6631 / WMT
│   │   ├── wmt_init_complete.c        # Fuente en C de inicialización WMT (ioctl directo a wmtdetect)
│   │   ├── wmt_init_complete          # Binario estático AArch64
│   │   ├── wmt_launcher & wmt_loader  # Binarios de soporte de microcódigo propietario
│   │   ├── wmt_wifi_start.sh          # Secuencia de encendido y registro dinámico en udev/NM
│   │   ├── Makefile                   # Compilación de wmt_init_complete
│   │   ├── install_wifi.sh            # Instalador y servicio OpenRC mtk-wifi
│   │   └── README_WIFI.md             # Manual técnico del bus WMT y chip MT6631
│   │
│   └── 05_arranque_kernel/            # 🚀 Arranque Android v4, Kernel y Watchdog
│       ├── wdt_pet.c                  # Código fuente en C del alimentador de watchdog de silicio
│       ├── wdt_pet                    # Binario estático AArch64
│       ├── pmos_usb_peripheral.dtb    # Árbol de dispositivos con USB Gadget forzado (CDC-NCM)
│       ├── pmos_usb_peripheral.dts    # Fuente DTS descompilada
│       ├── build_pmos_boot_v13.py     # Empaquetador de imágenes boot.img y vendor_boot.img
│       ├── flash_pmos_v13_mtkclient.sh# Flasheador BROM / Preloader automático
│       ├── flash_pmos_v13.sh          # Flasheador Fastboot estándar
│       ├── activar_slot_b.sh          # Switch de slot A/B
│       ├── apagar_tablet.sh           # Apagado seguro remoto vía mtkclient
│       ├── Makefile                   # Compilación de wdt_pet
│       └── README_BOOT.md             # Manual de particionado, initramfs y watchdog
│
├── firmware/                          # 💾 Microcódigos esenciales del SoC
│   ├── aw87xxx_acf.bin                # Configuración de los 4 amplificadores AW87359
│   ├── chipone_firmware.bin           # Microcódigo del digitalizador táctil TDDI
│   ├── WIFI_RAM_CODE_soc2_2_*.bin     # Firmware de memoria RAM Wi-Fi MT6631
│   ├── soc2_2_ram_wifi_*.bin          # Cabeceras binarias de microcódigo de red
│   └── WMT_SOC.cfg / WMT.cfg          # Configuración del bus WMT MediaTek
│
└── tools/                             # 🛠️ Herramientas de soporte y diagnóstico
    ├── tablet_cmd.py                  # Shell remota por socket TCP (puerto 2323)
    └── restore_live_tablet.py         # ⚡ Restaurador 1-click automático hacia la tablet viva
```

---

## 4. DETALLE TÉCNICO EXHAUSTIVO POR SUBSISTEMA

### 4.1. SUBSISTEMA 01: AUDIO (ALSA + MT6789 AFE + 4x AW87359)

#### A. Arquitectura de Hardware
La salida de audio estéreo cuádruple utiliza una arquitectura híbrida:
1. **DAC Interno MT6366**:
   El procesador de audio digital (`DL1` / Downlink 1 del AFE MT6789) convierte los paquetes PCM digitales a señal analógica diferencial a través de las salidas `HPL` (Headphone Left) y `HPR` (Headphone Right).
2. **4x Amplificadores AW87359**:
   Los 4 altavoces están alimentados por 4 chips Awinic AW87359 (buses I2C 6 y 7, direcciones `0x58` y `0x59`). **No son DACs I2S**. Son amplificadores de potencia analógicos Clase K que reciben la salida analógica de `HPL`/`HPR` y la amplifican.

#### B. Problemas Críticos Encontrados y Solución Aplicada
1. **Saturación de Ganancia y Silenciamiento por Protección DRC/AGC**:
   - *Causa*: Los valores originales de ganancia (`ADDA_DL_GAIN = 63311` = +14 dB y `Headset Volume = 18` = +8 dB) sobrecargaban la entrada analógica de los amplificadores AW87359. El circuito interno Dynamic Range Control (DRC) del chip detectaba distorsión extrema y recortaba por completo la salida musical para proteger los altavoces, dejando pasar únicamente el ruido térmico del circuito (un siseo o "sasss" constante).
   - *Solución*: Calibración precisa a ganancia nominal:
     - `ADDA_DL_GAIN = 32768` (0 dB exacto, sin saturación).
     - `Headset Volume = 12 12` (+2 dB, rango dinámico limpio).
2. **Interferencia Parásita del Bus I2S3**:
   - *Causa*: Los controles `I2S3_CH1 DL1_CH1` (numid 179) e `I2S3_CH2 DL1_CH2` (numid 192) activaban líneas de reloj digital compartidas que inducían zumbido sobre las pistas analógicas.
   - *Solución*: Ambos controles se mantienen en `0` (`Off`).
3. **Incompatibilidad de Buffer DMA en PipeWire**:
   - *Causa*: PipeWire negociaba con ALSA en formato `S32_LE` (32 bits), modo de acceso `MMAP_INTERLEAVED` y period-size de 1024 frames. El DMA del silicio MediaTek MT6789 exige estrictamente:
     - Formato: `S16_LE` (16 bits).
     - Acceso: `RW_INTERLEAVED` (sin MMAP directo).
     - Period size: `6000` frames.
     - Rate: `48000` Hz.
   - *Solución*: Archivo `/etc/pipewire/pipewire.conf.d/10-alsa.conf` forzando estas propiedades a nivel de sesión.
4. **Crash del Reproductor Elisa**:
   - *Causa*: Elisa (KDE) fallaba con `SIGSEGV` en Alpine musl bajo Wayland al reproducir audio.
   - *Solución*: Uso del reproductor GNOME Mobile oficial **Amberol** (`io.bassi.Amberol`) o `mpv`.

---

### 4.2. SUBSISTEMA 02: BATERÍA Y GESTIÓN DE ENERGÍA (MT6366 / MT6375)

#### A. Arquitectura
- **Fuel Gauge MT6366/MT6358**:
  - Lee el voltaje de celda (`voltage_now`) y la corriente instantánea (`FG_Battery_CurrentConsumption`).
- **Cargador Rápido MT6375**:
  - Controla la entrada USB-C y reporta presencia de corriente de carga (`online` y `status`).

#### B. Problemas y Solución Matemática
1. **Porcentaje de Batería Congelado (50% o 1%)**:
   - El kernel MediaTek requiere que un daemon en espacio de usuario escriba periódicamente el porcentaje calculado en `/sys/devices/platform/soc/10026000.pwrap/10026000.pwrap:mt6366/mt6358-gauge/UI_SOC`. Al faltar el HAL de Android, el valor quedaba estático.
2. **Caída Resistiva por Carga de CPU ($I \cdot R$)**:
   - Al exigir la CPU/GPU, la celda de la batería sufre una caída de tensión resistiva debido a su resistencia interna ($R_{int} \approx 85\text{ m}\Omega$). Si se toma el voltaje directo, el porcentaje oscila erráticamente.
   - *Solución*: Se calcula el Voltaje en Circuito Abierto real ($V_{OCV}$):
     $$V_{OCV} = V_{bat} - (I_{bat} \cdot R_{int})$$
     Ese valor se interpola en una **Tabla OCV calibrada de 100 puntos** (desde 4.37V = 100% hasta 3.00V = 1%) y se filtra con un suavizador IIR exponencial ($\alpha = 0.2$).
3. **Ausencia del Icono de Rayo de Carga en Phosh**:
   - El driver MTK no actualizaba `/sys/class/power_supply/battery/status` a `Charging`.
   - *Solución de Ingeniería*: Demonio con técnica de **Bind Mount dinámico**:
     ```bash
     mount --bind /tmp/battery_fake_status /sys/class/power_supply/battery/status
     ```
     Al conectar el cargador ($I > 20\text{ mA}$ o MT6375 online), se escribe `Charging`, se monta sobre el sysfs y se emite el sonido oficial `power-plug.ogg`. Al desconectar, se pasa a `Discharging` y suena `power-unplug.ogg`.

---

### 4.3. SUBSISTEMA 03: PANTALLA, ROTACIÓN Y TOUCH (DRM/KMS + WAYLAND)

#### A. Arquitectura
- **Controlador Gráfico**: `mediatek_drm` (`/dev/dri/card0`), panel MIPI-DSI (`DSI-1`, 1200x1920).
- **Touch**: TDDI Chipone I2C (`chipone_firmware.bin` en `/lib/firmware`).
- **Compositor y Shell**: Phoc + Phosh (Wayland / wlroots).

#### B. Solución al Bloqueo de Gestión de Sesión (`libseat`)
- *Problema*: Phoc requiere una biblioteca de gestión de sesión (`libseat`) para abrir `/dev/dri/card0` e `/dev/input/*`. En Alpine Linux sin systemd-logind y con el daemon `seatd` fallando por limitaciones de cgroups en el kernel 5.10 de Android, Phoc no podía abrir la pantalla y abortaba inmediatamente.
- *Solución*: Se creó `libseat_direct.c`:
  - Implementación pura en C enlazada con `-nostdlib` y ensamblador directo AArch64 (syscalls `openat`, `close`, `eventfd2`, `write`).
  - No depende de daemons externos, IPC ni privilegios de logind.
  - Se inyecta mediante `LD_PRELOAD=/usr/lib/libseat_direct.so` al ejecutar `phoc`.
  - Permite levantar Phosh de forma instantánea y 100% estable.
- *Orientación*: En `phoc.ini` se define `rotate=180` y `scale=1.0`, y tras levantar Wayland se aplica `wlr-randr --output DSI-1 --transform 180`, corrigiendo la inversión nativa de fábrica del panel.

---

### 4.4. SUBSISTEMA 04: WI-FI Y CONECTIVIDAD (WMT MT6631)

#### A. Arquitectura y Protocolo STP
MediaTek integra Wi-Fi, Bluetooth, FM y GPS en una arquitectura unificada llamada **WMT** (Wireless Management Tool) que se comunica mediante el protocolo serie STP (Serial Transfer Protocol).

#### B. Secuencia de Arranque Determinista (`wmt_wifi_start.sh`)
1. Creación de nodos dev estáticos: `/dev/stpwmt` (190,0), `/dev/wmtdetect` (154,0), `/dev/wmtWifi`.
2. Enlaces simbólicos de firmware a `/vendor/firmware` y `/system/etc/firmware`.
3. Carga de módulos de kernel: `cfg80211.ko`, `connadp.ko`, `btif_drv.ko`, `wmt_drv.ko`.
4. Ejecución del binario autónomo `wmt_init_complete 0x6631`: identifica el chip MT6631 e inicializa el subsistema.
5. Carga de `wmt_chrdev_wifi.ko` y `wlan_drv_gen4m_6789.ko`.
6. Encendido de la radio escribiendo `1` en `/dev/wmtWifi`.
7. **Auto-registro sintético en udev** (`/run/udev/data/n<ifindex>`): le notifica a NetworkManager que `wlan0` existe y debe ser gestionado (`managed=1`) de inmediato.

---

### 4.5. SUBSISTEMA 05: ARRANQUE, KERNEL Y WATCHDOG PETTER

#### A. Estructura de Arranque Android v4
- `boot_a`: Cabecera v4, kernel descompreso (`Image`), ramdisk de postmarketOS empaquetado (`initramfs.cpio.gz`).
- `vendor_boot_a`: Árbol de dispositivos `pmos_usb_peripheral.dtb` inyectado (fuerza la controladora USB en modo gadget periférico para proveer la red fija `172.16.42.1` por CDC-NCM).

#### B. El Perro Guardián del Silicio (`wdt_pet`)
- *Problema Crítico de Silicio*: El chip MediaTek MT6789 posee un watchdog por hardware (`mtk_wdt`) con un temporizador de 30 segundos. Si ningún proceso alimenta `/dev/watchdog`, la tablet se reinicia abruptamente en bucle.
- *Solución*: `wdt_pet.c`: binario C compilado estático que escanea los descriptores de watchdog y envía periódicamente el ioctl `WDIOC_KEEPALIVE` cada 500 ms. Es el proceso #1 lanzado desde el ramdisk de arranque.

---

## 5. PROCEDIMIENTOS OPERATIVOS Y GUÍA DE USO

### 5.1. Restauración en Vivo (1-Click Restore)
Si durante pruebas en caliente el audio, la batería o la pantalla se desconfiguran:
```bash
# Con la tablet encendida y conectada al cable USB (red 172.16.42.1):
cd /home/danilo/Documentos/TABLET/FUNCIONALIDADES_FINALES
python3 tools/restore_live_tablet.py
```
*Qué hace este comando:*
1. Se conecta por socket TCP al puerto `2323`.
2. Reinstala los daemons y configuraciones de audio, batería, pantalla y Wi-Fi en sus rutas del sistema (`/usr/local/bin`, `/etc/pipewire`, `/etc/phoc.ini`, etc.).
3. Reinicia los servicios y calibra los mezcladores ALSA instantáneamente.

### 5.2. Compilación Global desde Código Fuente
Si se dispone del compilador cruzado `aarch64-linux-gnu-gcc`:
```bash
cd /home/danilo/Documentos/TABLET/FUNCIONALIDADES_FINALES
./build_all.sh
```
*Compila de forma limpia y estática:*
- `battery_daemon` (C)
- `libseat_direct.so` (C con -nostdlib)
- `wmt_init_complete` (C)
- `wdt_pet` (C)

### 5.3. Regeneración de Imágenes y Flasheo de Arranque
Si se requiere actualizar el kernel o el initramfs:
```bash
cd /home/danilo/Documentos/TABLET/FUNCIONALIDADES_FINALES/subsistemas/05_arranque_kernel

# 1. Empaquetar nuevas imágenes boot y vendor_boot:
python3 build_pmos_boot_v13.py

# 2. Flashear por mtkclient en modo BROM (tablet apagada con Vol- o BROM automático):
./flash_pmos_v13_mtkclient.sh
```

### 5.4. Control de Versiones con Git
Este directorio está configurado como un repositorio Git autónomo:
```bash
cd /home/danilo/Documentos/TABLET/FUNCIONALIDADES_FINALES

# Ver estado de archivos modificados:
git status

# Crear una rama para experimentos futuros (ejemplo: aceleración GPU Mali):
git checkout -b feature-gpu-mali-acceleration

# Si un experimento falla y quieres regresar a la versión funcional estable:
git checkout master
git reset --hard v1.0-estable
python3 tools/restore_live_tablet.py
```

---

## 6. TABLA RESUMEN DE COMPONENTES Y CONTROLADORES

| Subsistema | Módulo Kernel Principal | Archivo Fuente / Config | Destino en la Tablet | Estado |
| :--- | :--- | :--- | :--- | :--- |
| **Audio** | `snd_soc_mt6789_afe`, `snd_soc_aw87xxx` | `tablet_audio_daemon.py`, `10-alsa.conf` | `/usr/local/bin/`, `/etc/pipewire/pipewire.conf.d/` | ✅ Estable (0 dB, sin siseo) |
| **Batería** | `mt6358_battery`, `mt6375_charger` | `tablet_battery_daemon.py`, `battery_daemon.c` | `/usr/bin/tablet_battery_daemon.py`, `/sysfs` bind | ✅ Estable (OCV + Rayo de carga) |
| **Pantalla / Touch** | `mediatek_drm`, `tddi_9551` | `libseat_direct.c`, `phoc.ini`, `start_phosh.sh` | `/usr/lib/libseat_direct.so`, `/etc/phoc.ini` | ✅ Estable (Wayland GLES2 180°) |
| **Wi-Fi** | `wmt_drv`, `wlan_drv_gen4m_6789` | `wmt_init_complete.c`, `wmt_wifi_start.sh` | `/usr/bin/wmt_init_complete`, `/dev/wmtWifi` | ✅ Estable (`wlan0` activo) |
| **Kernel / Boot** | `musb_hdrc`, `mtk_wdt` | `wdt_pet.c`, `pmos_usb_peripheral.dtb` | `boot_a`, `vendor_boot_a` | ✅ Estable (Sin reboot de 30s) |
