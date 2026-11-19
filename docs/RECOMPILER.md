# The SH-3 recompiler

`runtime/recomp/sh3_recomp.{h,cpp}` turns the game's SH-3 machine code into
native x86-64 code inside the running program. The game's code is never
shipped: it is translated on your machine, from your ROM, while you play,
and it is never written to disk.

## Where the code is

The CV1000-D's SH-3 starts in the 4 MB program ROM (`u4`). That boot program
copies the game itself from the NAND flash (`u2`) into the 16 MB main RAM
and jumps to it. Almost all of the time is spent in RAM code. So unlike a
ROM-only board, the program cannot be translated before it is loaded; the
recompiler works **lazily, as the CPU reaches code**:

1. The interpreter's execute loop (`sh3be_device::execute_run`, handwritten
   in `runtime/sh3/sh3_core.cpp` from MAME's) asks the recompiler for native
   code at every instruction boundary where it would fetch an opcode.
2. If the address has none, it is **noted**, and the interpreter runs the
   instruction as MAME would.
3. At the end of the frame, the recompiler walks the code from the noted
   addresses (following branches, calls and delay slots, up to 24,000
   instructions per batch), writes C for it and hands it to TinyCC **on a
   background thread**. The game keeps running on the interpreter meanwhile.
4. At a later frame end the compiled module is installed (after checking that
   the instructions in memory are still the ones that were translated), and
   from then on the CPU enters native code at those addresses.

In the test runs 16,000 (attract mode) to 167,000 (3.3 minutes of
two-player play) instructions were translated, in 46 to 699 modules.
Compilation took 0.3–3 s of background CPU time per run (up to 8.7 s when
the machine was busy with parallel runs), and 99.9% of SH-3 cycles were spent
in native code.

## What an instruction becomes

Each instruction is one block of C made from **MAME 0.264's handler for that
instruction** with the opcode's fields filled in.

- `tools/import_mame.py` imports MAME's SH-3 source (the `sh4` family's
  common code, `sh3be_device`) into `runtime/sh3/` as one flat class.
- `tools/gen_sh3_templates.py` turns each handler into a C template
  (`runtime/recomp/sh3_templates.inc`): member accesses become accesses to the
  register block (`S->r[n]`, `S->sr`, ...), memory accesses become the macros
  below, and C++-only constructs become C. It also generates a decoder probe
  (`sh3_probe.inc`) that runs MAME's own dispatch functions to learn which
  handler, and which fields, each of the 65,536 opcodes uses.
- 209 handlers exist; 142 are emitted inline. The other 67 (FPU and
  privileged instructions, register banks, exception logic, cache and
  MMU-related ones) are **call-outs**: the native code calls the
  interpreter's `execute_one(opcode)` for them, so they behave exactly as in
  MAME. The game spends little time in them.

Around each block the generated code repeats what MAME's execute loop does
between instructions:
- **Delay slots:** branches with a delay slot set `m_delay`; the slot
  instruction is emitted inline after the branch, and the jump follows it,
  as `execute_run` does.
- **Interrupts:** after each instruction, if `m_test_irq` is set the code
  calls MAME's `sh4_check_pending_irq` and continues at whatever `pc` that
  leaves.
- **Cycle count:** `icount` is decremented exactly as in MAME (one per
  instruction plus the handler's own extra cycles). When it runs out the
  code returns to the scheduler with the CPU state stored, at the same
  instruction where the interpreter would have stopped.
- **Control flow:** static branches inside a module are direct `goto`s;
  computed jumps (`JMP`, `RTS`, `BRAF`, ...) look the target up in the table
  of translated entry points and jump straight there when it is in the same
  module; otherwise the code returns to the execute loop, which enters the
  other module, or runs the interpreter if there is no native code yet.

## Memory accesses

`RB/RW/RL` and `WB/WW/WL` follow the device's `read_byte` ... `write_long`:
- main RAM and the program ROM are read (and RAM written) directly in the
  board's own word layout;
- every other address calls the board's bus (`read_long` etc.), with `pc`
  stored first so the board sees the same state as with the interpreter;
- the one 8-byte RAM word that MAME's idle-loop speedup watches
  (`0x0c002310`) always goes through the bus, so the speedup handler runs in
  the same places.

## Self-modifying and overwritten code

Code in RAM could be overwritten after it was translated (in the test runs
this never happened: no module was ever dropped, so this path has not been
exercised in a comparison with MAME).
- A map marks every RAM halfword that holds a translated instruction. Every
  write — from native code, from the interpreter, from DMA — is checked
  against it.
- A write to translated code **drops the module** that holds it and makes the
  running native code return after the current instruction. The new code is
  translated again when it is next reached.
- After a save state or rewind step is loaded, every module is checked
  against RAM and dropped if its instructions are no longer there.

## Correctness checks

- Every MAME comparison in [ACCURACY.md](ACCURACY.md) was run with the
  recompiler; CPU registers were identical at **every frame**.
- `ctest` runs `interpreter_matches_recompiled` for each version: the same
  inputs (2,400 frames) on the interpreter and on the recompiled code must
  give the same hash of RAM and screen every 100 frames.
- `CV1K_RECOMP_DEBUG=1` prints each batch of noted addresses;
  `CV1K_RECOMP_DUMP=DIR` writes the generated C (for development; it
  contains translated game code, so never share it).

## Speed

| Mode | Ver 1.5, 1-player script | Black Label, 2-player script |
|---|---|---|
| Recompiled | 221–225 fps (3.7× real time) | 178 fps (3.0×) |
| Interpreter (`--interpreter` / `--interp`) | 177–180 fps (3.0×) | 152 fps (2.5×) |

Headless release build, 8,000 frames from power-on (including the first-boot
EEPROM initialisation), on the test machine: a cloud container with 2 CPU
cores, figures from two runs each. The frontend adds presenting the picture
and audio.

The recompiled code is about 20–25% faster. Both are well above real time
on the test machine. Much of the remaining
time is the blitter and the YMZ770's MPEG decoder, which are the same in
both modes. TinyCC does little optimisation, so the gain from recompiling is
moderate.

## Limits

- **x86-64 only.** On other CPUs (or if TinyCC fails, for example where
  executable memory is forbidden) the program runs on the interpreter and
  logs why.
- Code reached for the first time runs on the interpreter until its batch is
  compiled, usually within a few frames.
- The recompiled code does not reorder, merge or skip instructions; it is a
  faithful unrolling of the interpreter, so it is only as accurate as MAME's
  SH-3 core, but no less.
