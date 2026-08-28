#include <axiom/core/action/module_builder.hpp>

#include <axiom/core/action/action_id.hpp>
#include <axiom/core/action/descriptor.hpp>
#include <axiom/core/action/detail/action.hpp>
#include <axiom/core/action/detail/module_builder_state.hpp>
#include <axiom/core/action/module.hpp>
#include <axiom/core/base/error.hpp>
#include <axiom/core/base/result.hpp>
#include <axiom/core/base/type_descriptor.hpp>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace axiom::core {
namespace {

[[nodiscard]] Error builderError(const ErrorCode code, std::string message) {
    return {
        .code = code, .message = std::move(message), .path = std::nullopt, .details = std::nullopt};
}

} // namespace

ModuleBuilder::ModuleBuilder(const ModuleDescriptor& descriptor)
    : state(std::make_unique<detail::ModuleBuilderState>(descriptor)) {}

ModuleBuilder::~ModuleBuilder() noexcept = default;
ModuleBuilder::ModuleBuilder(ModuleBuilder&&) noexcept = default;
ModuleBuilder& ModuleBuilder::operator=(ModuleBuilder&&) noexcept = default;

Result<void> ModuleBuilder::addPreparedAction(std::string_view action_name,
                                              std::string description,
                                              std::unique_ptr<detail::IAction> implementation,
                                              std::vector<ParameterDescriptor> parameters,
                                              const TypeDescriptor& return_type) {
    if(!state) {
        return Result<void>::failure(builderError(
            ErrorCode::InvalidArgument, "ModuleBuilder is empty or has already been registered"));
    }
    const std::string full_id = state->descriptor.namespace_name + "." + std::string{action_name};
    auto id = ActionId::parse(full_id);
    if(!id) {
        return Result<void>::failure(id.error());
    }
    auto descriptor = std::make_unique<ActionDescriptor>(ActionDescriptor{
        .id = std::move(id.value()),
        .description = std::move(description),
        .parameters = std::move(parameters),
        .return_type = return_type,
        .version = std::nullopt,
        .tags = {},
    });
    auto validation = validate(*descriptor);
    if(!validation) {
        return validation;
    }
    const auto duplicate = std::ranges::find_if(state->actions, [&descriptor](const auto& action) {
        return action.descriptor->id.str() == descriptor->id.str();
    });
    if(duplicate != state->actions.end()) {
        return Result<void>::failure(
            builderError(ErrorCode::AlreadyExists,
                         "Action is already pending: " + std::string{descriptor->id.str()}));
    }
    state->actions.emplace_back(std::move(descriptor), std::move(implementation));
    return Result<void>::success();
}

} // namespace axiom::core
