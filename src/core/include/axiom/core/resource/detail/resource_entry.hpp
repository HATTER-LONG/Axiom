#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <typeindex>
#include <typeinfo>

namespace axiom::core::resource::detail {

/**
 * @brief Exact C++ type identity used internally by ResourceRegistry.
 *
 * Comparison uses `std::type_info` equality, not the address of a translation-unit
 * local static. Plugin-unload identity is outside the MVP contract.
 */
class TypeIdentity {
public:
    explicit TypeIdentity(const std::type_info& info) noexcept : index_(info) {}

    [[nodiscard]] bool operator==(const TypeIdentity& other) const noexcept {
        return index_ == other.index_;
    }

private:
    std::type_index index_;
};

/**
 * @brief Move/copyable hold that keeps a registered object alive.
 *
 * This type is not a stable API and must not be constructed by callers.
 */
class ResourceKeepalive {
public:
    ResourceKeepalive() = default;
    explicit ResourceKeepalive(std::shared_ptr<void> hold) noexcept : hold_(std::move(hold)) {}

    ResourceKeepalive(const ResourceKeepalive&) = default;
    ResourceKeepalive(ResourceKeepalive&&) noexcept = default;
    ResourceKeepalive& operator=(const ResourceKeepalive&) = default;
    ResourceKeepalive& operator=(ResourceKeepalive&&) noexcept = default;
    ~ResourceKeepalive() = default;

private:
    std::shared_ptr<void> hold_;
};

struct RegistrationRequest {
    std::shared_ptr<void> object;
    std::string_view logical_name;
    TypeIdentity type;
};

struct Resolution {
    enum class Status : std::uint8_t { Missing, TypeMismatch, Found };

    Status status{Status::Missing};
    void* object{nullptr};
    ResourceKeepalive keepalive;
    std::string_view actual_logical_name{};
};

} // namespace axiom::core::resource::detail
