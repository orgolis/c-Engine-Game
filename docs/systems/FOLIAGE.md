# Foliage: trees, bushes, grass and rocks

A terrain's **Foliage** (Inspector, on the terrain) holds layers. Each says
what grows -- a grown conifer or broadleaf tree, a bush, grass, a rock, or a
model file -- how big (`Size` is the height at scale 1; each instance gets a
scale between the layer's smallest and largest), how many per 100 m², and
where: a placement rule (height, slope, noise), a painted texture layer, and a
density mask painted with the brush. The same settings on the same ground
always grow the same forest.

## Solid in Play

Each layer has **Solid**: in Play, solid foliage stops the player, rounds,
thrown and flying things, line of sight, and creatures (they steer round it).

- **By kind** (the default): trees, rocks and model files are solid, grass
  and bushes are not.
- **Yes / No**: for any layer.

A tree is solid at its trunk (an upright capsule, from the ground to about
half its height), a rock as a whole. The shapes are built from the layer when
Play starts.

To keep a path, a doorway or a camp clear, paint the layer's density mask
there.

Grass grows in clumps of curved blades of different heights, in three shades
of the layer's colour; the blades sway in the wind, gusts rolling across a
field. Nearer than 45% of the draw distance a clump is full, farther a lighter
one; past 60% the clumps thin out. Nothing grows under water when a world is
built (docs/systems/WORLDS_AND_WATER.md).

A tree's leaves **fade out near the camera** -- a dither from 2 m, gone by
0.9 m -- so walking through low branches does not fill the view with leaves or
cut through the gun in hand; they still cast their shadows. Trunks stay (they
are solid: the camera never gets inside one). The same is any material's
`NEAR_FADE=<metres>` (in a `.mat`; 0, the default: never).

Trees cast shadows -- and break the sun into light shafts (Volumetric light,
Render Settings) -- from a **directional light** that casts shadows: put a
Light (Directional, Cast Shadow) on the scene's sun. A scene without one is lit
by a fallback sun with no shadows at all.

Placement is `engine/scene/include/foliage.h` (`scatter_foliage`,
`foliage_collider`), checked by `tools/terrainbrush_check`; the solid shapes
go into Play's physics world in `editor/src/scene_playback_manager.cpp`.
