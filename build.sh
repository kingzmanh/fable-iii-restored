#!/bin/bash
# Build Fable III Restored (32-bit) with MSYS2 mingw32.  ./build.sh [install]
set -e
cd "$(dirname "$0")"
export PATH=/c/msys64/mingw32/bin:$PATH
mkdir -p build
CFLAGS="-O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -static-libgcc -DWINVER=0x0601 -D_WIN32_WINNT=0x0601"

gcc $CFLAGS -shared -o build/xlive.dll src/xlive/*.c src/xlive/xlive.def \
    -Wl,--enable-stdcall-fixup -lws2_32 -liphlpapi -lshell32 -lole32 -ladvapi32 -luser32 -luuid -ld3d9 -lole32 -lwinmm
strip build/xlive.dll

windres -O coff -i src/launcher/launcher.rc -o build/launcher_res.o
gcc $CFLAGS -municode -mwindows -o build/FableLauncher.exe src/launcher/launcher.c build/launcher_res.o     -lcomctl32 -lshell32 -lole32 -luuid
strip build/FableLauncher.exe

windres -O coff -i src/installer/setup.rc -o build/setup_res.o
gcc $CFLAGS -municode -mwindows -o build/Fable3Restored-Setup.exe src/installer/setup.c src/installer/nvidia.c src/installer/xboxlive.c build/setup_res.o     -lcomctl32 -lshell32 -lole32 -luuid -lwininet -lsetupapi -lbcrypt -ladvapi32
strip build/Fable3Restored-Setup.exe

if [ "$1" = "install" ]; then
    GAME="${FABLE3_DIR:-/e/SteamLibrary/steamapps/common/Fable 3}"
    cp build/xlive.dll "$GAME/xlive.dll"
    echo "installed into $GAME"
fi
echo ok
