#pragma once

#include <axiom/core/core.hpp>

struct InstallWidget {
    int value = 0;
};

template <> struct axiom::core::resource::ResourceTraits<InstallWidget> {
    static constexpr std::string_view type_name = "install_widget";
};

[[nodiscard]] axiom::core::Result<axiom::core::resource::Handle<InstallWidget>>
registerInstallWidget(axiom::core::resource::ResourceRegistry& registry, int value);

[[nodiscard]] axiom::core::Result<int>
resolveInstallWidgetValue(const axiom::core::resource::ResourceRegistry& registry,
                          const axiom::core::resource::Handle<InstallWidget>& handle);

[[nodiscard]] bool checkInstalledResource();
