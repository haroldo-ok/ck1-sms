# Commander Keen — Episode 1 for the Sega Master System

A port of *Commander Keen: Invasion of the Vorticons, Episode One* to the
Sega Master System, written in C for **devkitSMS / SDCC / SMSlib**.
`keen.sms` is a 400 KB ROM (25 banks, battery-backed cartridge RAM for
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
```

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
  (boot, jumps, map, doors, hints, saves, game over, ending, animation).

## Design notes

* The fixed ROM (banks 0–1) holds code only; level data, art and text sit
  in slot-2 banks that are paged on demand. Sprite art is streamed into
  VRAM: seven 12-tile slots hold the visible enemies (24x32 art loads in
  two halves), the player uploads one frame at a time.
* Animated tiles: quadrants that change across a tile group's frames
  become "live" subtiles; up to six are re-uploaded per frame in
  VBlanks that carry no other art burst.
* Levels are stored as 16x16 metatiles over de-duplicated, flip-aware
  8x8 subtiles; the name table is a 32x28 ring buffer scrolled eight ways.

## Differences from the original

* Enemy AI is a simplified re-implementation using the original
  speeds/gravity, hit points (vorticon 4 zaps), stun/bump rules and
  armoured robots/tanks, not a state-for-state copy of every AI routine.
* Ice cannon chunks stun Keen rather than playing the ice-cube animation;
  the level 16 chandelier rope is inert.
* The SMS palette has 64 colours, so the 16 EGA colours are approximated.
* Sound is the original PC-speaker data played on the PSG.
