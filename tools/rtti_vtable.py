"""Find the vtable(s) of a class by its MSVC RTTI name and list the virtual functions.
usage: python rtti_vtable.py ".?AVCClothSubMeshInstance@NEngine@@" [count]"""
import sys, struct, pefile

EXE = r"E:\SteamLibrary\steamapps\common\Fable 3\Fable3.exe"
pe = pefile.PE(EXE, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
img = pe.get_memory_mapped_image()
name = sys.argv[1].encode()
cnt = int(sys.argv[2]) if len(sys.argv) > 2 else 24
i = img.find(name + b'\0')
if i < 0:
    sys.exit('name not found')
td = base + i - 8                      # TypeDescriptor: vfptr, spare, name
tdp = struct.pack('<I', td)
code_lo, code_hi = base + 0x1000, base + 0x1000 + pe.sections[0].Misc_VirtualSize
j = img.find(tdp)
while j >= 0:
    col = base + j - 12                 # CompleteObjectLocator: sig, offset, cdOffset, pTypeDescriptor
    sig, off = struct.unpack_from('<II', img, j - 12)
    if sig == 0:
        colp = struct.pack('<I', col)
        k = img.find(colp)
        while k >= 0:
            vt = base + k + 4
            fns = struct.unpack_from('<%dI' % cnt, img, k + 4)
            print('vtable %08x (subobject offset %d):' % (vt, off))
            for n, f in enumerate(fns):
                if not (code_lo <= f < code_hi):
                    break
                print('  [%2d] +%03x %08x' % (n, n * 4, f))
            k = img.find(colp, k + 1)
    j = img.find(tdp, j + 1)
