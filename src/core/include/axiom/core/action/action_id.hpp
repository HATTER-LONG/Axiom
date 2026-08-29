#pragma once

#include <axiom/core/base/result.hpp>

#include <algorithm>
#include <string>
#include <string_view>

namespace axiom::core {

/**
 * @brief Identifies an Action by its module and local name.
 *
 * An ActionId is always in the canonical `module.action` form. Both components
 * contain one or more lowercase ASCII letters, digits, or underscores.
 */
class ActionId {
public:
    /**
     * @brief Parses an Action identifier.
     *
     * @param text Candidate identifier in `module.action` form.
     * @return A parsed identifier, or an InvalidArgument error when text is not
     *         canonical.
     */
    [[nodiscard]] static Result<ActionId> parse(std::string_view text) {
        const auto separator = text.find('.');
        if(separator == std::string_view::npos || separator != text.rfind('.') || separator == 0U ||
           separator + 1U == text.size() || !isIdentifier(text.substr(0U, separator)) ||
           !isIdentifier(text.substr(separator + 1U))) {
            return Result<ActionId>::failure({
                .code = ErrorCode::InvalidArgument,
                .message = "Action ID must have the form module.action using [a-z0-9_]+ components",
                .path = std::nullopt,
                .details = std::nullopt,
            });
        }

        return Result<ActionId>::success(ActionId{std::string{text},
                                                  std::string{text.substr(0U, separator)},
                                                  std::string{text.substr(separator + 1U)}});
    }

    /**
     * @brief Returns the canonical complete identifier.
     * @return View valid while this ActionId remains alive and unmodified.
     */
    [[nodiscard]] std::string_view str() const noexcept { return text_; }
    /**
     * @brief Returns the module component of this identifier.
     * @return View valid while this ActionId remains alive and unmodified.
     */
    [[nodiscard]] std::string_view module() const noexcept { return module_name_; }
    /**
     * @brief Returns the action component of this identifier.
     * @return View valid while this ActionId remains alive and unmodified.
     */
    [[nodiscard]] std::string_view action() const noexcept { return action_name_; }

private:
    ActionId(std::string text_value, std::string module_value, std::string action_value)
        : text_(std::move(text_value)), module_name_(std::move(module_value)),
          action_name_(std::move(action_value)) {}

    [[nodiscard]] static bool isIdentifier(std::string_view text) noexcept {
        return !text.empty() && std::ranges::all_of(text, [](const char character) noexcept {
            return (character >= 'a' && character <= 'z') ||
                   (character >= '0' && character <= '9') || character == '_';
        });
    }

    std::string text_;
    std::string module_name_;
    std::string action_name_;
};

} // namespace axiom::core
