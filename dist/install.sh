#!/usr/bin/env bash
# stalker-savesync install for Linux (Proton/Wine). Run: bash install.sh
cd "$(dirname "$0")" || exit 1
BIN="../bin"

done_msg() { echo; echo " $1"; echo; exit "${2:-0}"; }

[ -f "$BIN/steam_api64.dll" ] || done_msg "Can't find your game. Put this \"stalker-savesync\" folder INSIDE your
 Anomaly folder (the one that has \"bin\" and \"gamedata\" in it), then run this again." 1
[ -f savesync.dll ] || done_msg "savesync.dll is missing from this folder. Download the zip again." 1
if pgrep -fi 'anomaly[a-z0-9]*\.exe' >/dev/null 2>&1; then
    done_msg "Close the game first, then run this again." 1
fi

if grep -q "StalkerSaveSync_Version" "$BIN/steam_api64.dll"; then
    [ -f "$BIN/steam_api64_real.dll" ] ||
        done_msg "The original steam_api64.dll is missing. Reinstall xrRazom, then run this again." 1
else
    # Valve's DLL is in place: fresh install, or xrRazom was updated.
    cp -f "$BIN/steam_api64.dll" "$BIN/steam_api64_real.dll" || done_msg "Something went wrong copying files." 1
fi
cp -f savesync.dll "$BIN/steam_api64.dll" || done_msg "Something went wrong copying files." 1
mkdir -p ../gamedata/scripts
cp -f savesync.script ../gamedata/scripts/savesync.script || done_msg "Something went wrong copying files." 1

done_msg "Installed! Start the game like normal."
