#pragma once

/**
 * @file task_id.hpp
 * @brief Canonical identifier for an Axiom task.
 */

#include <axiom/export.hpp>
#include <axiom/foundation/result.hpp>

#include <compare>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace axiom::task {

/**
 * @brief Owns a canonical task identity of the form `task:<serial>`.
 *
 * A TaskId is only an identity value. Parsing does not establish that a task
 * exists in any Registry. Serial numbers are non-zero decimal integers.
 */
class AXIOM_API TaskId final {
public:
    TaskId() = delete;
    TaskId(const TaskId&) = default;
    TaskId(TaskId&&) noexcept = default;
    TaskId& operator=(const TaskId&) = default;
    TaskId& operator=(TaskId&&) noexcept = default;
    ~TaskId() = default;

    /** @brief Parses a canonical `task:<non-zero serial>` identity. */
    [[nodiscard]] static Result<TaskId> parse(std::string_view text);
    /** @brief Returns canonical identity text. */
    [[nodiscard]] std::string_view str() const noexcept { return text_; }
    [[nodiscard]] bool operator==(const TaskId& other) const noexcept = default;
    [[nodiscard]] std::strong_ordering operator<=>(const TaskId& other) const noexcept = default;

private:
    friend class TaskRegistry;
    explicit TaskId(std::string text) : text_(std::move(text)) {}
    std::string text_;
};

} // namespace axiom::task

template <> struct std::hash<axiom::task::TaskId> {
    [[nodiscard]] std::size_t operator()(const axiom::task::TaskId& id) const noexcept {
        return std::hash<std::string_view>{}(id.str());
    }
};
