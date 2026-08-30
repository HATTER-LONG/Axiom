#include "install_widget.hpp"

#include <functional>

bool checkInstalledResource() {
    const auto parsed = axiom::core::resource::ResourceId::parse("install_widget:42");
    if(!parsed) {
        return false;
    }
    const auto parsed_again = axiom::core::resource::ResourceId::parse(parsed.value().str());
    const std::hash<axiom::core::resource::ResourceId> hash_id{};
    if(!parsed_again || parsed_again.value() != parsed.value() ||
       hash_id(parsed.value()) != hash_id(parsed_again.value())) {
        return false;
    }

    axiom::core::resource::ResourceRegistry registry;
    auto added = registerInstallWidget(registry, 7);
    if(!added) {
        return false;
    }
    const auto handle = added.value();
    const auto id_roundtrip = axiom::core::resource::ResourceId::parse(handle.id().str());
    const std::hash<axiom::core::resource::Handle<InstallWidget>> hash_handle{};
    if(!id_roundtrip || id_roundtrip.value() != handle.id() ||
       hash_handle(handle) != hash_id(handle.id()) || !registry.contains(handle.id())) {
        return false;
    }

    auto resolved = resolveInstallWidgetValue(registry, handle);
    if(!resolved || resolved.value() != 7 || !registry.remove(handle.id()) ||
       registry.contains(handle.id()) || resolveInstallWidgetValue(registry, handle)) {
        return false;
    }
    return true;
}
