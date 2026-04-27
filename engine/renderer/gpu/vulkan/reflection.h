/**
 * @file reflection.h
 * @brief Reflection system for automatic property introspection (Phase 6 Week 22).
 *
 * Macro-based reflection allows automatic UI generation for component properties.
 * Used by Inspector panel to display editable properties without boilerplate.
 *
 * Usage:
 * ```cpp
 * class MyComponent : public Component {
 *     REFLECT_CLASS_BEGIN(MyComponent)
 *     REFLECT_PROPERTY(position, glm::vec3, "Position")
 *     REFLECT_PROPERTY(rotation, glm::quat, "Rotation")
 *     REFLECT_PROPERTY(scale, float, "Scale")
 *     REFLECT_PROPERTY(enabled, bool, "Enabled")
 *     REFLECT_CLASS_END()
 *
 * private:
 *     glm::vec3 position{0};
 *     glm::quat rotation{1,0,0,0};
 *     float scale{1.0f};
 *     bool enabled{true};
 * };
 * ```
 */

#pragma once

#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <typeinfo>
#include <any>
#include <unordered_map>

namespace gws::reflection {

enum class PropertyType {
    Float,
    Int,
    Bool,
    String,
    Vec2,      // glm::vec2
    Vec3,      // glm::vec3
    Vec4,      // glm::vec4
    Quat,      // glm::quat
    Mat4,      // glm::mat4
    Custom,    // User-defined
};

/**
 * @class Property
 * @brief Runtime property metadata and accessors.
 */
class Property {
public:
    Property(const std::string& name,
             PropertyType type,
             std::function<std::any()> getter,
             std::function<void(const std::any&)> setter)
        : name_(name), type_(type), getter_(getter), setter_(setter) {}

    const std::string& name() const { return name_; }
    PropertyType type() const { return type_; }

    std::any get() const { return getter_(); }
    void set(const std::any& value) { setter_(value); }

private:
    std::string name_;
    PropertyType type_;
    std::function<std::any()> getter_;
    std::function<void(const std::any&)> setter_;
};

/**
 * @class Class
 * @brief Runtime class metadata and property reflection.
 */
class Class {
public:
    Class(const std::string& name, const std::type_info& type_info)
        : name_(name), type_info_(type_info) {}

    const std::string& name() const { return name_; }
    const std::type_info& type_info() const { return type_info_.get(); }

    void add_property(const std::shared_ptr<Property>& prop) {
        properties_.push_back(prop);
        properties_by_name_[prop->name()] = prop;
    }

    const std::vector<std::shared_ptr<Property>>& properties() const {
        return properties_;
    }

    std::shared_ptr<Property> find_property(const std::string& name) const {
        auto it = properties_by_name_.find(name);
        return it != properties_by_name_.end() ? it->second : nullptr;
    }

private:
    std::string name_;
    std::reference_wrapper<const std::type_info> type_info_;
    std::vector<std::shared_ptr<Property>> properties_;
    std::unordered_map<std::string, std::shared_ptr<Property>> properties_by_name_;
};

/**
 * @class Registry
 * @brief Global reflection registry for all reflected classes.
 */
class Registry {
public:
    static Registry& instance();

    void register_class(const std::shared_ptr<Class>& cls) {
        classes_by_name_[cls->name()] = cls;
    }

    std::shared_ptr<Class> find_class(const std::string& name) const {
        auto it = classes_by_name_.find(name);
        return it != classes_by_name_.end() ? it->second : nullptr;
    }

    const std::unordered_map<std::string, std::shared_ptr<Class>>& classes() const {
        return classes_by_name_;
    }

private:
    Registry() = default;
    std::unordered_map<std::string, std::shared_ptr<Class>> classes_by_name_;
};

/**
 * @class ClassBuilder
 * @brief Helper for defining reflected classes.
 */
class ClassBuilder {
public:
    explicit ClassBuilder(const std::string& name, const std::type_info& type_info)
        : class_(std::make_shared<Class>(name, type_info)) {}

    ClassBuilder& add_property(const std::shared_ptr<Property>& prop) {
        class_->add_property(prop);
        return *this;
    }

    void finish() {
        Registry::instance().register_class(class_);
    }

    std::shared_ptr<Class> get() const { return class_; }

private:
    std::shared_ptr<Class> class_;
};

/**
 * Helper function to get property value as specific type.
 */
template<typename T>
T get_property_as(const std::any& value) {
    return std::any_cast<T>(value);
}

/**
 * Helper to convert property type to string.
 */
inline const char* property_type_name(PropertyType type) {
    switch (type) {
        case PropertyType::Float: return "Float";
        case PropertyType::Int: return "Int";
        case PropertyType::Bool: return "Bool";
        case PropertyType::String: return "String";
        case PropertyType::Vec2: return "Vec2";
        case PropertyType::Vec3: return "Vec3";
        case PropertyType::Vec4: return "Vec4";
        case PropertyType::Quat: return "Quat";
        case PropertyType::Mat4: return "Mat4";
        case PropertyType::Custom: return "Custom";
        default: return "Unknown";
    }
}

} // namespace gws::reflection

// Convenience macros for class reflection (simplified for now)
// Full macro system can be extended with code generation

#define REFLECT_CLASS_BEGIN(ClassName) \
    static std::shared_ptr<gws::reflection::Class> reflect_class() { \
        auto builder = gws::reflection::ClassBuilder(#ClassName, typeid(ClassName));

#define REFLECT_PROPERTY(field, type, display_name) \
        builder.add_property(std::make_shared<gws::reflection::Property>( \
            display_name, \
            gws::reflection::PropertyType::Custom, \
            [this]() { return std::any(field); }, \
            [this](const std::any& val) { field = std::any_cast<type>(val); } \
        ));

#define REFLECT_CLASS_END() \
        builder.finish(); \
        return builder.get(); \
    }
