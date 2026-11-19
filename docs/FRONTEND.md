# Frontend

`frontend/` is the game-independent SDL2 frontend shared by the arcade
projects (Ketsui, DoDonPachi Dai-Ou-Jou, Capcom vs. SNK, …).
- A game plugs in through `fe_game` (`frontend/fe.h`); several versions form
  an `fe_suite`.
- `ports/sdl2/main.cpp` is this project's adapter, with one `fe_game` per
  version.
- The frontend changes made for this project are listed in
  [MODIFIED_FILES.md](MODIFIED_FILES.md).

| File | Contents |
| --- | --- |
| `fe.h` | adapter interface (`fe_game`, `fe_suite`) |
| `fe_app.c` | window, loop, settings, menus, version selection, input, audio, states, NVRAM, rewind, movies, gadgets |
| `fe_draw.c` | embedded font atlas (Inter, OFL) and shapes |
| `fe_platform.c` | UTF-8 paths on Windows / POSIX, directory listing, atomic writes, INI, PNG, log |
| `fe_profile.c`, `fe_profile.h` | JSON game profiles |

The look is original (no game or third-party artwork):
- a dark gradient background and one accent colour per version;
- an animated focus bar, a help line and a controls legend;
- laid out for 1280×800 (Steam Deck) and 16:9.

## Versions

| Profile id | Version | MAME set | Accent |
|---|---|---|---|
| `ddpdfk` | Dai-Fukkatsu Ver 1.5 | `ddpdfk` | Crimson |
| `ddpdfk10` | Dai-Fukkatsu Ver 1.0 | `ddpdfk10` | Amber |
| `dfkbl` | Dai-Fukkatsu Black Label | `dfkbl` | Violet |

**Select Version** (main menu) lists the three versions with their state:
*selected*, *ready*, *ROM not set*, or *ROM not usable* and why.
- The selection is remembered; `--version ID` chooses one version for a
  single start.
- Choosing a version without usable files opens the ROM browser for it.
  Another version is never used in its place.
- Each version has its own ROM path, controls, EEPROM file, save-state
  slots, screenshots, movies and accent colour.
- Display, audio and the gadget layout are shared.

**Switch Version** in the quick menu:
1. asks first (default *No*), and offers **Save State First**;
2. ends the game (with an autosave when enabled) and saves the EEPROM;
3. unloads the board completely and loads the other version;
4. resets inputs, the audio queue and the rewind history;
5. powers the new board on.

A state file of another version is refused by name.

## Menus

- **Main menu:** Resume, Start Game (with the version), Select Version,
  Continue, Load State, Save State, ROM Configuration, Display, Audio,
  Controller, Gameplay Settings, Gadgets, Extras, About, Exit.
- **Quick menu** (`Esc`, or Back + Start): Resume, Restart Current Version,
  Save / Load State, Display, Audio, Controller Configuration, Gadgets,
  Extras, Switch Version, Return to Main Menu, Exit.

While a menu is open, the game and its audio are paused and game inputs are
cleared. Exit asks first.

## Display

The board draws a 320 × 240 picture for a 4:3 monitor mounted vertically
(MAME's ROT270).
- **Rotation:** the frontend shows the picture upright by default (rotation
  270°, marked as the original orientation). Display > Rotation can turn it
  for a rotated monitor or a "tate" setup.
- **Frame pacing:** defaults to the board's own rate, **60.024 Hz** (MAME's
  value for the CV1000). *Display sync* runs one frame per refresh on a
  59–61 Hz display.
- **Other options:** windowed / fullscreen, window size, fit / integer
  scaling, nearest / bilinear filtering, VSync, scanlines, phosphor masks,
  brightness, gamma, backgrounds, accent colour.
- No widescreen or curvature.

## Audio

Options: master volume, mute, output device, and a latency target of
16–250 ms.

- The board's sound is the YMZ770's output at 16,000 Hz, mono (MAME's
  `cv1k` takes the chip's right channel to the speaker); SDL resamples it.
- As in MAME, the chip's stream is brought up to date when the game writes
  to it and at the 50 Hz sound-manager update, so samples arrive in uneven
  blocks.
- The adapter smooths this: it waits until about 75 ms have accumulated,
  keeps about 30 ms in reserve and then passes on one frame's worth per
  video frame. This adds latency (roughly 50–80 ms) but does not change
  what the board does.
- Music and effects cannot be separated: the game's MPEG streams are mixed
  in the chip.

## EEPROM and flash (NVRAM)

The CV1000 keeps the game's backup data in the RTC9701's 512-byte EEPROM,
and the game can also rewrite blocks of its NAND flash. In the frontend
tests, the first start of each version rewrote one flash block during the
initialisation; that block is kept with the EEPROM, so the file
is then about 133 KB instead of 520 bytes.
- **File:** per version, `nvram/<version id>-<fingerprint>.nv` in the data
  folder, e.g. `nvram/ddpdfk-9976D699.nv`. It holds the tag `DFKN`, the EEPROM
  (512 bytes, the same contents as MAME's `nvram/<set>/eeprom`) and the
  flash blocks the game rewrote, if any (the ROM files are never changed).
- **First start:** the EEPROM is empty (all FF), as on a fresh MAME setup.
  The game then spends about 25 s initialising before the title.
- **When it is written:** checked every 2 seconds and written when it has
  changed; also when a game ends, when the version changes and at exit.
- Save states include it.

## Gadgets

| Gadget | Source |
|---|---|
| Current Game | the running version's title |
| Recompiled Code | share of SH-3 cycles spent in native code, and how many instructions are translated (or "interpreter") |
| FPS, frame time, input display | frontend |

Score, lives and bomb gadgets are **not** offered: their RAM addresses were
not verified for these versions.

## Files

The data folder holds `settings.ini`, `frontend.log`, `nvram/`, `states/`,
`screenshots/` and `movies/`. It is:
- `%APPDATA%\OmegaKatana\DoDonPachi Dai-Fukkatsu Recomp\` on Windows;
- `~/.local/share/OmegaKatana/DoDonPachi Dai-Fukkatsu Recomp/` on Linux;
- beside the executable in portable mode (`--portable`, or a `portable.txt`
  file there).

## Command line

```
--version ID      ddpdfk | ddpdfk10 | dfkbl
--rom PATH        ROM zip or folder for that version
--interpreter     run the SH-3 on MAME's interpreter (also DFK_INTERPRETER=1)
--fullscreen / --windowed, --scale N, --window WxH (1280x800 previews the Deck)
--portable        data beside the executable
--state FILE      load a save state at start (must belong to the version)
--no-menu         start the game immediately
--rotate N        picture rotation in quarter turns for this session
--frames N        quit after N frames (automation)
--screenshot FILE PNG of the last frame at exit (unrotated)
--profile FILE    use this profile instead of profiles/<id>.json
```
