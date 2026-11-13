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
  be clicked; needs an `id`), `bar` (a fill bar), `slots` (a grid of cells, for
  an inventory: `cols=`, `rows=`, `bind=player.inventory` or
  `bind=player.weapons` -- the guns carried; needs an `id`).
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
  maximum), `player.ammo`, `player.reserve`, `player.weapon`, `player.level`,
  `player.xp` (a bar shows it against the next level), `player.skill_points`,
  `player.ability.<n>` (the name on the Ability Bar's slot n, from 1; a bar
  shows its cooldown), `player.item.<id>` (how many the bag holds),
  `player.equipped.<slot>` (what is worn there), `skill.<id>` ("Learned",
  "Learn (1 pt)" or "Locked"),
  `flag.<key>`, `time`. Example: `text="{player.ammo} / {player.reserve}"`.
- **Enabled by a value:** `enabled=<value>` on a button greys it out (and it
  cannot be clicked) while the value is 0 -- `enabled=skill.<id>.can` (1 when
  that skill can be learned now), `enabled=bag.count` (an item is picked).
- **Slots:** a click on a cell picks it. `{<slots id>.name}`, `.count`,
  `.detail` (the item's kind), `.info` (its `about` line) and `.index` are the
  picked cell's, e.g. `text="{bag.name}"`.

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
| **On UI Button** (Button = id) | runs when that button is clicked -- or a cell of the slots grid with that id (Index: the cell) |
| **Selected Slot** (File, Id) | the picked cell of a slots grid (-1: none) |
| **Item In Slot** (Entity, Index) | what is in an inventory at that place: Item (its id), Name, Count -- for **Use Item** |
| **Show Message** (Text, Seconds) | a line across the top of the view (at the bottom while a menu is up) |
| **Pause Game** (Paused) | the game stands still; scripts and UI go on |
| **Quit Game** | ends Play in the editor; closes an exported game |

C++ scripts have the same in the API table (`ui_show`, `ui_hide`,
`ui_set_text`, `ui_shown`, `ui_selected`, `item_in_slot`, `set_game_paused`,
`quit_game`); a click is the gameplay event `ui.click` with the button's id as
its param (and a grid's cell as its value).

## Controls and settings

The player's controls are **actions**, each bound to a key or mouse button:
`move_forward`, `move_back`, `move_left`, `move_right`, `jump`, `sprint`,
`crouch`, `dash`, `fire`, `aim`, `reload`, `interact`, `ability_1` .. `ability_8`,
`next_weapon`, `prev_weapon`, `last_weapon`, `weapon_1` .. `weapon_4`,
`quick_save`, `quick_load`. The engine has defaults (WASD, Space, Left Shift,
Left Ctrl, V, the mouse buttons, R, E, 1-8, the mouse wheel, Q, F6, F9; the
weapon keys unbound). `Wheel Up` / `Wheel Down` can be bound like keys. Guns
and gear: docs/systems/WEAPONS_AND_GEAR.md. A project changes them and
adds its own in `input.cfg` in the project folder:

```
# input.cfg
inventory = I
label inventory = Open the bag
dash = Q
```

A player rebinds them in the game; their choices (and mouse sensitivity,
invert-Y, field of view) are kept per user and game, in the config folder
(`games/<project>/controls.txt`).

| Node | |
|---|---|
| **Action Pressed / Action Released** (Action) | runs when the action's key goes down / up (follows rebinding) |
| **Rebind Action** (Action) | the next key or mouse button pressed becomes the action's; Esc cancels; then the gameplay event `input.rebound` |
| **Reset Controls** | back to the project's controls |
| **Key For Action** (Action) | its key's name ("Space", "Mouse Left") |
| **Set Setting** (Name, Value), **Get Setting** (Name), **Next Setting Value** (Name) | a setting by name, below |

Settings by name: `quality` (Low, Medium, High), `ambient_occlusion`,
`reflections`, `clouds`, `volumetric` (On, Off), `shadow_quality` (Low, Medium,
High, Ultra), `render_scale` (50%, 67%, 75%, 100% -- on restart), `vsync` (On,
Off -- on restart), `fov` (Camera, 70 .. 110), `mouse_sensitivity` (0.05 .. 10),
`invert_y` (On, Off). Graphics settings are the machine's: the same as the
editor's render settings. Next Setting Value steps through a setting's
choices, so a settings menu needs one button per setting.

In UI documents, `{key.<action>}` shows an action's key ("press a key..."
while it is being rebound) and `{setting.<name>}` a setting's value -- in
labels and on buttons.

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
pause menu, settings, an inventory, a skill tree and a win screen this way:
`ui/*.ui` and its GameRules script.
