#!/usr/bin/env python3
"""
Script de Parcheado Binario para el Driver Userspace ARM Mali (libmali-bifrost.so / libmali-valhall.so)
Arquitectura: aarch64 (ARMv8-A Little Endian)
GPU: ARM Mali-G57 MC2 (Valhall Architecture ID 0x9000903)
Entorno: Linux musl libc (postmarketOS / Alpine Linux)

OBJETIVO TÉCNICO:
1. Bypass de la lista blanca de arquitecturas de GPU en el DDK de ARM:
   El driver userspace original rechaza el ID de hardware 0x9000903 (Mali-G57 MC2 Valhall),
   arrojando "Unknown Product ID" y fallando en eglInitialize.
2. Neutralización del fallo de compatibilidad de Weak Symbols en musl libc:
   En musl libc, __pthread_key_create y la inicialización de TLS difiere de glibc.
   El runtime C++ de ARM invoca std::system_error(-1) en libstdc++ al no encontrar
   la implementación exacta esperada de pthreads, abortando la ejecución.
   Se neutralizan las comprobaciones y la llamada a _ZSt20__throw_system_errori.

PARCHES APLICADOS:
1. 0x192abb0: 9f 02 01 6b -> 16 00 00 14 (cmp w20, w1 -> b +0x58, bypass de chequeo de arquitectura)
2. 0x1a6b1c0: e2 01 01 b4 -> 1f 20 03 d5 (cbz x2 -> nop, bypass de weak symbol pthread)
3. 0x1a6b1d8: 40 5d 00 35 -> 1f 20 03 d5 (cbnz -> nop)
4. 0x1a6b3c8: c0 4d 00 35 -> 1f 20 03 d5 (cbnz -> nop)
5. 0x1a6b780: 00 30 00 35 -> 1f 20 03 d5 (cbnz -> nop)
6. 0x1a6bd7c: 00 a4 -> 20 fd (salto incondicional seguro)
7. 0x1a6bd7f: 34 -> 17
8. 0x1a6bd8c: 41 b8 9f 97 -> 1f 20 03 d5 (bl _ZSt20__throw_system_errori -> nop)
"""

import sys
import os

PATCHES = [
    (0x192abb0, bytes.fromhex("9f02016b"), bytes.fromhex("16000014")),
    (0x1a6b1c0, bytes.fromhex("e20101b4"), bytes.fromhex("1f2003d5")),
    (0x1a6b1d8, bytes.fromhex("405d0035"), bytes.fromhex("1f2003d5")),
    (0x1a6b3c8, bytes.fromhex("c04d0035"), bytes.fromhex("1f2003d5")),
    (0x1a6b780, bytes.fromhex("00300035"), bytes.fromhex("1f2003d5")),
    (0x1a6bd7c, bytes.fromhex("00a40034"), bytes.fromhex("20fdff17")),
    (0x1a6bd8c, bytes.fromhex("41b89f97"), bytes.fromhex("1f2003d5")),
    # Neutralización del bucle infinito usleep(500) por saturación de cola de fences Android:
    # 0x18fbda0: Retornar siempre 0 en contador atómico de cola (mov w0, #0; ret; nop; nop)
    (0x18fbda0, bytes.fromhex("81a787d20000018b00fcdf88c0035fd6"), bytes.fromhex("00008052c0035fd61f2003d51f2003d5")),
    # 0x367430: Neutralizar salto condicional b.hi a usleep(500) con nop
    (0x367430, bytes.fromhex("48ffff54"), bytes.fromhex("1f2003d5")),
]

def patch_libmali(input_file, output_file):
    print(f"[*] Leyendo binario libmali: {input_file}")
    with open(input_file, "rb") as f:
        data = bytearray(f.read())

    for offset, orig_bytes, patch_bytes in PATCHES:
        actual = data[offset:offset+len(orig_bytes)]
        if actual != orig_bytes and actual != patch_bytes:
            print(f"[!] Advertencia: En offset {hex(offset)}, esperado {orig_bytes.hex()}, encontrado {actual.hex()}")
        data[offset:offset+len(patch_bytes)] = patch_bytes
        print(f"[+] Parcheado {hex(offset)}: {orig_bytes.hex()} -> {patch_bytes.hex()}")

    with open(output_file, "wb") as f:
        f.write(data)
    print(f"[✓] Driver userspace parcheado con éxito en: {output_file}")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        input_f = "/home/danilo/Documentos/TABLET/libmali-bifrost-g52.so"
        output_f = "/home/danilo/Documentos/TABLET/FUNCIONALIDADES_FINALES/subsistemas/06_aceleracion_grafica_gpu/libmali-bifrost-patched.so"
    else:
        input_f = sys.argv[1]
        output_f = sys.argv[2]
    patch_libmali(input_f, output_f)
