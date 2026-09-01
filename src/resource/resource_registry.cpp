#include <axiom/resource/resource_registry.hpp>

#include "resource_serial.hpp"

#include <axiom/foundation/error.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/foundation/value.hpp>
#include <axiom/resource/resource_id.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>

namespace axiom::resource {
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
    ResourceId id;
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
    mutable std::shared_mutex mutex;
    EntryMap entries;
    BindingMap bindings;
};

ResourceRegistry::ResourceRegistry() : impl_(std::make_unique<Impl>()) {}

ResourceRegistry::~ResourceRegistry() = default;

bool ResourceRegistry::remove(const ResourceId& id) {
    // Keep the removed hold alive until after the lock is released. In
    // particular, a Resource destructor may call into unrelated user state.
    std::shared_ptr<void> removed;
    {
        std::unique_lock lock{impl_->mutex};
        const auto found = impl_->entries.find(id.str());
        if(found == impl_->entries.end()) {
            return false;
        }
        removed = std::move(found->second.object);
        impl_->entries.erase(found);
    }
    return true;
}

bool ResourceRegistry::contains(const ResourceId& id) const {
    const std::shared_lock lock{impl_->mutex};
    return impl_->entries.contains(id.str());
}

Result<ResourceDescriptor> ResourceRegistry::describe(const ResourceId& id) const {
    const std::shared_lock lock{impl_->mutex};
    const auto found = impl_->entries.find(id.str());
    if(found == impl_->entries.end()) {
        return Result<ResourceDescriptor>::failure(resourceNotFoundError(id.str()));
    }
    return Result<ResourceDescriptor>::success(ResourceDescriptor{
        .id = found->second.id, .type = std::string{found->second.logical_name}});
}

std::vector<ResourceDescriptor> ResourceRegistry::list() const {
    std::vector<ResourceDescriptor> descriptions;
    {
        const std::shared_lock lock{impl_->mutex};
        descriptions.reserve(impl_->entries.size());
        for(const auto& [identity, registered] : impl_->entries) {
            static_cast<void>(identity);
            descriptions.push_back(ResourceDescriptor{
                .id = registered.id,
                .type = std::string{registered.logical_name},
            });
        }
    }
    std::ranges::sort(descriptions, {},
                      [](const ResourceDescriptor& descriptor) { return descriptor.id.str(); });
    return descriptions;
}

ResourceRegistry::LookupResult
ResourceRegistry::lookup(const std::string_view id_text,
                         const std::type_info& expected_type,
                         const std::string_view expected_name) const {
    const std::shared_lock lock{impl_->mutex};
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
    // The unique_ptr has already transferred ownership to this function. Make
    // adoption itself exception safe before taking the Registry lock.
    std::shared_ptr<void> hold;
    try {
        hold = std::shared_ptr<void>{object, destroy};
    } catch(...) {
        destroy(object);
        throw;
    }
    const TypeIdentity identity{type};
    // `hold` is declared before the lock so any failed operation releases the
    // user resource only after the lock guard has been destroyed.
    std::unique_lock lock{impl_->mutex};
    const auto binding = impl_->bindings.find(logical_name);
    if(binding != impl_->bindings.end() && binding->second != identity) {
        return Result<ResourceId>::failure(logicalNameConflictError(logical_name));
    }
    const auto serial = detail::allocateResourceSerial();
    if(!serial) {
        return Result<ResourceId>::failure(serial.error());
    }
    auto allocated = identityFromSerial(logical_name, serial.value());
    if(!allocated) {
        return Result<ResourceId>::failure(allocated.error());
    }
    const ResourceId identity_id = std::move(allocated.value());

    const auto [binding_position, binding_inserted] =
        impl_->bindings.try_emplace(std::string{logical_name}, identity);
    static_cast<void>(binding_position);
    // Keep an owner in this function even if map allocation throws or a
    // duplicate key is reported. This prevents a temporary Registered from
    // releasing the user's object while the Registry lock is held.
    const auto entry_hold = hold;
    try {
        // Serial allocation is process-wide and monotonic, so this key is
        // unique even when several registries add concurrently.
        impl_->entries.try_emplace(std::string{identity_id.str()},
                                   Registered{.object = entry_hold,
                                              .id = identity_id,
                                              .type = identity,
                                              .logical_name = logical_name});
    } catch(...) {
        if(binding_inserted) {
            impl_->bindings.erase(binding_position);
        }
        throw;
    }
    return Result<ResourceId>::success(identity_id);
}

} // namespace axiom::resource
