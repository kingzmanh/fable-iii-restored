"""Count pushes before every call to an xlive import thunk in Fable3.exe (cross-check of
stdcall arg sizes). The game calls `call thunk` where thunk = `jmp [IAT slot]`.
usage: python callsite_pushes.py <Fable3.exe>"""
import sys, struct, re, collections, pefile, capstone

pe = pefile.PE(sys.argv[1], fast_load=True)
pe.parse_data_directories([pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT']])
base = pe.OPTIONAL_HEADER.ImageBase
slots = {}
for e in pe.DIRECTORY_ENTRY_IMPORT:
    if e.dll.decode().lower() == 'xlive.dll':
        for i in e.imports:
            slots[i.address] = i.ordinal
text = next(s for s in pe.sections if s.Name.startswith(b'.text'))
code = text.get_data()
va0 = base + text.VirtualAddress
thunks = {}
for a in slots:
    for m in re.finditer(re.escape(b'\xff\x25' + struct.pack('<I', a)), code):
        thunks[va0 + m.start()] = a
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.skipdata = True
res = collections.defaultdict(collections.Counter)
sites = collections.defaultdict(list)
window = collections.deque(maxlen=24)
for ins in md.disasm(code, va0):
    window.append(ins)
    if ins.mnemonic == 'call' and ins.op_str.startswith('0x'):
        tgt = thunks.get(int(ins.op_str, 16))
        if tgt in slots:
            n = 0
            for p in reversed(list(window)[:-1]):
                if p.mnemonic == 'call' or p.mnemonic.startswith('ret') or p.mnemonic == 'jmp':
                    break
                if p.mnemonic == 'push':
                    n += 1
                elif p.mnemonic == 'pop':
                    n -= 1
            res[slots[tgt]][n * 4] += 1
            sites[slots[tgt]].append(hex(ins.address))
for o in sorted(res):
    print(o, dict(res[o]), ' '.join(sites[o][:8]))
