#include "resource_demo.hpp"

#include "accumulator.hpp"
#include "demo_output.hpp"

#include <axiom/core/core.hpp>

#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

// The application chooses a stable logical name, independent of C++ RTTI.
template <> struct axiom::core::resource::ResourceTraits<axiom::demo::Accumulator> {
    static constexpr std::string_view type_name = "demo_accumulator";
};

namespace axiom::demo {
namespace {

using core::ErrorCode;
using core::resource::Handle;
using core::resource::ResourceId;
using core::resource::ResourceRegistry;

bool showResourceLifetime(ResourceRegistry& resources, const Handle<Accumulator>& handle) {
    auto resolved = resources.resolve(handle);
    if(!resolved) {
        printError(resolved.error());
        return false;
    }
    // The ResourceRef owns the access lifetime; the Handle is only an identity.
    auto access = std::move(resolved.value());
    const auto total = access->add(10);
    std::printf("  resolved accumulator: total=%d\n", total);
    if(total != 10) {
        return false;
    }

    if(!resources.remove(handle.id()) || resources.contains(handle.id())) {
        std::puts("  expected removal to unregister the resource");
        return false;
    }
    const auto missing = resources.resolve(handle);
    if(missing || missing.error().code != ErrorCode::NotFound) {
        std::puts("  expected NotFound when resolving a removed resource");
        return false;
    }
    std::puts("  resolve after remove: NotFound");
    printError(missing.error());

    // Removing a registration does not invalidate an outstanding ResourceRef.
    const auto retained_total = access->add(5);
    std::printf("  retained ResourceRef after remove: total=%d\n", retained_total);
    // The resource is destroyed when access leaves scope.
    return retained_total == 15;
}

} // namespace

bool runResourceDemo() {
    std::puts("\nResources (registration, typed handle, RAII access):");
    ResourceRegistry resources;
    const auto added = resources.add(std::make_unique<Accumulator>());
    if(!added) {
        printError(added.error());
        return false;
    }
    const Handle<Accumulator>& handle = added.value();
    std::printf("  registered: ");
    printView(handle.id().str());
    std::puts("");

    // Only the identity travels as text; parsing it does not prove existence.
    const std::string id_text{handle.id().str()};
    const auto parsed = ResourceId::parse(id_text);
    if(!parsed) {
        printError(parsed.error());
        return false;
    }
    const Handle<Accumulator> restored{parsed.value()};
    if(restored != handle || !resources.contains(restored.id())) {
        std::puts("  expected the restored handle to name the registered resource");
        return false;
    }
    std::puts("  ResourceId text round-trip preserved the handle identity");
    return showResourceLifetime(resources, restored);
}

} // namespace axiom::demo
