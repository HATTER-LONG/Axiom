#pragma once

/**
 * @file type_descriptor.hpp
 * @brief Recursive value shape descriptors and validation.
 */

#include <axiom/core/base/result.hpp>
#include <axiom/core/export.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>

namespace axiom::core {

/**
 * @brief Describes the Value shape accepted by an Action parameter or result.
 *
 * Arrays own one element descriptor. Objects either declare an exact, ordered field
 * set or one homogeneous value descriptor for arbitrary string keys. A null Value
 * satisfies any descriptor marked nullable.
 */
struct TypeDescriptor {
    /** @brief Logical Value shapes supported by this descriptor. */
    enum class Kind : std::uint8_t { Null, Boolean, Integer, Number, String, Array, Object };

    /** @brief Named object fields and their required value shapes. */
    using Fields = std::map<std::string, std::shared_ptr<const TypeDescriptor>, std::less<>>;

    /** @brief Required non-null Value shape. */
    Kind kind{Kind::Null};
    /** @brief Whether a null Value is also accepted. */
    bool nullable{false};
    /** @brief Human-readable explanation of the expected value. */
    std::string description;
    /** @brief Array element descriptor; required only when kind is Array. */
    std::shared_ptr<const TypeDescriptor> element_type;
    /** @brief Object fields; permitted only for exact-field object descriptors. */
    Fields fields;
    /** @brief Homogeneous object member descriptor; mutually exclusive with fields. */
    std::shared_ptr<const TypeDescriptor> value_type;

    /** @brief Destroys the descriptor and all owned nested shapes. */
    ~TypeDescriptor() noexcept = default;

    /**
     * @brief Creates owned immutable storage for a nested descriptor.
     * @param descriptor Nested descriptor to copy.
     * @return Stable nested descriptor storage for an array element or object field.
     */
    [[nodiscard]] static std::shared_ptr<const TypeDescriptor>
    nested(const TypeDescriptor& descriptor) {
        return std::make_shared<const TypeDescriptor>(descriptor);
    }

    /**
     * @brief Creates an array descriptor.
     * @param element Descriptor for every array element.
     * @param description Human-readable explanation of the array.
     * @param nullable Whether a null Value is accepted.
     * @return An array descriptor with the supplied element descriptor.
     */
    [[nodiscard]] static TypeDescriptor
    array(const TypeDescriptor& element, std::string description = {}, bool nullable = false) {
        return {
            .kind = Kind::Array,
            .nullable = nullable,
            .description = std::move(description),
            .element_type = nested(element),
            .fields = {},
            .value_type = nullptr,
        };
    }

    /**
     * @brief Creates an object descriptor.
     * @param fields Named fields required in the object.
     * @param description Human-readable explanation of the object.
     * @param nullable Whether a null Value is accepted.
     * @return An object descriptor with the supplied fields.
     */
    [[nodiscard]] static TypeDescriptor
    object(Fields fields, std::string description = {}, bool nullable = false) {
        return {
            .kind = Kind::Object,
            .nullable = nullable,
            .description = std::move(description),
            .element_type = nullptr,
            .fields = std::move(fields),
            .value_type = nullptr,
        };
    }

    /**
     * @brief Creates an object descriptor for arbitrary string keys with one value shape.
     *
     * This describes C++ string-key maps, unlike object(), which requires the exact
     * supplied field set.
     *
     * @param value Descriptor for every object member value.
     * @param description Human-readable explanation of the object.
     * @param nullable Whether a null Value is accepted.
     * @return An object descriptor with a homogeneous member value descriptor.
     */
    [[nodiscard]] static TypeDescriptor
    objectValues(const TypeDescriptor& value, std::string description = {}, bool nullable = false) {
        return {
            .kind = Kind::Object,
            .nullable = nullable,
            .description = std::move(description),
            .element_type = nullptr,
            .fields = {},
            .value_type = nested(value),
        };
    }
};

/**
 * @brief Validates that a TypeDescriptor has a coherent recursive shape.
 *
 * @param descriptor Descriptor to inspect without modifying it.
 * @return Success when its nested descriptors, array element, and object fields
 *         are coherent; otherwise an InvalidDescriptor error.
 */
[[nodiscard]] AXIOM_CORE_API Result<void> validate(const TypeDescriptor& descriptor);

} // namespace axiom::core
