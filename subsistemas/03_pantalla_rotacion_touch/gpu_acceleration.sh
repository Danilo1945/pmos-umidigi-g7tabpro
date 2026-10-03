#!/bin/sh
# ==============================================================================
# Configuración Global de Aceleración y Wayland Nativo
# UMIDIGI G7 Tab Pro (SoC Helio G99 MT6789 - GPU ARM Mali-G57 MC2)
# ==============================================================================

# Entorno Wayland Nativo (evitar capas intermedias XWayland lentas)
export GDK_BACKEND=wayland
export QT_QPA_PLATFORM=wayland
export SDL_VIDEODRIVER=wayland
export CLUTTER_BACKEND=wayland
export XDG_SESSION_TYPE=wayland

# Aceleración por Pipeline GL en GTK4 y Qt
export GSK_RENDERER=gl
export QT_QUICK_BACKEND=gl

# Aceleración de Navegador Web (Firefox / Chromium WebRender a 60 fps)
export MOZ_ENABLE_WAYLAND=1

# Optimizaciones Mesa Gallium
export mesa_glthread=true
export LP_NUM_THREADS=8
