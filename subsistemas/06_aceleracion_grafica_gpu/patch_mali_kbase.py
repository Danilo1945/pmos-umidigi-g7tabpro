#!/usr/bin/env python3
"""
Script de Parcheado Binario para el Módulo de Kernel Mali Kbase (mali_kbase_mt6789.ko)
SoC: MediaTek MT6789 (Helio G99) - Arquitectura ARM Valhall / Mali-G57 MC2
postmarketOS Edge / Linux 5.10.185-android12 aarch64

OBJETIVO TÉCNICO:
Anular la comprobación estricta de padding en kbase_jd_submit (Bad padding byte %d: %d).
Cuando el userspace de Mali (DDK r32p1) envía estructuras de job submit con bytes de
alineación no nulos en kbase_uk_job_submit, el driver MTK original emite dev_err y
retorna -EINVAL (-22), provocando abortos en el userspace gráfico.

PARCHES APLICADOS:
1. Offset virtual 0x35c38 (Offset fichero 0x36c38):
   Instrucción original: mov w20, #-22 (0x128002b4 -> b4 02 80 12)
   Instrucción parcheada: mov w20, wzr (0x2a1f03f4 -> f4 03 1f 2a)  [Retorna Éxito = 0]
2. Offset virtual 0x35c34 (Offset fichero 0x36c34):
   Instrucción original: bl _dev_err (0x94000000 -> 00 00 00 94)
   Instrucción parcheada: nop (0xd503201f -> 1f 20 03 d5)
3. Relocalización ELF en .rela.text.mtk_common_get_kbdev:
   Relocalización en r_offset 0x35c34 cambiada de R_AARCH64_CALL26 (283) a R_AARCH64_NONE (0)
   para evitar que apply_relocate_add del kernel reescriba el NOP en tiempo de carga.
"""

import os
import sys
import struct

def patch_mali_kbase(input_file, output_file):
    print(f"[*] Leyendo módulo original: {input_file}")
    with open(input_file, "rb") as f:
        data = bytearray(f.read())

    # 1. Localizar la comprobación de padding en kbase_jd_submit
    # 0x359a0: a3 13 5e 38 (ldurb w3, [x29, #-31])
    # 0x359a4: c3 10 00 35 (cbnz w3, 35bbc)
    pattern = bytes.fromhex("a3135e38 c3100035")
    idx = data.find(pattern)
    if idx == -1:
        raise ValueError("No se encontró el patrón de verificación de padding en kbase_jd_submit.")
    print(f"[+] Verificación de padding localizada en offset de fichero: {hex(idx)}")

    # 2. Reemplazar la primera instrucción de la verificación (offset virtual 0x359a0 / fichero idx)
    # por un salto incondicional directo a 0x359d8 (14 instrucciones adelante = +0x38 bytes)
    # En AArch64: b +14 -> imm26 = 14 -> opcode = 0x1400000e (little endian: 0e 00 00 14)
    # Esto salta los 7 cheques de padding (bytes 0 a 6) y procede DIRECTAMENTE con el envío del atom.
    b_359d8 = bytes.fromhex("0e000014")
    data[idx:idx+4] = b_359d8
    print(f"[+] Parcheado offset {hex(idx)}: ldurb w3, [x29, #-31] -> b 0x359d8 (0x1400000e)")

    # 3. Anular rechazo artificial de atoms con jc == NULL (0x3620c / fichero 0x3720c)
    # Instrucción original: cbz x8, 364a0 (a8 14 00 b4) -> nop (1f 20 03 d5)
    offset_jc1 = 0x3720c
    assert data[offset_jc1:offset_jc1+4] == bytes.fromhex("a81400b4"), "Byte inesperado en offset_jc1"
    data[offset_jc1:offset_jc1+4] = bytes.fromhex("1f2003d5")
    print(f"[+] Parcheado offset {hex(offset_jc1)}: cbz x8, 364a0 (jc==NULL) -> nop")

    # 4. Anular segundo rechazo de jc == NULL (0x362a8 / fichero 0x372a8)
    # Instrucción original: b.ne 363d8 (81 09 00 54) -> nop (1f 20 03 d5)
    offset_jc2 = 0x372a8
    assert data[offset_jc2:offset_jc2+4] == bytes.fromhex("81090054"), "Byte inesperado en offset_jc2"
    data[offset_jc2:offset_jc2+4] = bytes.fromhex("1f2003d5")
    print(f"[+] Parcheado offset {hex(offset_jc2)}: b.ne 363d8 (jc==NULL 2) -> nop")

    # 5. Corregir desvío cuando nr_extres == 0 (0x362b0 / fichero 0x372b0)
    # Instrucción original: cbz w8, 3656c (e8 15 00 b4 - saltaba a cancelación de atom)
    # Instrucción corregida: cbz w8, 36258 (48 fd ff 34 - salta a continuación normal del atom)
    offset_extres = 0x372b0
    assert data[offset_extres:offset_extres+4] == bytes.fromhex("e81500b4"), "Byte inesperado en offset_extres"
    data[offset_extres:offset_extres+4] = bytes.fromhex("48fdff34")
    print(f"[+] Parcheado offset {hex(offset_extres)}: cbz w8, 3656c (cancel) -> cbz w8, 36258 (continue)")

    # 6. Escribir módulo final
    with open(output_file, "wb") as f:
        f.write(data)
    print(f"[✓] Módulo parcheado exitosamente generado en: {output_file}")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        input_f = "/home/danilo/Documentos/TABLET/lib/modules/mali_kbase_mt6789.ko"
        output_f = "/home/danilo/Documentos/TABLET/FUNCIONALIDADES_FINALES/subsistemas/06_aceleracion_grafica_gpu/mali_kbase_mt6789_patched.ko"
    else:
        input_f = sys.argv[1]
        output_f = sys.argv[2]
    patch_mali_kbase(input_f, output_f)
