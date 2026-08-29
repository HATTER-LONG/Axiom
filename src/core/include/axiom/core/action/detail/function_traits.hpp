#pragma once

#include <functional>
#include <tuple>
#include <type_traits>

namespace axiom::core::detail {

template <typename Return, typename... Arguments> struct FunctionSignature {
    using ReturnType = Return;
    using ArgumentTypes = std::tuple<Arguments...>;
};

template <typename T, typename = void> struct FunctionTraits {};

/** @brief Identifies type-erased bindings that cannot expose their original lifetime model. */
template <typename T> struct IsTypeErasedCallable : std::false_type {};

template <typename Return, typename... Arguments>
struct IsTypeErasedCallable<std::function<Return(Arguments...)>> : std::true_type {};

template <typename Return, typename... Arguments>
struct FunctionTraits<Return (*)(Arguments...), void> : FunctionSignature<Return, Arguments...> {};

template <typename Return, typename... Arguments>
struct FunctionTraits<Return (*)(Arguments...) noexcept, void>
    : FunctionSignature<Return, Arguments...> {};

template <typename Class, typename Return, typename... Arguments>
struct FunctionTraits<Return (Class::*)(Arguments...), void>
    : FunctionSignature<Return, Arguments...> {};

template <typename Class, typename Return, typename... Arguments>
struct FunctionTraits<Return (Class::*)(Arguments...) const, void>
    : FunctionSignature<Return, Arguments...> {};

template <typename Class, typename Return, typename... Arguments>
struct FunctionTraits<Return (Class::*)(Arguments...) noexcept, void>
    : FunctionSignature<Return, Arguments...> {};

template <typename Class, typename Return, typename... Arguments>
struct FunctionTraits<Return (Class::*)(Arguments...) const noexcept, void>
    : FunctionSignature<Return, Arguments...> {};

template <typename T>
struct FunctionTraits<T, std::void_t<decltype((&std::remove_cvref_t<T>::operator()))>>
    : FunctionTraits<decltype((&std::remove_cvref_t<T>::operator()))> {};

template <typename T>
concept HasFunctionTraits = requires {
    typename FunctionTraits<std::remove_cvref_t<T>>::ReturnType;
    typename FunctionTraits<std::remove_cvref_t<T>>::ArgumentTypes;
};

} // namespace axiom::core::detail
