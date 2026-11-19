# Building for Windows 10/11 (x86-64)

Two routes produce the same program: a native MinGW build on Windows, or a
cross-compile from Linux. The CMake files ship in the release source package.
No ROM is needed to build.

## A. Native build on Windows (MSYS2)

1. Install MSYS2, open the "MSYS2 MINGW64" shell and run
   `pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-SDL2`
2. Unpack the source package, then:

```bash
cd DoDonPachi-DaiFukkatsu-Recomp-0.1.0-source
cmake -S . -B build/cmake -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/cmake
cp /mingw64/bin/SDL2.dll build/cmake/bin/
cp -r profiles build/cmake/bin/
./build/cmake/bin/DFKRecomp-Windows-x86_64.exe
```

The SDL2 "mingw" development package also works; pass
`-DSDL2_DIR=<SDL2>\x86_64-w64-mingw32\lib\cmake\SDL2`. Route A was not
tried for this release (the packages were cross-compiled, route B). Building
with MSVC was not tried.

## B. Cross-compile from Linux

```bash
sudo apt install gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64-posix cmake ninja-build
# SDL2 2.30.9 mingw development package from https://github.com/libsdl-org/SDL/releases
tar xzf SDL2-devel-2.30.9-mingw.tar.gz
cmake -S . -B build/win64-release -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake \
  -DSDL2_DIR=$PWD/SDL2-2.30.9/x86_64-w64-mingw32/lib/cmake/SDL2
cmake --build build/win64-release
SDL2_DLL=$PWD/SDL2-2.30.9/x86_64-w64-mingw32/bin/SDL2.dll ./scripts/package-windows.sh
```

The package (`dist/DFKRecomp-Windows-x86_64.zip`) contains the
executable, `SDL2.dll`, the three version profiles, licences and docs, and no
game data. TinyCC is built in with its PE (Windows) target, so the
recompiled code is made on the user's PC. The executable links libgcc/libstdc++ statically; it needs only
system DLLs and `SDL2.dll`.

## Using it

Unzip anywhere and run
`DFKRecomp-Windows-x86_64.exe`. Choose your ROM sets under ROM
Configuration (ROM paths may contain any Unicode characters). Settings and saves go to
`%APPDATA%\OmegaKatana\DoDonPachi Dai-Fukkatsu Recomp\`, or beside the executable with
`--portable` or a `portable.txt` file there. Windows SmartScreen may warn
because the executable is not code-signed.

## Verified (2026-11-19)

- Cross-compiled with MinGW-w64 GCC 13 (posix threads).
- Under Wine 9.0 (see [TESTING.md](TESTING.md) for the full list):
  - `cv1k_headless.exe` recompiles the game with its built-in TinyCC (PE
    target) and runs the 1-player script identical to MAME, including a
    save-state round trip;
  - the frontend starts, finds the ROMs and runs the game.
- Wine had no audio device in the test container (dummy driver), so sound
  output on Windows was not checked.
- **Not run on a Windows 10/11 PC.**
