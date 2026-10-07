#!/usr/bin/env bash
# stalker-savesync uninstall for Linux (Proton/Wine). Run: bash uninstall.sh
cd "$(dirname "$0")" || exit 1
BIN="../bin"

done_msg() { echo; echo " $1"; echo; exit "${2:-0}"; }

[ -f "$BIN/steam_api64.dll" ] || done_msg "Can't find your game. This folder must be inside your Anomaly folder." 1
if pgrep -fi 'anomaly[a-z0-9]*\.exe' >/dev/null 2>&1; then
    done_msg "Close the game first, then run this again." 1
fi

if grep -q "StalkerSaveSync_Version" "$BIN/steam_api64.dll"; then
    [ -f "$BIN/steam_api64_real.dll" ] ||
        done_msg "The original steam_api64.dll is missing. Reinstall xrRazom to fix it." 1
    mv -f "$BIN/steam_api64_real.dll" "$BIN/steam_api64.dll" ||
        done_msg "Couldn't restore the original file. Make sure the game is closed." 1
else
    # Already Valve's DLL (e.g. xrRazom update replaced it); just tidy up.
    rm -f "$BIN/steam_api64_real.dll"
fi
rm -f "$BIN/savesync.log" ../gamedata/scripts/savesync.script

done_msg "Uninstalled. Your saves were not touched."
