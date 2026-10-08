# Subsistema de Pantalla, Rotación y Touch - UMIDIGI G7 Tab Pro
**postmarketOS Edge (Kernel 5.10.185-pmos aarch64)**

---

## 1. Arquitectura de Hardware de Visualización y Entrada
- **Controlador Gráfico DRM/KMS**: `mediatek_drm` (`/dev/dri/card0`).
  - Salida nativa MIPI-DSI: `DSI-1` con resolución nativa de panel de 1200x1920 (orientación de fábrica invertida 180°).
  - Chip de polarización del panel: `aw37503` (Dual-channel AMOLED/LCD power supply).
- **Controlador Táctil (Touchscreen)**:
  - Controlador TDDI / Chipone I2C (`tddi_9551` / `chipone_firmware.bin`).
  - Nodo evdev: `/dev/input/event*` (nombre `chipone-ts` o `mtk-tpd`).
- **Retroiluminación (Backlight)**:
  - `/sys/class/leds/lcd-backlight/brightness` (rango 0 a 255).

---

## 2. Desafíos Superados
1. **Fallo de Gestión de Sesión Wayland (`libseat` / `seatd`)**:
   - `phoc` (compositor wlroots para Phosh) requiere `libseat` para obtener descriptores de archivo privilegiados de DRM y evdev.
   - En Alpine Linux sin `systemd-logind` y con `seatd` fallando en Android kernels por restricciones de IPC/cgroups, Phoc abortaba de inmediato con:
     `[backend/session/session.c:79] Failed to initialize a seat: No suitable seat backend found`.
   - **Solución de Ingeniería**: Se desarrolló `libseat_direct.c`, una implementación pura en C que intercepta la API de `libseat` mediante llamadas al sistema directas (`openat`, `eventfd2`, etc.) en AArch64 con `-nostdlib`. Se inyecta mediante `LD_PRELOAD=/usr/lib/libseat_direct.so`, otorgando acceso directo a `/dev/dri/card0` y `/dev/input/*` de manera instantánea y 100% determinista.
2. **Orientación de Pantalla (Inversión 180°)**:
   - El panel físico DSI viene montado al revés de fábrica. Para corregirlo sin latencia de rotación por software pesado:
     - Configuración fija en `phoc.ini`:
       ```ini
       [output:DSI-1]
       scale=1.0
       rotate=180
       ```
     - Aplicación con `wlr-randr --output DSI-1 --on --mode 1200x1920 --transform 180`.

---

## 3. Gestión de Energía, Bloqueo de Pantalla y Suspensión

1. **Daemon de Control de Hardware (`tablet_power_daemon.py`)**:
   - Captura exclusiva mediante `EVIOCGRAB` sobre `/dev/input/event3` (`mtk-pmic-keys` - Tecla Power) y `/dev/input/event0` (`mtk-kpd` - Teclas de volumen).
   - **Botón Power Físico**:
     - *Pulsación 1 (Apagar/Suspender)*: Bloquea la sesión Phosh (`org.gnome.ScreenSaver.Lock()`), apaga el backlight a 0 nits y conmuta CPUs a `powersave`.
     - *Pulsación 2 (Despertar)*: Conmuta CPUs a `schedutil`, reactiva la salida DRM DSI-1 a 180°, enciende backlight a 200 y despierta la pantalla de bloqueo de Phosh.
   - **Menú de Energía de Phosh ("Suspender" Táctil)**:
     - El daemon expone `org.freedesktop.login1.Manager` en el bus del sistema (`Suspend`, `CanSuspend -> 'yes'`, `PrepareForSleep`) y `org.gnome.SessionManager` en el bus de sesión (`Shutdown`, `Reboot`).
     - Al tocar "Suspender" en la pantalla táctil, Phosh invoca `login1.Manager.Suspend()`, ejecutando el mismo ciclo de reposo profundo.
   - **Rotación y Sensores (`net.hadess.SensorProxy`)**:
     - Expone interfaz de orientación para que el widget de rotación de la barra superior de Phosh conmute entre Portrait (180°) y Landscape (90°/270°).
   - **Credenciales del Sistema**:
     - Contraseña unificada para `danilo` y `root`: **`1234`**.

---

## 4. Archivos Incluidos
- `tablet_power_daemon.py`: Daemon de control para botón de encendido, suspensión D-Bus, volumen y rotación.
- `libseat_direct.c`: Código fuente en C del bypass de libseat con soporte `DRM_IOCTL_SET_MASTER`.
- `libseat_direct.so`: Biblioteca compartida compilada aarch64.
- `phoc.ini`: Configuración de geometría, escala y rotación de pantalla (`rotate=180`).
- `start_phosh.sh`: Script maestro de supervisión y arranque de Phoc/Phosh con variables Wayland y D-Bus.
- `backlight_guard`: Centinela seguro de retroiluminación.
- `Makefile`: Script para compilar `libseat_direct.so`.
- `install_display.sh`: Instalador de componentes al sistema de archivos.

---

## 5. Compilación e Instalación
```bash
# Para compilar libseat_direct.so:
make

# Para instalar en la tablet:
sudo ./install_display.sh
```
