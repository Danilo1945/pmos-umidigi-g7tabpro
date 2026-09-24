# Subsistema de Audio - UMIDIGI G7 Tab Pro (MT6789 / Helio G99)
**postmarketOS Edge (Kernel 5.10.185-pmos aarch64)**

---

## 1. Arquitectura de Hardware
La tablet UMIDIGI G7 Tab Pro cuenta con 4 altavoces estéreo gobernados por dos etapas principales:
1. **DAC / Codec Integrado PMIC MT6366**:
   - Genera las salidas analógicas balanceadas/desbalanceadas para auriculares (`HPL` / `HPR`) y altavoces.
   - Enlace digital AFE: `DL1` (Downlink 1) a 48000 Hz, 16-bit PCM.
2. **Cuádruple Amplificador Inteligente Awinic AW87359**:
   - 4 chips analógicos Clase K gobernados por I2C (Buses I2C 6 y 7, direcciones `0x58` y `0x59`).
   - **Nota técnica crucial**: Los amplificadores AW87359 **NO son DACs I2S**. Reciben audio analógico desde las salidas `HPL`/`HPR` del codec MT6366 y lo amplifican hacia los 4 transductores.

```
[ App / PipeWire ] ---> [ ALSA MT6789 AFE (DL1) ] ---> [ DAC MT6366 (HPL/HPR Mux=2) ]
                                                                 |
                                                          (Señal Analógica)
                                                                 v
                                                    [ 4x AW87359 (I2C 6/7) ]
                                                                 |
                                                                 v
                                                        [ 4x Altavoces ]
```

---

## 2. Problemas Críticos Superados y Causa Raíz

### A. Siseo / Ruido Blanco ("Sasss") y Silenciamiento Repentino
- **Causa 1 (Saturación de Ganancia)**: 
  `ADDA_DL_GAIN` estaba configurado por defecto en `63311` (+14 dB) y `Headset Volume` en `18` (+8 dB). Esta sobreganancia saturaba las entradas de los amplificadores AW87359, disparando su circuito interno de protección AGC/DRC (Auto Gain Control / Dynamic Range Compression). El amplificador silenciaba la música para proteger los parlantes y solo dejaba pasar el suelo de ruido térmico (siseo constante).
- **Causa 2 (Interferencia parásita I2S3)**: 
  Si se activaba el puente digital `I2S3_CH1 DL1_CH1` o `I2S3_CH2 DL1_CH2`, se inyectaba un reloj de sincronización digital parásito sobre las pistas analógicas compartidas del SoC.

### B. Incompatibilidad de Buffer DMA en PipeWire
- **Causa**: PipeWire intentaba abrir ALSA en `S32_LE` (32-bit), modo `MMAP_INTERLEAVED` y period-size de 1024 frames. El controlador DMA de MediaTek MT6789 AFE requiere estrictamente:
  - Formato: `S16_LE` (16-bit little-endian)
  - Modo de acceso: `RW_INTERLEAVED` (sin MMAP directo de kernel)
  - Period-size: `6000` frames
  - Frecuencia: `48000` Hz

### C. Crash del Reproductor de Música (Elisa)
- **Causa**: El reproductor por defecto Elisa (KDE) fallaba con `SIGSEGV` (Signal 11) en Phosh/Wayland bajo Alpine musl.
- **Solución**: Se instaló el reproductor GNOME Mobile oficial **Amberol** (`io.bassi.Amberol`) o el uso de `mpv`.

---

## 3. Calibración Óptima y Definitiva

| Parámetro ALSA | NumID | Valor Óptimo | Función |
| :--- | :--- | :--- | :--- |
| `ADDA_DL_GAIN` | 134 | `32768` (0 dB) | Ganancia de Downlink nominal sin clipping |
| `Headset Volume` | 13 | `12 12` (+2 dB) | Nivel analógico de pre-amplificación |
| `HPL Mux` | 2 | `2` (Audio Playback) | Enruta DL1 al canal izquierdo |
| `HPR Mux` | 3 | `2` (Audio Playback) | Enruta DL1 al canal derecho |
| `I2S3_CH1 DL1_CH1` | 179 | `0` (Off) | Apaga reloj digital parásito |
| `I2S3_CH2 DL1_CH2` | 192 | `0` (Off) | Apaga reloj digital parásito |
| `Audio_Amp_R` / `Audio_Amp_L` | - | `1` (On) | Habilita conmutadores de salida |

---

## 4. Archivos Incluidos en este Módulo
1. `tablet_audio_daemon.py`: Demonio en Python que monitoriza el estado de PipeWire, inicializa los controles ALSA al arranque y reaplica la calibración al cambiar de volumen o silenciar.
2. `10-alsa.conf`: Archivo de configuración para `/etc/pipewire/pipewire.conf.d/10-alsa.conf` que fuerza S16LE, 48kHz, disable-mmap=true y period-size=6000.
3. `50-alsa-fix.conf`: Configuración auxiliar para WirePlumber.
4. `analog-output-headphones.conf`: Mapeo de ruta ALSA/PulseAudio.
5. `volume_keys_daemon.py`: Demonio para captura de eventos evdev de botones físicos de volumen.
6. `install_audio.sh`: Script automatizado para desplegar y activar el subsistema.

---

## 5. Procedimiento de Restauración
Para restaurar el subsistema de audio si se desconfigura:
```bash
# Ejecutar localmente en la tablet o vía tablet_cmd.py:
sudo /home/danilo/Documentos/TABLET/FUNCIONALIDADES_FINALES/subsistemas/01_audio/install_audio.sh
```
O manualmente vía shell remota:
```bash
python3 tablet_cmd.py "python3 /usr/local/bin/tablet_audio_daemon.py &"
```
