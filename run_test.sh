#!/bin/bash
# Hidden + muted test run of Fable3.exe with our xlive.dll.
#   ./run_test.sh <quit_ms> [shots_ms_csv] [keys "ms:vk,..."]
# Env passthrough: F3R_TRACE=1, F3R_WATCHMAP=1.
# The game gets its own APPDATA (work/test_appdata) so the player's settings are never touched;
# the Saved Games folder is backed up before and restored after every run.
cd "$(dirname "$0")"
ROOT="$(pwd)"
GAME="${FABLE3_DIR:-/e/SteamLibrary/steamapps/common/Fable 3}"
OUT="$ROOT/work/test"
TAPP="$ROOT/work/test_appdata"
SAVES="$(cygpath -u "$USERPROFILE")/Saved Games/Lionhead Studios/Fable 3"
rm -rf "$OUT"; mkdir -p "$OUT" "$TAPP/Lionhead Studios/Fable 3"
QUIT=${1:-30000}; SHOTS=${2:-}; KEYS=${3:-}
cp build/xlive.dll "$GAME/xlive.dll"
touch "$ROOT/work/test.ini"
# deterministic windowed 1280x720 test config
cat > "$TAPP/Lionhead Studios/Fable 3/VideoOptions.xml" <<'EOF'
<?xml version="1.0"?>
<VideoOptions>
	<Version type="uint">3</Version>
	<AdapterOrdinal type="uint">0</AdapterOrdinal>
	<ScreenResolutionX type="int">1280</ScreenResolutionX>
	<ScreenResolutionY type="int">720</ScreenResolutionY>
	<Fullscreen type="bool">False</Fullscreen>
	<VSync type="bool">False</VSync>
	<TextureDetail type="int">2</TextureDetail>
	<ModelDetail type="int">2</ModelDetail>
	<WaterDetail type="int">2</WaterDetail>
	<ShadowDetail type="int">0</ShadowDetail>
	<EffectsDetail type="int">2</EffectsDetail>
	<EnvironmentDetail type="int">2</EnvironmentDetail>
	<TreeDetail type="int">2</TreeDetail>
	<DrawDistance type="int">2</DrawDistance>
	<Calibrated type="bool">True</Calibrated>
</VideoOptions>
EOF
rm -f "$TAPP/Lionhead Studios/Fable 3/abnormal_shutdown.tmp"
SBK="$ROOT/work/saves_during_test"
rm -rf "$SBK"; [ -d "$SAVES" ] && cp -r "$SAVES" "$SBK"
WOUT=$(cygpath -w "$OUT")
cd "$GAME"
APPDATA="$(cygpath -w "$TAPP")" F3R_INI="$(cygpath -w "$ROOT/work/test.ini")" F3R_PAD="${F3R_PAD:-}" F3R_WATCHMAP=${F3R_WATCHMAP:-0} F3R_TRACE=${F3R_TRACE:-0} F3R_TEST=1 F3R_QUIT=$QUIT \
  F3R_SHOTS=$SHOTS F3R_KEYS=$KEYS F3R_SHOTDIR="$WOUT" timeout $(( QUIT/1000 + 60 )) ./Fable3.exe
echo "exit $?"
cp "$GAME/Fable3Restored.log" "$OUT/" 2>/dev/null
# keep what the test wrote for inspection, then put the player's saves back exactly
if [ -d "$SBK" ]; then
  rm -rf "$OUT/saves"; cp -r "$SAVES" "$OUT/saves" 2>/dev/null
  rm -rf "$SAVES"; cp -r "$SBK" "$SAVES"; rm -rf "$SBK"
fi
ls "$OUT"
