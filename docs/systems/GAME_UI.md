# Game UI: menus, HUDs and screens

A game's menus, HUD and screens are files in its project (`.ui`), shown and
hidden by its scripts. Nothing about a particular game's UI is in the engine.

## A UI document

One widget per line, values as `key=value`, `#` starts a comment:

```
# ui/pause.ui -- the pause menu
ui modal pause layer=30
panel  id=dim     anchor=fill color=0,0,0,0.6
label  id=title   anchor=center x=0 y=-210 w=480 h=80 text="Paused" size=60 align=center
button id=resume  anchor=center x=0 y=-90 w=400 h=66 text="Resume" size=28
button id=quit    anchor=center x=0 y=170 w=400 h=66 text="Quit" size=28
```

- **`ui`** line, options: `modal` (while shown, the mouse is free over the game
  and its buttons work; the game's own input is off), `pause` (the game stands
  still while shown), `layer=N` (higher is drawn on top).
- **Widgets:** `panel` (a coloured box), `label` (text), `button` (text that can
  be clicked; needs an `id`), `bar` (a fill bar).
- **Placement:** in a 1920×1080 reference screen, scaled to the real one.
  `anchor` is the point of the screen the widget hangs from, and the point of
  the widget that sits there: `top_left`, `top`, `top_right`, `left`, `center`,
  `right`, `bottom_left`, `bottom`, `bottom_right`. `x`, `y` move it from there;
  `w`, `h` are its size. `anchor=fill` covers the screen.
- **Looks:** `color=r,g,b,a` (0..1), `size=` (text, in reference pixels),
  `align=left|center|right`, `text_color=` and `hover=` for buttons, `bg=` for
  bars, `hidden=1`.
- **Live values:** text in braces and a bar's `bind=` read the game each frame:
  `player.<Attribute>` (e.g. `player.Health`; a bar shows it against its
  maximum), `player.ammo`, `player.reserve`, `player.weapon`, `flag.<key>`,
  `time`. Example: `text="{player.ammo} / {player.reserve}"`.

A line that cannot be read is reported (with its line number) when the
document is shown; the rest still shows.

## Scripts

Logic scripts (category UI and Game):

| Node | |
|---|---|
| **Show UI** (File) | shows a document, e.g. `ui/pause.ui` |
| **Hide UI** (File) | |
| **Set UI Text** (File, Id, Text) | a label's or button's text |
| **Is UI Shown** (File) | |
| **On UI Button** (Button = id) | runs when that button is clicked |
| **Pause Game** (Paused) | the game stands still; scripts and UI go on |
| **Quit Game** | ends Play in the editor; closes an exported game |

C++ scripts have the same in the API table (`ui_show`, `ui_hide`,
`ui_set_text`, `ui_shown`, `set_game_paused`, `quit_game`); a click is the
gameplay event `ui.click` with the button's id as its param.

## Esc, and getting the mouse back in the editor

In Play the game holds the mouse; the editor's panels get none of it.

- **Shift+F1** always frees the mouse for the editor (click the game view to
  go back).
- **Esc** frees it too, unless the project gives Esc to the game: add
  `escape = game` to `project.schizo`. Esc is then only the game's -- for a
  pause menu: a script listens with Key Pressed (Escape) and shows its menu,
  which, being modal, frees the mouse for its buttons. Closing the menu gives
  the mouse back to the game.

## Example

The TestGame project (docs/systems/TEST_GAME.md) has a title menu, a HUD, a
pause menu and a win screen this way: `ui/*.ui` and its GameRules script.
