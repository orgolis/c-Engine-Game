# Component reference

**Generated** by `gws docs` from the authorable component registry — do not edit by hand.

Every component below self-registers with one `make_authorable<T>("Name")` line, which is what
gives it inspector UI, `.gameplay` sidecar persistence and replication at the same time. This page is
derived from that same registration, so it cannot describe a component the engine does not have,
and it cannot miss one the engine does.

**42 components** are registered.

## Contents

- [Health](#health)
- [Ability State](#ability-state)
- [Attributes](#attributes)
- [Tags](#tags)
- [Trigger Volume](#trigger-volume)
- [Trigger Actor](#trigger-actor)
- [State Machine](#state-machine)
- [Combat Actor](#combat-actor)
- [Derived Stats](#derived-stats)
- [Regeneration](#regeneration)
- [Progression](#progression)
- [Skill Tree](#skill-tree)
- [Unlocked Skills](#unlocked-skills)
- [Inventory](#inventory)
- [Equipment](#equipment)
- [Vendor](#vendor)
- [Harvest Node](#harvest-node)
- [Quest Log](#quest-log)
- [Interactable](#interactable)
- [Pickup](#pickup)
- [Faction](#faction)
- [Aggro](#aggro)
- [Spawner](#spawner)
- [Save Id](#save-id)
- [World Flags](#world-flags)
- [Weapon](#weapon)
- [Vehicle](#vehicle)
- [Race Progress](#race-progress)
- [Building](#building)
- [Extractor](#extractor)
- [Machine](#machine)
- [Generator](#generator)
- [Conveyor](#conveyor)
- [Needs](#needs)
- [Sanity](#sanity)
- [Flashlight](#flashlight)
- [Light Source](#light-source)
- [Fear Source](#fear-source)
- [Vision Cone](#vision-cone)
- [Awareness](#awareness)
- [Stealth](#stealth)
- [Hearing](#hearing)

---

## Health

`schizo::ecs::Health` — 12 bytes, alignment 4, version 1

| field | type | offset | size |
|---|---|---|---|
| `current` | float | 0 | 4 |
| `max` | float | 4 | 4 |
| `regen` | float | 8 | 4 |

## Ability State

`schizo::ecs::AbilityState` — 16 bytes, alignment 4, version 1

| field | type | offset | size |
|---|---|---|---|
| `ability` | int | 0 | 4 |
| `cooldown` | float | 4 | 4 |
| `charges` | unsigned | 8 | 4 |
| `flags` | unsigned | 12 | 4 |

## Attributes

`schizo::ecs::AttributeSet` — 24 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

## Tags

`schizo::ecs::GameplayTags` — 24 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

## Trigger Volume

`schizo::ecs::TriggerVolume` — 128 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

## Trigger Actor

`schizo::ecs::TriggerActor` — 1 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

## State Machine

`schizo::ecs::StateMachine` — 120 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

## Combat Actor

`schizo::ecs::CombatActor` — 168 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

## Derived Stats

`schizo::ecs::DerivedStats` — 24 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

## Regeneration

`schizo::ecs::Regeneration` — 24 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

## Progression

`schizo::ecs::Progression` — 56 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

## Skill Tree

`schizo::ecs::SkillTree` — 24 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

## Unlocked Skills

`schizo::ecs::UnlockedSkills` — 24 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

## Inventory

`schizo::ecs::Inventory` — 32 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

## Equipment

`schizo::ecs::Equipment` — 72 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

## Vendor

`schizo::ecs::Vendor` — 64 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

## Harvest Node

`schizo::ecs::HarvestNode` — 48 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

## Quest Log

`schizo::ecs::QuestLog` — 48 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

## Interactable

`schizo::ecs::Interactable` — 72 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

## Pickup

`schizo::ecs::Pickup` — 40 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

## Faction

`schizo::ecs::Faction` — 32 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

## Aggro

`schizo::ecs::Aggro` — 40 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

## Spawner

`schizo::ecs::Spawner` — 56 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

## Save Id

`schizo::ecs::SaveId` — 8 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

## World Flags

`schizo::ecs::WorldFlags` — 24 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

## Weapon

`schizo::ecs::WeaponState` — 48 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

## Vehicle

`schizo::ecs::Vehicle` — 48 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

## Race Progress

`schizo::ecs::RaceProgress` — 56 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

## Building

`schizo::ecs::Building` — 32 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

## Extractor

`schizo::ecs::Extractor` — 40 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

## Machine

`schizo::ecs::Machine` — 48 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

## Generator

`schizo::ecs::Generator` — 4 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

## Conveyor

`schizo::ecs::Conveyor` — 48 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

## Needs

`schizo::ecs::Needs` — 24 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

## Sanity

`schizo::ecs::Sanity` — 20 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

## Flashlight

`schizo::ecs::Flashlight` — 20 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

## Light Source

`schizo::ecs::LightSource` — 4 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

## Fear Source

`schizo::ecs::FearSource` — 8 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

## Vision Cone

`schizo::ecs::VisionCone` — 8 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

## Awareness

`schizo::ecs::Awareness` — 20 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

## Stealth

`schizo::ecs::Stealth` — 4 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

## Hearing

`schizo::ecs::Hearing` — 4 bytes

_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,
so its layout is defined by its own serializer. Field-level documentation for these needs
reflection support for containers.

---

_2 components document their fields through reflection; 40 use custom serialization._
