# Controls

The cabinet controls follow MAME's `cv1k` input ports. Per player there is
an 8-way stick, four buttons, Start and Coin. The board also has a service
coin input, the test button on the JAMMA edge and the S3 test switch on the
board itself. Both players are supported on keyboard and controllers. Each
version keeps its own mappings.

The buttons are labelled A–D as on the cabinet (MAME's buttons 1–4). What
each one does is set by the game, which lets you change it in its own test
menu.

Everything can be remapped in **Controller Settings** (two keys and one
controller button/axis per input), with "restore defaults". Controllers are
hot-plugged. Menu input never reaches the game: while a menu is open the game
is paused, and buttons held when it closes are ignored until released.

## Default keyboard

| Input | Player 1 | Player 2 |
|---|---|---|
| Up / Down / Left / Right | Arrow keys | I / K / J / L |
| Button A | Z | F |
| Button B | X | G |
| Button C | C | H |
| Button D | V | T |
| Start | Enter (or 1) | 2 |
| Coin | 5 | 6 |

Cabinet: Service coin `9`, JAMMA test button `F1`, S3 test switch `F2`.

Frontend hotkeys (remappable): `Esc` quick menu, `F5` quick save, `F9` quick
load, `F6` / `F7` previous / next slot, `F12` screenshot, `F11` fullscreen,
`Backspace` (hold) rewind, `Pause` or `P` pause, `F10` FPS counter, `F3` reset.

## Default controller

Each player uses the controller assigned under Controller Settings > Player
Controllers (automatic: 1st controller = P1, 2nd = P2).

| Input | Xbox / Steam Deck | PlayStation |
|---|---|---|
| Move | D-pad (left stick also works) | |
| Button A | A | Cross |
| Button B | B | Circle |
| Button C | X | Square |
| Button D | Y | Triangle |
| Start | Start / Menu | Options |
| Coin | Back / View | Share |
| Quick menu | Back + Start held together (configurable) | |

## Opposing directions

As in MAME (whose default disallows "contradictory" joystick input), left +
right or up + down held together cancel each other, as on a real 8-way stick.

## Test menu

The CV1000 has no DIP switches; the game's backup data (the settings
made in its test menu, among others) is kept in the RTC9701's EEPROM, which the
frontend saves between sessions (see FRONTEND.md).

**Hold `F2`** (the S3 test switch) **or `F1`** (the JAMMA test button) for a
few seconds while the game runs to get the game's TEST MENU (input and
output tests, sound, colour and cross-hatch screens, game settings, memory
check, backup data, defaults). In the headless check (Ver 1.5) holding either
input from the attract mode opened the TEST MENU; a 1-second press did not.
The menu's options were not walked through or compared with MAME.
