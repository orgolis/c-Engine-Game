#ifndef TINYGLTF_HEADER_ONLY_DEFINITION
#define TINYGLTF_HEADER_ONLY_DEFINITION

#include <string>
#include <vector>
#include <map>
#include <cstring>
#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <memory>

#ifndef TINYGLTF_NOEXCEPTION
#define TINYGLTF_NOEXCEPTION 0
#endif

// ============================================================================
// tinygltf - Tiny glTF 2.0 Loader
// ============================================================================
// Simplified glTF 2.0 loader for loading models exported from Blender
// Supports both .gltf (JSON with separate bin) and .glb (binary container)
// ============================================================================

namespace tinygltf {

// Forward declarations
class Model;
class Node;
class Mesh;
class Primitive;
class Accessor;
class BufferView;
class Buffer;
class Asset;

// JSON parsing utility
class json {
public:
    enum Type {
        NULL_TYPE,
        OBJECT_TYPE,
        ARRAY_TYPE,
        STRING_TYPE,
        NUMBER_TYPE,
        BOOLEAN_TYPE
    };

    Type type;
    std::string string_value;
    double number_value = 0.0;
    bool bool_value = false;
    std::map<std::string, json> object_value;
    std::vector<json> array_value;

    json() : type(NULL_TYPE) {}
    explicit json(Type t) : type(t) {}

    bool Has(const std::string& key) const {
        return object_value.find(key) != object_value.end();
    }

    const json& Get(const std::string& key) const {
        static json null_json;
        auto it = object_value.find(key);
        return it != object_value.end() ? it->second : null_json;
    }

    double GetNumberAsDouble(const std::string& key, double default_value = 0.0) const {
        if (Has(key)) {
            const auto& v = Get(key);
            if (v.type == NUMBER_TYPE) return v.number_value;
        }
        return default_value;
    }

    int GetNumberAsInt(const std::string& key, int default_value = 0) const {
        return static_cast<int>(GetNumberAsDouble(key, default_value));
    }

    std::string GetStringAsString(const std::string& key, const std::string& default_value = "") const {
        if (Has(key)) {
            const auto& v = Get(key);
            if (v.type == STRING_TYPE) return v.string_value;
        }
        return default_value;
    }

    const std::vector<json>& GetArrayAsArray(const std::string& key) const {
        static std::vector<json> empty;
        if (Has(key)) {
            const auto& v = Get(key);
            if (v.type == ARRAY_TYPE) return v.array_value;
        }
        return empty;
    }
};

// Basic value structures
struct Value {
    json json_value;
    std::string string_value;
    int int_value = 0;
    double number_value = 0.0;

    bool Has(const std::string& key) const { return json_value.Has(key); }
    const Value& Get(const std::string& key) const {
        static Value null_val;
        return null_val;
    }
};

// Model component structures
struct Accessor {
    int bufferView = -1;
    int byteOffset = 0;
    int componentType = 0; // 5120-BYTE, 5121-UNSIGNED_BYTE, etc
    int count = 0;
    std::string type; // "SCALAR", "VEC2", "VEC3", "VEC4", "MAT2", "MAT3", "MAT4"
    std::vector<double> maxValues;
    std::vector<double> minValues;
    std::vector<uint8_t> sparse; // For sparse accessors
};

struct BufferView {
    int buffer = 0;
    int byteOffset = 0;
    int byteLength = 0;
    int byteStride = 0;
    int target = 0; // 34962=ARRAY_BUFFER, 34963=ELEMENT_ARRAY_BUFFER
};

struct Buffer {
    std::vector<uint8_t> data;
    int byteLength = 0;
    std::string name;
    std::string uri;
};

struct Primitive {
    std::map<std::string, int> attributes; // "POSITION", "NORMAL", etc -> accessor index
    int indices = -1;
    int material = -1;
    int mode = 4; // 4=TRIANGLES (default)
};

struct Mesh {
    std::string name;
    std::vector<Primitive> primitives;
};

struct Node {
    std::string name;
    int mesh = -1;
    int parent = -1;
    std::vector<int> children;
    std::vector<float> translation = {0, 0, 0};
    std::vector<float> rotation = {0, 0, 0, 1};
    std::vector<float> scale = {1, 1, 1};
    std::vector<float> matrix;
};

struct Scene {
    std::vector<int> nodes;
};

struct Asset {
    std::string version = "2.0";
    std::string generator;
};

struct Model {
    Asset asset;
    std::vector<Accessor> accessors;
    std::vector<BufferView> bufferViews;
    std::vector<Buffer> buffers;
    std::vector<Mesh> meshes;
    std::vector<Node> nodes;
    std::vector<Scene> scenes;
    int scene = 0;
    std::string path;

    std::vector<uint8_t> GetAccessorData(int accessor_idx) const {
        if (accessor_idx < 0 || accessor_idx >= accessors.size()) {
            return {};
        }

        const auto& accessor = accessors[accessor_idx];
        if (accessor.bufferView < 0 || accessor.bufferView >= bufferViews.size()) {
            return {};
        }

        const auto& bufferView = bufferViews[accessor.bufferView];
        if (bufferView.buffer < 0 || bufferView.buffer >= buffers.size()) {
            return {};
        }

        const auto& buffer = buffers[bufferView.buffer];
        int stride = bufferView.byteStride > 0 ? bufferView.byteStride : GetAccessorComponentCount(accessor) * GetAccessorComponentSize(accessor);
        int start = bufferView.byteOffset + accessor.byteOffset;
        int size = accessor.count * stride;

        std::vector<uint8_t> result;
        if (start + size <= buffer.data.size()) {
            result.insert(result.end(), buffer.data.begin() + start, buffer.data.begin() + start + size);
        }
        return result;
    }

    static int GetAccessorComponentCount(const Accessor& accessor) {
        if (accessor.type == "SCALAR") return 1;
        if (accessor.type == "VEC2") return 2;
        if (accessor.type == "VEC3") return 3;
        if (accessor.type == "VEC4") return 4;
        if (accessor.type == "MAT2") return 4;
        if (accessor.type == "MAT3") return 9;
        if (accessor.type == "MAT4") return 16;
        return 1;
    }

   static int GetAccessorComponentSize(const Accessor& accessor) {
        switch (accessor.componentType) {
            case 5120: return 1; // BYTE
            case 5121: return 1; // UNSIGNED_BYTE
            case 5122: return 2; // SHORT
            case 5123: return 2; // UNSIGNED_SHORT
            case 5125: return 4; // UNSIGNED_INT
            case 5126: return 4; // FLOAT
            default: return 1;
        }
    }
};

// ============================================================================
// Simple JSON Parser (minimal implementation)
// ============================================================================

class JsonParser {
public:
    static bool Parse(const std::string& json_str, tinygltf::json& out_json) {
        size_t pos = 0;
        return ParseValue(json_str, pos, out_json);
    }

private:
    static bool ParseValue(const std::string& str, size_t& pos, tinygltf::json& out_value) {
        SkipWhitespace(str, pos);
        
        if (pos >= str.length()) return false;

        char c = str[pos];
        if (c == '{') {
            out_value.type = tinygltf::json::OBJECT_TYPE;
            return ParseObject(str, pos, out_value);
        } else if (c == '[') {
            out_value.type = tinygltf::json::ARRAY_TYPE;
            return ParseArray(str, pos, out_value);
        } else if (c == '"') {
            out_value.type = tinygltf::json::STRING_TYPE;
            return ParseString(str, pos, out_value.string_value);
        } else if (c == 't' || c == 'f') {
            out_value.type = tinygltf::json::BOOLEAN_TYPE;
            if (str.substr(pos, 4) == "true") {
                out_value.bool_value = true;
                pos += 4;
                return true;
            } else if (str.substr(pos, 5) == "false") {
                out_value.bool_value = false;
                pos += 5;
                return true;
            }
            return false;
        } else if (c == 'n') {
            if (str.substr(pos, 4) == "null") {
                out_value.type = tinygltf::json::NULL_TYPE;
                pos += 4;
                return true;
            }
            return false;
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            out_value.type = tinygltf::json::NUMBER_TYPE;
            return ParseNumber(str, pos, out_value.number_value);
        }
        return false;
    }

    static bool ParseObject(const std::string& str, size_t& pos, tinygltf::json& out_object) {
        pos++; // skip '{'
        SkipWhitespace(str, pos);

        if (pos < str.length() && str[pos] == '}') {
            pos++;
            return true;
        }

        while (pos < str.length()) {
            SkipWhitespace(str, pos);
            
            std::string key;
            if (!ParseString(str, pos, key)) return false;

            SkipWhitespace(str, pos);
            if (pos >= str.length() || str[pos] != ':') return false;
            pos++; // skip ':'

            tinygltf::json value;
            if (!ParseValue(str, pos, value)) return false;

            out_object.object_value[key] = value;

            SkipWhitespace(str, pos);
            if (pos >= str.length()) return false;

            if (str[pos] == '}') {
                pos++;
                return true;
            } else if (str[pos] == ',') {
                pos++;
                continue;
            }
            return false;
        }
        return false;
    }

    static bool ParseArray(const std::string& str, size_t& pos, tinygltf::json& out_array) {
        pos++; // skip '['
        SkipWhitespace(str, pos);

        if (pos < str.length() && str[pos] == ']') {
            pos++;
            return true;
        }

        while (pos < str.length()) {
            tinygltf::json value;
            if (!ParseValue(str, pos, value)) return false;
            out_array.array_value.push_back(value);

            SkipWhitespace(str, pos);
            if (pos >= str.length()) return false;

            if (str[pos] == ']') {
                pos++;
                return true;
            } else if (str[pos] == ',') {
                pos++;
                continue;
            }
            return false;
        }
        return false;
    }

    static bool ParseString(const std::string& str, size_t& pos, std::string& out_string) {
        if (pos >= str.length() || str[pos] != '"') return false;
        pos++; // skip opening quote

        out_string.clear();
        while (pos < str.length()) {
            char c = str[pos];
            if (c == '"') {
                pos++; // skip closing quote
                return true;
            } else if (c == '\\') {
                if (pos + 1 >= str.length()) return false;
                pos++;
                char escape_char = str[pos];
                switch (escape_char) {
                    case '"': out_string += '"'; break;
                    case '\\': out_string += '\\'; break;
                    case '/': out_string += '/'; break;
                    case 'b': out_string += '\b'; break;
                    case 'f': out_string += '\f'; break;
                    case 'n': out_string += '\n'; break;
                    case 'r': out_string += '\r'; break;
                    case 't': out_string += '\t'; break;
                    default: out_string += escape_char;
                }
            } else {
                out_string += c;
            }
            pos++;
        }
        return false;
    }

    static bool ParseNumber(const std::string& str, size_t& pos, double& out_number) {
        size_t start = pos;
        if (pos < str.length() && str[pos] == '-') pos++;
        
        while (pos < str.length() && str[pos] >= '0' && str[pos] <= '9') pos++;
        if (pos < str.length() && str[pos] == '.') {
            pos++;
            while (pos < str.length() && str[pos] >= '0' && str[pos] <= '9') pos++;
        }
        if (pos < str.length() && (str[pos] == 'e' || str[pos] == 'E')) {
            pos++;
            if (pos < str.length() && (str[pos] == '+' || str[pos] == '-')) pos++;
            while (pos < str.length() && str[pos] >= '0' && str[pos] <= '9') pos++;
        }

        try {
            out_number = std::stod(str.substr(start, pos - start));
            return true;
        } catch (...) {
            return false;
        }
    }

    static void SkipWhitespace(const std::string& str, size_t& pos) {
        while (pos < str.length() && (str[pos] == ' ' || str[pos] == '\t' || str[pos] == '\n' || str[pos] == '\r')) {
            pos++;
        }
    }
};

// ============================================================================
// TinyGLTF Loader Class
// ============================================================================

class TinyGLTF {
public:
    bool LoadASCIIFromFile(Model* model, std::string* err, std::string* warn, const std::string& filename) {
        std::ifstream ifs(filename, std::ios::binary);
        if (!ifs) {
            if (err) *err = "Cannot open file: " + filename;
            return false;
        }

        std::stringstream ss;
        ss << ifs.rdbuf();
        std::string json_str = ss.str();

        tinygltf::json json_data;
        if (!JsonParser::Parse(json_str, json_data)) {
            if (err) *err = "Failed to parse JSON";
            return false;
        }

        return LoadFromJson(model, json_data, filename, err, warn);
    }

    bool LoadBinaryFromFile(Model* model, std::string* err, std::string* warn, const std::string& filename) {
        std::ifstream ifs(filename, std::ios::binary);
        if (!ifs) {
            if (err) *err = "Cannot open file: " + filename;
            return false;
        }

        // Read glTF binary header
        uint32_t magic;
        ifs.read(reinterpret_cast<char*>(&magic), 4);
        if (magic != 0x46546C67) { // 'glTF'
            if (err) *err = "Invalid glTF binary file";
            return false;
        }

        uint32_t version;
        ifs.read(reinterpret_cast<char*>(&version), 4);
        if (version != 2) {
            if (err) *err = "Only glTF 2.0 is supported";
            return false;
        }

        uint32_t length;
        ifs.read(reinterpret_cast<char*>(&length), 4);

        // Read JSON chunk
        uint32_t chunkLength;
        uint32_t chunkType;
        ifs.read(reinterpret_cast<char*>(&chunkLength), 4);
        ifs.read(reinterpret_cast<char*>(&chunkType), 4);

        if (chunkType != 0x4E4F534A) { // 'JSON'
            if (err) *err = "First chunk is not JSON";
            return false;
        }

        std::vector<char> json_data_raw(chunkLength);
        ifs.read(json_data_raw.data(), chunkLength);
        std::string json_str(json_data_raw.begin(), json_data_raw.end());

        tinygltf::json json_data;
        if (!JsonParser::Parse(json_str, json_data)) {
            if (err) *err = "Failed to parse glTF JSON";
            return false;
        }

        // Read BIN chunk
        ifs.read(reinterpret_cast<char*>(&chunkLength), 4);
        ifs.read(reinterpret_cast<char*>(&chunkType), 4);

        if (chunkType == 0x004E4942) { // 'BIN\0'
            std::vector<uint8_t> bin_data(chunkLength);
            ifs.read(reinterpret_cast<char*>(bin_data.data()), chunkLength);

            // Store binary data in the model
            if (!model->buffers.empty()) {
                model->buffers[0].data = bin_data;
            }
        }

        return LoadFromJson(model, json_data, filename, err, warn);
    }

private:
    bool LoadFromJson(Model* model, const tinygltf::json& json_data, const std::string& filename, std::string* err, std::string* warn) {
        model->path = filename;

        // Parse asset
        if (json_data.Has("asset")) {
            const auto& asset_json = json_data.Get("asset");
            model->asset.version = asset_json.GetStringAsString("version", "2.0");
        }

        // Parse buffers
        if (json_data.Has("buffers")) {
            const auto& buffers_json = json_data.GetArrayAsArray("buffers");
            for (const auto& buf_json : buffers_json) {
                Buffer buffer;
                buffer.name = buf_json.GetStringAsString("name", "");
                buffer.uri = buf_json.GetStringAsString("uri", "");
                buffer.byteLength = buf_json.GetNumberAsInt("byteLength", 0);
                
                // Load external binary file if URI is provided
                if (!buffer.uri.empty() && buffer.uri.find("data:") == std::string::npos) {
                    std::string bin_path = filename.substr(0, filename.find_last_of("/\\") + 1) + buffer.uri;
                    std::ifstream bin_file(bin_path, std::ios::binary);
                    if (bin_file) {
                        bin_file.seekg(0, std::ios::end);
                        size_t file_size = bin_file.tellg();
                        bin_file.seekg(0, std::ios::beg);
                        buffer.data.resize(file_size);
                        bin_file.read(reinterpret_cast<char*>(buffer.data.data()), file_size);
                    }
                }
                model->buffers.push_back(buffer);
            }
        }

        // Parse buffer views
        if (json_data.Has("bufferViews")) {
            const auto& views_json = json_data.GetArrayAsArray("bufferViews");
            for (const auto& view_json : views_json) {
                BufferView view;
                view.buffer = view_json.GetNumberAsInt("buffer", 0);
                view.byteOffset = view_json.GetNumberAsInt("byteOffset", 0);
                view.byteLength = view_json.GetNumberAsInt("byteLength", 0);
                view.byteStride = view_json.GetNumberAsInt("byteStride", 0);
                view.target = view_json.GetNumberAsInt("target", 0);
                model->bufferViews.push_back(view);
            }
        }

        // Parse accessors
        if (json_data.Has("accessors")) {
            const auto& accessors_json = json_data.GetArrayAsArray("accessors");
            for (const auto& acc_json : accessors_json) {
                Accessor accessor;
                accessor.bufferView = acc_json.GetNumberAsInt("bufferView", -1);
                accessor.byteOffset = acc_json.GetNumberAsInt("byteOffset", 0);
                accessor.componentType = acc_json.GetNumberAsInt("componentType", 5126);
                accessor.count = acc_json.GetNumberAsInt("count", 0);
                accessor.type = acc_json.GetStringAsString("type", "SCALAR");
                model->accessors.push_back(accessor);
            }
        }

        // Parse meshes
        if (json_data.Has("meshes")) {
            const auto& meshes_json = json_data.GetArrayAsArray("meshes");
            for (const auto& mesh_json : meshes_json) {
                Mesh mesh;
                mesh.name = mesh_json.GetStringAsString("name", "");
                
                if (mesh_json.Has("primitives")) {
                    const auto& prims_json = mesh_json.GetArrayAsArray("primitives");
                    for (const auto& prim_json : prims_json) {
                        Primitive primitive;
                        primitive.indices = prim_json.GetNumberAsInt("indices", -1);
                        primitive.material = prim_json.GetNumberAsInt("material", -1);
                        primitive.mode = prim_json.GetNumberAsInt("mode", 4);

                        if (prim_json.Has("attributes")) {
                            const auto& attr_json = prim_json.Get("attributes");
                            for (const auto& attr_pair : attr_json.object_value) {
                                primitive.attributes[attr_pair.first] = static_cast<int>(attr_pair.second.number_value);
                            }
                        }
                        mesh.primitives.push_back(primitive);
                    }
                }
                model->meshes.push_back(mesh);
            }
        }

        // Parse nodes
        if (json_data.Has("nodes")) {
            const auto& nodes_json = json_data.GetArrayAsArray("nodes");
            for (const auto& node_json : nodes_json) {
                Node node;
                node.name = node_json.GetStringAsString("name", "");
                node.mesh = node_json.GetNumberAsInt("mesh", -1);

                if (node_json.Has("translation")) {
                    const auto& trans = node_json.GetArrayAsArray("translation");
                    if (trans.size() >= 3) {
                        node.translation = {(float)trans[0].number_value, (float)trans[1].number_value, (float)trans[2].number_value};
                    }
                }
                if (node_json.Has("rotation")) {
                    const auto& rot = node_json.GetArrayAsArray("rotation");
                    if (rot.size() >= 4) {
                        node.rotation = {(float)rot[0].number_value, (float)rot[1].number_value, (float)rot[2].number_value, (float)rot[3].number_value};
                    }
                }
                if (node_json.Has("scale")) {
                    const auto& scl = node_json.GetArrayAsArray("scale");
                    if (scl.size() >= 3) {
                        node.scale = {(float)scl[0].number_value, (float)scl[1].number_value, (float)scl[2].number_value};
                    }
                }
                if (node_json.Has("children")) {
                    const auto& children = node_json.GetArrayAsArray("children");
                    for (const auto& child : children) {
                        node.children.push_back(static_cast<int>(child.number_value));
                    }
                }

                model->nodes.push_back(node);
            }
        }

        // Parse scenes
        if (json_data.Has("scenes")) {
            const auto& scenes_json = json_data.GetArrayAsArray("scenes");
            for (const auto& scene_json : scenes_json) {
                Scene scene;
                if (scene_json.Has("nodes")) {
                    const auto& scene_nodes = scene_json.GetArrayAsArray("nodes");
                    for (const auto& node_idx : scene_nodes) {
                        scene.nodes.push_back(static_cast<int>(node_idx.number_value));
                    }
                }
                model->scenes.push_back(scene);
            }
        }

        if (json_data.Has("scene")) {
            model->scene = json_data.GetNumberAsInt("scene", 0);
        }

        return true;
    }
};

} // namespace tinygltf

#endif // TINYGLTF_HEADER_ONLY_DEFINITION
