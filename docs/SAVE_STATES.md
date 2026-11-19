# Save states

Save states capture the complete running board and restore it exactly. MAME
does not support save states for these sets; the format is this program's
own. They are separate from the EEPROM file (see FRONTEND.md), but contain
the EEPROM too.

## Using them

- Each version has its own **slots 1–10** and **Autosave**, with thumbnail,
  date and time, and play length.
- **Quick save / quick load** (`F5` / `F9`) use the current slot (`F6` / `F7`
  change it). Saving over a slot from the menu asks first; loading during a
  game asks first.
- **Autosave** is written when you return to the main menu, exit or switch
  version (each can be turned off in Extras); **Continue** resumes it.
- A state is large (about 15 MB in play: the blitter's 128 MB video RAM is
  part of the board) and takes roughly 0.5–1 s to write or read on the test
  machine.

## Where they are stored

`<data folder>/states/<version id>-<ROM fingerprint>/slotNN.fest` and
`autosave.fest`, e.g. `states/ddpdfk-9976D699/slot01.fest`. The data
folder is `%APPDATA%\OmegaKatana\DoDonPachi Dai-Fukkatsu Recomp\` on Windows,
`~/.local/share/OmegaKatana/DoDonPachi Dai-Fukkatsu Recomp/` on Linux and
Steam Deck, or the executable's folder in portable mode.

## What a state file contains

| Field | Purpose |
| --- | --- |
| `FESTATE1` container | file format (frontend) |
| version id (`ddpdfk` / `ddpdfk10` / `dfkbl`), runtime version | label; another version's states are refused |
| ROM fingerprint (CRC32 of the program ROM) | refuses another revision or a modified set |
| timestamp, frame count, thumbnail | shown in the slot list |
| core state | the board, see below |
| CRC32 of the whole file | detects damage |

The core state (`runtime/src/state.cpp`, magic `CV1K`, format version 1)
starts with a build-layout signature, the version and the ROM fingerprint,
all of which must match before anything is restored. It contains no
recompiled code (the native code keeps no state of its own), so a state
saved with the recompiler loads with `--interpreter` and the other way round.
It holds:

- **Scheduler:** base time, the quantum list, the SH-3's execution state
  (local time, suspend and spin-until-trigger state), every timer with the
  order of the active timer list, and pending input-line updates.
- **SH-3:** registers, delay slot, cache-control and on-chip peripheral
  registers (interrupt controller, timers TMU0–2, DMA channels, refresh
  controller, RTC, ports).
- **Devices:** the RTC9701 (clock, EEPROM, serial state); the NAND flash's
  command state and any blocks the game rewrote; the YMZ770 (channels,
  sequencers, MPEG decoder state); the EP1C12 blitter's registers, its busy
  timer and its **video RAM** (8192 × 4096 pixels), stored as 32 × 32 tiles:
  all-zero tiles as one bit, the rest deflated.
- **Memory and latches:** main RAM (16 MB, deflated), the non-zero 64 KB
  blocks of the cache-RAM area, input and EEPROM-port latches, the sound
  stream position, the screen's vblank times and the frame number.

No ROM data is stored. Both builds are x86-64 and write the same layout
(little-endian), so Windows and Linux builds read each other's states (see
TESTING.md for the check).

## Restoring safely

The frontend checks the container before touching the board; the core then
checks its own header and keeps a copy of the running board while it loads.
If anything is wrong (truncated data, an impossible value) the copy is put
back and the game continues as before. After a load the recompiler checks
that every translated module still matches the instructions in RAM and drops
the ones that do not; queued audio is flushed, and held inputs must be
released before they count again.

## Rewind

Rewind (off by default; Extras > Rewind, hold `Backspace`) keeps a capture
every 4 frames within a memory budget (default 64 MB).
- A full state is too large to take every 4 frames, so a capture holds the
  board without main RAM and video RAM, plus the 4 KB RAM pages and the
  32 × 32 video-RAM tiles changed since the capture before it (the blitter's
  command list tells which tiles it may write). The machine keeps a copy of
  RAM and video RAM as they were at the newest capture, and stepping back
  walks these differences backwards.
- In play a capture is about 0.6 MB, so the default 64 MB holds roughly
  7–10 seconds; raise Rewind Memory Limit (up to 512 MB) for more.
- A capture costs about 25–35 ms on the test machine, once every 4 frames,
  on the main thread. On a slow CPU this can cost frame rate; leave rewind
  off there.

## Verified

See ACCURACY.md ("Save states"): round trips continue identical; re-saving
gives identical bytes; truncated files are refused; the rewind test
reproduces every state. Frontend (Xvfb): quick save and load, autosave,
Continue after a restart, per-version state folders, rewind.
