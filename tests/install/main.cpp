#include <axiom/core/core.hpp>

#include <cstdlib>
#include <memory>
#include <string>

int main() {
    axiom::core::logging::LogCollector collector{1};
    axiom::core::logging::LoggingService logging;
    const auto subscription = logging.addSink(std::make_shared<axiom::core::logging::CallbackSink>(
        [&collector](const axiom::core::logging::LogRecord& record) {
            collector.consume(record);
        }));
    logging.logger("install").write(axiom::core::logging::LogLevel::Info, "consumer logging works");
    axiom::core::ModuleBuilder builder{
        axiom::core::ModuleDescriptor{.namespace_name = "install", .metadata = {}}};
    const auto registered = builder.add(
        "answer", "Returns a stable answer",
        [](const double multiplier) { return 21.0 * multiplier; },
        axiom::core::param("multiplier", "Optional multiplier",
                           axiom::core::Value{std::int64_t{2}}));
    axiom::core::Runtime runtime;
    const auto installed = registered && runtime.registerModule(std::move(builder));
    const auto id = axiom::core::ActionId::parse("install.answer");
    const auto invoked = id ? runtime.invoke(id.value(), {}, {})
                            : axiom::core::Result<axiom::core::Value>::failure(id.error());
    return std::string{axiom::core::frameworkName()} == "Axiom" && installed && invoked &&
                   invoked.value().asNumber() == 42.0 && collector.records().size() == 1
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
