#pragma once

#include <axiom/core/action/detail/registry.hpp>

#include <vector>

namespace axiom::core::detail {

/** @brief Internal pending Module state owned by the public builder facade. */
class ModuleBuilderState {
public:
    explicit ModuleBuilderState(const ModuleDescriptor& module_descriptor)
        : descriptor(module_descriptor) {}

    ModuleDescriptor descriptor;
    std::vector<PendingAction> actions;
};

} // namespace axiom::core::detail
