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

    # 1. Verificar firma en la función kbase_jd_submit
    pattern = bytes.fromhex("c2008052 601240f9") # mov w2, #6; ldr x0, [x19, #32]
    idx = data.find(pattern)
    if idx == -1:
        raise ValueError("No se encontró el patrón de kbase_jd_submit en el módulo.")
    print(f"[+] Patrón localizado en offset de fichero: {hex(idx)}")

    offset_err_code = idx + 0x14 # 0x36c38
    offset_call_err = idx + 0x10 # 0x36c34

    # 2. Verificar instrucción mov w20, #-22
    orig_err_code = data[offset_err_code:offset_err_code+4]
    assert orig_err_code == bytes.fromhex("b4028012"), f"Byte inesperado en código de error: {orig_err_code.hex()}"

    # Parchear a mov w20, wzr (f4 03 1f 2a)
    data[offset_err_code:offset_err_code+4] = bytes.fromhex("f4031f2a")
    print(f"[+] Parcheado offset {hex(offset_err_code)}: mov w20, #-22 -> mov w20, wzr")

    # 3. Localizar y neutralizar relocalización ELF para 0x35c34
    e_shoff = struct.unpack_from("<Q", data, 0x28)[0]
    e_shentsize = struct.unpack_from("<H", data, 0x3a)[0]
    e_shnum = struct.unpack_from("<H", data, 0x3c)[0]

    found_reloc = False
    for i in range(e_shnum):
        s_hdr = e_shoff + i * e_shentsize
        sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size = struct.unpack_from("<IIQQQQ", data, s_hdr)
        if sh_type == 4: # SHT_RELA
            num_relas = sh_size // 24
            for r in range(num_relas):
                r_entry_offset = sh_offset + r * 24
                r_offset, r_info, r_addend = struct.unpack_from("<QQq", data, r_entry_offset)
                if r_offset == 0x35c34:
                    print(f"[+] Relocalización encontrada en offset {hex(r_entry_offset)}: r_offset=0x35c34, r_info={hex(r_info)}")
                    # Poner r_info en 0 (R_AARCH64_NONE)
                    struct.pack_into("<Q", data, r_entry_offset + 8, 0)
                    found_reloc = True
                    break

    if not found_reloc:
        raise ValueError("No se pudo localizar la relocalización ELF para 0x35c34.")

    # 4. Parchear llamada a dev_err con NOP
    assert data[offset_call_err:offset_call_err+4] == bytes.fromhex("00000094"), "Instrucción de llamada inesperada"
    data[offset_call_err:offset_call_err+4] = bytes.fromhex("1f2003d5")
    print(f"[+] Parcheado offset {hex(offset_call_err)}: bl _dev_err -> nop")

    # 5. Escribir módulo final
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
