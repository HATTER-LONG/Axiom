#pragma once

#include <cstddef>
#include <tuple>
#include <type_traits>

namespace axiom::runtime::detail {

/**
 * @brief Extracts ReturnType, ArgsTuple and ARITY from a callable type.
 *
 * Supported callables: plain function types, function pointers and references,
 * member function pointers (const/non-const, noexcept variants), and lambdas or
 * functors inspected through their operator(). Unsupported callables fail with a
 * static_assert at instantiation.
 */
template <typename F> struct FunctionTraits;

template <typename F, typename = void> struct FunctionTraitsImpl {
    static_assert(sizeof(F*) == 0,
                  "FunctionTraits supports only function types, function pointers, member "
                  "function pointers, and callables with operator()");
};

template <typename F>
struct FunctionTraitsImpl<F, std::void_t<decltype(&F::operator())>>
    : FunctionTraitsImpl<decltype(&F::operator())> {};

template <typename R, typename... Args> struct FunctionTraitsImpl<R(Args...)> {
    using ReturnType = R;
    using ArgsTuple = std::tuple<Args...>;
    static constexpr std::size_t ARITY = sizeof...(Args);
};

template <typename R, typename... Args>
struct FunctionTraitsImpl<R(Args...) noexcept> : FunctionTraitsImpl<R(Args...)> {};

template <typename R, typename... Args>
struct FunctionTraitsImpl<R (*)(Args...)> : FunctionTraitsImpl<R(Args...)> {};

template <typename R, typename... Args>
struct FunctionTraitsImpl<R (*)(Args...) noexcept> : FunctionTraitsImpl<R(Args...)> {};

template <typename R, typename... Args>
struct FunctionTraitsImpl<R (&)(Args...)> : FunctionTraitsImpl<R(Args...)> {};

template <typename R, typename... Args>
struct FunctionTraitsImpl<R (&)(Args...) noexcept> : FunctionTraitsImpl<R(Args...)> {};

template <typename C, typename R, typename... Args>
struct FunctionTraitsImpl<R (C::*)(Args...)> : FunctionTraitsImpl<R(Args...)> {};

template <typename C, typename R, typename... Args>
struct FunctionTraitsImpl<R (C::*)(Args...) const> : FunctionTraitsImpl<R(Args...)> {};

template <typename C, typename R, typename... Args>
struct FunctionTraitsImpl<R (C::*)(Args...) noexcept> : FunctionTraitsImpl<R(Args...)> {};

template <typename C, typename R, typename... Args>
struct FunctionTraitsImpl<R (C::*)(Args...) const noexcept> : FunctionTraitsImpl<R(Args...)> {};

template <typename F> struct FunctionTraits : FunctionTraitsImpl<F> {};

} // namespace axiom::runtime::detail
