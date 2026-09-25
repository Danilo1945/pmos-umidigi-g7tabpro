# Subsistema de Batería y Energía - UMIDIGI G7 Tab Pro (MT6789)
**postmarketOS Edge (Kernel 5.10.185-pmos aarch64)**

---

## 1. Arquitectura de Hardware de Alimentación
- **PMIC MT6366 / MT6358**:
  - Incorpora el medidor de combustible (Fuel Gauge `mt6358-gauge`).
  - Nodos sysfs:
    - Voltaje: `/sys/class/power_supply/battery/voltage_now`
    - Corriente instantánea: `/sys/devices/platform/soc/10026000.pwrap/10026000.pwrap:mt6366/mt6358-gauge/FG_Battery_CurrentConsumption`
    - Interfaz UI: `/sys/devices/platform/soc/10026000.pwrap/10026000.pwrap:mt6366/mt6358-gauge/UI_SOC`
- **Cargador Rápido Sub-PMIC MT6375**:
  - Controla la negociación de carga Tipo C (USB-PD / PE+).
  - Estado del cargador:
    - `/sys/class/power_supply/11017000.i2c:mt6375@34:chg/online`
    - `/sys/class/power_supply/11017000.i2c:mt6375@34:chg/status`

---

## 2. Problemas Superados
1. **Porcentaje congelado en 50% o 1%**:
   - En kernels genéricos Android adaptados a Linux mainline/pmos, el daemon del espacio de usuario propietario de MediaTek no corre, dejando `UI_SOC` sin alimentar.
2. **Ausencia del Icono de Carga (Rayo) o Icono Rojo en 0% en Phosh**:
   - El nodo `/sys/class/power_supply/battery/status` permanecía permanentemente en `Discharging` o `Not charging` porque el driver MTK espera órdenes privativas del HAL de Android.
   - En implementaciones previas, se ejecutaba `killall upowerd` al cambiar el estado. Al ser eliminado el proceso D-Bus `upowerd`, Phosh (`battery-manager.c` / `PhoshBatteryManager`) perdía la conexión y no se reconectaba, entrando en fallback visual: icono rojo `battery-missing-symbolic` y porcentaje estático en `0%`.
3. **Fluctuación de porcentaje bajo carga de CPU**:
   - Cuando la CPU/GPU consume corriente pico, la resistencia interna de la celda ($R_{bat} \approx 85\text{ m}\Omega$) provoca una caída de tensión resistiva ($I \cdot R$). Si se lee el voltaje directo, el porcentaje parece descender bruscamente y luego subir.

---

## 3. Solución Implementada (Daemon v4)
- **Compensación IR en Tiempo Real**:
  $$V_{OCV} = V_{bat} - (I_{bat} \cdot R_{int})$$
- **Tabla OCV Calibrada para 7500mAh**:
  Curva de descarga no lineal de 100 puntos enlazada a voltajes entre 4.37V (100%) y 3.00V (1%).
- **Filtro Exponencial IIR**:
  Suavizado $\alpha = 0.2$ para evitar saltos bruscos.
- **Detección Tri-fuente de Carga con Debounce**:
  Combina corriente positiva entrante ($I > 30\text{ mA}$), flag de online de MT6375 y estado de carga filtrado por 2 ciclos estables.
- **Emulación de Status mediante Bind Mount Persistente**:
  Se monta un archivo temporal con `mount --bind /tmp/battery_fake_status /sys/class/power_supply/battery/status` tanto en `/` como en `/sysroot`. No se desmonta agresivamente, sino que se reescribe su contenido atómicamente.
- **Notificación Atómica a UPower sin Matar el Proceso**:
  En lugar de `killall upowerd`, se sintetizan eventos `change` en `/sys/class/power_supply/battery/uevent` y se invoca el método oficial D-Bus `org.freedesktop.UPower.Device.Refresh` vía `busctl`. Esto actualiza Phosh en milisegundos sin cortar la sesión gráfica.
- **Feedback Auditivo**:
  Emisión de sonidos `power-plug.ogg` y `power-unplug.ogg` por PipeWire.

---

## 4. Archivos en este Directorio
- `tablet_battery_daemon.py`: Demonio en producción con audio y bind mount.
- `battery_daemon.c` & `battery_table.h`: Implementación en C de alto rendimiento sin sobrecarga.
- `battery_daemon`: Binario precompilado aarch64.
- `Makefile`: Script para compilar el binario C.
- `install_battery.sh`: Instalador para OpenRC (`/etc/init.d/tablet-battery`).

---

## 5. Restauración Rápida
```bash
sudo /home/danilo/Documentos/TABLET/FUNCIONALIDADES_FINALES/subsistemas/02_bateria_energia/install_battery.sh
```
