#include <axiom/action/module_builder.hpp>

#include "detail/module_builder_state.hpp"
#include <axiom/action/action_id.hpp>
#include <axiom/action/descriptor.hpp>
#include <axiom/action/module.hpp>
#include <axiom/foundation/error.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/foundation/type_descriptor.hpp>

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace axiom {
namespace {

[[nodiscard]] Error builderError(const ErrorCode code, std::string message) {
    return {
        .code = code, .message = std::move(message), .path = std::nullopt, .details = std::nullopt};
}

} // namespace

ModuleBuilder::ModuleBuilder(const ModuleDescriptor& descriptor)
    : state_(std::make_unique<detail::ModuleBuilderState>(descriptor)) {}

ModuleBuilder::~ModuleBuilder() noexcept = default;
ModuleBuilder::ModuleBuilder(ModuleBuilder&&) noexcept = default;
ModuleBuilder& ModuleBuilder::operator=(ModuleBuilder&&) noexcept = default;

Result<void> ModuleBuilder::addPreparedAction(std::string_view action_name,
                                              std::string description,
                                              std::unique_ptr<detail::IAction> implementation,
                                              std::vector<ParameterDescriptor> parameters,
                                              const TypeDescriptor& return_type) {
    if(!state_) {
        return Result<void>::failure(builderError(
            ErrorCode::InvalidArgument, "ModuleBuilder is empty or has already been registered"));
    }
    const std::string full_id = state_->descriptor.namespace_name + "." + std::string{action_name};
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
        .metadata = {},
    });
    auto validation = validate(*descriptor);
    if(!validation) {
        return validation;
    }
    const auto duplicate = std::ranges::find_if(state_->actions, [&descriptor](const auto& action) {
        return action.descriptor->id.str() == descriptor->id.str();
    });
    if(duplicate != state_->actions.end()) {
        return Result<void>::failure(
            builderError(ErrorCode::AlreadyExists,
                         "Action is already pending: " + std::string{descriptor->id.str()}));
    }
    state_->actions.emplace_back(std::move(descriptor), std::move(implementation));
    return Result<void>::success();
}

} // namespace axiom
