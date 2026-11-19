# Accuracy: comparison with MAME 0.264

The board model and the recompiled SH-3 were compared with **MAME 0.264**
(`mame -nodrc`, i.e. MAME's SH-3 interpreter) running the same set with the
same inputs from power-on. Both started with an empty EEPROM and the clock
set to 2026-11-14 12:00:00.

## What is compared

`tools/cv1k_reference.lua` runs inside MAME and records:
- **every frame**, the SH-3's registers (PC, SR, PR, GBR, VBR, MACH, MACL,
  R0–R15, SPC, SSR, SGR) at the end of the frame;
- every frame or every 10th frame, a hash of each 64 KB block of the 16 MB
  main RAM (`tools/hashdump.c`), plus full dumps of chosen frames;
- a snapshot (the screen as MAME shows it) every 600 frames.

`cv1k_headless --compare DIR` runs the same inputs and checks the same
values at the same frames; `--png` writes the screen for comparison with the
snapshots. Inputs are applied as MAME's Lua `set_value()` does
(`--mame-latency`).

## Results (version 0.1.0)

Eight reference runs. "Play" runs insert coins, start at frame 2000 and then
play with random stick and button input (`tools/test-inputs/gen.py`), with an
occasional coin and continue; the 2-player runs move both players.

| Run | Version | Frames | CPU registers | Main RAM | Screenshots |
|---|---|---|---|---|---|
| attract | Ver 1.5 | 3,600 | 3,600 of 3,600 identical | 3,600 of 3,600 identical | 6 of 6 identical |
| 1 player | Ver 1.5 | 12,000 | 12,000 / 12,000 | 1,200 / 1,200 | 20 / 20 |
| 2 players | Ver 1.5 | 12,000 | 12,000 / 12,000 | 1,200 / 1,200 | 20 / 20 |
| attract | Ver 1.0 | 3,600 | 3,600 / 3,600 | 360 / 360 | 6 / 6 |
| 1 player | Ver 1.0 | 12,000 | 12,000 / 12,000 | 1,200 / 1,200 | 20 / 20 |
| attract | Black Label | 3,600 | 3,600 / 3,600 | 360 / 360 | 6 / 6 |
| 1 player | Black Label | 12,000 | 12,000 / 12,000 | 1,200 / 1,200 | 20 / 20 |
| 2 players | Black Label | 12,000 | 12,000 / 12,000 | 1,200 / 1,200 | 20 / 20 |

- 12,000 frames are 3 minutes 20 seconds; the first ~1,700 frames are the
  first-boot EEPROM initialisation.
- All eight runs were done with the recompiler (development build, GCC) and
  again with the release build (zig, the one in the packages); both gave
  the results above. The screenshots were made with the release build.
- Two runs (Ver 1.5 attract, Black Label 1 player) were repeated on the
  interpreter (`--interp`): identical to MAME too, as expected, since the
  interpreter is MAME's code.
- The Windows build was checked under Wine; see [TESTING.md](TESTING.md).

Because the registers match at every frame and RAM at every compared frame,
the CPU executed the same instructions with the same timing: a difference
of even one cycle moves the interrupts and would almost certainly show up
within a few frames.

## Save states and rewind

- `--state-test F`: save at frame F, run 300 frames with other inputs, load,
  re-save. The re-saved state is byte-identical, and the run continues
  identical (hashes) to an uninterrupted one.
- `--rewind-test F`: 31 captures 4 frames apart, restored newest first back
  to back, then each replayed: every state is reproduced exactly.
- Truncated state files are refused, and the running game continues.

See TESTING.md for the per-version results.

## Audio

MAME writes its output after its own resampling to 48 kHz; the board's
stream is 16 kHz mono. `tools/compare_wav.py` resamples ours, aligns the two
and measures:

| Run | Compared at | Correlation | Residual | Level ratio per second |
|---|---|---|---|---|
| Ver 1.5, 1 player, 4,800 frames (80 s) | 48 kHz (ours resampled linearly) | 0.990 | 14.4% | 0.87–0.99 over 47 s with sound |
| same | 16 kHz (MAME's recording resampled down) | 0.982 | | 1.00–1.08 |

The CPU registers matched MAME at all 4,800 frames of this run, so the game
sent the chip the same commands at the same times.

The waveforms match closely but not exactly: MAME resamples the 16 kHz
stream to 48 kHz with its own filter, and this program hands 16 kHz samples
to SDL. The YMZ770 model itself is MAME's code. The audio was not listened
to.

## What this does not show

- Only the first ~3.3 minutes of play per run, with random inputs: later
  stages, bosses, the endings, name entry and the test menu's options were
  not compared.
- MAME is the reference, not the real board. Where MAME is wrong (it marks
  the CV1000's blitter timing as an estimate), this program is wrong the
  same way.

## How to repeat

```bash
# MAME 0.264 reference (needs faketime for the fixed clock)
CV_FRAMES=12000 CV_EVERY=10 CV_SNAP=600 CV_INPUTS=tools/test-inputs/p1.txt \
CV_HASHDUMP=./hashdump faketime -f '@2026-11-14 12:00:00 x0.001' \
  mame ddpdfk -nodrc -video none -sound none -nothrottle \
  -nvram_directory nv -snapshot_directory snap -autoboot_script tools/cv1k_reference.lua

# this program
cv1k_headless --version ddpdfk --rom ddpdfk.zip --frames 12000 --rtc "2026-11-14 12:00:00" \
  --inputs tools/test-inputs/p1.txt --mame-latency --compare REFERENCE_DIR
```

Delete MAME's `nv` folder between runs: otherwise MAME starts from the EEPROM
the previous run left. `tools/hashdump.c` is built with `cc -O2 -o hashdump
tools/hashdump.c`.
