#pragma once

#include <axiom/action/descriptor.hpp>
#include <axiom/action/invocation_context.hpp>
#include <axiom/action/module.hpp>
#include <axiom/export.hpp>
#include <axiom/foundation/error.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/foundation/type_descriptor.hpp>
#include <axiom/foundation/value.hpp>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace axiom {

namespace detail {

#ifndef AXIOM_DETAIL_IACTION_DEFINED
#define AXIOM_DETAIL_IACTION_DEFINED
/**
 * @brief Internal polymorphic implementation of a registered Action.
 *
 * Registry owns implementations through this interface. It is an in-process
 * extension point only and does not promise binary compatibility across
 * compilers, Axiom versions, or shared-library boundaries.
 */
class IAction {
public:
    /** @brief Destroys an Action through its internal base interface. */
    virtual ~IAction() noexcept = default;

    /**
     * @brief Invokes the Action with structurally validated named arguments.
     *
     * @param arguments Named Values supplied by the caller.
     * @param context Diagnostic context preserved without Axiom policy decisions.
     * @return A dynamic result or an expected business Error.
     * @throws std::exception Implementations may signal unexpected failures; the
     *         Dispatcher converts them at the invocation boundary.
     */
    [[nodiscard]] virtual Result<Value> invoke(const Arguments& arguments,
                                               const InvocationContext& context) = 0;
};
#endif

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

[[nodiscard]] std::string appendArrayPath(std::string_view parent_path, std::size_t index);
[[nodiscard]] std::string appendObjectPath(std::string_view parent_path,
                                           std::string_view field_name);

namespace value_converter_detail {

template <typename T> struct IsVector : std::false_type {};

template <typename Element, typename Allocator>
struct IsVector<std::vector<Element, Allocator>> : std::true_type {
    using ElementType = Element;
};

template <typename T> struct IsStringMap : std::false_type {};

template <typename Element, typename Compare, typename Allocator>
struct IsStringMap<std::map<std::string, Element, Compare, Allocator>> : std::true_type {
    using ElementType = Element;
};

template <typename Element, typename Hash, typename Equal, typename Allocator>
struct IsStringMap<std::unordered_map<std::string, Element, Hash, Equal, Allocator>>
    : std::true_type {
    using ElementType = Element;
};

template <typename T>
inline constexpr bool IS_SIGNED_INTEGER =
    std::is_integral_v<T> && std::is_signed_v<T> && !std::is_same_v<T, bool>;

template <typename T>
struct IsValueConvertible
    : std::bool_constant<std::is_same_v<T, bool> || IS_SIGNED_INTEGER<T> ||
                         std::is_floating_point_v<T> || std::is_same_v<T, std::string>> {};

template <typename Element, typename Allocator>
struct IsValueConvertible<std::vector<Element, Allocator>>
    : std::bool_constant<std::is_default_constructible_v<Allocator> &&
                         IsValueConvertible<std::remove_cv_t<Element>>::value> {};

template <typename Element, typename Compare, typename Allocator>
struct IsValueConvertible<std::map<std::string, Element, Compare, Allocator>>
    : std::bool_constant<std::is_default_constructible_v<Compare> &&
                         std::is_default_constructible_v<Allocator> &&
                         IsValueConvertible<std::remove_cv_t<Element>>::value> {};

template <typename Element, typename Hash, typename Equal, typename Allocator>
struct IsValueConvertible<std::unordered_map<std::string, Element, Hash, Equal, Allocator>>
    : std::bool_constant<std::is_default_constructible_v<Hash> &&
                         std::is_default_constructible_v<Equal> &&
                         std::is_default_constructible_v<Allocator> &&
                         IsValueConvertible<std::remove_cv_t<Element>>::value> {};

[[nodiscard]] inline std::string valueTypeName(const Value::Type type) {
    switch(type) {
    case Value::Type::Null:
        return "null";
    case Value::Type::Boolean:
        return "boolean";
    case Value::Type::Integer:
        return "integer";
    case Value::Type::Number:
        return "number";
    case Value::Type::String:
        return "string";
    case Value::Type::Array:
        return "array";
    case Value::Type::Object:
        return "object";
    }

    return "unknown";
}

[[nodiscard]] inline Error typeMismatch(const std::string_view path,
                                        const std::string_view expected,
                                        const Value::Type actual) {
    return {
        .code = ErrorCode::TypeMismatch,
        .message = "Expected " + std::string{expected} + " but received " + valueTypeName(actual),
        .path = std::string{path},
        .details = Value{Value::Object{{"actual", Value{valueTypeName(actual)}},
                                       {"expected", Value{std::string{expected}}}}},
    };
}

template <typename Integer> [[nodiscard]] bool fitsInInteger(const std::int64_t value) noexcept {
    if constexpr(std::numeric_limits<Integer>::digits >=
                 std::numeric_limits<std::int64_t>::digits) {
        return true;
    } else {
        return value >= static_cast<std::int64_t>(std::numeric_limits<Integer>::lowest()) &&
               value <= static_cast<std::int64_t>(std::numeric_limits<Integer>::max());
    }
}

/**
 * @brief Tests whether a key can be represented as an unquoted path segment.
 *
 * The accepted ASCII grammar is `[A-Za-z_][A-Za-z0-9_]*`. All other keys use
 * the quoted object-key representation so literal punctuation cannot be confused
 * with path structure.
 *
 * @param key String key to test.
 * @return `true` if key has the unquoted path-segment grammar.
 */
[[nodiscard]] constexpr bool isIdentifierPathSegment(const std::string_view key) noexcept {
    if(key.empty()) {
        return false;
    }

    const auto is_letter = [](const unsigned char character) constexpr {
        return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
    };
    const auto is_digit = [](const unsigned char character) constexpr {
        return character >= '0' && character <= '9';
    };

    const auto first = static_cast<unsigned char>(key.front());
    if(!is_letter(first) && first != '_') {
        return false;
    }

    return std::all_of(key.cbegin() + 1, key.cend(), [&](const char character) {
        const auto byte = static_cast<unsigned char>(character);
        return is_letter(byte) || is_digit(byte) || byte == '_';
    });
}

/**
 * @brief Maps one byte to its C-style escape sequence.
 *
 * @param byte Raw key byte.
 * @return Escape sequence for backslash, quote, and common control characters; empty
 *         for bytes without a C-style escape.
 */
[[nodiscard]] inline std::string_view commonEscapeSequence(const unsigned char byte) noexcept {
    switch(byte) {
    case '\\':
        return "\\\\";
    case '"':
        return "\\\"";
    case '\b':
        return "\\b";
    case '\f':
        return "\\f";
    case '\n':
        return "\\n";
    case '\r':
        return "\\r";
    case '\t':
        return "\\t";
    default:
        return {};
    }
}

/**
 * @brief Escapes a string key for the quoted object-key path representation.
 *
 * Backslashes, quotes, and common control characters use C-style escapes. Other
 * control bytes and DEL use fixed-width `\\u00XX` escapes. This keeps every quoted
 * key segment unambiguous and reversible without altering UTF-8 bytes.
 *
 * @param key Raw string key to escape.
 * @return Escaped key contents without the surrounding quotes.
 */
[[nodiscard]] inline std::string escapePathKey(const std::string_view key) {
    constexpr std::string_view hexadecimal = "0123456789ABCDEF";
    std::string escaped;
    escaped.reserve(key.size());

    for(const unsigned char byte : key) {
        const auto escape_sequence = commonEscapeSequence(byte);
        if(!escape_sequence.empty()) {
            escaped += escape_sequence;
            continue;
        }
        if(byte < 0x20U || byte == 0x7FU) {
            escaped += "\\u00";
            escaped += hexadecimal[(byte >> 4U) & 0x0FU];
            escaped += hexadecimal[byte & 0x0FU];
        } else {
            escaped += static_cast<char>(byte);
        }
    }

    return escaped;
}

template <typename T> struct Converter;

template <> struct Converter<bool> {
    [[nodiscard]] static Result<bool> from(const Value& value, const std::string_view path) {
        if(!value.isBoolean()) {
            return Result<bool>::failure(typeMismatch(path, "boolean", value.type()));
        }
        return Result<bool>::success(value.asBoolean());
    }

    [[nodiscard]] static Value to(const bool value) { return Value{value}; }
};

template <typename T>
    requires IS_SIGNED_INTEGER<T>
struct Converter<T> {
    [[nodiscard]] static Result<T> from(const Value& value, const std::string_view path) {
        if(!value.isInteger()) {
            return Result<T>::failure(typeMismatch(path, "integer", value.type()));
        }

        const std::int64_t integer = value.asInteger();
        if(!fitsInInteger<T>(integer)) {
            return Result<T>::failure({
                .code = ErrorCode::TypeMismatch,
                .message = "Integer value is outside the target type range",
                .path = std::string{path},
                .details = std::nullopt,
            });
        }
        return Result<T>::success(static_cast<T>(integer));
    }

    [[nodiscard]] static Value to(const T value) { return Value{static_cast<std::int64_t>(value)}; }
};

template <typename T>
    requires std::is_floating_point_v<T>
struct Converter<T> {
    [[nodiscard]] static Result<T> from(const Value& value, const std::string_view path) {
        if(value.isInteger()) {
            return Result<T>::success(static_cast<T>(value.asInteger()));
        }
        if(value.isNumber()) {
            return Result<T>::success(static_cast<T>(value.asNumber()));
        }
        return Result<T>::failure(typeMismatch(path, "number", value.type()));
    }

    [[nodiscard]] static Value to(const T value) { return Value{static_cast<double>(value)}; }
};

template <> struct Converter<std::string> {
    [[nodiscard]] static Result<std::string> from(const Value& value, const std::string_view path) {
        if(!value.isString()) {
            return Result<std::string>::failure(typeMismatch(path, "string", value.type()));
        }
        return Result<std::string>::success(value.asString());
    }

    [[nodiscard]] static Value to(const std::string& value) { return Value{value}; }
};

template <typename Vector> struct VectorConverter {
    using Element = IsVector<Vector>::ElementType;

    [[nodiscard]] static Result<Vector> from(const Value& value, const std::string_view path) {
        if(!value.isArray()) {
            return Result<Vector>::failure(typeMismatch(path, "array", value.type()));
        }

        Vector converted;
        const Value::Array& values = value.asArray();
        converted.reserve(values.size());
        for(std::size_t index = 0; index < values.size(); ++index) {
            auto element = Converter<Element>::from(values[index], appendArrayPath(path, index));
            if(!element) {
                return Result<Vector>::failure(std::move(element.error()));
            }
            converted.push_back(std::move(element.value()));
        }
        return Result<Vector>::success(std::move(converted));
    }

    [[nodiscard]] static Value to(const Vector& value) {
        Value::Array converted;
        converted.reserve(value.size());
        std::transform(value.cbegin(), value.cend(), std::back_inserter(converted),
                       [](const Element& element) { return Converter<Element>::to(element); });
        return Value{std::move(converted)};
    }
};

template <typename Element, typename Allocator>
struct Converter<std::vector<Element, Allocator>>
    : VectorConverter<std::vector<Element, Allocator>> {};

template <typename Map> struct StringMapConverter {
    using Element = IsStringMap<Map>::ElementType;

    [[nodiscard]] static Result<Map> from(const Value& value, const std::string_view path) {
        if(!value.isObject()) {
            return Result<Map>::failure(typeMismatch(path, "object", value.type()));
        }

        Map converted;
        for(const auto& [name, member] : value.asObject()) {
            auto element = Converter<Element>::from(member, appendObjectPath(path, name));
            if(!element) {
                return Result<Map>::failure(std::move(element.error()));
            }
            converted.emplace(name, std::move(element.value()));
        }
        return Result<Map>::success(std::move(converted));
    }

    [[nodiscard]] static Value to(const Map& value) {
        Value::Object converted;
        std::transform(value.cbegin(), value.cend(), std::inserter(converted, converted.end()),
                       [](const auto& entry) {
                           return std::pair<std::string, Value>{
                               entry.first, Converter<Element>::to(entry.second)};
                       });
        return Value{std::move(converted)};
    }
};

template <typename Element, typename Compare, typename Allocator>
struct Converter<std::map<std::string, Element, Compare, Allocator>>
    : StringMapConverter<std::map<std::string, Element, Compare, Allocator>> {};

template <typename Element, typename Hash, typename Equal, typename Allocator>
struct Converter<std::unordered_map<std::string, Element, Hash, Equal, Allocator>>
    : StringMapConverter<std::unordered_map<std::string, Element, Hash, Equal, Allocator>> {};

} // namespace value_converter_detail

/**
 * @brief Reports whether a C++ type can cross the Value conversion boundary.
 *
 * Supported types are bool, signed integer types, floating-point types, std::string,
 * std::vector of a supported element type, and std::map or std::unordered_map with
 * std::string keys and a supported mapped type. Conversion default constructs every
 * container, so allocator, comparator, hash, and key-equality policies must be
 * default constructible; non-default-constructible policies are rejected at compile
 * time instead of failing during adapter instantiation.
 *
 * @tparam T Candidate C++ type, excluding cv/ref qualifiers.
 */
template <typename T>
concept ValueConvertible =
    value_converter_detail::IsValueConvertible<std::remove_cvref_t<T>>::value;

/**
 * @brief Appends an array index to an input path.
 *
 * @param parent_path Existing input path, or empty for a root array.
 * @param index Zero-based array element index.
 * @return Path in the public array notation, for example `points[2]`.
 */
[[nodiscard]] inline std::string appendArrayPath(const std::string_view parent_path,
                                                 const std::size_t index) {
    return std::string{parent_path} + '[' + std::to_string(index) + ']';
}

/**
 * @brief Appends a named object field to an input path.
 *
 * Identifier keys use the existing dot form (for example, `shape.size`). Other
 * keys use a quoted bracket segment (for example, `shape["a.b"]`). Within a
 * quoted segment, `\\`, `"`, common control characters, and remaining control
 * bytes are escaped so punctuation cannot be mistaken for nesting or an array index.
 * This encoding preserves the established `shape.size.x` and `points[2]` forms.
 *
 * @param parent_path Existing input path, or empty for a root object.
 * @param field_name Object field name.
 * @return An unambiguous object path, for example `shape.size` or `shape["a.b"]`.
 */
[[nodiscard]] inline std::string appendObjectPath(const std::string_view parent_path,
                                                  const std::string_view field_name) {
    if(value_converter_detail::isIdentifierPathSegment(field_name)) {
        if(parent_path.empty()) {
            return std::string{field_name};
        }
        return std::string{parent_path} + '.' + std::string{field_name};
    }

    return std::string{parent_path} + "[\"" + value_converter_detail::escapePathKey(field_name) +
           "\"]";
}

/**
 * @brief Converts a Value into a supported C++ value while retaining its input path.
 *
 * Integer Values may convert to floating-point targets. Integer targets accept only
 * Integer Values and reject values outside the target range. Recursive containers
 * preserve the precise failing array index or object field in their Error path.
 *
 * @tparam T Supported target type.
 * @param value Dynamic input value to convert.
 * @param path Public input location for value.
 * @return Converted C++ value or a TypeMismatch error with the failing input path.
 */
template <ValueConvertible T>
[[nodiscard]] Result<std::remove_cvref_t<T>> fromValue(const Value& value,
                                                       const std::string_view path) {
    using Target = std::remove_cvref_t<T>;
    return value_converter_detail::Converter<Target>::from(value, path);
}

/**
 * @brief Converts a supported C++ value into a Value for an Action result.
 *
 * Container elements are converted recursively. String-key maps are materialized as
 * ordered Value objects, preserving the stable Value object iteration contract.
 *
 * @tparam T Supported source type.
 * @param value C++ value to convert.
 * @return Dynamic representation of value.
 */
template <ValueConvertible T> [[nodiscard]] Value toValue(const T& value) {
    using Source = std::remove_cvref_t<T>;
    return value_converter_detail::Converter<Source>::to(value);
}

template <typename T> struct ResultValue {
    static constexpr bool IS_RESULT = false;
    using ValueType = T;
};

template <typename T> struct ResultValue<Result<T>> {
    static constexpr bool IS_RESULT = true;
    using ValueType = T;
};

template <typename T>
inline constexpr bool IS_ADAPTED_RETURN = [] {
    using Type = std::remove_cvref_t<T>;
    if constexpr(std::is_void_v<Type>) {
        return true;
    } else if constexpr(ResultValue<Type>::IS_RESULT) {
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
               IS_ADAPTED_RETURN<Return> &&
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
    } else if constexpr(value_converter_detail::IS_SIGNED_INTEGER<Type>) {
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
    } else if constexpr(ResultValue<Type>::IS_RESULT) {
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
        if constexpr(ResultValue<Type>::IS_RESULT) {
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

class ModuleBuilderState;
class IAction;

template <typename Target>
[[nodiscard]] Result<void> validateParameterDefault(const ParameterDescriptor& parameter) {
    if(!parameter.default_value) {
        return Result<void>::success();
    }
    const auto conversion = fromValue<Target>(*parameter.default_value, parameter.name);
    if(conversion) {
        return Result<void>::success();
    }
    return Result<void>::failure(
        {.code = ErrorCode::InvalidDescriptor,
         .message = "Parameter default value cannot convert to its inferred C++ type",
         .path = parameter.name,
         .details = std::nullopt});
}

template <typename Tuple>
[[nodiscard]] Result<void>
validateParameterDefaults(const std::vector<ParameterDescriptor>& parameters) {
    return [&parameters]<std::size_t... Index>(std::index_sequence<Index...>) {
        Result<void> result = Result<void>::success();
        ((result ? result = validateParameterDefault<
                       std::remove_cvref_t<std::tuple_element_t<Index, Tuple>>>(parameters[Index])
                 : result),
         ...);
        return result;
    }(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
}
} // namespace detail

/**
 * @brief Supplies the caller-facing name and documentation for one callable parameter.
 *
 * Callable C++ parameter and return types are always inferred. This object deliberately
 * cannot declare a second, potentially conflicting type.
 */
struct ParameterDocumentation {
    /** @brief Caller-facing parameter name. */
    std::string name;
    /** @brief Human-readable parameter documentation. */
    std::string description;
    /** @brief Optional fallback converted with the callable's inferred parameter type. */
    std::optional<Value> default_value;
};

/**
 * @brief Creates parameter documentation for ModuleBuilder registration.
 *
 * @param name Caller-facing parameter name matching `[a-z0-9_]+`.
 * @param description Human-readable explanation of the parameter.
 * @param default_value Optional fallback used when callers omit this parameter.
 * @return Documentation that ModuleBuilder combines with an inferred C++ type.
 */
[[nodiscard]] inline ParameterDocumentation
param(std::string name,
      std::string description = {},
      std::optional<Value> default_value = std::nullopt) {
    return {.name = std::move(name),
            .description = std::move(description),
            .default_value = std::move(default_value)};
}

/**
 * @brief Accumulates typed Action registrations for one Module before Runtime mutation.
 *
 * A builder owns each accepted callable, copying lvalue callables and moving rvalue
 * callables into pending storage. It accepts ordinary functions and unambiguous
 * copyable callable objects, including copyable lambdas. Generic lambdas, overloaded
 * function objects, non-copyable captures, and raw member-function pointers are
 * rejected at compile time; use bindMember() to retain a shared object explicitly.
 */
class AXIOM_API ModuleBuilder {
public:
    /**
     * @brief Starts a detached Module definition.
     * @param descriptor Module identity and metadata copied for later Runtime registration.
     * @throws std::bad_alloc If builder state cannot be allocated.
     */
    explicit ModuleBuilder(const ModuleDescriptor& descriptor);
    /** @brief Releases any pending callable registrations. */
    ~ModuleBuilder() noexcept;

    /**
     * @brief Transfers pending module ownership from another builder.
     * @param other Builder to leave empty.
     */
    ModuleBuilder(ModuleBuilder&& other) noexcept;
    /**
     * @brief Replaces pending module ownership with another builder's state.
     * @param other Builder to leave empty.
     * @return This builder.
     */
    ModuleBuilder& operator=(ModuleBuilder&& other) noexcept;
    ModuleBuilder(const ModuleBuilder&) = delete;
    ModuleBuilder& operator=(const ModuleBuilder&) = delete;

    /**
     * @brief Adds a callable Action without changing a Runtime.
     *
     * @tparam Callable A supported ordinary function or copyable callable object with an
     * unambiguous non-generic signature.
     * @tparam Documentation Parameter documentation argument types.
     * @param action_name Local Action name; Runtime combines it with the Module namespace.
     * @param description Caller-facing Action description.
     * @param callable Business callable copied (or moved, when an rvalue) into pending storage.
     * @param documentation One param() entry for every callable parameter, in signature order.
     * @return Success, or an empty-builder, invalid/duplicate descriptor error without
     *         changing this builder.
     * @throws std::bad_alloc If descriptor or callable storage preparation cannot allocate.
     * @throws Any exception raised while copying or moving callable into owned storage.
     *         The builder remains unchanged when callable construction throws.
     * @note This overload participates only when callable storage can be copied and invoked
     *       with the converted lvalue argument objects. In particular, rvalue-reference-only,
     *       generic, overloaded, non-copyable, and type-erased callables are rejected during
     *       overload resolution. Ordinary function names decay to function pointers before
     *       their signature is inspected and stored.
     */
    template <typename Callable, typename... Documentation>
        requires detail::AdaptableCallable<Callable>
    [[nodiscard]] Result<void> add(std::string_view action_name,
                                   std::string description,
                                   Callable&& callable,
                                   Documentation&&... documentation);

private:
    friend class Runtime;

    [[nodiscard]] Result<void> addPreparedAction(std::string_view action_name,
                                                 std::string description,
                                                 std::unique_ptr<detail::IAction> implementation,
                                                 std::vector<ParameterDescriptor> parameters,
                                                 const TypeDescriptor& return_type);

    std::unique_ptr<detail::ModuleBuilderState> state_;
};

} // namespace axiom

namespace axiom {

namespace detail {

/** @brief Copyable callable retaining a shared owner for a bound member function. */
template <auto Method, typename Object, typename Return, typename... Arguments>
class MemberBinding {
public:
    /**
     * @brief Retains the object used for each member invocation.
     * @param object Non-null shared owner of the member function receiver.
     */
    explicit MemberBinding(std::shared_ptr<Object> object) : object_(std::move(object)) {}

    /**
     * @brief Invokes the bound member function.
     * @param arguments Arguments forwarded to the member function.
     * @return The member function's result when it is non-void.
     */
    Return operator()(Arguments... arguments) const {
        return std::invoke(Method, *object_, std::forward<Arguments>(arguments)...);
    }

private:
    std::shared_ptr<Object> object_;
};

template <auto Method, typename Object, typename Return, typename... Arguments>
[[nodiscard]] MemberBinding<Method, Object, Return, Arguments...>
makeMemberBinding(std::shared_ptr<Object> object,
                  FunctionSignature<Return, Arguments...> signature) {
    static_cast<void>(signature);
    return MemberBinding<Method, Object, Return, Arguments...>{std::move(object)};
}

} // namespace detail

/**
 * @brief Creates a lifetime-safe callable wrapper for a member function.
 *
 * @tparam Method Pointer to a non-static member function.
 * @tparam Object Member function owner type.
 * @param object Shared owner retained by the returned callable.
 * @return A copyable callable that keeps object lifetime explicit through invocation.
 * @pre object is non-null.
 */
template <auto Method, typename Object>
[[nodiscard]] auto bindMember(std::shared_ptr<Object> object) {
    static_assert(std::is_member_function_pointer_v<decltype(Method)>,
                  "bindMember requires a non-static member-function pointer");
    return detail::makeMemberBinding<Method>(std::move(object),
                                             detail::FunctionTraits<decltype(Method)>{});
}

template <typename Callable, typename... Documentation>
    requires detail::AdaptableCallable<Callable>
Result<void> ModuleBuilder::add(std::string_view action_name,
                                std::string description,
                                Callable&& callable,
                                Documentation&&... documentation) {
    using StoredCallable = std::decay_t<Callable>;
    static_assert((std::same_as<std::remove_cvref_t<Documentation>, ParameterDocumentation> && ...),
                  "Each Action parameter must be described with param(name, description)");

    using Traits = detail::FunctionTraits<StoredCallable>;
    using SourceArguments = Traits::ArgumentTypes;
    using Return = Traits::ReturnType;
    constexpr auto parameter_count = std::tuple_size_v<SourceArguments>;
    static_assert(
        sizeof...(Documentation) == parameter_count,
        "Action registration needs exactly one param(name, description) per callable parameter");

    std::vector<ParameterDescriptor> parameters;
    parameters.reserve(parameter_count);
    (parameters.push_back({.name = documentation.name,
                           .description = documentation.description,
                           .required = !documentation.default_value.has_value(),
                           .type = {},
                           .default_value = documentation.default_value}),
     ...);
    [&]<std::size_t... Index>(std::index_sequence<Index...>) {
        ((parameters[Index].type = detail::typeDescriptorFor<
              std::remove_cvref_t<std::tuple_element_t<Index, SourceArguments>>>()),
         ...);
    }(std::make_index_sequence<parameter_count>{});

    auto default_validation = detail::validateParameterDefaults<SourceArguments>(parameters);
    if(!default_validation) {
        return default_validation;
    }

    auto implementation = std::make_unique<detail::TypedActionAdapter<StoredCallable>>(
        std::forward<Callable>(callable), parameters);
    return addPreparedAction(action_name, std::move(description), std::move(implementation),
                             std::move(parameters), detail::returnTypeDescriptor<Return>());
}

} // namespace axiom
