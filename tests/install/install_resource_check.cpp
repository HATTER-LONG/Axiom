#include "install_widget.hpp"

#include <functional>

namespace {

[[nodiscard]] bool parsedIdentityRoundTrips() {
    const auto parsed = axiom::core::resource::ResourceId::parse("install_widget:42");
    if(!parsed) {
        return false;
    }
    const auto parsed_again = axiom::core::resource::ResourceId::parse(parsed.value().str());
    if(!parsed_again) {
        return false;
    }
    if(parsed_again.value() != parsed.value()) {
        return false;
    }
    const std::hash<axiom::core::resource::ResourceId> hash_id{};
    return hash_id(parsed.value()) == hash_id(parsed_again.value());
}

[[nodiscard]] bool
registeredHandleRoundTrips(axiom::core::resource::ResourceRegistry& registry,
                           const axiom::core::resource::Handle<InstallWidget>& handle) {
    const auto id_roundtrip = axiom::core::resource::ResourceId::parse(handle.id().str());
    if(!id_roundtrip) {
        return false;
    }
    if(id_roundtrip.value() != handle.id()) {
        return false;
    }
    const std::hash<axiom::core::resource::ResourceId> hash_id{};
    const std::hash<axiom::core::resource::Handle<InstallWidget>> hash_handle{};
    return hash_handle(handle) == hash_id(handle.id()) && registry.contains(handle.id());
}

[[nodiscard]] bool
resolveRemoveAndLookup(axiom::core::resource::ResourceRegistry& registry,
                       const axiom::core::resource::Handle<InstallWidget>& handle) {
    const auto resolved = resolveInstallWidgetValue(registry, handle);
    if(!resolved || resolved.value() != 7) {
        return false;
    }
    if(!registry.remove(handle.id()) || registry.contains(handle.id())) {
        return false;
    }
    return !resolveInstallWidgetValue(registry, handle);
}

} // namespace

bool checkInstalledResource() {
    if(!parsedIdentityRoundTrips()) {
        return false;
    }

    axiom::core::resource::ResourceRegistry registry;
    const auto added = registerInstallWidget(registry, 7);
    if(!added) {
        return false;
    }
    const auto handle = added.value();
    return registeredHandleRoundTrips(registry, handle) && resolveRemoveAndLookup(registry, handle);
}
