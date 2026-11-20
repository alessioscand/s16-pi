# s16-pi - Technical notes

Bare-metal SEGA System 16 and System 18 emulator for Raspberry Pi 3 & 4, with highly responsive input. The front-end (video, beam racing, TV filters, pads, audio) is shared with neo-pi and m92-pi; the core is new and follows MAME's drivers, reusing MAME's code where it decrypts the CPUs.

Website: <https://alessioscand.github.io/s16-pi/>

**s16-pi** is a human-led, vibe-coded experiment by Alessio Scanderebech.  
If you like s16-pi, [neo-pi](https://alessioscand.github.io/neo-pi/), [m92-pi](https://alessioscand.github.io/m92-pi/) or [nes-pi](https://alessioscand.github.io/nes-pi/), please consider supporting me: [paypal.me/alescandy](https://paypal.me/alescandy)

This file is for whoever carries on the work.

---

## 1. Layout

```
core/          the System 16A, 16B, 16C and System 18 machines (C++20, no Circle): CPUs, memory
               maps, video, sound, decryption, save states; s16_api.h is the C interface the
               front-end uses
               s16_games.inc, s16_hiscore.inc: GENERATED from MAME (see 3), never edit by hand
src/           s16-pi's own front-end: start-up, game list, menus, game loop, look (theme.cpp)
common/        front-end parts shared byte for byte with neo-pi and m92-pi (nothing System 16 in them)
third_party/   code by others: Musashi (68000), Z80, ymfm, miniz, snes_ntsc, Pi firmware
tools/         generators of the game and hiscore tables from MAME's sources
harness/       Linux test programs (s16test; gunmap for Laser Ghost)
test/          beam racing host test
patches/       Circle patches (USB polling, XInput pads)
dist/sd/       what goes on the SD card besides kernels and firmware
docs/          the site (GitHub Pages)
```

## 2. Building

Linux x86_64 with xPack `aarch64-none-elf-gcc` 14.2.1-1.1 in `toolchain/` and Circle commit `7a81e1b` in `circle-rpi4mc/`, `circle-rpi3mc/` and `circle-qemumc/` (`AARCH = 64`, `RASPPI = 4` or `3`, `STDLIB_SUPPORT = 1`, `-DKERNEL_MAX_SIZE=0x1000000 -DARM_ALLOW_MULTI_CORE`; the QEMU copy also `-DNO_SDHOST -DNO_SCREEN_DMA_BURST_LENGTH`), patched with the two scripts in `patches/`, then `./makeall` and `make` in `addon/SDCard` and `addon/fatfs`.

```
./build.sh all          # build/rpi4/kernel8-rpi4.img and build/rpi3/kernel8.img
./build.sh qemu         # a Pi 3 kernel for QEMU (fake pad, not for hardware)
./make_release.sh       # dist/s16-pi-<version>-sd.zip (checks version, firmware, README.txt)
```

The core (C++20 with the standard library) is built apart into `libs16.a`; everything else follows Circle's rules and sees only `s16_api.h`. The build must stay free of warnings. GitHub release: tag `v<version>`, asset `s16-pi-<version>-sd.zip`.

## 3. The core

Written by hand on the logic of MAME's `segas16a.cpp`, `segas16b.cpp` and `segas18.cpp` (same memory maps, timings, interrupts) and checked against MAME 0.264 on the harness; the decryption is MAME's own algorithms and tables, taken with the least changes.

- **Timing** (`s16.cpp`): 262 lines per frame, the frame starting at line 224 with the vertical blank, when the games read the pads; the 68000 and the Z80 take turns 8 times a line. Everything runs at the screen's 60 Hz: System 16B is 60.05 Hz, System 18 57.23 Hz (4.8% faster here, the sound in step).
- **System 16B**: 68000 (Musashi), Z80, the 315-5195 memory mapper, ROM boards 171-5358, 171-5521, 171-5704 and 171-5797 (with its multiplier and compare chips). System 16C is a 16B with 256 KB of work RAM (Fantasy Zone II).
- **System 16A**: a fixed map with its mirrors, the 8255, 8 tile pages, its own sprites; the uPD7751 (an 8048) and its 8243 play the samples (`n7751.cpp`).
- **System 18**: the mapper's own regions, 171-5874 and 171-5987 boards, the 315-5296 I/O chip, D. D. Crew's 8255 for players 3 and 4; the 315-5313 VDP, the Mega Drive's, mixed among the System 16 layers (`s18_vdp.cpp`).
- **Video** (`s16_video.cpp`): tile layers, text, sprites and palette, one line at a time from the machine's own video RAM, so a line can be drawn on another core; sprite RAM is read as it is at each line, the scroll registers are latched at line 261.
- **Sound**: YM2151 (ymfm) and uPD7759, with the board's analogue section as digital filters from MAME's netlist (`s16_sound.cpp`); System 18: two YM3438 (ymfm) and the RF5C68 (`s18_sound.cpp`). The sound is brought up to date within each line, so the audio queue is one frame long (`PROJECT_AUDIO_TARGET`).
- **Protection and encryption**: the 8751 MCU, written for s16-pi (`i8751.cpp`); FD1094, FD1089A/B and MC8123 (`s16_crypt.cpp`), each 64 KB page of program decrypted the first time it is used and kept.
- **Laser Ghost**: its three guns played with pads; the MSM6253 reads the crosshairs, `s16_guns.inc` (measured with `gunmap`) compensates the side guns' shots, the front-end does the gun adjustment.
- **Game tables**, from MAME's sources (ROMs, hardware, DIP switches, inputs): `tools/gen_s16_tables.py`, 100 sets (18 System 16A, 57 System 16B, 2 System 16C, 23 System 18), with the reason for every set left out (bootlegs, sets not working in MAME, controls a pad does not have, vertical games); DIP names shortened in `nice()` to fit the panel. Hiscores, 57 sets, from MAME's `hiscore.dat` with `gen_s16_hiscore.py`.
- **Not there yet**: vertical games, flip screen, the VDP's sprite collision flag.

## 4. The front-end

Cores: 0 = emulation, sound and main loop; 1 = drawing the lines (`mcline`); 2 = TV filters; 3 idle. `multicore=off` keeps everything on core 0.

| `src/` | |
|---|---|
| `project.h` | everything `common/` needs from the project: name, version, picture size (320x224), buttons, error codes |
| `kernel.cpp`, `console.cpp` | start-up, the main flow (list, game, list...), power off, the log |
| `games.cpp`, `gametable.cpp` | the game list, loading a set (and its parent), DIP switches; titles, years, makers and button names (by hand) |
| `menus.cpp`, `gameloop.cpp` | the menus; one emulated frame per screen frame, pads to the game |
| `frame.cpp`, `racing.cpp`, `mcline.cpp` | where each line goes (screen, other cores), beam racing, multicore drawing |
| `saves.cpp`, `settings.cpp` | hiscores, battery RAM and Laser Ghost's gun adjustment; `s16pi.cfg` and `pads.cfg` |
| `theme.cpp` | s16-pi's look: colours, fonts, logo, the grid |

| `common/` | |
|---|---|
| `screen.h`, `video.cpp` | the screen interface and its HDMI version: scaling, TV filters, bezel, status line |
| `ui.cpp`, `theme.h`, `fonts.h` | every menu, drawn on 304x224 in the middle; the look comes from the project's theme |
| `race.cpp`, `raster.cpp`, `pacing.cpp` | beam racing and frame delay |
| `input.cpp`, `audio.cpp` | USB pads and keyboard (up to 4 players); HDMI audio |
| `hiscore.cpp`, `gameinfo.cpp` | hiscores as MAME's plugin, battery RAM; the game info lookup |
| `tvcore.cpp`, `crtfx.cpp`, `composite.c` | TV filters on core 2: RGB and CRT, COMPOSITE |
| `report.cpp`, `stats.cpp`, `profile.cpp`, `smp.h` | the per-minute log; statistics; profiler; data shared between cores |

Buttons A B C D (the games use up to three; D only for Select + D, the credits); the DIP switches are one 16-bit value (SW1 low byte, SW2 high byte); no CPU SPEED.

Files on the card, in `s16pi/`: `s16pi.cfg` (`filter=`, `bezel=` and the testing options in `README.txt`), `pads.cfg`, `saves/<set>.dip`, `.st1`, `.nv`, `.hi` (MAME's format), `LOG.TXT`; the empty files `BENCH` and `MCTEST` start the benchmark and the multicore check.

## 5. Testing

- **Harness**: `cd harness && make`, then `./s16test <rom folder> <set>` with frames (`-f`), screenshots (`-s`), WAV (`-w`), a save state check (`-state`), coin and start (`-coin`), Laser Ghost's guns (`-guns`).
- **Against MAME**: MAME 0.264 snapshots compared pixel by pixel with the harness's. Identical: every CPU kind and ROM board of System 16B (`altbeast`, `goldnaxe`, `aliensyn`, `aurail`, `shinobi4`, `wb3` and their clones), System 16A (`shinobi`, `alexkidd`, `bodyslam`, `fantzone`, `tetris`, `wb35`...), System 18 `mwalk` for its first 37 seconds of attract mode. The other sets have not been run (no ROMs).
- **QEMU**: `./build.sh qemu`, raspi3b machine with an SD image; it checks screens and menus, not timing (no audio, no beam racing).

## 6. Conventions

- MAME's code keeps its BSD-3-Clause header; s16-pi's own files are GPL-3.0-or-later.
- The core has no Circle headers; the front-end includes no C++ standard library headers (only `s16_api.h`); `common/` has nothing System 16 in it.
- A variable one core writes and another reads goes through `smp.h`.
- No run-ahead: input lag is handled by the frame start, frame delay and beam racing.
- `dist/sd/LICENSES.txt` lists every third-party component in the kernel.

## 7. Credits

s16-pi was cobbled together by Alessio Scanderebech and his much-maligned programming partner Claude, standing on the shoulders of:

- **MAME** authors, among them Aaron Giles, David Haywood, Juergen Buchmueller, Mike Balfour, Howie Cohen, Olivier Galibert, Couriersud, Nicola Salmoria, Andreas Naive, Charles MacDonald and David Widel: the System 16 and 18 drivers, the VDP, uPD7759, RF5C68 and the audio circuit, the FD1094, FD1089 and MC8123 decryption, the game tables (BSD 3-Clause).
- **Karl Stenerud**: Musashi, the 68000 core (MIT).
- **Nicolas Allemand**, Rupert Carmichael and rofl0r: the Z80 core (MIT).
- **Aaron Giles** and Rupert Carmichael: ymfm (BSD 3-Clause).
- **Rene Stange**: Circle (GPLv3).
- **Shay Green** (blargg): snes_ntsc, the COMPOSITE filter (LGPL 2.1+).
- **Hyllian**: crt-hyllian-fast, the RGB filter (MIT); **davej**: crt-pi, the CRT filter (GPL 2+); both ported from libretro's slang shaders.
- **Rich Geldreich** and contributors: miniz (MIT).
- **Leezer** and contributors: the hiscore.dat of MAME's hiscore plugin.
- **Damien Guard**: the ZX Origins font collection.
