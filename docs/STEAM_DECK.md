# Steam Deck

The Steam Deck build is `DFKRecomp-SteamDeck-x86_64.AppImage`.
- It is a native Linux program: no Proton needed.
- It contains no game data.
- The SH-3 program is recompiled on the Deck while the game runs (in the
  background, starting at power-on).

**Status:** built, and run under Xvfb on a Linux PC. **Not yet run on a
Steam Deck.**

## Install

1. In Desktop Mode, copy the AppImage and your ROM zips into one folder, for
   example `~/Games/DFK/` with `ddpdfk.zip`, `ddpdfk10.zip` and `dfkbl.zip`.
   Zips beside the AppImage are found automatically.
2. Right-click the AppImage > Properties > Permissions > "Is executable"
   (or `chmod +x`).
3. Run it once from Desktop Mode. If a version shows *ROM not set*, open ROM
   Configuration and pick the zip (touchscreen, trackpad or controller).
4. Steam > Add a Non-Steam Game > browse to the AppImage. An optional launch
   option, for example `--version dfkbl`, always starts that version.
5. Launch from Game Mode.

## Behaviour on the Deck

**Screen**
- Starts in borderless fullscreen when the `SteamDeck` environment variable
  is present (Steam sets it). Menus are laid out for 1280×800.
- The game is vertical. On the Deck's landscape screen it is shown upright
  with wide side borders (Display > Background fills them).
- If you hold the Deck sideways ("tate"), Display > Rotation turns the
  picture to fill the screen.
- In Game Mode, gamescope provides XWayland, so `AppRun` selects SDL's X11
  driver there. Desktop Mode keeps SDL's default.

**Controls** arrive as a standard controller through Steam Input:
- A / B / X / Y = buttons A–D;
- View = coin, Menu = start;
- View + Menu = quick menu (Switch Version is there).

Player 2 uses the next controller. The test buttons (`F1` / `F2` on a
keyboard) can be mapped to spare buttons in Controller Settings.

**Other details**
- Settings, the EEPROM file, saves, screenshots and movies are kept in
  `~/.local/share/OmegaKatana/DoDonPachi Dai-Fukkatsu Recomp/`, outside the
  read-only AppImage, so they survive updates.
- Audio goes through PulseAudio / PipeWire.
- The game runs at 60.024 Hz. Frame pacing = Exact (the default) keeps that
  rate; Display sync runs one frame per refresh of the 60 Hz panel (0.04%
  slow).
- Rewind is off by default; on the Deck, try it before relying on it (see
  SAVE_STATES.md for its cost).

## Packaging details

- The binary links SDL2 2.30.9 statically and needs only glibc ≥ 2.29.
- SDL loads X11 / Wayland / PulseAudio / ALSA from SteamOS at run time.
- See `docs/BUILD_LINUX.md` ("Release build") and `scripts/build-appimage.sh`.

## Things to check on real hardware

- **Speed:** on the build machine (a cloud container with 2 cores), the
  headless runner reached 3.0–3.7× real time with the recompiled code and
  2.5–3.0× on the interpreter (RECOMPILER.md). The Deck was not measured. Dense
  bullet scenes cost more blitter time. If the Deck is too slow with the
  recompiled code, compare with `--interpreter` in the launch options (both
  give identical results).
- The blitter uses a second thread; the recompiler's compiler a third, for
  short bursts.
- Game Mode launch, fullscreen, focus with the Steam overlay (losing focus
  pauses by default), suspend and resume.
- The built-in controls, and an external pad as Player 2.
