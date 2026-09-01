#pragma once

#include <axiom/foundation/error.hpp>

#include <stdexcept>
#include <utility>
#include <variant>

namespace axiom {

/**
 * @brief Owns either a successful value or a structured boundary error.
 *
 * The interface deliberately does not promise a storage layout or ABI compatible
 * with std::expected. Callers must check hasValue() before reading the alternate.
 *
 * @tparam T Value type delivered by a successful operation.
 */
template <typename T> class Result {
public:
    /**
     * @brief Creates a successful result.
     * @param value Value to own.
     * @return Result holding value.
     */
    // NOLINTNEXTLINE(performance-unnecessary-value-param): sink owns then moves.
    [[nodiscard]] static Result success(T value) { return Result{std::move(value)}; }
    /**
     * @brief Creates a failed result.
     * @param error Structured failure to own.
     * @return Result holding error.
     */
    [[nodiscard]] static Result failure(Error error) { return Result{std::move(error)}; }

    /** @brief Returns whether this result holds a successful value. */
    [[nodiscard]] bool hasValue() const noexcept { return std::holds_alternative<T>(storage_); }
    /** @brief Returns whether this result holds an error. */
    [[nodiscard]] bool hasError() const noexcept { return !hasValue(); }
    /** @brief Returns whether this result holds a successful value. */
    [[nodiscard]] explicit operator bool() const noexcept { return hasValue(); }

    /**
     * @brief Returns the successful value.
     * @return Reference valid while this result remains alive and unmodified.
     * @throws std::logic_error If this result holds an error.
     */
    [[nodiscard]] const T& value() const {
        if(hasError()) {
            throw std::logic_error{"Result does not contain a value"};
        }
        return std::get<T>(storage_);
    }
    /**
     * @brief Returns the successful value.
     * @return Reference valid while this result remains alive and unmodified.
     * @throws std::logic_error If this result holds an error.
     */
    [[nodiscard]] T& value() {
        if(hasError()) {
            throw std::logic_error{"Result does not contain a value"};
        }
        return std::get<T>(storage_);
    }
    /**
     * @brief Returns the structured error.
     * @return Reference valid while this result remains alive and unmodified.
     * @throws std::logic_error If this result holds a value.
     */
    [[nodiscard]] const Error& error() const {
        if(hasValue()) {
            throw std::logic_error{"Result does not contain an error"};
        }
        return std::get<Error>(storage_);
    }
    /**
     * @brief Returns the structured error.
     * @return Reference valid while this result remains alive and unmodified.
     * @throws std::logic_error If this result holds a value.
     */
    [[nodiscard]] Error& error() {
        if(hasValue()) {
            throw std::logic_error{"Result does not contain an error"};
        }
        return std::get<Error>(storage_);
    }

private:
    // NOLINTNEXTLINE(performance-unnecessary-value-param): sink owns then moves.
    explicit Result(T value) : storage_(std::move(value)) {}
    explicit Result(Error error) : storage_(std::move(error)) {}

    std::variant<T, Error> storage_;
};

/**
 * @brief Result specialization for successful operations without a value.
 */
template <> class Result<void> {
public:
    /**
     * @brief Creates a successful valueless result.
     * @return Successful result.
     */
    [[nodiscard]] static Result success() { return Result{std::monostate{}}; }
    /**
     * @brief Creates a failed result.
     * @param error Structured failure to own.
     * @return Result holding error.
     */
    [[nodiscard]] static Result failure(Error error) { return Result{std::move(error)}; }

    /** @brief Returns whether this result represents success. */
    [[nodiscard]] bool hasValue() const noexcept {
        return std::holds_alternative<std::monostate>(storage_);
    }
    /** @brief Returns whether this result holds an error. */
    [[nodiscard]] bool hasError() const noexcept { return !hasValue(); }
    /** @brief Returns whether this result represents success. */
    [[nodiscard]] explicit operator bool() const noexcept { return hasValue(); }

    /**
     * @brief Checks that this result represents success.
     * @throws std::logic_error If this result holds an error.
     */
    void value() const {
        if(hasError()) {
            throw std::logic_error{"Result does not contain a value"};
        }
    }
    /**
     * @brief Returns the structured error.
     * @return Reference valid while this result remains alive and unmodified.
     * @throws std::logic_error If this result represents success.
     */
    [[nodiscard]] const Error& error() const {
        if(hasValue()) {
            throw std::logic_error{"Result does not contain an error"};
        }
        return std::get<Error>(storage_);
    }
    /**
     * @brief Returns the structured error.
     * @return Reference valid while this result remains alive and unmodified.
     * @throws std::logic_error If this result represents success.
     */
    [[nodiscard]] Error& error() {
        if(hasValue()) {
            throw std::logic_error{"Result does not contain an error"};
        }
        return std::get<Error>(storage_);
    }

private:
    explicit Result(std::monostate value) : storage_(value) {}
    explicit Result(Error error) : storage_(std::move(error)) {}

    std::variant<std::monostate, Error> storage_;
};

} // namespace axiom
