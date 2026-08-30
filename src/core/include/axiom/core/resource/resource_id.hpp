#pragma once

/**
 * @file resource_id.hpp
 * @brief Canonical, copyable identifier for a registered Core resource.
 */

#include <axiom/core/base/result.hpp>
#include <axiom/core/export.hpp>

#include <compare>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace axiom::core::resource {

/**
 * @brief Owns canonical resource-identity text of the form `<type>:<serial>`.
 *
 * A ResourceId stores only identity data. It does not hold an object address,
 * ownership, or a Registry association. There is no empty default identifier;
 * callers that need an optional identity use `std::optional<ResourceId>`.
 *
 * Canonical `type` matches `[a-z][a-z0-9_]*`. Canonical `serial` is the decimal
 * representation of a non-zero `std::uint64_t` with no leading zeros, signs,
 * whitespace, or overflow. Comparison is lexicographic on that canonical text.
 *
 * @note After a ResourceId is moved from, only destruction or reassignment is
 *       allowed. Reading a moved-from identifier (including `str()`) is undefined.
 * @note `std::hash` agrees with equality. Hash values are not stable across
 *       processes or Core instances.
 */
class AXIOM_CORE_API ResourceId {
public:
    ResourceId() = delete;

    ResourceId(const ResourceId&) = default;
    ResourceId(ResourceId&&) noexcept = default;
    ResourceId& operator=(const ResourceId&) = default;
    ResourceId& operator=(ResourceId&&) noexcept = default;
    ~ResourceId() = default;

    /**
     * @brief Parses canonical identity text.
     *
     * Validation is syntactic only: a successful parse does not mean a resource
     * exists, belongs to a Registry, or will remain valid after removal.
     *
     * @param text Candidate identity in `<type>:<serial>` form.
     * @return A ResourceId owning the canonical text, or InvalidArgument when
     *         @p text is not canonical.
     * @throws std::bad_alloc If the identifier storage cannot be allocated.
     */
    [[nodiscard]] static Result<ResourceId> parse(std::string_view text);

    /**
     * @brief Returns the canonical identity text.
     * @return View valid while this ResourceId remains alive and unmodified.
     */
    [[nodiscard]] std::string_view str() const noexcept { return text_; }

    /**
     * @brief Compares canonical identity text for equality.
     * @param other Identifier to compare with.
     * @return true when both identifiers have identical canonical text.
     */
    [[nodiscard]] bool operator==(const ResourceId& other) const noexcept = default;

    /**
     * @brief Orders identifiers lexicographically by canonical text.
     * @param other Identifier to compare with.
     * @return strong ordering of the canonical strings.
     */
    [[nodiscard]] std::strong_ordering
    operator<=>(const ResourceId& other) const noexcept = default;

private:
    explicit ResourceId(std::string text) : text_(std::move(text)) {}

    std::string text_;
};

} // namespace axiom::core::resource

// NOLINTBEGIN(bugprone-std-namespace-modification): C++ standard mandates std::hash.
template <> struct std::hash<axiom::core::resource::ResourceId> {
    /**
     * @brief Hashes canonical identity text.
     * @param id Identifier whose canonical text is hashed.
     * @return Hash of @p id; equal identifiers produce equal hashes.
     */
    [[nodiscard]] std::size_t
    operator()(const axiom::core::resource::ResourceId& id) const noexcept {
        return std::hash<std::string_view>{}(id.str());
    }
};
// NOLINTEND(bugprone-std-namespace-modification)
