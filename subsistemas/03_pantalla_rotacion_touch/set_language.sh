#!/bin/sh
# ==============================================================================
# Selector y Conmutador Rápido de Idioma (Español / Inglés) - postmarketOS Tablet
# ==============================================================================

TARGET="$1"

if [ -z "$TARGET" ]; then
    echo "========================================================"
    echo "       Selector de Idioma / Language Selector           "
    echo "========================================================"
    echo " Uso / Usage:"
    echo "   set_language es     -> Configurar sistema en Español"
    echo "   set_language en     -> Set system to English"
    echo "========================================================"
    if [ -f /etc/locale.conf ]; then
        echo " Idioma actual / Current language:"
        cat /etc/locale.conf
    fi
    exit 0
fi

case "$TARGET" in
    es|ES|spanish|es_ES)
        LOC="es_ES.UTF-8"
        SOURCES="[('xkb', 'es'), ('xkb', 'us')]"
        DESC="Español (con teclado ES + US)"
        ;;
    en|EN|english|en_US)
        LOC="en_US.UTF-8"
        SOURCES="[('xkb', 'us'), ('xkb', 'es')]"
        DESC="English (with US + ES keyboard)"
        ;;
    *)
        echo "[-] Idioma desconocido / Unknown language: $TARGET"
        echo "    Opciones válidas: 'es' o 'en'"
        exit 1
        ;;
esac

echo "[*] Aplicando configuración de idioma: $DESC..."

# 1. /etc/locale.conf
mkdir -p /etc
echo "LANG=$LOC" > /etc/locale.conf
echo "LC_ALL=$LOC" >> /etc/locale.conf
echo "MUSL_LOCPATH=/usr/share/i18n/locales/musl" >> /etc/locale.conf

# 2. /etc/environment
if [ -f /etc/environment ]; then
    grep -v -E '^LANG=|^LC_ALL=|^MUSL_LOCPATH=' /etc/environment > /etc/environment.tmp
    echo "LANG=$LOC" >> /etc/environment.tmp
    echo "LC_ALL=$LOC" >> /etc/environment.tmp
    echo "MUSL_LOCPATH=/usr/share/i18n/locales/musl" >> /etc/environment.tmp
    mv /etc/environment.tmp /etc/environment
fi

# 3. AccountsService para GNOME Settings / Phosh
mkdir -p /var/lib/AccountsService/users
cat << EOF > /var/lib/AccountsService/users/root
[User]
Language=$LOC
FormatsLocale=$LOC
InputSources=xkb:es;xkb:us;
EOF
chmod 600 /var/lib/AccountsService/users/root

# 4. GSettings dinámico si la sesión de Phosh está activa
PHOSH_PID=$(pgrep -f 'phosh -U' | head -n1)
if [ -n "$PHOSH_PID" ] && [ -d "/proc/$PHOSH_PID" ]; then
    DBUS_ADDR=$(tr '\0' '\n' < /proc/$PHOSH_PID/environ 2>/dev/null | grep DBUS_SESSION_BUS_ADDRESS | cut -d= -f2-)
    if [ -n "$DBUS_ADDR" ]; then
        env DBUS_SESSION_BUS_ADDRESS="$DBUS_ADDR" HOME=/root XDG_RUNTIME_DIR=/tmp/runtime-root \
            gsettings set org.gnome.desktop.input-sources sources "$SOURCES" 2>/dev/null || true
        env DBUS_SESSION_BUS_ADDRESS="$DBUS_ADDR" HOME=/root XDG_RUNTIME_DIR=/tmp/runtime-root \
            gsettings set org.gnome.system.locale region "$LOC" 2>/dev/null || true
    fi
fi

sync
echo "[✅] Idioma configurado exitosamente a $LOC."
echo "[*] Reinicia la sesión o la tablet para aplicar las traducciones a todas las aplicaciones abiertas."
