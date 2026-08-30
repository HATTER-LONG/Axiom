#include "install_widget.hpp"

axiom::core::Result<int>
resolveInstallWidgetValue(const axiom::core::resource::ResourceRegistry& registry,
                          const axiom::core::resource::Handle<InstallWidget>& handle) {
    auto resolved = registry.resolve(handle);
    if(!resolved) {
        return axiom::core::Result<int>::failure(resolved.error());
    }
    return axiom::core::Result<int>::success(resolved.value()->value);
}
