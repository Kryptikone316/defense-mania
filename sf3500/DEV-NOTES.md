# Defense Mania on the Data Frog SF3500: development notes

For anyone building, testing or changing the port. Players want the setup guide, not this file.

## 1. Builds

| What | How | Result |
|--|--|--|
| Release (what is shipped) | `MIPS_PREFIX=... SYSROOT=... SDL_TTF_DIR=... ./build_sf3500.sh` | `defensemania.stripped.mipsel`, about 470 kB |
| Test build for the device or QEMU | the same with `DEV=1` | `defensemania-dev.stripped.mipsel`, objects in `obj-sf3500-dev/` |
| Native (x86) test build | a script outside the repository (`defensemania-asan/build_native.sh`) with `-DDM_TESTHOOKS` and a stub SDL_ttf; also an ASan and a TSan variant | `defensemania_native` |

`-O2` only (no `-ffast-math`, no `-mdspr2`: a device-only fault was traced to them in the Quake ports). The release build is checked with
`strings defensemania.stripped.mipsel | grep -c "DM_\|DMCFG\|DBG "`, which must print 0.

QEMU (`qemu-mipsel-static`) only exists on the host: `flatpak-spawn --host ...`. It runs the real fonts and the real MIPS code but has no display driver
(the game then runs silent and draws only to its canvas, which the test hooks can save).

## 2. Test hooks (`sf3500/devhooks.hpp`)

Everything the tests read from the environment goes through `dm_env()`. A release build does not define `DM_TESTHOOKS`, so `dm_env()` is a constant NULL, all of
this is dead code and the compiler removes it, strings included. Frame numbers below count frames since the program started.

| Variable | What it does |
|--|--|
| `DM_AUTOMAP=n` | skip the menus and play map `n` (the order of `maps.xml`) by itself: builds, upgrades, repairs and sells towers at speed, prints `DBG` status lines and a final `DBG RESULT` line, then exits |
| `DM_AUTODIFF=1..3`, `DM_AUTOFRAMES=n`, `DM_AUTOSPEED=x`, `DM_AUTOPAN=1`, `DM_AUTOENDLESS=1`, `DM_AUTOPLAY=1`, `DM_SEED=n` | difficulty, frame limit, game speed, camera panning, endless mode, autoplay without choosing a map, random seed of the autoplayer |
| `DM_PAD="frame:mask,frame:mask,..."` | a scripted pad. Mask bits: Left 0x1, Right 0x2, Up 0x4, Down 0x8, A 0x10, B 0x20, L1 0x40, R1 0x80, X 0x100, Y 0x200, Start 0x400, Select 0x800 (the order of `joykey.hpp`). The mask holds from its frame until the next entry; release with `frame:0` |
| `DM_SHOTDIR=dir`, `DM_SHOTS=n,n,...` | save those frames as `dir/frameNNNNN.ppm` (640 x 358). A negative number is that many frames after `gfx_mark_frame()` (used by `DM_SOLDKILL`) |
| `DMCFG_<key>=value` | overrides one line of `defensemania.cfg` |
| `DM_CURSOR=x,y`, `DM_BUILDAREAS=1`, `DM_HQ=0/1`, `DM_HELP=page`, `DM_AFTERVIEW=1` | start the cursor there, show the build areas, set HQ graphics, open How To Play on a page (0-based), open the end screen on the match stats |
| `DM_BASTION=0/1`, `DM_BASTION_SEL=n` | open the Bastion panel on the research or the actions tab with some tiers done and 300 gold |
| `DM_SOLDIERS=1`, `DM_SOLDKILL=n`, `DM_SOLDLOG=1` | send one soldier of each kind once the first wave starts, kill them all `n` frames later (to watch them fall), log `SOLDLOG`/`SOLDEND` lines |
| `DM_CAMKEEP[=dx]` | keep the camera on the keep (offset `dx`) |
| `DM_MONEY=n` | set the gold to `n` (a test of the seven-digit HUD) |
| `DM_FAKE_RESULT=waves`, `DM_FAKE_WIN=1`, `DM_FAKE_LIVES=n`, `DM_FAKE_DELAY=ms` | end the match after a moment with those results, to test scores, perfect marks and unlocks |
| `DM_ROUTELOG=1`, `DM_TRAPLOG=1`, `DM_ENDLESS_DUMP=1` | log which way each creature goes at the Maze junction, the traps and thrown boulders, and what each endless wave is made of |
| `DM_ALLTHROW`, `DM_THROWRANGE`, `DM_ALLFLY`, `DM_ARMOR` | make every creature a boulder thrower or a flyer, set the throw range or the armour |
| `DM_CRASH_TEST`, `DM_HITCH_MS` | crash after four seconds (tests the crash report), the frame-hitch threshold in ms |
| `DM_MUSICTEST`, `DM_MUSIC_STRESS`, `DM_FAKE_HWAUDIO` | music without an audio device, switch tracks at once and often, fake the hardware audio speed |
| `DM_DISPTEST*`, `DM_SLOTCHECK_*` | the display test and the slot check (see section 6) |

A test-only bot (`DM_SMARTBOT`, `DM_BOT_TYPES`, `DM_BOT_RESEARCH`, `DM_BOT_SOLDFIRST`, `DM_BOT_NOSOLDIERS`) plays with a tower mix and the Bastion; it is not in the
tree but kept as a patch (`defensemania-asan/smartbot_hook_new.patch`, `patch -p0 -F3 sf3500/main.cpp < patch`). Take it out again with `git checkout sf3500/main.cpp` before committing.

## 3. Data made by scripts (`tools/`)

Every script takes the data folder (`src/shared/data`) and is deterministic. Python with pillow and numpy; `make_coast.py` also needs scipy.

| Script | Makes |
|--|--|
| `make_towers.py` | `maps/*/towers.xml` from one table of tower stats (the same towers on every map) |
| `make_tower_art.py`, `make_trap_art.py`, `make_monster_art.py` | the Resupply Depot and Ballista level 3, the three traps, the new creatures (recoloured sheets and icons) |
| `make_waves.py [map ...]` | `waves.xml` from one creature table and one plan per map; the health follows from the load each wave is meant to ask for. With map names it rewrites only those |
| `make_research.py` | `research.xml`: the Bastion's research lines (three tiers each) |
| `make_soldiers.py <data> [renders]` | `soldiers.xml` and the soldier sheets (384 x 512: walk, strike, die, four directions) from the Blender renders |
| `make_bastion_art.py <data> [render]` | the keep (64 x 128) |
| `tools/blender/soldiers.py`, `bastion.py`, `lib.py` | the Blender 4.0 models: `blender -b -P soldiers.py -- <out>` |
| `make_swamp.py`, `make_tundra.py`, `make_coast.py` | the three maps added for this port: picture, `maptree.xml`, `waypoints.xml` (`make_coast.py` also writes `towers.xml` and the map icon) |
| `make_hud_variants.py` | `hud/<map>.png`, the top-left plank per map |
| `make_gold_icon.py` | `money.png`, `money_small.png` (the gold nugget) |
| `make_sfx.py`, `make_loudness.py` | the sound effects (CC0); the loudness table of the music pack |
| `check_maps.py`, `propose_rebalance.py` | is every map winnable (the share of the ideal damage a win needs) and how much to cut when it is not |

## 4. Maps and the files that drive them

* `maps.xml` lists the maps in the order of the list. `unlock="normal-all"` on a map keeps it closed until every other map has been won on Normal (The Coast).
* A map folder holds `maptree.xml` (three layers; layer 2 decides what can be built: `-1,-1` free, `1,0` path, `3,0` blocked; the generated maps mark only their tower spots free),
  `waypoints.xml`, `waves.xml`, `towers.xml`, `map_logo.png` and `data/map.png`.
* `waypoints.xml` may carry `<detour from="2" chance="50,30,10">x,y x,y ...</detour>`: at way point 2 a ground creature may go out along those points to the dead end and
  come back, with that chance on Easy, Normal and Hard. The Maze uses it for its northern loop. Flyers go straight. Traps can be placed along a detour.
* Progress is in `~/.defensemania_scores` (best scores, `won` and `perfect` masks per map) and `~/.defensemania_settings` (the options). A perfect score is decided by the match
  (won, every wave cleared, lives at the end at least the start) and stored with each score row.

## 5. Choices worth knowing

* The balance is meant to be checked, not guessed: change `make_towers.py` / `make_waves.py` / `make_research.py`, regenerate, run `check_maps.py`, then play.
* The gold counter has room for seven digits; the plank stretches to fit and the lives heart sits after them.
* Select pauses in a match (Options, Restart Level, Exit to Main Menu; the last two ask for a second press); Start opens the Bastion.
* Options has RESET PROGRESS (two confirmations): it empties scores, wins and perfect marks and keeps the settings.

## 6. Diagnostics and what the release leaves out

Left in the release because they are cheap and make bug reports useful: the crash report (signal, address and stack words written to `log.txt`), the frame-hitch lines
(at most 30), guards that refuse to draw a tower with an impossible position or a shot that is not on its way and that put the canvas pointers back if something overwrites them,
and the `music:` / `hwaudio:` lines.

Compiled out of the release (a `DEV=1` or native build has them):

* the test hooks above;
* `slotcheck.cpp` (`slot_check = 1` in the cfg): it compares every frame sent to the display driver with what the driver rendered into `/dev/fb0`;
* `disptest.cpp` (`DM_DISPTEST=1`): sends test patterns to the driver and reads them back;
* the per-frame ghost hunt in `Map::display` (a tower drawn last frame and not this one, or a tower picture drawn where no tower is).

They exist because of a rare one-frame glitch (a tower picture drawn in the wrong place for a moment). It was traced into the game's own canvas, not the driver; the cause was
not found, and its rate fell after the frame cap (30 per second) and an audio fix. The full investigation is in the handoff notes (sections 17 to 36).

## 7. Release checklist

1. `strings` check on the release binary (section 1), clean `git status`, no bot patch applied.
2. `check_maps.py` shows every map winnable on every difficulty to the extent the notes say.
3. A smoke run of every map with the native ASan build and one with the QEMU dev build; the device run of the release binary.
4. Package: binary, cfg, `data/` (only the files the game opens), boxart, licences, NOTICE, setup guide. No README edits in the collection pull request.
