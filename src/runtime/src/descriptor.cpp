#include <axiom/runtime/action_id.hpp>

#include <algorithm>
#include <string>
#include <string_view>

namespace axiom::runtime {

static bool isValidCharacter(char character) {
    return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
           character == '_';
}

static bool isValidPart(std::string_view part) {
    return !part.empty() &&
           std::ranges::all_of(part, [](char character) { return isValidCharacter(character); });
}

ActionId::ActionId(std::string_view full_id) {
    const auto separator = full_id.find('.');
    if(separator == std::string_view::npos) {
        m_module = std::string{full_id};
    } else {
        m_module = std::string{full_id.substr(0, separator)};
        m_action = std::string{full_id.substr(separator + 1)};
    }
    m_fullId = m_module + "." + m_action;
    m_valid = isValidPart(m_module) && isValidPart(m_action);
}

ActionId::ActionId(std::string_view module, std::string_view action)
    : m_module{module}, m_action{action}, m_fullId{m_module + "." + m_action},
      m_valid{isValidPart(m_module) && isValidPart(m_action)} {}

std::string_view ActionId::module() const noexcept { return m_module; }

std::string_view ActionId::action() const noexcept { return m_action; }

const std::string& ActionId::toString() const noexcept { return m_fullId; }

bool ActionId::isValid() const noexcept { return m_valid; }

bool ActionId::operator==(const ActionId& other) const noexcept {
    return m_module == other.m_module && m_action == other.m_action;
}

} // namespace axiom::runtime
