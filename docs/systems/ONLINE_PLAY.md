# Online play

A project can be played together over the network: co-op, a raid, a duel --
hosted by one player, joined by others with a **lobby code** (over the
internet) or at the host's address (LAN, or a forwarded port). It is opt-in:
a project without `config/multiplayer.ini` plays exactly as before.

## Turning it on

`config/multiplayer.ini` in the project:

```
game_id=mygame.1                    # both sides must have the same (and the same engine)
start_scene=scenes/start.scene      # Play opens here: the mode menu
lobby_server=lobby.example.com:7780 # optional: a relay for lobby codes (below); without it codes still work
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

The menu document's buttons are found by id:

| Id | |
|---|---|
| `online_solo_<n>` | play mode n alone (not for a duel) |
| `online_lobby_<n>` | host mode n **online, with a code** |
| `online_code` | where a player types a code (click, type, Enter) |
| `online_join_code` | join the game with that code |
| `online_host_<n>` | host mode n on this computer's address (LAN / forwarded port) |
| `online_endpoint`, `online_join` | the host's `address:port`, and join it |
| `message` (a label) | what is happening, and why something did not work |

Session controls have `online_back`, `online_restart` and `online_return`.

## Lobby codes

A host presses **Online** on a mode and, as soon as it has a code -- six
letters and digits, e.g. `K7Q2X9` -- goes into the mode's world. The code
stays on the HUD (`Co-op Expedition / Host / 1 players / code K7Q2X9`) and in
the F10 panel. Friends click the code field, type it (case, spaces and dashes
do not matter; there is no 0/O or 1/I to mix up), and press Enter or **Join
with code**. Everything else -- the scene, the fingerprint check, the
session -- is as with an address.

A code works two ways; the project chooses by naming a lobby server or not.

### Without a lobby server (the default)

Nothing to set up and nothing to pay for. The two games find each other and
then talk directly (`engine/core/network/include/lobby_direct.h`):

1. **Address.** Each game asks a public STUN server (Google's, Cloudflare's)
   how the internet sees its game socket: its router's public address and the
   port the router gave that socket.
2. **Finding.** The host puts that address in the **BitTorrent DHT** -- the
   public, server-less address book every torrent program shares -- under a
   key made from the `game_id` and the code (only an address goes there, never
   a file). A joining game looks the key up, and puts its own address under the
   code's "join" key, which the host keeps asking for.
   On the **same network** (or the same PC) a broadcast finds the host at once
   (UDP port 7778), without any of that.
3. **Joining.** Both games send packets to each other's public address at the
   same time (UDP hole punching), so each router sees the other game's packets
   as answers to its own game's and lets them in; then the joiner's game
   connects from that same socket.

This gets through most home routers -- those that keep a socket's public port
the same whatever it sends to (most do). It cannot connect two networks that
change the port for every destination ("symmetric" NAT: some mobile networks
and carrier-grade NAT); the host is told when its own router is one of those.
A lobby server (below) gets through them.

Some DHT nodes answer every lookup with addresses of their own choosing. An
address is used only when nodes on two different networks report it, and at
most eight are tried, a few packets each.

**Privacy.** The host's public address is in the DHT, under a key only the
code leads to, for as long as the game is open (anyone who can join the game
learns it anyway, as with any direct connection). `lobby_server` keeps
addresses off the DHT.

**Firewall.** Windows asks the first time a game hosts whether to let it
through; on a private network say yes, or friends on the same network cannot
find the game (the internet path still works).

**Two games on one router.** They find each other by the broadcast. A router
seldom sends a packet addressed to its own public address back inside, so the
internet path alone does not connect two players behind the same router --
the player is told to check the firewall or join by the host's local address.

### Through a lobby server

`lobby_server=host:port` in the config makes every code go through that
server, which relays the games' packets (like a TURN relay): every connection
goes *out* from a home network to the server, so it works behind any router,
mobile hotspots and carrier-grade NAT included, and adds the trip to the
server to everyone's ping. Choose a server near the players.

1. The host's game registers the lobby from its own game socket and says it
   is still there every few seconds.
2. A joining player's code is turned into a **relay port** on the server.
3. The server tells the host a player is coming; the host's game sends one
   small packet to that relay port, which opens the host's router for it.
4. The player's game connects to the relay port; the server forwards the two
   games' packets to each other as they are.

If the server does not answer, a host opens its game without it (the way
above) and a joiner looks for the code without it too -- a server that is
down does not stop anyone playing.

### What a player is told

*No game with that code* (mistyped, or the host has left), *That game is
full*, *That code is for another game* (a different `game_id`: another game,
or another version of it), *Found the game but could not reach it* (two
networks that block direct connections: a lobby server is needed),
*...on this same internet connection* (both behind one router: the firewall
on the host's computer). Looking for a code takes up to 30 seconds before it
says so.

A game with a code is open until the host leaves; anyone who has the code can
join until the mode's `players` are in -- the code is the only key, so share
it like a party invite.

## Running the lobby server

`lobby_server` ships with the engine (beside the editor: `lobby_server.exe`
on Windows, `lobby_server` in the Linux package). It needs a computer the
internet can reach, on these UDP ports:

| Port | |
|---|---|
| 7780 | control: codes, joining, keep-alive |
| 7781-7899 | relay: one per player joined to a host while they play |

```
lobby_server [--port 7780] [--relay-ports 7781-7899] [--quiet]
```

It prints a line per lobby and relay and a summary every minute; Ctrl+C or
SIGTERM stops it. It keeps nothing on disk: restarting it closes the open
lobbies (hosts get a new code automatically).

**On a cloud machine (Linux).** Any small VM with a public IPv4 address does
(a free tier is enough: relaying a few games is little CPU and bandwidth):

1. Copy `lobby_server` from the engine's Linux package (`engine-v<version>-linux-x86_64.tar.gz`)
   to the machine, e.g. `/opt/gws/lobby_server`, and `chmod +x` it.
2. Open UDP 7780-7899 in the provider's firewall / security group, and on
   the machine: `sudo ufw allow 7780:7899/udp`.
3. Keep it running -- `/etc/systemd/system/gws-lobby.service`:

   ```
   [Unit]
   Description=GameWorldshaper lobby server
   After=network-online.target

   [Service]
   ExecStart=/opt/gws/lobby_server --port 7780 --relay-ports 7781-7899
   Restart=always
   DynamicUser=yes

   [Install]
   WantedBy=multi-user.target
   ```

   then `sudo systemctl enable --now gws-lobby` (its log: `journalctl -u gws-lobby -f`).
4. Put the machine's address in the game: `lobby_server=<address>:7780` in
   `config/multiplayer.ini` (an exported game reads it from its own copy, so
   export again after changing it).

**On a Windows PC** (a friend's always-on PC, or for tests): run
`lobby_server.exe` from the engine folder and forward UDP 7780-7899 on that
PC's router to it. With `lobby_server=127.0.0.1:7780` two copies of the game
on the same PC play through it, which is how the TestGame is set up out of
the box.

## Progress kept between modes

What a player earns stays with them from one mode to the next -- a co-op
expedition, then the raid, then the expedition again: their level, XP and
skill points, the skills they unlocked, the abilities in their slots, their bag,
what they wear and the guns they carry. It is stored on their own computer
(`saves/profile.sav` in the project; an exported game: the player's own folder,
`<config>/games/<project>/saves/`), saved every 15 seconds and whenever they
leave a mode (the start screen, another mode, closing the game).

Each mode starts from the player as its scene makes them; the profile is then
rebuilt on top -- the levels' growth, each skill's bonus, the gear's modifiers --
so a change to the game's balance reaches saved players, and every mode starts
at full health. A skill the game no longer has is dropped.

Online, a guest sends their profile when they join; the host gives the guest
their own progress (not the host's), and the guest keeps what they earn there.
A duel (`rounds_to_win`) uses its own rules (health, skill points) and leaves
the profile alone. `keep_progress=0` in `config/multiplayer.ini` turns it off.

## How it plays

- **Host** starts mode n -- with a code, or on a UDP port (7777 unless the
  address field says otherwise) -- and accepts guests; **Join** connects to a
  code's relay port or to `host:port`. A joining player loads the host's scene
  after its `fingerprint` files are checked.
- The host runs the game: damage, ammunition, reloads, cooldowns, skills,
  enemy AI, native simulation scripts and duel scores. Guests send their weapon,
  ability, item and skill actions and receive the results; each moves its own
  player (movement is the client's -- casual hosted play, not anti-cheat).
- What is sent: gameplay attributes, inventories, progression, combat state,
  actors, spawned encounter visuals, animation clips and procedural bone poses,
  and the UI listed in `sync_ui`; unchanged data is left out between full
  snapshots, and a late joiner gets a full one. Other players are drawn with
  the authored character model and get matching hurtboxes.
- Animation goes by its parameters, not its poses: the host sends what each
  NPC looks at, whether it aims and when it flinches (a guest runs no AI), and
  the montages and graph parameters its scripts set; each machine's own
  animation graphs pose the characters from that (and from the movement and
  weapon state it already gets). A host older than 0.8.62 sends none of it and
  a guest older than 0.8.62 ignores it.
- Co-op has no friendly fire. In a duel the two sides are `team.duelist.host`
  and `team.duelist.guest`. Leaving a hosted session ends it for everyone (no
  host migration). Saving and loading are off during a session, and menus do
  not pause the shared game.
- **By address:** LAN -- join the host's LAN address. Internet -- the host's
  public address with its UDP port reachable (router/firewall), or a shared VPN.

## Switches (tests and diagnostics)

`GWS_ONLINE_DISABLE=1` ignores the configuration (plays solo as before);
`GWS_ONLINE_DIAGNOSTICS=1` logs each side's state once a second;
`GWS_LOBBY_SERVER=<host:port>` replaces `lobby_server`;
`GWS_LOBBY_LAN=0` / `GWS_LOBBY_INTERNET=0` turn off one of the two ways a code
is found without a server (the broadcast; STUN and the DHT);
`GWS_ONLINE_LOBBY=<mode key>` hosts that mode with a code and
`GWS_ONLINE_CODE=<code>` joins one (as the buttons do). The UI test hook
`GWS_TEST_UI` can click, type and press keys:
`click:online_code@60,type:k7q-2x9@70,key:Enter@80`.
`tools/lobby_check` runs a lobby server in-process and plays real game
traffic through its relay, and a host and players found by the broadcast;
`lobby_check --internet` also tries public STUN servers and the DHT (it needs
the internet, so it is not in the suite).
