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

## 3. Archivos Incluidos
- `libseat_direct.c`: Código fuente en C del bypass de libseat.
- `libseat_direct.so`: Biblioteca compartida compilada aarch64.
- `phoc.ini`: Configuración de geometría, escala y rotación de pantalla.
- `start_phosh.sh`: Script maestro de arranque del entorno de escritorio móvil GNOME/Phosh.
- `Makefile`: Script para compilar `libseat_direct.so`.
- `install_display.sh`: Instalador de componentes al sistema de archivos.

---

## 4. Compilación e Instalación
```bash
# Para compilar libseat_direct.so:
make

# Para instalar en la tablet:
sudo ./install_display.sh
```
