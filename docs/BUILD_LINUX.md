# Building on Linux

Requirements: CMake ≥ 3.20, Ninja (or make), C11 and C++17 compilers, SDL2
development files (`libsdl2-dev`) for the frontend. No ROM and no Python are
needed to build: the imported MAME code (`runtime/sh3/*.inc`,
`runtime/devices/*_mame.inc`) and the recompiler's templates
(`runtime/recomp/sh3_templates.inc`, `sh3_probe.inc`) are kept in the
repository. Rerun `tools/import_mame.py MAME_0.264_SOURCE` and
`tools/gen_sh3_templates.py` only to re-import them. TinyCC is built from
`runtime/third_party/tinycc`; the recompiler targets x86-64. The CMake files
ship in the release source package (they are not kept in the git
repository).

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cp -r profiles build/bin/
./build/bin/DFKRecomp
```

Tests with your own game files (any subset):

```bash
scripts/build-local.sh --ddpdfk ~/roms/ddpdfk.zip --ddpdfk10 ~/roms/ddpdfk10.zip --dfkbl ~/roms/dfkbl.zip
```

This configures with `-DDFK_ROM_DDPDFK=…` / `-DDFK_ROM_DDPDFK10=…` /
`-DDFK_ROM_DFKBL=…` and runs `ctest`. For each version it runs:
- a 600-frame boot;
- a save-state round trip (`--state-test`);
- the rewind test;
- a check that the recompiled and the interpreted SH-3 give identical
  hashes over 2,400 frames of the 1-player script.

The paths only end up in your local build directory.

## Headless runner

`cv1k_headless --version ddpdfk|ddpdfk10|dfkbl --rom PATH --frames N` runs
the board without a window.

| Option | |
|---|---|
| `--inputs FILE` | lines `FRAME MASK` (MASK = `cv1k::IN_*` bits from `runtime/include/cv1k/machine.h`), applied from FRAME on |
| `--mame-latency` | apply inputs one frame later, as MAME applies Lua `set_value()` |
| `--interp` | MAME's SH-3 interpreter instead of the recompiled code |
| `--rtc "YYYY-MM-DD HH:MM:SS"` | clock time at power-on (default: the host's local time) |
| `--eeprom FILE`, `--save-eeprom FILE` | start from / write a 512-byte EEPROM image (MAME's `nvram/<set>/eeprom`) |
| `--hash N`, `--hash-log FILE` | hash RAM and screen every N frames (printed at the end) / write the hashes |
| `--png FRAME FILE` | the screen after FRAME (repeatable); `--rotated` turns it like MAME's snapshots (240×320) |
| `--dump FRAME FILE` | main RAM after FRAME |
| `--wav FILE` | the board's mono output at 16,000 Hz |
| `--compare DIR` | compare with a MAME reference recorded by `tools/cv1k_reference.lua` (see ACCURACY.md) |
| `--state-test FRAME` | save, run 300 frames with other inputs, load, re-save: must be identical |
| `--rewind-test FRAME` | 31 captures 4 frames apart, restored newest first, each replayed: all must reproduce exactly |
| `--save-state FRAME FILE`, `--load-state FILE` | state files |
| `--trace FROM TO FILE` (instruction trace between two frames), `--vram-dump FRAME FILE`, `--diffs N` (stop after N differing frames in `--compare`), `--quiet` | development aids |

Environment: `CV1K_BLIT_THREAD=0` (draw on the main thread),
`CV1K_RECOMP_DEBUG=1`, `CV1K_RECOMP_DUMP=DIR`, `CV1K_IPROF=1` (interpreter
profile).

## Release build (portable binary for SteamOS and older distributions)

The release binary links SDL2 statically and targets glibc 2.31 (Steam Runtime
"sniper" / Debian 11 baseline); SDL2 loads X11, Wayland, PulseAudio
(PipeWire's pulse server on the Deck) and ALSA from the system at run time.
The packages were built with zig as the C/C++ compiler:

```bash
pip install ziglang
mkdir -p ~/zigcc && for t in cc c++; do cat > ~/zigcc/$t <<EOS
#!/bin/sh
exec python3 -m ziglang $t -target x86_64-linux-gnu.2.31 -idirafter /usr/include -idirafter /usr/include/x86_64-linux-gnu "\$@"
EOS
done
printf '#!/bin/sh\nexec python3 -m ziglang ar "$@"\n' > ~/zigcc/ar
printf '#!/bin/sh\nexec python3 -m ziglang ranlib "$@"\n' > ~/zigcc/ranlib
chmod +x ~/zigcc/*

# static SDL2 2.30.9 (video/audio backends loaded dynamically)
CC=~/zigcc/cc cmake -S SDL2-2.30.9 -B sdl-build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_AR=$HOME/zigcc/ar -DCMAKE_RANLIB=$HOME/zigcc/ranlib \
  -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_STATIC_PIC=ON -DSDL_TEST=OFF -DCMAKE_INSTALL_PREFIX=$PWD/sdl-static
cmake --build sdl-build && cmake --install sdl-build

CC=~/zigcc/cc CXX=~/zigcc/c++ cmake -S . -B build/linux-release -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_AR=$HOME/zigcc/ar -DCMAKE_RANLIB=$HOME/zigcc/ranlib \
  -DSDL2_DIR=$PWD/sdl-static/lib/cmake/SDL2 -DDFK_BUILD_SDL2=ON
cmake --build build/linux-release
APPIMAGETOOL=/path/to/appimagetool ./scripts/build-appimage.sh
```

The result is `dist/DFKRecomp-SteamDeck-x86_64.AppImage`. It contains
the program, the three version profiles, the icon, licences and the user docs; no
game data.

Check: `ldd build/linux-release/bin/DFKRecomp` lists only libc, libm,
libpthread and libdl (plus the loader); `objdump -T … | grep GLIBC_` tops out
at 2.29.

## Verified (2026-11-19, Ubuntu 24.04 container)

- Development build (GCC 13) and release build (zig `cc`/`c++`, glibc 2.31
  target, SDL2 2.30.9 static).
- All MAME comparisons in ACCURACY.md pass with both builds.
- See [TESTING.md](TESTING.md) for the glibc and library checks of the
  release binary and the AppImage run.
- Not run on physical Linux desktop hardware or a Steam Deck.
