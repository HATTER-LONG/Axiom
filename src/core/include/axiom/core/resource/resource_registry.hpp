#pragma once

/**
 * @file resource_registry.hpp
 * @brief Host-created registry that owns resource registrations and type bindings.
 */

#include <axiom/core/base/error.hpp>
#include <axiom/core/base/result.hpp>
#include <axiom/core/export.hpp>
#include <axiom/core/resource/handle.hpp>
#include <axiom/core/resource/resource_id.hpp>
#include <axiom/core/resource/resource_ref.hpp>
#include <axiom/core/resource/resource_traits.hpp>

#include <cstdint>
#include <memory>
#include <string_view>
#include <typeinfo>
#include <utility>

namespace axiom::core::resource {

/**
 * @brief Manages resource registration records, type bindings, and identity allocation.
 *
 * A ResourceRegistry is created by the host and is neither copyable nor movable.
 * Its public surface is only `add`, `resolve`, `remove`, and `contains`. The host
 * must serialize all operations and destruction of a given Registry. Distinct
 * Registries may be used independently; process-wide serial allocation remains
 * unique under concurrent use.
 *
 * Objects are not copied on resolve. A const Registry still yields mutable access.
 * Removing a registration or destroying the Registry drops the Registry's hold;
 * outstanding ResourceRefs continue to access the same object until the last ref
 * is released. Destruction order among resources is unspecified. Handles remain
 * usable as identity values after removal or Registry destruction; they do not
 * keep objects alive.
 *
 * One logical name maps to one C++ type for the Registry lifetime, including after
 * every instance of that type has been removed. A different C++ type claiming the
 * same logical name fails `add` with TypeMismatch. Failed `add` leaves existing
 * entries and type bindings unchanged. Ownership of the input `unique_ptr` has
 * already transferred by value; on failure the new object is released and is not
 * returned. Allocation failure may throw `std::bad_alloc` and is not converted
 * into InvocationFailed. Failed registration may consume a serial.
 *
 * @note Resource destructor callbacks must not re-enter the Registry that is
 *       currently removing or destroying them.
 */
class AXIOM_CORE_API ResourceRegistry {
public:
    /** @brief Creates an empty Registry with no registrations or type bindings. */
    ResourceRegistry();

    ResourceRegistry(const ResourceRegistry&) = delete;
    ResourceRegistry(ResourceRegistry&&) = delete;
    ResourceRegistry& operator=(const ResourceRegistry&) = delete;
    ResourceRegistry& operator=(ResourceRegistry&&) = delete;

    /**
     * @brief Drops every registration and the Registry's remaining holds.
     *
     * Outstanding ResourceRefs stay valid. Handles remain as identity values.
     */
    ~ResourceRegistry();

    /**
     * @brief Takes ownership of @p object and registers it under a new identity.
     *
     * Equal content may be registered separately and receives a distinct ID. A null
     * pointer returns InvalidArgument. Serial exhaustion returns InternalError.
     *
     * @tparam T Resource type satisfying ResourceHandleTarget.
     * @param object Object to own; must not be null.
     * @return A Handle naming the new registration, or a structured failure.
     * @throws std::bad_alloc If registration storage cannot be allocated.
     */
    template <typename T>
        requires detail::ResourceHandleTarget<T>
    [[nodiscard]] Result<Handle<T>> add(std::unique_ptr<T> object) {
        if(!object) {
            return Result<Handle<T>>::failure(nullResourceError());
        }
        T* const owned = object.release();
        auto committed = adopt(owned, &destroyResource<T>, ResourceTraits<T>::type_name, typeid(T));
        if(!committed) {
            return Result<Handle<T>>::failure(committed.error());
        }
        return Result<Handle<T>>::success(Handle<T>{std::move(committed.value())});
    }

    /**
     * @brief Resolves @p handle to a keepalive access object.
     *
     * Lookup is by full ID. Missing, removed, or foreign-Registry identities return
     * NotFound. A hit whose logical name or exact C++ type does not match @tparam T
     * returns TypeMismatch. Success yields a non-null ResourceRef. The object is not
     * copied.
     *
     * @tparam T Resource type satisfying ResourceHandleTarget.
     * @param handle Identity token to look up in this Registry.
     * @return A ResourceRef to the live object, or a structured failure.
     * @throws std::bad_alloc If the keepalive copy or failure diagnostics cannot be allocated.
     */
    template <typename T>
        requires detail::ResourceHandleTarget<T>
    [[nodiscard]] Result<ResourceRef<T>> resolve(const Handle<T>& handle) const {
        auto found = lookup(handle.id().str(), typeid(T), ResourceTraits<T>::type_name);
        if(found.status == LookupStatus::Missing) {
            return Result<ResourceRef<T>>::failure(resourceNotFoundError(handle.id().str()));
        }
        if(found.status == LookupStatus::TypeMismatch) {
            return Result<ResourceRef<T>>::failure(
                resourceTypeMismatchError({.id = handle.id().str(),
                                           .expected = ResourceTraits<T>::type_name,
                                           .actual = found.actual_logical_name}));
        }
        return Result<ResourceRef<T>>::success(
            ResourceRef<T>{static_cast<T*>(found.access.get()), std::move(found.access)});
    }

    /**
     * @brief Removes the registration for @p id if it is currently registered here.
     *
     * The first successful remove returns true. Missing or repeated remove returns
     * false with no other side effects. Removal does not cascade. Outstanding
     * ResourceRefs still access the object; it is destroyed immediately when none
     * remain, otherwise when the last ResourceRef is released.
     *
     * @param id Full identity to unregister.
     * @return true when this call removed a live registration.
     */
    [[nodiscard]] bool remove(const ResourceId& id);

    /**
     * @brief Reports whether this Registry currently registers @p id.
     *
     * A true result does not prove that a given `T` can resolve and does not keep
     * the object alive.
     *
     * @param id Full identity to query.
     * @return true when @p id is currently registered in this Registry.
     */
    [[nodiscard]] bool contains(const ResourceId& id) const;

private:
    class Impl;

    enum class LookupStatus : std::uint8_t { Missing, TypeMismatch, Found };

    struct TypeMismatchNames {
        std::string_view id;
        std::string_view expected;
        std::string_view actual;
    };

    struct LookupResult {
        LookupStatus status{LookupStatus::Missing};
        detail::ResourceKeepalive access;
        std::string_view actual_logical_name;
    };

    template <typename T> static void destroyResource(void* pointer) noexcept {
        delete static_cast<T*>(pointer);
    }

    [[nodiscard]] static Error nullResourceError();
    [[nodiscard]] static Error resourceNotFoundError(std::string_view id);
    [[nodiscard]] static Error resourceTypeMismatchError(const TypeMismatchNames& names);

    [[nodiscard]] Result<ResourceId> adopt(void* object,
                                           void (*destroy)(void*),
                                           std::string_view logical_name,
                                           const std::type_info& type);
    [[nodiscard]] LookupResult lookup(std::string_view id_text,
                                      const std::type_info& expected_type,
                                      std::string_view expected_name) const;

    std::unique_ptr<Impl> impl_;
};

} // namespace axiom::core::resource
