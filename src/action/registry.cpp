#include "detail/registry.hpp"
#include <axiom/action/descriptor.hpp>
#include <axiom/action/detail/action.hpp>
#include <axiom/action/module.hpp>
#include <axiom/foundation/error.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/foundation/type_descriptor.hpp>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace axiom::detail {
namespace {

[[nodiscard]] Error registrationError(const ErrorCode code, std::string message) {
    return {
        .code = code,
        .message = std::move(message),
        .path = std::nullopt,
        .details = std::nullopt,
    };
}

[[nodiscard]] Result<void> unexpectedRegistrationFailure() {
    return Result<void>::failure({.code = ErrorCode::InternalError,
                                  .message = "Registry could not prepare registration",
                                  .path = std::nullopt,
                                  .details = std::nullopt});
}

/**
 * @brief Validates one staged Action without mutating Registry state.
 *
 * @tparam RegisteredActions Ordered Registry Action storage used for conflict checks.
 * @param pending Candidate descriptor and implementation.
 * @param module_descriptor Module that must own the Action identifier.
 * @param registered_actions Existing Registry Actions.
 * @return Success when pending may be committed, otherwise the first registration error.
 */
template <typename RegisteredActions>
[[nodiscard]] Result<void> validatePendingAction(const PendingAction& pending,
                                                 const ModuleDescriptor& module_descriptor,
                                                 const RegisteredActions& registered_actions) {
    if(!pending.descriptor) {
        return Result<void>::failure(
            registrationError(ErrorCode::InvalidArgument, "Action descriptor must not be null"));
    }
    auto action_validation = validate(*pending.descriptor);
    if(!action_validation) {
        return action_validation;
    }
    if(!pending.implementation) {
        return Result<void>::failure(registrationError(ErrorCode::InvalidArgument,
                                                       "Action implementation must not be null"));
    }
    const std::string action_id{pending.descriptor->id.str()};
    if(pending.descriptor->id.module() != module_descriptor.namespace_name) {
        return Result<void>::failure(registrationError(
            ErrorCode::InvalidDescriptor, "Action ID does not belong to Module: " + action_id));
    }
    if(registered_actions.contains(action_id)) {
        return Result<void>::failure(registrationError(
            ErrorCode::AlreadyExists, "Action is already registered: " + action_id));
    }
    return Result<void>::success();
}

[[nodiscard]] TypeDescriptor copyTypeDescriptor(const TypeDescriptor& source) {
    std::unordered_map<const TypeDescriptor*, std::shared_ptr<TypeDescriptor>> copies;
    std::vector<const TypeDescriptor*> pending;
    const auto ensure_copy = [&copies, &pending](const TypeDescriptor* node) {
        const auto [position, inserted] = copies.emplace(node, nullptr);
        if(inserted) {
            position->second = std::make_shared<TypeDescriptor>(TypeDescriptor{
                .kind = node->kind,
                .nullable = node->nullable,
                .description = node->description,
                .element_type = nullptr,
                .fields = {},
                .value_type = nullptr,
            });
            pending.push_back(node);
        }
        return position->second;
    };
    ensure_copy(&source);
    while(!pending.empty()) {
        const TypeDescriptor* node = pending.back();
        pending.pop_back();
        TypeDescriptor& copy = *copies.at(node);
        if(node->element_type) {
            copy.element_type = ensure_copy(node->element_type.get());
        }
        if(node->value_type) {
            copy.value_type = ensure_copy(node->value_type.get());
        }
        for(const auto& [name, field] : node->fields) {
            copy.fields.emplace(name, ensure_copy(field.get()));
        }
    }
    return *copies.at(&source);
}

[[nodiscard]] ActionDescriptor copyActionDescriptor(const ActionDescriptor& source) {
    std::vector<ParameterDescriptor> parameters;
    parameters.reserve(source.parameters.size());
    std::ranges::transform(source.parameters, std::back_inserter(parameters),
                           [](const ParameterDescriptor& parameter) {
                               return ParameterDescriptor{.name = parameter.name,
                                                          .description = parameter.description,
                                                          .required = parameter.required,
                                                          .type =
                                                              copyTypeDescriptor(parameter.type),
                                                          .default_value = parameter.default_value};
                           });
    return {
        .id = source.id,
        .description = source.description,
        .parameters = std::move(parameters),
        .return_type = copyTypeDescriptor(source.return_type),
        .version = source.version,
        .tags = source.tags,
        .metadata = source.metadata,
    };
}

} // namespace

Registry::Registry() : state_(std::make_shared<State>()) {}
Registry::~Registry() noexcept = default;

Result<void> Registry::registerModule(const ModuleDescriptor& descriptor) {
    try {
        auto validation = validate(descriptor);
        if(!validation) {
            return validation;
        }
        std::unique_lock lock{registration_mutex_};
        const auto current = state_.load(std::memory_order_acquire);
        if(current->modules.contains(descriptor.namespace_name)) {
            lock.unlock();
            return Result<void>::failure(
                registrationError(ErrorCode::AlreadyExists,
                                  "Module is already registered: " + descriptor.namespace_name));
        }

        auto next = std::make_shared<State>(*current);
        auto registered = std::make_shared<ModuleDescriptor>(descriptor);
        next->modules.emplace(descriptor.namespace_name, std::move(registered));
        state_.store(std::shared_ptr<const State>{std::move(next)}, std::memory_order_release);
        lock.unlock();
        return Result<void>::success();
    } catch(const std::exception&) {
        return unexpectedRegistrationFailure();
    } catch(...) {
        return unexpectedRegistrationFailure();
    }
}

Result<void> Registry::registerModuleWithActions(const ModuleDescriptor& descriptor,
                                                 std::vector<PendingAction>& pending_actions) {
    try {
        auto module_validation = validate(descriptor);
        if(!module_validation) {
            return module_validation;
        }
        std::unique_lock lock{registration_mutex_};
        const auto current = state_.load(std::memory_order_acquire);
        if(current->modules.contains(descriptor.namespace_name)) {
            lock.unlock();
            return Result<void>::failure(
                registrationError(ErrorCode::AlreadyExists,
                                  "Module is already registered: " + descriptor.namespace_name));
        }

        for(const auto& pending : pending_actions) {
            auto validation = validatePendingAction(pending, descriptor, current->actions);
            if(!validation) {
                lock.unlock();
                return validation;
            }
        }

        auto next = std::make_shared<State>(*current);
        auto registered_module = std::make_shared<ModuleDescriptor>(descriptor);
        next->modules.emplace(descriptor.namespace_name, std::move(registered_module));
        std::vector<std::shared_ptr<RegisteredAction>> prepared_actions;
        prepared_actions.reserve(pending_actions.size());
        for(const auto& pending : pending_actions) {
            const std::string action_id{pending.descriptor->id.str()};
            auto registered = std::make_shared<RegisteredAction>();
            registered->descriptor =
                std::make_unique<ActionDescriptor>(copyActionDescriptor(*pending.descriptor));
            const auto [entry, inserted] = next->actions.emplace(action_id, registered);
            static_cast<void>(entry);
            if(!inserted) {
                lock.unlock();
                return Result<void>::failure(registrationError(
                    ErrorCode::AlreadyExists, "Action is already registered: " + action_id));
            }
            prepared_actions.emplace_back(std::move(registered));
        }

        for(std::size_t index = 0; index < pending_actions.size(); ++index) {
            prepared_actions[index]->implementation =
                std::move(pending_actions[index].implementation);
        }
        state_.store(std::shared_ptr<const State>{std::move(next)}, std::memory_order_release);
        lock.unlock();
        return Result<void>::success();
    } catch(const std::exception&) {
        return unexpectedRegistrationFailure();
    } catch(...) {
        return unexpectedRegistrationFailure();
    }
}

Result<void> Registry::registerAction(const ActionDescriptor& descriptor,
                                      std::unique_ptr<IAction> implementation) {
    try {
        auto validation = validate(descriptor);
        if(!validation) {
            return validation;
        }
        if(!implementation) {
            return Result<void>::failure(registrationError(
                ErrorCode::InvalidArgument, "Action implementation must not be null"));
        }

        const std::string module_name{descriptor.id.module()};
        const std::string action_id{descriptor.id.str()};
        std::unique_lock lock{registration_mutex_};
        const auto current = state_.load(std::memory_order_acquire);
        if(!current->modules.contains(module_name)) {
            lock.unlock();
            return Result<void>::failure(
                registrationError(ErrorCode::NotFound, "Module is not registered: " + module_name));
        }
        if(current->actions.contains(action_id)) {
            lock.unlock();
            return Result<void>::failure(registrationError(
                ErrorCode::AlreadyExists, "Action is already registered: " + action_id));
        }

        auto next = std::make_shared<State>(*current);
        auto registered = std::make_shared<RegisteredAction>();
        registered->descriptor =
            std::make_unique<ActionDescriptor>(copyActionDescriptor(descriptor));
        const auto [entry, inserted] = next->actions.emplace(action_id, registered);
        static_cast<void>(entry);
        if(!inserted) {
            lock.unlock();
            return Result<void>::failure(registrationError(
                ErrorCode::InternalError, "Registry prepared a duplicate Action: " + action_id));
        }
        registered->implementation = std::move(implementation);
        state_.store(std::shared_ptr<const State>{std::move(next)}, std::memory_order_release);
        lock.unlock();
        return Result<void>::success();
    } catch(const std::exception&) {
        return unexpectedRegistrationFailure();
    } catch(...) {
        return unexpectedRegistrationFailure();
    }
}

Result<std::reference_wrapper<const ModuleDescriptor>>
Registry::findModule(const std::string_view namespace_name) const {
    const auto state = state_.load(std::memory_order_acquire);
    const auto module = state->modules.find(namespace_name);
    if(module == state->modules.end()) {
        return Result<std::reference_wrapper<const ModuleDescriptor>>::failure(registrationError(
            ErrorCode::NotFound, "Module is not registered: " + std::string{namespace_name}));
    }
    return Result<std::reference_wrapper<const ModuleDescriptor>>::success(
        std::cref(*module->second));
}

Result<std::reference_wrapper<const ActionDescriptor>>
Registry::findAction(const ActionId& id) const {
    const auto state = state_.load(std::memory_order_acquire);
    const auto action = state->actions.find(id.str());
    if(action == state->actions.end()) {
        return Result<std::reference_wrapper<const ActionDescriptor>>::failure(registrationError(
            ErrorCode::NotFound, "Action is not registered: " + std::string{id.str()}));
    }
    return Result<std::reference_wrapper<const ActionDescriptor>>::success(
        std::cref(*action->second->descriptor));
}

Result<std::reference_wrapper<IAction>> Registry::findImplementation(const ActionId& id) {
    const auto state = state_.load(std::memory_order_acquire);
    const auto action = state->actions.find(id.str());
    if(action == state->actions.end()) {
        return Result<std::reference_wrapper<IAction>>::failure(registrationError(
            ErrorCode::NotFound, "Action is not registered: " + std::string{id.str()}));
    }
    return Result<std::reference_wrapper<IAction>>::success(
        std::ref(*action->second->implementation));
}

std::vector<std::reference_wrapper<const ModuleDescriptor>> Registry::discoverModules() const {
    const auto state = state_.load(std::memory_order_acquire);
    std::vector<std::reference_wrapper<const ModuleDescriptor>> result;
    result.reserve(state->modules.size());
    for(const auto& [name, descriptor] : state->modules) {
        static_cast<void>(name);
        result.emplace_back(std::cref(*descriptor));
    }
    return result;
}

std::vector<std::reference_wrapper<const ActionDescriptor>> Registry::discoverActions() const {
    const auto state = state_.load(std::memory_order_acquire);
    std::vector<std::reference_wrapper<const ActionDescriptor>> result;
    result.reserve(state->actions.size());
    for(const auto& [id, action] : state->actions) {
        static_cast<void>(id);
        result.emplace_back(std::cref(*action->descriptor));
    }
    return result;
}

} // namespace axiom::detail
