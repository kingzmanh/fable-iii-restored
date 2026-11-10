"""Find direct call/jmp (E8/E9 rel32) and push/mov imm32 references to a VA in Fable3.exe.
usage: python xrefs.py <va_hex> [exe]"""
import sys, struct, pefile

EXE = sys.argv[2] if len(sys.argv) > 2 else r"E:\SteamLibrary\steamapps\common\Fable 3\Fable3.exe"
pe = pefile.PE(EXE, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
t = pe.sections[0]
code = t.get_data()
va0 = base + t.VirtualAddress
target = int(sys.argv[1], 16)
out = []
for i in range(len(code) - 5):
    op = code[i]
    if op in (0xE8, 0xE9):
        rel = struct.unpack_from('<i', code, i + 1)[0]
        if va0 + i + 5 + rel == target:
            out.append(('call' if op == 0xE8 else 'jmp', va0 + i))
imm = struct.pack('<I', target)
p = code.find(imm)
while p != -1:
    out.append(('imm', va0 + p - 1))
    p = code.find(imm, p + 1)
for k, a in out:
    print(f'{k:4} {a:08x}')
