# Commander Keen — Sega Master System port

A port of [HTML5-Keen](https://github.com/roganty/HTML5-Keen) (a melonJS
Commander Keen 1 demake) to the Sega Master System, built with
**devkitSMS / SDCC / SMSlib**.

`keen.sms` is a 144 KB SEGA-mapper ROM. It runs the full game loop of the
original JS version: title screen → Mars overworld → three platform levels
→ win screen.

## Controls

| Input            | Action                                   |
|------------------|------------------------------------------|
| D-pad            | Walk / steer (4-way on the overworld)    |
| **Button 1**     | Jump / enter level                       |
| **Button 2**     | Fire the raygun                          |
| **Down + B1**    | Mount / dismount the pogo stick          |
| *Title screen:*  | hold **B2** while pressing **B1** = pogo + 99 ammo cheat |

Collect the pogo stick / keycards in the levels, bounce on a yorp's head to
stun it, shoot it to stop it for good. Walk into the exit door to clear a
level; clear all three to win. The rock gate east of the first city opens
once level 1 is beaten, unlocking the rest of the map (the `level-block`
object from the original's TMX data — its collision handling was commented
out in the JS, which made the blocker decorative; here it works as the
classic Keen progression gate).

## Building

Requirements: `sdcc` (tested with 4.2), `python3` + Pillow, `make`.
The devkitSMS pieces (`SMSlib/`, `crt0_sms.rel`, `ihx2sms`) are vendored.

```
make assets    # regenerate gen/ from the HTML5-Keen TMX/PNG data
make           # -> keen.sms
```

`tools/build_assets.py` expects the original HTML5-Keen asset tree (see the
ASSETS_DIR constant at the top of the script).

## Architecture

```
banks 0-1   fixed: engine code + LevelDesc tables + 1bpp font
bank 2      sprite art (streamed to VRAM at runtime)
bank 3      title screen (419 tiles + 32x24 map)
bank 4/5/6  level 1/2/3: subtile pool + metatile defs + map + objects
bank 7      overworld subtile pool (346 tiles)
bank 8      overworld map + objects (split: >16 KB together)
```

* **World format** — 16×16 metatiles (2×2 SMS tiles) with per-metatile
  collision flags (solid / one-way platform / deadly). Maps and metatile
  definitions are read directly from the mapped ROM bank; only the flags
  table (≤256 B) is copied to RAM. Item pickups go into a small RAM
  override list consulted by the renderer — collision flags never change,
  so physics probes skip the list entirely.
* **8-way scrolling** — the 32×28 name table is a ring buffer on both
  axes. Crossing an 8-px camera boundary queues the entering column/row
  strip, which is drawn right after the next VBlank (after the scroll
  registers), with the left-column blank hiding the horizontal seam. The
  strip/camera math is fuzz-tested in `tools/` (480 k random camera steps
  across all four map sizes).
* **Sprites** — Keen is 16×24 = six hardware sprites whose frame (192 B)
  is streamed into VRAM every VBlank. Up to three on-screen yorps share
  three streaming slots acquired/released by visibility, one upload per
  frame. Bullets use 12 static tiles loaded at level init.
* **Physics** — 8.8 fixed point, tuned to the melonJS original
  (walk 2.25 px/f, gravity ≈0.15, jump −4, pogo with squat+auto-bounce and
  air steering, 14-frame shooting freeze).
* **Input** — button edges are detected in the game loop against the
  loop's own previous sample (not `SMS_getKeysPressed`, whose ISR-side
  edge pair can consume a press whenever a frame overruns), plus a
  5-frame jump buffer so a press just before landing still jumps on
  touchdown. Verified in the emulator: 20/20 two-frame (33 ms) taps
  register while walking.
* **Performance** — offscreen yorps skip their AI (as in melonJS),
  scroll strips use incremental VRAM addressing (rows stream on the VDP
  auto-increment), item pickup scans are byte-prefiltered by cell
  distance, sprites take an unclipped fast path when fully on-screen,
  and the per-frame `%224`/`%3` divisions are gone. Sustained worst-case
  stress (running + jumping through yorp clusters) overruns ~25% of
  frames; normal play holds 60 fps.
* **Audio** — small procedural PSG driver (tone ch2 + noise ch3):
  jump, pickup, keycard, shoot, zap, bump, land, die, exit, enter jingles.
* **RAM** — everything fits in ~1.2 KB; no map copies.

## Faithful quirks kept from the JS original

* Keycards also grant the pogo stick (a bug in the JS `Keycard` class).
* There is no in-game HUD — the JS version literally renders "LOL" as its
  HUD placeholder, so score/ammo appear on the level-entry interstitials
  instead.
* Death returns you to the overworld (score kept).

## Simplifications

* Garg / vorticon / pat-pat are static deadly hazards, as in the JS
  entities file; spikes don't animate.
* One bullet on screen at a time; max three yorps drawn at once
  (VRAM streaming slots).

## Tools

* `tools/build_assets.py` — TMX/PNG → EGA-quantized SMS data pipeline
  (subtile dedup, metatile+flag build, item compositing, object export).
* `tools/preview.py` — decodes the generated C arrays back to PNGs for
  visual verification.
* `tools/smssim.py` — minimal headless SMS emulator (around the `z80`
  pip package) that boots the ROM, scripts the pads and saves screenshots.
* `tools/playtest.py` — closed-loop end-to-end test: BFS-navigates the
  overworld to level 1, enters it, platforms, jumps and shoots, asserting
  on game state read from emulated RAM.
* `tools/jumptest.py` — input reliability: 20 two-frame button taps while
  walking, asserting every one produces a jump.
* `tools/nttest.py` — roams level 1 and validates every visible name-table
  cell against the map+metatile data (~80 k cell comparisons), proving the
  ring-buffer scroll engine writes exactly the right tiles.
* `tools/owtest.py` — walks laps around the Mars map asserting scroll
  registers and all visible name-table cells match the camera derived
  from the player position.
* `tools/gatetest.py` — verifies the level-1 gate blocks until the level
  is beaten, opens afterwards, and that level 2 is then reachable and
  enterable.
