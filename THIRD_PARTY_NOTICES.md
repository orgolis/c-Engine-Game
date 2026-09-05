# Third-party notices

GameWorldshaper incorporates the third-party software listed here. Each component
remains governed by **its own licence**, not by the GameWorldshaper Engine License —
nothing in that licence limits your rights under these, and nothing here is
intended to conflict with them.

**This file must travel with any distribution of the Engine, and with any Product
that embeds it.** MIT, BSD and zlib all require the copyright notice to be carried
with the code; shipping without it is a licence violation regardless of what the
Engine's own licence says.

None of these components are copyleft. There is no GPL or LGPL code in the Engine,
which is what makes the Engine's own source-available licence possible.

---

## Components

| Component | Licence | Copyright | Upstream |
|---|---|---|---|
| **Jolt Physics** | MIT | Jorrit Rouwe | https://github.com/jrouwe/JoltPhysics |
| **EnTT** | MIT | 2017–2026 Michele Caini | https://github.com/skypjack/entt |
| **Dear ImGui** | MIT | 2014–2026 Omar Cornut | https://github.com/ocornut/imgui |
| **GLM** | MIT (Happy Bunny / MIT dual) | 2005 G-Truc Creation | https://github.com/g-truc/glm |
| **spdlog** | MIT | 2016–present Gabi Melman and contributors | https://github.com/gabime/spdlog |
| **ufbx** | MIT | 2020 Samuli Raivio | https://github.com/ufbx/ufbx |
| **LZ4** | BSD 2-Clause | 2011–2020 Yann Collet | https://github.com/lz4/lz4 |
| **Zstandard** | BSD 3-Clause / GPL-2 dual — used under BSD | Meta Platforms, Inc. and affiliates | https://github.com/facebook/zstd |
| **ENet** | MIT | 2002–2024 Lee Salzman | https://github.com/lsalzman/enet |
| **GLFW** | zlib/libpng | 2002–2006 Marcus Geelnard; 2006–2019 Camilla Löwy | https://www.glfw.org/ |
| **TinyUSDZ** | Apache-2.0 | 2020–2023 Syoyo Fujita | https://github.com/syoyo/tinyusdz |
| **Catch2** *(tests only)* | Boost Software License 1.0 | Catch2 Authors | https://github.com/catchorg/Catch2 |
| **bc7enc** | MIT | 2020 Richard Geldreich, Jr. | https://github.com/richgel999/bc7enc_rdo |
| **miniaudio** | Public domain / MIT-0 (dual) | David Reid | https://github.com/mackron/miniaudio |
| **stb** | Public domain / MIT (dual) | Sean Barrett | https://github.com/nothings/stb |
| **meshoptimizer** | MIT | Arseny Kapoulkine | https://github.com/zeux/meshoptimizer |
| **pocketpy** | MIT | blueloveTH and contributors | https://github.com/pocketpy/pocketpy |
| **VulkanMemoryAllocator** | MIT | Advanced Micro Devices, Inc. | https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator |
| **glslang** | BSD 3-Clause / Apache-2.0 | The Khronos Group Inc., NVIDIA, Google | https://github.com/KhronosGroup/glslang |
| **TinyGLTF** | MIT | Syoyo Fujita, Aurélien Chatelain and contributors | https://github.com/syoyo/tinygltf |
| **glad** | MIT (generated loader) | David Herberth | https://github.com/Dav1dde/glad |

The Vulkan SDK and its headers are used under the Apache-2.0 licence of The Khronos
Group Inc.

---

## ⚠ Known gap — vendored copies missing their licence text

Most of the above are git submodules and carry their own `LICENSE` file in-tree, so
the notice travels with the source automatically. **These are vendored as bare
sources with no licence file alongside them:**

`stb` · `vma` · `glad` · `meshoptimizer` · `pocketpy` · `tinygltf` · `glslang`

Their licences still require the copyright notice to be distributed with the code,
so the table above is currently the only thing satisfying that obligation for them.
That is thin. **The correct fix is to place each upstream `LICENSE` file next to the
vendored source**, which is tracked as follow-up work — see the licensing note in
`docs/`.

The binary release archives do not currently include this file either. They should.

---

## Full licence texts

The complete text of each licence is available in the corresponding
`third_party/<component>/LICENSE` file for submoduled components, and at the
upstream URLs above for all components. The three licence families used are:

- **MIT** — permission to use, copy, modify, merge, publish, distribute, sublicense
  and sell, provided the copyright notice and permission notice are included in all
  copies or substantial portions.
- **BSD 2-/3-Clause** — as MIT, additionally requiring that the notice be reproduced
  in documentation accompanying binary distributions, and (3-Clause) that the names
  of contributors not be used to endorse derived products without permission.
- **zlib/libpng** — permission to use and redistribute, provided the origin is not
  misrepresented, altered versions are marked as such, and the notice is not removed.
- **Apache-2.0** — as above plus an express patent grant, requiring that a copy of
  the licence and a NOTICE of modifications accompany redistribution.
- **Boost 1.0** — permissive; the notice need not be reproduced in binary form.
