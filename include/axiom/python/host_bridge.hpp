#pragma once

/**
 * @file host_bridge.hpp
 * @brief Revocable embedding contract between a C++ application and the Python adapter.
 */

#include <axiom/action/invocation_context.hpp>
#include <axiom/action/runtime.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/foundation/value.hpp>
#include <axiom/python/export.hpp>
#include <axiom/resource/resource_registry.hpp>
#include <axiom/task/task_registry.hpp>

#include <memory>
#include <string_view>

namespace axiom::python {

/**
 * @brief Opaque adapter-owned control block shared between a HostBridge and its handles.
 *
 * A HostState owns the revocable dispatch session but never owns the Core
 * sources. Applications cannot construct or inspect it directly.
 */
class AXIOM_PYTHON_API HostState;

/** @brief Shared observation handle declared early for HostBridge::attach(). */
class AXIOM_PYTHON_API HostHandle;

/**
 * @brief Application-side revocable session over one complete Command source set.
 *
 * The bridge binds a Runtime, ResourceRegistry, and TaskRegistry combination
 * atomically at construction; there is no partial attach path. `dispatch()`
 * calls acquire a short lease. Closing marks the session closed, rejects new
 * leases, waits for in-flight leases to finish, and then destroys the internal
 * dispatcher. Thereafter no source is accessed again through this bridge.
 *
 * The bridge shares its control block with every `HostHandle` produced by
 * `attach()`, so Python can keep a handle alive while the application destroys
 * the bridge object. Closing is idempotent and safe to call from several
 * threads concurrently with dispatch. Handles may outlive a closed session;
 * they then fail deterministically with `ErrorCode::HostClosed`.
 *
 * @pre The sources passed to the constructor must remain alive until `close()`
 *      (or destruction) completes, and source destruction must not overlap an
 *      in-flight dispatch. Handles impose no lifetime requirement on the
 *      sources once the session is closed.
 * @note Moving a bridge transfers the session; the moved-from bridge behaves
 *       like a never-attached bridge.
 */
class AXIOM_PYTHON_API HostBridge final {
public:
    /**
     * @brief Creates the session over one complete source combination.
     *
     * @param runtime Runtime that owns Modules and Actions.
     * @param resources Registry that owns Resource registrations.
     * @param tasks Registry that retains Task descriptors and accepts cancel.
     * @throws std::bad_alloc If the session state cannot be allocated.
     * @pre Each source remains alive until this bridge is closed or destroyed;
     *      handles may outlive the closed session but never touch the sources.
     */
    HostBridge(const Runtime& runtime,
               const resource::ResourceRegistry& resources,
               task::TaskRegistry& tasks);

    /**
     * @brief Closes the session without touching the sources.
     *
     * In-flight dispatches complete before the internal dispatcher is destroyed.
     */
    ~HostBridge() noexcept;

    HostBridge(const HostBridge&) = delete;
    HostBridge& operator=(const HostBridge&) = delete;
    HostBridge(HostBridge&& other) noexcept;
    HostBridge& operator=(HostBridge&& other) noexcept;

    /**
     * @brief Revokes the session and blocks until in-flight dispatches finish.
     *
     * Idempotent; never throws; the sources are not accessed after returning.
     */
    void close() noexcept;

    /**
     * @brief Reports whether the session is closed or was never attached.
     * @return `true` when no further dispatch can succeed through this bridge.
     */
    [[nodiscard]] bool closed() const noexcept;

    /**
     * @brief Produces the only sanctioned shared handle for the Python adapter.
     * @return A handle sharing this session's control block without owning it exclusively.
     */
    [[nodiscard]] HostHandle attach() const noexcept;

    /**
     * @brief Routes one Command through the session with a short lease.
     *
     * @param method Stable Command method name.
     * @param params Strict Command parameter object.
     * @param context Diagnostic context forwarded unchanged to the running Action.
     * @return An owned success Value or a structured failure. Dispatches after
     *         close fail with `ErrorCode::HostClosed` without touching sources.
     */
    [[nodiscard]] Result<Value> dispatch(std::string_view method,
                                         const Value::Object& params,
                                         const InvocationContext& context = {}) const;

private:
    std::shared_ptr<HostState> state_;
};

/**
 * @brief Shared, adapter-owned observation of one host session.
 *
 * A handle is a lightweight value sharing the bridge control block. It never
 * exposes source addresses, dispatcher state, or Core internals. Handles may
 * be copied, stored in Python objects, and outlive the application's bridge
 * object; every call on a closed or empty handle fails deterministically with
 * `ErrorCode::HostClosed`.
 */
class AXIOM_PYTHON_API HostHandle final {
public:
    /** @brief Creates an empty handle that reports closed and accepts nothing. */
    HostHandle() noexcept = default;

    /**
     * @brief Reports whether this handle has no session at all.
     * @return `true` for a default-constructed or moved-from handle.
     */
    [[nodiscard]] bool empty() const noexcept;

    /**
     * @brief Reports whether the shared session is closed or absent.
     * @return `true` when dispatch cannot succeed through this handle.
     */
    [[nodiscard]] bool closed() const noexcept;

    /**
     * @brief Routes one Command through the shared session.
     *
     * @param method Stable Command method name.
     * @param params Strict Command parameter object.
     * @param context Diagnostic context forwarded unchanged to the running Action.
     * @return Success or a structured failure; empty/closed handles fail with
     *         `ErrorCode::HostClosed`.
     */
    [[nodiscard]] Result<Value> dispatch(std::string_view method,
                                         const Value::Object& params,
                                         const InvocationContext& context = {}) const;

private:
    friend class HostBridge;

    explicit HostHandle(std::shared_ptr<HostState> state) noexcept;

    std::shared_ptr<HostState> state_;
};

} // namespace axiom::python