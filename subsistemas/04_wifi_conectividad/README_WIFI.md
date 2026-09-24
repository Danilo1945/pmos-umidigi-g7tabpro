# Subsistema de Wi-Fi y Conectividad - UMIDIGI G7 Tab Pro (MT6789)
**postmarketOS Edge (Kernel 5.10.185-pmos aarch64)**

---

## 1. Arquitectura de Hardware de Conectividad
- **Combo Chip MediaTek**: MT6631 / MT6635 (Wi-Fi 802.11ac dual-band 2.4/5GHz + BT 5.2 + FM + GPS).
- **Bus de Comunicación WMT**:
  - Enlace serie/SDIO con protocolo propietario STP (Serial Transfer Protocol).
  - Módulos de Kernel requeridos:
    1. `cfg80211.ko` (Subcapa estándar de Linux inalámbrico)
    2. `connadp.ko` (Adaptador de conectividad MediaTek)
    3. `btif_drv.ko` (Driver de interfaz Bluetooth)
    4. `wmt_drv.ko` (Controlador central WMT)
    5. `wmt_chrdev_wifi.ko` (Dispositivo de caracteres para encendido de radio)
    6. `wlan_drv_gen4m_6789.ko` (Driver 802.11 generación 4m)
- **Nodos de Caracteres (`/dev`)**:
  - `/dev/stpwmt` (190, 0)
  - `/dev/wmtdetect` (154, 0)
  - `/dev/wmtWifi` (dinámico según `/sys/class/wmtWifi/wmtWifi/dev`, fallback 504 o 508)
  - `/dev/stpbt` (191, 0)

---

## 2. Desafíos Superados
1. **Ausencia de Daemons Propietarios de Android**:
   - En Android estándar, un daemon nativo Bionic (`wmt_loader` y `wmt_launcher`) negocia la secuencia de baudrates y descarga de microcódigo binario sobre UART/SDIO.
   - En Alpine Linux musl estos daemons fallan por falta de Bionic libc.
2. **Solución Implementada**:
   - Desarrollo de `wmt_init_complete.c`: Un programa C autónomo y estático que envía los ioctls exactos a `/dev/wmtdetect` y `/dev/stpwmt` para identificar el chip como `0x6631`, descargar los parches de firmware `soc2_2_ram_wifi_*` y cambiar el estado del SoC WMT a listo (`READY`).
   - Carga de `wmt_chrdev_wifi` y activación de radio escribiendo `1` en `/dev/wmtWifi`.
   - Auto-registro del dispositivo de red en la base de datos de `udev` (`/run/udev/data/n<ifindex>`) para que `NetworkManager` lo reconozca instantáneamente como gestionado (`managed=1`) sin requerir reinicios.

---

## 3. Archivos Incluidos
- `wmt_init_complete.c`: Código fuente en C del inicializador de hardware WMT.
- `wmt_init_complete`: Binario precompilado estático para AArch64.
- `wmt_launcher` y `wmt_loader`: Binarios auxiliares propietarios para entornos mixtos.
- `wmt_wifi_start.sh`: Script integral de puesta en marcha de la red inalámbrica.
- `Makefile`: Script para compilar `wmt_init_complete`.
- `install_wifi.sh`: Instalador y configurador del servicio OpenRC `mtk-wifi`.

---

## 4. Compilación y Despliegue
```bash
# Compilar si se cuenta con toolchain:
make

# Desplegar en la tablet:
sudo ./install_wifi.sh
```
