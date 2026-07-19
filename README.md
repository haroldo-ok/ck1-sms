# Commander Keen — Sega Master System port

A from-scratch Sega Master System engine that plays **all 16 levels of the
original *Commander Keen: Invasion of the Vorticons, Episode One*** plus its
real Mars overworld, built with **devkitSMS / SDCC / SMSlib**.

`keen.sms` is a 224 KB SEGA-mapper ROM. It boots to the title screen, drops
you on the original Mars world map, and lets you play every level from the
shareware episode, collect the four ship parts, and reach the ending.

The engine grew out of a port of [HTML5-Keen](https://github.com/roganty/HTML5-Keen)
(a melonJS demake of the first three levels); Keen's own sprite art and the
title screen still come from that project, but the **levels, world map,
tiles, enemies and tile behaviours are converted straight from the original
`*.CK1` game files** using CloneKeen's file-format source as documentation.

## Controls

| Input            | Action                                   |
|------------------|------------------------------------------|
| D-pad            | Walk / steer (4-way on the overworld)    |
| **Button 1**     | Jump / enter level                       |
| **Button 2**     | Fire the raygun                          |
| **Down + B1**    | Mount / dismount the pogo stick          |
| **Into a door**  | Walk into a locked door holding the matching keycard to open it |

Bounce on a yorp's head to stun it; shoot enemies to defeat them (gargs and
vorticons take several hits; the robotic guards and tanks are bulletproof —
dodge them). Grab keycards to open the coloured doors, pick up the four ship
parts hidden across the levels, and walk into a level's exit hatch to clear
it. Collect **all four ship parts** and finish the level you're in to win.

## Building

Requirements: `sdcc` (tested with 4.2), `python3` + Pillow, `make`.
The devkitSMS pieces (`SMSlib/`, `crt0_sms.rel`, `ihx2sms`) are vendored.

```
make assets    # regenerate gen/ from the original CK1 data + Keen art
make           # -> keen.sms
```

`make assets` runs `tools/convert_ck1.py`, which needs three input trees
(paths are constants at the top of the script):

* the original Keen 1 data files — `EGAHEAD.CK1`, `EGALATCH.CK1`,
  `EGASPRIT.CK1`, `LEVEL01.CK1`…`LEVEL16.CK1` and `LEVEL80.CK1` (the world
  map). These are **not** included; supply your own copy of the shareware.
* CloneKeen's extracted episode-1 tile-attribute table (`ep1attr.dat`),
  used for per-tile collision / pickup / door / exit behaviour.
* the HTML5-Keen asset tree, for Keen's sprite sheet, the overworld Keen
  sprite, the bullet, the yorp and the title screen.

## How the conversion works

`tools/ck1lib.py` decodes the original formats:

* **LZW** — Keen's variable-width (9–12 bit) MSB-first bitstream, used to
  compress `EGALATCH`/`EGASPRIT`. Codes grow one step early and the new
  dictionary entry is defined before the current code is emitted.
* **EGA tiles** — `EGALATCH` stores 611 16×16 background tiles as four
  1-bit colour planes; `ep1attr.dat` gives each tile its solidity, whether
  it's a pickup / door / exit, its point value and its post-pickup
  replacement tile.
* **EGA sprites** — `EGASPRIT` stores each sprite as four colour planes
  plus a mask plane (bit set = transparent), each plane one continuous
  bitstream across all sprites, described by an `EGAHEAD` table (whose
  records repeat four times). The yorp, garg, vorticon, robot guard,
  tank, enemy ray and ice-chunk art are all cut straight from it
  (all enemies come from the original data files). One decoder
  subtlety cost a lot of debugging: at 12-bit codes Keen's LZW still
  defines dictionary entry 4095 (adding stops only at 4096); stopping one
  entry early makes code 4095 decode to an empty string, silently
  dropping bytes mid-stream and shifting every later sprite's planes.
* **Levels** — a dword length followed by RLE-compressed 16-bit words
  (`0xFEFE` marker / count / value). Header word 7 is the plane size; the
  tile plane starts at word 16 and the object plane just after it. Object
  value 255 marks Keen's spawn; values 1–9 are enemies.

`tools/convert_ck1.py` turns that into the SMS engine's data:

* Each 16×16 tile is cut into four 8×8 SMS tiles with horizontal/vertical
  **flip dedup**, so mirrored scenery shares VRAM; priority tiles set the
  name-table priority bit. Every level's subtile pool fits the 256-tile
  BG budget (world map: 323, under its own budget).
* Per level it emits the metatile definitions + collision flags, the tile
  map, and flat tables of **items** (with kind + replacement tile),
  **doors** (colour + replacement), **exits**, and **enemies**.
* The world map emits one **entry** per city cell (map coords → level
  number, plus the "completed" tile to stamp once beaten).
* Everything is auto-packed into 16 KB ROM banks (a level's tile blob and
  its map data may land in different banks); `gen/banks.mk` is generated so
  the Makefile always links exactly the banks that were produced.

## Architecture

```
banks 0-1   fixed: engine code + LevelDesc tables + palettes + 1bpp font
bank 2      all sprite art (Keen, yorp, garg, vort, guard, tank, ray, chunk)
bank 3      title screen
banks 4-13  the 17 maps (16 levels + world), tile pools + map data, auto-packed
```

* **World format** — 16×16 metatiles (2×2 SMS tiles) with per-metatile
  collision flags (solid / one-way platform / deadly). Maps and metatile
  definitions are read directly from the mapped ROM bank; only the flags
  table (≤256 B) is copied to RAM. Pickups, opened doors and completed
  cities push onto a small RAM **override list** consulted by the renderer;
  collision flags themselves never change, so physics probes stay cheap.
* **8-way scrolling** — the 32×28 name table is a ring buffer on both axes.
  Crossing an 8-px camera boundary queues the entering column/row strip,
  baked into RAM buffers at the end of the frame (map reads, override
  lookups, metatile math, even the VRAM address stream) and blitted in
  the next VBlank as pure port writes, in the same frame that moves the
  scroll registers. Vertically the name table is 224 px tall but only
  192 px shows, so rows always enter in the off-screen band;
  horizontally the hardware's blanked left column gives an 8-px cushion:
  while the camera sits inside a tile, the wrapping column's stale half
  is entirely under the blank, so a swap done during VBlank is invisible
  at both edges. Drawing strips a frame late instead is what produced
  the 1-3 px "loading seams". **Everything that hits the VDP with raw
  ports or `UNSAFE_*` OUTI bursts fits inside the ~16 k-cycle VBlank
  window** (measured worst case ~9 k, enforced by a test): past the end
  of VBlank the VDP silently drops over-fast writes, which corrupts the
  sprite table and streamed art. On frames where a column and a row both
  cross (diagonal scroll), sprite-art streaming is deferred one frame to
  keep the margin. Teleporter full-redraws happen with the display on
  and use a paced writer instead. The strip/camera math is fuzz-tested
  (480 k random camera steps).
* **Entities** — a single typed entity system (`ent_update`) drives yorps
  (hop + chase, head-bump stun), gargs (wander + charge, ledge-aware),
  vorticons (stalk + jump, multi-hit), robot guards and tanks (patrol,
  bulletproof; tanks fire a ray), and ice cannons (fire ice chunks).
  Killed enemies play their original dying frame and leave a corpse
  (a live enemy can take over a corpse's art slot if all are busy). Up to
  **eight** on-screen entities share eight VRAM streaming slots, one sprite
  upload per frame; offscreen entities skip their AI.
* **Doors & keycards** — each colour's cells open together when Keen bumps
  one holding the matching card; opened cells become walk-through via the
  override list and a door-forgiveness check in the collision probe.
* **Win condition** — the four ship parts (joystick, battery, vacuum, fuel)
  are ordinary pickups that set inventory bits; finishing a level with all
  four triggers the ending screen.
* **World-map teleporters** — the Mars teleporter pair (object markers
  38/41 in the original data) is emitted as a src-cell → dest-cell table.
  Standing on a pad and pressing a button warps Keen to its partner and
  snaps the camera / redraws the name table.
* **Performance** — pickups are checked against a column-sorted item
  table through a cached window (a handful of items per frame instead of
  the whole table); entity AI and sprite building walk a page-prefiltered
  near list; entity sprites are written straight into SMSlib's sprite
  buffers; and picked-up/opened/completed cell overrides are kept
  row-bucketed so scroll strips look up a cell's override in O(row
  entries) — with 100+ items collected a naive full-list scan per cell
  used to blow entire frames on every scrolled column.
* **Sound** — all effects are the ORIGINAL PC-speaker sounds converted
  from `SOUNDS.CK1` (word at 0x06 = count; 16-byte directory entries
  from 0x10 with data offset, priority and a 12-char name; data = 16-bit
  words where 0 is a silent tick, 0xFFFF ends the sound, and any other
  value is a PC timer divisor). The SMS PSG clock is exactly 3x the PC
  timer clock, so each divisor converts to a PSG tone period as
  `word * 3 / 32`. Playback advances at the original ~44 values/sec
  (44 ticks per 60 frames via an accumulator) on one tone channel, with
  the original priority rule: a new sound only replaces the current one
  if its priority is at least as high. 31 sounds are wired up: jump,
  land, pogo bounce, high pogo jump, head bump, walk ticks, wall block,
  fire, empty-gun click, all pickups, doors, level jingles, teleporter,
  shot hits, the three enemy screams, yorp bop and shove, tank and
  cannon fire, the freeze hit, plummet and death.
* **Physics (pogo per the original)** — 8.8 fixed point (walk 2.25 px/f, gravity ≈0.15, jump −4,
  pogo with squat + auto-bounce and air steering, 14-frame shoot freeze).
* **Input** — button edges are detected in the game loop against its own
  previous sample (not `SMS_getKeysPressed`, whose ISR-side edge pair can
  drop a press on a frame overrun), plus a 5-frame jump buffer. Verified:
  20/20 two-frame (33 ms) taps register while walking.
* **Audio** — small procedural PSG driver (tone ch2 + noise ch3): jump,
  pickup, keycard, shoot, zap, bump, land, die, exit, enter jingles.

## Simplifications

These trade fidelity for fitting the SMS and a reasonable code size; all are
cosmetic or minor gameplay:

* Background tiles don't animate (each animated tile shows its first frame).
* Vorticons (24×32 in the original) are center-cropped to 16×24 hardware
  sprites; the robot guard's 16×16 art is padded to the shared 16×24 slot.
* Ice chunks **stun** Keen briefly instead of freezing him in an ice cube.
* The chandelier rope (level 16) is inert — vorticons are simply shootable.
* The level-13 secret-level teleporter is inert; the two Mars world-map
  teleporter pads work (stand on one, press a button to warp to its pair).

## Tools

* `tools/ck1lib.py` — decoders for the original CK1 formats (LZW, EGA latch
  tiles, EGA sprites, tile attributes, RLE levels).
* `tools/convert_ck1.py` — the full converter: original data + Keen art →
  `gen/` banks, `game_data.c/.h` and `banks.mk`.
* `tools/smssim.py` — minimal headless SMS emulator (around the `z80` pip
  package) that boots the ROM, scripts the pads and saves screenshots.
* `tools/ck_playtest.py` — end-to-end test: BFS-navigates the real world map
  to the level-1 city, enters it, platforms/jumps/shoots, walks into the
  exit, then asserts the level is marked done, that completed cities refuse
  re-entry, and (after granting the parts) that finishing a level reaches
  the win state — all read back from emulated RAM.
* `tools/jumptest.py` — input reliability: 20 two-frame taps, each must jump.
* `tools/tptest.py` — walks Keen onto a Mars teleporter pad and asserts he
  warps to the paired pad and back.
* `tools/seamtest.py` — scrolls the camera up repeatedly in level 1 and
  verifies the top visible name-table row always matches the map before the
  scroll register reveals it (guards against the vertical "loading seam").
* `tools/sndtest.py` — captures PSG port writes during a jump and
  verifies the emitted tone periods match the KEENJUMPSND data converted
  from `SOUNDS.CK1`, including the priority interruption when Keen bumps
  his head.
* `tools/pogotest.py` — verifies the three vertical impulses (jump
  -1024, pogo bounce -980 ~= 0.92x jump height, held-jump pogo -1474 ~=
  2.07x jump height, calibrated from CloneKeen's jump model).
* `tools/vblanktest.py` — jump-runs through level 13 and measures, on
  every non-overrun frame, how long the engine's VBlank section takes
  (SAT copy + scroll + art streaming + strip blits); fails if any frame
  exceeds the hardware VBlank window, since spilling drops VRAM writes
  and corrupts sprites.
* `tools/nttest.py`, `tools/owtest.py` — name-table / scroll validation
  against the map data (these reference the earlier three-level build).
