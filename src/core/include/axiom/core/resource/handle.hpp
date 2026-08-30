#pragma once

/**
 * @file handle.hpp
 * @brief Copyable, typed identity token that names a resource without accessing it.
 */

#include <axiom/core/resource/resource_id.hpp>
#include <axiom/core/resource/resource_traits.hpp>

#include <compare>
#include <cstddef>
#include <functional>
#include <utility>

namespace axiom::core::resource {

/**
 * @brief Holds a ResourceId tagged with the expected C++ resource type.
 *
 * A Handle identifies a resource; it does not dereference it, keep it alive, or
 * store a Registry pointer. Constructing `Handle<T>` from a ResourceId does not
 * prove that the identifier names a live object of type `T`. Existence and type
 * safety are established later by Registry resolution.
 *
 * @tparam T Complete, non-cv, non-array object type with a non-throwing
 *           destructor and a valid ResourceTraits specialization. `Handle<const T>`
 *           is not supported. Type aliases name the same Handle specialization as
 *           their underlying type. There is no implicit conversion between
 *           `Handle<T>` and `Handle<U>` for distinct types. There is no empty
 *           default Handle; callers that need an optional handle use
 *           `std::optional<Handle<T>>`.
 *
 * @note After a Handle is moved from, only destruction or reassignment is
 *       allowed. Reading a moved-from handle (including `id()`) is undefined.
 */
template <typename T>
    requires detail::ResourceHandleTarget<T>
class Handle {
public:
    Handle() = delete;

    /**
     * @brief Constructs a handle that names @p id as type @tparam T.
     * @param id Canonical identity to store. Copied into this handle.
     */
    explicit Handle(ResourceId id) : id_(std::move(id)) {}

    Handle(const Handle&) = default;
    Handle(Handle&&) noexcept = default;
    Handle& operator=(const Handle&) = default;
    Handle& operator=(Handle&&) noexcept = default;
    ~Handle() = default;

    /**
     * @brief Returns the stored identity.
     * @return Reference valid while this Handle remains alive and unmodified.
     */
    [[nodiscard]] const ResourceId& id() const noexcept { return id_; }

    /**
     * @brief Compares stored identities for equality.
     * @param other Handle of the same type to compare with.
     * @return true when both handles store equal ResourceId values.
     */
    [[nodiscard]] bool operator==(const Handle& other) const noexcept = default;

    /**
     * @brief Orders handles by the lexicographic order of their ResourceId text.
     * @param other Handle of the same type to compare with.
     * @return strong ordering of the stored identifiers.
     */
    [[nodiscard]] std::strong_ordering operator<=>(const Handle& other) const noexcept = default;

private:
    ResourceId id_;
};

} // namespace axiom::core::resource

// NOLINTBEGIN(bugprone-std-namespace-modification): C++ standard mandates std::hash.
template <typename T> struct std::hash<axiom::core::resource::Handle<T>> {
    /**
     * @brief Hashes the identity stored by @p handle.
     * @param handle Handle whose ResourceId is hashed.
     * @return Hash of the stored identifier; equal handles produce equal hashes.
     */
    [[nodiscard]] std::size_t
    operator()(const axiom::core::resource::Handle<T>& handle) const noexcept {
        return std::hash<axiom::core::resource::ResourceId>{}(handle.id());
    }
};
// NOLINTEND(bugprone-std-namespace-modification)
