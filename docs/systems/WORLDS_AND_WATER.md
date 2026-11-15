# Worlds and water

## A whole world from a preset

The editor's World menu (and `gws world`, below) builds a landscape from one
choice -- Rolling hills, Mountains, Islands, Plain: ground generated and worn
by erosion, rivers let out at the highest points and carved into the ground,
biomes laid on it, ground textures and foliage placed. Everything it makes is
ordinary objects and components, editable afterwards with the usual tools.

### Rivers

Water from a spring runs the steepest way down -- over the land with its
hollows filled, so it always reaches the edge of the land (or the sea) in one
go instead of wandering between hollows. Where the ground under it lies more
than 3 m (the preset's `breach`) below that filled land, the river widens into
a **lake** at the level of the lake's rim; shallower hollows it cuts straight
through. Its bed is carved into the ground, always falling.

Each stretch of river between lakes is **one water surface**: a Water component
with a **path** -- points downstream, each with its level and half width -- that
the surface follows as a continuous ribbon, its level falling with the bed,
its ripples flowing downstream (faster where it falls steeply, with foam where
it runs fast). A lake is a flat surface at its level, over **its own basin**:
its shape (the ground it filled, grown a little to run on under the shore) is
kept with it, so it does not flood a river valley or another lake that crosses
its bounding box. A water surface only shows where the ground is below it, so
a river shows in its bed and a lake takes the shape of its shore. Nothing
grows under the water: the foliage masks are cleared where the ground lies
below a river or a lake.

Two rivers running into the same lake make one lake surface. A river that
reaches another river (or its lake) **joins it there and ends**: there is one
water surface on any bed, never two one on the other. A river that flows into
a lake ends at the shore -- none of its points lie under the lake -- and the
lake's bed is the filled hollow, not a trench carved under it.

Where surfaces meet (a river's mouth at a lake) only the nearest one is drawn
at each pixel: the water is drawn twice, depth first, so two surfaces at
almost the same level never show through each other or flicker.

### Water

| | |
|---|---|
| Physical | in Play, dynamic things float and the player swims in it -- in a river, anywhere within its width at the level there |
| Size | a lake or pond: the rectangle (a river has its path instead); with a shape, only its water cells of it |
| Deep / Shallow colour | what thick water and thin water look like |
| Wave height, speed, scale | swell; for a river, speed is how fast it flows |
| Clarity | metres of water before it takes the deep colour: thin water is clear and shows its bed |
| Reflectivity | how much of the sky it reflects (more at a glancing angle) |

Ripples are noise drifting along the flow (a slow drift on still water), calmer
in the distance; the sky's reflection is softened; foam where water meets the
bank. A scene saves a river's path as `WATER_PATH=x,z,level,half width;...` and
a lake's shape as `WATER_MASK=nx,nz,runs...` -- `nx` x `nz` cells over its
rectangle, row by row, as run lengths starting with dry (dry, water, dry, ...).

**In Play** water answers what touches it: a round, a beam or something thrown
that goes into it throws up a **splash** (`<gun>_splash`, else the built-in
`splash`; `splash_big` for a blast) and sends **rings** spreading across the
surface -- as does anyone wading or swimming through it (the player, NPC
agents), and a jump into it. Swimming and splashes see a lake's shape: no
water in a dry hollow inside its rectangle.

## `gws world`

The same build without the editor, written as a scene:

```
gws world --save world/world.scene --preset "Rolling hills" --size 1024 --rivers 4 --seed 7
```

The heightmap, splat map and foliage masks are written beside the scene. A
bigger `--size` keeps the preset's cell size (2 m for Rolling hills) unless
`--resolution` is given; `--height` the tallest hills, `--materials <dir>` writes
ground materials there (default: none). Each run with the same options builds
the same world.

The code: `engine/scene/include/world_preset.h` (the build),
`terrain_hydrology.h` (`trace_water`, `carve_river`, `river_paths`),
`water_component.h`; the water pass `engine/renderer/gpu/vulkan/vulkan_water_pass.*`
(`tools/water*.vert`, `water.frag`); `tools/worldgen` (driven by `gws world`).
Checked by `tools/terrainbrush_check` and `tools/sceneio_check`.
