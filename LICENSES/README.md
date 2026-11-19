# Third-party notices

This directory keeps the licences of the code and libraries the project uses.
It does not relicense the games or their ROMs, and no ROM data is included
anywhere.

| File | Component | Licence |
| --- | --- | --- |
| `MAME-BSD-3-Clause.txt` | MAME 0.264 code (see docs/MODIFIED_FILES.md):<br>• the SH-3 interpreter (`runtime/sh3`, imported by `tools/import_mame.py`; also the source of the recompiler's templates)<br>• the EP1C12 blitter, the YMZ770 and its MPEG audio decoder, the NAND flash and the RTC9701 (`runtime/devices`)<br>• the CV1000 board (`cv1k.cpp`): memory map, ports, inputs, idle-loop handler<br>• scheduler, screen and sound-stream behaviour (`runtime/emu/kemu.*`) | BSD-3-Clause |
| `TinyCC-LGPL-2.1.txt` | TinyCC (`runtime/third_party/tinycc`), the in-memory C compiler the recompiler uses | LGPL-2.1 |
| `miniz-MIT.txt` | miniz ZIP / deflate library | MIT |
| `SDL2-zlib.txt` | SDL2 (front end) | zlib |
| `Inter-OFL.txt` | Inter font embedded in the front end | SIL Open Font License 1.1 |

The repository's original code is under the GPL-2.0 in the root `LICENSE`.
