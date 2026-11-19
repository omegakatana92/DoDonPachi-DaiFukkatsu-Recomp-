# Known issues and limitations (0.1.0)

## Recompiler

- **Not all code is native.** Code reached for the first time runs on the
  interpreter until its batch is compiled in the background (usually a few
  frames). At most 0.1% of cycles stayed on the interpreter in the test
  runs.
- **Overwritten code** is handled (the module is dropped and translated
  again), but the game never did it in the test runs, so that path was not
  compared with MAME.
- **x86-64 only.** On other CPUs the program falls back to the interpreter.
- **Executable memory.** The compiled code is placed in memory that is made
  executable at run time. A system that forbids this (some hardened
  configurations) makes the compile step fail. The program then runs on the
  interpreter and writes the reason to `frontend.log`.
- **Moderate speed-up.** TinyCC does little optimisation, and much of the
  time goes to the blitter and the MPEG audio decoder anyway. See
  RECOMPILER.md for numbers.

## Board model

- The board model reproduces MAME 0.264, including its estimate of the
  blitter's drawing time, which decides when the game sees slowdown. Where
  MAME differs from the real board, so does this program.
- MAME's `cv1k` sends only the YMZ770's right channel to the speaker; so
  does this program (mono).

## Sound

- Checked as data against MAME's recording (correlation 0.98–0.99 after
  resampling; see ACCURACY.md), **not listened to**.
- The YMZ770's stream arrives in uneven blocks (as in MAME). Smoothing them
  adds roughly 50–80 ms of latency.
- The frontend logged audio-queue underruns in the Linux test (SDL's dummy
  audio driver under Xvfb, 2 CPU cores: 475 in 2,415 frames with rewind on),
  none under Wine. Whether there are any on real hardware with a real audio
  device was not checked.

## Save states and rewind

- A state is about 15 MB (the blitter's video RAM is 128 MB uncompressed)
  and takes roughly 0.5–1 s to save or load.
- A rewind capture takes 25–35 ms on the main thread every 4 frames, and
  the default 64 MB budget holds about 7–10 seconds. Rewind is off by
  default; with it on (and a movie recording) the frontend dropped to about
  56 fps on the 2-core test machine.
- Save states written by the Windows and the Linux build are interchangeable
  but not byte-identical (low bits of the audio decoder's floating-point
  buffers; see TESTING.md).

## First start

- With an empty EEPROM the game initialises its backup data for about
  25 seconds before the title appears. This happens once per version (the
  EEPROM file is then kept).

## Not tested on the target hardware

- **Windows 10/11:** cross-compiled and run under Wine only.
- **Steam Deck:** the AppImage was run under Xvfb on a Linux PC only.
- **Controllers:** no physical controller was used.
- **Audio output:** checked as data (WAV against MAME), not listened to.
- **Human play:** scripted and random inputs only. 3.3 minutes of play per
  run were compared with MAME; later stages, bosses, the endings, name entry
  and the test menu's options were not.

## Game data

- Only the MAME 0.264 sets `ddpdfk`, `ddpdfk10` and `dfkbl` are supported.
  Other dumps or revisions are refused.
- Score / lives gadgets are not offered: their RAM addresses were not
  verified.

## Frontend

- Two copies of the program running at once share `settings.ini`; whichever
  exits last writes it.
- No widescreen or CRT curvature.
