# Testing: checklist and compatibility matrix (0.1.0)

✅ passed · ⚠️ passed with a limitation (see the note) · ❌ failed · — not
applicable · **NT** not tested

All tests ran in a Linux container (Ubuntu 24.04, 2 CPU cores).

| Platform | How it was tested |
|---|---|
| **Linux** | the development build (GCC 13) and the release build (zig, glibc 2.31 target) |
| **Windows** | the cross-compiled release build, under Wine 9.0 |
| **Steam Deck** | the AppImage, run under Xvfb with a window manager (openbox) and the home folder laid out as on a Deck (`/home/deck`, `SteamDeck=1`), not on a Steam Deck |

Every frontend test used the keyboard through `xdotool`. Audio ran through
SDL's dummy driver (Linux) or found no device (Wine): no sound was listened
to.

## 1. Per-version checklist

The three versions share the board and the frontend, so most rows apply to
all three; differences are noted. L = Linux, W = Windows (Wine), D =
AppImage.

| Check | Ver 1.5 | Ver 1.0 | Black Label | How |
|---|---|---|---|---|
| Application launches | ✅ L W D | ✅ L D | ✅ L D | frontend started |
| Game data detected | ✅ | ✅ | ✅ | by `--rom`, by path, and found automatically beside the AppImage / the Windows executable (in a folder with a Japanese name) |
| Wrong / incomplete data refused with a clear message | ✅ | ✅ | ✅ | another version's zip, a folder without `u2`, a split `ddpdfk10.zip` without `ddpdfk.zip` beside it (and accepted with it) |
| Title screen correct | ✅ | ✅ | ✅ | pixel-identical to MAME (attract runs) |
| Ship and control-type selection | ✅ | ✅ | ✅ | scripted runs identical to MAME; seen and used in the frontend (Black Label, Ver 1.5 on Windows) |
| Gameplay starts | ✅ | ✅ | ✅ | frontend: Black Label (AppImage), Ver 1.5 (Windows); all three in the scripted runs |
| Graphics render correctly | ✅ | ✅ | ✅ | 118 screenshots pixel-identical to MAME (6 per attract run, 20 per play run) |
| Animations | ✅ | ✅ | ✅ | follow from RAM identical to MAME at every compared frame and the screenshots |
| Music, effects, voices | ⚠️ | NT | NT | checked as data: Ver 1.5 WAV against MAME's recording, correlation 0.98–0.99; Windows and Linux builds give sample-identical WAVs; **not listened to** |
| Keyboard inputs | ✅ | ✅ | ✅ | frontend: coin, start, movement, buttons, hotkeys |
| Controller inputs | NT | NT | NT | no physical controller |
| Shots, bombs, other buttons | ✅ | ✅ | ✅ | random inputs with all four buttons, identical to MAME |
| Game timing | ✅ | ✅ | ✅ | CPU registers identical to MAME at every frame |
| Frame pacing | ⚠️ | ⚠️ | ⚠️ | 59.5–61 fps on the FPS gadget under Xvfb / Wine; with rewind on and a movie recording it dropped to about 56 fps on the 2-core test machine; not measured on a real display |
| Recompiled code used | ✅ | ✅ | ✅ | 99.9% of SH-3 cycles in native code in every run; the gadget shows it |
| Test menu | ✅ | NT | NT | holding the S3 switch or the JAMMA test button opens the TEST MENU (headless, Ver 1.5) |
| Quick menu, resume | ✅ | ✅ | ✅ | |
| Save / load state | ✅ | ✅ | ✅ | headless round trips exact; frontend quick save / load (Black Label, Ver 1.5 on Windows), slot overwrite question |
| Rewind | ✅ | ✅ | ✅ | headless rewind test exact (ctest, all three); frontend: rewind on, 64 MB held 6.8–10 s, stepping back (Black Label) |
| Input movies | ⚠️ | — | ⚠️ | frontend: recorded and played back to its end (Black Label); exactness of playback not compared; another version's movie is not offered |
| EEPROM / flash file | ✅ | ✅ | ✅ | one file per version written after the first boot (all three; 133 KB with the rewritten flash block); restored at the next start (Ver 1.0, from the log) |
| Settings persist after restart | ✅ | ✅ | ✅ | ROM paths, last version, rewind setting in `settings.ini` |
| Continue after restart | — | ✅ | — | autosave on exit, Continue in the next session |
| Game exits safely | ✅ | ✅ | ✅ | exit confirmation, autosave, EEPROM written, process ends (AppImage and Windows) |
| Switching versions | ✅ | ✅ | ✅ | Ver 1.5 → Black Label from the main menu; Black Label → Ver 1.0 in the quick menu, with the confirmation and autosave |

## 2. Accuracy runs (details in ACCURACY.md)

| Version | Run | Linux dev | Linux release | Windows release (Wine) |
|---|---|---|---|---|
| Ver 1.5 | attract 3,600 frames | ✅ | ✅ | ✅ on the interpreter |
| Ver 1.5 | 1-player 12,000 frames | ✅ | ✅ | ✅ frames 3,600–6,000 from a Linux state |
| Ver 1.5 | 2-player 12,000 frames | ✅ | ✅ | — |
| Ver 1.0 | attract | ✅ | ✅ | — |
| Ver 1.0 | 1-player | ✅ | ✅ | — |
| Black Label | attract | ✅ | ✅ | — |
| Black Label | 1-player | ✅ | ✅ (also on the interpreter) | — |
| Black Label | 2-player | ✅ | ✅ | — |
| all | ctest: boot, state round trip, rewind, interpreter = recompiled | ✅ | — | — |
| Black Label | state round trip | ✅ | ✅ | ✅ (same hash as Linux) |
| Ver 1.5 | state saved by Linux, loaded by Windows, and the other way round | — | ✅ | ✅ |

"✅" means: CPU registers and every compared RAM image identical to MAME,
the state re-save identical, and the rewind test reproducing every state.

**Cross-platform states.** A state saved by the Windows build loads in the
Linux build and the other way round, and both continue identical to MAME.
The two files are not byte-identical: about 45,000 bytes differ, all in the
low bits of floating-point values inside the YMZ770's MPEG decoder buffers
(Windows' and Linux's maths libraries round differently). The game state
and, in the 80-second test, the audio output (16-bit samples) are identical.

## 3. Compatibility matrix

| Feature | Implemented | Linux | Windows | Steam Deck |
|---|---|---|---|---|
| Ver 1.5 | ✅ | ✅ | ✅ (Wine) | ⚠️ AppImage under Xvfb (menus only) |
| Ver 1.0 | ✅ | ✅ | NT (Linux ✅) | ⚠️ AppImage under Xvfb |
| Black Label | ✅ | ✅ | ✅ headless (Wine) | ⚠️ AppImage under Xvfb |
| Recompiled SH-3 | ✅ | ✅ | ✅ (TinyCC PE code under Wine) | ⚠️ AppImage under Xvfb |
| Interpreter (`--interpreter`) | ✅ | ✅ | ✅ headless | NT |
| Save states, Continue | ✅ | ✅ | ✅ | ⚠️ |
| Rewind | ✅ | ✅ | NT in the frontend | ⚠️ |
| Input movies | ✅ | ⚠️ (see above) | NT | ⚠️ |
| EEPROM / flash (NVRAM) | ✅ | ✅ | ✅ (file written) | ⚠️ |
| Display options (scaling, filters, CRT, rotation) | ✅ (shared frontend) | ⚠️ Xvfb, OpenGL | ⚠️ Wine | NT |
| Audio output | ✅ | ⚠️ dummy driver | ⚠️ no device | NT |
| Controllers, hot-plug | ✅ (SDL GameController) | NT | NT | NT |
| Gadgets (version, recompiled code, FPS, frame time, input display) | ✅ | ✅ | ✅ FPS | NT |
| Unicode paths | ✅ | ✅ | ✅ (Japanese folder name) | — |
| No game data in packages | ✅ | ✅ checked | ✅ checked | ✅ checked |

## 4. Release packages

| Check | Result |
|---|---|
| Windows zip runs from an extracted folder | ✅ under Wine |
| Windows: DLLs needed | `SDL2.dll` (included) plus Windows system DLLs |
| Linux binary: glibc needed | 2.29 at most; links libc, libm, libpthread and libdl only |
| AppImage: starts fullscreen at 1280×800, finds the zips beside it, plays | ✅ under Xvfb + openbox |
| No ROM or recompiled code in any package | ✅ listed and checked |

## 5. Not tested

- A real Windows 10/11 PC; a real Steam Deck (Game Mode, controls, suspend,
  speed).
- Physical controllers; audio on real speakers.
- Play by a person, later stages, bosses, endings, name entry.
- The test menu's options.
- Overwritten translated code (the game never did it in the test runs).
