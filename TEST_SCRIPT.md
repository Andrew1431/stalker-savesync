# savesync test script (v1.4.1)

Two people (A and B) can run everything except test 9. A third person (C)
covers a player who never hosts, and a Linux/Proton player covers test 10.

## Before you start (everyone)

1. Close the game. Copy your whole `Anomaly\appdata\savedgames` folder somewhere safe.
2. Install v1.4.1 (`install.bat`, or manual install from the README).
3. Start the game, then close it. `Anomaly\bin\savesync.log` should say `savesync 1.4.1 ready`.

## Make your characters easy to recognise

Each player should change these so the characters are clearly different, and write them down:

- money (buy or sell something so it's an odd number)
- outfit and helmet
- the weapon in your hands, with an attachment or upgrade, and how many rounds are in it
- one artefact on your belt
- quick slots 1–4
- health (take some damage) and a damaged limb if you can
- one odd item in your backpack (example: 3 cans of tushonka)

After every swap below, check your list against what you have.

## Tests

Write down the save name, who hosted, and the result (pass / fail / weird).

**1. Sync.** A hosts, B joins. A saves as `t1`.
- B gets `sync-A - t1` in their Load menu within about 10 seconds.
- Check `Anomaly\appdata\savedgames`: B should have `.scop`, `.scoc`, `.dds` and `.xrr_peers` for it.

**2. Take over.** Everyone quits. B loads `sync-A - t1` and hosts.
- About 2 seconds after pressing a key to start, B turns into B's own character. No extra reload.
- A joins and gets A's own character.

**3. Hand it back.** B saves as `t2`, everyone quits. A loads `sync-B - t2` and hosts. B joins.
- Both of you have your own characters, including anything you picked up in test 2.

**4. Load your own save.** A loads their original `t1` (not a `sync-` copy).
- Nothing changes; A plays as A.

**5. Quicksave and autosave.** Host presses F5, or travels to another level to trigger an autosave.
- The other player gets the save.
- Take it over like test 2. It should work the same.

**5b. Save from plain xrRazom.** Someone without savesync (or with it uninstalled) hosts and saves, and sends you the save files by hand.
- Put them in your `savedgames` folder and load: same result as test 8.

**6. Join quickly.** B loads a `sync-A` save, and A joins straight away, during B's loading screen if possible.
- A still gets A's own character.

**7. Save right after loading in.** B loads a `sync-A` save and saves within 2 seconds of pressing a key.
- Check nothing is duplicated or missing, either right away or after reloading that save.

**8. Old save.** Load a save someone else hosted before anyone had v1.4.1, one you were in as a client.
- You become your own character, then the game saves and reloads by itself once.
- The old host joins and gets their own character.
- Load the same save again: no second reload this time.

**9. Third player (needs C).** A hosts and A, B and C play; save. B takes over and A and C join.
- C's character is untouched the whole time.

**10. Linux/Proton.** Repeat tests 1 and 2 with the Linux player as the host, then as B.

**11. Reload twice.** After a takeover, save, then load that save twice in a row.
- No duplicated items and no changed money.

**12. Level change.** After a takeover, walk to another level.
- Your character survives the transition.

## When something fails

Note which test failed and what was wrong, then send these from the person who was hosting:

- `Anomaly\bin\savesync.log`
- `Anomaly\appdata\logs\xray_<your windows name>.log` (lines with `[savesync]` matter most)
- the save files (`.scop`, `.scoc`, `.xrr_peers`) for the save you loaded

If you get a crash window, take a screenshot of it.
