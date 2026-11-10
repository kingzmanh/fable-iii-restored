# Reverse-engineering notes (Steam Fable III, Fable3.exe 1.0.0.283)

Fable3.exe, 29,067,024 bytes, SHA-256 `aac98798…42da42`. Image base 0x400000, but the exe is relocatable (ASLR), so every address below is a file VA. Tools: Ghidra 12.1 (headless, `tools/decomp.sh`), and capstone/pefile scripts (`tools/f3dis.py`, `tools/xrefs.py`).

## Launch chain
Steam starts `FableLauncher.exe`, which starts `F3Secu.exe` (SecuROM PA, `paul.dll`), which starts `Fable3.exe`. FableLauncher restarts the chain when the exit code is 0x123 (DLC or language change). Today F3Secu only shows a "Fable III - Welcome" activation wizard.

## The SecuROM block
At start, `0x615380` (the "GameThread" entry) opens the file mapping `-=[SMS_<exe name>_SMS]=-` and copies 0x6C4 bytes into the launch-info struct (`0x619aa0`). If the mapping is missing, the game shows "Wrong executable" and quits. Used fields:

| Offset | Meaning |
|---|---|
| +0x04 | HWND of SecuROM's message window |
| +0x44 | message id |
| +0x6C | interface version (≥ 0x1000B) |
| +0x2A8 | exe path |
| +0x6C0 | dword |

Our dll creates the mapping before the game reads it.

## Spot checks
Seven pass/fail pairs, `0x1C86C18 … 0x1C86C4C` (A, B). A check passes when A == B. They are set when entering areas such as `Albion\OldRepository` and `Albion\MistPeak_GypsyCamp`, by sending a message to the SecuROM window and hashing files. They are read through accessors `0x616470 … 0x616530` (`mov ecx,[A]; xor eax,eax; cmp ecx,[B]; sete al; ret`):

- the first six are Lua functions `SpotCheckSuccessful_1..6`, used by the quest scripts
- the seventh is read by the loading state machine (`0xBF0543`, `TEXT_GUI_SECUROM_SPOT_CHECK_FAILED`) and by `0x64946D`

`secu.c` checks each accessor byte for byte (relocated) and replaces it with `mov eax,1; ret`.

## GFWL
101 ordinals are imported. Stdcall argument sizes were measured from the retail xlive.dll exports and the game's call sites (`analysis/xlive_argsizes.txt`, `analysis/callsite_pushes.txt`).

- **Voice:** the game never checks XHVCreateEngine. It calls `RegisterLocalTalker` (vtable +0x20) on the result, so a failure crashes it. `xhv.c` has the vtable layout, confirmed from the game's calls.
- **DLC:**
  - `XLiveContentCreateEnumerator(cItems, info{ver 1, …, +0x18 title 0x4D53090A, +0x1C type 2}, pcb, ph)` returns a Win32 error; the game requires cb ≤ cItems·32.
  - For each item: `XLiveContentGetPath`, plus `\content`, then `XLiveContentCreateAccessHandle(user, item, 1, pbuf(28), 0, &h, NULL)`, then it reads the 28-byte license from the PBuffer. Dword +0x18 is the premium content mask; each `package_collection_info.xmb` package has a `premiumContentMask` bit.
- **Profile settings:** the game keeps its controls, audio and misc options in a title-specific profile setting (1000-byte binary). Real GFWL lost these offline.

## DLC packages (download.xbox.com/content/4d53090a/)
| CAB | Content ID | Contents |
|---|---|---|
| 4d53090a0ecf0000 | B7D5FE3D720F347CE446785C3F55488C8E7FF201 | DLC1: Understone, launch packs |
| 4d53090a0ecf000e | D7FCB87DC6790538CC5EE45EC44EC782603B8ACB | Traitor's Keep (dlc2free/dlc2paid) |
| 4d53090a0ecf0010 | 60340833BA99AD081835DF2F3EF01F3F247D0DC3 | DLC3: Inquisitor, armoury tattoos |

## Input / window (for the test harness)
- The input manager reads DirectInput only while `GetForegroundWindow()` equals its window and the WM_ACTIVATEAPP flag is set (`0x188D600`, `0x188E050`, `0x188AF20`).
- XInput is polled through `0x188C7B0` (vtable `0x1BF9280`).
- Window procedure: `0x18F7460`.

## Video
- With VSync off, the game creates its device windowed with `PresentationInterval = IMMEDIATE`.
- `CreateDevice` returns `D3DERR_DEVICELOST` while another program holds exclusive fullscreen. The game then shows "Direct3D failed to initialise" and crashes at `0x18D031E`; `d3dfix.c` retries instead.
- `Fable3.exe` is already large-address-aware.
