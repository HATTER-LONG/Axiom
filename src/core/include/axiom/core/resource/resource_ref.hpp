#pragma once

/**
 * @file resource_ref.hpp
 * @brief Move-only RAII access to a live resource after a successful resolve.
 */

#include <axiom/core/resource/resource_traits.hpp>

#include <memory>
#include <utility>

namespace axiom::core::resource {

class ResourceRegistry;

namespace detail {

/**
 * @brief Opaque hold that keeps a resolved object alive.
 *
 * Not a stable API. Callers cannot attach storage; only ResourceRegistry may
 * populate a live hold during a successful resolve.
 */
class ResourceKeepalive {
public:
    ResourceKeepalive() = default;
    ResourceKeepalive(const ResourceKeepalive&) = default;
    ResourceKeepalive(ResourceKeepalive&&) noexcept = default;
    ResourceKeepalive& operator=(const ResourceKeepalive&) = default;
    ResourceKeepalive& operator=(ResourceKeepalive&&) noexcept = default;
    ~ResourceKeepalive() = default;

private:
    friend class axiom::core::resource::ResourceRegistry;

    ResourceKeepalive(void* object, std::shared_ptr<void> hold) noexcept
        : object_(object), hold_(std::move(hold)) {}

    [[nodiscard]] void* get() const noexcept { return object_; }

    void* object_{nullptr};
    std::shared_ptr<void> hold_;
};

} // namespace detail

/**
 * @brief Move-only access object that keeps a resolved resource alive.
 *
 * A ResourceRef is produced only by a successful `ResourceRegistry::resolve`.
 * It is not default-constructible; there is no empty accessible reference outside
 * that success path. `operator->` and `operator*` yield access to the same object
 * observed by every other live ResourceRef for that registration. Pointers and
 * references obtained that way are valid only while this ResourceRef remains alive
 * and must not be cached across dynamic boundaries.
 *
 * Keepalive is internal. This type does not expose shared-ownership APIs such as
 * `shared_ptr`. Keepalive is not a thread-safety contract for the domain object.
 *
 * @tparam T Complete, non-cv, non-array object type with a non-throwing destructor
 *           and a valid ResourceTraits specialization.
 *
 * @note After a ResourceRef is moved from, only destruction or reassignment is
 *       allowed. Dereferencing a moved-from ResourceRef is undefined.
 */
template <typename T>
    requires detail::ResourceHandleTarget<T>
class ResourceRef {
public:
    ResourceRef() = delete;

    ResourceRef(const ResourceRef&) = delete;
    ResourceRef& operator=(const ResourceRef&) = delete;

    ResourceRef(ResourceRef&& other) noexcept
        : object_(other.object_), keepalive_(std::move(other.keepalive_)) {
        other.object_ = nullptr;
    }

    ResourceRef& operator=(ResourceRef&& other) noexcept {
        if(this != &other) {
            object_ = other.object_;
            keepalive_ = std::move(other.keepalive_);
            other.object_ = nullptr;
        }
        return *this;
    }

    ~ResourceRef() = default;

    /**
     * @brief Returns a pointer to the resolved object.
     * @return Pointer valid only while this ResourceRef remains alive.
     * @pre This ResourceRef is not in a moved-from state.
     */
    [[nodiscard]] T* operator->() const noexcept { return object_; }

    /**
     * @brief Returns a reference to the resolved object.
     * @return Reference valid only while this ResourceRef remains alive.
     * @pre This ResourceRef is not in a moved-from state.
     */
    [[nodiscard]] T& operator*() const noexcept { return *object_; }

private:
    friend class ResourceRegistry;

    ResourceRef(T* object, detail::ResourceKeepalive keepalive) noexcept
        : object_(object), keepalive_(std::move(keepalive)) {}

    T* object_;
    detail::ResourceKeepalive keepalive_;
};

} // namespace axiom::core::resource
