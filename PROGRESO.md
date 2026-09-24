# 📋 BITÁCORA DE PROGRESO Y CONTROL DE SESIONES
## Repositorio: `FUNCIONALIDADES_FINALES` - UMIDIGI G7 Tab Pro (`TG2403GBA`)
### postmarketOS Edge (Kernel 5.10.185-pmos aarch64 - Alpine Linux musl)

---

## 🚀 INSTRUCCIONES DIRECTAS PARA LA PRÓXIMA SESIÓN

> [!IMPORTANT]
> **ORDEN ESTRICTA PARA EL ASISTENTE AL INICIAR LA PRÓXIMA SESIÓN:**
> 1. Lee este archivo [`PROGRESO.md`](file:///home/danilo/Documentos/TABLET/FUNCIONALIDADES_FINALES/PROGRESO.md) y [`DOCUMENTACION_GENERAL.md`](file:///home/danilo/Documentos/TABLET/FUNCIONALIDADES_FINALES/DOCUMENTACION_GENERAL.md).
> 2. Mantén congelada la rama `master` (etiqueta `v1.0-estable`).
> 3. No te vayas por las ramas ni toques configuraciones ya estabilizadas de Audio, Batería o Wi-Fi.
> 4. Concéntrate **al 100% en los dos objetivos clave** descritos a continuación.

---

### 🎯 OBJETIVOS DE LA PRÓXIMA SESIÓN:

#### 1. Aceleración Gráfica por Hardware GLOBAL para TODO el Sistema y Aplicaciones
- **Alcance**: La aceleración NO debe limitarse a una sola app. Debe ser **global a nivel de sistema**:
  - **Compositor Wayland (`phoc` / wlroots)**: Reemplazar el renderizado por software (`WLR_RENDERER=pixman`) por el backend nativo acelerado GLES2/EGL (`mali` / `dri`).
  - **Entorno de Escritorio Phosh / GNOME**: Asegurar aceleración por hardware en la interfaz de usuario, animaciones y panel superior.
  - **Aplicaciones del Sistema (GTK4 / GTK3 / Qt)**: Habilitar el renderizado GPU directo (`GSK_RENDERER=gl`, `QT_QPA_PLATFORM=wayland`).
  - **Navegadores y Multimedia**: Firefox (`MOZ_ENABLE_WAYLAND=1` + WebRender por hardware) para que los videos de YouTube se reproduzcan fluidos a 60 fps sin lag ni saturación de CPU.
- **Hardware Objetivo**: GPU ARM Mali-G57 MC2 (Valhall 1ra gen, SoC MT6789 Helio G99).
- **Controladores involucrados**: `mali_kbase_mt6789.ko`, `/dev/mali0`, biblioteca EGL/GLES (blob Mali oficial o Mesa Panfrost / libmali-valhall).

#### 2. Restauración Definitiva de la Regulación de Brillo de Pantalla (Slider de Phosh)
- **Problema Detectado**: El hardware de la tablet regula el brillo físico en `/sys/class/leds/lcd-backlight/brightness` (rango 0 a 255), pero GNOME/Phosh busca estrictamente en `/sys/class/backlight/*`. Al estar ausente, D-Bus reporta `HasBrightnessControl = false` y el slider de la barra superior desaparece o no actúa.
- **Solución Preparada**: Enlace y bind mount dinámico de `/tmp/fake_backlight/lcd-backlight` hacia `/sys/class/backlight` y `/sysroot/sys/class/backlight` con permisos `666`.
- **Tarea**: Conectar el servicio `gsd-backlight` o vincular la interfaz D-Bus `org.gnome.Shell.Brightness` para que el slider de los Quick Settings de Phosh responda de forma fluida.

---

### 📝 Pasos Inmediatos al Arrancar:
1. **Comprobar la conexión con la tablet**:
   ```bash
   python3 tools/tablet_cmd.py "uptime; uname -a"
   ```
2. **Crear y activar la rama de trabajo en Git**:
   ```bash
   cd /home/danilo/Documentos/TABLET/FUNCIONALIDADES_FINALES
   git checkout -b feature/gpu-global-y-brillo
   ```
3. **Inspeccionar el estado del subsistema de GPU y drivers disponibles**:
   ```bash
   python3 tools/tablet_cmd.py "ls -l /dev/mali* /dev/dri/*; lsmod | grep -E 'mali|gpu|ged'; apk info -vv | grep -i -E 'mesa|mali|vulkan|gles'"
   ```
4. **Verificar regulación de brillo en vivo**:
   ```bash
   python3 tools/tablet_cmd.py "chroot /sysroot brightnessctl -d lcd-backlight set 50%; cat /sys/class/leds/lcd-backlight/brightness"
   ```
5. **Protocolo Anti-Regresión**:
   - Si durante las pruebas de GPU se altera el audio, la pantalla no levanta o se descalibra la batería, ejecutar inmediatamente:
     ```bash
     python3 tools/restore_live_tablet.py
     ```

---

## 📊 ESTADO ACTUAL DE SUBSISTEMAS (Línea Base: `v1.0-estable`)

| Subsistema | Componente | Estado | Versión / Commit | Notas |
| :--- | :--- | :---: | :---: | :--- |
| **01. Audio** | MT6789 AFE + 4x AW87359 | 🟢 **100% OK** | `v1.0-estable` | Ganancias: 32768 (0 dB), Headset: 12 (+2 dB). PipeWire: S16LE, 48kHz, disable-mmap, period-size 6000. Sonido limpio, nítido y sin siseo. |
| **02. Batería** | PMIC MT6366 + MT6375 | 🟢 **100% OK** | `v1.0-estable` | Algoritmo OCV para celda 7500mAh con compensación IR ($85\text{ m}\Omega$). Bind mount dinámico de carga en sysfs con audio feedback. |
| **03. Pantalla/Touch**| MIPI-DSI + TDDI Chipone | 🟢 **100% OK** | `v1.0-estable` | Bypass de sesión `libseat_direct.so` inyectado en Phoc. Orientación invertida 180° aplicada sin latencia. Touch fluido. |
| **04. Wi-Fi** | MediaTek MT6631 / WMT | 🟢 **100% OK** | `v1.0-estable` | Bus WMT inicializado por `wmt_init_complete.c`. Interfaz `wlan0` administrada por NetworkManager. |
| **05. Arranque** | Boot v4 + WDT Petter | 🟢 **100% OK** | `v1.0-estable` | Kernel 5.10.185-pmos estable sin reinicio de 30s gracias a `wdt_pet`. Red USB gadget fija en `172.16.42.1`. |
| **06. Brillo Pantalla**| `lcd-backlight` (PWM) | 🟡 **En Integración**| `v1.0-estable` | Hardware responde (0..255). Se montó `/sys/class/backlight` virtual para que el slider de Phosh lo controle. |
| **07. GPU / Aceleración**| ARM Mali-G57 MC2 | 🟡 **Prioridad Próxima**| - | Actualmente corre con renderizado CPU (`pixman`). Requiere aceleración global GLES2 para eliminar lag en apps y YouTube. |

---

## 📜 HISTORIAL DE SESIONES Y HITOS COMPLETADOS

### Sesión 1: Núcleo de Arranque y Control de Watchdog
- **Hito**: Arranque funcional de postmarketOS Edge en arquitectura A/B Android v4.
- **Logro de Silicio**: Creación de `wdt_pet.c` para resolver el reinicio forzado por hardware de MediaTek a los 30 segundos. Inyección del árbol DTB para forzar modo periférico USB (red `172.16.42.1`).

### Sesión 2: Subsistema Wi-Fi MediaTek WMT
- **Hito**: Activación completa de la tarjeta de red inalámbrica MT6631.
- **Solución**: Creación del binario C `wmt_init_complete.c` para comunicar directamente con `/dev/wmtdetect` y `/dev/stpwmt` sin depender de daemons Bionic de Android. Integración sintética con `udev` y NetworkManager.

### Sesión 3: Pantalla DSI, Touch y Bypass Libseat
- **Hito**: Levantamiento del entorno gráfico GNOME/Phosh sobre compositor Phoc.
- **Solución**: Programación de `libseat_direct.c` en ensamblador AArch64 puro para abrir directamente DRM (`/dev/dri/card0`) y evdev sin systemd-logind ni seatd. Corrección de rotación de fábrica a 180°.

### Sesión 4: Medidor OCV de Batería y Cargador
- **Hito**: Porcentaje de batería en tiempo real y detección del rayo de carga en Phosh.
- **Solución**: Algoritmo de compensación de resistencia interna ($R_{int} = 85\text{ m}\Omega$) contra tabla OCV de 100 puntos y técnica de bind mount sobre `/sys/class/power_supply/battery/status`.

### Sesión 5: Calibración Definitiva de Audio, Repositorio Central y Diagnóstico de Brillo
- **Hitos**:
  1. Identificación y eliminación del siseo/silenciamiento por saturación DRC/AGC en los 4 amplificadores AW87359. Calibración a 0 dB nominal y apagado de reloj parásito I2S3.
  2. Ajuste de buffer DMA en PipeWire a S16LE / 48kHz / disable-mmap / period-size 6000. Reemplazo de reproductor inestable por Amberol. Audio 100% claro y validado por el usuario.
  3. Creación del repositorio `FUNCIONALIDADES_FINALES`, compilador `build_all.sh`, herramienta 1-click `restore_live_tablet.py`, manuales técnicos y etiquetado Git `v1.0-estable`.
  4. Diagnóstico del brillo: resolución de discrepancia entre `/sys/class/leds` y `/sys/class/backlight` para restablecer el slider de Phosh.

---

## 🗺️ HOJA DE RUTA (BACKLOG DE FUTURAS SESIONES)

- [ ] **Fase 1 (Siguiente sesión - Foco Central)**: 
  - Aceleración por Hardware GPU ARM Mali-G57 MC2 **GLOBAL** (Compositor Wayland sin Pixman + Phosh + GTK4/Qt + Firefox/YouTube fluido a 60 fps).
  - Vinculación definitiva del slider de brillo de pantalla en Phosh.
- [ ] **Fase 2**: Decodificación de video por hardware VPU (MediaTek Vcodec / V4L2 M2M para 1080p/4K a 0% de CPU).
- [ ] **Fase 3**: Gestión avanzada de energía, suspensión profunda (Deep Sleep) y reactivación por botón físico Power.
- [ ] **Fase 4**: Soporte de cámaras frontal y trasera (MIPI-CSI) y sensores de orientación física por hardware.
