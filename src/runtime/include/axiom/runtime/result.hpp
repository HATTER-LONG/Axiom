#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

#include <axiom/runtime/value.hpp>

namespace axiom::runtime {

/** @brief Structured error categories reported across the runtime ABI boundary. */
enum class ErrorCode {
    ActionNotFound,
    InvalidArgument,
    MissingArgument,
    TypeMismatch,
    InvocationFailed,
    InternalError
};

/** @brief Structured error carried by Result across the runtime ABI boundary. */
struct Error {
    ErrorCode m_code = ErrorCode::InternalError;
    std::string m_message;
    std::string m_path;
    Value m_details;
};

/**
 * @brief Minimal success/error result used as the only error-propagation mechanism across the
 * runtime ABI boundary.
 *
 * Accessors have a precondition on the stored alternative: accessing the wrong alternative
 * throws std::invalid_argument. These throws are for internal use only and never escape the ABI
 * boundary.
 */
template <typename T> class Result {
public:
    /** @brief Constructs a successful result holding a value. */
    explicit Result(T value) : m_storage{std::in_place_index<0>, std::move(value)} {}
    /** @brief Constructs a failed result holding an error. */
    explicit Result(Error error) : m_storage{std::in_place_index<1>, std::move(error)} {}

    /** @brief Returns whether the result holds a value. */
    [[nodiscard]] bool hasValue() const noexcept { return std::holds_alternative<T>(m_storage); }
    /** @brief Returns whether the result holds a value. */
    explicit operator bool() const noexcept { return hasValue(); }

    /** @brief Returns the stored value; throws std::invalid_argument if it holds an error. */
    [[nodiscard]] const T& value() const& {
        checkValue();
        return std::get<T>(m_storage);
    }
    /** @brief Returns the stored value; throws std::invalid_argument if it holds an error. */
    [[nodiscard]] T& value() & {
        checkValue();
        return std::get<T>(m_storage);
    }
    /** @brief Returns the stored value; throws std::invalid_argument if it holds an error. */
    [[nodiscard]] T&& value() && {
        checkValue();
        return std::get<T>(std::move(m_storage));
    }

    /** @brief Returns the stored error; throws std::invalid_argument if it holds a value. */
    [[nodiscard]] const Error& error() const& {
        if(hasValue()) {
            throw std::invalid_argument{"Result holds a value"};
        }
        return std::get<Error>(m_storage);
    }

private:
    void checkValue() const {
        if(!hasValue()) {
            throw std::invalid_argument{"Result holds an error"};
        }
    }

    std::variant<T, Error> m_storage;
};

/** @brief Specialization of Result for actions that produce no value. */
template <> class Result<void> {
public:
    /** @brief Constructs a successful result. */
    Result() = default;
    /** @brief Constructs a failed result holding an error. */
    explicit Result(Error error) : m_error{std::move(error)} {}

    /** @brief Returns whether the result holds success. */
    [[nodiscard]] bool hasValue() const noexcept { return !m_error.has_value(); }
    /** @brief Returns whether the result holds success. */
    explicit operator bool() const noexcept { return hasValue(); }

    /** @brief Returns the stored error; throws std::invalid_argument on success. */
    [[nodiscard]] const Error& error() const& {
        if(!m_error.has_value()) {
            throw std::invalid_argument{"Result holds no error"};
        }
        return *m_error;
    }

private:
    std::optional<Error> m_error;
};

} // namespace axiom::runtime
