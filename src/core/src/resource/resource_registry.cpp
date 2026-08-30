#include <axiom/core/resource/resource_registry.hpp>

#include "resource_serial.hpp"

#include <axiom/core/base/error.hpp>
#include <axiom/core/base/result.hpp>
#include <axiom/core/base/value.hpp>
#include <axiom/core/resource/resource_id.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>

namespace axiom::core::resource {
namespace {

class TypeIdentity {
public:
    explicit TypeIdentity(const std::type_info& info) noexcept : index_(info) {}

    [[nodiscard]] bool operator==(const TypeIdentity& other) const noexcept {
        return index_ == other.index_;
    }

private:
    std::type_index index_;
};

struct TransparentStringHash {
    // NOLINTNEXTLINE(readability-identifier-naming): unordered_map mandates is_transparent.
    using is_transparent = void;

    [[nodiscard]] std::size_t operator()(const std::string_view value) const noexcept {
        return std::hash<std::string_view>{}(value);
    }

    [[nodiscard]] std::size_t operator()(const std::string& value) const noexcept {
        return std::hash<std::string_view>{}(value);
    }
};

struct Registered {
    std::shared_ptr<void> object;
    TypeIdentity type;
    std::string_view logical_name;
};

using EntryMap =
    std::unordered_map<std::string, Registered, TransparentStringHash, std::equal_to<>>;
using BindingMap =
    std::unordered_map<std::string, TypeIdentity, TransparentStringHash, std::equal_to<>>;

[[nodiscard]] Error logicalNameConflictError(const std::string_view logical_name) {
    return {
        .code = ErrorCode::TypeMismatch,
        .message = "Logical resource type name is already bound to a different C++ type",
        .path = std::nullopt,
        .details = Value{Value::Object{{"expected", Value{std::string{logical_name}}}}},
    };
}

[[nodiscard]] Error duplicateIdentityError(const std::string_view id) {
    return {
        .code = ErrorCode::InternalError,
        .message = "Allocated resource identity collided with an existing registration",
        .path = std::nullopt,
        .details = Value{Value::Object{{"id", Value{std::string{id}}}}},
    };
}

[[nodiscard]] Error malformedAllocatedIdError() {
    return {
        .code = ErrorCode::InternalError,
        .message = "Allocated resource identity was not canonical",
        .path = std::nullopt,
        .details = std::nullopt,
    };
}

[[nodiscard]] Result<ResourceId> identityFromSerial(const std::string_view type,
                                                    const std::uint64_t serial) {
    std::string text;
    text.reserve(type.size() + 21U);
    text.append(type);
    text.push_back(':');
    text.append(std::to_string(serial));
    auto parsed = ResourceId::parse(text);
    if(!parsed) {
        return Result<ResourceId>::failure(malformedAllocatedIdError());
    }
    return Result<ResourceId>::success(std::move(parsed.value()));
}

} // namespace

Error ResourceRegistry::nullResourceError() {
    return {
        .code = ErrorCode::InvalidArgument,
        .message = "Cannot register a null resource",
        .path = std::nullopt,
        .details = std::nullopt,
    };
}

Error ResourceRegistry::resourceNotFoundError(const std::string_view id) {
    return {
        .code = ErrorCode::NotFound,
        .message = "Resource is not registered",
        .path = std::nullopt,
        .details = Value{Value::Object{{"id", Value{std::string{id}}}}},
    };
}

Error ResourceRegistry::resourceTypeMismatchError(const TypeMismatchNames& names) {
    return {
        .code = ErrorCode::TypeMismatch,
        .message = "Resource type does not match the requested handle type",
        .path = std::nullopt,
        .details = Value{Value::Object{{"id", Value{std::string{names.id}}},
                                       {"expected", Value{std::string{names.expected}}},
                                       {"actual", Value{std::string{names.actual}}}}},
    };
}

class ResourceRegistry::Impl {
public:
    [[nodiscard]] std::optional<Error> bindingConflict(std::string_view logical_name,
                                                       TypeIdentity type) const;
    [[nodiscard]] Result<ResourceId> store(ResourceId identity,
                                           std::shared_ptr<void> object,
                                           std::string_view logical_name,
                                           TypeIdentity type);
    EntryMap entries;
    BindingMap bindings;
};

ResourceRegistry::ResourceRegistry() : impl_(std::make_unique<Impl>()) {}

ResourceRegistry::~ResourceRegistry() = default;

bool ResourceRegistry::remove(const ResourceId& id) {
    const auto found = impl_->entries.find(id.str());
    if(found == impl_->entries.end()) {
        return false;
    }
    impl_->entries.erase(found);
    return true;
}

bool ResourceRegistry::contains(const ResourceId& id) const {
    return impl_->entries.contains(id.str());
}

std::optional<Error> ResourceRegistry::Impl::bindingConflict(const std::string_view logical_name,
                                                             const TypeIdentity type) const {
    const auto found = bindings.find(logical_name);
    if(found != bindings.end() && found->second != type) {
        return logicalNameConflictError(logical_name);
    }
    return std::nullopt;
}

Result<ResourceId> ResourceRegistry::Impl::store(ResourceId identity,
                                                 std::shared_ptr<void> object,
                                                 const std::string_view logical_name,
                                                 const TypeIdentity type) {
    const auto [position, inserted] = entries.try_emplace(
        std::string{identity.str()},
        Registered{.object = std::move(object), .type = type, .logical_name = logical_name});
    if(!inserted) {
        return Result<ResourceId>::failure(duplicateIdentityError(position->first));
    }
    try {
        bindings.try_emplace(std::string{logical_name}, type);
    } catch(...) {
        entries.erase(position);
        throw;
    }
    return Result<ResourceId>::success(std::move(identity));
}

ResourceRegistry::LookupResult
ResourceRegistry::lookup(const std::string_view id_text,
                         const std::type_info& expected_type,
                         const std::string_view expected_name) const {
    const auto found = impl_->entries.find(id_text);
    if(found == impl_->entries.end()) {
        return {.status = LookupStatus::Missing, .access = {}, .actual_logical_name = {}};
    }
    if(found->second.logical_name != expected_name ||
       found->second.type != TypeIdentity{expected_type}) {
        return {.status = LookupStatus::TypeMismatch,
                .access = {},
                .actual_logical_name = found->second.logical_name};
    }
    return {.status = LookupStatus::Found,
            .access = detail::ResourceKeepalive{found->second.object.get(), found->second.object},
            .actual_logical_name = found->second.logical_name};
}

Result<ResourceId> ResourceRegistry::adopt(void* const object,
                                           void (*const destroy)(void*),
                                           const std::string_view logical_name,
                                           const std::type_info& type) {
    std::shared_ptr<void> hold{object, destroy};
    const TypeIdentity identity{type};
    if(const auto conflict = impl_->bindingConflict(logical_name, identity)) {
        return Result<ResourceId>::failure(*conflict);
    }
    const auto serial = detail::allocateResourceSerial();
    if(!serial) {
        return Result<ResourceId>::failure(serial.error());
    }
    auto allocated = identityFromSerial(logical_name, serial.value());
    if(!allocated) {
        return allocated;
    }
    return impl_->store(std::move(allocated.value()), std::move(hold), logical_name, identity);
}

} // namespace axiom::core::resource
