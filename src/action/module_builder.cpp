#include <axiom/action/module_builder.hpp>

#include "detail/module_builder_state.hpp"
#include <axiom/action/action_id.hpp>
#include <axiom/action/descriptor.hpp>
#include <axiom/action/module.hpp>
#include <axiom/foundation/error.hpp>
#include <axiom/foundation/result.hpp>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
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

Result<void> ModuleBuilder::addPreparedAction(PreparedAction prepared) {
    if(!state_) {
        return Result<void>::failure(builderError(
            ErrorCode::InvalidArgument, "ModuleBuilder is empty or has already been registered"));
    }
    const std::string full_id =
        state_->descriptor.namespace_name + "." + std::string{prepared.action_name};
    auto id = ActionId::parse(full_id);
    if(!id) {
        return Result<void>::failure(id.error());
    }
    auto descriptor = std::make_unique<ActionDescriptor>(ActionDescriptor{
        .id = std::move(id.value()),
        .description = std::move(prepared.description),
        .parameters = std::move(prepared.parameters),
        .return_type = prepared.return_type,
        .version = std::move(prepared.version),
        .tags = std::move(prepared.tags),
        .metadata = std::move(prepared.metadata),
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
    prepared.implementation->bindActionId(descriptor->id);
    state_->actions.emplace_back(std::move(descriptor), std::move(prepared.implementation));
    return Result<void>::success();
}

} // namespace axiom
