#include <axiom/core/resource/resource_registry.hpp>

#include <axiom/core/base/error.hpp>
#include <axiom/core/base/value.hpp>
#include <axiom/core/resource/detail/resource_serial.hpp>
#include <axiom/core/resource/resource_id.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace axiom::core::resource {
namespace {

struct TransparentStringHash {
    using is_transparent = void;

    [[nodiscard]] std::size_t operator()(const std::string_view value) const noexcept {
        return std::hash<std::string_view>{}(value);
    }

    [[nodiscard]] std::size_t operator()(const std::string& value) const noexcept {
        return std::hash<std::string>{}(value);
    }
};

struct Registered {
    std::shared_ptr<void> object;
    detail::TypeIdentity type;
    std::string_view logical_name;
};

using EntryMap =
    std::unordered_map<std::string, Registered, TransparentStringHash, std::equal_to<>>;
using BindingMap =
    std::unordered_map<std::string, detail::TypeIdentity, TransparentStringHash, std::equal_to<>>;

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
    return parsed;
}

} // namespace

namespace detail {

Error nullResourceError() {
    return {
        .code = ErrorCode::InvalidArgument,
        .message = "Cannot register a null resource",
        .path = std::nullopt,
        .details = std::nullopt,
    };
}

Error resourceNotFoundError(const std::string_view id) {
    return {
        .code = ErrorCode::NotFound,
        .message = "Resource is not registered",
        .path = std::nullopt,
        .details = Value{Value::Object{{"id", Value{std::string{id}}}}},
    };
}

Error resourceTypeMismatchError(const std::string_view id,
                                const std::string_view expected,
                                const std::string_view actual) {
    return {
        .code = ErrorCode::TypeMismatch,
        .message = "Resource type does not match the requested handle type",
        .path = std::nullopt,
        .details = Value{Value::Object{{"id", Value{std::string{id}}},
                                       {"expected", Value{std::string{expected}}},
                                       {"actual", Value{std::string{actual}}}}},
    };
}

} // namespace detail

class ResourceRegistry::Impl {
public:
    [[nodiscard]] std::optional<Error> bindingConflict(std::string_view logical_name,
                                                       detail::TypeIdentity type) const;
    [[nodiscard]] Result<ResourceId> store(ResourceId identity,
                                           detail::RegistrationRequest request);
    [[nodiscard]] detail::Resolution lookup(std::string_view id_text,
                                            std::string_view expected_name,
                                            detail::TypeIdentity expected_type) const;

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

std::optional<Error>
ResourceRegistry::Impl::bindingConflict(const std::string_view logical_name,
                                        const detail::TypeIdentity type) const {
    const auto found = bindings.find(logical_name);
    if(found != bindings.end() && found->second != type) {
        return logicalNameConflictError(logical_name);
    }
    return std::nullopt;
}

Result<ResourceId> ResourceRegistry::Impl::store(ResourceId identity,
                                                 detail::RegistrationRequest request) {
    const auto [position, inserted] = entries.try_emplace(
        std::string{identity.str()},
        Registered{std::move(request.object), request.type, request.logical_name});
    if(!inserted) {
        return Result<ResourceId>::failure(duplicateIdentityError(position->first));
    }
    try {
        bindings.try_emplace(std::string{request.logical_name}, request.type);
    } catch(...) {
        entries.erase(position);
        throw;
    }
    return Result<ResourceId>::success(std::move(identity));
}

detail::Resolution ResourceRegistry::Impl::lookup(const std::string_view id_text,
                                                  const std::string_view expected_name,
                                                  const detail::TypeIdentity expected_type) const {
    const auto found = entries.find(id_text);
    if(found == entries.end()) {
        return {};
    }
    if(found->second.logical_name != expected_name || found->second.type != expected_type) {
        return {.status = detail::Resolution::Status::TypeMismatch,
                .actual_logical_name = found->second.logical_name};
    }
    return {.status = detail::Resolution::Status::Found,
            .object = found->second.object.get(),
            .keepalive = detail::ResourceKeepalive{found->second.object},
            .actual_logical_name = found->second.logical_name};
}

Result<ResourceId> ResourceRegistry::commit(detail::RegistrationRequest request) {
    if(const auto conflict = impl_->bindingConflict(request.logical_name, request.type)) {
        return Result<ResourceId>::failure(*conflict);
    }
    const auto serial = detail::allocateResourceSerial();
    if(!serial) {
        return Result<ResourceId>::failure(serial.error());
    }
    auto identity = identityFromSerial(request.logical_name, serial.value());
    if(!identity) {
        return identity;
    }
    return impl_->store(std::move(identity.value()), std::move(request));
}

detail::Resolution ResourceRegistry::lookup(const std::string_view id_text,
                                            const std::string_view expected_name,
                                            const detail::TypeIdentity expected_type) const {
    return impl_->lookup(id_text, expected_name, expected_type);
}

} // namespace axiom::core::resource
