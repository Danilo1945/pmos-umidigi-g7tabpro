#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include "battery_table.h"

#define PATH_VOLTAGE     "/sys/class/power_supply/battery/voltage_now"
#define PATH_RAW_CURRENT "/sys/devices/platform/soc/10026000.pwrap/10026000.pwrap:mt6366/mt6358-gauge/FG_Battery_CurrentConsumption"
#define PATH_STATUS_CHG  "/sys/devices/platform/soc/11017000.i2c/i2c-5/5-0034/11017000.i2c:mt6375@34:chg/power_supply/11017000.i2c:mt6375@34:chg/status"
#define PATH_ONLINE_CHG  "/sys/devices/platform/soc/11017000.i2c/i2c-5/5-0034/11017000.i2c:mt6375@34:chg/power_supply/11017000.i2c:mt6375@34:chg/online"
#define PATH_INIT        "/sys/class/power_supply/battery/init_done"
#define PATH_UI_SOC      "/sys/devices/platform/soc/10026000.pwrap/10026000.pwrap:mt6366/mt6358-gauge/UI_SOC"

#define FAKE_STATUS_FILE  "/tmp/battery_fake_status"
#define FAKE_CURRENT_FILE "/tmp/battery_fake_current"

static int read_int_from_file(const char *path, int default_val) {
    FILE *f = fopen(path, "r");
    if (!f) return default_val;
    int val = default_val;
    if (fscanf(f, "%d", &val) != 1) {
        val = default_val;
    }
    fclose(f);
    return val;
}

static int is_hardware_charging(int raw_current_ua) {
    /* Si la corriente que entra a la batería es positiva (> 0) */
    if (raw_current_ua > 20000) return 1;

    /* Si el chip MT6375 detecta cargador online */
    int online = read_int_from_file(PATH_ONLINE_CHG, 0);
    if (online > 0) return 1;

    /* Si el status del cargador es Charging */
    FILE *f = fopen(PATH_STATUS_CHG, "r");
    if (f) {
        char buf[32];
        if (fgets(buf, sizeof(buf), f)) {
            if (strstr(buf, "Charging") != NULL) {
                fclose(f);
                return 1;
            }
        }
        fclose(f);
    }
    return 0;
}

static void write_ui_soc(int soc) {
    if (soc < 1) soc = 1;
    if (soc > 100) soc = 100;
    
    FILE *f = fopen(PATH_UI_SOC, "w");
    if (f) {
        fprintf(f, "%d\n", soc);
        fclose(f);
    }
}

static void ensure_init_done(void) {
    int init = read_int_from_file(PATH_INIT, 0);
    if (init == 0) {
        FILE *f = fopen(PATH_INIT, "w");
        if (f) {
            fprintf(f, "1\n");
            fclose(f);
        }
    }
}

static double calculate_raw_soc(int vbat_uv, int ibat_ua) {
    int vbat_mv = vbat_uv / 1000;
    int ibat_ma = ibat_ua / 1000;
    int r_mohm = 85; 
    
    int vocv_mv = vbat_mv - (ibat_ma * r_mohm) / 1000;
    int vocv_mv10 = vocv_mv * 10;
    
    if (vocv_mv10 >= battery_table[0].ocv_mv10) return 100.0;
    if (vocv_mv10 <= battery_table[99].ocv_mv10) return 1.0;
    
    for (int i = 0; i < 99; i++) {
        if (vocv_mv10 <= battery_table[i].ocv_mv10 && vocv_mv10 >= battery_table[i+1].ocv_mv10) {
            double v_high = (double)battery_table[i].ocv_mv10;
            double v_low  = (double)battery_table[i+1].ocv_mv10;
            double soc_high = 100.0 - i;
            double soc_low  = 100.0 - (i + 1);
            
            double frac = (v_high == v_low) ? 0.0 : ((double)vocv_mv10 - v_low) / (v_high - v_low);
            return soc_low + frac * (soc_high - soc_low);
        }
    }
    return 50.0;
}

static void notify_charger_state(int charging) {
    if (charging) {
        FILE *f = fopen(FAKE_STATUS_FILE, "w");
        if (f) { fprintf(f, "Charging\n"); fclose(f); }

        FILE *fc = fopen(FAKE_CURRENT_FILE, "w");
        if (fc) { fprintf(fc, "1500000\n"); fclose(fc); }

        mount(FAKE_STATUS_FILE, "/sys/class/power_supply/battery/status", NULL, MS_BIND, NULL);
        mount(FAKE_STATUS_FILE, "/sysroot/sys/class/power_supply/battery/status", NULL, MS_BIND, NULL);
        mount(FAKE_CURRENT_FILE, "/sys/class/power_supply/battery/current_now", NULL, MS_BIND, NULL);
        mount(FAKE_CURRENT_FILE, "/sysroot/sys/class/power_supply/battery/current_now", NULL, MS_BIND, NULL);

        system("chroot /sysroot su - danilo -c 'XDG_RUNTIME_DIR=/tmp/runtime-danilo pw-play /usr/share/sounds/oxygen/stereo/power-plug.ogg' >/dev/null 2>&1 &");
        system("killall upowerd 2>/dev/null");
        printf("[battery-daemon] Cargador CONECTADO -> Sonido emitido e icono de rayo activado.\n");
    } else {
        umount("/sys/class/power_supply/battery/status");
        umount("/sysroot/sys/class/power_supply/battery/status");
        umount("/sys/class/power_supply/battery/current_now");
        umount("/sysroot/sys/class/power_supply/battery/current_now");

        system("chroot /sysroot su - danilo -c 'XDG_RUNTIME_DIR=/tmp/runtime-danilo pw-play /usr/share/sounds/oxygen/stereo/power-unplug.ogg' >/dev/null 2>&1 &");
        system("killall upowerd 2>/dev/null");
        printf("[battery-daemon] Cargador DESCONECTADO -> Sonido emitido e icono restaurado.\n");
    }
    fflush(stdout);
}

int main(int argc, char *argv[]) {
    daemon(1, 1);
    printf("[battery-daemon] Iniciando monitor inteligente de batería y cargador...\n");
    fflush(stdout);
    
    int last_written_soc = -1;
    double smoothed_soc = -1.0;
    int last_charging_state = -1;
    int cycle_count = 0;
    
    unlink(FAKE_STATUS_FILE);
    unlink(FAKE_CURRENT_FILE);
    umount("/sys/class/power_supply/battery/status");
    umount("/sysroot/sys/class/power_supply/battery/status");
    umount("/sys/class/power_supply/battery/current_now");
    umount("/sysroot/sys/class/power_supply/battery/current_now");
    
    while (1) {
        ensure_init_done();
        
        int vbat_uv = read_int_from_file(PATH_VOLTAGE, -1);
        int raw_current = read_int_from_file(PATH_RAW_CURRENT, 0);
        int charging = is_hardware_charging(raw_current);
        
        if (charging != last_charging_state) {
            notify_charger_state(charging);
            last_charging_state = charging;
        }
        
        if (vbat_uv > 2500000 && vbat_uv < 5000000) {
            double raw_soc = calculate_raw_soc(vbat_uv, raw_current);
            
            if (smoothed_soc < 0.0) {
                smoothed_soc = raw_soc;
            } else {
                double alpha = 0.15;
                smoothed_soc = alpha * raw_soc + (1.0 - alpha) * smoothed_soc;
            }
            
            int target_soc = (int)(smoothed_soc + 0.5);
            if (target_soc < 1) target_soc = 1;
            if (target_soc > 100) target_soc = 100;
            
            if (target_soc != last_written_soc || (cycle_count % 10 == 0)) {
                write_ui_soc(target_soc);
                last_written_soc = target_soc;
                printf("[battery-daemon] V=%dmV I=%dmA SOC=%d%% (chg=%d)\n",
                       vbat_uv / 1000, raw_current / 1000, target_soc, charging);
                fflush(stdout);
            }
        }
        
        cycle_count++;
        sleep(1);
    }
    
    return 0;
}
