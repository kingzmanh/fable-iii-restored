"""For each xlive ordinal Fable3.exe imports, find the stdcall argument byte count
(the `ret N` the export ends with) in the system xlive.dll. Read-only reference use:
we only learn signatures, nothing from xlive.dll is copied into our dll.
usage: python xlive_argsizes.py <Fable3.exe> <xlive.dll>"""
import sys, pefile, capstone

game = pefile.PE(sys.argv[1], fast_load=True)
game.parse_data_directories([pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT']])
ords = []
for e in game.DIRECTORY_ENTRY_IMPORT:
    if e.dll.decode().lower() == 'xlive.dll':
        ords = sorted(i.ordinal for i in e.imports)

xl = pefile.PE(sys.argv[2], fast_load=True)
xl.parse_data_directories([pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXPORT']])
base = xl.OPTIONAL_HEADER.ImageBase
exp = {s.ordinal: s.address for s in xl.DIRECTORY_ENTRY_EXPORT.symbols}
img = xl.get_memory_mapped_image()
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

def scan(rva, depth=0, seen=None):
    """Follow straight-line code + unconditional jumps; collect ret sizes."""
    seen = seen if seen is not None else set()
    rets = set()
    work = [rva]
    steps = 0
    while work and steps < 4000:
        r = work.pop()
        if r in seen or r <= 0 or r >= len(img):
            continue
        seen.add(r)
        for ins in md.disasm(img[r:r + 0x400], base + r):
            steps += 1
            if ins.mnemonic == 'ret':
                rets.add(int(ins.op_str, 16) if ins.op_str else 0)
                break
            if ins.mnemonic == 'jmp':
                try:
                    work.append(int(ins.op_str, 16) - base)
                except ValueError:
                    pass
                break
            if ins.mnemonic.startswith('j'):
                try:
                    work.append(int(ins.op_str, 16) - base)
                except ValueError:
                    pass
    return rets

for o in ords:
    rva = exp.get(o)
    print(o, sorted(scan(rva)) if rva else 'MISSING')
