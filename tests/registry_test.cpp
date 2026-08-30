#include "../src/action/detail/registry.hpp"
#include <axiom/action/action_id.hpp>
#include <axiom/action/descriptor.hpp>
#include <axiom/action/detail/action.hpp>
#include <axiom/action/invocation_context.hpp>
#include <axiom/foundation/error.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/foundation/type_descriptor.hpp>
#include <axiom/foundation/value.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using axiom::ActionDescriptor;
using axiom::ActionId;
using axiom::TypeDescriptor;
using axiom::detail::IAction;
using axiom::detail::PendingAction;
using axiom::detail::Registry;

class LifetimeTrackingAction final : public IAction {
public:
    explicit LifetimeTrackingAction(int& destruction_count) noexcept
        : destruction_count_(destruction_count) {}
    ~LifetimeTrackingAction() noexcept override { ++destruction_count_; }

    [[nodiscard]] axiom::Result<axiom::Value>
    invoke(const axiom::Arguments& arguments, const axiom::InvocationContext& context) override {
        static_cast<void>(arguments);
        static_cast<void>(context);
        return axiom::Result<axiom::Value>::success(axiom::Value{});
    }

private:
    int& destruction_count_;
};

ActionId actionId(const std::string_view text) {
    auto result = ActionId::parse(text);
    EXPECT_TRUE(result);
    return std::move(result.value());
}

TypeDescriptor integerType() {
    return {.kind = TypeDescriptor::Kind::Integer,
            .description = {},
            .element_type = {},
            .fields = {},
            .value_type = {}};
}

ActionDescriptor action(const std::string_view id) {
    return {
        .id = actionId(id),
        .description = "Test Action",
        .parameters = {},
        .return_type = integerType(),
        .version = std::nullopt,
        .tags = {},
    };
}

std::unique_ptr<IAction> implementation(int& destruction_count) {
    return std::make_unique<LifetimeTrackingAction>(destruction_count);
}

PendingAction pendingAction(const std::string_view id, int& destruction_count) {
    return {std::make_unique<ActionDescriptor>(action(id)), implementation(destruction_count)};
}

} // namespace

TEST(Registry, RegistersAndFindsOwnedModuleAndActionDescriptions) {
    int destruction_count = 0;
    Registry registry;

    ASSERT_TRUE(registry.registerModule({.namespace_name = "math", .metadata = {}}));
    ASSERT_TRUE(registry.registerAction(action("math.add"), implementation(destruction_count)));

    const auto module = registry.findModule("math");
    const auto registered_action = registry.findAction(actionId("math.add"));

    ASSERT_TRUE(module);
    ASSERT_TRUE(registered_action);
    EXPECT_EQ(module.value().get().namespace_name, "math");
    EXPECT_EQ(registered_action.value().get().id.str(), "math.add");
    EXPECT_EQ(destruction_count, 0);
}

TEST(Registry, DeepCopiesNestedDescriptorsIntoRecursivelyReadOnlyStorage) {
    static_assert(std::is_const_v<std::remove_pointer_t<
                      decltype(std::declval<const TypeDescriptor&>().element_type.get())>>);
    static_assert(std::is_const_v<std::remove_pointer_t<
                      decltype(std::declval<const TypeDescriptor&>().value_type.get())>>);

    int destruction_count = 0;
    Registry registry;
    ASSERT_TRUE(registry.registerModule({.namespace_name = "math", .metadata = {}}));
    auto mutable_leaf = std::make_shared<TypeDescriptor>(integerType());
    auto descriptor = action("math.describe");
    descriptor.return_type = {.kind = TypeDescriptor::Kind::Array,
                              .description = {},
                              .element_type = mutable_leaf,
                              .fields = {},
                              .value_type = {}};

    ASSERT_TRUE(registry.registerAction(descriptor, implementation(destruction_count)));
    mutable_leaf->kind = TypeDescriptor::Kind::String;
    const auto discovered = registry.findAction(actionId("math.describe"));

    ASSERT_TRUE(discovered);
    ASSERT_NE(discovered.value().get().return_type.element_type, nullptr);
    EXPECT_EQ(discovered.value().get().return_type.element_type->kind,
              TypeDescriptor::Kind::Integer);
}

TEST(Registry, DestroysRegisteredActionImplementationWithRegistry) {
    int destruction_count = 0;

    {
        Registry registry;
        ASSERT_TRUE(registry.registerModule({.namespace_name = "math", .metadata = {}}));
        ASSERT_TRUE(registry.registerAction(action("math.add"), implementation(destruction_count)));
        EXPECT_EQ(destruction_count, 0);
    }

    EXPECT_EQ(destruction_count, 1);
}

TEST(Registry, RejectsInvalidAndConflictingRegistrationsWithoutChangingState) {
    int destruction_count = 0;
    Registry registry;

    ASSERT_TRUE(registry.registerModule({.namespace_name = "math", .metadata = {}}));
    ASSERT_TRUE(registry.registerAction(action("math.add"), implementation(destruction_count)));

    const auto invalid_module =
        registry.registerModule({.namespace_name = "math-tools", .metadata = {}});
    const auto duplicate_module =
        registry.registerModule({.namespace_name = "math", .metadata = {}});
    auto invalid_action = action("math.invalid");
    invalid_action.return_type = {.kind = TypeDescriptor::Kind::Array,
                                  .description = {},
                                  .element_type = {},
                                  .fields = {},
                                  .value_type = {}};
    const auto invalid_action_result =
        registry.registerAction(invalid_action, implementation(destruction_count));
    const auto unknown_module =
        registry.registerAction(action("other.add"), implementation(destruction_count));
    const auto duplicate_action =
        registry.registerAction(action("math.add"), implementation(destruction_count));

    EXPECT_EQ(invalid_module.error().code, axiom::ErrorCode::InvalidDescriptor);
    EXPECT_EQ(duplicate_module.error().code, axiom::ErrorCode::AlreadyExists);
    EXPECT_EQ(invalid_action_result.error().code, axiom::ErrorCode::InvalidDescriptor);
    EXPECT_EQ(unknown_module.error().code, axiom::ErrorCode::NotFound);
    EXPECT_EQ(duplicate_action.error().code, axiom::ErrorCode::AlreadyExists);
    ASSERT_EQ(registry.discoverModules().size(), 1U);
    ASSERT_EQ(registry.discoverActions().size(), 1U);
    EXPECT_EQ(registry.discoverActions().front().get().id.str(), "math.add");
    EXPECT_EQ(destruction_count, 3);
}

TEST(Registry, RejectsUnknownDescriptorKindWithoutChangingState) {
    int destruction_count = 0;
    Registry registry;
    ASSERT_TRUE(registry.registerModule({.namespace_name = "math", .metadata = {}}));
    auto invalid = action("math.invalid_kind");
    invalid.return_type.kind = static_cast<TypeDescriptor::Kind>(255);

    const auto result = registry.registerAction(invalid, implementation(destruction_count));

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, axiom::ErrorCode::InvalidDescriptor);
    EXPECT_EQ(registry.discoverModules().size(), 1U);
    EXPECT_TRUE(registry.discoverActions().empty());
    EXPECT_EQ(destruction_count, 1);
}

TEST(Registry, AtomicallyRegistersPreparedModuleAndActionsAfterOwnershipTransfer) {
    int destruction_count = 0;
    Registry registry;
    std::vector<PendingAction> pending_actions;
    pending_actions.emplace_back(pendingAction("math.add", destruction_count));
    pending_actions.emplace_back(pendingAction("math.subtract", destruction_count));

    const auto result = registry.registerModuleWithActions(
        {.namespace_name = "math", .metadata = {{"title", "Math"}}}, pending_actions);

    ASSERT_TRUE(result);
    EXPECT_EQ(registry.discoverModules().size(), 1U);
    ASSERT_EQ(registry.discoverActions().size(), 2U);
    EXPECT_EQ(registry.discoverActions()[0].get().id.str(), "math.add");
    EXPECT_EQ(registry.discoverActions()[1].get().id.str(), "math.subtract");
    EXPECT_EQ(pending_actions.size(), 2U);
    EXPECT_EQ(pending_actions[0].implementation, nullptr);
    EXPECT_EQ(pending_actions[1].implementation, nullptr);
    EXPECT_EQ(destruction_count, 0);
}

TEST(Registry, PreservesRegistryAndPendingActionsWhenPreparedModuleRegistrationFails) {
    int destruction_count = 0;
    Registry registry;
    std::vector<PendingAction> pending_actions;
    pending_actions.emplace_back(pendingAction("math.add", destruction_count));
    pending_actions.emplace_back(pendingAction("math.add", destruction_count));

    const auto result = registry.registerModuleWithActions(
        {.namespace_name = "math", .metadata = {}}, pending_actions);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, axiom::ErrorCode::AlreadyExists);
    EXPECT_TRUE(registry.discoverModules().empty());
    EXPECT_TRUE(registry.discoverActions().empty());
    ASSERT_EQ(pending_actions.size(), 2U);
    EXPECT_NE(pending_actions[0].descriptor, nullptr);
    EXPECT_NE(pending_actions[0].implementation, nullptr);
    EXPECT_NE(pending_actions[1].descriptor, nullptr);
    EXPECT_NE(pending_actions[1].implementation, nullptr);
    EXPECT_EQ(destruction_count, 0);
}

TEST(Registry, RejectsEachInvalidPreparedActionBeforeChangingOwnershipOrState) {
    int destruction_count = 0;
    const axiom::ModuleDescriptor module{.namespace_name = "math", .metadata = {}};

    Registry missing_descriptor_registry;
    std::vector<PendingAction> missing_descriptor;
    missing_descriptor.emplace_back(nullptr, implementation(destruction_count));

    const auto missing_descriptor_result =
        missing_descriptor_registry.registerModuleWithActions(module, missing_descriptor);

    EXPECT_EQ(missing_descriptor_result.error().code, axiom::ErrorCode::InvalidArgument);
    EXPECT_TRUE(missing_descriptor_registry.discoverModules().empty());
    EXPECT_NE(missing_descriptor.front().implementation, nullptr);

    Registry invalid_descriptor_registry;
    std::vector<PendingAction> invalid_descriptor;
    invalid_descriptor.emplace_back(pendingAction("math.invalid", destruction_count));
    invalid_descriptor.front().descriptor->return_type = {.kind = TypeDescriptor::Kind::Array,
                                                          .description = {},
                                                          .element_type = {},
                                                          .fields = {},
                                                          .value_type = {}};

    const auto invalid_descriptor_result =
        invalid_descriptor_registry.registerModuleWithActions(module, invalid_descriptor);

    EXPECT_EQ(invalid_descriptor_result.error().code, axiom::ErrorCode::InvalidDescriptor);
    EXPECT_TRUE(invalid_descriptor_registry.discoverModules().empty());
    EXPECT_NE(invalid_descriptor.front().implementation, nullptr);

    Registry missing_implementation_registry;
    std::vector<PendingAction> missing_implementation;
    missing_implementation.emplace_back(std::make_unique<ActionDescriptor>(action("math.null")),
                                        nullptr);

    const auto missing_implementation_result =
        missing_implementation_registry.registerModuleWithActions(module, missing_implementation);

    EXPECT_EQ(missing_implementation_result.error().code, axiom::ErrorCode::InvalidArgument);
    EXPECT_TRUE(missing_implementation_registry.discoverModules().empty());
    EXPECT_NE(missing_implementation.front().descriptor, nullptr);

    Registry wrong_module_registry;
    std::vector<PendingAction> wrong_module;
    wrong_module.emplace_back(pendingAction("other.add", destruction_count));

    const auto wrong_module_result =
        wrong_module_registry.registerModuleWithActions(module, wrong_module);

    EXPECT_EQ(wrong_module_result.error().code, axiom::ErrorCode::InvalidDescriptor);
    EXPECT_TRUE(wrong_module_registry.discoverModules().empty());
    EXPECT_NE(wrong_module.front().implementation, nullptr);
}

TEST(Registry, RejectsNullDirectActionImplementationWithoutChangingRegisteredModule) {
    Registry registry;
    ASSERT_TRUE(registry.registerModule({.namespace_name = "math", .metadata = {}}));

    const auto result = registry.registerAction(action("math.add"), nullptr);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, axiom::ErrorCode::InvalidArgument);
    EXPECT_EQ(registry.discoverModules().size(), 1U);
    EXPECT_TRUE(registry.discoverActions().empty());
}

TEST(Registry, ReturnsNotFoundForUnknownModuleAndAction) {
    Registry registry;

    const auto module = registry.findModule("math");
    const auto registered_action = registry.findAction(actionId("math.add"));

    EXPECT_EQ(module.error().code, axiom::ErrorCode::NotFound);
    EXPECT_EQ(registered_action.error().code, axiom::ErrorCode::NotFound);
}

TEST(Registry, DiscoversDescriptionsInStableCanonicalOrder) {
    int destruction_count = 0;
    Registry registry;

    ASSERT_TRUE(registry.registerModule({.namespace_name = "zeta", .metadata = {}}));
    ASSERT_TRUE(registry.registerModule({.namespace_name = "alpha", .metadata = {}}));
    ASSERT_TRUE(registry.registerModule({.namespace_name = "middle", .metadata = {}}));
    ASSERT_TRUE(registry.registerAction(action("zeta.last"), implementation(destruction_count)));
    ASSERT_TRUE(registry.registerAction(action("alpha.first"), implementation(destruction_count)));
    ASSERT_TRUE(registry.registerAction(action("middle.next"), implementation(destruction_count)));

    const auto modules = registry.discoverModules();
    const auto actions = registry.discoverActions();

    ASSERT_EQ(modules.size(), 3U);
    ASSERT_EQ(actions.size(), 3U);
    EXPECT_EQ(modules[0].get().namespace_name, "alpha");
    EXPECT_EQ(modules[1].get().namespace_name, "middle");
    EXPECT_EQ(modules[2].get().namespace_name, "zeta");
    EXPECT_EQ(actions[0].get().id.str(), "alpha.first");
    EXPECT_EQ(actions[1].get().id.str(), "middle.next");
    EXPECT_EQ(actions[2].get().id.str(), "zeta.last");
}
