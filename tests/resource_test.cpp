#include <axiom/core/base/error.hpp>
#include <axiom/core/resource/handle.hpp>
#include <axiom/core/resource/resource_id.hpp>
#include <axiom/core/resource/resource_traits.hpp>

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace {

struct Shape {};
struct Mesh {};
struct NoTraits {};
struct Incomplete;

struct ThrowingDestructor {
    ~ThrowingDestructor() noexcept(false) {}
};

struct UppercaseName {};
struct DigitPrefixedName {};
struct UnderscorePrefixedName {};
struct HyphenatedName {};
struct EmptyName {};

} // namespace

template <> struct axiom::core::resource::ResourceTraits<Shape> {
    static constexpr std::string_view type_name = "shape";
};

template <> struct axiom::core::resource::ResourceTraits<Mesh> {
    static constexpr std::string_view type_name = "mesh";
};

template <> struct axiom::core::resource::ResourceTraits<ThrowingDestructor> {
    static constexpr std::string_view type_name = "throwing";
};

template <> struct axiom::core::resource::ResourceTraits<UppercaseName> {
    static constexpr std::string_view type_name = "Shape";
};

template <> struct axiom::core::resource::ResourceTraits<DigitPrefixedName> {
    static constexpr std::string_view type_name = "1shape";
};

template <> struct axiom::core::resource::ResourceTraits<UnderscorePrefixedName> {
    static constexpr std::string_view type_name = "_shape";
};

template <> struct axiom::core::resource::ResourceTraits<HyphenatedName> {
    static constexpr std::string_view type_name = "shape-name";
};

template <> struct axiom::core::resource::ResourceTraits<EmptyName> {
    static constexpr std::string_view type_name = "";
};

template <> struct axiom::core::resource::ResourceTraits<Incomplete> {
    static constexpr std::string_view type_name = "incomplete";
};

namespace {

using axiom::core::ErrorCode;
using axiom::core::resource::Handle;
using axiom::core::resource::ResourceId;

template <typename T>
concept CanFormHandle = requires { typename Handle<T>; };

using ShapeAlias = Shape;

[[nodiscard]] ResourceId resourceId(const std::string_view text) {
    auto result = ResourceId::parse(text);
    EXPECT_TRUE(result) << text;
    return std::move(result.value());
}

TEST(ResourceId, ParsesCanonicalTypeAndSerial) {
    const auto result = ResourceId::parse("shape:42");

    ASSERT_TRUE(result);
    EXPECT_EQ(result.value().str(), "shape:42");
}

TEST(ResourceId, RoundTripsCanonicalIdentities) {
    for(const std::string_view text :
        {"a:1", "z:9", "shape:42", "type_2:10", "shape:18446744073709551615"}) {
        const auto result = ResourceId::parse(text);

        ASSERT_TRUE(result) << text;
        EXPECT_EQ(result.value().str(), text) << text;
        EXPECT_EQ(resourceId(text), result.value()) << text;
    }
}

TEST(ResourceId, RejectsIllegalIdentities) {
    for(const std::string_view invalid : {"",
                                          "shape",
                                          ":42",
                                          "shape:",
                                          "shape:0",
                                          "shape:01",
                                          "shape:00",
                                          "Shape:42",
                                          "1shape:1",
                                          "_shape:1",
                                          "shape-name:1",
                                          "shape:1:2",
                                          "shape:+1",
                                          "shape:-1",
                                          "shape: 1",
                                          " shape:1",
                                          "shape:1 ",
                                          "shape:4 2",
                                          "shape:1a",
                                          "shape:18446744073709551616",
                                          "shape:184467440737095516160"}) {
        const auto result = ResourceId::parse(invalid);

        EXPECT_FALSE(result) << invalid;
        EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument) << invalid;
    }
}

TEST(ResourceId, OrdersLexicographicallyOnCanonicalText) {
    const auto lower = resourceId("shape:10");
    const auto higher = resourceId("shape:9");
    const auto copy = resourceId("shape:10");

    EXPECT_EQ(lower, copy);
    EXPECT_LT(lower, higher);
    EXPECT_GT(higher, lower);
    EXPECT_EQ(lower <=> copy, std::strong_ordering::equal);
}

TEST(ResourceId, HashMatchesEquality) {
    const auto first = resourceId("shape:42");
    const auto second = resourceId("shape:42");
    const auto other = resourceId("mesh:42");

    EXPECT_EQ(first, second);
    EXPECT_EQ(std::hash<ResourceId>{}(first), std::hash<ResourceId>{}(second));
    EXPECT_NE(first, other);

    std::unordered_set<ResourceId> identities;
    identities.insert(first);
    EXPECT_TRUE(identities.contains(second));
    EXPECT_FALSE(identities.contains(other));
}

TEST(ResourceId, MoveTransfersIdentityToDestination) {
    ResourceId original = resourceId("shape:7");
    ResourceId moved{std::move(original)};

    EXPECT_EQ(moved.str(), "shape:7");
    original = resourceId("mesh:3");
    EXPECT_EQ(original.str(), "mesh:3");
}

TEST(Handle, WrapsResourceIdWithoutCrossTypeConversion) {
    const Handle<Shape> shape{resourceId("shape:42")};
    const Handle<Shape> copy = shape;
    const Handle<Mesh> mesh{resourceId("mesh:1")};

    EXPECT_EQ(shape.id().str(), "shape:42");
    EXPECT_EQ(shape, copy);
    EXPECT_EQ(std::hash<Handle<Shape>>{}(shape), std::hash<Handle<Shape>>{}(copy));
    EXPECT_LT(Handle<Shape>{resourceId("shape:10")}, Handle<Shape>{resourceId("shape:9")});
    EXPECT_NE(shape.id(), mesh.id());

    std::unordered_set<Handle<Shape>> handles;
    handles.insert(shape);
    EXPECT_TRUE(handles.contains(copy));
}

TEST(Handle, MoveTransfersIdentityToDestination) {
    Handle<Shape> original{resourceId("shape:8")};
    Handle<Shape> moved{std::move(original)};

    EXPECT_EQ(moved.id().str(), "shape:8");
    original = Handle<Shape>{resourceId("shape:9")};
    EXPECT_EQ(original.id().str(), "shape:9");
}

static_assert(CanFormHandle<Shape>);
static_assert(CanFormHandle<Mesh>);
static_assert(std::is_same_v<Handle<Shape>, Handle<ShapeAlias>>);
static_assert(!CanFormHandle<NoTraits>);
static_assert(!CanFormHandle<const Shape>);
static_assert(!CanFormHandle<volatile Shape>);
static_assert(!CanFormHandle<Shape[]>);
static_assert(!CanFormHandle<ThrowingDestructor>);
static_assert(!CanFormHandle<UppercaseName>);
static_assert(!CanFormHandle<DigitPrefixedName>);
static_assert(!CanFormHandle<UnderscorePrefixedName>);
static_assert(!CanFormHandle<HyphenatedName>);
static_assert(!CanFormHandle<EmptyName>);
static_assert(!CanFormHandle<Incomplete>);
static_assert(!std::is_convertible_v<Handle<Shape>, Handle<Mesh>>);
static_assert(!std::is_constructible_v<Handle<Mesh>, Handle<Shape>>);
static_assert(!std::is_convertible_v<ResourceId, Handle<Shape>>);
static_assert(std::is_constructible_v<Handle<Shape>, ResourceId>);
static_assert(!std::is_default_constructible_v<ResourceId>);
static_assert(!std::is_default_constructible_v<Handle<Shape>>);

} // namespace
