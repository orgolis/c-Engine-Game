"""Parse a VK_LAYER_LUNARG_api_dump text log into a GPU resource inventory.

usage: vk_inventory.py <api.txt> [section ...]
sections: images buffers memory accel pipelines frame all
"""
import re, sys, collections

path = sys.argv[1]
sections = set(sys.argv[2:]) or {"all"}
want = lambda s: s in sections or "all" in sections

hdr = re.compile(r"^Thread (\d+), Frame (\d+):")
call = re.compile(r"^(vk[A-Za-z0-9]+)\(")

blocks = []          # (frame, name, [lines])
frame = 0
cur = None
with open(path, encoding="utf-8", errors="replace") as f:
    for line in f:
        m = hdr.match(line)
        if m:
            frame = int(m.group(2))
            continue
        m = call.match(line)
        if m:
            cur = (frame, m.group(1), [line])
            blocks.append(cur)
        elif cur is not None:
            cur[2].append(line)

def field(lines, name, cast=str, nth=0, default=None):
    """nth occurrence of '<name>: <type> = <value>'."""
    pat = re.compile(r"^\s*" + re.escape(name) + r":\s+.*? = (.*)$")
    k = 0
    for ln in lines:
        m = pat.match(ln)
        if m:
            if k == nth:
                v = m.group(1).strip()
                try:
                    return cast(v.split(" ")[0]) if cast is not str else v
                except ValueError:
                    return default
            k += 1
    return default

# bytes per texel for uncompressed formats, (block bytes, block w, block h) for compressed
BPT = {
    "R8_UNORM": 1, "R8G8_UNORM": 2, "R8G8B8A8_UNORM": 4, "R8G8B8A8_SRGB": 4,
    "B8G8R8A8_UNORM": 4, "B8G8R8A8_SRGB": 4, "A2B10G10R10_UNORM_PACK32": 4,
    "A2R10G10B10_UNORM_PACK32": 4, "B10G11R11_UFLOAT_PACK32": 4,
    "R16_SFLOAT": 2, "R16_UNORM": 2, "R16G16_SFLOAT": 4, "R16G16_UNORM": 4, "R16G16_SNORM": 4,
    "R16G16B16A16_SFLOAT": 8, "R16G16B16A16_UNORM": 8,
    "R32_SFLOAT": 4, "R32_UINT": 4, "R32G32_SFLOAT": 8, "R32G32B32A32_SFLOAT": 16,
    "D32_SFLOAT": 4, "D16_UNORM": 2, "D24_UNORM_S8_UINT": 4, "D32_SFLOAT_S8_UINT": 8,
    "R8_SNORM": 1, "R8G8B8A8_SNORM": 4, "R32G32B32_SFLOAT": 12,
}
BLOCK = {"BC1": 8, "BC2": 16, "BC3": 16, "BC4": 8, "BC5": 16, "BC6H": 16, "BC7": 16}

def image_bytes(fmt, w, h, d, mips, layers):
    f = fmt.replace("VK_FORMAT_", "")
    total = 0
    for m in range(mips):
        mw, mh, md = max(1, w >> m), max(1, h >> m), max(1, d >> m)
        blk = next((v for k, v in BLOCK.items() if f.startswith(k + "_")), None)
        if blk:
            total += ((mw + 3) // 4) * ((mh + 3) // 4) * md * blk
        else:
            total += mw * mh * md * BPT.get(f, 0)
    return total * layers

MB = 1024.0 * 1024.0

# ---------------------------------------------------------------- images
images = {}      # handle -> dict
img_req = {}
order = 0
for fr, name, ln in blocks:
    if name == "vkCreateImage":
        h = field(ln, "pImage")
        fmt = field(ln, "format").split(" ")[0]
        images[h] = dict(
            fmt=fmt, w=field(ln, "width", int), h=field(ln, "height", int), d=field(ln, "depth", int),
            mips=field(ln, "mipLevels", int), layers=field(ln, "arrayLayers", int),
            usage=re.sub(r"VK_IMAGE_USAGE_|_BIT", "", (field(ln, "usage") or "")),
            type=field(ln, "imageType").split(" ")[0].replace("VK_IMAGE_TYPE_", ""),
            frame=fr, order=order, alive=True)
        order += 1
    elif name in ("vkGetImageMemoryRequirements", "vkGetImageMemoryRequirements2"):
        h = field(ln, "image")
        sz = field(ln, "size", int)
        if h and sz:
            img_req[h] = sz
    elif name == "vkDestroyImage":
        h = field(ln, "image")
        if h in images:
            images[h]["alive"] = False

if want("images"):
    print("== IMAGES (alive at end of capture) ==")
    print(f"{'#':>3} {'format':28} {'type':3} {'extent':>16} {'mip':>3} {'lay':>3} {'calc MB':>9} {'drv MB':>9}  usage")
    tot_calc = tot_drv = 0
    rows = sorted((v for v in images.values() if v["alive"]), key=lambda v: v["order"])
    by_fmt = collections.Counter()
    for h, v in images.items():
        v["handle"] = h
    for v in rows:
        calc = image_bytes(v["fmt"], v["w"], v["h"], v["d"], v["mips"], v["layers"])
        drv = img_req.get(v["handle"], 0)
        tot_calc += calc
        tot_drv += drv
        by_fmt[v["fmt"].replace("VK_FORMAT_", "")] += max(calc, drv)
        ext = f"{v['w']}x{v['h']}" + (f"x{v['d']}" if v["d"] > 1 else "")
        print(f"{v['order']:>3} {v['fmt'].replace('VK_FORMAT_',''):28} {v['type']:3} {ext:>16} {v['mips']:>3} {v['layers']:>3} "
              f"{calc/MB:9.3f} {drv/MB:9.3f}  {v['usage'][:70]}")
    print(f"TOTAL images alive: {len(rows)}  calc {tot_calc/MB:.1f} MB  driver-reported {tot_drv/MB:.1f} MB")
    print("by format (max of calc/driver):")
    for k, v in by_fmt.most_common():
        print(f"   {k:28} {v/MB:9.2f} MB")

# ---------------------------------------------------------------- buffers
buffers = {}
for fr, name, ln in blocks:
    if name == "vkCreateBuffer":
        h = field(ln, "pBuffer")
        buffers[h] = dict(size=field(ln, "size", int),
                          usage=re.sub(r"VK_BUFFER_USAGE_|_BIT", "", (field(ln, "usage") or "")),
                          frame=fr, alive=True)
    elif name == "vkDestroyBuffer":
        h = field(ln, "buffer")
        if h in buffers:
            buffers[h]["alive"] = False

if want("buffers"):
    print("\n== BUFFERS (alive at end of capture), grouped by usage ==")
    grp = collections.defaultdict(lambda: [0, 0, 0])
    for b in buffers.values():
        if not b["alive"]:
            continue
        u = re.sub(r"^\d+ \(|\)$", "", b["usage"])
        g = grp[u]
        g[0] += 1
        g[1] += b["size"] or 0
        g[2] = max(g[2], b["size"] or 0)
    tot = 0
    for u, (n, sz, mx) in sorted(grp.items(), key=lambda kv: -kv[1][1]):
        tot += sz
        print(f"{n:5} bufs {sz/MB:10.3f} MB  (largest {mx/MB:8.3f} MB)  {u[:110]}")
    print(f"TOTAL buffers alive: {sum(g[0] for g in grp.values())}  {tot/MB:.1f} MB;  created {len(buffers)}, destroyed {sum(1 for b in buffers.values() if not b['alive'])}")

# ---------------------------------------------------------------- memory
if want("memory"):
    allocs = {}
    for fr, name, ln in blocks:
        if name == "vkAllocateMemory":
            h = field(ln, "pMemory")
            allocs[h] = dict(size=field(ln, "allocationSize", int), type=field(ln, "memoryTypeIndex", int), alive=True, frame=fr)
        elif name == "vkFreeMemory":
            h = field(ln, "memory")
            if h in allocs:
                allocs[h]["alive"] = False
    live = [a for a in allocs.values() if a["alive"]]
    print("\n== DEVICE MEMORY ALLOCATIONS (vkAllocateMemory) ==")
    print(f"allocations made {len(allocs)}, alive {len(live)}, total alive {sum(a['size'] for a in live)/MB:.1f} MB")
    byt = collections.defaultdict(lambda: [0, 0])
    for a in live:
        byt[a["type"]][0] += 1
        byt[a["type"]][1] += a["size"]
    for t, (n, sz) in sorted(byt.items()):
        print(f"   memoryTypeIndex {t}: {n:4} allocations, {sz/MB:9.1f} MB")
    sizes = sorted((a["size"] for a in live), reverse=True)
    print("   largest 25 (MB):", " ".join(f"{s/MB:.1f}" for s in sizes[:25]))
    small = [s for s in sizes if s < 65536]
    print(f"   allocations under 64 KiB: {len(small)} (sum {sum(small)/1024:.0f} KiB)")
    hist = collections.Counter()
    for s in sizes:
        b = 1
        while b < s:
            b <<= 1
        hist[b] += 1
    print("   size histogram (<= bucket: count):", ", ".join(f"{k if k < 1048576 else str(k // 1048576) + 'M'}:{v}" for k, v in sorted(hist.items())))

# ---------------------------------------------------------------- acceleration structures
if want("accel"):
    n = 0
    tot = 0
    types = collections.Counter()
    sizes = []
    for fr, name, ln in blocks:
        if name == "vkCreateAccelerationStructureKHR":
            n += 1
            sz = field(ln, "size", int) or 0
            tot += sz
            sizes.append(sz)
            types[(field(ln, "type") or "").split(" ")[0]] += 1
    print(f"\n== ACCELERATION STRUCTURES ==\ncreated {n}, total {tot/MB:.2f} MB, types {dict(types)}")
    if sizes:
        sizes.sort(reverse=True)
        print("   largest 10 (KiB):", " ".join(f"{s/1024:.0f}" for s in sizes[:10]), " median KiB:", sizes[len(sizes)//2] / 1024)
    flags = collections.Counter()
    scratch = 0
    for fr, name, ln in blocks:
        if name == "vkCmdBuildAccelerationStructuresKHR":
            flags[field(ln, "flags") or "?"] += 1
    for k, v in flags.most_common(5):
        print(f"   build flags x{v}: {k}")

# ---------------------------------------------------------------- pipelines / shaders
if want("pipelines"):
    sm = [field(ln, "codeSize", int) or 0 for fr, name, ln in blocks if name == "vkCreateShaderModule"]
    print(f"\n== SHADERS / PIPELINES ==\nshader modules {len(sm)}, SPIR-V total {sum(sm)/1024:.1f} KiB, largest {max(sm)/1024 if sm else 0:.1f} KiB")
    for nm in ("vkCreateGraphicsPipelines", "vkCreateComputePipelines", "vkCreateRayTracingPipelinesKHR",
               "vkCreatePipelineCache", "vkGetPipelineCacheData", "vkCreateSampler", "vkCreateDescriptorPool",
               "vkAllocateDescriptorSets", "vkCreateRenderPass", "vkCreateFramebuffer", "vkCreateImageView",
               "vkQueueSubmit", "vkQueueWaitIdle", "vkDeviceWaitIdle", "vkWaitForFences"):
        c0 = sum(1 for fr, name, ln in blocks if name == nm and fr == 0)
        c1 = sum(1 for fr, name, ln in blocks if name == nm and fr != 0)
        print(f"   {nm:36} init {c0:5}   steady frame {c1:4}")
    caches = collections.Counter(field(ln, "pipelineCache") for fr, name, ln in blocks
                                 if name in ("vkCreateGraphicsPipelines", "vkCreateComputePipelines"))
    print("   pipelineCache arg values:", dict(caches))

# ---------------------------------------------------------------- one steady frame
if want("frame"):
    frames = sorted({fr for fr, _, _ in blocks if fr != 0})
    for F in frames:
        c = collections.Counter(name for fr, name, ln in blocks if fr == F)
        print(f"\n== FRAME {F}: {sum(c.values())} Vulkan calls ==")
        for k, v in c.most_common():
            print(f"   {v:5}  {k}")
