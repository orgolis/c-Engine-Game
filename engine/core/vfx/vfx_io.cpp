// ============================================================================
// vfx_io.cpp — .vfx text serialisation. See header.
// ============================================================================

#include "vfx/vfx_io.h"

#include <fstream>
#include <sstream>

namespace schizo::vfx {
namespace {

std::string trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

float parse_float(const std::string& s, float fallback) {
    try { return std::stof(s); } catch (...) { return fallback; }
}

std::vector<float> parse_floats(const std::string& s) {
    std::vector<float> out;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, ',')) out.push_back(parse_float(trim(part), 0.0f));
    return out;
}

const char* stage_key(VfxStage s) {
    switch (s) {
        case VfxStage::Spawn: return "SPAWN";
        case VfxStage::Init:  return "INIT";
        default:              return "UPDATE";
    }
}

/// Curves and gradients are written as flat comma lists because the format is
/// line-based and a nested block would need a parser rather than a split.
/// Curve: time,value,in,out per key. Gradient: time,r,g,b,a per stop.
std::string curve_to_text(const gws::anim::Curve& c) {
    std::ostringstream o;
    for (size_t i = 0; i < c.keys().size(); ++i) {
        const auto& k = c.keys()[i];
        if (i) o << ",";
        o << k.time << "," << k.value << "," << k.in_tangent << "," << k.out_tangent;
    }
    return o.str();
}

std::string gradient_to_text(const gws::anim::Gradient& g) {
    std::ostringstream o;
    for (size_t i = 0; i < g.stops().size(); ++i) {
        const auto& s = g.stops()[i];
        if (i) o << ",";
        o << s.time << "," << s.color.r << "," << s.color.g << ","
          << s.color.b << "," << s.color.a;
    }
    return o.str();
}

void write_param(std::ostringstream& o, size_t index, const std::string& key,
                 const ParamValue& v) {
    o << "MODULE." << index << "." << key << "=";
    if (const float* f = std::get_if<float>(&v))                     o << *f;
    else if (const glm::vec3* p = std::get_if<glm::vec3>(&v))        o << p->x << "," << p->y << "," << p->z;
    else if (const glm::vec4* p = std::get_if<glm::vec4>(&v))        o << p->x << "," << p->y << "," << p->z << "," << p->w;
    else if (const auto* c = std::get_if<gws::anim::Curve>(&v))      o << "curve:" << curve_to_text(*c);
    else if (const auto* g = std::get_if<gws::anim::Gradient>(&v))   o << "grad:" << gradient_to_text(*g);
    o << "\n";
}

ParamValue parse_param(const std::string& val) {
    if (val.rfind("curve:", 0) == 0) {
        const std::vector<float> f = parse_floats(val.substr(6));
        std::vector<gws::anim::CurveKey> keys;
        for (size_t i = 0; i + 3 < f.size(); i += 4)
            keys.push_back({f[i], f[i + 1], f[i + 2], f[i + 3]});
        return gws::anim::Curve(keys);
    }
    if (val.rfind("grad:", 0) == 0) {
        const std::vector<float> f = parse_floats(val.substr(5));
        std::vector<gws::anim::GradientStop> stops;
        for (size_t i = 0; i + 4 < f.size(); i += 5)
            stops.push_back({f[i], glm::vec4(f[i + 1], f[i + 2], f[i + 3], f[i + 4])});
        return gws::anim::Gradient(stops);
    }
    const std::vector<float> f = parse_floats(val);
    if (f.size() >= 4) return glm::vec4(f[0], f[1], f[2], f[3]);
    if (f.size() == 3) return glm::vec3(f[0], f[1], f[2]);
    return f.empty() ? 0.0f : f[0];
}

}  // namespace

std::string vfx_to_text(const VfxGraph& g) {
    std::ostringstream o;
    o << "# GameWorldshaper VFX\n";
    o << "VFX_VERSION=" << kVfxFormatVersion << "\n";
    o << "NAME=" << g.name << "\n";
    o << "MAX_PARTICLES=" << g.max_particles << "\n";
    o << "WORLD_SPACE=" << (g.world_space ? 1 : 0) << "\n";

    size_t index = 0;
    const VfxStage stages[3] = {VfxStage::Spawn, VfxStage::Init, VfxStage::Update};
    for (VfxStage s : stages) {
        o << "STAGE=" << stage_key(s) << "\n";
        for (const VfxModule& m : g.stage(s)) {
            o << "MODULE." << index << ".KIND=" << module_kind_name(m.kind) << "\n";
            // std::map iterates sorted by key, so this is deterministic.
            for (const auto& kv : m.params) write_param(o, index, kv.first, kv.second);
            ++index;
        }
    }
    return o.str();
}

VfxGraph vfx_from_text(const std::string& text) {
    VfxGraph g;
    std::istringstream in(text);
    std::string line;

    VfxStage current = VfxStage::Spawn;
    // Modules are appended in FILE order. `pending` accumulates the keys of the
    // module currently being read; a new KIND line flushes the previous one.
    bool      have_pending = false;
    VfxModule pending;
    VfxStage  pending_stage = VfxStage::Spawn;

    const auto flush = [&]() {
        if (!have_pending) return;
        g.stage(pending_stage).push_back(pending);
        have_pending = false;
        pending = VfxModule();
    };

    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eq));
        const std::string val = trim(line.substr(eq + 1));

        if (key == "NAME")          { g.name = val; continue; }
        if (key == "MAX_PARTICLES") { g.max_particles = static_cast<uint32_t>(parse_float(val, 2048.0f)); continue; }
        if (key == "WORLD_SPACE")   { g.world_space = parse_float(val, 1.0f) != 0.0f; continue; }
        if (key == "STAGE") {
            flush();
            current = val == "SPAWN" ? VfxStage::Spawn
                    : val == "INIT"  ? VfxStage::Init
                                     : VfxStage::Update;
            continue;
        }
        if (key.rfind("MODULE.", 0) != 0) continue;   // VFX_VERSION, unknown keys

        // MODULE.<n>.<FIELD> — n is ignored on purpose (see header).
        const size_t dot = key.find('.', 7);
        if (dot == std::string::npos) continue;
        const std::string field = key.substr(dot + 1);

        if (field == "KIND") {
            flush();
            bool ok = false;
            const ModuleKind k = module_kind_from_name(val, ok);
            if (!ok) continue;          // unknown kind: skip it and its params
            pending = VfxModule();
            pending.kind  = k;
            pending_stage = current;
            have_pending  = true;
        } else if (have_pending) {
            pending.params[field] = parse_param(val);
        }
    }
    flush();
    return g;
}

bool load_vfx(const std::string& path, VfxGraph& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = vfx_from_text(ss.str());
    return true;
}

bool save_vfx(const std::string& path, const VfxGraph& g) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << vfx_to_text(g);
    return f.good();
}

}  // namespace schizo::vfx
