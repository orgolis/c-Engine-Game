Creating a own 3D Open World Modern RPG MMO Looter Shooter, inspired by Games like Destiny 2, Cyberpunk, Guild Wars 2, Elden Ring. Via Own Game Engine

# Game: Project Schizo

**Genre:** Open-World Multiplayer Action RPG  
**Inspirations:** Wuthering Waves (Movement/Combat), Destiny 2 (Abilities/Builds), Elden Ring (Combat Depth/Boss Design), Cyberpunk 2077 (World Density/Narrative)

---

## Executive Summary

Project Schizo is an ambitious open-world multiplayer action RPG that combines the best elements of modern action games. Players take on the role of a Sigil-Bearer—inheritors of ancient power connected to the 25 Titans who shaped reality itself. The game focuses on Orgolis, the 13th Titan of Wisdom, who was shunned by his father and siblings for possessing knowledge beyond their understanding.

---

## Table of Contents
1. [Design Pillars](#design-pillars)
2. [Lore & World](#lore--world)
3. [Gameplay Systems](#gameplay-systems)
4. [Combat & Abilities](#combat--abilities)
5. [Character Progression](#character-progression)
6. [World & Exploration](#world--exploration)
7. [Items & Equipment](#items--equipment)
8. [Multiplayer & PvP](#multiplayer--pvp)
9. [End-Game Content](#end-game-content)
10. [Vehicles](#vehicles)
11. [Narrative & Themes](#narrative--themes)

---

# Design Pillars

## 1. Fluid, High-Speed Movement (Wuthering Waves)
- **Air dashes** with momentum conservation
- **Wall-running** and climbing with parkour chains
- **Grapple mechanics** for rapid repositioning
- **Combat-movement integration** - attacks don't cancel momentum
- **Animation-driven movement** with root motion for weight and impact

## 2. Deep Build Complexity (Destiny 2)
- **Modifier Pipeline System** - gear, perks, buffs feed into unified stat calculations
- **Data-driven abilities** defined in files, not hardcoded
- **Synergy-focused builds** where items enhance specific playstyles
- **Exotic-tier effects** that fundamentally alter gameplay

## 3. Frame-Data Combat (Elden Ring)
- **Startup, Active, Recovery frames** for every attack
- **Parry/Guard/Dodge windows** with precise timing
- **Poise system** for hyper-armor and stance-breaking
- **Hitbox system** decoupled from render mesh
- **Boss phase transitions** with moveset changes

## 4. Living, Dense World (Cyberpunk 2077)
- **Seamless streaming** of high-density environments
- **NPC schedules** and emergent encounters
- **World state flags** that persist across sessions
- **Choices with long-term consequences** visible in the world
- **Seamless interior spaces**

## 5. Multiplayer-First Architecture
- **Client-side prediction** with server reconciliation
- **Rollback netcode** for combat and hit detection
- **Server-authoritative state** for anti-cheat
- **Cooperative gameplay** designed into core systems
- **Competitive modes** balanced separately from PvE

---

# Lore & World

## Origin Story: The First and the Titans

Aeons ago in an Age long forgotten, there was a man, the same as you and I, that grasped for True Might and seized it, only for himself. He should be known as **the First**. 25 descendants were to follow, all inheriting the same force as their father.

They should be known as **the Titans** and together they built the Core Celestial Forces. Their descendants are all known as **Gods**.

### The Sigil Binding Ritual

When the Titans were born, they were each assigned a sigil. On the day of reaching adulthood, they bound their sigil to their life force and gave it purpose. They also forced space to their will, creating their Domain—shaped by their desire. These are known as **Throne Worlds**.

The Gods went through the same procedure, but their sigils are of lesser might and their domains don't even come close to the dimensions of their predecessors.

---

## The 25 Titans (The Pantheon)

### 1. Qertrahob - Titan of Time
**Domain:** Temporal manipulation, causality, the flow of ages

### 2. Orhylunox - Titan of Space
**Domain:** Dimensional travel, spatial distortion, geometry of reality

### 3. Ixhmatakata - Titan of Existence
**Domain:** Being itself, the boundary between void and form

### 4. Belaphim - Titan of Waves
**Domain:** Oscillation, vibration, the rhythm of reality

### 5. Dimonus - Titan of Force
**Domain:** Motion, impact, the fundamental push and pull

### 6. Mobqu - Titan of Polarization
**Domain:** Duality, opposition, the tension between extremes

### 7. Wopdn - Titan of Charge
**Domain:** Energy transfer, potential, the spark of change

### 8. Martrontis - Titan of Mass
**Domain:** Matter, substance, the weight of reality

### 9. Casitwen - Titan of Gravity
**Domain:** Attraction, orbit, the bending of space

### 10. Denatre - Titan of Life
**Domain:** Growth, vitality, the animate force

### 11. Heme - Titan of Death
**Domain:** Entropy, decay, the return to stillness

### 12. Palea - Titan of Love
**Domain:** Connection, bond, the force that draws together

### 13. Orgolis - Titan of Wisdom (The Shunned One)
**Domain:** Knowledge, memory, understanding

### 14. Tagobir - Titan of Arcanum
**Domain:** Magic itself, the hidden laws

### 15-25. [To Be Revealed]
The remaining 11 Titans are shrouded in mystery, their names lost to time or deliberately hidden.

---

## Orgolis: The Titan of Knowledge

### Background

Before Orgolis became a titan at the age of adulthood, his father, **the First**, was disappointed in him, believing he had been born without any powers. But when his 13th son was assigned a sigil and shaped his own throne world, it terrified him. Orgolis possessed a power the First didn't understand—he was the only one who had a power their father didn't have.

It scared him, so **the First shunned his own flesh**, and his children followed him blindly.

### Powers & Abilities

**Titanic Powers:**
- Curses / Blessings
- Throne World manifestation
- Trans-dimensional Travel
- Shapeshifting
- Ethereal Form

**Powers of the Titan of Knowledge:**
- Reality-bending illusion projections
- Telepathy
- Mind Reading
- Photographic Memory
- Memory Sharing
- Memory Erasure / Denial
- Precognition
- Revealing the Past
- Revealing the Truth
- Transfer of Memory / Information

**The Abomination (Dark Power):**
- Consume the very life force
- Suppress, annul, and deny magic or godly powers nearby

**Mage Powers:**
- Arcanum
- Runik
- Summoning Magic
- Eldritch
- Enchanting
- Necromancy
- Elemental Magic
- Skothavma (Shadow manipulation)
- Agioglossa (Divine language)
- Solorig (Soul magic)

### Character Voice Lines

> "How do they say that they know me, if even I don't know myself?"

> "When you know enough, even magic becomes science."

> "I have my reasons to veil it in mysteries."

> "When I was little, I asked my father if he could show me the cosmos, but even I have not seen everything yet."

> "They made me like this, they killed my feelings, until the only thing I could feel was pain and emptiness."

> "Understanding the World means to Accept suffering."

> "Embrace the pain and wield it as a tool."

> "When I tried to leave from this cruelty, they forced me back into existence."

> "My Power is the burden of mine, I cannot wield."

> "Even with this pain I do not drown myself in sorrow, I know better."


---

## Historical Records & Ancient Texts

### Verse (16,438 BC)

A child was born on the stillest night the world has seen, the 13th son of Deuteras, he shall henceforth be known as **The Orgolis**. As the middle eldest child of Deuteras, he has often been forgotten and neglected. His gift was his unchanging will and keen spirit. Like all the children of Deuteras, he was blessed by the gods who fell for the people. Yet he was unlike his siblings, with no talent in combat. He had been gifted with the power to **bend the cosmos**.

### Tome of Depths, Chapter 147, Verses 93-98

In the deepest darks of the depths in "Tarlahem", a young frightened boy of unordinary potential came to him. He asked into the pit, who he was and the reply he received shall have been:

*"A Shadow of time, a knight of the past, a prophet of the future, the Preceptor of logic and space, king of mind, god of might, the titan of knowledge. I am The Orgolis, I speak to you, adjoin me into the knowledge I shall reveal. Son of the spiritual and scholars, and I shall grant you thy desire."*

*"Oh being of might, answer me, What are you?"* the youngling asked with fright and respect in his tiny voice.

The Orgolis revealed: *"I am no living nor Undead, bound to this plane with no escape, I have been in ancient times the same as you. Let me give you a gift of mine."*

*"But what is the price of such a gift of your Unimaginable Power?"* the bellows inquired.

*"In the name of my Will, it shall have no fee,"* the Titan of Knowledge responded.

This so-called Gift was a mark on the left forearm of the boy with ancient Sigils and the Sigil of Knowledge in the Center. From this point on, he became the first of the soothsayers.

### The 13 Toniet, Chapter 3

In this war, they called him **the last soldier**, for he was the last one standing between the infernos and the mountains of lifeless.

### The 5th Grimoire, Chapter 27, Verses 72-76

According to ancient lore, the ancient sigil of knowledge is said to be directly connected to the titan of knowledge, also known by the Title Orgolis. Despite this, in combination with summoning sigils, **it is not possible to summon him in any form or to harness his power**.

The only known use of this sigil is to:
1. Reveal the unknown or past knowledge of an environment or object
2. Manipulate or transport knowledge

---

## Songs & Poetry of the World

### An Ancient Folk Song

*Be brave when you're alone*  
*Be cautious when it's dark*  
*Hope that Orgolis may grant his guidance*

*In the shadows, we roam*  
*Underneath the silver moon's arc*  
*With faith in our hearts, we'll find our way*  
*Through the night, we embark*

*Guided by the stars above*  
*In this world so vast and stark*  
*Orgolis, we trust your guidance is true*  
*Through the darkness, we leave our mark*

*As we journey through the night,*  
*Our spirits remain strong and stark,*  
*With Orgolis as our guiding twilight,*  
*We'll overcome any peril or darkness.*

### Theme Song: The Fallen Titan

*In the annals of time, a tale unfolds,*  
*A forgotten child, a story seldom told,*  
*Guiding us through life's intricate code,*  
*With cosmic fire, their wisdom we hold.*

**[Chorus]**  
*Oh, veiled mysteries, in cosmic seas they hide,*  
*In shadows deep, their truths abide,*  
*In the darkest nights, their presence as a guide,*  
*The depth of their knowledge, our hearts confide.*

*Bound to a plane, no chance to flee,*  
*A titan of wisdom, the past and what's to be,*  
*Generously sharing gifts, with no fee,*  
*An ancient Sigil, lighting our destiny.*

**[Chorus]**  
*Oh, profound secrets, like ancient scripture's scroll,*  
*In cosmic seas, their wisdom unfolds,*  
*Through the shadows deep, their essence takes its toll,*  
*In the darkest nights, their insight consoles.*

*A Sigil of wisdom, from ages long past,*  
*Inscribed with knowledge, enduring and steadfast,*  
*Beyond the grasp of any summoner's cast,*  
*Revealing the enigma of history's vast.*

*Let this song resonate, in their name, we embark,*  
*Through the echoes of time, in the knowledge's spark,*  
*In their profound light, we'll find our arc.*


### Orgolis: A Tale of Power and Isolation (Epic Ballad)

*He used to roam across the World,*  
*Crowds would rise when he gave his Word,*  
*Now in the morning he rises alone,*  
*Hiding in the streets, that once were his own.*

*He used to cast the dice of fate,*  
*Fear were in the eyes of ye who'd hate,*  
*Echoes of the crowds would ring,*  
*The old gods have fallen, long live the new kings.*

*One minute, I held the key,*  
*Next, the walls closed in on me,*  
*I found his castles built so grand,*  
*On pillars of salt, and pillars of sand.*

*I hear Jerusalem bells a-ringing',*  
*Roman Cavalry choirs singing',*  
*Be his tome, his sword, his shield,*  
*His missionaries in a foreign field.*

*For some reason, even he can't explain,*  
*Once she'd gone, there was never, never an honest word,*  
*That was when he roamed the world.*

*In an age of myths and lore untold,*  
*A titan born, with power to behold,*  
*The thirteenth son, unseen,*  
*With clueless might and with spirit keen.*

*On a still and silent night,*  
*A child of stars, with no visible might,*  
*Bending the cosmos to his bidding,*  
*His throne world shaped, his sigil been sealed.*

*How can they claim to know his name,*  
*When even he can't bear the same,*  
*Mysteries cloak my deepest thoughts,*  
*In the shadows cast, his power's wrought.*

*Shunned and feared, they turned away,*  
*Yet wisdom's light within him stayed,*  
*They made him cold, they made him strong,*  
*In pain and dark, where they belong.*

*One minute, he held the key,*  
*Next, the walls closed in on me,*  
*I found his temple built so grand,*  
*Upon pillars of salt, and pillars of sand.*

*Eons past, in shadows cast,*  
*Orgolis's might, from first to last,*  
*Father's fear, a son's true power,*  
*Cast aside in his darkest hour.*

*"They made him this, in pain confined,*  
*A heart of stone, a sharpened mind,*  
*Seeking knowledge, though shunned and spurned,*  
*In solitude, his wisdom burned."*

*But for a minute, He held the key,*  
*Next, the walls are closed on ye,*  
*I found his temple ruined so grand,*  
*On pillars of salt, and pillars of sand.*

*For the reason, that even he can't explain,*  
*Once she'd gone, there was never, never an honest word again,*  
*That was when he abandoned the world.*

*In the shadows, he still roams,*  
*Guided by the stars and stone,*  
*His wisdom, a guiding flame,*  
*Through the darkest nights, they call his name.*

*In the daylight, we fear his might,*  
*With his wisdom, we gain our sight,*  
*Though shunned and scorned, his rein endures,*  
*In knowledge deep, we find our cures.*

*Oh, veiled mysteries, in those cosmic seas.*


### Narrative Poetry: Theme of Struggle

*Thought we found a way*  
*Thought we found a way out (found)*  
*But it never go away (never go away)*  
*So I guess we gotta face now*

*Oh, I hope some day I'll make it out of here*  
*Even if it takes a night or a hundred years*  
*Need a place to pray, but I can't find one near*  
*Wanna feel alive, outside I can't fight my fear*

*It isn't lovely, all along.*  
*Hearts made of glass and these minds of stone*  
*Tearing us to pieces, deep into bone*  
*Hello, peace is gone*

*Walkin' out of time*  
*Lookin' for the better place (lookin' for a better place)*  
*Something's on my mind (mind)*  
*Always in my head space*

*But I know some day we'll make it out of here*  
*Even if it takes all night or a hundred years*  
*Need a place to pray, but I can't find one near*  
*Wanna feel alive, outside I can't fight my fear*

*It isn't lovely, all along.*  
*Hearts made of glass and these minds of stone*  
*Tearing us to pieces, deep into bone*  
*Hello, peace is gone*

*Whoa, yeah*  
*Yeah, ah*  
*Whoa, whoa*  
*Hello, welcome home*

### Epic Poetry: The Warrior's Path (The Exile's Journey)

*In a realm cloaked in shadow, a bell's sombre toll,*  
*He heeded its call, embraced a role, dark and bold,*  
*Yet wearied, his heart sighed, burdened with pain untold,*  
*A gem of the month, he had been born, a tale yet to unfold.*

*In twilight's tender grasp, he wandered alone,*  
*Where strength waned, solace for his heart's heavy stone,*  
*Humbled sword, his silent groan,*  
*Words of false vows, bound never to leave his home.*

*A solitary man's cry, passion rare reflection, a connection profound,*  
*Compelled him to unlock the door, where exile's roots could be found,*  
*A man, a harbinger of bloodshed, war's tempest unbound,*  
*Stood before him, the weight of fate on the battleground.*

**[Refrain: Woe to Power]**  
*Woe to the ones with power, he sighed to the whispering breeze,*  
*May their homes be consumed by fiery seas,*  
*Never to rise from ashes, bound in sorrow's freeze,*  
*Alone, with comrades, seeking solace underneath ancient trees.*

*In a distant realm, he traced the path of exiles' plight,*  
*Malevolent horns, boundless power's fearful might,*  
*In the prisoner's gaze, seething hate took flight,*  
*As a modest sanctuary unveiled a treacherous blight.*

*Within a cavern's depths, they shared a sacred space,*  
*Comrades dwelling in desolation's embrace,*  
*As he gave them his grace to the exile, they bid him farewell with grace,*  
*Leaving him pondering his path in the endless cosmic chase.*

*"Violence for reason, freedom's grace," he mused,*  
*Breaking free from the oppressor's hold, he refused,*  
*Believing in justice, embracing the law, he'd used,*  
*To combat despots, ending suffering, he'd enthused.*

**[Refrain: Woe to Power]**  
*Woe to the ones with power he whispered to the night's silent cries,*  
*May their homes be consumed by fiery skies,*  
*Never to rise from ashes, where sorrow forever lies,*  
*Alone, with comrades, beneath starlit celestial ties.*

*Preparation, thrift, war's meticulous design,*  
*Veiled truths revealing a treacherous sign,*  
*Unsteady allies, wavering faith's thin line,*  
*Victory attained, but what cost in the grand design?*

*In battle's darkness, blood roared in their ears,*  
*Eyes met in horror, as treachery fueled their fears,*  
*The leader's betrayal, his voice drenched in tears,*  
*He recoiled, aghast, as deception's sting appeared.*

*Corruption and power, desire vile and dire,*  
*He spurned such darkness, embraced a nobler pyre,*  
*To shun those monsters, whether friend or foe, he'd aspire,*  
*The hero's path, his chosen journey, is ever higher.*

**[Refrain: Woe to Power]**  
*Woe to the ones with power, he whispered to distant stars,*  
*May their homes be consumed, as history's fiery memoirs,*  
*Never to rise from ashes, bound by fate's cruel bars,*  
*Woe to those who called him friend, beneath celestial avatars.*

*In solitude, he sought refuge, silence's gentle caress,*  
*Echoes of voices, now spectral, left him in distress,*  
*In isolation's grip, he bore his heavy duress,*  
*Vowing not to seek solace in violence's fiery mess.*

*News arrived, of his lone friend's desperate plea,*  
*"Hide or perish," they warned, in the shroud of mystery,*  
*Bound wings and a path guided by dread's decree,*  
*Promising justice, yet lusting for another's destiny.*

*An armed phalanx approached, cruel intent in their stride,*  
*He placed the axe in the innocent's hands, by their side,*  
*"Return to your bow," echoed the command, a dire guide,*  
*As the king's fall unfolded, destiny's currents would not subside.*

*Upon the anvil, they placed him, cruel intentions unveiled,*  
*To break his spirit, in night's dark abysses, he'd be jailed,*  
*Yet he endured, escaping the webs that treachery had sailed,*  
*Strength clenched in the butcher's teeth, a burden to be hailed.*

**[Refrain: Woe to Power]**  
*Woe to the ones with power, he whispered to the moon's pale glow,*  
*May their homes be forever engulfed, in fires that grow,*  
*Never to rise from ashes, in eternal ebb and flow,*  
*Woe to those who called him friend, in life's endless throes.*

*Returning from war, a shattered soul, heavy and weary,*  
*He encountered an orphaned boy, burdened and teary,*  
*The lad's story, pain unveiled, spirits dreary,*  
*Together, they'd unite against common foes, comrades, and leery.*

*In familiar lands, they ventured, hidden from view,*  
*Gathering weapons, laying plans, comrades anew,*  
*Yet the lad's words sparked a different avenue,*  
*Abandoning their mission, he'd now journey solo, destiny askew.*

*Once comrades, now estranged, bitterness' bitter brew,*  
*Explosions rained from above, skies painted with fiery hues,*  
*"Goodbye," he wished, in the arms of an old friend's adieu,*  
*As he embarked on a solitary path, with footsteps few.*

**[Final Refrain]**  
*Woe to the ones with power, he whispered to the winds' gentle breeze,*  
*May their homes forever be in flames, 'neath rustling trees.*

###  Personal Reflection: Orgolis's Inner Voice

I am adrift in the tempest of emotions, lost at sea with no land in sight. My heart cracks with the urge to weep, but there are no tears of sorrow to fall. Instead, a heavy void sits where grief should be, my silent scream echoing inside. Each night, I lie awake, trapped in this spiral of thought—like ghosts of all the paths I failed to walk haunt me, whispering of the mountains I could have climbed, the glittering peaks with heavenly sparks glowing, now forever out of reach.

I am the architect and the ruin of my own dreams, and now all I can offer is the hope that others will learn from my scars, from the echoes of what might have been. I am the hollow, my shadow where a soul once burned bright, untouched by joy or pain, as though this numbness has been carved into me for eternity. How can I still shape these words when I no longer know what it is to feel?

I long to be alive again, to race through golden fields where the wheat brushes against my skin, sending shivers of life through me. I crave the kiss of a cool breeze, the pure sensation of existence, untethered and free from the weight I carry. I yearn to lose myself in that rush, to be swept away by the current of feeling, to drown in the beauty of simply being.

I wish to speak endlessly, to pour out words like rivers, because in those fleeting moments of connection, I touch the spark of life again. But each time, just as quickly, that flame flickers out, leaving me in the dark alone, the moment dead before it could take its first breath.

---


# Gameplay Systems

## Movement & Traversal

### Core Movement Mechanics

**General Movement:**
- **Crouching and Sliding** - Fluid transition maintaining momentum
- **Swimming and Diving** - Full aquatic navigation with oxygen management  
- **Wall-running and Climbing** - Context-sensitive parkour with stamina system
- **Grapple Hook** - Rapid repositioning tool with cooldown
- **Air Dashing** - Mid-air momentum shift (limited charges, recharge on landing)

### Movement State Machine (Technical Design)

The movement system uses a **priority-based state machine** where:
- States can interrupt each other based on priority
- Input buffer queues actions during animation locks
- Root motion drives character position for weighted feel
- Momentum is conserved across state transitions

**Priority Order (Highest to Lowest):**
1. Knockback/Ragdoll
2. Ability Activation
3. Attack (Heavy → Light)
4. Dodge/Parry
5. Grapple
6. Air Dash
7. Wall Run
8. Sprint/Slide
9. Walk/Jog

---

## Inventory & Equipment

### Equipment Slots

**3 Weapon Slots:**
- Primary (Main weapon)
- Secondary (Sidearm/alternate)
- Heavy (Power weapon, limited ammo)

**5 Armor Pieces:**
- Helmet
- Gloves
- Chest Piece
- Leg Piece
- Boots

### Stats System

**Core Stats:**
- **Mobility** - Movement speed, jump height
- **Resilience** - Damage resistance, flinch resistance
- **Recovery** - Health regeneration rate, ability cooldown reduction
- **Discipline** - Grenade cooldown
- **Intellect** - Super/ultimate charge rate
- **Strength** - Melee damage, melee cooldown

**Derived Stats:**
- Total Power Level (average of all equipped gear)
- Elemental Resistances (fire, ice, lightning, void, etc.)
- Status Effect Resistances (bleed, poison, stagger, etc.)

### Trait System

Each piece of gear can roll with **trait modifiers**:
- **Minor Traits:** Small stat bonuses (+5 Mobility, +10% reload speed)
- **Major Traits:** Significant effects (Kills restore health, precision hits increase damage)
- **Exotic Traits:** Build-defining effects (Headshots create explosions, dodging reloads weapons)

### Skill Tree and Ability Selection

- Each class has a unique skill tree with 3 major branches
- Branching choices that define build specialization
- Unlock new abilities and passive bonuses
- Respec available at cost (encourages experimentation)

---

## Relic System (Cyberware-Inspired)

**Design Concept:** Gameplay-altering modifiers similar to Cyberpunk's cyberware system.

Can slot in gameplay-altering modifiers similar to Cyberpunk cyberware. Multiple relics can be equipped.

### Design Questions to Resolve:
- How do relics differ from each other?
- Should relics have tiers?
- Do different relics have different amounts of slots?
- If tiered, should there be passive effects per tier?

### Proposed Relic Tier System

**1. Common Relics (1-2 slots)**
   - Basic stat bonuses
   - Single effect per relic
   
**2. Rare Relics (2-3 slots)**
   - Moderate effects
   - Can combine two minor traits
   
**3. Legendary Relics (3-4 slots)**
   - Powerful effects
   - Multiple traits with synergies
   
**4. Ancient Relics (4-5 slots)**
   - Build-defining effects
   - Unique passive abilities

### Relic Slot System

- Players have a **total Relic Capacity** (e.g., 12 slots at max level)
- Each relic consumes slots based on its power
- Strategic choice: Equip one powerful Ancient relic, or multiple weaker relics for broad bonuses

### Example Relics

**Ancient: "Eye of Orgolis"**
- Slots Required: 5
- Passive: Gain Truesight for 3 seconds on precision kill
- Active (30s cooldown): Reveal all enemies through walls for 10 seconds

**Legendary: "Vortex Core"**
- Slots Required: 3
- Passive: Kills extend air dash duration by 1 second
- Passive: Air dashing through enemies deals damage

**Rare: "Momentum Keeper"**
- Slots Required: 2
- Passive: Sliding increases reload speed by 50% for 3 seconds

---

# Combat & Abilities

## Frame-Data Combat System

### Attack Structure
Every attack has three phases:
1. **Startup Frames** - Wind-up before hitbox activates (vulnerable)
2. **Active Frames** - Hitbox is active and can hit enemies
3. **Recovery Frames** - Follow-through after attack (committed, can't cancel)

### Combat Mechanics
- **Parry Windows** - 3-5 frame window for perfect parry (deflects attack, opens enemy)
- **Guard Frames** - Block reduces damage but doesn't deflect
- **Dodge I-Frames** - 6-10 invincibility frames during dodge roll
- **Poise System** - Each attack deals poise damage; breaking poise staggers enemy
- **Hyper-Armor** - Some attacks grant temporary poise to prevent interruption

### Hitbox System (Technical Implementation)
- **Decoupled from render mesh** - separate collision capsules
- **Per-bone collision** - different parts of weapon have different hitboxes
- **Frame-by-frame activation** - hitboxes only active during Active Frames
- **Multi-hit detection** - prevents hitting same enemy multiple times in one swing

---

## Ability Structure

**3 Main Ability Slots**
Players equip 3 abilities from their class pool, each with independent cooldowns.

**1 Ultimate Ability**
One powerful ability that charges through combat actions:
- Dealing damage
- Taking damage
- Killing enemies
- Picking up energy orbs

---

## Class System

### Tank Class
**Role:** Frontline protector, crowd control, ally shielding

**Standard Abilities:**
- **Barrier** - Deploy a wall that blocks enemy fire (30s cooldown)
- **Cleanse** - Remove all debuffs from nearby allies (45s cooldown)

**Ultimate Ability:**
- **Bubble** - Create an invincible dome for 10 seconds; allies inside are immune to damage

**Aspects:**
- [To Be Designed]

**Fragments:**
- [To Be Designed]

**Playstyle Focus:** Zone control, protecting teammates, initiating engagements

---

### Support Class
**Role:** Healing, buffing, utility, team enhancement

**Standard Abilities:**
- **Bless / Curse** - Buff ally damage OR debuff enemy defense (toggle, 20s cooldown)
- **Ghost Form** - Turn invisible and intangible for 5 seconds (40s cooldown)
- **Portals** - Create linked portals for team repositioning (60s cooldown)
- **Rift** - Create a zone that heals allies over time (25s cooldown)

**Ultimate Ability:**
- **Mobius Quiver** - Fire a volley of seeking arrows that track enemies and split on hit

**Aspects (Modifiers):**
- **Flight** - Hold jump to levitate (consumes stamina)
- **Following Rift** - Rift follows you instead of being stationary

**Fragments:**
- [To Be Designed]

**Playstyle Focus:** Keeping team alive, providing utility, enabling aggressive plays

---

### Offensive Class
**Role:** High damage, aggression, eliminating priority targets

**Standard Abilities:**
- **Parry** - Perfect timing deflects attacks and counters (10s cooldown)
- **Fireball** - Launch explosive projectile (15s cooldown)
- **Blink** - Short-range teleport in any direction (12s cooldown)

**Ultimate Ability:**
- **Chaos Reach** - Channel a beam of destruction for 6 seconds, can redirect aim

**Aspects:**
- **Controlled Demolitionist** - Your explosions deal more damage but have smaller radius

**Fragments:**
- [To Be Designed]

**Playstyle Focus:** Burst damage, assassination, high-risk high-reward combat

---

### Defensive Class
**Role:** Sustained damage, area denial, pet/summon management

**Standard Abilities:**
- **Telekinesis** - Levitate and throw enemies or objects (20s cooldown)
- **Self Rewind** - Restore health and position from 5 seconds ago (45s cooldown)
- **Child of the Old Gods** - Summon a void entity that attacks enemies (60s cooldown)
- **Duskfield** - Create a slowing field that drains enemy movement (30s cooldown)
- **Earthquake** - Ground slam that staggers enemies in radius (35s cooldown)
- **Dragon's Breath** - Cone of fire damage over 3 seconds (25s cooldown)
- **Turrets** - Deploy automated gun turret (40s cooldown)
- **Summons** - Call forth allied creatures to fight (60s cooldown)

**Ultimate Abilities:**
- **Dawnblade** - Summon flaming sword, fly and rain down fire projectiles
- **Summon Eldritch** - Call forth a massive creature to fight for 30 seconds

**Aspects:**
- **Devour** - Kills restore health and extend ability duration
- **Shadow Miasma** - Void abilities leave damaging pools
- **Stasis Turrets** - Turrets slow enemies instead of dealing damage

**Fragments:**
- [To Be Designed]

**Playstyle Focus:** Area control, sustained pressure, managing battlefield with summons

---

## Aspects System (Subclass Modifiers)

**Design:** Each class has **Aspects** that fundamentally change how abilities work. Players can equip 2 Aspects at a time.

**Examples:**

**Support - Flight**
- Converts double-jump into sustained flight
- Drains stamina while airborne
- Enables aerial support and scouting

**Defensive - Devour**
- Kills trigger health regeneration
- Extends duration of active abilities by 2 seconds per kill
- Encourages aggressive defensive play

**Offensive - Controlled Demolitionist**
- Explosive abilities have reduced radius but increased damage
- Precision in explosive placement rewarded

**Support - Following Rift**
- Rift healing zone follows your movement
- Enables mobile healing during combat

---

## Fragments System (Minor Modifiers)

**Design:** Smaller passive bonuses that stack with Aspects. Players can equip 3-4 Fragments.

**Planned Categories:**
- Elemental damage bonuses
- Cooldown reduction
- Movement enhancements
- Damage type conversions

**[To Be Fleshed Out in Future Design Pass]**

---

# Character Progression

## Leveling & Experience

**Experience Sources:**
- Enemy kills
- Quest completion
- Exploration discoveries
- Boss defeats
- Raid/Dungeon clears

**Level Cap:** TBD (likely 50-100 at launch)

**Power Level:** Separate from character level; determined by gear average

## Skill Tree System

**Structure:**
- Each class has a unique skill tree with 3 major branches
- Branching choices that define build specialization
- Unlock new abilities and passive bonuses
- Respec available at cost (encourages experimentation)

**Tree Branches (Example for Offensive):**
1. **Gunslinger** - Focus on ranged weapons and precision
2. **Blade Dancer** - Focus on melee combos and mobility
3. **Arcanist** - Focus on elemental abilities and magic

---


# World & Exploration

## World Design

### Scale & Scope
- **LARGE** seamless open world
- Multiple distinct biomes and climate zones
- Vertical exploration (caves, mountains, sky islands)
- **Underwater sections** with unique ecosystems

### Features
- **Many Secrets** - Hidden areas and lore scattered throughout
- Lore fragments that reveal Titan history
- Hidden bosses and optional encounters
- Environmental puzzles
- Treasure hunts and collectibles

### NPCs & Quests
- **NPC questlines** outside of main story
- Dynamic world events
- Faction reputation systems
- Choice-driven dialogue with consequences

---

## World Structure (Design Question)

**Options to Consider:**

**1. Single Planet with Countries?**
- Pros: Cohesive geography, realistic travel, easier narrative
- Cons: Limited visual variety, harder to justify biome extremes

**2. Multiple Planets?**
- Pros: Extreme biome variety, sci-fi/fantasy blend, travel mechanics
- Cons: More complex lore, space travel systems needed

**3. Multiple Dimensions/Realms?**
- Pros: Ties into Titan lore (Throne Worlds), portal mechanics, reality-bending visuals
- Cons: Complex navigation, harder to create cohesive world feeling

**Current Design Lean:** Hybrid approach - One primary world with countries/continents, but **Throne Worlds** as instanced raid/dungeon dimensions tied to specific Titans.

---

## Races (Player & NPC)

**To Be Determined - Potential Races:**
1. Humans (baseline)
2. Descendants of the Gods (minor divine bloodlines)
3. Sigil-Bearers (those marked by Titans, like Orgolis's gift)
4. Constructs (artificial beings created by ancient magic)
5. Exiles (beings from other dimensions)

**Design Question:** Should races be:
- Cosmetic only?
- Have gameplay differences (stats, abilities)?
- Tied to specific class restrictions?

---

## Enemies

**Enemy Types to Design:**
- Common foot soldiers
- Elite variants with special abilities
- Mini-bosses in open world
- Dungeon bosses with mechanics
- Raid bosses with complex phase systems
- World bosses for public events

**Enemy Factions:**
- Corrupted Gods
- Titan Manifestations
- Rogue Sigil-Bearers
- Ancient constructs gone mad
- Dimensional invaders

**[Full enemy design to be fleshed out in future pass]**

---

# Items & Equipment

## Loot Tier System

### Common Tier
- Standard equipment
- Random stat rolls
- Easy to find
- No special effects

### Rare Tier
- Enhanced base stats
- 1 minor trait
- Uncommon drops

### Legendary Tier
- **Set Effects** - Equipping multiple pieces grants bonuses
  - 2-piece bonus
  - 4-piece bonus
  - Full set (5-piece) bonus
- Major traits
- Activity-specific drops (Raids, Dungeons)

### Mythic Tier
- **Full Set Effects** - Powerful 5-piece bonuses
- **Half Set Effects** - Get partial bonus at 3 pieces
- Multiple major traits
- Build-defining effects
- Rare drops from hardest content

### Ancient Tier
**Special Rule:** Can only carry **one Ancient armor piece** and **one Ancient weapon** at a time.

Ancient items are the most powerful in the game, with unique effects that can define an entire playstyle. The restriction forces meaningful choice.

---

## Ancient Item Examples

### The Eyes of Orgolis (Ancient Helmet)

**Description:**  
A helm forged from the fragments of Orgolis's shattered Throne World, imbued with his gift of sight beyond sight.

**Intrinsic Perk: Endless Sight**  
Activating your Ultimate grants you and nearby allies **Truesight**. Kills on debuffed targets also grant Truesight for a short period.

**Truesight Effect:**
- Reveals enemies through walls (highlighted outline)
- Shows enemy health bars and debuffs
- Reveals teammates and projectiles through walls
- Enemy weak points glow

**Community Insight (Passive Bonuses):**
- Max Truesight Duration: 14 seconds
- Activating Ultimate: Grants Truesight for entire Ultimate duration (if roaming) or 9 seconds (if instant cast)
- Teammates Near You: Gain Truesight for 9 seconds when you activate Ultimate
- Combat Bonus: Killing debuffed targets grants 6 seconds of Truesight

**Infusion Slot: Key of Wisdom**
- Increases duration of applied Truesight by +2 seconds (all sources)
- Maximum Truesight duration increased to 20 seconds
- **Target Prioritization:** Automatically highlights enemies below 50% HP in red
- **Damage Bonus:** Deal 5% increased damage to highlighted enemies (10% if below 25% HP)

**Gameplay Impact:**
- Enables information-based support role
- Synergizes with abilities that apply debuffs
- Powerful in PvP for tracking enemy movements
- Encourages coordinated team play

**Lore Tab:**
> *"He saw too much. The weight of infinite knowledge, infinite possibilities, infinite paths not taken. In the end, he chose blindness over the burden of sight. But the gift remains, for those brave enough to look."*
> 
> *— Fragment recovered from the Shattered Archives*

---

## Weapons

### Ranged Weapons

**Primary Weapons:**
- **Bows** - Precision, charge time, stealth-friendly
- **Crossbows** - Higher damage, slower fire rate, penetration
- **Hand Cannons** - High impact, low rate of fire
- **Pistols** - Balanced stats, quick draw
- **SMGs** - High rate of fire, close range
- **Auto Rifles** - Sustained damage, mid-range
- **Scout Rifles** - Long range, precision
- **Pulse Rifles** - Burst fire, mid-range

**Special Weapons:**
- **Shotguns** - Close range devastation
- **Slings** - Arc projectiles, indirect fire
- **Fusion Rifles** - Charge-up energy weapons
- **Trace Rifles** - Continuous beam weapons

**Heavy Weapons:**
- **Rocket Launchers** - Explosive area damage
- **Grenade Launchers** - Arc projectiles, remote detonation
- **Machine Guns** - Sustained heavy fire

### Melee Weapons

- **Swords** - Balanced speed and damage
- **Axes** - High damage, slow swings
- **Hammers** - Crush armor, stagger enemies
- **Glaives** - Reach, thrust attacks
- **Whips** - Range, crowd control
- **Knives/Daggers** - Fast attacks, critical hits
- **Shields** - Defense + bash attacks

### Weapon Perks System

Each weapon can roll with:
- **Base Stats** (damage, range, stability, reload speed)
- **Barrel/Sight Perks** (enhance handling or range)
- **Magazine Perks** (increase capacity or reload speed)
- **Trait Perk 1** (minor effect)
- **Trait Perk 2** (major effect)

**Example Legendary Hand Cannon Roll:**

**"Fate's Judgment"**
- *Barrel:* Extended Barrel (+10 Range)
- *Magazine:* Ricochet Rounds (+5 Stability, overpenetration)
- *Trait 1:* Outlaw (Precision kills increase reload speed)
- *Trait 2:* Rampage (Kills increase damage for 5 seconds, stacks 3x)

---

# Vehicles

## Vehicle Types

### Ground Vehicles
- **Cars** - Fast travel across open terrain
- **Tanks** - Armored, slower, mounted weapons

### Water Vehicles
- **Boats** - Surface travel, fishing platforms
- **Submarines** (potential) - Underwater exploration

### Air Vehicles
- **Helicopters** - Vertical takeoff, scouting
- **Aircraft** - Fast travel, dogfighting potential

### Space Vehicles
- **Spaceships** - If multiple planets/systems
- Travel between worlds/dimensions

**Design Considerations:**
- Should vehicles have combat capabilities?
- Personal vehicles vs. shared/deployable?
- Vehicle customization and upgrades?
- Vehicle-specific activities (races, combat arenas)?

---

# Multiplayer & PvP

## Cooperative Multiplayer

### Fireteam System
- **Standard Activities:** 1-3 players
- **Dungeons:** 3 players
- **Raids:** 6 players

### Matchmaking
- Automatic matchmaking for standard content
- Manual fireteam creation for high-end content
- LFG (Looking for Group) system integrated

### Cross-Platform Play
- Full cross-play across PC, Console (if applicable)
- Cross-progression (carry your account anywhere)

---

## PvP Modes

### Arena Modes (Small-Scale)
- **Duel (1v1)** - Ranked competitive dueling
- **Skirmish (3v3)** - Tactical small-team combat
- **Elimination (3v3)** - Round-based, no respawns

### Objective Modes
- **Control (6v6)** - Capture and hold zones
- **Clash (6v6)** - Team deathmatch
- **Rift (4v4)** - Carry objective to enemy goal

### Large-Scale PvP (Future)
- **Siege (12v12)** - Attack/defend fortifications
- **Warzone (Open)** - Open-world PvP zones with objectives

### PvP Balance Philosophy
- Separate balance from PvE where necessary
- Ability cooldowns are longer in PvP
- Heavy ammo is contested resource
- Skill-based matchmaking with competitive ranks

---

# End-Game Content

## Raids

**Design:** Large-scale 6-player cooperative challenges requiring:
- Team coordination and communication
- Puzzle-solving and mechanics mastery
- DPS checks and optimization
- Multiple bosses with unique mechanics

**Structure:**
- 4-6 encounters per raid
- Checkpoints between encounters
- Weekly lockout for rewards (can re-run for fun)
- Hard Mode variants with additional mechanics

**Reward Structure:**
- Raid-exclusive armor sets (Legendary/Mythic)
- Raid-exclusive weapons with unique perks
- Ancient items from final boss (rare drop)
- Cosmetic rewards (shaders, emotes, ships)

---

## Dungeons

**Design:** 3-player mini-raids with:
- 2-3 major encounters
- Shorter time commitment (30-60 minutes)
- Solo-able for skilled players (challenge mode)
- Puzzle and combat mix

**Difficulty Tiers:**
- Normal (recommended power)
- Heroic (+20 power, better rewards)
- Master (+40 power, Legendary/Mythic drops)

---

## World Bosses

**Design:** Open-world bosses that require multiple players
- Spawn on timers or triggered by events
- Scale difficulty based on player count
- Public events anyone can join
- Unique mechanics tied to environment

**Examples:**
- Titan Manifestations (fragments of Titan power)
- Corrupted Gods (lesser deities gone mad)
- Dimensional Rifts (portals to Throne Worlds)

---

## Challenge Modes & Modifiers

**Weekly Modifiers:**
- Enemy shields are stronger
- Player abilities recharge faster
- Elemental burn (one element deals +50% damage)
- Chaff (no radar)
- Grounded (airborne players take more damage)

**Endgame Activities:**
- **Nightfall Strikes** - Harder versions of missions with modifiers
- **Lost Sectors** - Solo dungeons with rotating modifiers
- **Trials** - Weekend competitive PvP event with exclusive rewards

---

# Narrative & Themes

## Central Themes

### Knowledge as Burden
Orgolis's story embodies the idea that infinite knowledge brings suffering. The more you know, the more you understand what you've lost, what could have been, and what will inevitably be.

### Power and Isolation
The Titans are the most powerful beings in existence, yet they are profoundly alone. Power separates them from those they might care about.

### Legacy and Inheritance
The Gods inherit diluted power from Titans. Humanity inherits fragments from Gods. The player is the latest link in a chain of declining might. Or are they? Can the cycle be broken?

### Choice and Consequence
Inspired by Cyberpunk 2077, choices made early in the game ripple forward. Help someone in Act 1, they remember in Act 3. Betray a faction, face consequences later.

---

## Main Story Structure (Proposed)

### Act 1: Awakening
- Player discovers they bear a Sigil (connection to a Titan)
- Training and introduction to game systems
- First encounter with Orgolis (in visions or dreams)
- Establishing conflict: Titans are awakening, world is in danger

### Act 2: The Hunt for Knowledge
- Seeking information about the Titans
- Traveling to different regions/realms
- Encountering other Sigil-Bearers (allies and enemies)
- Raid 1: Entering a Throne World

### Act 3: The Shunned One
- Deep dive into Orgolis's past
- Uncovering the truth about the First
- Player must choose: Side with Orgolis or against him
- Multiple endings based on choices throughout the game

---

## Side Content Narrative

### Faction Questlines
- **The Soothsayers** - Descendants of those gifted by Orgolis
- **The Shattered Court** - Gods trying to reclaim Titan power
- **The Exiles** - Beings cast out from Throne Worlds
- **The Forgotten** - NPCs seeking to erase their own memories of the Titans

### World Lore
- Scannable objects reveal history
- Books and tomes scattered throughout world
- NPC dialogue changes based on story progress
- Environmental storytelling (ruined Throne Worlds, ancient battlefields)

---

# Development Notes & Open Questions

## Design Decisions to Make

### 1. World Structure
- Single planet with regions?
- Multiple planets with space travel?
- Dimensional realms accessed via portals?
- **Recommendation:** Single world + instanced Throne Worlds

### 2. Player Races
- Cosmetic only or gameplay differences?
- How many races?
- Tie to lore (descendants of Gods, etc.)?

### 3. Relic Slot Balance
- Fixed total slots or increase with level?
- Should Ancient relics be more restrictive?

### 4. Fragments System
- How many fragment slots?
- Active vs. passive fragments?
- Element-specific or universal?

### 5. Vehicle Integration
- Core gameplay or optional convenience?
- Combat-enabled or travel-only?
- Required for some content?

### 6. Endgame Loop
- Weekly reset structure (like Destiny)?
- Seasonal content model?
- Expansion cadence?

---

## Technical Implementation Priorities (From Engine Plan)

### Phase 1-3: Foundation (Months 1-12)
- Core math and physics
- Renderer with PBR and deferred shading
- Basic ECS for entities

### Phase 4-7: Core Systems (Months 12-20)
- Movement state machine
- Combat system with frame data
- Physics integration (Jolt)
- Animation system with IK

### Phase 8-10: Multiplayer (Months 20-28)
- Networking with client prediction
- Rollback netcode
- Server authority

### Phase 11-13: Content Pipeline (Months 28-36)
- Ability system and modifier pipeline
- AI and boss behavior trees
- World streaming

### Phase 14+: Polish & Content
- Editor tools
- Balancing
- Content creation (quests, bosses, raids)

---

## Design Philosophy Summary

1. **Respect Player Time** - Clear goals, fair rewards, no excessive grinding
2. **Build Diversity** - Multiple viable playstyles per class
3. **Skill Expression** - High skill ceiling, but accessible floor
4. **Cooperative Focus** - Best experience is with friends
5. **Story Matters** - Lore is integrated, not optional
6. **Player Choice** - Decisions have weight and consequences
7. **Fair Monetization** - (If F2P) Cosmetics only, no pay-to-win

---

## Closing Note

This is a living document. As development progresses and systems are tested, expect significant iteration on:
- Class balance
- Loot economy
- World structure
- Narrative branches
- Multiplayer modes

**Core Vision:** Create a game where movement feels like Wuthering Waves, builds are as deep as Destiny 2, combat is as precise as Elden Ring, and the world feels as alive as Cyberpunk 2077—all wrapped in the haunting lore of the Titans and their shunned son, Orgolis.

---

*Document Version: 1.0*  
*Last Updated: April 2026*  
*Status: Pre-Production Design*