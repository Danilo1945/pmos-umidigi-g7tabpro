#!/usr/bin/env python3
"""
build_pmos_boot_v9.py - Generador de Arranque v9.0 para UMIDIGI G7 Tab Pro (MT6789)
Objetivo v9.0: Arranque de la Interfaz Gráfica (DRM / Panel DSI / Backlight)

Arquitectura v9.0:
1. Conmutación GPIO por CharDev (/bin/gpio_switch):
   * GPIO 157 = 1 (Puerto Type-C exterior activo).
   * GPIO 95  = 0 (Modo periférico/gadget).
   * GPIO 104 = 0 (Pogo pin desacoplado).
2. DTB con dr_mode = 'peripheral':
   * Inicialización autónoma e instantánea de MUSB HDRC como Gadget UDC.
3. Red USB CDC-NCM y Acceso Remoto:
   * Tablet en 172.16.42.1 con DHCP (asigna 172.16.42.2 a la PC).
   * Shell root instantáneo por TCP en puerto 2323 (nc 172.16.42.1 2323).
   * Consola serie interactiva en /dev/ttyGS0 (/dev/ttyACM0 en PC).
4. Watchdog de Silicio Activo:
   * Daemon /bin/wdt_pet alimentando /dev/watchdog0 cada 500ms.
5. Subsistema de Pantalla DRM (/bin/start_display.sh):
   * Fuente +/- 5V AW37503.
   * Driver de Panel DSI L0A9W006C.
   * Backlight PWM y LEDs.
   * Motor de colas CMDQ y Compositor DRM /dev/dri/card0.
"""
import os
import shutil
import struct
import subprocess

# Módulos en lista negra temprana (módulos que provocan deadlocks I2C o cuelgues si se cargan antes de montar sysroot)
BLACKLIST_MODULES = {
    "aee_hangdet.ko",
    "monitor_hang.ko",
    "load_track.ko",
    "meta.ko",
    "tkcore.ko",
    "tkcore_drv.ko",
    "it6112_i2c.ko",
    "adapter_class.ko",             # Cargar en start_phosh.sh para evitar deadlock I2C temprano
    "charger_class.ko",
    "mtk_charger_algorithm_class.ko",
    "mt6375-charger.ko",
    "pd_dbg_info.ko",
    "tcpc_class.ko",
    "tcpc_mt6375.ko",
    "mtk_chg_type_det.ko",
    "rt_pd_manager.ko",
}

# Cadena topológica segura de módulos tempranos (almacenamiento UFS, display DRM, clocks, watchdog e input)
ORDERED_EARLY_MODULES = [
    "mtk_wdt.ko",
    "pinctrl-mtk-v2.ko",
    "pinctrl-mt6789.ko",
    "mrdump.ko",
    "aee_aed.ko",
    # Reguladores esenciales de PMIC y alimentación eMMC / UFS
    "mt6358-regulator.ko",
    "rt5133-regulator.ko",
    "mt6375.ko",
    "mt6375_adc.ko",
    # Clocks y dominios de energía scpsys
    "clk-common.ko",
    "clk-mt6789.ko",
    "clk-mt6789-bus.ko",
    "clk-bringup.ko",
    "clk-chk-mt6789.ko",
    "clk-dbg-mt6789.ko",
    "clk-fmeter-mt6789.ko",
    "clk-mt6789-audiosys.ko",
    "clk-mt6789-cam.ko",
    "clk-mt6789-i2c.ko",
    "clk-mt6789-img.ko",
    "clk-mt6789-impen.ko",
    "clk-mt6789-mfgcfg.ko",
    "clk-mt6789-vcodec.ko",
    "mtk-scpsys.ko",
    "mtk-scpsys-mt6789.ko",
    "mtk-scpsys-bringup.ko",
    "clk-mt6789-mmsys.ko",
    "clk-mt6789-disp_dsc.ko",
    # Seguridad, APC, IOMMU y buses SMI
    "device-apc-common.ko",
    "device-apc-mt6789.ko",
    "iommu_secure.ko",
    "iommu_debug.ko",
    "mtk_iommu.ko",
    "emi-mpu.ko",
    "emi.ko",
    "mtk-smi.ko",
    "mtk-smi-dbg.ko",
    "mtk-socinfo.ko",
    # USB y almacenamiento UFS
    "phy-generic.ko",
    "phy-mtk-tphy.ko",
    "musb_hdrc.ko",
    "musb_main.ko",
    "mtk-spmi-pmic.ko",
    "phy-mtk-ufs.ko",
    "blocktag.ko",
    "ufs-mediatek-dbg.ko",
    "ufs-mediatek-mod.ko",
    # Módulos de pantalla y DRM verificados
    "aw37503.ko",
    "mtk_panel_ext.ko",
    "panel-dk068h5gq-dsi-vdo.ko",
    "panel-l0a9w006c-dsi-vdo.ko",
    "pwm-mtk-disp.ko",
    "leds-mtk.ko",
    "mmprofile.ko",
    "irq-dbg.ko",
    "vcp_status.ko",
    "mtk_sync.ko",
    "mtk_slbc.ko",
    "mtk-icc-core.ko",
    "mtk_dramc.ko",
    "mtk-cmdq-drv-ext.ko",
    "cmdq-platform-mt6789.ko",
    "cmdq_helper_inf.ko",
    "mmqos-common.ko",
    "mtk-mml.ko",
    "mtk_disp_notify.ko",
    "mediatek-drm.ko",
    "leds-mtk-disp.ko",
    # Módulos de entrada (Touchscreen TDDI y teclas físicas)
    "mtk-pmic-keys.ko",
    "mtk-kpd.ko",
    "tddi_9551.ko",
    # Módulos de conectividad base MediaTek
    "cfg80211.ko",
    "connadp.ko",
    "ccci_util_lib.ko",
    "ccci_auxadc.ko",
    "ccmni.ko",
    "mtk_pbm.ko",
    "mtk_mdpm.ko",
    "ccci_md_all.ko",
    "btif_drv.ko",
    "wmt_drv.ko",
]

def align_4k(size):
    page_size = 4096
    return ((size + page_size - 1) // page_size) * page_size

def prepare_initramfs_v9(output_gz_path):
    print("==================================================")
    print(" 1. Preparando Initramfs Modular v9.0           ")
    print("==================================================")
    workdir = "/tmp/pmos_custom_initfs_v9"
    shutil.rmtree(workdir, ignore_errors=True)
    os.makedirs(workdir, exist_ok=True)

    raw_initramfs = "/home/danilo/.local/var/pmbootstrap/chroot_rootfs_umidigi-g7tabpro/boot/initramfs"
    print(f"Extrayendo initramfs base: {raw_initramfs}")
    subprocess.run(f"zcat {raw_initramfs} | cpio -idm 2>/dev/null",
                   cwd=workdir, shell=True, check=True)

    pmos_shared_initfs = "/home/danilo/.local/var/pmbootstrap/chroot_rootfs_umidigi-g7tabpro/usr/share/initramfs"
    for s_file in ["init_2nd.sh", "init_functions_2nd.sh"]:
        src_s = os.path.join(pmos_shared_initfs, s_file)
        if os.path.exists(src_s):
            dst_s = os.path.join(workdir, s_file)
            shutil.copy2(src_s, dst_s)
            os.chmod(dst_s, 0o755)

    vendor_mod_dir = "/home/danilo/Documentos/TABLET/lib/modules"
    if not os.path.exists(vendor_mod_dir):
        vendor_mod_dir = "/tmp/vendor_rd/lib/modules"
        if not os.path.exists(vendor_mod_dir):
            os.makedirs("/tmp/vendor_rd", exist_ok=True)
            vr_cpio = "/home/danilo/Documentos/TABLET_TERMUX/linux_port/unpack_vendor_boot/vendor_ramdisk/ramdisk.cpio"
            subprocess.run(f'cpio -idm "lib/modules/*" < {vr_cpio} 2>/dev/null',
                           shell=True, cwd="/tmp/vendor_rd", check=True)

    target_mod_dir = os.path.join(workdir, "lib/modules")
    os.makedirs(target_mod_dir, exist_ok=True)

    krel = "5.10.185-android12-9-gcc7d44e69095-dirty"
    target_krel_dir = os.path.join(target_mod_dir, krel)
    os.makedirs(target_krel_dir, exist_ok=True)

    copied = 0
    skipped = 0
    for item in os.listdir(vendor_mod_dir):
        if item in BLACKLIST_MODULES:
            skipped += 1
            continue
        s = os.path.join(vendor_mod_dir, item)
        if os.path.isfile(s):
            shutil.copy2(s, os.path.join(target_mod_dir, item))
            shutil.copy2(s, os.path.join(target_krel_dir, item))
            copied += 1
    print(f"  Módulos copiados: {copied}  |  Omitidos (blacklist): {skipped}")

    # Copiar firmwares oficiales (Chipone TDDI y Wi-Fi) al initramfs
    target_fw_dir = os.path.join(workdir, "lib/firmware")
    os.makedirs(target_fw_dir, exist_ok=True)
    host_fw_dir = "/home/danilo/Documentos/TABLET/lib/firmware"
    fw_copied = 0
    if os.path.exists(host_fw_dir):
        for fw in os.listdir(host_fw_dir):
            src_fw = os.path.join(host_fw_dir, fw)
            if os.path.isfile(src_fw) and os.path.getsize(src_fw) > 0:
                shutil.copy2(src_fw, os.path.join(target_fw_dir, fw))
                fw_copied += 1
    print(f"  [+] Firmwares embebidos en initramfs: {fw_copied} archivos en {target_fw_dir}")

    # Copiar bundle android_compat oficial (linker64, wmt_loader, wmt_launcher y librerías)
    host_compat_dir = "/home/danilo/Documentos/TABLET/android_compat"
    target_compat_dir = os.path.join(workdir, "android_compat")
    if os.path.exists(host_compat_dir):
        shutil.copytree(host_compat_dir, target_compat_dir, dirs_exist_ok=True)
        for b in ["linker64", "wmt_loader", "wmt_launcher"]:
            bp = os.path.join(target_compat_dir, "bin", b)
            if os.path.exists(bp):
                os.chmod(bp, 0o755)
        print(f"  [+] Bundle android_compat embebido exitosamente en {target_compat_dir}")

    # Crear rutas y enlaces de firmware estándar de Android que busca wmt_launcher
    for ad in ["vendor/firmware", "system/etc/firmware", "system/vendor/firmware", "data/vendor/stp_dump", "data/vendor/connsyslog"]:
        os.makedirs(os.path.join(workdir, ad), exist_ok=True)
    if os.path.exists(host_fw_dir):
        for fw in os.listdir(host_fw_dir):
            for ad in ["vendor/firmware", "system/etc/firmware", "system/vendor/firmware"]:
                dst = os.path.join(workdir, ad, fw)
                if not os.path.exists(dst):
                    try:
                        os.symlink(os.path.join("/lib/firmware", fw), dst)
                    except Exception:
                        pass
        # Asegurar WMT.cfg apuntando a WMT_SOC.cfg
        for base_d in ["lib/firmware", "vendor/firmware", "system/etc/firmware", "system/vendor/firmware"]:
            wmt_cfg = os.path.join(workdir, base_d, "WMT.cfg")
            if not os.path.exists(wmt_cfg):
                try:
                    os.symlink("WMT_SOC.cfg", wmt_cfg)
                except Exception:
                    pass

    # Parche verificado en musb_hdrc.ko para forzar pullup por hardware
    for target_dir in [target_mod_dir, target_krel_dir]:
        mod_file = os.path.join(target_dir, "musb_hdrc.ko")
        if os.path.exists(mod_file):
            with open(mod_file, "r+b") as f:
                f.seek(0x18070)
                curr = f.read(4)
                if curr == b"\xc8\x01\x00\x34":
                    f.seek(0x18070)
                    f.write(b"\x1f\x20\x03\xd5")
                    print(f"  [+] Parche SOFT_CONN aplicado a {mod_file}")

    for mf in ["modules.alias", "modules.softdep", "modules.load", "modules.load.recovery"]:
        src = os.path.join(vendor_mod_dir, mf)
        if os.path.exists(src):
            shutil.copy2(src, os.path.join(target_mod_dir, mf))
            shutil.copy2(src, os.path.join(target_krel_dir, mf))

    # Guardar lista temprana
    early_list_path = os.path.join(workdir, "lib/modules/modules.early.order")
    with open(early_list_path, "w") as f:
        f.write("\n".join(ORDERED_EARLY_MODULES) + "\n")
    print(f"  [+] Lista topológica escrita: {len(ORDERED_EARLY_MODULES)} módulos en {early_list_path}")

    subprocess.run(f"depmod -b {workdir} {krel} 2>/dev/null || true", shell=True)
    dep_src = os.path.join(target_krel_dir, "modules.dep")
    if os.path.exists(dep_src):
        shutil.copy2(dep_src, os.path.join(target_mod_dir, "modules.dep"))

    # Symlinks para kmod y utilidades del sistema
    for d in ["boot", "sysroot", "bin", "sbin", "usr/bin", "usr/sbin"]:
        os.makedirs(os.path.join(workdir, d), exist_ok=True)
        for tool in ["insmod", "modprobe", "rmmod", "lsmod"]:
            ln = os.path.join(workdir, d, tool)
            if not os.path.exists(ln):
                try:
                    os.symlink("/usr/bin/kmod", ln)
                except Exception:
                    pass

    sym_old = os.path.join(target_mod_dir, "5.10.185-android12-9-00001-gc475c0851364-ab10989012")
    if not os.path.exists(sym_old):
        os.symlink(".", sym_old)
    sym1 = os.path.join(target_mod_dir, krel)
    if not os.path.exists(sym1):
        os.symlink(".", sym1)

    v9_dir = "/home/danilo/Documentos/TABLET/version_v9"
    v13_dir = "/home/danilo/Documentos/TABLET/version_v13"
    # wdt_pet
    wdt_bin_src = os.path.join(v13_dir, "wdt_pet") if os.path.exists(os.path.join(v13_dir, "wdt_pet")) else os.path.join(v9_dir, "wdt_pet")
    if os.path.exists(wdt_bin_src):
        wdt_bin_dst = os.path.join(workdir, "bin", "wdt_pet")
        shutil.copy2(wdt_bin_src, wdt_bin_dst)
        os.chmod(wdt_bin_dst, 0o755)
        print(f"  [+] wdt_pet instalado en {wdt_bin_dst}")

    # gpio_switch
    gpio_switch_src = os.path.join(v13_dir, "gpio_switch") if os.path.exists(os.path.join(v13_dir, "gpio_switch")) else os.path.join(v9_dir, "gpio_switch")
    if os.path.exists(gpio_switch_src):
        gpio_switch_dst = os.path.join(workdir, "bin", "gpio_switch")
        shutil.copy2(gpio_switch_src, gpio_switch_dst)
        os.chmod(gpio_switch_dst, 0o755)
        print(f"  [+] gpio_switch nativo instalado en {gpio_switch_dst}")

    # drm_splash (Utilidad KMS nativa AArch64)
    drm_splash_src = os.path.join(v9_dir, "drm_splash")
    if os.path.exists(drm_splash_src):
        drm_splash_dst = os.path.join(workdir, "bin", "drm_splash")
        shutil.copy2(drm_splash_src, drm_splash_dst)
        os.chmod(drm_splash_dst, 0o755)
        print(f"  [+] drm_splash instalado en {drm_splash_dst}")

    # wmt_init_complete (Utilidad de inicialización Wi-Fi MediaTek WMT)
    wmt_bin_src = "/home/danilo/Documentos/TABLET/version_v13/wmt_init_complete"
    if os.path.exists(wmt_bin_src):
        wmt_bin_dst = os.path.join(workdir, "bin", "wmt_init_complete")
        shutil.copy2(wmt_bin_src, wmt_bin_dst)
        os.chmod(wmt_bin_dst, 0o755)
        print(f"  [+] wmt_init_complete instalado en {wmt_bin_dst}")

    # backlight_guard (Supervisor Anti-UVLO para estabilización de hardware)
    guard_bin_src = "/home/danilo/Documentos/TABLET/FUNCIONALIDADES_FINALES/subsistemas/03_pantalla_rotacion_touch/backlight_guard"
    if os.path.exists(guard_bin_src):
        guard_bin_dst = os.path.join(workdir, "bin", "backlight_guard")
        shutil.copy2(guard_bin_src, guard_bin_dst)
        os.chmod(guard_bin_dst, 0o755)
        print(f"  [+] backlight_guard instalado en {guard_bin_dst}")

    # wmt_wifi_start.sh (Script integral de encendido de Wi-Fi con android_compat y STP)
    wmt_wifi_sh = os.path.join(workdir, "bin", "wmt_wifi_start.sh")
    with open(wmt_wifi_sh, "w") as f:
        f.write(r"""#!/bin/sh
echo "=========================================================="
echo "  Inicializando Wi-Fi MediaTek MT6631 (WMT + STP + gen4m) "
echo "=========================================================="

# 1. Nodos de comunicacion estaticos
[ ! -c /dev/stpwmt ] && mknod -m 666 /dev/stpwmt c 190 0
[ ! -c /dev/wmtdetect ] && mknod -m 666 /dev/wmtdetect c 154 0
[ ! -c /dev/wmtWifi ] && mknod -m 666 /dev/wmtWifi c 508 0
[ ! -c /dev/stpbt ] && mknod -m 666 /dev/stpbt c 191 0
[ ! -c /dev/stpgps ] && mknod -m 666 /dev/stpgps c 192 0
[ ! -c /dev/fm ] && mknod -m 666 /dev/fm c 193 0
chmod 666 /dev/stpwmt /dev/wmtdetect /dev/wmtWifi /dev/stpbt /dev/stpgps /dev/fm 2>/dev/null || true

# 2. Enlaces esperados por el firmware
mkdir -p /vendor/firmware /system/etc/firmware /system/vendor/firmware /data/vendor/stp_dump /data/vendor/connsyslog /proc/driver 2>/dev/null || true
for fw in /lib/firmware/*; do
    [ -e "$fw" ] || continue
    fname=$(basename "$fw")
    [ ! -e "/vendor/firmware/$fname" ] && ln -sf "$fw" "/vendor/firmware/$fname" 2>/dev/null || true
    [ ! -e "/system/etc/firmware/$fname" ] && ln -sf "$fw" "/system/etc/firmware/$fname" 2>/dev/null || true
    [ ! -e "/system/vendor/firmware/$fname" ] && ln -sf "$fw" "/system/vendor/firmware/$fname" 2>/dev/null || true
done
[ ! -e /lib/firmware/WMT.cfg ] && ln -sf /lib/firmware/WMT_SOC.cfg /lib/firmware/WMT.cfg 2>/dev/null || true
[ ! -e /vendor/firmware/WMT.cfg ] && ln -sf /lib/firmware/WMT_SOC.cfg /vendor/firmware/WMT.cfg 2>/dev/null || true

# 3. Asegurar driver base WMT cargado
load_m() {
    local m="$1"
    local base="${m%.ko}"
    base="${base//-/_}"
    /sbin/modprobe "$base" 2>/dev/null || \
    insmod "/lib/modules/$m" 2>/dev/null || \
    insmod "/lib/modules/5.10.185-android12-9-gcc7d44e69095-dirty/$m" 2>/dev/null || true
}
load_m cfg80211.ko
load_m connadp.ko
load_m btif_drv.ko
load_m wmt_drv.ko

# 4. Localizar bundle android_compat
COMPAT="/android_compat"
[ ! -d "$COMPAT" ] && [ -d "/sysroot/android_compat" ] && COMPAT="/sysroot/android_compat"

# 4. Inicializacion de hardware y deteccion de chip MT6631
if [ -x /bin/wmt_init_complete ]; then
    echo "[1/4] Ejecutando wmt_init_complete (MT6631 / MT6789)..."
    /bin/wmt_init_complete 0x6631 >/tmp/wmt_init_complete.log 2>&1 || true
    echo "    -> wmt_init_complete finalizado (ret=$?)"
fi

# 5. Lanzar wmt_launcher oficial parcheado en segundo plano
if [ -x "$COMPAT/bin/linker64" ] && [ -x "$COMPAT/bin/wmt_launcher" ]; then
    echo "[2/4] Lanzando daemon wmt_launcher..."
    killall -9 wmt_launcher 2>/dev/null || true
    LD_LIBRARY_PATH="$COMPAT/lib64" "$COMPAT/bin/linker64" "$COMPAT/bin/wmt_launcher" -p /lib/firmware/ -o 1 >/tmp/wmt_launcher.log 2>&1 &
    sleep 2
fi

# 6. Cargar controladores de interfaz Wi-Fi
echo "[3/4] Cargando módulos de red Wi-Fi (wmt_chrdev_wifi + wlan_drv)..."
load_m wmt_chrdev_wifi.ko
sleep 0.5

# Asegurar creacion de nodo /dev/wmtWifi con major dinamico real
if [ -f /sys/class/wmtWifi/wmtWifi/dev ]; then
    maj_min=$(cat /sys/class/wmtWifi/wmtWifi/dev 2>/dev/null)
    rm -f /dev/wmtWifi
    mknod -m 666 /dev/wmtWifi c ${maj_min%:*} ${maj_min#*:} 2>/dev/null || true
else
    [ ! -c /dev/wmtWifi ] && mknod -m 666 /dev/wmtWifi c 504 0 2>/dev/null || true
fi
chmod 666 /dev/wmtWifi 2>/dev/null || true

load_m wlan_drv_gen4m_6789.ko
sleep 1.5

# 7. Encender radio Wi-Fi vía CharDev
echo "[4/4] Encendiendo radio Wi-Fi (/dev/wmtWifi)..."
echo 1 > /dev/wmtWifi 2>/dev/null || true
sleep 2.5

# 8. Verificar interfaz wlan0 e integrar con NetworkManager / udev
if ip link show wlan0 >/dev/null 2>&1; then
    echo "🎉 ¡INTERFAZ wlan0 ACTIVA Y DISPONIBLE!"
    ip link set wlan0 up 2>/dev/null || true
    ip link show wlan0

    # Auto-registro en udev para que NetworkManager lo administre
    if [ -f /sys/class/net/wlan0/ifindex ]; then
        wlan_ifidx=$(cat /sys/class/net/wlan0/ifindex 2>/dev/null)
        for udir in /run/udev/data /sysroot/run/udev/data; do
            mkdir -p "$udir" 2>/dev/null || true
            cat << EOF_UDEV_N > "$udir/n$wlan_ifidx"
I:243319448
E:DEVPATH=/devices/platform/soc/18000000.wifi/net/wlan0
E:DEVTYPE=wlan
E:ID_NET_DRIVER=wlan
E:IFINDEX=$wlan_ifidx
E:INTERFACE=wlan0
E:SUBSYSTEM=net
E:SYSTEMD_READY=1
E:USEC_INITIALIZED=243319448
TAGS=:systemd:
EOF_UDEV_N
        done
    fi

    # Configuración de wpa_supplicant y NetworkManager en sysroot
    mkdir -p /sysroot/etc/wpa_supplicant /sysroot/run/wpa_supplicant 2>/dev/null || true
    if [ ! -f /sysroot/etc/wpa_supplicant/wpa_supplicant.conf ]; then
        cat << 'EOF_WPA' > /sysroot/etc/wpa_supplicant/wpa_supplicant.conf
ctrl_interface=/run/wpa_supplicant
ctrl_interface_group=0
update_config=1
EOF_WPA
    fi

    mkdir -p /sysroot/etc/NetworkManager/conf.d 2>/dev/null || true
    cat << 'EOF_NM' > /sysroot/etc/NetworkManager/conf.d/10-manage-wifi.conf
[device-wifi]
match-device=interface-name:wlan*
managed=1
EOF_NM

    # Iniciar servicios en sysroot si aún no corren
    if [ -d /sysroot/usr/sbin ]; then
        if ! chroot /sysroot pgrep -x wpa_supplicant >/dev/null 2>&1; then
            chroot /sysroot /usr/sbin/wpa_supplicant -u -B -c /etc/wpa_supplicant/wpa_supplicant.conf 2>/dev/null || true
        fi
        sleep 1
        if ! chroot /sysroot pgrep -x NetworkManager >/dev/null 2>&1; then
            chroot /sysroot /usr/sbin/NetworkManager 2>/dev/null &
        fi
    fi
else
    echo "[-] wlan0 no aparecio de inmediato. Revisando dmesg:"
    dmesg | grep -i -E "wmt|wlan|6631" | tail -n 20
fi
""")
    os.chmod(wmt_wifi_sh, 0o755)
    print(f"  [+] Script wmt_wifi_start.sh instalado en {wmt_wifi_sh}")

    # libseat_direct.so y start_phosh.sh para GNOME / Phosh
    v13_dir = "/home/danilo/Documentos/TABLET/version_v13"
    lsd_src = os.path.join(v13_dir, "libseat_direct.so")
    if os.path.exists(lsd_src):
        os.makedirs(os.path.join(workdir, "bin"), exist_ok=True)
        os.makedirs(os.path.join(workdir, "lib"), exist_ok=True)
        shutil.copy2(lsd_src, os.path.join(workdir, "bin", "libseat_direct.so"))
        shutil.copy2(lsd_src, os.path.join(workdir, "lib", "libseat_direct.so"))
        print(f"  [+] libseat_direct.so instalado en initramfs")
    
    sph_src = os.path.join(v13_dir, "start_phosh.sh")
    if os.path.exists(sph_src):
        os.makedirs(os.path.join(workdir, "bin"), exist_ok=True)
        shutil.copy2(sph_src, os.path.join(workdir, "bin", "start_phosh.sh"))
        os.chmod(os.path.join(workdir, "bin", "start_phosh.sh"), 0o755)
        print(f"  [+] start_phosh.sh instalado en initramfs (con GPU Mali y sin cargador I2C)")

    bat_daemon_src = os.path.join(v13_dir, "battery_daemon")
    if os.path.exists(bat_daemon_src):
        os.makedirs(os.path.join(workdir, "bin"), exist_ok=True)
        shutil.copy2(bat_daemon_src, os.path.join(workdir, "bin", "battery_daemon"))
        os.chmod(os.path.join(workdir, "bin", "battery_daemon"), 0o755)
        print(f"  [+] battery_daemon instalado en initramfs")

    phoc_src = os.path.join(v13_dir, "phoc.ini")
    if os.path.exists(phoc_src):
        os.makedirs(os.path.join(workdir, "etc"), exist_ok=True)
        shutil.copy2(phoc_src, os.path.join(workdir, "etc", "phoc.ini"))
        print(f"  [+] phoc.ini instalado en initramfs")

    audio_daemon_src = os.path.join(v13_dir, "tablet_audio_daemon.py")
    if os.path.exists(audio_daemon_src):
        os.makedirs(os.path.join(workdir, "bin"), exist_ok=True)
        shutil.copy2(audio_daemon_src, os.path.join(workdir, "bin", "tablet_audio_daemon.py"))
        os.chmod(os.path.join(workdir, "bin", "tablet_audio_daemon.py"), 0o755)
        print(f"  [+] tablet_audio_daemon.py instalado en initramfs")

    alsa_conf_src = os.path.join(v13_dir, "50-alsa-fix.conf")
    if os.path.exists(alsa_conf_src):
        os.makedirs(os.path.join(workdir, "etc", "wireplumber"), exist_ok=True)
        shutil.copy2(alsa_conf_src, os.path.join(workdir, "etc", "wireplumber", "50-alsa-fix.conf"))
        print(f"  [+] 50-alsa-fix.conf instalado en initramfs")

    hp_conf_src = os.path.join(v13_dir, "analog-output-headphones.conf")
    if os.path.exists(hp_conf_src):
        os.makedirs(os.path.join(workdir, "etc", "alsa-card-profile"), exist_ok=True)
        shutil.copy2(hp_conf_src, os.path.join(workdir, "etc", "alsa-card-profile", "analog-output-headphones.conf"))
        print(f"  [+] analog-output-headphones.conf instalado en initramfs")

    pw_conf_src = os.path.join(v13_dir, "10-alsa.conf")
    if os.path.exists(pw_conf_src):
        os.makedirs(os.path.join(workdir, "etc", "pipewire"), exist_ok=True)
        shutil.copy2(pw_conf_src, os.path.join(workdir, "etc", "pipewire", "10-alsa.conf"))
        print(f"  [+] 10-alsa.conf (PipeWire S16LE/48kHz/disable-mmap) instalado en initramfs")

    # start_phosh_service.sh (Servicio de Phosh Wayland y GPU Mali)
    phosh_service_src = "/home/danilo/Documentos/TABLET/FUNCIONALIDADES_FINALES/subsistemas/03_pantalla_rotacion_touch/start_phosh_service.sh"
    if os.path.exists(phosh_service_src):
        phosh_service_dst = os.path.join(workdir, "bin", "start_phosh_service.sh")
        shutil.copy2(phosh_service_src, phosh_service_dst)
        os.chmod(phosh_service_dst, 0o755)
        print(f"  [+] start_phosh_service.sh instalado en {phosh_service_dst}")

    # Neutralizaciones para evitar que pmOS desmonte el USB Gadget o aborte
    for fn_file in ["init_functions.sh", "init_functions_2nd.sh", "usr/share/initramfs/init_functions.sh"]:
        target_fn = os.path.join(workdir, fn_file)
        if os.path.exists(target_fn):
            with open(target_fn, "a") as f:
                f.write("\n# Neutralizaciones v9 para mantener Gadget USB y shell perpetuos\n")
                f.write("setup_usb_network() { return 0; }\n")
                f.write("setup_usb_network_configfs() { return 0; }\n")
                f.write("setup_usb_configfs_udc() { return 0; }\n")
                f.write("export_logs() { return 0; }\n")
                f.write("mount_subpartitions() {\n")
                f.write("    for i in $(seq 1 75); do\n")
                f.write("        for cand in /dev/sdc61 /dev/userdata /dev/block/by-name/userdata; do\n")
                f.write("            if [ -b \"$cand\" ]; then\n")
                f.write("                losetup -P /dev/loop0 \"$cand\" 2>/dev/null || true\n")
                f.write("                sleep 0.3\n")
                f.write("                [ -f /sys/class/block/loop0p1/dev ] && { maj_min=$(cat /sys/class/block/loop0p1/dev); mknod /dev/loop0p1 b ${maj_min%:*} ${maj_min#*:} 2>/dev/null || true; }\n")
                f.write("                [ -f /sys/class/block/loop0p2/dev ] && { maj_min=$(cat /sys/class/block/loop0p2/dev); mknod /dev/loop0p2 b ${maj_min%:*} ${maj_min#*:} 2>/dev/null || true; }\n")
                f.write("                if [ -b /dev/loop0p1 ] && [ -b /dev/loop0p2 ]; then\n")
                f.write("                    export PMOS_BOOT=/dev/loop0p1\n")
                f.write("                    export PMOS_ROOT=/dev/loop0p2\n")
                f.write("                    mkdir -p /dev/disk/by-label /dev/disk/by-partlabel 2>/dev/null || true\n")
                f.write("                    ln -sf /dev/loop0p1 /dev/disk/by-label/pmOS_boot 2>/dev/null || true\n")
                f.write("                    ln -sf /dev/loop0p2 /dev/disk/by-label/pmOS_root 2>/dev/null || true\n")
                f.write("                    echo '>> [pmOS-v9] Subparticiones loop0p1 y loop0p2 listas!' > /dev/kmsg\n")
                f.write("                    return 0\n")
                f.write("                fi\n")
                f.write("            fi\n")
                f.write("        done\n")
                f.write("        populate_block_nodes 2>/dev/null || true\n")
                f.write("        sleep 1\n")
                f.write("    done\n")
                f.write("    return 0\n")
                f.write("}\n")
                f.write("resize_root_partition() { return 0; }\n")
                f.write("resize_root_filesystem() { return 0; }\n")
                f.write("fail_halt_boot() {\n")
                f.write("    echo '[pmOS-v9] Debug Shell & DRM activo permanentemente.'\n")
                f.write("    while true; do sleep 3600; done\n")
                f.write("}\n")

    # Parchear udevadm settle con timeout en funciones de 2nd stage
    for fn2_file in ["init_functions_2nd.sh", "usr/share/initramfs/init_functions_2nd.sh"]:
        target_fn2 = os.path.join(workdir, fn2_file)
        if os.path.exists(target_fn2):
            with open(target_fn2, "r") as f:
                f2_data = f.read()
            f2_data = f2_data.replace("udevadm settle", "udevadm settle --timeout=3 2>/dev/null || true")
            with open(target_fn2, "w") as f:
                f2_data = f.write(f2_data)

    # Inyección de correcciones automáticas de KDE Plasma Mobile, Display y Consolas en init_2nd.sh
    for init2_file in ["init_2nd.sh", "usr/share/initramfs/init_2nd.sh"]:
        target_i2 = os.path.join(workdir, init2_file)
        if os.path.exists(target_i2):
            with open(target_i2, "r") as f:
                i2_content = f.read()
            patch_sysroot = r"""mount_root_partition
echo ">> [pmOS-v9.1] Configurando Systemd, KDE Plasma Mobile, Display y Shell..." > /dev/kmsg

# 1. Copiar binarios nativos a sysroot
cp -f /bin/drm_splash /sysroot/usr/bin/drm_splash 2>/dev/null || true
cp -f /bin/wdt_pet /sysroot/usr/bin/wdt_pet 2>/dev/null || true
cp -f /bin/gpio_switch /sysroot/usr/bin/gpio_switch 2>/dev/null || true
cp -f /bin/wmt_init_complete /sysroot/usr/bin/wmt_init_complete 2>/dev/null || true
cp -f /bin/wmt_wifi_start.sh /sysroot/usr/bin/wmt_wifi_start.sh 2>/dev/null || true
cp -f /bin/start_plasma_mobile.sh /sysroot/usr/bin/start_plasma_mobile.sh 2>/dev/null || true
cp -f /bin/backlight_guard /sysroot/usr/bin/backlight_guard 2>/dev/null || true
cp -f /bin/backlight_guard /sysroot/bin/backlight_guard 2>/dev/null || true
cp -f /bin/start_phosh_service.sh /sysroot/usr/bin/start_phosh_service.sh 2>/dev/null || true
chmod 755 /sysroot/usr/bin/drm_splash /sysroot/usr/bin/wdt_pet /sysroot/usr/bin/gpio_switch /sysroot/usr/bin/wmt_init_complete /sysroot/usr/bin/wmt_wifi_start.sh /sysroot/usr/bin/start_plasma_mobile.sh /sysroot/usr/bin/backlight_guard /sysroot/bin/backlight_guard /sysroot/usr/bin/start_phosh_service.sh 2>/dev/null || true
mknod /sysroot/dev/mali0 c 10 116 2>/dev/null || true
chmod 666 /sysroot/dev/mali0 2>/dev/null || true

# Copiar bundle android_compat y firmwares oficiales a sysroot
if [ -d /android_compat ]; then
    mkdir -p /sysroot/android_compat 2>/dev/null || true
    cp -a /android_compat/* /sysroot/android_compat/ 2>/dev/null || true
fi
mkdir -p /sysroot/lib/firmware 2>/dev/null || true
cp -a /lib/firmware/* /sysroot/lib/firmware/ 2>/dev/null || true

# 2. Configurar usuario y grupos
echo "danilo" > /sysroot/etc/default_user 2>/dev/null || true
grep -q "^danilo:" /sysroot/etc/passwd 2>/dev/null || echo "danilo:x:10000:10000::/home/danilo:/bin/ash" >> /sysroot/etc/passwd 2>/dev/null || true
for grp in video input render tty wheel audio dialout disk; do
    grep -q "^$grp:" /sysroot/etc/group && sed -i "s/^$grp:.*$/&,danilo,root/" /sysroot/etc/group 2>/dev/null || true
done

# 3. Watchdog Daemon permanente y neutralización del reinicio de systemd
mkdir -p /sysroot/dev /sysroot/etc/systemd/system.conf.d /sysroot/etc/systemd/system/sysinit.target.wants 2>/dev/null || true
rm -f /sysroot/etc/systemd/system.conf.d/01-watchdog.conf 2>/dev/null || true

cat << 'EOF_SC' > /sysroot/etc/systemd/system.conf.d/01-watchdog.conf
[Manager]
RuntimeWatchdogSec=0
RebootWatchdogSec=0
KExecWatchdogSec=0
EOF_SC

cat << 'EOF_WDT' > /sysroot/etc/systemd/system/wdt-pet.service
[Unit]
Description=Silicon Watchdog Pet Daemon
DefaultDependencies=no
Before=basic.target
[Service]
Type=simple
ExecStart=/bin/sh -c 'while true; do [ -e /dev/watchdog0 ] && printf "\\x01" > /dev/watchdog0 2>/dev/null; [ -e /dev/watchdog ] && printf "\\x01" > /dev/watchdog 2>/dev/null; sleep 1; done'
Restart=always
RestartSec=1s
[Install]
WantedBy=sysinit.target
EOF_WDT
ln -sf /etc/systemd/system/wdt-pet.service /sysroot/etc/systemd/system/sysinit.target.wants/wdt-pet.service 2>/dev/null || true

# 4. Asegurar Backlight apagado (0) para máxima velocidad de recarga y ahorro
echo 0 > /sysroot/sys/class/leds/lcd-backlight/brightness 2>/dev/null || true

# 5. Shell Root Directo en Consola Serie (/dev/ttyGS0 -> /dev/ttyACM0 en PC)
mkdir -p /sysroot/etc/systemd/system/multi-user.target.wants 2>/dev/null || true
cat << 'EOF_SER' > /sysroot/etc/systemd/system/pmos-serial-shell.service
[Unit]
Description=pmOS Direct Serial Root Shell (ttyGS0)
After=dev-ttyGS0.device
[Service]
Type=simple
ExecStart=/bin/sh -c 'exec /bin/sh </dev/ttyGS0 >/dev/ttyGS0 2>&1'
Restart=always
RestartSec=1s
[Install]
WantedBy=multi-user.target
EOF_SER
ln -sf /etc/systemd/system/pmos-serial-shell.service /sysroot/etc/systemd/system/multi-user.target.wants/pmos-serial-shell.service 2>/dev/null || true

# 6. Red USB Periférica (IP 172.16.42.1 + DHCP + Telnet Root en puerto 23)
cat << 'EOF_NET' > /sysroot/etc/systemd/system/pmos-usb-network.service
[Unit]
Description=pmOS USB Network & Root Telnet Daemon
After=network.target
Wants=network.target
[Service]
Type=simple
ExecStart=/bin/sh -c 'ip link set usb0 up 2>/dev/null; ip addr add 172.16.42.1/24 dev usb0 2>/dev/null; /usr/bin/unudhcpd -i usb0 -s 172.16.42.1 -c 172.16.42.2 2>/dev/null || true; exec /bin/busybox-extras telnetd -F -p 23 -l /bin/sh'
Restart=always
RestartSec=2s
[Install]
WantedBy=multi-user.target
EOF_NET
ln -sf /etc/systemd/system/pmos-usb-network.service /sysroot/etc/systemd/system/multi-user.target.wants/pmos-usb-network.service 2>/dev/null || true

# 6.5 Servicio Autónomo de Wi-Fi MediaTek MT6631
cat << 'EOF_WIFI' > /sysroot/etc/systemd/system/pmos-wifi.service
[Unit]
Description=pmOS MediaTek Wi-Fi Daemon
After=network.target
[Service]
Type=oneshot
ExecStart=/usr/bin/wmt_wifi_start.sh
RemainAfterExit=yes
[Install]
WantedBy=multi-user.target
EOF_WIFI
ln -sf /etc/systemd/system/pmos-wifi.service /sysroot/etc/systemd/system/multi-user.target.wants/pmos-wifi.service 2>/dev/null || true

# 7. Override optimizado de KDE Plasma Mobile
mkdir -p /sysroot/etc/systemd/system/plasma-mobile.service.d 2>/dev/null || true
cat << 'EOF_PM' > /sysroot/etc/systemd/system/plasma-mobile.service.d/override.conf
[Unit]
ConditionPathExists=
[Service]
User=danilo
StandardInput=null
TTYPath=
TTYReset=no
TTYVHangup=no
TTYVTDisallocate=no
Environment=KWIN_DRM_NO_AMS=1
Environment=KWIN_DRM_USE_MODIFIERS=0
Environment=KWIN_DRM_NO_DIRECT_SCANOUT=1
Environment=KWIN_DRM_PREFER_COLOR_DEPTH=24
Environment=KWIN_FORCE_SW_CURSOR=1
Environment=QT_QPA_PLATFORM=wayland
Environment=XDG_SESSION_TYPE=wayland
EOF_PM
mkdir -p /sysroot/etc/systemd/system/graphical.target.wants 2>/dev/null || true
ln -sf /usr/lib/systemd/system/plasma-mobile.service /sysroot/etc/systemd/system/graphical.target.wants/display-manager.service 2>/dev/null || true
ln -sf /usr/lib/systemd/system/graphical.target /sysroot/etc/systemd/system/default.target 2>/dev/null || true
"""
            if "mount_root_partition" in i2_content:
                i2_content = i2_content.replace("mount_root_partition", patch_sysroot, 1)
            # Evitar que init_2nd.sh mate procesos útiles antes de switch_root
            i2_content = i2_content.replace('kill -9 "$pid"', '# kill -9 "$pid"')
            i2_content = i2_content.replace('killall udevd syslogd unudhcpd', 'killall udevd syslogd')

            # Ping fresco al watchdog justo antes de switch_root (31 segundos frescos para arrancar systemd)
            pre_switch = r"""
[ -e /dev/watchdog ] && printf '\x01' > /dev/watchdog 2>/dev/null || true
[ -e /dev/watchdog0 ] && printf '\x01' > /dev/watchdog0 2>/dev/null || true
"""
            i2_content = i2_content.replace('exec switch_root /sysroot "$init"', pre_switch + '\nexec switch_root /sysroot "$init"')

            with open(target_i2, "w") as f:
                f.write(i2_content)
            print(f"  [+] Parche de Plasma Mobile, Display, Watchdog y Systemd inyectado en {target_i2}")


    # pmos_getty
    pmos_getty_path = os.path.join(workdir, "sbin", "pmos_getty")
    with open(pmos_getty_path, "w") as f:
        f.write("#!/bin/sh\nexport PATH=/usr/bin:/bin:/usr/sbin:/sbin\nexec /bin/sh -i\n")
    os.chmod(pmos_getty_path, 0o755)

    # Symlinks para shell y servicios de red
    for target, link_name in [
        ("/bin/busybox", "bin/sh"),
        ("/bin/busybox", "bin/getty"),
        ("/bin/busybox", "sbin/getty"),
        ("/bin/busybox-extras", "bin/telnetd"),
        ("/bin/busybox-extras", "usr/bin/telnetd"),
        ("/bin/busybox-extras", "bin/tcpsvd"),
        ("/bin/busybox-extras", "usr/bin/tcpsvd"),
    ]:
        lp = os.path.join(workdir, link_name)
        if not os.path.exists(lp):
            try:
                os.symlink(target, lp)
            except Exception:
                pass

    # Script de activación del subsistema gráfico DRM (/bin/start_display.sh)
    display_script = os.path.join(workdir, "bin", "start_display.sh")
    with open(display_script, "w") as f:
        f.write(r"""#!/bin/sh
echo "=========================================================="
echo "  Inicializando Subsistema Gráfico DRM (MediaTek MT6789)  "
echo "=========================================================="

load_mod() {
    local m="$1"
    local base="${m%.ko}"
    base="${base//-/_}"
    /sbin/modprobe "$base" 2>/dev/null || \
    /bin/modprobe "$base" 2>/dev/null || \
    /usr/bin/modprobe "$base" 2>/dev/null || \
    insmod "/lib/modules/$m" 2>/dev/null || \
    insmod "/lib/modules/5.10.185-android12-9-gcc7d44e69095-dirty/$m" 2>/dev/null || true
}

echo "[1/4] Polarización del Panel LCD (+/- 5V)..."
load_mod aw37503.ko

echo "[2/4] Infraestructura de Pantalla y Panel DSI..."
load_mod mtk_panel_ext.ko
load_mod panel-l0a9w006c-dsi-vdo.ko
load_mod pwm-mtk-disp.ko
load_mod leds-mtk.ko

echo "[3/4] Infraestructura CMDQ, MMSYS Clocks y MML..."
load_mod clk-mt6789-mmsys.ko
load_mod clk-mt6789-disp_dsc.ko
load_mod mmprofile.ko
load_mod irq-dbg.ko
load_mod vcp_status.ko
load_mod mtk_sync.ko
load_mod mtk_slbc.ko
load_mod mtk-icc-core.ko
load_mod mtk-smi-dbg.ko
load_mod mtk_dramc.ko
load_mod mtk-cmdq-drv-ext.ko
load_mod cmdq-platform-mt6789.ko
load_mod cmdq_helper_inf.ko
load_mod mmqos-common.ko
load_mod mtk-mml.ko
load_mod mtk_disp_notify.ko

echo "[4/4] Cargando Controlador DRM y Backlight..."
load_mod mediatek-drm.ko
load_mod leds-mtk-disp.ko

mkdir -p /dev/dri
for w in $(seq 1 120); do
    if [ -f /sys/class/drm/card0/dev ]; then
        maj_min=$(cat /sys/class/drm/card0/dev 2>/dev/null)
        [ -n "$maj_min" ] && [ ! -c /dev/dri/card0 ] && mknod /dev/dri/card0 c ${maj_min%:*} ${maj_min#*:} 2>/dev/null || true
        chmod 666 /dev/dri/card0 2>/dev/null || true
    fi
    if [ -c /dev/dri/card0 ]; then
        echo "🎉 ¡DRM /dev/dri/card0 CREADO CON ÉXITO!"
        ls -la /dev/dri/
        echo 0 > /sys/class/leds/lcd-backlight/brightness 2>/dev/null || true
        echo 0 > /sys/class/backlight/*/brightness 2>/dev/null || true
        mkdir -p /sys/class/backlight 2>/dev/null || true
        echo "[+] Subsistema DRM /dev/dri/card0 inicializado y backlight en 0 (ahorro total)"
        exit 0
    fi
    sleep 0.5
done
echo "[-] /dev/dri/card0 aún no disponible. Verifique 'dmesg | tail -n 25'."
""")
    os.chmod(display_script, 0o755)
    print(f"  [+] Script de pantalla instalado en {display_script}")

    devinfo_path = os.path.join(workdir, "usr/share/deviceinfo/deviceinfo")
    if os.path.exists(devinfo_path):
        with open(devinfo_path, "a") as f:
            f.write('deviceinfo_usb_network_function="ncm.usb0"\n')
            f.write('deviceinfo_usb_network_host_addr="42:69:00:11:22:34"\n')
            f.write('deviceinfo_getty="ttyGS0;115200"\n')

    init_path = os.path.join(workdir, "init")
    with open(init_path, "r") as f:
        init_script = f.read()

    early_hook_v9 = r"""
# >>> EARLY BOOT v9.0 (CharDev GPIO + USB Gadget + TCP Root + DRM Prep) <<<
LOGFILE="/tmp/pmos_boot.log"
echo "=== PMOS EARLY BOOT v9.0 START ===" > "$LOGFILE"
echo ">> [pmOS-v9.0] Iniciando arranque de Linux GKI 5.10.185..." > /dev/kmsg

# Nodos estáticos esenciales (creados atómicamente antes de cualquier redirección)
rm -f /dev/null /dev/kmsg /dev/watchdog /dev/watchdog0
mknod -m 666 /dev/null c 1 3
mknod -m 666 /dev/zero c 1 5
mknod -m 666 /dev/urandom c 1 9
mknod -m 666 /dev/random c 1 8
mknod -m 666 /dev/tty c 5 0
mknod -m 600 /dev/console c 5 1
mknod -m 666 /dev/ptmx c 5 2
mknod -m 666 /dev/kmsg c 1 11 2>/dev/null || true
mknod -m 660 /dev/watchdog c 10 130 2>/dev/null || true
mknod -m 660 /dev/watchdog0 c 10 130 2>/dev/null || true
[ ! -c /dev/loop-control ] && mknod /dev/loop-control c 10 237 2>/dev/null || true
for l in $(seq 0 7); do
    [ ! -b /dev/loop$l ] && mknod /dev/loop$l b 7 $l 2>/dev/null || true
done

populate_block_nodes() {
    for b in /sys/class/block/*; do
        [ -d "$b" ] || continue
        local bname=$(basename "$b")
        if [ -f "$b/dev" ]; then
            local maj_min=$(cat "$b/dev" 2>/dev/null)
            if [ -n "$maj_min" ]; then
                local maj=${maj_min%:*}
                local min=${maj_min#*:}
                rm -f "/dev/$bname"
                mknod "/dev/$bname" b "$maj" "$min" 2>/dev/null || true
            fi
        fi
    done
}

resolve_misc() {
    populate_block_nodes
    mkdir -p /dev/block/by-name
    for uevent in /sys/class/block/*/uevent; do
        [ -f "$uevent" ] || continue
        pname=$(grep "^PARTNAME=" "$uevent" 2>/dev/null | cut -d= -f2)
        dname=$(grep "^DEVNAME=" "$uevent" 2>/dev/null | cut -d= -f2)
        if [ "$pname" = "misc" ] && [ -n "$dname" ] && [ -b "/dev/$dname" ]; then
            ln -sf "/dev/$dname" /dev/block/by-name/misc 2>/dev/null || true
            echo "/dev/$dname"
            return 0
        fi
    done
    for cand in /dev/block/by-name/misc /dev/sda1 /dev/sdb1 /dev/sdc1 /dev/misc; do
        if [ -b "$cand" ]; then
            echo "$cand"
            return 0
        fi
    done
    return 1
}

sync_debug_to_misc() {
    local m=$(resolve_misc 2>/dev/null)
    if [ -n "$m" ] && [ -b "$m" ]; then
        dd if="$LOGFILE" of="$m" bs=4096 seek=1 count=4 conv=notrunc 2>/dev/null || true
        dmesg | dd of="$m" bs=4096 seek=5 count=110 conv=notrunc 2>/dev/null || true
        sync 2>/dev/null || true
    fi
}

[ -x /usr/bin/kmod ] && {
    ln -sf /usr/bin/kmod /bin/insmod
    ln -sf /usr/bin/kmod /bin/modprobe
    ln -sf /usr/bin/kmod /sbin/insmod
    ln -sf /usr/bin/kmod /sbin/modprobe
}

/sbin/depmod -a 2>>"$LOGFILE" || /usr/bin/depmod -a 2>>"$LOGFILE" || depmod -a 2>>"$LOGFILE" || true

load_mod() {
    local mod="$1"
    local base="${mod%.ko}"
    base="${base//-/_}"
    echo "load: $mod" >>"$LOGFILE"
    /sbin/modprobe "$base" 2>>"$LOGFILE" || \
    /bin/modprobe "$base" 2>>"$LOGFILE" || \
    /usr/bin/modprobe "$base" 2>>"$LOGFILE" || \
    insmod "/lib/modules/$mod" 2>>"$LOGFILE" || \
    insmod "/lib/modules/5.10.185-android12-9-gcc7d44e69095-dirty/$mod" 2>>"$LOGFILE" || true
    echo "done: $mod" >>"$LOGFILE"
}

# 1. Watchdog con timeout extendido a 300 segundos y alimentador dual
echo "load: mtk_wdt.ko (timeout=300)" >>"$LOGFILE"
insmod /lib/modules/mtk_wdt.ko timeout=300 2>>"$LOGFILE" || \
insmod /lib/modules/5.10.185-android12-9-gcc7d44e69095-dirty/mtk_wdt.ko timeout=300 2>>"$LOGFILE" || \
modprobe mtk_wdt timeout=300 2>>"$LOGFILE" || true
echo "done: mtk_wdt.ko" >>"$LOGFILE"
if [ -x /bin/wdt_pet ]; then
    /bin/wdt_pet &
elif [ -x /sbin/wdt_pet ]; then
    /sbin/wdt_pet &
fi
if [ -x /bin/backlight_guard ]; then
    /bin/backlight_guard &
fi
(while true; do
    printf "\x01" > /dev/watchdog0 2>/dev/null || true
    printf "\x01" > /dev/watchdog 2>/dev/null || true
    sleep 0.5
done) &

populate_gpio_nodes() {
    for g in /sys/class/gpio/gpiochip* /sys/bus/gpio/devices/gpiochip*; do
        [ -e "$g" ] || continue
        local gname=$(basename "$g")
        if [ -f "$g/dev" ]; then
            local maj_min=$(cat "$g/dev" 2>/dev/null)
            if [ -n "$maj_min" ]; then
                local maj=${maj_min%:*}
                local min=${maj_min#*:}
                rm -f "/dev/$gname"
                mknod "/dev/$gname" c "$maj" "$min" 2>/dev/null || true
            fi
        fi
    done
}

switch_typec() {
    populate_gpio_nodes
    if [ -x /bin/gpio_switch ]; then
        /bin/gpio_switch 157 1 >/dev/null 2>&1 || true
        /bin/gpio_switch 95 0  >/dev/null 2>&1 || true
        /bin/gpio_switch 104 0 >/dev/null 2>&1 || true
    fi
}

# 2. Carga ordenada de los módulos tempranos
if [ -f /lib/modules/modules.early.order ]; then
    while IFS= read -r m || [ -n "$m" ]; do
        [ -z "$m" ] && continue
        load_mod "$m"
        case "$m" in
            pinctrl-mt6789.ko) switch_typec ;;
            musb_main.ko)      switch_typec ;;
        esac
    done < /lib/modules/modules.early.order
fi
switch_typec
mknod /dev/mali0 c 10 116 2>/dev/null || true
chmod 666 /dev/mali0 2>/dev/null || true

# 3. Configuración de Gadget USB ConfigFS
mkdir -p /sys/kernel/config /config
mount -t configfs none /sys/kernel/config 2>/dev/null || mount -t configfs none /config 2>/dev/null || true
CG="/sys/kernel/config/usb_gadget/g1"
[ ! -d /sys/kernel/config/usb_gadget ] && [ -d /config/usb_gadget ] && CG="/config/usb_gadget/g1"

if [ -d "$CG" ]; then
    echo "" > "$CG/UDC" 2>/dev/null || true
    rm -f "$CG/configs/c.1/acm.usb0" 2>/dev/null || true
    rm -f "$CG/configs/c.1/ncm.usb0" 2>/dev/null || true
fi

mkdir -p "$CG" 2>/dev/null || true
echo "0x1d6b" > "$CG/idVendor" 2>/dev/null || true
echo "0x0104" > "$CG/idProduct" 2>/dev/null || true
echo "0xEF"   > "$CG/bDeviceClass" 2>/dev/null || true
echo "0x02"   > "$CG/bDeviceSubClass" 2>/dev/null || true
echo "0x01"   > "$CG/bDeviceProtocol" 2>/dev/null || true
mkdir -p "$CG/strings/0x409" 2>/dev/null || true
echo "UMIDIGI"         > "$CG/strings/0x409/manufacturer" 2>/dev/null || true
echo "G7 Tab Pro"      > "$CG/strings/0x409/product" 2>/dev/null || true
echo "pmOS_v9_MT6789"  > "$CG/strings/0x409/serialnumber" 2>/dev/null || true

mkdir -p "$CG/functions/acm.usb0" 2>/dev/null || true
mkdir -p "$CG/functions/ncm.usb0" 2>/dev/null || true
echo "42:69:00:11:22:33" > "$CG/functions/ncm.usb0/dev_addr" 2>/dev/null || true
echo "42:69:00:11:22:34" > "$CG/functions/ncm.usb0/host_addr" 2>/dev/null || true

mkdir -p "$CG/configs/c.1/strings/0x409" 2>/dev/null || true
echo "Serial + Ethernet" > "$CG/configs/c.1/strings/0x409/configuration" 2>/dev/null || true
echo 500 > "$CG/configs/c.1/MaxPower" 2>/dev/null || true

ln -s "$CG/functions/acm.usb0" "$CG/configs/c.1/acm.usb0" 2>/dev/null || true
ln -s "$CG/functions/ncm.usb0" "$CG/configs/c.1/ncm.usb0" 2>/dev/null || true

# Vincular al controlador UDC
(
    while true; do
        if [ ! -f /tmp/_gadget_active ]; then
            for u in /sys/class/udc/*; do
                [ -e "$u" ] || continue
                uname=$(basename "$u")
                case "$uname" in
                    *dummy*) continue ;;
                    *)
                        echo "$uname" > "$CG/UDC" 2>>"$LOGFILE" && {
                            touch /tmp/_gadget_active
                            ip addr add 172.16.42.1/24 dev usb0 2>/dev/null || ifconfig usb0 172.16.42.1 netmask 255.255.255.0 up 2>/dev/null || true
                            ip link set usb0 up 2>/dev/null || true
                            # Crear nodo de caracteres real para ttyGS0 (c 238 0)
                            if [ -f /sys/class/tty/ttyGS0/dev ]; then
                                maj_min=$(cat /sys/class/tty/ttyGS0/dev 2>/dev/null)
                                rm -f /dev/ttyGS0
                                mknod /dev/ttyGS0 c ${maj_min%:*} ${maj_min#*:} 2>/dev/null || true
                                chmod 666 /dev/ttyGS0 2>/dev/null || true
                            fi
                        }
                        break
                        ;;
                esac
            done
            switch_typec 2>/dev/null || true
        fi
        sleep 2
    done
) &

# Nodos PTY esenciales
mkdir -p /dev/pts
mount -t devpts devpts /dev/pts 2>/dev/null || true
[ ! -c /dev/ptmx ] && mknod /dev/ptmx c 5 2 2>/dev/null || true
chmod 666 /dev/ptmx 2>/dev/null || true

# Consola serie interactiva en /dev/ttyGS0
(while true; do
    if [ -c /dev/ttyGS0 ]; then
        /sbin/getty -n -l /sbin/pmos_getty ttyGS0 115200 vt100 2>/dev/null || \
        /bin/sh </dev/ttyGS0 >/dev/ttyGS0 2>&1 || true
    fi
    sleep 0.5
done) &

# Red Ethernet CDC-NCM e IP fija + DHCP
ip addr add 172.16.42.1/24 dev usb0 2>/dev/null || ifconfig usb0 172.16.42.1 netmask 255.255.255.0 up 2>/dev/null || true
ip link set usb0 up 2>/dev/null || true
if [ -x /usr/bin/unudhcpd ]; then
    /usr/bin/unudhcpd -i usb0 -s 172.16.42.1 -c 172.16.42.2 &
fi

# 1. Shell TCP puro via tcpsvd en puerto 2323
(while true; do
    /bin/busybox-extras tcpsvd -vE 0.0.0.0 2323 /sbin/pmos_getty 2>/dev/null || \
    /usr/bin/busybox-extras tcpsvd -vE 0.0.0.0 2323 /bin/sh 2>/dev/null || sleep 1
done) &

# 2. Servidor Telnet en puerto 23
(while true; do
    /bin/busybox-extras telnetd -p 23 -l /sbin/pmos_getty 2>/dev/null || \
    /usr/bin/busybox-extras telnetd -p 23 -l /sbin/pmos_getty 2>/dev/null || sleep 1
done) &

# 3. Sincronización periódica de telemetría a partición misc
(while true; do
    sync_debug_to_misc 2>/dev/null || true
    sleep 0.5
done) &

# 3.5 Carga de infraestructura esencial de hardware (SCMI, PMIC, UFS, Clocks, DRM, Touch, Teclas, Wi-Fi, CPUFreq)
echo ">> [pmOS-v13] Cargando controladores de hardware UFS, Pantalla, Touchscreen, Wi-Fi y CPUFreq..." > /dev/kmsg
for m in \
    reset-ti-syscon \
    mtk-mbox mtk-mbox-mailbox mtk-tinysys-ipi mtk-rpmsg-mbox tinysys-scmi \
    i2c-mt65xx mtk-pmic-wrap mt6397 \
    phy-mtk-ufs blocktag rpmb ufs-mediatek-dbg ufs-mediatek-mod \
    clk-mt6789-mmsys clk-mt6789-disp_dsc mmprofile irq-dbg vcp_status \
    mtk_sync mtk_slbc mtk-icc-core mtk-smi-dbg mtk_dramc mtk-cmdq-drv-ext \
    cmdq-platform-mt6789 cmdq_helper_inf mmqos-common mmqos-mt6789 \
    mtk-mml mtk_disp_notify mtk-dvfsrc mtk-dvfsrc-regulator mtk-dvfsrc-devfreq \
    mediatek-cpufreq-hw \
    aw37503 mtk_panel_ext panel-dk068h5gq-dsi-vdo panel-l0a9w006c-dsi-vdo pwm-mtk-disp \
    leds-mtk leds-mtk-disp leds-mtk-pwm mediatek-drm \
    mtk-pmic-keys mtk-kpd tddi_9551 \
    cfg80211 connadp ccci_util_lib ccci_auxadc ccmni mtk_pbm mtk_mdpm ccci_md_all \
    btif_drv wmt_drv; do
    load_mod "$m.ko"
done


# Activar modo de máximo rendimiento para CPU Cortex-A76 (2.20 GHz) y bus DRAM LPDDR4X (4.26 GHz)
for g in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    [ -f "$g" ] && echo performance > "$g" 2>/dev/null || true
done
for g in /sys/class/devfreq/*dvfsrc*/governor; do
    [ -f "$g" ] && echo performance > "$g" 2>/dev/null || true
done

# Inyección automática de firmware Chipone TDDI si el hardware aún no lo tiene en flash
if [ -f /sys/bus/i2c/drivers/chipone_tddi/0-0048/cts_firmware/res_x ]; then
    rx=$(cat /sys/bus/i2c/drivers/chipone_tddi/0-0048/cts_firmware/res_x 2>/dev/null)
    if [ "$rx" = "0" ] || [ -z "$rx" ]; then
        echo ">> [pmOS-v11] Inyectando firmware oficial a Chipone TDDI..." > /dev/kmsg
        echo "chipone_firmware.bin flash" > /sys/bus/i2c/drivers/chipone_tddi/0-0048/cts_firmware/update_from_file 2>/dev/null || true
        sleep 0.5
    fi
fi

# Inicialización y encendido de Wi-Fi MediaTek MT6631 autónomo
if [ -x /bin/wmt_wifi_start.sh ]; then
    echo ">> [pmOS-v13] Disparando wmt_wifi_start.sh en segundo plano..." > /dev/kmsg
    /bin/wmt_wifi_start.sh >/tmp/wmt_wifi_init.log 2>&1 &
fi


# Poblar nodos de entrada
mdev -s 2>/dev/null || true
mkdir -p /dev/input
for ev in /sys/class/input/event*; do
    [ -d "$ev" ] || continue
    evname=$(basename "$ev")
    if [ -f "$ev/dev" ]; then
        maj_min=$(cat "$ev/dev" 2>/dev/null)
        [ -n "$maj_min" ] && [ ! -c "/dev/input/$evname" ] && mknod "/dev/input/$evname" c ${maj_min%:*} ${maj_min#*:} 2>/dev/null || true
        chmod 666 "/dev/input/$evname" 2>/dev/null || true
    fi
done

# 4. Mapear almacenamiento userdata y subparticiones pmOS (loop0p1, loop0p2)
populate_block_nodes
USERDATA_DEV=""
for attempt in $(seq 1 50); do
    for cand in /dev/sdc61 /dev/block/by-name/userdata /dev/userdata; do
        if [ -b "$cand" ]; then
            USERDATA_DEV="$cand"
            ln -sf "$cand" /dev/userdata 2>/dev/null || true
            break 2
        fi
    done
    for uevent in /sys/class/block/*/uevent; do
        [ -f "$uevent" ] || continue
        pname=$(grep "^PARTNAME=" "$uevent" 2>/dev/null | cut -d= -f2)
        dname=$(grep "^DEVNAME=" "$uevent" 2>/dev/null | cut -d= -f2)
        if [ "$pname" = "userdata" ] && [ -n "$dname" ] && [ -b "/dev/$dname" ]; then
            USERDATA_DEV="/dev/$dname"
            ln -sf "/dev/$dname" /dev/userdata 2>/dev/null || true
            break 2
        fi
    done
    sleep 0.2
    populate_block_nodes
done

mkdir -p /boot /sysroot

if [ -n "$USERDATA_DEV" ] && [ -b "$USERDATA_DEV" ]; then
    echo ">> [pmOS-v9] Mapeando subparticiones en $USERDATA_DEV..." > /dev/kmsg
    losetup -P /dev/loop0 "$USERDATA_DEV" 2>/dev/null || losetup -Pf "$USERDATA_DEV" 2>/dev/null || true
    sleep 0.4
    [ -f /sys/class/block/loop0p1/dev ] && { maj_min=$(cat /sys/class/block/loop0p1/dev); mknod /dev/loop0p1 b ${maj_min%:*} ${maj_min#*:} 2>/dev/null || true; }
    [ -f /sys/class/block/loop0p2/dev ] && { maj_min=$(cat /sys/class/block/loop0p2/dev); mknod /dev/loop0p2 b ${maj_min%:*} ${maj_min#*:} 2>/dev/null || true; }
    populate_block_nodes
    if [ -b /dev/loop0p1 ] && [ -b /dev/loop0p2 ]; then
        export PMOS_BOOT="/dev/loop0p1"
        export PMOS_ROOT="/dev/loop0p2"
        export SUBPARTITION_DEV="$USERDATA_DEV"
        export SUBPARTITION_LOOP="/dev/loop0"
        mkdir -p /dev/disk/by-label /dev/disk/by-partlabel
        ln -sf /dev/loop0p1 /dev/disk/by-label/pmOS_boot
        ln -sf /dev/loop0p2 /dev/disk/by-label/pmOS_root
        ln -sf "$USERDATA_DEV" /dev/disk/by-partlabel/userdata
        echo ">> [pmOS-v9] Particiones PMOS listas: boot=$PMOS_BOOT, root=$PMOS_ROOT" > /dev/kmsg
        
        # Montar rootfs y boot de postmarketOS
        mount -o rw /dev/loop0p2 /sysroot 2>/dev/null || mount -o ro /dev/loop0p2 /sysroot 2>/dev/null || true
        mount -o ro /dev/loop0p1 /sysroot/boot 2>/dev/null || true
    fi
fi

# 5. Inicializar DRM Nodes, Backlight y Configuración de Pantalla KWin
mkdir -p /dev/dri /sysroot/dev/dri
if [ -f /sys/class/drm/card0/dev ]; then
    maj_min=$(cat /sys/class/drm/card0/dev 2>/dev/null)
    mknod /dev/dri/card0 c ${maj_min%:*} ${maj_min#*:} 2>/dev/null || true
    mknod /dev/dri/renderD128 c 226 128 2>/dev/null || true
    chmod 666 /dev/dri/* 2>/dev/null || true
    cp -a /dev/dri/* /sysroot/dev/dri/ 2>/dev/null || true
fi

# Apagar backlight (0) para maxima velocidad de carga de bateria
for b in /sys/class/leds/lcd-backlight/brightness /sys/class/backlight/*/brightness; do
    [ -f "$b" ] && echo 0 > "$b" 2>/dev/null || true
done

# Preconfigurar KWin Output para panel DSI-1 nativo (1200x1920)
mkdir -p /sysroot/home/danilo/.config /sysroot/root/.config 2>/dev/null || true
cat << 'EOF_KWIN_OUT' > /sysroot/home/danilo/.config/kwinoutputconfig.json
[
    {
        "data": [
            {
                "connectorName": "DSI-1",
                "uuid": "ba8b13ec-0703-4735-9e60-e71e1a06933b"
            }
        ],
        "name": "outputs"
    },
    {
        "data": [
            {
                "outputs": [
                    {
                        "autoRotationPolicy": "InTabletMode",
                        "automaticBrightness": false,
                        "enabled": true,
                        "highDynamicRange": false,
                        "mode": {
                            "height": 1920,
                            "refreshRate": 60000,
                            "width": 1200
                        },
                        "overscan": 0,
                        "pos": {
                            "x": 0,
                            "y": 0
                        },
                        "primary": true,
                        "rgbRange": "Automatic",
                        "scale": 1,
                        "transform": "Normal",
                        "uuid": "ba8b13ec-0703-4735-9e60-e71e1a06933b",
                        "vrrPolicy": "Never",
                        "wideColorGamut": false
                    }
                ]
            }
        ],
        "name": "setups"
    }
]
EOF_KWIN_OUT
cp -f /sysroot/home/danilo/.config/kwinoutputconfig.json /sysroot/root/.config/kwinoutputconfig.json 2>/dev/null || true

cat << 'EOF_KWINRC' > /sysroot/home/danilo/.config/kwinrc
[Compositing]
Backend=OpenGL
GLCore=false
HiddenPreviews=5
OpenGLIsUnsafe=false
WindowsBlockCompositing=false
MaxFPS=60
RefreshRate=60

[Plugins]
blurEnabled=false
contrastEnabled=false
slideEnabled=false
slidingpopupsEnabled=false

[KDE]
AnimationDurationFactor=0
EOF_KWINRC
cp -f /sysroot/home/danilo/.config/kwinrc /sysroot/root/.config/kwinrc 2>/dev/null || true

cat << 'EOF_LOCK' > /sysroot/home/danilo/.config/kscreenlockerrc
[Daemon]
Autolock=false
LockOnResume=false
Timeout=0
EOF_LOCK
cp -f /sysroot/home/danilo/.config/kscreenlockerrc /sysroot/root/.config/kscreenlockerrc 2>/dev/null || true
chown -R 10000:10000 /sysroot/home/danilo/.config 2>/dev/null || true


# 6. Bind mounts y arranque de D-Bus + KDE Plasma Mobile con soporte táctil
if [ -d /sysroot/usr/bin ]; then
    mount -t proc proc /sysroot/proc 2>/dev/null || true
    mount -t sysfs sysfs /sysroot/sys 2>/dev/null || true
    mount --bind /dev /sysroot/dev 2>/dev/null || true
    mount -t devpts devpts /sysroot/dev/pts 2>/dev/null || true
    mount -t tmpfs tmpfs /sysroot/run 2>/dev/null || true
    mount -t tmpfs tmpfs /sysroot/tmp 2>/dev/null || true

    # Base de datos udev para libinput (touch y teclas)
    mkdir -p /sysroot/run/udev/data /run/udev/data
    chmod -R 666 /dev/input /sysroot/dev/input 2>/dev/null || true
    chown -R 10000:23 /sysroot/dev/input 2>/dev/null || true

    for ev in /sys/class/input/event*; do
        [ ! -e "$ev" ] && continue
        devname=$(basename "$ev")
        devnum=$(cat "$ev/dev" 2>/dev/null)
        [ -z "$devnum" ] && continue
        sysname=$(cat "$ev/device/name" 2>/dev/null || echo "")
        
        for udir in /run/udev/data /sysroot/run/udev/data; do
            datafile="$udir/c$devnum"
            echo "I:559115511" > "$datafile"
            echo "E:DEVNAME=/dev/input/$devname" >> "$datafile"
            echo "E:MAJOR=13" >> "$datafile"
            echo "E:ID_INPUT=1" >> "$datafile"
            
            if echo "$sysname" | grep -i -q "chipone_tddi"; then
                echo "E:ID_INPUT_TOUCHSCREEN=1" >> "$datafile"
                echo "E:ID_BUS=i2c" >> "$datafile"
                echo "E:LIBINPUT_CALIBRATION_MATRIX=-1 0 1 0 -1 1" >> "$datafile"
            elif echo "$sysname" | grep -i -q "key\|kpd"; then
                echo "E:ID_INPUT_KEY=1" >> "$datafile"
                echo "E:ID_INPUT_KEYBOARD=1" >> "$datafile"
            fi
        done
    done

    # Asegurar scripts, firmwares y compatibilidad android en sysroot
    [ -f /bin/start_plasma_mobile.sh ] && cp -f /bin/start_plasma_mobile.sh /sysroot/usr/bin/start_plasma_mobile.sh 2>/dev/null || true
    [ -f /bin/wmt_wifi_start.sh ] && cp -f /bin/wmt_wifi_start.sh /sysroot/usr/bin/wmt_wifi_start.sh 2>/dev/null || true
    [ -f /bin/wmt_init_complete ] && cp -f /bin/wmt_init_complete /sysroot/usr/bin/wmt_init_complete 2>/dev/null || true
    chmod 755 /sysroot/usr/bin/start_plasma_mobile.sh /sysroot/usr/bin/wmt_wifi_start.sh /sysroot/usr/bin/wmt_init_complete 2>/dev/null || true
    if [ -d /android_compat ]; then
        mkdir -p /sysroot/android_compat 2>/dev/null || true
        cp -a /android_compat/* /sysroot/android_compat/ 2>/dev/null || true
    fi
    mkdir -p /sysroot/lib/firmware 2>/dev/null || true
    cp -a /lib/firmware/* /sysroot/lib/firmware/ 2>/dev/null || true

    echo ">> [pmOS-v13] Lanzando sesion de GNOME Mobile (Phosh) supervisada..." > /dev/kmsg
    (
        sleep 2
        # Instalar libseat_direct, battery_daemon y configs de Phosh
        cp -f /bin/libseat_direct.so /sysroot/usr/lib/libseat_direct.so 2>/dev/null || true
        cp -f /bin/start_phosh.sh /sysroot/usr/bin/start_phosh.sh 2>/dev/null || true
        cp -f /bin/battery_daemon /sysroot/usr/bin/battery_daemon 2>/dev/null || true
        cp -f /bin/tablet_audio_daemon.py /sysroot/usr/bin/tablet_audio_daemon.py 2>/dev/null || true
        cp -f /etc/phoc.ini /sysroot/etc/phoc.ini 2>/dev/null || true
        mkdir -p /sysroot/etc/wireplumber/wireplumber.conf.d /sysroot/etc/pipewire/pipewire.conf.d /sysroot/usr/share/alsa-card-profile/mixer/paths
        cp -f /etc/wireplumber/50-alsa-fix.conf /sysroot/etc/wireplumber/wireplumber.conf.d/50-alsa-fix.conf 2>/dev/null || true
        cp -f /etc/pipewire/10-alsa.conf /sysroot/etc/pipewire/pipewire.conf.d/10-alsa.conf 2>/dev/null || true
        cp -f /etc/alsa-card-profile/analog-output-headphones.conf /sysroot/usr/share/alsa-card-profile/mixer/paths/analog-output-headphones.conf 2>/dev/null || true
        chmod 755 /sysroot/usr/lib/libseat_direct.so /sysroot/usr/bin/start_phosh.sh /sysroot/usr/bin/battery_daemon /sysroot/usr/bin/tablet_audio_daemon.py 2>/dev/null || true

        mkdir -p /sysroot/etc/dbus-1/system.d
        cat << 'DBUSEOF' > /sysroot/etc/dbus-1/system.d/net.hadess.SensorProxy.conf
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-BUS Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <policy user="root">
    <allow own="net.hadess.SensorProxy"/>
  </policy>
  <policy context="default">
    <allow send_destination="net.hadess.SensorProxy"/>
  </policy>
</busconfig>
DBUSEOF
        echo "CHASSIS=tablet" > /sysroot/etc/machine-info 2>/dev/null || true

        # Precalentar udev en sysroot
        chroot /sysroot udevadm trigger --action=add --subsystem-match=input 2>/dev/null || true
        chroot /sysroot udevadm settle --timeout=2 2>/dev/null || true
        echo ">> [pmOS-v13] Entorno sysroot listo. Levantando sesión UI interactiva Phosh..." > /dev/kmsg
        if [ -x /bin/start_phosh_service.sh ]; then
            /bin/start_phosh_service.sh
        fi
    ) &
fi

echo ">> [pmOS-v10] SISTEMA ESTABLE Y VIVO. Shell interactiva lista en 172.16.42.1:2323 y /dev/ttyGS0." > /dev/kmsg
while true; do sleep 3600; done
# >>> FIN EARLY BOOT v10.0 <<<
"""

    if "mount_proc_sys_dev" in init_script:
        init_script = init_script.replace(
            "mount_proc_sys_dev",
            "mount_proc_sys_dev\n" + early_hook_v9, 1)
    else:
        init_script = early_hook_v9 + "\n" + init_script

    with open(init_path, "w") as f:
        f.write(init_script)
    os.chmod(init_path, 0o755)

    print("Comprimiendo CPIO del Initramfs v9.0...")
    cmd = f"find . | cpio -H newc -o 2>/dev/null | gzip -9 -c > {output_gz_path}"
    subprocess.run(cmd, cwd=workdir, shell=True, check=True)
    sz = os.path.getsize(output_gz_path)
    print(f"Initramfs v9.0 listo: {output_gz_path} ({sz:,} bytes / {sz/1024/1024:.1f} MB)")
    return output_gz_path

def build_boot_v9(kernel_path, ramdisk_path, stock_boot_path, out_boot_path):
    print("\n==================================================")
    print(" 2. Generando boot_v9.0.img (Header v4 Oficial + AVB)    ")
    print("==================================================")
    mkbootimg = "/home/danilo/.local/var/pmbootstrap/chroot_rootfs_umidigi-g7tabpro/usr/share/android-tools/mkbootimg/mkbootimg.py"
    avbtool   = "/home/danilo/Documentos/TABLET/avbtool.py"

    cmdline = (
        "root=/dev/ram0 rdinit=/init "
        "bootopt=64S3,32N2,64N2 "
        "androidboot.hardware=mt6789 "
        "vmalloc=400M swiotlb=noforce cma=64M "
        "loop.max_part=7 "
        "console=tty0 "
        "fbcon=map:0 "
        "loglevel=8 ignore_loglevel "
        "panic=0 panic_on_warn=0 panic_on_oops=0 "
        "mtk_wdt.timeout=120 "
        "clk_ignore_unused "
        "pmos.root_uuid=9de93fa1-178d-42a5-aacf-19df23f64739 "
        "pmos.boot_uuid=d52dfdfd-26f3-4e85-a91d-d862866bab13 "
        "systemd.unit=graphical.target "
        "androidboot.verifiedbootstate=orange"
    )

    cmd = [
        "python3", mkbootimg,
        "--header_version", "4",
        "--kernel", kernel_path,
        "--ramdisk", ramdisk_path,
        "--cmdline", cmdline,
        "--pagesize", "4096",
        "-o", out_boot_path
    ]
    subprocess.run(cmd, check=True)

    subprocess.run(
        f"python3 {avbtool} erase_footer --image {out_boot_path} 2>/dev/null || true",
        shell=True, check=True)
    subprocess.run(
        f"python3 {avbtool} add_hash_footer --image {out_boot_path} "
        f"--partition_name boot --partition_size 67108864 --algorithm NONE",
        shell=True, check=True)
    print(f"boot_v10.img creado con AVB Footer de 64 MB: {out_boot_path}")

def build_vendor_boot_v10(dtb_path, stock_vb_path, out_vb_path):
    print("\n==================================================")
    print(" 3. Generando vendor_boot_v10.0.img              ")
    print("==================================================")
    with open(stock_vb_path, "rb") as f:
        hdr = bytearray(f.read(4096))
    with open(dtb_path, "rb") as f:
        dtb_data = f.read()

    empty_cpio = subprocess.check_output(
        "echo -n | cpio -H newc -o 2>/dev/null", shell=True)
    rd_size  = len(empty_cpio)
    dtb_size = len(dtb_data)

    hdr[24:28]     = struct.pack("<I", rd_size)
    hdr[2100:2104] = struct.pack("<I", dtb_size)
    hdr[2112:2116] = struct.pack("<I", 0)
    hdr[2116:2120] = struct.pack("<I", 0)
    hdr[2120:2124] = struct.pack("<I", 0)
    hdr[2124:2128] = struct.pack("<I", 0)

    rd_padded  = empty_cpio + b"\x00" * (align_4k(rd_size)  - rd_size)
    dtb_padded = dtb_data   + b"\x00" * (align_4k(dtb_size) - dtb_size)

    with open(out_vb_path, "wb") as f:
        f.write(hdr)
        f.write(rd_padded)
        f.write(dtb_padded)

    avbtool = "/home/danilo/Documentos/TABLET/avbtool.py"
    subprocess.run(
        f"python3 {avbtool} erase_footer --image {out_vb_path} 2>/dev/null || true",
        shell=True, check=True)
    subprocess.run(
        f"python3 {avbtool} add_hash_footer --image {out_vb_path} "
        f"--partition_name vendor_boot --partition_size 67108864 --algorithm NONE",
        shell=True, check=True)
    print(f"vendor_boot_v13.img creado con AVB Footer de 64 MB: {out_vb_path}")

def main():
    V13_DIR = "/home/danilo/Documentos/TABLET/version_v13"
    STOCK_DIR = "/home/danilo/Documentos/TABLET/stock_firmware/UMIDIGI_G7_Tab_Pro_E_V1.0_20250512"

    kernel_path = "/home/danilo/Documentos/TABLET/vmlinuz-umidigi-g7tabpro"
    stock_boot  = os.path.join(STOCK_DIR, "boot.img")
    stock_vb    = os.path.join(STOCK_DIR, "vendor_boot.img")
    dtb_path    = os.path.join(V13_DIR, "pmos_usb_peripheral.dtb")
    
    out_rd      = "/tmp/pmos_initramfs_v13.gz"
    out_boot    = os.path.join(V13_DIR, "pmos_boot_v13.img")
    out_vb      = os.path.join(V13_DIR, "vendor_boot_pmos_v13.img")

    print(f"Iniciando compilación de imágenes postmarketOS v13.0 en {V13_DIR}...")
    prepare_initramfs_v9(out_rd)
    build_boot_v9(kernel_path, out_rd, stock_boot, out_boot)
    build_vendor_boot_v10(dtb_path, stock_vb, out_vb)

    print("\n🎉 ¡Compilación de Versión 12.0 finalizada con éxito!")
    print(f"  -> Boot v13:        {out_boot}")
    print(f"  -> Vendor Boot v13: {out_vb}")

if __name__ == "__main__":
    main()

