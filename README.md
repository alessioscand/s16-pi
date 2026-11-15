# s16-pi - Technical notes

Bare-metal SEGA System 16 emulator for Raspberry Pi 3 and 4 (no operating
system: Circle, AArch64), the sibling of neo-pi, nes-pi and m92-pi. First
public release: 1.0.0.

## State

- **Front-end**: m92-pi's, adapted to the System 16 picture (320x224, 262
  lines, the frame starts at line 224 with the vertical blank). `common/`
  is shared with the three other projects; `hiscore.cpp/.h` (hiscores and
  battery RAM, from m92-pi's front-end) are new here, the others take them
  at their next release.
- **Menus**: the new look (petrol grid, dark panels with a white frame,
  crimson band; Cyberwire for the game list, Matinee for the rest), drawn
  at 320x224 like the game. Long titles scroll in the list. The logo is
  drawn by `CUI::Logo`.
- **Bezel** (OPTIONS > BEZEL, `bezel=off` in `s16pi.cfg` for black): the
  border is the menus' grid at 30%, its lines fading out near the picture;
  at 1080p the game's title, maker and year above the picture and its
  buttons below (`src/gameinfo.cpp`). The status line (left column) and
  the messages (right column) are in Area51 with a hard shadow, the same
  at 720p and 1080p. With no game loaded, the menus' own background
  becomes the same pattern, fading near panels and text (`BezelMenu`). The
  HVS enlarges at most 2x, so the text can have 2 screen pixels per font
  pixel (1080p: 2x CPU, 2x HVS).
- **Core** (`core/`): the System 16B and System 18 machines, written by
  hand on MAME's logic and checked against MAME 0.264 on the Linux harness.
  - `s16.cpp`: 68000 (Musashi), Z80, the 315-5195 memory mapper, I/O, time
    kept in scanlines (262 per frame, the CPUs take turns 8 times a line),
    save states.
  - `i8751.cpp`: the 8751 protection MCU of some boards (it drives the
    mapper). Written for s16-pi, not taken from MAME.
  - `s16_video.cpp`: tile layers, text, sprites and palette, one line at a
    time. Sprites are read from sprite RAM as it is at each line (MAME draws
    the frame at its end); the scroll registers are latched at line 261 as
    in MAME.
  - `s16_sound.cpp`: YM2151 (ymfm), uPD7759 in slave mode, and the board's
    analogue section from the components of MAME's netlist, as digital
    filters (within 1 dB of MAME up to 8 kHz).
  - Multicore: the lines are drawn on core 1 from the machine's own video
    RAM; every write to what the drawing reads waits for the lines handed
    over (`VideoSync`).
  - `s16_crypt.cpp`: the encrypted CPUs, FD1094, FD1089A/B (68000) and
    MC8123 (Z80): MAME's algorithms and tables, taken with the least
    changes. `s16.cpp` decrypts a 64 KB page of program the first time it is
    used (FD1094: once per state of the chip; FD1089: once per address the
    mapper puts it at) and keeps it.
  - ROM boards: 171-5358 (and its small version), 171-5521, 171-5704,
    171-5797 (with its 315-5248 multiplier and 315-5250 compare chips).
  - System 18 (as MAME's `segas18.cpp`): the mapper's own regions for the
    shadow, 171-5874 and 171-5987 boards (tile and sprite banks), the
    315-5296 I/O chip, D. D. Crew's 8255 for players 3 and 4, 57.23 Hz with
    the Z80 at 8 MHz.
  - `s18_vdp.cpp`: the 315-5313 VDP (the Mega Drive's): ports, DMA, fill,
    copy, planes, window, sprites, one line at a time, mixed among the
    System 16 layers as the board's mixing value says.
  - `s18_sound.cpp`: two YM3438 (ymfm) and the RF5C68 with its wave RAM.
    The output is 800 samples per game frame: the game runs at the
    screen's 60 Hz, 4.8% faster than the board, with the sound in step.
- **Games**: `core/s16_games.inc` is written by `tools/gen_s16_tables.py`
  from MAME's `segas16b.cpp` and `segas18.cpp`:
  80 sets (57 System 16B, 23 System 18) with their ROMs, hardware, DIP
  switches and what each input bit is (and the bits that change with a
  DIP switch, as Moonwalker's coin chutes). Left out (the script says why
  for each): bootlegs and Korean boards, sets not working in MAME, System
  16C, controls a pad does not have (trackball, dial, analog stick,
  mahjong, two sticks, Where's Wally's light gun) and, for
  now, the games on a vertical monitor (Desert Breaker, Hammer Away). Tough Turf and Wonder Boy III (set 6), whose 8751 is
  not dumped, use MAME's stand-ins for it. `src/gameinfo.cpp`, written
  by hand, has what the list shows of each set (short title, year, maker)
  and the names of its buttons.
- **Checked against MAME 0.264** (attract mode, a frame every 150, first
  20 seconds): identical pixel for pixel on `altbeast`, `goldnaxe`,
  `goldnaxe1`, `goldnaxe2`, `goldnaxe3`, `goldnaxeu`, `aliensyn`,
  `aliensyn3`, `aliensyn7`, `aurail`, `aurail1`, `aurailj`, `shinobi4`,
  `wb3`, `wb32`, `wb33`, `wb34`: every CPU kind and every ROM board. The
  only difference: a logo of Altered Beast that changes colour in the
  middle of a frame (MAME shows the change a frame early). The still
  screens of some boots come a few frames earlier than in MAME. The sound
  of the two MC8123 sets follows MAME's after a coin and a start. The
  other 40 sets have not been run: no ROMs here.
  System 18: `mwalk` (FD1094 + 8751, 171-5874) identical to MAME for the
  first 37 seconds of attract mode; then its demo play drifts by a few
  frames for a while and comes back in step. Its sound is at MAME's level.
  The other System 18 sets have not been run: no ROMs here.
- **Laser Ghost**: its three guns played with pads. The MSM6253 A/D
  converter reads the crosshairs; the game bends the side guns' shots
  towards the centre, which `core/s16_guns.inc` (measured with the
  harness's `gunmap`) compensates; the gun adjustment the game asks for
  without its battery RAM is done by the front-end.
- **Battery RAM**: the work RAM of every game, in `saves/<game>.nv`
  (`common/hiscore.cpp`); DIP SWITCHES > CLEAR MEMORY wipes it.
- **Hiscores**: 57 sets, from MAME's hiscore.dat (`core/s16_hiscore.inc`,
  `tools/gen_s16_hiscore.py`), kept by `common/hiscore.cpp`.
- **Not there yet**: flip screen, vertical games, the VDP's sprite
  collision flag.

## Plan

1. The vertical games (Passing Shot, Time Scanner, Action Fighter,
   Desert Breaker ...).

## Layout

```
core/        the System 16B and 18 machines (s16_api.h is all the front-end sees)
harness/     Linux test harness: make, ./s16test <roms> altbeast -s 300 -w
tools/       gen_s16_tables.py: the game tables from MAME's driver;
             gen_s16_hiscore.py: the hiscore rows from MAME's hiscore.dat
common/      shared with neo-pi, nes-pi and m92-pi (pads, audio, beam racing, TV filter, hiscores)
src/         the s16-pi front-end (as m92-pi's, see its README, 4.1)
third_party/ musashi (68000) and z80 from Geolith (z80.c with one addition: a function
             of its own for the opcode fetches, for the MC8123), ymfm, miniz, snes_ntsc,
             rpi-firmware
patches/     Circle patches (USB polling, XInput pads)
dist/sd/     what goes on the SD card
test/        race_sim.cpp (beam racing host test)
```

Differences from m92-pi's front-end: 4 buttons (A B C D; the games use up
to three), CONTROLS asks A B C D SELECT START, DIP switches are one 16-bit value (SW1 low byte, SW2 high byte),
no CPU SPEED, no EEPROM, no cabinet voicing.

## Building

As m92-pi (toolchain and Circle copies: see neo-pi's `BUILD.md`):
`./build.sh all`, `./build.sh qemu`, `./make_release.sh`.
