# ROM setup

The program needs your own **MAME 0.264** ROM sets. Nothing from the games is
included in this repository or in any package, and the program never writes
to your ROM files.

## The sets

All three run on CAVE's CV1000-D board. Each set has four files: the SH-3
program ROM (`u4`), the NAND flash with the game, graphics and music (`u2`,
132 MB) and the two halves of the YMZ770's sample ROM (`u23`, `u24`).

### DoDonPachi Dai-Fukkatsu Ver 1.5 — MAME `ddpdfk` (`ddpdfk.zip`)

| File | Size | CRC32 |
|---|---|---|
| `ddpdfk_u4` | 4 MB | `9976D699` |
| `ddpdfk_u2` | 132 MB | `84A51A4F` |
| `u23` | 4 MB | `27032CDE` |
| `u24` | 4 MB | `A6178C2C` |

### DoDonPachi Dai-Fukkatsu Ver 1.0 — MAME `ddpdfk10` (`ddpdfk10.zip`)

| File | Size | CRC32 |
|---|---|---|
| `ddpdfk10_u4` | 4 MB | `A3D650B2` |
| `ddpdfk10_u2` | 132 MB | `D349CB2A` |
| `u23` | 4 MB | `27032CDE` |
| `u24` | 4 MB | `A6178C2C` |

In MAME `ddpdfk10` is a clone of `ddpdfk` and shares `u23` / `u24` with it.
A "split" `ddpdfk10.zip` that lacks them works if `ddpdfk.zip` is in the same
folder; the program then takes those two files from it (by CRC).

### DoDonPachi Dai-Fukkatsu Black Label — MAME `dfkbl` (`dfkbl.zip`)

| File | Size | CRC32 |
|---|---|---|
| `u4` | 4 MB | `8092CA9D` |
| `u2` | 132 MB | `29F9D73A` |
| `u23` | 4 MB | `36D4093B` |
| `u24` | 4 MB | `31F9EB0A` |

The flash file `u2` is 0x8400000 bytes: 128 MB of data plus the NAND's spare
areas, as MAME dumps it. Its CRC is checked like the others (on a slow disk
this takes a moment when a version is selected).

## How files are found

- A path can be a zip or a folder. Inside a zip, files may be at the top or
  in one sub-folder (`ddpdfk10/u2`, as some ROM managers store them).
- Each file is matched by its name first and by CRC32 otherwise, so renamed
  files are found too.
- A set that is incomplete or has a wrong CRC is refused with a list of what
  is missing or different. If the files belong to one of the other two
  versions, the message says so.
- Another version is never loaded in place of the one you chose.

## In the frontend

1. **ROM Configuration** lists the three versions. Pick each zip you have
   (any subset); they may live in any folder, and paths may contain any
   Unicode characters.
2. Zips named `ddpdfk.zip`, `ddpdfk10.zip` or `dfkbl.zip` are picked up
   automatically from: the program's folder, the folder holding the AppImage,
   the data folder and its `roms` sub-folder, `~/Downloads` and `~/roms`.
3. **Select Version** shows each version as *ready*, *ROM not set* or *ROM
   not usable* with the reason.

## Headless runner

```bash
cv1k_headless --version ddpdfk   --rom ddpdfk.zip   --frames 3000
cv1k_headless --version ddpdfk10 --rom ddpdfk10.zip --frames 3000
cv1k_headless --version dfkbl    --rom dfkbl.zip    --frames 3000
```

## What is computed from your files

- A ROM fingerprint (CRC32 of the program ROM) labels save states and the
  EEPROM file, so different versions never share them.
- The recompiled code is made in memory from the program as it runs and is
  never written to disk.
