s16-pi 1.0.0 - Bare-metal SEGA System 16 emulator for Raspberry Pi 3 and 4
============================================================================

  The SEGA System 16B and System 18 games that play with a stick and
  buttons on a horizontal screen, 80 MAME sets, protected ones included:
  Altered Beast, Golden Axe, Shinobi, Alien Syndrome, Aurail, Wonder Boy
  III, ESWAT, Cotton, Dynamite Dux, Riot City ... and on System 18 Shadow
  Dancer, Moonwalker, Alien Storm, D. D. Crew, Bloxeed, Clutch Hitter and
  Laser Ghost.

  - Beam racing: each line is sent to the screen as soon as it is
    drawn, for the lowest possible lag.
  - Scanlines (light, medium, strong) and a COMPOSITE filter that looks
    like an old TV.
  - Up to 4 players, save state, the games' own DIP switches.
  - Around the picture: the game's title, maker and year, and what its
    buttons do (at 1080p). OPTIONS > BEZEL: INFO, or OFF for black.
  - Hiscores are saved in s16pi/saves/<game>.hi (57 sets, as MAME's
    hiscore plugin).
  - Each game's battery-kept RAM (settings, bookkeeping, and the scores
    of the games that keep them there) is saved in
    s16pi/saves/<game>.nv: credits left in a game are still there next
    time. To start a game afresh: DIP SWITCHES > CLEAR MEMORY (the last
    row).
  - Laser Ghost is played with the pads instead of its 3 guns: the stick
    moves the crosshair (blue, yellow and red for players 1, 2 and 3; it
    shows while the player is in the game), A fires, B is the bomb. The
    first time, the game asks for its gun adjustment: s16-pi does it by
    itself in about 15 seconds (GUN ADJUST), then keeps it in
    saves/lghost.nv.

  System 18 boards run at 57.23 frames a second: here they run at the
  screen's 60, a little faster (music a little higher) than the real
  thing. Vertical games (Passing Shot, Time Scanner, Action Fighter,
  Desert Breaker) are not supported yet.

SET-UP

  1. Use a microSD card formatted as FAT32, with a standard partition
     table.
  2. Copy EVERYTHING in this folder to the card's root.
  3. Put your games in the s16pi folder as MAME romsets, one .zip per
     set with MAME's names (goldnaxe.zip, mwalk.zip ...), from a
     recent MAME. A clone needs its parent's zip beside it: the Shinobi
     and Tetris sets need shinobi.zip and tetris.zip, System 16A sets
     used only for their ROMs.
  4. Connect a USB gamepad and the display (on a Pi 4, HDMI 0, next to the
     power socket). Insert the card and switch on.

CONTROLS

  The first time a pad model is connected, the CONTROLS screen opens by
  itself: press the button you want for A, B, C, D, SELECT/COIN, START.
  To skip one, press a button already assigned: it stays unassigned
  (shown as -).

  Game list: Up/Down to choose, Left/Right to jump by 8, A or Start to
  play, D for OPTIONS. A long title scrolls.

  In game:
  Select (short press)      Insert a coin
  Select held 1 second      In-game menu
  Select + A                Screenshot: s16pi/shots/<game>_NNN.png
  Select + B                Lazy finger on/off
  Select + C                Status line on/off
  Select + D                Credits

  Always switch off with POWER OFF. For help, s16pi/LOG.TXT is all that
  is needed.

----------------------------------------------------------------------------

s16-pi is a human-led, vibe-coded experiment by Alessio Scanderebech,
written with Claude. Free software, licensed under GPLv3 (see LICENSES.txt).

SEGA is a trademark of its owner. Games are not included.
