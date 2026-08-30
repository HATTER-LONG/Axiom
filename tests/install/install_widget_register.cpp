#include "install_widget.hpp"

#include <memory>
#include <utility>

axiom::core::Result<axiom::core::resource::Handle<InstallWidget>>
registerInstallWidget(axiom::core::resource::ResourceRegistry& registry, const int value) {
    auto widget = std::make_unique<InstallWidget>();
    widget->value = value;
    return registry.add(std::move(widget));
}
