# stalker-savesync

> **⚠️ Testing.** Back up your saves folder (`Anomaly\appdata\savedgames`) before using it.

When the host saves, everyone in the co-op game gets a copy of that save.
Anyone can then load it and host next time.

Works with xrRazom co-op over Steam. Everyone in the group needs it installed.

## Install

1. Close the game.
2. Download `stalker-savesync.zip` from the [Releases page](https://github.com/Andrew1431/stalker-savesync/releases/latest).
3. Unzip it **into your Anomaly folder** (the folder that has `bin` and `gamedata` in it).
   You should now have `Anomaly\stalker-savesync\install.bat`.
4. Double-click `install.bat`.

Done. Play like normal.

## Using it

- When the host saves, the save shows up for everyone a few seconds later.
- In the Load menu it's called `sync-<host name> - <save name>`.
- Joining a game also gives you the host's latest save.
- To take over hosting: load the newest `sync-...` save and host. You play your
  own character; the old host gets theirs back when they join.
- Older saves (made before you had savesync) work too, as long as you were in
  them. The first time, the game saves and reloads once by itself.

## After an xrRazom update

The update turns savesync off. Just double-click `install.bat` again.

## Uninstall

Close the game and double-click `uninstall.bat`. Your saves are not touched.

## Manual install (if install.bat doesn't work, or on Linux)

1. In `Anomaly\bin`, rename `steam_api64.dll` to `steam_api64_real.dll`.
   (Skip this if `steam_api64_real.dll` is already there: you're updating.)
2. Copy `savesync.dll` from the zip into `Anomaly\bin` and rename it to `steam_api64.dll`.
3. Copy `savesync.script` from the zip into `Anomaly\gamedata\scripts`
   (create the `scripts` folder if it doesn't exist).

To undo: delete `bin\steam_api64.dll`, rename `steam_api64_real.dll` back to
`steam_api64.dll`, and delete `gamedata\scripts\savesync.script`.

## Problems?

- Everyone (especially the host) must have it installed.
- Check `Anomaly\bin\savesync.log` and send it to whoever set this up.

## Building (devs only)

Needs Visual Studio 2022+ with C++ tools. Run `package.bat` → `build\stalker-savesync.zip`.
If xrRazom ships a new Steam DLL, regenerate exports with
`python tools/gen_forwards.py path\to\original\steam_api64.dll`.
