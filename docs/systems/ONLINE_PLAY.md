# Online play

A project can be played together over the network: co-op, a raid, a duel --
hosted by one player, joined by others at the host's address. It is opt-in:
a project without `config/multiplayer.ini` plays exactly as before.

## Turning it on

`config/multiplayer.ini` in the project:

```
game_id=mygame.1                    # both sides must have the same (and the same engine)
start_scene=scenes/start.scene      # Play opens here: the mode menu
menu_ui=ui/online_start.ui          # the menu (its buttons below)
session_ui=ui/online_session.ui     # session controls (F10)
status_ui=ui/online_status.ui       # who is in, scores
hud_ui=ui/hud.ui                    # the game's HUD
objective_id=objective              # the HUD label the mode's objective goes in
session_hide=ui/menu.ui             # documents a session hides (a solo title menu)
sync_flags=boss.phase,boss.defeated # world flags the host sends to everyone
sync_ui=ui/boss.ui,ui/win.ui        # documents the host's show/hide is mirrored for
fingerprint=testgame/GameRules.lgraph,assets/scripts/boss.cpp   # files that must match to join
respawn_supplies=rifle_rounds:600,health_potion:3               # given on a respawn
mark_tag=status.marked              # optional: a tag that shows mark_hint to its player
mark_hint=You are marked.

mode.1.key=coop
mode.1.scene=scenes/coop_world.scene
mode.1.title=Co-op Expedition
mode.1.players=4
mode.1.guest_offset=3,0,0           # where a guest appears beside the host
mode.1.respawn_seconds=6
mode.1.wipe_seconds=8               # everyone down this long: the party is restored
mode.2.key=duel
mode.2.scene=scenes/duel_arena.scene
mode.2.title=PvP Duel
mode.2.players=2
mode.2.rounds_to_win=3              # a duel: rounds, scored
mode.2.arena_radius=54              # outside it is a lost round
mode.2.health=150
mode.2.skill_points=12
mode.2.objective=Win three rounds
mode.2.victory_flag=boss.defeated   # (a raid) the world flag that ends it
mode.2.victory_text=Boss defeated
```

The menu document's buttons are found by id: `online_solo_<n>` and
`online_host_<n>` for mode n, `online_endpoint` (the host:port text) and
`online_join`; session controls have `online_back`, `online_restart` and
`online_return`.

## How it plays

- **Host** starts mode n on a UDP port (7777 unless the address field says
  otherwise) and accepts guests; **Join** connects to `host:port`. A joining
  player loads the host's scene after its `fingerprint` files are checked.
- The host runs the game: damage, ammunition, reloads, cooldowns, skills,
  enemy AI, native simulation scripts and duel scores. Guests send their weapon,
  ability, item and skill actions and receive the results; each moves its own
  player (movement is the client's -- casual hosted play, not anti-cheat).
- What is sent: gameplay attributes, inventories, progression, combat state,
  actors, spawned encounter visuals, animation clips and procedural bone poses,
  and the UI listed in `sync_ui`; unchanged data is left out between full
  snapshots, and a late joiner gets a full one. Other players are drawn with
  the authored character model and get matching hurtboxes.
- Co-op has no friendly fire. In a duel the two sides are `team.duelist.host`
  and `team.duelist.guest`. Leaving a hosted session ends it for everyone (no
  host migration). Saving and loading are off during a session, and menus do
  not pause the shared game.
- LAN: join the host's LAN address. Internet: the host's public address with
  its UDP port reachable (router/firewall), or a shared VPN. No relay,
  matchmaking or accounts.

`GWS_ONLINE_DISABLE=1` ignores the configuration (plays solo as before);
`GWS_ONLINE_DIAGNOSTICS=1` logs each side's state once a second.
