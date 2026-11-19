# Reused code and what changed

## From the Ketsui Recomp project (same author)

| Here | Changes for this project |
|---|---|
| `frontend/` | **Rewind hooks:** `fe_game` gains `rewind_capture`, `rewind_restore` and `rewind_clear`, so a game can keep its rewind history as differences instead of full states. `fe_app.c` uses them when present (capture, step back, clear on state load, movie load and reset) and falls back to full states otherwise. Nothing else changed. |
| `runtime/emu/kemu.*` | MAME's `suspend_until_trigger` / `spin_until_time` (the SH-3's idle-loop handler), `trigger()`, the trigger counter (saved in states), `attotime::as_double`, `timer::elapsed` |
| `runtime/emu/state_io.h` | none |
| `ports/headless/main.cpp` | rewritten for the CV1000: `--version ddpdfk/ddpdfk10/dfkbl`, `--rtc`, `--compare` against `tools/cv1k_reference.lua` dumps, `--rotated`, `--eeprom`, `--vram-dump`, `--rewind-test` through the incremental rewind API |
| `ports/sdl2/main.cpp` | rewritten for the three versions (inputs, EEPROM/flash NVRAM blob, rewind hooks, audio cushion, Recompiled Code gadget) |
| `scripts/`, `packaging/`, `cmake/mingw-w64-x86_64.cmake`, `tools/make_font.py` | names and paths (`DFKRecomp`, `dfk-recomp`, `usr/share/ddpdfk`) |
| `runtime/third_party/tinycc/`, `runtime/third_party/miniz/` | none |

## Imported from MAME 0.264 (BSD-3-Clause)

`tools/import_mame.py` copies the member functions out of a MAME 0.264
source tree, so the imported code is MAME's text; only the class framework
around it is new.

| Here | From | How |
|---|---|---|
| `runtime/sh3/sh3_mame_defs.inc`, `sh3_mame_decls.inc` | `cpu/sh/sh.cpp`, `sh4.cpp`, `sh3comn.cpp`, `sh4comn.cpp`, `sh4dmac.cpp`, `sh4tmu.cpp` | the interpreter's member functions of `sh_common_execution`, `sh34_base_device`, `sh3_base_device` and `sh3be_device` merged into one class `sh3_device`; the DRC, debugger, state and construction functions left out; `inline` dropped; three base-class functions renamed where a subclass overrides them; `sh3be_device::execute_run` replaced (below) |
| `runtime/sh3/sh3comn.h`, `sh4regs.h` | `cpu/sh/sh3comn.h`, `sh4regs.h` | unchanged |
| `runtime/devices/ep1c12_mame.inc`, `ep1c12/ep1c12_blit*.cpp`, `ep1c12in.ipp`, `ep1c12pixel.ipp`, `ep1c12.h` | `cave/ep1c12.cpp`, `ep1c12_blit*.cpp`, `.ipp`, `ep1c12.h` | member functions less construction, start-up, save-state and MAME-interface ones; blit files copied whole; the header keeps MAME's members, with the device framework replaced |
| `runtime/devices/ymz770_mame.inc`, `mpeg_audio.*` | `sound/ymz770.cpp`, `sound/mpeg_audio.*` | as above; the MPEG decoder copied whole |
| `runtime/devices/nandflash_mame.inc` | `machine/nandflash.cpp` | as above |
| `runtime/devices/rtc9701_mame.inc` | `machine/rtc9701.cpp` | as above |

## Written for this project after MAME 0.264

| File | |
|---|---|
| `runtime/sh3/sh3_core.cpp`, `sh3.h`, `sh3_regs.h`, `sh3_shim.h` | the class declaration, construction / start / reset, `execute_run` (MAME's loop plus the native-code hook and cycle statistics), save / load |
| `runtime/emu/mame_shim.*`, `runtime/devices/emu.h` | the small part of MAME's framework the imported code uses (`attotime`, timers, `COMBINE_DATA`, logging, bitmaps) |
| `runtime/devices/cv1k_devices.*`, `ep1c12/ep1c12.cpp` | device construction, timers, the YMZ770 stream, save / load; the blitter's worker thread (MAME's work queue), `draw_screen`, the dirty-tile tracking for rewind |
| `runtime/src/machine.cpp`, `runtime/include/cv1k/machine.h` | the `cv1k_d` machine after `cave/cv1k.cpp`: memory map, I/O ports, inputs, EEPROM port, idle-loop speedup, screen and interrupt timing, MAME's timer order |
| `runtime/src/roms.cpp` | `ROM_START(ddpdfk)`, `(ddpdfk10)`, `(dfkbl)`; zip / folder reading; the parent set for `ddpdfk10` |
| `runtime/src/state.cpp` | save states and incremental rewind |
| `runtime/recomp/sh3_recomp.*` | the recompiler |
| `runtime/recomp/sh3_templates.inc`, `sh3_probe.inc` | generated from MAME's handlers by `tools/gen_sh3_templates.py` |
| `tools/import_mame.py`, `tools/gen_sh3_templates.py` | the import and template generators |
| `tools/cv1k_reference.lua`, `tools/hashdump.c`, `tools/compare_wav.py`, `tools/test-inputs/` | MAME reference recording and comparison |
| `cmake/compare_modes.cmake` | ctest: recompiled vs interpreter |
| `profiles/`, `packaging/appimage/` | the three version profiles; AppImage files |

## Third-party libraries

| Here | Version | Changes |
|---|---|---|
| `runtime/third_party/tinycc/` | TinyCC 0.9.28rc (git f830a169), as in the Ketsui project | our `config.h` |
| `runtime/third_party/miniz/` | as in the earlier projects | none |
| SDL2 | 2.30.9 | none (static on Linux, `SDL2.dll` on Windows) |

## Project files

Everything not listed as MAME's or third-party is original to this project
(GPL-2.0).
