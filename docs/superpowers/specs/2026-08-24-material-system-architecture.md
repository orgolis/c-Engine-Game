# Material & texture system — architecture

**Created:** 2026-08-24
**Status:** architecture agreed in outline; per-tier specs follow
**Scope decision:** Tiers 1–4, chosen deliberately. This **overrules** `WORKFLOW_PLAN.md`
9.6 (virtual-texture runtime, material layering ⚪ Later) and the "explicitly not doing"
line about chasing fidelity. Recorded here so the contradiction is visible rather than silent.

---

## 1. Why this needs an architecture pass first

The request could be read as a feature list. It is not one. Two findings from reading the
renderer decide the shape of everything else, and getting either wrong means redoing the
tiers built on top of it.

### 1.1 The G-buffer is full

| Attachment | Format | Contents | Spare |
|---|---|---|---|
| 0 | `R16G16B16A16_SFLOAT` | `vec4(worldPos, 1.0)` | **`.w`** — constant, unused |
| 1 | `R16G16B16A16_SFLOAT` | `vec4(N, roughness)` | none |
| 2 | `R8G8B8A8_UNORM` | `vec4(albedo, metallic)` | none |
| 3 | `R8G8B8A8_UNORM` | `vec4(emissive, ao)` | none |

Tier 3 needs roughly thirteen more per-pixel values (clearcoat + its roughness, sheen colour
+ roughness, subsurface colour + thickness, anisotropy + tangent). There is **one free
16-bit channel**. Neither `gbuffer_scene.frag` nor `lighting_pass.frag` implements any lobe
beyond metal-rough today — verified, not assumed.

### 1.2 There is no bindless texturing

No `VK_EXT_descriptor_indexing`, no `runtimeDescriptorArray`, no bindless path anywhere.
Every texture occupies an explicit binding, and **terrain already uses 14** for one splat map
plus three maps across four layers. That limit is why terrain has a dedicated pipeline at all.

This is the wall behind two separate asks: "more than four terrain layers" (Tier 2) and
material layering (Tier 4). Both are blocked by the same missing capability, and adding
either without it means yet another hand-written pipeline with yet more bindings.

**So the program is not four tiers of features. It is two foundations with features on top.**

---

## 2. Foundation A — bindless textures

Adopt `VK_EXT_descriptor_indexing` (core in Vulkan 1.2): one large descriptor array of all
resident textures, with materials referencing textures by **index** rather than by binding.

**What it unlocks, all at once:**
- Terrain layers beyond four — layers become indices, not bindings
- Material layering — a layered material is a list of indices
- Virtual texturing (Tier 4) — the page table is itself an indexed lookup
- Fewer pipelines: the terrain pipeline exists only because of the binding count

**The capability risk, and how it is handled.** This engine has already shipped a build that
would not start on an RX 5700 because a capability was assumed rather than checked. Descriptor
indexing is widely available but not universal, so it is **queried and gated**, and the
existing bound-texture path stays as the fallback. `GWS_FORCE_NO_BINDLESS` forces the fallback
locally, because the lesson recorded in the plan is that a capability-dependent branch needs a
way to force it or it is untested by construction.

**Texture identity changes.** A material stops holding a `VkImageView` and starts holding a
stable texture handle resolved to an array index each frame. That indirection is exactly what
virtual texturing needs later, which is why it belongs at the bottom rather than bolted on.

---

## 3. Foundation B — shading models in the G-buffer

Store a **shading-model ID** and reinterpret one attachment per model, rather than adding a
fifth attachment for every pixel whether it uses one or not.

**Why not simply add an attachment.** At 1920×1080 a fifth RGBA8 target costs ~8 MB and its
bandwidth on every pixel of every frame, to serve materials that are a small fraction of a
scene. This project has already measured that bandwidth claims must be checked rather than
assumed (workflow item P1, where the bandwidth diagnosis did not survive measurement) — but
the asymmetry here is clear enough: the cost is universal and the benefit is not.

**The scheme.**
- `outPosition.w` — currently a constant 1.0 — carries the shading-model ID.
- Each model beyond Standard reinterprets attachment 3 (`emissive, ao`) as its custom data.
  A clearcoat or cloth material that also needs emissive is the trade; emissive stays available
  on Standard, which is where it is actually used.
- The lighting pass branches on the ID and evaluates the matching lobe.

| ID | Model | Attachment 3 becomes |
|---|---|---|
| 0 | Standard (metal-rough) | `emissive.rgb, ao` — unchanged |
| 1 | Clearcoat | `coat_strength, coat_roughness, ao, —` |
| 2 | Sheen / cloth | `sheen.rgb, ao` |
| 3 | Subsurface | `subsurface.rgb, thickness` |
| 4 | Anisotropic | `tangent.xy, anisotropy, ao` |

**Divergence is the cost, and it is bounded.** A branchy lighting shader runs at the speed of
the worst lane in a wave. Models are assigned per material and therefore cluster spatially, so
in practice most waves are uniform. The alternative — a separate forward pass per exotic
material — was rejected because it duplicates the entire lighting path and every fix to it.

---

## 4. The tiers, on those foundations

### Tier 1 — finish what is half-built ✅ **DONE (v0.7.6–v0.7.8)**
- **Terrain per-layer metallic / roughness / normal_scale.** `TerrainComponent` carries only
  `tiling_`; the renderer's `TerrainLayerMaterial` already accepts the rest. This single gap is
  why terrain cannot be made to reflect at all, which is the symptom that started this.
- **Per-material UV offset / scale.** Only terrain has tiling today.
- **Material instances** — one `.mat` reused with per-object overrides, instead of duplicating
  a file to change one number. Implemented as a bitmask plus the component's EXISTING inline
  fields as the override values, so an override stores no new state and unticking one restores
  the asset's value exactly, with nothing to reset.

**Found while building it:** `MaterialDesc::content_hash()` did not cover the new UV fields, so a
tiling edit would never have invalidated the cached GPU material — the surface keeps its old
repeat and the edit appears to do nothing. Caught because `material_check` already asserts the
hash reacts to every field the GPU can observe.

### Tier 2 — the features you see
- **Triplanar projection.** Terrain textures currently stretch into smears on any steep slope,
  because a heightfield is UV-mapped from above. The single most visible item in this document.
- **Detail maps / detail normals** for close-up fidelity.
- **Parallax occlusion mapping.**
- **Vertex-colour blending.**
- **Terrain layers beyond four** — requires Foundation A.

### Tier 3 — extended BRDF
Clearcoat, sheen, subsurface, anisotropy — requires Foundation B. Each is a lobe in the
lighting shader plus authoring in `.mat`, the material editor, and the material graph.

### Tier 4 — infrastructure
- **Material layering** — requires Foundation A.
- **Decals** — a deferred decal pass writing into the G-buffer before lighting.
- **Texture streaming** — mip-level residency driven by what the camera can see.
- **Virtual texturing** — requires Foundation A and streaming. The largest single item here and
  the one most likely to be cut on reflection; it is included because the scope was chosen
  explicitly, but it should be re-examined before it starts rather than entered by momentum.

---

## 5. Order, and why

```
Foundation A (bindless)  ──┬── Tier 1  (independent, ships first)
                           ├── Tier 2  (triplanar needs nothing; >4 layers needs A)
                           └── Tier 4  (layering, virtual texturing)
Foundation B (shading IDs) ─── Tier 3
```

**Tier 1 ships first regardless**, because it is independent of both foundations and it answers
the original question — whether a textured terrain reflects. It also exercises the coloured
metal reflections added in v0.7.2, so the two validate each other.

Then Foundation A, then Tier 2, then Foundation B and Tier 3, then Tier 4.

Each tier is its own spec, its own plan, and its own release, so there is something testable at
every step rather than one long silence.

---

## 6. Testing

The recurring failure mode in this renderer is code that passes validation and draws the wrong
thing — 3.9's billboard pass, and the metal reflections that were white for the entire life of
the project. Each tier therefore needs assertions on the parts that *can* be asserted headlessly,
and honest acknowledgement of the part that cannot.

- **Headless and testable:** `.mat` round-trips including every new field; material-instance
  override resolution; shading-model ID packing and unpacking; texture-index resolution and its
  fallback when bindless is unavailable; triplanar blend weights summing to 1 at every normal.
- **Needs a GPU but automatable:** the bindless fallback path actually renders
  (`GWS_FORCE_NO_BINDLESS`), and every shading model compiles into both lighting variants.
- **Needs a human:** whether it looks right. Stated per release rather than implied.

---

## 7. Risks, named

1. **Descriptor indexing is not universal.** Queried, gated, with the bound path kept and a
   force-off switch. Non-negotiable given the RX 5700 history.
2. **The G-buffer repack changes every shader that reads it** — lighting, SSR, SSAO, DDGI,
   transparent. Foundation B lands as one change with all readers updated together, not
   incrementally, because a reader left on the old interpretation reads another model's data as
   emissive and the error is silent.
3. **Virtual texturing is a multi-week subsystem** whose benefit at this project's scale is
   unproven. Flagged for a deliberate go/no-go before it starts.
4. **Shader permutations multiply.** Both lighting variants (RT / non-RT) already have to be
   regenerated together; every shading model widens that. The regeneration is now scripted for
   both, which is what makes this tolerable.
