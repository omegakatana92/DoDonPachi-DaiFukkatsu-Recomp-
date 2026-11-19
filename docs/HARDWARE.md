# The board model

DoDonPachi Dai-Fukkatsu runs on CAVE's **CV1000-D**, the later version of
the CV1000 with 16 MB of main RAM. The model follows MAME 0.264's
`src/mame/cave/cv1k.cpp` (`cv1k_d` machine configuration), and most device
code is MAME's own, imported by `tools/import_mame.py`
(see [MODIFIED_FILES.md](MODIFIED_FILES.md)).

| Part | Here | Notes |
|---|---|---|
| Hitachi SH-3 (SH7709S), 102.4 MHz (12.8 MHz × 8), big-endian | `runtime/sh3/` (MAME's `sh3be_device`), `runtime/recomp/` | game code recompiled, see RECOMPILER.md. On-chip timers, DMA, interrupt controller, refresh counter, ports as in MAME |
| Program ROM `u4`, 4 MB | `cv1k::machine` | at 0, 64-bit bus |
| Main RAM, 16 MB | `cv1k::machine` | at 0x0c000000 |
| Samsung K9F1G08U0M NAND flash `u2`, 128 MB + spare | `runtime/devices/cv1k_devices.cpp` (MAME's `nand_device`) | the game, graphics and music; read through ports at 0x10000000 |
| Altera Cyclone EP1C12 blitter | `runtime/devices/ep1c12/` (MAME's `ep1c12_device` and its blit functions) | 8192 × 4096 × 32-bit video RAM; draws command lists from main RAM; its busy time is estimated as in MAME |
| Yamaha YMZ770 | `runtime/devices/cv1k_devices.cpp` (MAME's `ymz770_device`), `mpeg_audio.*` | 16 channels of MPEG audio from the 8 MB sample ROM `u23`+`u24` plus sequencers; 16 kHz stream |
| Epson RTC9701 | `runtime/devices/cv1k_devices.cpp` (MAME's `rtc9701_device`) | real-time clock and the 512-byte EEPROM with the game's backup data, on the serial port at 0x10c00000 |
| Screen | `cv1k::machine` | 60.024 Hz, 320 × 240 visible in a 512 × 512 frame; VBLANK raises IRQ 2 (held until acknowledged) |
| Inputs | `cv1k::machine` | MAME's `cv1k` ports on the SH-3's I/O ports: coins, starts, service and the JAMMA test button on port C; player 1 on port D, player 2 on port L (8-way stick, 4 buttons each); the S3 test switch on port F; the NAND busy line on port E; the blitter's FPGA configuration on port J |

## Memory map (`cv1k_d_map`)

| Address | |
|---|---|
| `0x00000000`–`0x003fffff` | program ROM |
| `0x0c000000`–`0x0cffffff` | main RAM |
| `0x10000000`–`0x10000007` | NAND flash data / command / address |
| `0x10400000`–`0x10400007` | YMZ770 (write only) |
| `0x10c00000`–`0x10c00007` | RTC9701 / EEPROM serial port |
| `0x18000000`–`0x18000057` | blitter registers |
| `0xf0000000`–`0xf0ffffff` | SH-3 cache RAM area |
| `0x04000000`…, `0xffffd000`… | SH-3 on-chip registers |

## Timing

- **Scheduler:** `runtime/emu/kemu.*` reproduces MAME's timer list and time
  slicing: the CPU runs until the next timer, timers fire in MAME's order,
  and the CPU's "spin until trigger / time" used by the idle-loop handler
  behaves as in MAME.
- **Idle loop:** like MAME's `init_ddpdfk`, a read of RAM word `0x0c002310`
  from the game's wait loop (`pc = 0x0c1d1346`) makes the CPU skip 10 µs.
  This changes how fast MAME (and this program) run, not what the game does,
  and doing it in exactly the same places is what keeps both identical.
- **Blitter:** a list is copied when it is started, its duration estimated
  from the list (MAME's model), and the "busy" flag cleared by a timer at
  that time. The drawing itself runs on a worker thread (MAME uses its work
  queue); results are waited for before anything reads them, so threading
  does not change the picture. `CV1K_BLIT_THREAD=0` draws on the main thread.
- **Sound:** the YMZ770's stream is brought up to date when the CPU writes to
  the chip and at MAME's 50 Hz sound-manager update, as in MAME.
- **Frame:** `run_frame()` runs until the next VBLANK has been handled,
  then the screen (320 × 240) is copied from video RAM at the blitter's
  scroll position.

## Not modelled / differences from the real board

The model reproduces MAME 0.264, including MAME's simplifications:
- the blitter's timing is MAME's estimate, not the real FPGA's;
- the CPU's caches are not modelled beyond what MAME does;
- MAME (and so this program) takes only the YMZ770's right channel to the
  mono speaker.
