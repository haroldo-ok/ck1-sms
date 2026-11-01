# Commander Keen — Episode 1 for the Sega Master System

A port of *Commander Keen: Invasion of the Vorticons, Episode One* to the
Sega Master System, written in C for **devkitSMS / SDCC / SMSlib**.
`keen.sms` is a 432 KB ROM (27 banks, battery-backed cartridge RAM for
saves and high scores).

**Every asset is converted from the original game files** (`*.CK1` and
`KEEN1.EXE`, which you must supply — they are not included): levels, tiles,
sprites, fonts, screens, story/help/ordering text, strings, sounds and the
tile behaviour tables. Behaviour (physics, pickups, doors, hints, ship
parts, teleporters, ending) follows the original as documented by
[Commander Genius](https://gitlab.com/Dringgstein/Commander-Genius).

## What's in it

* Title with the Apogee / id logos, the original main menu (New Game,
  Continue Game, Story, About ID, High Scores, Ordering Info, Previews!),
  idle attract mode that cycles to the high-score screen.
* The Mars world map, all 16 levels, the world-map and in-level
  teleporters, ship-part collection, lives, extra lives at 20 000 points,
  game over with high-score name entry.
* Original physics (jump height from button hold, pogo, ice sliding),
  keycard doors, hint statues, ammo/pogo/key pickups, animated tiles
  (the original's 4-frame tile cycles), the status box and pause menu.
* **Save Game / Continue Game**: press PAUSE on the map for Status /
  Save Game / Resume. Three save slots plus the high-score table live in
  cartridge RAM.
* The ending: "Keen's ship" flight over the original starfield, the
  finale picture with the original captions, the end text pages and the
  high-score table.
* Full-size enemies: yorps (16x24), gargs and vorticons (24x32), robots
  (16x16) and tanks (16x24), the tank ray, ice cannons and chunks, all
  cut from `EGASPRIT.CK1`.

## Controls

| Input           | Action                                          |
|-----------------|-------------------------------------------------|
| D-pad           | Walk / steer (4-way on the map)                 |
| **Button 1**    | Jump / enter a level / confirm                  |
| **Button 2**    | Fire the raygun / go back                       |
| **Down + B1**   | Mount / dismount the pogo stick                 |
| **PAUSE**       | Status (level) / Status, Save, Resume (map)     |
| Button 2 on "New Game" | Cheat start: pogo stick + 99 shots       |

## Building

Requirements: `sdcc` ≥ 4.2, `python3` + Pillow, `make`. The devkitSMS
pieces (`SMSlib/`, `crt0_sms.rel`, `ihx2sms`) are vendored.

```
export CK1_DIR=/path/to/original   # KEEN1.EXE + *.CK1  (default: ./original)
make                               # converts the assets, then builds keen.sms
make OPT=                          # quick dev build (see Performance)
```

The release build compiles `main.c` with `--max-allocs-per-node 100000`,
which takes several minutes but gives SDCC room to keep values in
registers; `make OPT=` skips that for fast iteration.

## Layout

* `tools/ck1lib.py` — LZW/RLE decoders, EGA tile/sprite/bitmap readers,
  level loader, tile-info and string extraction from the unpacked EXE.
  `tools/unlzexe.py` unpacks the LZEXE-compressed `KEEN1.EXE`.
* `tools/convert_ck1.py`, `ck1front.py`, `ck1ui.py` — build everything
  in `gen/` (level banks, sprite banks, text/screens, animated tiles).
* `src/main.c` — the fixed-ROM engine (scrolling, physics, entities,
  map, status dialogs, ship flight).
* `src/ovl_front.c` — a **code overlay** living in its own switchable
  bank: logos, menu, text viewer, high scores, saves, finale. Overlays
  are full-screen modes; they reach other banks through the fixed
  helpers declared in `src/engine.h`.
* `tests/*.txt` — scripts for the headless `smstest` emulator
  (boot, jumps, map, doors, hints, items, saves, game over, ending,
  enemies, animation).
* `perf/` — the benchmark and the checks used while optimising (see
  Performance).

## Design notes

* The fixed ROM (banks 0–1) holds code only; level data, art and text sit
  in slot-2 banks that are paged on demand. Sprite art is streamed into
  VRAM: seven 12-tile slots hold the visible enemies (24x32 art loads in
  two halves), the player uploads one frame at a time.
* Animated tiles: quadrants that change across a tile group's frames
  become "live" subtiles; up to six are re-uploaded per frame in
  VBlanks that carry no other art burst.
* The camera moves at most 8px a frame, since the name table ring gets
  exactly one new column and one new row per frame; jumps go through a
  full redraw.
* Levels are stored as 16x16 metatiles over de-duplicated, flip-aware
  8x8 subtiles; the name table is a 32x28 ring buffer scrolled eight ways.

## Performance

The game logic runs once per video frame (60 Hz); a frame whose work
doesn't fit makes the game slow down. Measured on the headless emulator
with the same 10-second run-and-jump input on every level (`perf/run.sh`;
Keen made invulnerable so he stays in the level; level 5 starts leftward
so he doesn't reach its exit), as
game-loop iterations per 600 video frames (600 = full speed):

| Level | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| originally | 507 | 601 | 349 | 592 | 547 | 594 | 360 | 299 | 516 | 343 | 523 | 191 | 350 | 256 | 428 | 304 |
| faithful AI (release) | 597 | 600 | 356 | 600 | 600 | 600 | 596 | 599 | 599 | 601 | 598 | 373 | 599 | 599 | 581 | 588 |

The state-for-state AI costs more than the simplified one did, so levels 3
and 12 (eighteen enemies packed into a small area, all inside the wake
window) now run at about 60% speed when most of them are on screen; every
other level runs at 96-100%.

* **Entities are an array of structs.** The one being updated is copied
  into a global (`E`), whose fields SDCC addresses directly; indexed
  `y_px[i]`-style access cost ~100 cycles a field.
* **Fewer entities are visited.** The near list is now a box around the
  AI wake window (it was ±1 screen *columns* with no vertical limit), and
  the sprite pass walks only entities the AI pass found near the screen.
* **Hot leaves in assembly**: the collision lookup `mflag` (~650 → ~190
  cycles), the metasprite drawer (one routine for every actor, ~150
  cycles a hardware sprite, with per-sprite clipping), the animated-tile
  scan and upload, and the camera-relative position helper.
* **Less work per call**: walls are probed only when an enemy's leading
  edge enters a new tile column; ledges only after it actually moved;
  items only when Keen's cell rectangle changes; the animated-tile scan
  stops as soon as every tile is current for the step.
* **Strip rendering** fetches each metatile once per pair of tile rows,
  patches item/door overrides into a row in one pass instead of a lookup
  per tile, and replaces the `% 28` divisions with subtraction.
* Release builds use `--max-allocs-per-node 100000`.

`perf/` holds the tools: `prof.sh` (busy cycles per game loop, via
`perf/gen.py`'s benchmark scripts and smstest's profiler), `lines.py`
(profile buckets attributed to C source lines), `ntcheck.py` +
`ntscen.py` (every visible name-table cell checked against the level
data after scrolling around six levels and collecting items) and
`animcheck.py` (every animated tile in VRAM checked against its frame).
The ROM keeps a few bytes of benchmark hooks: `perf_loops`/`perf_vbl`
counters and a `perf_on = 2` invulnerability flag that only a debugger
or the test harness can set.

## Fidelity notes

* **Enemy AI** is ported state for state from Commander Genius' `CYorp`,
  `CGarg`, `CVorticon`, `CButler`, `CTank`, `CIceCannon`/`CIceChunk` and
  `CRope`: same states, timers, probabilities (`rand()%1000 <= n`), speeds
  (CG units x 8 in 8.8 fixed point), gravity, friction, hit points, armour,
  per-sprite hit boxes and the CG activation window.
* **Ice cannons** fire every 65 ticks, chunks smash into four ice bits on
  walls, and a hit freezes Keen in the ice cube (full-height kick, slide
  with friction, 250 ticks; a ray thaws him).
* **The level 16 chandelier rope** works: shoot it and the stone drops four
  rows (tile rows move in a RAM copy of the map), shaking the screen and
  crushing whatever it lands on.
* **Walk-behind exit door**: Keen is cut at the pixel at the doorway when he
  leaves a level.
* **PAL consoles** run a sixth logic tick every fifth frame so the game keeps
  its 60 Hz speed (detected from the VDP V counter at boot).
* **Colours**: all 16 EGA colours are exactly representable (2 bits per
  channel) and every background tile uses them. A sprite has only 15 opaque
  colours plus transparent, and Keen's frames use all 16, so one near-duplicate
  EGA colour is merged in sprites. That is a hardware limit.
* Sound is the original PC-speaker data played on the PSG.

## Where the code lives

The fixed ROM (banks 0-1) is the engine core; the enemy / projectile / rope
code is a separate translation unit (`src/ai.c`, shared state declared in
`src/shared.h`) linked into its own switchable bank and paged into slot 2 for
one tick at a time (`ai_run`). Wrappers that need the level bank
(`mflag_a`, `nt_update_cell_a`) live in fixed ROM because they switch slot 2.
Link order of the bank objects must equal their physical bank order
(`gen/banks.mk`).
