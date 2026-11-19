# Changelog

## 0.1.0 (2026-11-19)

First release.

### Versions
- DoDonPachi Dai-Fukkatsu Ver 1.5 (`ddpdfk`), Ver 1.0 (`ddpdfk10`) and
  Black Label (`dfkbl`) in one application, with a version selector. Each
  needs the user's own MAME 0.264 set; files are checked by name or CRC32.
  `ddpdfk10` can take its shared files from `ddpdfk.zip` beside it.

### Board
- CAVE CV1000-D after MAME 0.264's `cv1k.cpp`: SH-3 at 102.4 MHz, 16 MB RAM,
  NAND flash, EP1C12 blitter (drawing on a worker thread), YMZ770 with MPEG
  audio, RTC9701 with EEPROM, MAME's idle-loop handler and timer order.
- MAME's SH-3 interpreter and device code imported by `tools/import_mame.py`.

### Recompiler
- The SH-3 program is translated as the CPU reaches it, from MAME's handlers
  (`tools/gen_sh3_templates.py`), and compiled in memory by TinyCC on a
  background thread. 99.9% of cycles ran in native code in the test runs.
- Code overwritten in RAM is dropped and translated again; translated code
  is checked after a state load.
- `--interpreter` runs MAME's interpreter instead.

### Accuracy
- Eight runs of the three versions (attract, 1 and 2 players, up to 12,000
  frames) identical to MAME 0.264: CPU registers every frame, main RAM every
  frame or every 10th, 118 of 118 screenshots pixel-identical.

### Frontend
- The shared SDL2 frontend: menus for 1280×800, ROM configuration and
  automatic ROM discovery, per-version controls, save states with slots and
  autosave, input movies, screenshots, gadgets, display options.
- New rewind hooks in `fe_game`: rewind stores RAM pages and video-RAM tiles
  that changed instead of full states.
- EEPROM (and any rewritten flash blocks) saved per version when it changes.

### Packages
- Windows x64 zip and Steam Deck / Linux AppImage. No game data in either.
