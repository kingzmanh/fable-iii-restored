<p align="center"><img src="installer/fable3restored.png" width="128" alt="Fable III Restored icon"></p>

# Fable III Restored

Makes the PC version of **Fable III** (Steam, 2011) run again on Windows 10 and 11. One installer, no manual fixes:

- **No Games for Windows LIVE.** A replacement `xlive.dll`, written from scratch, gives the game a signed-in profile, saves, settings, achievements and networking, all without Microsoft's dead servers.
- **No SecuROM activation wizard.** The game starts straight from Steam's Play button, the desktop shortcut, or by double-clicking `Fable3.exe`.
- **All DLC, unlocked.** The installer downloads Microsoft's original Fable III PC DLC from Microsoft's own download server (Traitor's Keep, Understone, Inquisitor, the outfits, weapons, tattoos, potions and dog packs), checks it and installs it. No license files, no editing game data.
- **Saves that don't break.** Saves and in-game settings (controls, audio, misc) are kept as normal files, not tied to a LIVE profile.
- **Online co-op without Microsoft** [ need more testing ]. Host or join by IP, or find a host on your LAN.
- **Optional Xbox Live mode** [ need more testing ]. Microsoft's own online co-op with your Xbox account, set up automatically.
- **Display fixes.** Borderless window at your desktop resolution (including ultrawide), the Nvidia VSync/triple-buffering FPS fix, an optional frame cap, and no "Direct3D failed to initialise" crash when another program holds the screen.
- **Skips the intro logos.** Optional preload of the world textures for hard-drive PCs.

You need your own copy of Fable III (Steam). No game files are included.

> **Disclaimer:** the online features (our own co-op and Xbox Live mode) have not been tested yet.

## Install

1. Download `Fable3Restored-Setup.exe` from Releases.
2. Run it. It finds your Fable III folder automatically, or you can pick it with **Browse**.
3. Click **Install**. The DLC download is 1.3 GB.
4. Play from Steam, the desktop, or the Start menu.

Uninstall from Windows *Apps & features* or the Start menu. Your original files are put back.

## Xbox Live mode (optional) [ need more testing ]

If you want Fable III's original online co-op through Microsoft (orbs, invites, your Xbox friends), tick **"Also set up Xbox Live mode"** in the installer. It does the Steam-guide steps automatically:

- installs Games for Windows LIVE 3.5.92 and Marketplace 3.5.67 from Microsoft's own download server, removing older versions first
- builds the merged DLC folder GFWL expects (hard links, no extra space) and adds the Traitor's Keep license files

Start **Fable III (Xbox Live mode)** from the Start menu, or pick it in Options. The launcher then sets our `xlive.dll` aside, turns on Windows 7 compatibility and admin for the game, and starts the original launcher.

You still do two things yourself, once:

1. Click through the SecuROM activation wizard.
2. Sign in with your Xbox account in the game (press **Home**).

After the first sign-in, the launcher gives your profile the DLC license, so restart once. Normal mode always switches everything back.

## Options and co-op

Start menu → **Fable III Restored → Options and Co-op**, or hold **Shift** while starting the game.

- **Hero name** shown to your co-op partner.
- **Display:** borderless window, fullscreen or window. The resolution can be `auto` (your desktop) or any size, e.g. `3440x1440`.
- **Co-op** [ need more testing ]
  - The host clicks **Host co-op**, then opens their world to co-op in game.
  - The partner types the host's IP address and clicks **Join co-op**.
  - Over the internet, the host forwards UDP ports 3074 and 27421 on their router, or both players use a LAN/VPN such as ZeroTier or Radmin VPN.

All settings are kept in `Fable3Restored.ini` in the game folder.

## What's inside

| Part | File | What it does |
|---|---|---|
| GFWL replacement | `src/xlive/` → `xlive.dll` | profile, settings, saves, DLC licensing, sessions, sockets, voice stub, display fixes |
| Launcher | `src/launcher/` → `FableLauncher.exe` | replaces the SecuROM launcher Steam starts; writes the chosen resolution; options and co-op window |
| Installer | `src/installer/` → `Fable3Restored-Setup.exe` | one file; finds the game, installs, downloads and verifies the DLC, Nvidia profile, shortcuts, uninstall |

How the game was analysed and which fixes were needed: [docs/RE_NOTES.md](docs/RE_NOTES.md).

## Build

You need MSYS2 with the `mingw-w64-i686-gcc` toolchain (32-bit, because the game is 32-bit).

```
./build.sh            # build/xlive.dll, build/FableLauncher.exe, build/Fable3Restored-Setup.exe
./build.sh install    # copy xlive.dll into the game folder (FABLE3_DIR overrides the path)
```

`run_test.sh` runs the game hidden and muted for automated tests. It has scripted input, screenshots, and its own settings folder.

## Thanks

The list of problems to solve came from the Fable III community: the Steam guides on GFWL, DLC and Nvidia FPS fixes, and PCGamingWiki. The code here is our own.

Fable III © Microsoft Corporation, developed by Lionhead Studios. This project is not affiliated with Microsoft. It contains no game code or game files.
