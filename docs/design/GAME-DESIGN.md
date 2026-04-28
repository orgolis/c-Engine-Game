# Game Design Document — Worldshaper

## High Concept

**Worldshaper** is a 3D open-world action RPG MMO looter shooter combining the movement intensity of Wuthering Waves, the build complexity of Destiny 2, the combat depth of Elden Ring, and the world density of Cyberpunk 2077.

**Core Loop:** Explore Throne Worlds → defeat enemies → collect loot → build custom ability loadouts → tackle harder content.

**Target Audience:** Core gamers who value mechanical skill, build experimentation, and seamless multiplayer cooperation.

---

## World & Lore

### The Lore: Orgolis

The game centers on **Orgolis**, a powerful entity with reality-bending abilities. Orgolis possesses:

- **Ethereal Form** — Phase between dimensions, become invulnerable
- **Reality Bending Illusions** — Manipulate perception, confuse enemies
- **Trans-Dimensional Travel** — Move between Throne Worlds
- **Precognition & Mind Reading** — Predict enemy actions, perceive hidden threats
- **Abomination Power** — Suppress and annul nearby magic (suppresses other players' abilities)

The world is inhabited by:
- **The Titans** — Ancient world-shapers
- **The Gods** — Divine entities with delegated power
- **Descendants** — Mortals with fragments of divine power (players)
- **Security Forces** — Advanced enemies guarding Throne Worlds

### Setting: Throne Worlds

The game world consists of discrete **Throne Worlds** — dimensional domains with different physical laws:

- Different gravity (normal, low-g, inverted)
- Different lighting (bioluminescent, cosmic, industrial)
- Different physics rules (water-based levels, void levels, crystalline levels)
- Seamless streaming with no loading screens between regions

**Examples:**
- Throne World of Orgolis (reality-warped, illusions prevalent)
- Cosmic Throne (space-themed, low gravity spaces)
- Crystalline Throne (geometry-based navigation)
- Industrial Throne (futuristic security systems)

---

## Player Abilities & Build System

### Class Structure

Each player chooses a **Primary Class** that defines core abilities:

**Orgolis's Abilities:**
- **Arcanum** — Mystical magic, crowd control
- **Runic** — Rune-based buffs and debuffs
- **Summoning Magic** — Pet spawning, minion control
- **Eldritch** — Cosmic horror-themed abilities
- **Necromancy** — Life drain, death magic
- **Elemental** — Fire, ice, lightning, restoration
- **Trans-Dimensional** — Orgolis signature (phase, warp, illusion)

### Ability Loadout (Destiny 2 Model)

**Loadout Structure:**
- **3 Ability Slots** — Pick 3 of your class abilities
- **1 Ultimate** — Powerful, limited-use ability (120s+ cooldown)
- **Aspects** — Inherent class modifiers
- **Fragments** — Flat bonuses that stack (like Destiny 2 mods)

**Example Loadout:**
```
Slot 1: Ethereal Form (4s duration, 8s cooldown)
  + Fragment: "Eyes of Orgolis" (+2s duration, see through walls)
  + Fragment: "+10 recovery"
  
Slot 2: Reality Bending Illusion (confuse target for 3s)
  + Fragment: "+2 ability charges"
  
Slot 3: Trans-Dimensional Warp (dash + phase)
  + Fragment: "+20% movement speed for 2s after cast"
  
Ultimate: Abomination (suppress all nearby enemy abilities for 8s)
  
Aspects: Reality Anchor (enemies near you move 15% slower)
```

### Gear Modifiers

Every equipped item modifies:
- Ability durations, cooldowns, charges
- Damage, range, AoE
- Defensive stats (health, armor, resistances)
- Movement speed, attack speed

**Exotic Weapons/Armor** have unique rules (e.g., "Eyes of Orgolis" — grants Truesight, see through walls for duration).

---

## Combat System

### Frame-Data Driven (Elden Ring Model)

Every attack (player ability or enemy move) is defined as:

```
Attack {
    name: "Sword Slash",
    startup_frames: 8,       // Windup
    active_frames: 12,       // Hitbox active
    recovery_frames: 10,     // Can't act
    
    damage: 25,
    poise_damage: 10,
    range: 3.0,
    
    hitstun_duration: 0.3s,  // Target is stunned
    knockback: 5.0
}
```

### Poise & Parry System

- Every character has a **Poise Pool** (120 default)
- Taking hits reduces poise
- When poise reaches 0: enter **stagger** state (5 frames)
- During stagger: take 30% more damage, can be grabbed
- Parrying an attack (exact frame timing) fully negates damage

**Why this matters:** Poise management is a core skill. Aggressive play trades poise for positioning.

### Hitstun & Recovery

- On hit: target enters **hitstun** (0.3–1.0s depending on attack)
- During hitstun: movement input is reduced, abilities are slower
- However: attack combos are always possible (you can attack during hitstun)

---

## Movement System (Wuthering Waves Intensity)

### Movement States

- **Idle, Walk, Sprint** — Basic locomotion
- **Jump** — Double jump capable
- **Air Dash** — Mid-air momentum shift
- **Wall Run** — Run vertically on walls (exit via jump or climb)
- **Climb** — Climb walls and ledges
- **Swim** — Water traversal with diving
- **Dive** — Underwater movement + air dives
- **Crouch** — Stealth stance (reduced hitbox)
- **Slide** — Speed boost, dodges incoming fire
- **Falling** — Free-fall with air control
- **Recovery** — Animation recovery windows (input buffering applies here)

### Input Buffer

If you press Jump while your character is recovering from an ability, the jump queues and executes as soon as recovery frames end. This creates a **input queue** that feels responsive rather than sluggish.

### Animation Driven Movement

Character position is driven by **animation root motion**, not just physics:

- Jump animation pulls character up at frame-perfect timing
- Dash animation applies momentum in the direction you're facing
- This is why movement feels weighty and precise

**Why this is hard:** Movement state, animation state, physics state, and networking state must all stay in sync.

---

## AI & Encounters

### Enemy Types

**Basic:**
- Patrol → Detect → Engage → Retreat/Regroup

**Advanced:**
- Squad tactics (focus fire, flanking, suppression)
- Coordination (wait for allies, synchronized attacks)
- Evasion patterns (strafe, backpedal, dive)

**Boss**
- Multi-phase encounters
- Phase-specific movesets
- Telegraphed attacks (windup visual)
- Calculated difficulty spikes

### Boss Structure (Orgolis Example)

```
Phase 1 (100–66% HP):
  Movepool: Ethereal Form, Reality Bending, basic attacks
  Behavior: Normal aggression
  
Phase 2 (66–33% HP):
  Movepool: + Summons minions
  Behavior: More aggressive, phase transition has cinematic
  
Phase 3 (33–0% HP):
  Movepool: + Ability speed doubled, + Rampage attacks
  Behavior: Desperate aggression, no retreating
```

Each phase has its own **behavior tree** controlling second-by-second decisions.

---

## Multiplayer Structure

### Squad Size

Primary content supports **1–4 players** (solo + coop).

Larger raids (6–12 players) are planned as post-launch dungeons.

Dungeons (1-6 players) scale difficulty based on player count.

### Authority Model

**Client-Predicted:**
- Your own movement
- Your own ability activation

**Server-Authoritative:**
- Hit confirmation (did your attack actually land?)
- Ability effects on other players
- Other players' positions (with client-side extrapolation)
- World state changes

**Special Case: Orgolis's Abomination**
- Suppresses nearby player abilities for 8s
- Must be server-confirmed (affects all players in the world)
- Cannot be client-predicted

### Matchmaking

- **Fireteam** — You pick your squad (friends, clan, etc.)
- **Matchmaking** — Recommended for harder content; pairs you with skill-matched players
- **Raid Finder** — Large group matching for endgame

---

## Progression & Loot

### Power Progression

1. **Gear Score** — Higher gear score = higher damage, survivability
2. **Ability Unlocks** — Unlock new abilities by exploring/defeating bosses
3. **Fragment/Aspect Unlocks** — Drops from bosses, grants new build options
4. **Exotic Drops** — Rare, ability-changing gear pieces

### Loot Categories

- **Common (White)** — +1 stat, boring
- **Uncommon (Green)** — +5 stats
- **Rare (Blue)** — +10 stats
- **Exotic (Yellow)** — Unique perk (e.g., Eyes of Orgolis)
- **Legendary (Red)** — Multiple unique perks

### Secrets & World State

The game world is filled with **secrets:**
- Hidden ability upgrades
- Lore documents (world-building)
- NPC questlines outside the main story
- Secret cosmetics and weapons

**Finding a secret sets a persistent world flag** → other systems react (NPCs acknowledge it, quests change, new areas open).

---

## Adventure & Narrative

### Story Hooks

- Main story: Unite against the Titans and Gods
- Sidequests: NPC questlines with branching outcomes
- Environmental storytelling: Lore scattered across Throne Worlds
- Endgame: Defeat Orgolis and reclaim reality

### NPC Social Features

- NPC schedules (alive world, not static)
- Reputation system (friendly, neutral, hostile)
- Dialogue trees with consequence
- Faction alignment (affects access to faction equipment)

---

## Inspirations: What We're Learning

| Inspiration | What We're Taking | Specific Games Referenced |
|-------------|-------------------|--------------------------|
| **Wuthering Waves** | Fluid movement, state machine design, animation weight | Movement precision, aerial combat |
| **Destiny 2** | Mod/fragment system, ability loadouts, matchmaking | Build depth, endgame replayability |
| **Elden Ring** | Frame-data design, poise system, boss phases | Combat feel, boss difficulty scaling |
| **Cyberpunk 2077** | World density, NPC schedules, seamless streaming | Open-world immersion, no loading screens |

---

## Success Criteria

- [ ] Movement feels as responsive as Wuthering Waves
- [ ] Ability combinations feel as deep as Destiny 2
- [ ] Boss fights are as technically challenging as Elden Ring
- [ ] World density rivals Cyberpunk 2077
- [ ] Multiplayer cooperation is seamless (no latency feel)
- [ ] Endgame is infinitely replayable (gear chase + build experimentation)

---

## Technical Constraints (For Engine Team)

### Hard Requirements

1. **Deterministic 32-bit floats** — Rollback netcode requires exact replay
2. **Frame-accurate hitbox timing** — Tied to animation frame data
3. **World streaming at scale** — 100+ NPCs with AI simultaneously
4. **Ability modifier aggregation** — Every stat is computed from modifier sources
5. **VFX pipeline first-class** — Reality Bending, Ethereal Form require rich particles

### Nice-to-Haves (Post-Launch)

- Motion Matching for procedural locomotion
- GPU-driven particle systems
- Ray-traced reflections
- Neural network upscaling (DLSS-style)

---

## Post-Launch Roadmap

### Season 1 (Launch + 3 months)
- Tutorial & onboarding
- Tier 1 endgame (difficulty scaling)
- 3 Throne Worlds with full quest chains

### Season 2 (3–6 months post-launch)
- 2 new Throne Worlds
- Raid raid (8–12 player endgame)
- 3 new exotics

### Expansion 1 (6–12 months)
- New class (alternative to Orgolis)
- PvP arena
- Clan system with perks

---

## Notes

- **Combat feel > perfect balance.** Player feedback on frame timings is critical.
- **Build diversity drives retention.** 100+ viable ability combinations required before launch.
- **World secrets drive discovery.** 50+ findable secrets minimum in first Throne World.
- **Multiplayer is not bolted-on.** Everything is designed with coop in mind.
