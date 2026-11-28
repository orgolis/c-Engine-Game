#pragma once

// ============================================================================
// Items editor templates -- starting points for abilities, statuses and perks.
// ============================================================================
//
// Each template is the text of one or more .items blocks with {id} where the
// new entry's id goes (helpers it needs are named {id}_<something>). The Items
// editor puts them into the open file; from there they are ordinary data to
// rename and tune. They show the engine's parts, not any one game's design.

#include <string>
#include <vector>

namespace schizo::editor {

struct ItemsTemplate {
    const char* group;   // Ability | Status | Perk
    const char* title;
    const char* about;
    const char* text;    // {id} is replaced
};

inline const std::vector<ItemsTemplate>& items_templates() {
    static const std::vector<ItemsTemplate> t = {
        // ---- abilities --------------------------------------------------------------------
        {"Ability", "Bolt", "Flies straight and hurts the first one it meets.",
         "ability {id}\n  name Bolt\n  cooldown 3\n  cast 0.15\n  target aimed\n  range 60\n  damage 40 fire\n  speed 50\n"
         "  about A bolt that flies to the first one in its way: 40 fire damage.\nend\n"},
        {"Ability", "Thrown blast", "Thrown in an arc; goes off where it lands.",
         "ability {id}\n  name Thrown Blast\n  cooldown 8\n  target area_at_aim\n  range 30\n  radius 4\n  damage 60 explosive\n"
         "  speed 18\n  arc 9.8\n  labels thrown\n  about Thrown: 60 damage to everyone within 4 m of where it lands.\nend\n"},
        {"Ability", "Explosive grenade", "Thrown; a real explosion where it lands -- less at its edge, throws, shakes.",
         "ability {id}\n  name Explosive Grenade\n  cooldown 10\n  target area_at_aim\n  range 30\n  radius 5\n  damage 80 explosive\n"
         "  speed 18\n  arc 9.8\n  falloff 0.25\n  force 12\n  shake 0.5\n  labels thrown\n"
         "  about Thrown: an explosion -- 80 damage at its centre, a quarter of it 5 m out; it throws things and shakes.\nend\n"},
        {"Ability", "Vortex", "Thrown; a field that pulls things in while it hurts.",
         "ability {id}\n  name Vortex\n  cooldown 14\n  target area_at_aim\n  range 25\n  radius 4\n  damage 8 magic\n"
         "  speed 18\n  arc 9.8\n  zone 4 0.5\n  force -6\n  labels thrown\n"
         "  about Where it lands, for 4 s: things are pulled in, and 8 damage every 0.5 s.\nend\n"},
        {"Ability", "Lingering field", "Thrown; leaves an area that keeps hurting.",
         "ability {id}\n  name Lingering Field\n  cooldown 12\n  target area_at_aim\n  range 25\n  radius 4\n  damage 6 fire\n"
         "  speed 20\n  arc 9.8\n  zone 5 0.5\n  labels thrown\n  about Where it lands, 6 fire damage every 0.5 s for 5 s.\nend\n"},
        {"Ability", "Close strike", "Lunges and hits those in front.",
         "ability {id}\n  name Close Strike\n  cooldown 6\n  target cone\n  radius 3.5\n  angle 100\n  damage 50\n  move dash 4\n"
         "  labels close\n  about A lunge: 50 damage to those in front within 3.5 m.\nend\n"},
        {"Ability", "Dash", "Moves the caster quickly along the view.",
         "ability {id}\n  name Dash\n  cooldown 4\n  target self\n  move dash 8\n  labels movement\n  about 8 m along the view.\nend\n"},
        {"Ability", "Self buff", "Gives the caster a status for a while (and the status).",
         "ability {id}\n  name Empower\n  cooldown 20\n  target self\n  self_status {id}_status\n"
         "  about For 8 s: 20% more damage.\nend\n\n"
         "status {id}_status\n  name Empowered\n  labels buff\n  duration 8\n  dealt 20\n  about Deals 20% more damage.\nend\n"},
        {"Ability", "Healing area", "Leaves an area that heals the caster's side.",
         "ability {id}\n  name Healing Area\n  cooldown 25\n  target self\n  radius 5\n  zone 6 1\n  zone_hits allies\n"
         "  effect Health 10\n  about For 6 s, those on your side within 5 m heal 10 a second.\nend\n"},
        {"Ability", "Beam", "Channelled: keeps hurting while the key is held.",
         "ability {id}\n  name Beam\n  cooldown 10\n  target aimed\n  range 40\n  damage 12 shock\n  channel 3 0.25\n"
         "  about Hold: 12 shock damage every 0.25 s for up to 3 s.\nend\n"},
        {"Ability", "Pulse", "Hurts everyone around the caster at once.",
         "ability {id}\n  name Pulse\n  cooldown 8\n  target area\n  radius 6\n  damage 30\n  about 30 damage to everyone within 6 m.\nend\n"},
        // ---- statuses ---------------------------------------------------------------------
        {"Status", "Stacking burn that bursts", "Damage over time per stack; at 5 stacks it bursts around its bearer.",
         "status {id}\n  name Burning\n  labels fire debuff\n  stacks 5\n  duration 4\n  decay 1\n  tick 0.5 3 fire\n"
         "  at 5 burst {id}_burst\n  about Burns 3 a stack every 0.5 s; at 5 stacks it bursts.\nend\n\n"
         "ability {id}_burst\n  name Burst\n  target area\n  radius 4\n  damage 60 fire\nend\n"},
        {"Status", "Slow that freezes", "Slower per stack; at 3 stacks its bearer is frozen (stunned, takes more).",
         "status {id}\n  name Chilled\n  labels cold debuff\n  stacks 3\n  duration 5\n  mod MoveSpeed -15\n"
         "  at 3 apply {id}_frozen\n  about 15% slower a stack; at 3, frozen.\nend\n\n"
         "status {id}_frozen\n  name Frozen\n  labels cold debuff\n  duration 2\n  tag status.stunned\n  taken 30\n"
         "  hurt 40 clear\n  about Cannot act; takes 30% more; a hard hit breaks it.\nend\n"},
        {"Status", "Weaken", "Its bearer takes more damage.",
         "status {id}\n  name Weakened\n  labels debuff\n  duration 6\n  taken 25\n  about Takes 25% more damage.\nend\n"},
        {"Status", "Shield", "Takes damage before health does.",
         "status {id}\n  name Shielded\n  labels buff\n  duration 8\n  shield 50\n  about A 50-point shield for 8 s.\nend\n"},
        {"Status", "Hidden", "Enemies stop seeing its bearer (status.hidden).",
         "status {id}\n  name Hidden\n  labels buff\n  duration 4\n  tag status.hidden\n  about Enemies lose sight of you for 4 s.\nend\n"},
        {"Status", "Stun", "Its bearer can do nothing (status.stunned).",
         "status {id}\n  name Stunned\n  labels debuff\n  duration 1.5\n  tag status.stunned\n  about Cannot move, attack or cast.\nend\n"},
        {"Status", "Disarm", "Its bearer cannot attack (status.disarmed).",
         "status {id}\n  name Disarmed\n  labels debuff\n  duration 3\n  tag status.disarmed\n  about Cannot attack for 3 s.\nend\n"},
        {"Status", "Chain on hit", "Hits on its bearer leap to others nearby.",
         "status {id}\n  name Charged\n  labels shock debuff\n  duration 5\n  hurt 1 chain 25 shock 3 8\n"
         "  about When hit, 25 shock damage leaps to 3 others within 8 m.\nend\n"},
        {"Status", "Spreads on death", "Dying with it passes it on.",
         "status {id}\n  name Plague\n  labels poison debuff\n  stacks 3\n  duration 8\n  tick 1 4 poison\n  death spread {id} 2 6\n"
         "  about Poison; dying with it passes 2 stacks to those within 6 m.\nend\n"},
        {"Status", "Heal over time", "Heals its bearer every second.",
         "status {id}\n  name Mending\n  labels buff\n  duration 6\n  tick 1 8 heal\n  about Heals 8 a second for 6 s.\nend\n"},
        // ---- perks ------------------------------------------------------------------------
        {"Perk", "Stat bonus", "A lasting bonus while carried.",
         "perk {id}\n  name Quick Hands\n  mod CooldownReduction 10\n  about Abilities cool down 10% faster.\nend\n"},
        {"Perk", "On hit, a status", "Hits with abilities put a status on the one hit.",
         "perk {id}\n  name Marking Strikes\n  when hit ability\n  chance 0.5\n  do apply {id}_mark 1\n"
         "  about Ability hits have a 50% chance to weaken.\nend\n\n"
         "status {id}_mark\n  name Marked\n  labels debuff\n  duration 5\n  taken 15\n  about Takes 15% more damage.\nend\n"},
        {"Perk", "On kill, recharge", "A kill gives back part of every cooldown.",
         "perk {id}\n  name Momentum\n  when kill\n  every 2\n  do recharge all 20\n"
         "  about Kills give back 20% of every cooldown (once in 2 s).\nend\n"},
        {"Perk", "Meter filler", "Hits and kills fill an attribute of the game's (a rage, a charge...).",
         "perk {id}\n  name Building Charge\n  when hit\n  do add Charge 0.05 per\n  when kill\n  do add Charge 5\n"
         "  about Damage dealt and kills fill Charge (an attribute the carrier must have).\nend\n"},
        {"Perk", "Last stand", "Low on health, a shield (and the shield).",
         "perk {id}\n  name Last Stand\n  when hurt\n  if health_below 30\n  every 20\n  do self {id}_shield\n"
         "  about Below 30% health, a 60-point shield (once in 20 s).\nend\n\n"
         "status {id}_shield\n  name Last Stand\n  labels buff\n  duration 6\n  shield 60\nend\n"},
    };
    return t;
}

/// A template's text with its id put in.
inline std::string items_template_text(const ItemsTemplate& t, const std::string& id) {
    std::string s = t.text;
    for (size_t at = s.find("{id}"); at != std::string::npos; at = s.find("{id}", at + id.size())) s.replace(at, 4, id);
    return s;
}

}  // namespace schizo::editor
