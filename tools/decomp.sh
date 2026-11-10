#!/bin/bash
# Decompile Fable3.exe functions with the headless Ghidra project.  tools/decomp.sh <addr|start-end>...
# Output: analysis/decomp/fn_XXXXXXXX.c
P="$(cd "$(dirname "$0")/.." && pwd)"
G=/c/Users/Msi/Tools/ghidra_12.1.4_PUBLIC
OUT='C:\Users\Msi\Tools\fable3_ghidra\out'
mkdir -p /c/Users/Msi/Tools/fable3_ghidra/out
cd /c/Users/Msi/Tools/fable3_ghidra
MAXMEM=8G "$G/support/analyzeHeadless.bat" 'C:\Users\Msi\Tools\fable3_ghidra\proj' fable3 -process Fable3.exe -noanalysis -readOnly \
  -scriptPath 'C:\Users\Msi\Tools\fable3_ghidra\scripts' -postScript DecompileFuncs.java "$OUT" "$@" 2>&1 | grep -E "DECOMPILED|NOT FOUND|ERROR" 
mkdir -p "$P/analysis/decomp"
cp /c/Users/Msi/Tools/fable3_ghidra/out/*.c "$P/analysis/decomp/" 2>/dev/null
