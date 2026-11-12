import struct
import sys

ORIG_PATH = "/home/danilo/Documentos/TABLET/dlkm_extract/vendor_dlkm/lib/modules/mtk-vcodec-dec-v1.ko"
PATCHED_PATH = "/tmp/mtk-vcodec-dec-v1.ko"

with open(ORIG_PATH, "rb") as f:
    data = bytearray(f.read())

# Read ELF header
e_shoff = struct.unpack_from("<Q", data, 0x28)[0]
e_shentsize = struct.unpack_from("<H", data, 0x3a)[0]
e_shnum = struct.unpack_from("<H", data, 0x3c)[0]
e_shstrndx = struct.unpack_from("<H", data, 0x3e)[0]

# Read section headers
sections = []
for i in range(e_shnum):
    off = e_shoff + i * e_shentsize
    sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size, sh_link, sh_info, sh_addralign, sh_entsize = struct.unpack_from("<IIQQQQIIQQ", data, off)
    sections.append({
        'index': i,
        'sh_name': sh_name,
        'sh_type': sh_type,
        'sh_flags': sh_flags,
        'sh_addr': sh_addr,
        'sh_offset': sh_offset,
        'sh_size': sh_size,
        'sh_link': sh_link,
        'sh_info': sh_info,
        'sh_entsize': sh_entsize
    })

shstrtab_off = sections[e_shstrndx]['sh_offset']
for s in sections:
    name_end = data.find(b'\0', shstrtab_off + s['sh_name'])
    s['name'] = data[shstrtab_off + s['sh_name']:name_end].decode('latin1')

text_sec = next(s for s in sections if s['name'] == '.text')
print(f".text section: offset=0x{text_sec['sh_offset']:x}, size=0x{text_sec['sh_size']:x}")

# Relocation section for .text
rela_sec = next(s for s in sections if s['name'] == '.rela.text.mtk_prepare_vdec_dvfs')
print(f"rela section: offset=0x{rela_sec['sh_offset']:x}, size=0x{rela_sec['sh_size']:x}")

# Find symbol index for mtk_vdec_formats
symtab_sec = sections[rela_sec['sh_link']]
strtab_sec = sections[symtab_sec['sh_link']]
sym_entsize = symtab_sec['sh_entsize']
num_syms = symtab_sec['sh_size'] // sym_entsize
vdec_formats_sym_idx = None

for i in range(num_syms):
    sym_off = symtab_sec['sh_offset'] + i * sym_entsize
    st_name, st_info, st_other, st_shndx, st_value, st_size = struct.unpack_from("<IBBHQQ", data, sym_off)
    name_end = data.find(b'\0', strtab_sec['sh_offset'] + st_name)
    sym_name = data[strtab_sec['sh_offset'] + st_name:name_end].decode('latin1')
    if sym_name == "mtk_vdec_formats":
        vdec_formats_sym_idx = i
        print(f"Found mtk_vdec_formats symbol index: {i} (0x{i:x})")
        break

assert vdec_formats_sym_idx is not None, "mtk_vdec_formats not found!"

# Now scan relocations in rela_sec
num_relas = rela_sec['sh_size'] // rela_sec['sh_entsize']
print(f"Scanning {num_relas} relocations...")

# Instructions generated from our exact assembly test:
# 7b94 to 7c14 (33 instructions = 132 bytes)
instructions = [
    0x90000008, # adrp x8, mtk_vdec_formats
    0x91000108, # add  x8, x8, #0
    0x9118210e, # add  x14, x8, #0x608
    0xaa0803ed, # mov  x13, x8
    0x1000046c, # adr  x12, 7c30 <fourcc_table>
    0xaa1f03ea, # mov  x10, xzr
    0xd2800802, # mov  x2, #0x40
    0xf2c1e002, # movk x2, #0xf00, lsl #32
    0xd2800203, # mov  x3, #0x10
    0xf2c00803, # movk x3, #0x40, lsl #32
    0xd2810e04, # mov  x4, #0x870
    0xf2c00204, # movk x4, #0x10, lsl #32
    0xb86a798b, # ldr  w11, [x12, x10, lsl #2]
    0xf100115f, # cmp  x10, #0x4
    0x1a9f37ef, # cset w15, cs
    0x531f79ef, # lsl  w15, w15, #1
    0x28813dab, # stp  w11, w15, [x13], #8
    0xb80045bf, # str  wzr, [x13], #4
    0xb80045cb, # str  w11, [x14], #4
    0xf80085df, # str  xzr, [x14], #8
    0xa8810dc2, # stp  x2, x3, [x14], #16
    0xf80085c4, # str  x4, [x14], #8
    0x9100054a, # add  x10, x10, #1
    0xf100195f, # cmp  x10, #0x6
    0x54fffe81, # b.ne 7bc4
    0xb90001bf, # str  wzr, [x13]
    0xb90001df, # str  wzr, [x14]
    0xb906011f, # str  wzr, [x8, #1536] (default_out_fmt_idx = 0)
    0x5280008a, # mov  w10, #0x4
    0x5280008a, # mov  w10, #4 (will be str w10, [x8, #1540] below)
    0xd360fc4b, # lsr  x11, x2, #32
    0xb918090b, # str  w11, [x8, #6152] (max_width = 3840)
    0xb9180d04, # str  w4, [x8, #6156] (max_height = 2160)
]
# Fix instruction at index 29 (0x7c08) to str w10, [x8, #1540]:
instructions[29] = 0xb906050a # str w10, [x8, #1540]

assert len(instructions) == 33, f"Expected 33 instructions, got {len(instructions)}"

# Write instructions to .text at 0x7b94
# Patch 7b64 (tbnz w8, #3, 7c30) to NOP so it never branches into data
struct.pack_into("<I", data, text_sec['sh_offset'] + 0x7b64, 0xd503201f)
patch_text_off = text_sec['sh_offset'] + 0x7b94
for i, inst in enumerate(instructions):
    struct.pack_into("<I", data, patch_text_off + i * 4, inst)

# Write fourcc table at 0x7c30
fourccs = [
    0x34363248, # H264
    0x43564548, # HEVC
    0x30395056, # VP90
    0x30385056, # VP80
    0x31324d4d, # MM21
    0x3231564e  # NV12
]
fourcc_off = text_sec['sh_offset'] + 0x7c30
for i, fcc in enumerate(fourccs):
    struct.pack_into("<I", data, fourcc_off + i * 4, fcc)

# NOP-fill from 0x7c48 to 0x7c94
nop_off = text_sec['sh_offset'] + 0x7c48
nop_count = (0x7c94 - 0x7c48) // 4
for i in range(nop_count):
    struct.pack_into("<I", data, nop_off + i * 4, 0xd503201f)

# Update relocations
R_AARCH64_NONE = 0
R_AARCH64_ADR_PREL_PG_HI21 = 0x113
R_AARCH64_ADD_ABS_LO12_NC = 0x115

modified_relas = 0
for i in range(num_relas):
    r_off = rela_sec['sh_offset'] + i * rela_sec['sh_entsize']
    offset, info, addend = struct.unpack_from("<QQq", data, r_off)
    sym = info >> 32
    r_type = info & 0xffffffff

    if offset == 0x7b94:
        # Relocation for adrp x8, mtk_vdec_formats
        new_info = (vdec_formats_sym_idx << 32) | R_AARCH64_ADR_PREL_PG_HI21
        struct.pack_into("<QQq", data, r_off, offset, new_info, 0)
        print(f"Patched rela at 0x7b94 -> mtk_vdec_formats ADR_PREL_PG_HI21")
        modified_relas += 1
    elif offset == 0x7b98:
        # Relocation for add x8, x8, :lo12:mtk_vdec_formats
        new_info = (vdec_formats_sym_idx << 32) | R_AARCH64_ADD_ABS_LO12_NC
        struct.pack_into("<QQq", data, r_off, offset, new_info, 0)
        print(f"Patched rela at 0x7b98 -> mtk_vdec_formats ADD_ABS_LO12_NC")
        modified_relas += 1
    elif (0x7b9c <= offset < 0x7c18) or (0x7c30 <= offset < 0x7c94):
        # Disable all former relocations in this range
        struct.pack_into("<QQq", data, r_off, offset, 0, 0)
        print(f"Disabled rela at 0x{offset:x} (was sym={sym}, type=0x{r_type:x})")
        modified_relas += 1

print(f"Total relocations modified: {modified_relas}")

with open(PATCHED_PATH, "wb") as f:
    f.write(data)

print(f"Wrote patched module to {PATCHED_PATH}")
