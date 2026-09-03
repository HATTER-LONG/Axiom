#pragma once

#ifdef AXIOM_ENABLE_TEST_SEAMS

/**
 * @file dispatch_fault.hpp
 * @brief Non-installed test seam that forces HostState dispatch to throw after a lease is held.
 *
 * This header is not part of the installed PythonHost or Core public surface. Tests may
 * include it through a non-install include path. Dispatch faults are not a switch on
 * HostBridge.
 */

#include <axiom/python/export.hpp>

namespace axiom::python::detail {

/**
 * @brief Forces every HostBridge/HostHandle dispatch to throw while any guard is alive.
 *
 * The fault fires after the dispatch lease is acquired and before Core is called, so the
 * exception escapes the same way an unexpected failure during dispatch would. Overlapping
 * guards are reference-counted and may be destroyed in any order.
 */
class AXIOM_PYTHON_API DispatchFaultGuard {
public:
    DispatchFaultGuard();
    ~DispatchFaultGuard();

    DispatchFaultGuard(const DispatchFaultGuard&) = delete;
    DispatchFaultGuard(DispatchFaultGuard&&) = delete;
    DispatchFaultGuard& operator=(const DispatchFaultGuard&) = delete;
    DispatchFaultGuard& operator=(DispatchFaultGuard&&) = delete;
};

} // namespace axiom::python::detail

#endif
