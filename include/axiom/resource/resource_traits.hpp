#pragma once

/**
 * @file resource_traits.hpp
 * @brief Compile-time mapping from a C++ resource type to a stable logical name.
 */

#include <algorithm>
#include <concepts>
#include <string_view>
#include <type_traits>

namespace axiom::resource {

/**
 * @brief Associates a C++ resource type with a stable logical type name.
 *
 * Specialize this template for each resource type. The specialization must
 * provide `static constexpr std::string_view type_name` whose value matches
 * `[a-z][a-z0-9_]*`. Names are chosen by the domain module; they must not be
 * derived from RTTI display names or compiler type signatures.
 *
 * The primary template is intentionally undefined. Missing specializations and
 * illegal names are rejected when forming a Handle (and any other constrained
 * Resource API) at compile time.
 *
 * @tparam T Complete resource type being named. Type aliases name the same C++
 *           type as their underlying type.
 */
template <typename T> struct ResourceTraits;

namespace detail {

/** @brief True when @p name matches `[a-z][a-z0-9_]*`. Shared with ResourceId::parse. */
[[nodiscard]] constexpr bool isCanonicalTypeName(const std::string_view name) noexcept {
    if(name.empty() || name.front() < 'a' || name.front() > 'z') {
        return false;
    }
    const auto rest = name.substr(1U);
    return std::ranges::all_of(rest, [](const char character) noexcept {
        return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
               character == '_';
    });
}

template <typename T>
concept CompleteNonCvObject = requires { sizeof(T); } && std::is_object_v<T> &&
                              !std::is_array_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T>;

template <typename T>
concept HasValidResourceTraits = requires {
    requires std::same_as<std::remove_cv_t<decltype(ResourceTraits<T>::type_name)>,
                          std::string_view>;
} && isCanonicalTypeName(ResourceTraits<T>::type_name);

/**
 * @brief True when @tparam T may be used as a Handle (and later Registry) type argument.
 */
template <typename T>
concept ResourceHandleTarget =
    CompleteNonCvObject<T> && std::is_nothrow_destructible_v<T> && HasValidResourceTraits<T>;

} // namespace detail

} // namespace axiom::resource
