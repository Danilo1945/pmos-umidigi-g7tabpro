#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>

#define COMBO_IOCTL_GET_CHIP_ID            0x80047700
#define COMBO_IOCTL_DO_MODULE_INIT         0x40047701
#define COMBO_IOCTL_EXT_CHIP_PWR_ON        0x80047702
#define COMBO_IOCTL_EXT_CHIP_PWR_OFF       0x80047703
#define COMBO_IOCTL_DO_MODULE_INIT_V2      0x80047704
#define COMBO_IOCTL_MODULE_CLEANUP         0x80047705
#define COMBO_IOCTL_EXT_CHIP_DETECT        0x80047706
#define COMBO_IOCTL_DO_SDIO_AUTOK          0x80047707
#define COMBO_IOCTL_SET_ADIE_CHIPID        0x80047708
#define COMBO_IOCTL_GET_SOC_CHIP_ID        0x80047709
#define COMBO_IOCTL_CONNSYS_SOC_HW_INIT    0x8004770a

int main(int argc, char *argv[]) {
    printf("[*] Abriendo /dev/wmtdetect...\n");
    int fd = open("/dev/wmtdetect", O_RDWR);
    if (fd < 0) {
        perror("[-] Error abriendo /dev/wmtdetect");
        return 1;
    }
    printf("[+] /dev/wmtdetect abierto con fd=%d\n", fd);

    int ret;

    // 1. CONNSYS_SOC_HW_INIT
    printf("[1/8] Ejecutando COMBO_IOCTL_CONNSYS_SOC_HW_INIT (0x%08x)...\n", COMBO_IOCTL_CONNSYS_SOC_HW_INIT);
    ret = ioctl(fd, COMBO_IOCTL_CONNSYS_SOC_HW_INIT, 0);
    printf("    -> ret=%d\n", ret);

    // 2. EXT_CHIP_DETECT
    printf("[2/8] Ejecutando COMBO_IOCTL_EXT_CHIP_DETECT (0x%08x)...\n", COMBO_IOCTL_EXT_CHIP_DETECT);
    ret = ioctl(fd, COMBO_IOCTL_EXT_CHIP_DETECT, 0);
    printf("    -> ret=%d\n", ret);

    // 3. EXT_CHIP_PWR_ON
    printf("[3/8] Ejecutando COMBO_IOCTL_EXT_CHIP_PWR_ON (0x%08x)...\n", COMBO_IOCTL_EXT_CHIP_PWR_ON);
    ret = ioctl(fd, COMBO_IOCTL_EXT_CHIP_PWR_ON, 0);
    printf("    -> ret=%d\n", ret);

    // 4. GET_CHIP_ID
    printf("[4/8] Ejecutando COMBO_IOCTL_GET_CHIP_ID (0x%08x)...\n", COMBO_IOCTL_GET_CHIP_ID);
    int chipid = ioctl(fd, COMBO_IOCTL_GET_CHIP_ID, 0);
    printf("    -> SoC Chip ID detectado: 0x%04x (%d)\n", chipid, chipid);

    int adie_id = 0x6631;
    if (argc > 1) {
        adie_id = (int)strtol(argv[1], NULL, 0);
    }
    printf("    -> Usando A-Die Chip ID: 0x%04x\n", adie_id);

    // 5. SET_ADIE_CHIPID
    printf("[5/8] Ejecutando COMBO_IOCTL_SET_ADIE_CHIPID (0x%08x) con arg=0x%04x...\n", COMBO_IOCTL_SET_ADIE_CHIPID, adie_id);
    ret = ioctl(fd, COMBO_IOCTL_SET_ADIE_CHIPID, adie_id);
    printf("    -> ret=%d\n", ret);

    // 6. DO_SDIO_AUTOK
    printf("[6/8] Ejecutando COMBO_IOCTL_DO_SDIO_AUTOK (0x%08x)...\n", COMBO_IOCTL_DO_SDIO_AUTOK);
    ret = ioctl(fd, COMBO_IOCTL_DO_SDIO_AUTOK, 0);
    printf("    -> ret=%d\n", ret);

    // 7. DO_MODULE_INIT (0x40047701)
    printf("[7/8] Ejecutando COMBO_IOCTL_DO_MODULE_INIT (0x40047701) con adie_id=0x%04x...\n", adie_id);
    ret = ioctl(fd, COMBO_IOCTL_DO_MODULE_INIT, adie_id);
    printf("    -> ret=%d\n", ret);

    // 7b. MODULE_CLEANUP / INIT_TARGET (0x80047705)
    printf("[7b] Ejecutando COMBO_IOCTL_MODULE_CLEANUP (0x80047705) con arg=0x%04x...\n", adie_id);
    ret = ioctl(fd, COMBO_IOCTL_MODULE_CLEANUP, adie_id);
    printf("    -> ret=%d\n", ret);

    // 7c. DO_MODULE_INIT_V2 (0x80047704)
    printf("[7c] Ejecutando COMBO_IOCTL_DO_MODULE_INIT_V2 (0x80047704) con arg=0x%04x...\n", adie_id);
    ret = ioctl(fd, COMBO_IOCTL_DO_MODULE_INIT_V2, adie_id);
    printf("    -> ret=%d\n", ret);

    // 8. GET_SOC_CHIP_ID
    printf("[8/8] Ejecutando COMBO_IOCTL_GET_SOC_CHIP_ID (0x%08x)...\n", COMBO_IOCTL_GET_SOC_CHIP_ID);
    int socid = ioctl(fd, COMBO_IOCTL_GET_SOC_CHIP_ID, 0);
    printf("    -> SoC ID: 0x%04x\n", socid);

    close(fd);
    printf("[+] Inicialización completa de MediaTek WMT finalizada.\n");
    return 0;
}
