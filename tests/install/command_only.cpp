#include <axiom/command/command_dispatcher.hpp>

#include <cstdlib>

int main() {
    axiom::Runtime runtime;
    axiom::resource::ResourceRegistry resources;
    axiom::task::TaskRegistry tasks;
    const axiom::command::CommandDispatcher dispatcher{runtime, resources, tasks};
    const auto snapshot = dispatcher.dispatch("system.snapshot", {}, {});
    return snapshot && snapshot.value().isObject() && snapshot.value().asObject().size() == 4U
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
