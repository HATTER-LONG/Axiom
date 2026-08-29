#include <axiom/core/action/action_id.hpp>
#include <axiom/core/action/descriptor.hpp>
#include <axiom/core/action/module.hpp>
#include <axiom/core/base/error.hpp>
#include <axiom/core/base/type_descriptor.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace {

axiom::core::ActionId actionId(const std::string_view text) {
    auto result = axiom::core::ActionId::parse(text);
    EXPECT_TRUE(result);
    return std::move(result.value());
}

axiom::core::TypeDescriptor integerType() {
    return {.kind = axiom::core::TypeDescriptor::Kind::Integer};
}

} // namespace

TEST(ActionId, ParsesCanonicalModuleAndActionComponents) {
    const auto result = axiom::core::ActionId::parse("math_2.add_3");

    ASSERT_TRUE(result);
    EXPECT_EQ(result.value().str(), "math_2.add_3");
    EXPECT_EQ(result.value().module(), "math_2");
    EXPECT_EQ(result.value().action(), "add_3");
}

TEST(ActionId, RejectsAnythingOtherThanTwoCanonicalComponents) {
    for(const std::string_view invalid :
        {"", "math", ".add", "math.", "math.add.more", "Math.add", "math-add", "math.add!"}) {
        const auto result = axiom::core::ActionId::parse(invalid);

        EXPECT_FALSE(result) << invalid;
        EXPECT_EQ(result.error().code, axiom::core::ErrorCode::InvalidArgument) << invalid;
    }
}

TEST(ActionId, AcceptsOneCharacterComponentsAtEveryIdentifierBoundary) {
    for(const std::string_view valid : {"a.a", "z.z", "0.0", "9.9", "_._"}) {
        const auto result = axiom::core::ActionId::parse(valid);

        ASSERT_TRUE(result) << valid;
        EXPECT_EQ(result.value().str(), valid);
    }
}

TEST(TypeDescriptor, RepresentsEveryValueShapeIncludingNestedObjectAndArray) {
    const auto descriptor = axiom::core::TypeDescriptor::object(
        {{"items", axiom::core::TypeDescriptor::nested(
                       axiom::core::TypeDescriptor::array(integerType(), "values"))},
         {"name", axiom::core::TypeDescriptor::nested(
                      {.kind = axiom::core::TypeDescriptor::Kind::String, .nullable = true})}},
        "request");

    EXPECT_TRUE(axiom::core::validate(descriptor));
    EXPECT_EQ(descriptor.kind, axiom::core::TypeDescriptor::Kind::Object);
    EXPECT_EQ(descriptor.fields.at("items")->element_type->kind,
              axiom::core::TypeDescriptor::Kind::Integer);
    EXPECT_TRUE(descriptor.fields.at("name")->nullable);
}

TEST(TypeDescriptor, RejectsIncoherentNestedShapes) {
    const axiom::core::TypeDescriptor missing_element{.kind =
                                                          axiom::core::TypeDescriptor::Kind::Array};
    const axiom::core::TypeDescriptor scalar_with_fields{
        .kind = axiom::core::TypeDescriptor::Kind::Integer,
        .fields = {{"invalid", axiom::core::TypeDescriptor::nested(integerType())}},
    };
    const auto nested_invalid = axiom::core::TypeDescriptor::object(
        {{"children", axiom::core::TypeDescriptor::nested(
                          {.kind = axiom::core::TypeDescriptor::Kind::Array})}});

    EXPECT_EQ(axiom::core::validate(missing_element).error().code,
              axiom::core::ErrorCode::InvalidDescriptor);
    EXPECT_EQ(axiom::core::validate(scalar_with_fields).error().code,
              axiom::core::ErrorCode::InvalidDescriptor);
    EXPECT_EQ(axiom::core::validate(nested_invalid).error().code,
              axiom::core::ErrorCode::InvalidDescriptor);
}

TEST(TypeDescriptor, RejectsSelfReferentialAndMultiNodeCycles) {
    auto self = std::make_shared<axiom::core::TypeDescriptor>();
    self->kind = axiom::core::TypeDescriptor::Kind::Array;
    self->element_type = self;

    auto first = std::make_shared<axiom::core::TypeDescriptor>();
    auto second = std::make_shared<axiom::core::TypeDescriptor>();
    first->kind = axiom::core::TypeDescriptor::Kind::Array;
    second->kind = axiom::core::TypeDescriptor::Kind::Array;
    first->element_type = second;
    second->element_type = first;

    const auto self_result = axiom::core::validate(*self);
    const auto multi_node_result = axiom::core::validate(*first);

    self->element_type.reset();
    first->element_type.reset();
    second->element_type.reset();

    ASSERT_FALSE(self_result);
    ASSERT_FALSE(multi_node_result);
    EXPECT_EQ(self_result.error().code, axiom::core::ErrorCode::InvalidDescriptor);
    EXPECT_EQ(multi_node_result.error().code, axiom::core::ErrorCode::InvalidDescriptor);
}

TEST(TypeDescriptor, RepresentsHomogeneousObjectMembersForStringKeyMaps) {
    const auto descriptor = axiom::core::TypeDescriptor::objectValues(
        axiom::core::TypeDescriptor::array({.kind = axiom::core::TypeDescriptor::Kind::Number}));

    ASSERT_TRUE(axiom::core::validate(descriptor));
    ASSERT_NE(descriptor.value_type, nullptr);
    EXPECT_TRUE(descriptor.fields.empty());
    EXPECT_EQ(descriptor.value_type->kind, axiom::core::TypeDescriptor::Kind::Array);
    EXPECT_EQ(descriptor.value_type->element_type->kind, axiom::core::TypeDescriptor::Kind::Number);
}

TEST(ActionDescriptor, PreservesParameterOrderWhenDescriptionIsValid) {
    const axiom::core::ActionDescriptor descriptor{
        .id = actionId("math.add"),
        .description = "Adds two integers",
        .parameters = {{.name = "left", .description = "Left operand", .type = integerType()},
                       {.name = "right", .description = "Right operand", .type = integerType()}},
        .return_type = integerType(),
        .version = "1.0",
        .tags = {"math", "basic"},
    };

    EXPECT_TRUE(axiom::core::validate(descriptor));
    ASSERT_EQ(descriptor.parameters.size(), 2U);
    EXPECT_EQ(descriptor.parameters[0].name, "left");
    EXPECT_EQ(descriptor.parameters[1].name, "right");
}

TEST(ActionDescriptor, RejectsInvalidOrDuplicateParameterNames) {
    const axiom::core::ActionDescriptor invalid_name{
        .id = actionId("math.add"),
        .parameters = {{.name = "left operand", .type = integerType()}},
        .return_type = integerType(),
    };
    const axiom::core::ActionDescriptor duplicate_name{
        .id = actionId("math.add"),
        .parameters = {{.name = "value", .type = integerType()},
                       {.name = "value", .type = integerType()}},
        .return_type = integerType(),
    };

    EXPECT_EQ(axiom::core::validate(invalid_name).error().code,
              axiom::core::ErrorCode::InvalidDescriptor);
    EXPECT_EQ(axiom::core::validate(duplicate_name).error().code,
              axiom::core::ErrorCode::InvalidDescriptor);
}

TEST(ActionDescriptor, AcceptsParameterNamesAtEveryIdentifierBoundary) {
    const axiom::core::ActionDescriptor descriptor{
        .id = actionId("math.boundaries"),
        .parameters = {{.name = "a", .type = integerType()},
                       {.name = "z", .type = integerType()},
                       {.name = "a0", .type = integerType()},
                       {.name = "z9", .type = integerType()},
                       {.name = "_", .type = integerType()}},
        .return_type = integerType(),
    };

    EXPECT_TRUE(axiom::core::validate(descriptor));
}

TEST(ActionDescriptor, RejectsInconsistentDefaultValues) {
    const axiom::core::ActionDescriptor required_default{
        .id = actionId("math.add"),
        .parameters = {{.name = "value",
                        .required = true,
                        .type = integerType(),
                        .default_value = axiom::core::Value{std::int64_t{1}}}},
        .return_type = integerType(),
    };
    const axiom::core::ActionDescriptor wrong_type_default{
        .id = actionId("math.add"),
        .parameters = {{.name = "value",
                        .required = false,
                        .type = integerType(),
                        .default_value = axiom::core::Value{"one"}}},
        .return_type = integerType(),
    };

    EXPECT_EQ(axiom::core::validate(required_default).error().code,
              axiom::core::ErrorCode::InvalidDescriptor);
    EXPECT_EQ(axiom::core::validate(wrong_type_default).error().code,
              axiom::core::ErrorCode::InvalidDescriptor);
}

TEST(ActionDescriptor, AcceptsCompatibleOptionalDefaultIncludingNestedValues) {
    const auto type = axiom::core::TypeDescriptor::object(
        {{"names", axiom::core::TypeDescriptor::nested(axiom::core::TypeDescriptor::array(
                       {.kind = axiom::core::TypeDescriptor::Kind::String}))}});
    const axiom::core::ActionDescriptor descriptor{
        .id = actionId("people.list"),
        .parameters = {{.name = "filter",
                        .required = false,
                        .type = type,
                        .default_value = axiom::core::Value{axiom::core::Value::Object{
                            {"names", axiom::core::Value{axiom::core::Value::Array{
                                          axiom::core::Value{"Ada"}}}}}}}},
        .return_type = type,
    };

    EXPECT_TRUE(axiom::core::validate(descriptor));
}

TEST(ActionDescriptor, AcceptsIntegerDefaultsForNumberParameters) {
    const axiom::core::ActionDescriptor descriptor{
        .id = actionId("math.scale"),
        .parameters = {{.name = "factor",
                        .required = false,
                        .type = {.kind = axiom::core::TypeDescriptor::Kind::Number},
                        .default_value = axiom::core::Value{std::int64_t{2}}}},
        .return_type = {.kind = axiom::core::TypeDescriptor::Kind::Number},
    };

    EXPECT_TRUE(axiom::core::validate(descriptor));
}

TEST(ActionDescriptor, RejectsNullAndMalformedContainerDefaults) {
    const auto array_of_integers = axiom::core::TypeDescriptor::array(integerType());
    const auto fixed_object = axiom::core::TypeDescriptor::object(
        {{"value", axiom::core::TypeDescriptor::nested(integerType())}});
    const auto values_object = axiom::core::TypeDescriptor::objectValues(integerType());
    const axiom::core::ActionDescriptor null_scalar{
        .id = actionId("math.null_default"),
        .parameters = {{.name = "value",
                        .required = false,
                        .type = integerType(),
                        .default_value = axiom::core::Value{nullptr}}},
        .return_type = integerType(),
    };
    const axiom::core::ActionDescriptor bad_array{
        .id = actionId("math.array_default"),
        .parameters = {{.name = "values",
                        .required = false,
                        .type = array_of_integers,
                        .default_value = axiom::core::Value{axiom::core::Value::Array{
                            axiom::core::Value{"wrong"}}}}},
        .return_type = integerType(),
    };
    const axiom::core::ActionDescriptor missing_fixed_member{
        .id = actionId("math.object_default"),
        .parameters = {{.name = "object",
                        .required = false,
                        .type = fixed_object,
                        .default_value = axiom::core::Value{axiom::core::Value::Object{}}}},
        .return_type = integerType(),
    };
    const axiom::core::ActionDescriptor bad_value_member{
        .id = actionId("math.values_default"),
        .parameters = {{.name = "object",
                        .required = false,
                        .type = values_object,
                        .default_value = axiom::core::Value{axiom::core::Value::Object{
                            {"key", axiom::core::Value{"wrong"}}}}}},
        .return_type = integerType(),
    };

    EXPECT_FALSE(axiom::core::validate(null_scalar));
    EXPECT_FALSE(axiom::core::validate(bad_array));
    EXPECT_FALSE(axiom::core::validate(missing_fixed_member));
    EXPECT_FALSE(axiom::core::validate(bad_value_member));
}

TEST(ModuleDescriptor, ValidatesNamespaceAndMetadataBeforeRegistration) {
    const axiom::core::ModuleDescriptor valid{
        .namespace_name = "math_2",
        .metadata = {{"display_name", "Mathematics"}, {"version", "1.0"}},
    };
    const axiom::core::ModuleDescriptor invalid{
        .namespace_name = "math-tools",
        .metadata = {{"display name", "Mathematics"}},
    };

    EXPECT_TRUE(axiom::core::validate(valid));
    EXPECT_EQ(axiom::core::validate(invalid).error().code,
              axiom::core::ErrorCode::InvalidDescriptor);
}

TEST(TypeDescriptor, ValidatesEveryScalarShape) {
    EXPECT_TRUE(axiom::core::validate({.kind = axiom::core::TypeDescriptor::Kind::Null}));
    EXPECT_TRUE(axiom::core::validate({.kind = axiom::core::TypeDescriptor::Kind::Boolean}));
    EXPECT_TRUE(axiom::core::validate({.kind = axiom::core::TypeDescriptor::Kind::Integer}));
    EXPECT_TRUE(axiom::core::validate({.kind = axiom::core::TypeDescriptor::Kind::Number}));
    EXPECT_TRUE(axiom::core::validate({.kind = axiom::core::TypeDescriptor::Kind::String}));
}

TEST(TypeDescriptor, RejectsUnknownKindValues) {
    const axiom::core::TypeDescriptor descriptor{
        .kind = static_cast<axiom::core::TypeDescriptor::Kind>(255)};

    const auto result = axiom::core::validate(descriptor);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, axiom::core::ErrorCode::InvalidDescriptor);
}

TEST(TypeDescriptor, RejectsInvalidFieldNamesAndIncoherentShapes) {
    const auto invalid_field = axiom::core::TypeDescriptor::object(
        {{"not valid", axiom::core::TypeDescriptor::nested(integerType())}});
    const auto missing_field_type = axiom::core::TypeDescriptor::object({{"value", nullptr}});
    const axiom::core::TypeDescriptor scalar_with_element{
        .kind = axiom::core::TypeDescriptor::Kind::String,
        .element_type = std::make_shared<axiom::core::TypeDescriptor>(integerType()),
    };
    const auto array_with_fields = axiom::core::TypeDescriptor::array(integerType());
    const auto invalid_array = axiom::core::TypeDescriptor{
        .kind = array_with_fields.kind,
        .element_type = array_with_fields.element_type,
        .fields = {{"value", axiom::core::TypeDescriptor::nested(integerType())}},
    };

    EXPECT_FALSE(axiom::core::validate(invalid_field));
    EXPECT_FALSE(axiom::core::validate(missing_field_type));
    EXPECT_FALSE(axiom::core::validate(scalar_with_element));
    EXPECT_FALSE(axiom::core::validate(invalid_array));
}

TEST(TypeDescriptor, AcceptsBoundaryCharactersInObjectFieldNames) {
    const auto descriptor = axiom::core::TypeDescriptor::object(
        {{"a", axiom::core::TypeDescriptor::nested(integerType())},
         {"z", axiom::core::TypeDescriptor::nested(integerType())},
         {"a0", axiom::core::TypeDescriptor::nested(integerType())},
         {"z9", axiom::core::TypeDescriptor::nested(integerType())},
         {"_", axiom::core::TypeDescriptor::nested(integerType())}});

    EXPECT_TRUE(axiom::core::validate(descriptor));
}

TEST(TypeDescriptor, RejectsConflictingObjectAndArrayMemberDefinitions) {
    const auto values = axiom::core::TypeDescriptor::objectValues(integerType());
    const auto fixed = axiom::core::TypeDescriptor::object(
        {{"value", axiom::core::TypeDescriptor::nested(integerType())}});
    const axiom::core::TypeDescriptor mixed_object{
        .kind = axiom::core::TypeDescriptor::Kind::Object,
        .fields = fixed.fields,
        .value_type = values.value_type,
    };
    const axiom::core::TypeDescriptor array_with_value_type{
        .kind = axiom::core::TypeDescriptor::Kind::Array,
        .element_type = axiom::core::TypeDescriptor::nested(integerType()),
        .value_type = axiom::core::TypeDescriptor::nested(integerType()),
    };

    EXPECT_FALSE(axiom::core::validate(mixed_object));
    EXPECT_FALSE(axiom::core::validate(array_with_value_type));
}

TEST(TypeDescriptor, ValidatesTheNestedValueTypeOfHomogeneousObjects) {
    const axiom::core::TypeDescriptor invalid_value_type{
        .kind = axiom::core::TypeDescriptor::Kind::Object,
        .value_type =
            axiom::core::TypeDescriptor::nested({.kind = axiom::core::TypeDescriptor::Kind::Array}),
    };
    const axiom::core::TypeDescriptor scalar_with_value_type{
        .kind = axiom::core::TypeDescriptor::Kind::String,
        .value_type = axiom::core::TypeDescriptor::nested(integerType()),
    };

    EXPECT_FALSE(axiom::core::validate(invalid_value_type));
    EXPECT_FALSE(axiom::core::validate(scalar_with_value_type));
}

TEST(ActionDescriptor, RejectsEmptyVersionAndAcceptsNullableDefaults) {
    const axiom::core::ActionDescriptor empty_version{
        .id = actionId("math.add"), .return_type = integerType(), .version = ""};
    const axiom::core::ActionDescriptor nullable_default{
        .id = actionId("math.add"),
        .parameters = {{.name = "value",
                        .required = false,
                        .type = {.kind = axiom::core::TypeDescriptor::Kind::String,
                                 .nullable = true},
                        .default_value = axiom::core::Value{nullptr}}},
        .return_type = integerType(),
    };

    EXPECT_FALSE(axiom::core::validate(empty_version));
    EXPECT_TRUE(axiom::core::validate(nullable_default));
}

TEST(ActionDescriptor, RejectsDefaultsWithIncompatibleContainerShapes) {
    const axiom::core::ActionDescriptor array_default{
        .id = actionId("shape.array"),
        .parameters = {{.name = "values",
                        .required = false,
                        .type = axiom::core::TypeDescriptor::array(integerType()),
                        .default_value = axiom::core::Value{axiom::core::Value::Object{}}}},
        .return_type = integerType(),
    };
    const axiom::core::ActionDescriptor object_default{
        .id = actionId("shape.object"),
        .parameters = {{.name = "value",
                        .required = false,
                        .type = axiom::core::TypeDescriptor::object(
                            {{"count", axiom::core::TypeDescriptor::nested(integerType())}}),
                        .default_value = axiom::core::Value{axiom::core::Value::Array{}}}},
        .return_type = integerType(),
    };

    EXPECT_FALSE(axiom::core::validate(array_default));
    EXPECT_FALSE(axiom::core::validate(object_default));
}

TEST(ActionDescriptor, RejectsDefaultsThatDoNotMatchObjectMembers) {
    const auto type = axiom::core::TypeDescriptor::object(
        {{"count", axiom::core::TypeDescriptor::nested(integerType())}});
    const axiom::core::ActionDescriptor missing_member{
        .id = actionId("shape.missing"),
        .parameters = {{.name = "value",
                        .required = false,
                        .type = type,
                        .default_value = axiom::core::Value{axiom::core::Value::Object{}}}},
        .return_type = integerType(),
    };
    const axiom::core::ActionDescriptor extra_member{
        .id = actionId("shape.extra"),
        .parameters = {{.name = "value",
                        .required = false,
                        .type = type,
                        .default_value = axiom::core::Value{axiom::core::Value::Object{
                            {"count", axiom::core::Value{std::int64_t{1}}},
                            {"other", axiom::core::Value{std::int64_t{2}}}}}}},
        .return_type = integerType(),
    };

    EXPECT_FALSE(axiom::core::validate(missing_member));
    EXPECT_FALSE(axiom::core::validate(extra_member));
}

TEST(ActionDescriptor, RejectsNullDefaultForNonNullableScalar) {
    const axiom::core::ActionDescriptor descriptor{
        .id = actionId("shape.null"),
        .parameters = {{.name = "value",
                        .required = false,
                        .type = integerType(),
                        .default_value = axiom::core::Value{nullptr}}},
        .return_type = integerType(),
    };

    EXPECT_FALSE(axiom::core::validate(descriptor));
}

TEST(ActionDescriptor, ValidatesDefaultsForEveryScalarCategory) {
    const axiom::core::ActionDescriptor boolean_default{
        .id = actionId("shape.boolean"),
        .parameters = {{.name = "value",
                        .required = false,
                        .type = {.kind = axiom::core::TypeDescriptor::Kind::Boolean},
                        .default_value = axiom::core::Value{true}}},
        .return_type = integerType(),
    };
    const axiom::core::ActionDescriptor null_default{
        .id = actionId("shape.null_value"),
        .parameters = {{.name = "value",
                        .required = false,
                        .type = {.kind = axiom::core::TypeDescriptor::Kind::Null},
                        .default_value = axiom::core::Value{nullptr}}},
        .return_type = integerType(),
    };
    const axiom::core::ActionDescriptor non_null_for_null_type{
        .id = actionId("shape.not_null"),
        .parameters = {{.name = "value",
                        .required = false,
                        .type = {.kind = axiom::core::TypeDescriptor::Kind::Null},
                        .default_value = axiom::core::Value{std::int64_t{1}}}},
        .return_type = integerType(),
    };
    const axiom::core::ActionDescriptor number_default{
        .id = actionId("shape.number"),
        .parameters = {{.name = "value",
                        .required = false,
                        .type = {.kind = axiom::core::TypeDescriptor::Kind::Number},
                        .default_value = axiom::core::Value{1.5}}},
        .return_type = integerType(),
    };

    EXPECT_TRUE(axiom::core::validate(boolean_default));
    EXPECT_TRUE(axiom::core::validate(null_default));
    EXPECT_FALSE(axiom::core::validate(non_null_for_null_type));
    EXPECT_TRUE(axiom::core::validate(number_default));
}

TEST(ActionDescriptor, ValidatesHomogeneousObjectDefaultsAndInvalidParameterTypes) {
    const auto values = axiom::core::TypeDescriptor::objectValues(integerType());
    const axiom::core::ActionDescriptor valid_default{
        .id = actionId("shape.values"),
        .parameters = {{.name = "value",
                        .required = false,
                        .type = values,
                        .default_value = axiom::core::Value{axiom::core::Value::Object{
                            {"first", axiom::core::Value{std::int64_t{1}}}}}}},
        .return_type = integerType(),
    };
    const axiom::core::ActionDescriptor invalid_member{
        .id = actionId("shape.invalid_value"),
        .parameters = {{.name = "value",
                        .required = false,
                        .type = values,
                        .default_value = axiom::core::Value{axiom::core::Value::Object{
                            {"first", axiom::core::Value{"one"}}}}}},
        .return_type = integerType(),
    };
    const axiom::core::ActionDescriptor invalid_parameter_type{
        .id = actionId("shape.invalid_type"),
        .parameters = {{.name = "value",
                        .type = {.kind = axiom::core::TypeDescriptor::Kind::Array}}},
        .return_type = integerType(),
    };

    EXPECT_TRUE(axiom::core::validate(valid_default));
    EXPECT_FALSE(axiom::core::validate(invalid_member));
    EXPECT_FALSE(axiom::core::validate(invalid_parameter_type));
}

TEST(ActionDescriptor, RejectsSameSizeObjectDefaultWithMissingNamedMember) {
    const auto type = axiom::core::TypeDescriptor::object(
        {{"first", axiom::core::TypeDescriptor::nested(integerType())},
         {"second", axiom::core::TypeDescriptor::nested(integerType())}});
    const axiom::core::ActionDescriptor descriptor{
        .id = actionId("shape.named"),
        .parameters = {{.name = "value",
                        .required = false,
                        .type = type,
                        .default_value = axiom::core::Value{axiom::core::Value::Object{
                            {"first", axiom::core::Value{std::int64_t{1}}},
                            {"other", axiom::core::Value{std::int64_t{2}}}}}}},
        .return_type = integerType(),
    };

    EXPECT_FALSE(axiom::core::validate(descriptor));
}

TEST(TypeDescriptor, RejectsConflictingAndInvalidContainerMembers) {
    const auto array_value_type = axiom::core::TypeDescriptor{
        .kind = axiom::core::TypeDescriptor::Kind::Array,
        .element_type = std::make_shared<axiom::core::TypeDescriptor>(integerType()),
        .value_type = std::make_shared<axiom::core::TypeDescriptor>(integerType()),
    };
    const auto object_both_member_forms = axiom::core::TypeDescriptor{
        .kind = axiom::core::TypeDescriptor::Kind::Object,
        .fields = {{"count", axiom::core::TypeDescriptor::nested(integerType())}},
        .value_type = std::make_shared<axiom::core::TypeDescriptor>(integerType()),
    };

    EXPECT_FALSE(axiom::core::validate(array_value_type));
    EXPECT_FALSE(axiom::core::validate(object_both_member_forms));
}

TEST(TypeDescriptor, RejectsScalarDescriptorsWithHomogeneousObjectMembers) {
    const axiom::core::TypeDescriptor descriptor{
        .kind = axiom::core::TypeDescriptor::Kind::String,
        .value_type = std::make_shared<axiom::core::TypeDescriptor>(integerType()),
    };

    EXPECT_FALSE(axiom::core::validate(descriptor));
}

TEST(TypeDescriptor, AcceptsSharedChildrenWithoutTreatingThemAsCycles) {
    const auto shared = std::make_shared<axiom::core::TypeDescriptor>(integerType());
    const auto descriptor =
        axiom::core::TypeDescriptor::object({{"first", shared}, {"second", shared}});

    EXPECT_TRUE(axiom::core::validate(descriptor));
}
