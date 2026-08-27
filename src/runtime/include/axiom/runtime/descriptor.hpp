#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <axiom/runtime/value.hpp>

namespace axiom::runtime {

/** @brief Self-description of a single action parameter. */
struct ParameterDescriptor {
    std::string m_name;
    ValueType m_type = ValueType::Null;
    std::string m_description;
    bool m_required = true;
    std::optional<Value> m_defaultValue;
};

/** @brief Self-description of a registered action. */
struct ActionDescriptor {
    std::string m_name;
    std::string m_description;
    std::vector<ParameterDescriptor> m_parameters;
    ValueType m_returnType = ValueType::Null;
    std::string m_returnDescription;
    std::string m_version;
    std::vector<std::string> m_tags;
};

/**
 * @brief Self-description of a module.
 *
 * Does not embed action descriptors: actions are owned by the Registry, so Module does not
 * become a god object.
 */
struct ModuleDescriptor {
    std::string m_name;
    std::string m_description;
    std::string m_version;
};

/**
 * @brief Compile-time mapping from C++ types to runtime value types.
 *
 * Left undefined so that unsupported types fail loudly at instantiation.
 * The trait constant is named VALUE because repository naming rules require
 * UPPER_CASE for static constexpr constants.
 */
template <typename T> struct ValueTypeOf {
    static_assert(sizeof(T) == 0, "ValueTypeOf has no mapping for this type");
};

template <> struct ValueTypeOf<bool> {
    static constexpr ValueType VALUE = ValueType::Boolean;
};

template <> struct ValueTypeOf<int> {
    static constexpr ValueType VALUE = ValueType::Integer;
};

template <> struct ValueTypeOf<std::int64_t> {
    static constexpr ValueType VALUE = ValueType::Integer;
};

template <> struct ValueTypeOf<float> {
    static constexpr ValueType VALUE = ValueType::Number;
};

template <> struct ValueTypeOf<double> {
    static constexpr ValueType VALUE = ValueType::Number;
};

template <> struct ValueTypeOf<std::string> {
    static constexpr ValueType VALUE = ValueType::String;
};

template <> struct ValueTypeOf<const char*> {
    static constexpr ValueType VALUE = ValueType::String;
};

template <> struct ValueTypeOf<void> {
    static constexpr ValueType VALUE = ValueType::Null;
};

template <typename T> struct ValueTypeOf<std::vector<T>> {
    static constexpr ValueType VALUE = ValueType::Array;
};

template <typename T> struct ValueTypeOf<std::unordered_map<std::string, T>> {
    static constexpr ValueType VALUE = ValueType::Object;
};

/** @brief Builds a required parameter descriptor for the given C++ type. */
template <typename T> ParameterDescriptor param(std::string name, std::string description = {}) {
    return ParameterDescriptor{.m_name = std::move(name),
                               .m_type = ValueTypeOf<T>::VALUE,
                               .m_description = std::move(description),
                               .m_required = true};
}

/** @brief Builds an optional parameter descriptor for the given C++ type. */
template <typename T>
ParameterDescriptor optionalParam(std::string name, std::string description = {}) {
    return ParameterDescriptor{.m_name = std::move(name),
                               .m_type = ValueTypeOf<T>::VALUE,
                               .m_description = std::move(description),
                               .m_required = false};
}

} // namespace axiom::runtime
