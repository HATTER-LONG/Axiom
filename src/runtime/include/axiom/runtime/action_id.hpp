#pragma once

#include <string>
#include <string_view>

namespace axiom::runtime {

/**
 * @brief Identifier of a registered action in the form `module.action`.
 *
 * Both parts must be non-empty and contain only [a-z0-9_] characters. Invalid identifiers are
 * still constructible: isValid() reports false and module()/action() expose whatever parts were
 * parsed (for a full id without a separator the whole string becomes the module part and the
 * action part stays empty).
 */
class ActionId {
public:
    /** @brief Parses a full `module.action` identifier. */
    explicit ActionId(std::string_view full_id);
    /** @brief Combines separate module and action parts; both parts are validated. */
    ActionId(std::string_view module, std::string_view action);

    /** @brief Returns the module part. */
    [[nodiscard]] std::string_view module() const noexcept;
    /** @brief Returns the action part. */
    [[nodiscard]] std::string_view action() const noexcept;
    /** @brief Returns the full identifier as `module.action`. */
    [[nodiscard]] const std::string& toString() const noexcept;
    /** @brief Returns whether the identifier satisfies the format rules. */
    [[nodiscard]] bool isValid() const noexcept;

    /** @brief Returns whether both identifiers carry the same module and action parts. */
    [[nodiscard]] bool operator==(const ActionId& other) const noexcept;

private:
    std::string m_module;
    std::string m_action;
    std::string m_fullId;
    bool m_valid = false;
};

} // namespace axiom::runtime
