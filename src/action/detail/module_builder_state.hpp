#pragma once

#include "registry.hpp"
#include <axiom/action/module.hpp>

#include <vector>

namespace axiom::detail {

/** @brief Internal pending Module state owned by the public builder facade. */
class ModuleBuilderState {
public:
    explicit ModuleBuilderState(const ModuleDescriptor& module_descriptor)
        : descriptor(module_descriptor) {}

    ModuleDescriptor descriptor;
    std::vector<PendingAction> actions;
};

} // namespace axiom::detail
