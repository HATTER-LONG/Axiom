#include <axiom/core/base/error.hpp>
#include <axiom/core/resource/detail/resource_serial.hpp>
#include <axiom/core/resource/handle.hpp>
#include <axiom/core/resource/resource_id.hpp>
#include <axiom/core/resource/resource_ref.hpp>
#include <axiom/core/resource/resource_registry.hpp>
#include <axiom/core/resource/resource_traits.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

struct Shape {};
struct Mesh {};
struct UniqueBody {
    UniqueBody() = default;
    UniqueBody(const UniqueBody&) = delete;
    UniqueBody& operator=(const UniqueBody&) = delete;
    UniqueBody(UniqueBody&&) = delete;
    UniqueBody& operator=(UniqueBody&&) = delete;
    int value = 0;
};
struct Counted {
    static int constructed;
    static int destroyed;
    Counted() { ++constructed; }
    ~Counted() { ++destroyed; }
};
int Counted::constructed = 0;
int Counted::destroyed = 0;
struct ShapeImpostor {
    static int destroyed;
    ShapeImpostor() = default;
    ~ShapeImpostor() { ++destroyed; }
};
int ShapeImpostor::destroyed = 0;
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

template <> struct axiom::core::resource::ResourceTraits<UniqueBody> {
    static constexpr std::string_view type_name = "body";
};

template <> struct axiom::core::resource::ResourceTraits<Counted> {
    static constexpr std::string_view type_name = "counted";
};

template <> struct axiom::core::resource::ResourceTraits<ShapeImpostor> {
    static constexpr std::string_view type_name = "shape";
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
using axiom::core::resource::ResourceRef;
using axiom::core::resource::ResourceRegistry;

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
static_assert(!std::is_default_constructible_v<ResourceRef<Shape>>);
static_assert(!std::is_copy_constructible_v<ResourceRef<Shape>>);
static_assert(!std::is_copy_assignable_v<ResourceRef<Shape>>);
static_assert(std::is_move_constructible_v<ResourceRef<Shape>>);
static_assert(std::is_move_assignable_v<ResourceRef<Shape>>);
static_assert(!std::is_copy_constructible_v<ResourceRegistry>);
static_assert(!std::is_move_constructible_v<ResourceRegistry>);
static_assert(!std::is_copy_assignable_v<ResourceRegistry>);
static_assert(!std::is_move_assignable_v<ResourceRegistry>);

void resetLifetimeCounters() {
    Counted::constructed = 0;
    Counted::destroyed = 0;
    ShapeImpostor::destroyed = 0;
}

TEST(ResourceRegistry, RegistersAndMutatesNonCopyableObject) {
    ResourceRegistry registry;
    auto added = registry.add(std::make_unique<UniqueBody>());
    ASSERT_TRUE(added);
    EXPECT_TRUE(registry.contains(added.value().id()));

    auto resolved = registry.resolve(added.value());
    ASSERT_TRUE(resolved);
    resolved.value()->value += 11;
    EXPECT_EQ((*resolved.value()).value, 11);
}

TEST(ResourceRegistry, MultipleResolvesObserveTheSameObject) {
    ResourceRegistry registry;
    auto added = registry.add(std::make_unique<UniqueBody>());
    ASSERT_TRUE(added);

    auto first = registry.resolve(added.value());
    auto second = registry.resolve(added.value());
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    first.value()->value = 3;
    EXPECT_EQ(second.value()->value, 3);
    EXPECT_EQ(&*first.value(), &*second.value());
}

TEST(ResourceRegistry, HandleCopyDoesNotExtendLifetime) {
    resetLifetimeCounters();
    ResourceRegistry registry;
    auto added = registry.add(std::make_unique<Counted>());
    ASSERT_TRUE(added);
    const Handle<Counted> copy = added.value();
    EXPECT_EQ(Counted::constructed, 1);
    EXPECT_TRUE(registry.remove(copy.id()));
    EXPECT_EQ(Counted::destroyed, 1);
    EXPECT_FALSE(registry.contains(added.value().id()));
}

TEST(ResourceRegistry, RejectsNullInput) {
    ResourceRegistry registry;
    std::unique_ptr<Shape> empty;
    const auto added = registry.add(std::move(empty));
    EXPECT_FALSE(added);
    EXPECT_EQ(added.error().code, ErrorCode::InvalidArgument);
}

TEST(ResourceRegistry, UnknownIdIsNotFound) {
    ResourceRegistry registry;
    const Handle<Shape> unknown{resourceId("shape:1")};
    EXPECT_FALSE(registry.contains(unknown.id()));
    const auto resolved = registry.resolve(unknown);
    EXPECT_FALSE(resolved);
    EXPECT_EQ(resolved.error().code, ErrorCode::NotFound);
}

TEST(ResourceRegistry, CrossRegistryIdIsNotFound) {
    ResourceRegistry first;
    ResourceRegistry second;
    auto added = first.add(std::make_unique<Shape>());
    ASSERT_TRUE(added);
    EXPECT_FALSE(second.contains(added.value().id()));
    const auto resolved = second.resolve(added.value());
    EXPECT_FALSE(resolved);
    EXPECT_EQ(resolved.error().code, ErrorCode::NotFound);
}

TEST(ResourceRegistry, WrongTypeHandleIsTypeMismatch) {
    ResourceRegistry registry;
    auto added = registry.add(std::make_unique<Shape>());
    ASSERT_TRUE(added);
    const Handle<Mesh> wrong{added.value().id()};
    const auto resolved = registry.resolve(wrong);
    EXPECT_FALSE(resolved);
    EXPECT_EQ(resolved.error().code, ErrorCode::TypeMismatch);
}

TEST(ResourceRegistry, SameLogicalNameDifferentCppTypesIsTypeMismatch) {
    ResourceRegistry registry;
    auto added = registry.add(std::make_unique<Shape>());
    ASSERT_TRUE(added);
    resetLifetimeCounters();
    const auto rejected = registry.add(std::make_unique<ShapeImpostor>());
    EXPECT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, ErrorCode::TypeMismatch);
    EXPECT_EQ(ShapeImpostor::destroyed, 1);
    EXPECT_TRUE(registry.contains(added.value().id()));
    EXPECT_TRUE(registry.resolve(added.value()));
}

TEST(ResourceRegistry, FailedAddDoesNotCorruptExistingEntries) {
    ResourceRegistry registry;
    auto first = registry.add(std::make_unique<Shape>());
    auto second = registry.add(std::make_unique<Mesh>());
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_FALSE(registry.add(std::make_unique<ShapeImpostor>()));
    EXPECT_TRUE(registry.contains(first.value().id()));
    EXPECT_TRUE(registry.contains(second.value().id()));
    EXPECT_NE(first.value().id(), second.value().id());
}

TEST(ResourceRegistry, BindingSurvivesRemovalOfAllInstances) {
    ResourceRegistry registry;
    auto added = registry.add(std::make_unique<Shape>());
    ASSERT_TRUE(added);
    EXPECT_TRUE(registry.remove(added.value().id()));
    EXPECT_FALSE(registry.contains(added.value().id()));
    const auto rejected = registry.add(std::make_unique<ShapeImpostor>());
    EXPECT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, ErrorCode::TypeMismatch);
    auto again = registry.add(std::make_unique<Shape>());
    ASSERT_TRUE(again);
    EXPECT_NE(added.value().id(), again.value().id());
}

TEST(ResourceRegistry, RepeatRemoveHasNoSideEffects) {
    ResourceRegistry registry;
    auto added = registry.add(std::make_unique<Shape>());
    ASSERT_TRUE(added);
    EXPECT_TRUE(registry.remove(added.value().id()));
    EXPECT_FALSE(registry.remove(added.value().id()));
    EXPECT_FALSE(registry.contains(added.value().id()));
}

TEST(ResourceRegistry, ResolveAfterRemoveIsNotFoundWhileRefRemainsValid) {
    ResourceRegistry registry;
    auto added = registry.add(std::make_unique<UniqueBody>());
    ASSERT_TRUE(added);
    auto kept = registry.resolve(added.value());
    ASSERT_TRUE(kept);
    kept.value()->value = 9;
    EXPECT_TRUE(registry.remove(added.value().id()));
    EXPECT_FALSE(registry.contains(added.value().id()));
    const auto later = registry.resolve(added.value());
    EXPECT_FALSE(later);
    EXPECT_EQ(later.error().code, ErrorCode::NotFound);
    EXPECT_EQ(kept.value()->value, 9);
}

TEST(ResourceRegistry, ResourceRefSurvivesRegistryDestruction) {
    std::optional<ResourceRef<UniqueBody>> kept;
    {
        ResourceRegistry registry;
        auto added = registry.add(std::make_unique<UniqueBody>());
        ASSERT_TRUE(added);
        auto resolved = registry.resolve(added.value());
        ASSERT_TRUE(resolved);
        resolved.value()->value = 4;
        kept = std::move(resolved.value());
    }
    ASSERT_TRUE(kept.has_value());
    EXPECT_EQ((*kept)->value, 4);
}

TEST(ResourceRegistry, LastRefReleaseDestroysExactlyOnce) {
    resetLifetimeCounters();
    ResourceRegistry registry;
    auto added = registry.add(std::make_unique<Counted>());
    ASSERT_TRUE(added);
    {
        auto first = registry.resolve(added.value());
        auto second = registry.resolve(added.value());
        ASSERT_TRUE(first);
        ASSERT_TRUE(second);
        EXPECT_TRUE(registry.remove(added.value().id()));
        EXPECT_EQ(Counted::destroyed, 0);
        auto moved = std::move(first.value());
        (void)moved;
    }
    EXPECT_EQ(Counted::destroyed, 1);
    EXPECT_EQ(Counted::constructed, 1);
}

TEST(ResourceRegistry, ConstRegistryYieldsMutableAccess) {
    ResourceRegistry registry;
    auto added = registry.add(std::make_unique<UniqueBody>());
    ASSERT_TRUE(added);
    const ResourceRegistry& frozen = registry;
    auto resolved = frozen.resolve(added.value());
    ASSERT_TRUE(resolved);
    resolved.value()->value = 2;
    EXPECT_EQ(registry.resolve(added.value()).value()->value, 2);
}

TEST(ResourceRegistry, EqualContentReceivesDistinctIds) {
    ResourceRegistry registry;
    auto first = registry.add(std::make_unique<Shape>());
    auto second = registry.add(std::make_unique<Shape>());
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_NE(first.value().id(), second.value().id());
}

TEST(ResourceRegistry, SerialsAreNeverReusedAcrossRebuildDeleteAndAdd) {
    std::string first_text;
    {
        ResourceRegistry registry;
        auto added = registry.add(std::make_unique<Shape>());
        ASSERT_TRUE(added);
        first_text = std::string{added.value().id().str()};
        EXPECT_TRUE(registry.remove(added.value().id()));
        auto replacement = registry.add(std::make_unique<Shape>());
        ASSERT_TRUE(replacement);
        EXPECT_NE(first_text, replacement.value().id().str());
    }
    ResourceRegistry rebuilt;
    auto again = rebuilt.add(std::make_unique<Shape>());
    ASSERT_TRUE(again);
    EXPECT_NE(first_text, again.value().id().str());
}

TEST(ResourceRegistry, ConcurrentAllocateFromDistinctRegistriesNeverReusesIds) {
    constexpr int registry_count = 4;
    constexpr int per_registry = 64;
    std::mutex mutex;
    std::unordered_set<std::string> identities;
    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(registry_count));
    for(int index = 0; index < registry_count; ++index) {
        workers.emplace_back([&] {
            ResourceRegistry registry;
            for(int n = 0; n < per_registry; ++n) {
                auto added = registry.add(std::make_unique<Shape>());
                ASSERT_TRUE(added);
                const std::string text{added.value().id().str()};
                const std::lock_guard lock{mutex};
                EXPECT_TRUE(identities.insert(text).second) << text;
            }
        });
    }
    for(auto& worker : workers) {
        worker.join();
    }
    EXPECT_EQ(identities.size(), static_cast<std::size_t>(registry_count * per_registry));
}

TEST(ResourceRegistry, SerialExhaustionReturnsInternalErrorWithoutMutation) {
    ResourceRegistry registry;
    auto existing = registry.add(std::make_unique<Shape>());
    ASSERT_TRUE(existing);
    resetLifetimeCounters();
    {
        const axiom::core::resource::detail::ResourceSerialExhaustionGuard guard;
        const auto rejected = registry.add(std::make_unique<Counted>());
        EXPECT_FALSE(rejected);
        EXPECT_EQ(rejected.error().code, ErrorCode::InternalError);
        EXPECT_EQ(Counted::destroyed, 1);
        EXPECT_TRUE(registry.contains(existing.value().id()));
    }
    auto after = registry.add(std::make_unique<Shape>());
    ASSERT_TRUE(after);
    EXPECT_NE(existing.value().id(), after.value().id());
}

} // namespace
