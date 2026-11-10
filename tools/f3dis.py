"""Disassemble Fable3.exe at a VA, naming xlive thunks by our .def names.
usage: python f3dis.py <va_hex> [count=80] [exe]"""
import sys, os, re, struct, pefile, capstone

HERE = os.path.dirname(os.path.abspath(__file__))
EXE = sys.argv[3] if len(sys.argv) > 3 else r"E:\SteamLibrary\steamapps\common\Fable 3\Fable3.exe"
names = {}
for line in open(os.path.join(HERE, '..', 'src', 'xlive', 'xlive.def')):
    m = re.match(r'\s+(\w+)@\d+\s+@(\d+)', line)
    if m:
        names[int(m.group(2))] = m.group(1)

pe = pefile.PE(EXE, fast_load=True)
pe.parse_data_directories([pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT']])
base = pe.OPTIONAL_HEADER.ImageBase
iat = {}
for e in pe.DIRECTORY_ENTRY_IMPORT:
    dll = e.dll.decode()
    for i in e.imports:
        iat[i.address] = names.get(i.ordinal, f'{dll}#{i.ordinal}') if dll.lower() == 'xlive.dll' else f'{dll}!{(i.name or b"").decode()}'
text = pe.sections[0]
code = text.get_data()
va0 = base + text.VirtualAddress
thunks = {}
for a, n in iat.items():
    for m in re.finditer(re.escape(b'\xff\x25' + struct.pack('<I', a)), code):
        thunks[va0 + m.start()] = n
img = pe.get_memory_mapped_image()
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
va = int(sys.argv[1], 16)
cnt = int(sys.argv[2]) if len(sys.argv) > 2 else 80

def strat(a):
    try:
        b = pe.get_data(a - base, 120)
    except Exception:
        return None
    s = b.split(b'\0')[0]
    if len(s) >= 4 and all(32 <= c < 127 for c in s):
        return s.decode()
    w = b.decode('utf-16le', 'ignore').split('\0')[0]
    if len(w) >= 4 and all(32 <= ord(c) < 127 for c in w):
        return 'L' + w
    return None

for n, ins in enumerate(md.disasm(img[va - base: va - base + cnt * 16], va)):
    if n >= cnt:
        break
    note = ''
    for tok in re.findall(r'0x[0-9a-f]+', ins.op_str):
        v = int(tok, 16)
        if v in thunks:
            note += ' ; ' + thunks[v]
        elif v in iat:
            note += ' ; ' + iat[v]
        elif base <= v < base + 0x2400000:
            s = strat(v)
            if s:
                note += ' ; "' + s[:70] + '"'
    print(f'{ins.address:08x}  {ins.mnemonic:6} {ins.op_str}{note}')
