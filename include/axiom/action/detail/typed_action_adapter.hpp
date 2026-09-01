#pragma once

/** @file typed_action_adapter.hpp
 * @brief Callable adaptation machinery required by ModuleBuilder templates.
 * @note These detail declarations are not supported public extension points.
 */

#include <axiom/action/descriptor.hpp>
#include <axiom/action/detail/action.hpp>
#include <axiom/action/detail/value_converter.hpp>
#include <axiom/action/invocation_context.hpp>
#include <axiom/foundation/error.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/foundation/type_descriptor.hpp>
#include <axiom/foundation/value.hpp>

#include <concepts>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace axiom::detail {

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

template <typename T> struct ResultValue {
    static constexpr bool is_result = false;
    using ValueType = T;
};

template <typename T> struct ResultValue<Result<T>> {
    static constexpr bool is_result = true;
    using ValueType = T;
};

template <typename T>
inline constexpr bool is_adapted_return = [] {
    using Type = std::remove_cvref_t<T>;
    if constexpr(std::is_void_v<Type>) {
        return true;
    } else if constexpr(ResultValue<Type>::is_result) {
        using ResultType = ResultValue<Type>::ValueType;
        return std::is_void_v<ResultType> || ValueConvertible<ResultType>;
    } else {
        return ValueConvertible<Type>;
    }
}();

template <typename Tuple, std::size_t... Index>
consteval bool areArgumentTypesConvertible(std::index_sequence<Index...> index_sequence) {
    static_cast<void>(index_sequence);
    return (ValueConvertible<std::remove_cvref_t<std::tuple_element_t<Index, Tuple>>> && ...);
}

template <typename Callable> consteval bool isAdaptableCallable() {
    using StoredCallable = std::decay_t<Callable>;
    if constexpr(!HasFunctionTraits<StoredCallable> || !std::copy_constructible<StoredCallable> ||
                 IsTypeErasedCallable<StoredCallable>::value) {
        return false;
    } else {
        using Traits = FunctionTraits<StoredCallable>;
        using Arguments = Traits::ArgumentTypes;
        using Return = Traits::ReturnType;
        constexpr auto count = std::tuple_size_v<Arguments>;
        return areArgumentTypesConvertible<Arguments>(std::make_index_sequence<count>{}) &&
               is_adapted_return<Return> &&
               []<std::size_t... Index>(std::index_sequence<Index...> index_sequence) {
                   static_cast<void>(index_sequence);
                   return std::invocable<
                       StoredCallable&,
                       std::remove_cvref_t<std::tuple_element_t<Index, Arguments>>&...>;
               }(std::make_index_sequence<count>{});
    }
}

/**
 * @brief Constrains callables that can be stored and invoked by TypedActionAdapter.
 *
 * The predicate models the adapter's actual call expression: a decayed callable is
 * invoked with mutable lvalue storage produced from each converted Value.
 *
 * @tparam Callable Candidate callable type before storage decay.
 */
template <typename Callable>
concept AdaptableCallable = isAdaptableCallable<std::decay_t<Callable>>();

template <typename T> [[nodiscard]] TypeDescriptor typeDescriptorFor();

template <typename T> [[nodiscard]] TypeDescriptor typeDescriptorFor() {
    using Type = std::remove_cvref_t<T>;
    if constexpr(std::is_same_v<Type, bool>) {
        return {.kind = TypeDescriptor::Kind::Boolean,
                .description = {},
                .element_type = {},
                .fields = {},
                .value_type = {}};
    } else if constexpr(value_converter_detail::is_signed_integer<Type>) {
        return {.kind = TypeDescriptor::Kind::Integer,
                .description = {},
                .element_type = {},
                .fields = {},
                .value_type = {}};
    } else if constexpr(std::is_floating_point_v<Type>) {
        return {.kind = TypeDescriptor::Kind::Number,
                .description = {},
                .element_type = {},
                .fields = {},
                .value_type = {}};
    } else if constexpr(std::is_same_v<Type, std::string>) {
        return {.kind = TypeDescriptor::Kind::String,
                .description = {},
                .element_type = {},
                .fields = {},
                .value_type = {}};
    } else if constexpr(value_converter_detail::IsVector<Type>::value) {
        return TypeDescriptor::array(
            typeDescriptorFor<typename value_converter_detail::IsVector<Type>::ElementType>());
    } else if constexpr(value_converter_detail::IsStringMap<Type>::value) {
        return TypeDescriptor::objectValues(
            typeDescriptorFor<typename value_converter_detail::IsStringMap<Type>::ElementType>());
    }

    static_assert(ValueConvertible<Type>);
    return {};
}

template <typename Return> [[nodiscard]] TypeDescriptor returnTypeDescriptor() {
    using Type = std::remove_cvref_t<Return>;
    if constexpr(std::is_void_v<Type>) {
        return {.kind = TypeDescriptor::Kind::Null,
                .description = {},
                .element_type = {},
                .fields = {},
                .value_type = {}};
    } else if constexpr(ResultValue<Type>::is_result) {
        using ResultType = ResultValue<Type>::ValueType;
        if constexpr(std::is_void_v<ResultType>) {
            return {.kind = TypeDescriptor::Kind::Null,
                    .description = {},
                    .element_type = {},
                    .fields = {},
                    .value_type = {}};
        } else {
            return typeDescriptorFor<ResultType>();
        }
    } else {
        return typeDescriptorFor<Type>();
    }
}

/**
 * @brief Adapts one stored callable instance to the dynamic Action boundary.
 *
 * @note This
 * adapter intentionally shares `callable_` across invocations. The Runtime may
 *       invoke it
 * concurrently and does not add locking around user code; mutable callable
 *       state must
 * therefore be synchronized by the callable's owner.
 */
template <typename Callable> class TypedActionAdapter final : public IAction {
public:
    using Traits = FunctionTraits<std::remove_cvref_t<Callable>>;
    using Return = Traits::ReturnType;
    using SourceArguments = Traits::ArgumentTypes;

    TypedActionAdapter(Callable callable, std::vector<ParameterDescriptor> parameters)
        : callable_(std::move(callable)), parameters_(std::move(parameters)) {}

    [[nodiscard]] Result<Value> invoke(const Arguments& arguments,
                                       const InvocationContext& context) override {
        static_cast<void>(context);
        using ConvertedArguments = StoredTuple<SourceArguments>::Type;
        ConvertedArguments converted;
        const auto conversion = convertArguments(converted, arguments);
        if(!conversion) {
            return Result<Value>::failure(conversion.error());
        }
        return invokeCallable(converted);
    }

private:
    template <typename Tuple> struct StoredTuple;

    template <typename... Source> struct StoredTuple<std::tuple<Source...>> {
        using Type = std::tuple<std::remove_cvref_t<Source>...>;
    };

    template <std::size_t Index = 0, typename Tuple>
    [[nodiscard]] Result<void> convertArguments(Tuple& converted,
                                                const Arguments& arguments) const {
        if constexpr(Index == std::tuple_size_v<SourceArguments>) {
            return Result<void>::success();
        } else {
            const auto supplied = arguments.find(parameters_[Index].name);
            const Value* value = nullptr;
            if(supplied != arguments.end()) {
                value = &supplied->second;
            } else if(const auto& default_value = parameters_[Index].default_value;
                      default_value.has_value()) {
                value = &default_value.value();
            }
            if(value == nullptr) {
                return Result<void>::failure(
                    {.code = ErrorCode::MissingArgument,
                     .message = "Required argument is missing: " + parameters_[Index].name,
                     .path = parameters_[Index].name,
                     .details = std::nullopt});
            }
            using Target = std::tuple_element_t<Index, Tuple>;
            auto result = fromValue<Target>(*value, parameters_[Index].name);
            if(!result) {
                return Result<void>::failure(result.error());
            }
            std::get<Index>(converted) = std::move(result.value());
            return convertArguments<Index + 1>(converted, arguments);
        }
    }

    template <typename Tuple> [[nodiscard]] Result<Value> invokeCallable(Tuple& converted) {
        if constexpr(std::is_void_v<Return>) {
            std::apply([this](auto&... values) { std::invoke(callable_, values...); }, converted);
            return Result<Value>::success(Value{});
        } else {
            auto result = std::apply(
                [this](auto&... values) { return std::invoke(callable_, values...); }, converted);
            return convertReturn(std::move(result));
        }
    }

    template <typename ResultType>
    [[nodiscard]] static Result<Value> convertReturn(ResultType&& result) {
        using Type = std::remove_cvref_t<ResultType>;
        if constexpr(ResultValue<Type>::is_result) {
            if(!result) {
                return Result<Value>::failure(result.error());
            }
            using BusinessValue = ResultValue<Type>::ValueType;
            if constexpr(std::is_void_v<BusinessValue>) {
                return Result<Value>::success(Value{});
            } else {
                return Result<Value>::success(toValue(result.value()));
            }
        } else {
            return Result<Value>::success(toValue(result));
        }
    }

    Callable callable_;
    std::vector<ParameterDescriptor> parameters_;
};

} // namespace axiom::detail
