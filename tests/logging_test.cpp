#include <axiom/core/logging/callback_sink.hpp>
#include <axiom/core/logging/console_sink.hpp>
#include <axiom/core/logging/log_collector.hpp>
#include <axiom/core/logging/logger.hpp>
#include <axiom/core/logging/logging_service.hpp>

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using axiom::core::Value;
using axiom::core::logging::CallbackSink;
using axiom::core::logging::ConsoleSink;
using axiom::core::logging::ILogSink;
using axiom::core::logging::LogCollector;
using axiom::core::logging::LogFilter;
using axiom::core::logging::LoggingService;
using axiom::core::logging::LogLevel;
using axiom::core::logging::LogQuery;
using axiom::core::logging::LogRecord;

class RecordingSink final : public ILogSink {
public:
    void consume(const LogRecord& record) override { records.push_back(record); }
    void flush() noexcept override { ++flushes; }

    std::vector<LogRecord> records;
    std::size_t flushes{0};
};

class ThrowingSink final : public ILogSink {
public:
    void consume(const LogRecord& record) override {
        static_cast<void>(record);
        throw std::runtime_error{"sink failure"};
    }
};

class ReentrantSink final : public ILogSink {
public:
    explicit ReentrantSink(axiom::core::logging::Logger logger) : logger_(std::move(logger)) {}

    void consume(const LogRecord& record) override {
        messages.push_back(record.message);
        if(record.message == "outer") {
            logger_.write(LogLevel::Debug, "inner");
        }
    }

    std::vector<std::string> messages;

private:
    axiom::core::logging::Logger logger_;
};

LogRecord record(std::string message,
                 const LogLevel level = LogLevel::Info,
                 std::string category = "runtime") {
    return {.level = level,
            .message = std::move(message),
            .timestamp = std::chrono::system_clock::time_point{},
            .category = std::move(category),
            .source_file = "logging_test.cpp",
            .source_function = "record",
            .source_line = 1};
}

TEST(LogFilter, MatchesOnlyWholeCategorySegments) {
    const LogFilter filter{.minimum_level = LogLevel::Info, .category_prefixes = {"runtime"}};

    EXPECT_TRUE(filter.matches(LogLevel::Info, "runtime"));
    EXPECT_TRUE(filter.matches(LogLevel::Warning, "runtime.action"));
    EXPECT_FALSE(filter.matches(LogLevel::Debug, "runtime.action"));
    EXPECT_FALSE(filter.matches(LogLevel::Info, "runtime2"));
    EXPECT_TRUE(LogFilter{}.matches(LogLevel::Trace, "any.category"));
    EXPECT_TRUE(LogFilter{.category_prefixes = {""}}.matches(LogLevel::Trace, "any.category"));
}

TEST(LogFilter, RejectsCategoriesThatOnlyShareCharacterPrefixes) {
    const LogFilter filter{.category_prefixes = {"runtime.action"}};

    EXPECT_TRUE(filter.matches(LogLevel::Info, "runtime.action"));
    EXPECT_TRUE(filter.matches(LogLevel::Info, "runtime.action.child"));
    EXPECT_FALSE(filter.matches(LogLevel::Info, "runtime.actions"));
    EXPECT_FALSE(filter.matches(LogLevel::Info, "runtime.act"));
}

static_assert(!std::is_copy_constructible_v<axiom::core::logging::LogSubscription>);
static_assert(std::is_move_constructible_v<axiom::core::logging::LogSubscription>);

TEST(LoggingService, FansOutToMatchingSinksAndRemovesSubscriptions) {
    LoggingService service;
    const auto first = std::make_shared<RecordingSink>();
    const auto second = std::make_shared<RecordingSink>();
    auto all_subscription = service.addSink(first);
    {
        auto warning_subscription = service.addSink(
            second, {.minimum_level = LogLevel::Warning, .category_prefixes = {"geometry"}});
        service.logger("geometry").write(LogLevel::Info, "created");
        service.logger("geometry.mesh").write(LogLevel::Warning, "invalid");
    }

    service.logger("geometry").write(LogLevel::Error, "after removal");

    ASSERT_EQ(first->records.size(), 3U);
    ASSERT_EQ(second->records.size(), 1U);
    EXPECT_EQ(second->records.front().message, "invalid");
}

TEST(LoggingService, SupportsEmptyAndMovedSubscriptions) {
    LoggingService service;
    const auto first = std::make_shared<RecordingSink>();
    const auto second = std::make_shared<RecordingSink>();
    auto first_subscription = service.addSink(first);
    auto second_subscription = service.addSink(second);
    second_subscription = std::move(first_subscription);
    auto empty_subscription = service.addSink(nullptr);

    service.logger("runtime").write(LogLevel::Info, "only first");
    empty_subscription.reset();

    ASSERT_EQ(first->records.size(), 1U);
    EXPECT_EQ(first->records.front().message, "only first");
    EXPECT_TRUE(second->records.empty());
}

TEST(LoggingService, RemovesMovedSubscriptionWhenItIsReset) {
    LoggingService service;
    const auto sink = std::make_shared<RecordingSink>();
    auto original = service.addSink(sink);
    auto replacement = std::move(original);

    replacement.reset();
    service.logger("runtime").write(LogLevel::Info, "not delivered");

    EXPECT_TRUE(sink->records.empty());
}

TEST(LoggingService, ResetsTheRegistrationAcquiredByMoveAssignment) {
    LoggingService service;
    const auto retained = std::make_shared<RecordingSink>();
    const auto replaced = std::make_shared<RecordingSink>();
    auto original = service.addSink(retained);
    auto owner = service.addSink(replaced);

    owner = std::move(original);
    service.logger("runtime").write(LogLevel::Info, "while registered");
    owner.reset();
    service.logger("runtime").write(LogLevel::Info, "after reset");

    ASSERT_EQ(retained->records.size(), 1U);
    EXPECT_EQ(retained->records.front().message, "while registered");
    EXPECT_TRUE(replaced->records.empty());
}

TEST(LoggingService, RemovesSubscriptionWhenItsScopeEnds) {
    LoggingService service;
    const auto sink = std::make_shared<RecordingSink>();
    {
        auto subscription = service.addSink(sink);
        service.logger("runtime").write(LogLevel::Info, "delivered");
    }

    service.logger("runtime").write(LogLevel::Info, "not delivered");

    ASSERT_EQ(sink->records.size(), 1U);
    EXPECT_EQ(sink->records.front().message, "delivered");
}

TEST(LoggingService, KeepsMovedFromServicesSafeToUseAsNoOps) {
    LoggingService original;
    LoggingService owner = std::move(original);
    const auto sink = std::make_shared<RecordingSink>();

    auto subscription = original.addSink(sink);
    auto context = original.scopedContext({{"ignored", Value{true}}});
    original.flush();
    original.logger("runtime").write(LogLevel::Info, "ignored");

    EXPECT_TRUE(sink->records.empty());
    EXPECT_TRUE(sink->flushes == 0U);
    static_cast<void>(owner);
}

TEST(Logger, MergesContextAndFixedFieldsInPrecedenceOrder) {
    LoggingService service;
    const auto sink = std::make_shared<RecordingSink>();
    auto subscription = service.addSink(sink);
    const auto logger = service.logger("runtime", {{"shared", Value{"logger"}}, {"logger", Value{1}}});

    auto outer = service.scopedContext({{"outer", Value{true}}, {"shared", Value{"outer"}}});
    {
        auto inner = service.scopedContext({{"inner", Value{"yes"}}, {"shared", Value{"inner"}}});
        logger.write(LogLevel::Info, "record", {{"record", Value{2}}, {"shared", Value{"record"}}});
    }

    ASSERT_EQ(sink->records.size(), 1U);
    const auto& fields = sink->records.front().fields;
    EXPECT_TRUE(fields.at("outer").asBoolean());
    EXPECT_EQ(fields.at("inner").asString(), "yes");
    EXPECT_EQ(fields.at("logger").asInteger(), 1);
    EXPECT_EQ(fields.at("record").asInteger(), 2);
    EXPECT_EQ(fields.at("shared").asString(), "record");
}

TEST(Logger, DerivesCategoriesAndFixedFieldsWithoutChangingTheOriginal) {
    LoggingService service;
    const auto sink = std::make_shared<RecordingSink>();
    auto subscription = service.addSink(sink);
    const auto logger = service.logger("runtime", {{"source", Value{"parent"}}});

    logger.child("").write(LogLevel::Info, "parent");
    logger.child("action").withFields({{"source", Value{"child"}}, {"child", Value{true}}})
        .write(LogLevel::Info, "child");
    service.logger("").child("root").write(LogLevel::Info, "root");

    ASSERT_EQ(sink->records.size(), 3U);
    EXPECT_EQ(sink->records.at(0).category, "runtime");
    EXPECT_EQ(sink->records.at(0).fields.at("source").asString(), "parent");
    EXPECT_EQ(sink->records.at(1).category, "runtime.action");
    EXPECT_EQ(sink->records.at(1).fields.at("source").asString(), "child");
    EXPECT_TRUE(sink->records.at(1).fields.at("child").asBoolean());
    EXPECT_EQ(sink->records.at(2).category, "root");
}

TEST(Logger, KeepsScopedContextThreadLocal) {
    LoggingService service;
    const auto sink = std::make_shared<RecordingSink>();
    auto subscription = service.addSink(sink);
    const auto logger = service.logger("runtime");

    auto context = service.scopedContext({{"request", Value{"main"}}});
    std::thread worker{[&service, logger] {
        auto worker_context = service.scopedContext({{"request", Value{"worker"}}});
        logger.write(LogLevel::Info, "worker");
    }};
    worker.join();
    logger.write(LogLevel::Info, "main");

    ASSERT_EQ(sink->records.size(), 2U);
    EXPECT_EQ(sink->records.at(0).fields.at("request").asString(), "worker");
    EXPECT_EQ(sink->records.at(1).fields.at("request").asString(), "main");
}

TEST(Logger, MovesScopedContextWithoutLeavingTheOldGuardActive) {
    LoggingService service;
    const auto sink = std::make_shared<RecordingSink>();
    auto subscription = service.addSink(sink);
    const auto logger = service.logger("runtime");

    auto first = service.scopedContext({{"first", Value{true}}});
    auto second = service.scopedContext({{"second", Value{true}}});
    second = std::move(first);
    logger.write(LogLevel::Info, "record");

    ASSERT_EQ(sink->records.size(), 1U);
    EXPECT_TRUE(sink->records.front().fields.at("first").asBoolean());
    EXPECT_FALSE(sink->records.front().fields.contains("second"));
}

TEST(Logger, RemovesContextAfterAMovedGuardIsDestroyed) {
    LoggingService service;
    const auto sink = std::make_shared<RecordingSink>();
    auto subscription = service.addSink(sink);
    const auto logger = service.logger("runtime");

    {
        auto source = service.scopedContext({{"request", Value{"active"}}});
        auto owner = std::move(source);
        logger.write(LogLevel::Info, "while active");
    }
    logger.write(LogLevel::Info, "after scope");

    ASSERT_EQ(sink->records.size(), 2U);
    EXPECT_EQ(sink->records.at(0).fields.at("request").asString(), "active");
    EXPECT_FALSE(sink->records.at(1).fields.contains("request"));
}

TEST(Logger, RemovesContextWhenTheGuardIsDestroyed) {
    LoggingService service;
    const auto sink = std::make_shared<RecordingSink>();
    auto subscription = service.addSink(sink);
    const auto logger = service.logger("runtime");
    {
        auto context = service.scopedContext({{"request", Value{"scoped"}}});
        logger.write(LogLevel::Info, "inside scope");
    }

    logger.write(LogLevel::Info, "outside scope");

    ASSERT_EQ(sink->records.size(), 2U);
    EXPECT_EQ(sink->records.at(0).fields.at("request").asString(), "scoped");
    EXPECT_FALSE(sink->records.at(1).fields.contains("request"));
}

TEST(Logger, RemovesTheContextAcquiredByMoveAssignmentAtScopeExit) {
    LoggingService service;
    const auto sink = std::make_shared<RecordingSink>();
    auto subscription = service.addSink(sink);
    const auto logger = service.logger("runtime");
    {
        auto original = service.scopedContext({{"request", Value{"original"}}});
        auto replacement = service.scopedContext({{"request", Value{"replacement"}}});
        replacement = std::move(original);
        logger.write(LogLevel::Info, "while active");
    }

    logger.write(LogLevel::Info, "after scope");

    ASSERT_EQ(sink->records.size(), 2U);
    EXPECT_EQ(sink->records.at(0).fields.at("request").asString(), "original");
    EXPECT_FALSE(sink->records.at(1).fields.contains("request"));
}

TEST(Logger, IgnoresFailingSinksAndCapturesMacroCallSite) {
    LoggingService service;
    const auto good_sink = std::make_shared<RecordingSink>();
    auto bad_subscription = service.addSink(std::make_shared<ThrowingSink>());
    auto good_subscription = service.addSink(good_sink);
    const auto logger = service.logger("runtime").child("action");

    AXIOM_LOG_INFO(logger, "action {} finished", 7);

    ASSERT_EQ(good_sink->records.size(), 1U);
    const auto& record = good_sink->records.front();
    EXPECT_EQ(record.message, "action 7 finished");
    EXPECT_EQ(record.category, "runtime.action");
    EXPECT_NE(record.source_line, 0U);
    EXPECT_FALSE(record.source_file.empty());
}

TEST(Logger, PreservesTheExplicitSourceLocationInTheRecord) {
    LoggingService service;
    const auto sink = std::make_shared<RecordingSink>();
    auto subscription = service.addSink(sink);
    const auto logger = service.logger("runtime");
    const auto expected_location = std::source_location::current();

    logger.write(LogLevel::Info, "located", {}, expected_location);

    ASSERT_EQ(sink->records.size(), 1U);
    const auto& logged = sink->records.front();
    EXPECT_EQ(logged.source_file, expected_location.file_name());
    EXPECT_EQ(logged.source_line, expected_location.line());
    EXPECT_EQ(logged.source_column, expected_location.column());
    EXPECT_EQ(logged.source_function, expected_location.function_name());
}

TEST(Logger, ProvidesAllSixLevelShortcuts) {
    LoggingService service;
    const auto sink = std::make_shared<RecordingSink>();
    auto subscription = service.addSink(sink);
    const auto logger = service.logger("runtime");

    AXIOM_LOG_TRACE(logger, "trace");
    AXIOM_LOG_DEBUG(logger, "debug");
    AXIOM_LOG_INFO(logger, "info");
    AXIOM_LOG_WARNING(logger, "warning");
    AXIOM_LOG_ERROR(logger, "error");
    AXIOM_LOG_CRITICAL(logger, "critical");

    ASSERT_EQ(sink->records.size(), 6U);
    EXPECT_EQ(sink->records.at(0).level, LogLevel::Trace);
    EXPECT_EQ(sink->records.at(1).level, LogLevel::Debug);
    EXPECT_EQ(sink->records.at(2).level, LogLevel::Info);
    EXPECT_EQ(sink->records.at(3).level, LogLevel::Warning);
    EXPECT_EQ(sink->records.at(4).level, LogLevel::Error);
    EXPECT_EQ(sink->records.at(5).level, LogLevel::Critical);
}

TEST(LoggingService, AllowsSinksToLogRecursivelyAfterTakingTheSinkSnapshot) {
    LoggingService service;
    const auto logger = service.logger("runtime");
    const auto sink = std::make_shared<ReentrantSink>(logger);
    auto subscription = service.addSink(sink);

    logger.write(LogLevel::Info, "outer");

    EXPECT_EQ(sink->messages, (std::vector<std::string>{"outer", "inner"}));
}

TEST(Logger, IsSafeNoOpWhenDefaultConstructed) {
    axiom::core::logging::Logger logger;

    EXPECT_FALSE(logger.enabled(LogLevel::Critical));
    logger.write(LogLevel::Critical, "ignored");
    AXIOM_LOG_ERROR(logger, "{}", "ignored");
}

TEST(LoggingService, FlushesSinksWithoutThrowing) {
    LoggingService service;
    const auto sink = std::make_shared<RecordingSink>();
    auto subscription = service.addSink(sink);

    service.flush();

    EXPECT_EQ(sink->flushes, 1U);
}

TEST(ConsoleSink, WritesColorizedUtcStructuredRecordsToStandardError) {
    ConsoleSink sink;
    auto logged = record("created", LogLevel::Warning, "runtime.action");
    logged.timestamp += std::chrono::milliseconds{123};
    logged.source_line = 42;
    logged.source_function = "invoke";
    logged.fields = {{"alpha", Value{std::int64_t{1}}},
                     {"nested", Value{Value::Object{{"a", Value{true}}, {"z", Value{"last"}}}}}};

    testing::internal::CaptureStderr();
    sink.consume(logged);
    sink.flush();
    const auto output = testing::internal::GetCapturedStderr();

    EXPECT_NE(output.find("\x1B["), std::string::npos);
    EXPECT_NE(output.find("1970-01-01T00:00:00.123Z [warning] [runtime.action] created"),
              std::string::npos);
    EXPECT_NE(output.find("(logging_test.cpp:42 invoke)"), std::string::npos);
    EXPECT_NE(output.find("{alpha=1, nested={a=true, z=\"last\"}}"), std::string::npos);
}

TEST(ConsoleSink, FormatsAllLevelsAndValueShapes) {
    ConsoleSink sink;
    const Value::Array array{Value{nullptr}, Value{false}, Value{2.5}, Value{"quote\\\""}};
    const std::array<LogLevel, 6> levels{LogLevel::Trace,   LogLevel::Debug, LogLevel::Info,
                                         LogLevel::Warning, LogLevel::Error, LogLevel::Critical};

    testing::internal::CaptureStderr();
    sink.consume(record("without fields", levels.front()));
    for(const auto level : levels) {
        auto logged = record("all shapes", level);
        logged.fields = {{"array", Value{array}}, {"null", Value{nullptr}}};
        sink.consume(logged);
    }
    sink.flush();
    const auto output = testing::internal::GetCapturedStderr();

    EXPECT_NE(output.find("[trace]"), std::string::npos);
    EXPECT_NE(output.find("[debug]"), std::string::npos);
    EXPECT_NE(output.find("[info]"), std::string::npos);
    EXPECT_NE(output.find("[warning]"), std::string::npos);
    EXPECT_NE(output.find("[error]"), std::string::npos);
    EXPECT_NE(output.find("[critical]"), std::string::npos);
    EXPECT_NE(output.find("[null, false, 2.500000, \"quote\\\\\\\"\"]"), std::string::npos);
}

TEST(CallbackSink, LetsLoggingServiceIsolateCallbackExceptions) {
    LoggingService service;
    const auto collected = std::make_shared<LogCollector>();
    auto throwing = service.addSink(std::make_shared<CallbackSink>(
        [](const LogRecord&) { throw std::runtime_error{"callback failure"}; }));
    auto collector = service.addSink(collected);

    service.logger("runtime").write(LogLevel::Error, "still collected");

    const auto records = collected->records();
    ASSERT_EQ(records.size(), 1U);
    EXPECT_EQ(records.front().message, "still collected");
}

TEST(CallbackSink, ForwardsTheConsumedRecordToItsCallback) {
    std::vector<LogRecord> received;
    CallbackSink callback{[&received](const LogRecord& logged) { received.push_back(logged); }};
    const auto expected = record("forwarded", LogLevel::Critical, "runtime.callback");

    callback.consume(expected);

    ASSERT_EQ(received.size(), 1U);
    EXPECT_EQ(received.front().message, "forwarded");
    EXPECT_EQ(received.front().level, LogLevel::Critical);
    EXPECT_EQ(received.front().category, "runtime.callback");
}

TEST(CallbackSink, AcceptsAnEmptyCallback) {
    CallbackSink callback{std::function<void(const LogRecord&)>{}};

    callback.consume(record("ignored"));
    callback.flush();
}

TEST(LogCollector, UsesDefaultAndConfiguredCapacities) {
    LogCollector defaults;
    LogCollector disabled{0};
    LogCollector custom{2};

    defaults.consume(record("default"));
    disabled.consume(record("discarded"));
    custom.consume(record("first"));
    custom.consume(record("second"));
    custom.consume(record("third"));

    EXPECT_EQ(defaults.capacity(), 1000U);
    EXPECT_EQ(disabled.capacity(), 0U);
    EXPECT_TRUE(disabled.records().empty());
    const auto retained = custom.records();
    ASSERT_EQ(retained.size(), 2U);
    EXPECT_EQ(retained.at(0).message, "second");
    EXPECT_EQ(retained.at(1).message, "third");
}

TEST(LogCollector, QueriesLatestMatchesInCollectionOrder) {
    LogCollector collector{8};
    collector.consume(record("trace", LogLevel::Trace, "runtime"));
    collector.consume(record("first", LogLevel::Warning, "runtime.action"));
    collector.consume(record("other", LogLevel::Error, "runtime2"));
    collector.consume(record("second", LogLevel::Error, "runtime.action.child"));
    collector.consume(record("third", LogLevel::Critical, "runtime.action"));

    const auto matching = collector.records(
        {.minimum_level = LogLevel::Warning, .category_prefixes = {"runtime.action"}, .limit = 2});

    ASSERT_EQ(matching.size(), 2U);
    EXPECT_EQ(matching.at(0).message, "second");
    EXPECT_EQ(matching.at(1).message, "third");
}

TEST(LogCollector, FiltersWholeCategorySegmentsAndKeepsAnOversizedLimit) {
    LogCollector collector{4};
    collector.consume(record("exact", LogLevel::Info, "runtime.action"));
    collector.consume(record("child", LogLevel::Info, "runtime.action.child"));
    collector.consume(record("similar", LogLevel::Info, "runtime.actions"));

    const auto matching = collector.records(
        {.category_prefixes = {"runtime.action"}, .limit = 4});

    ASSERT_EQ(matching.size(), 2U);
    EXPECT_EQ(matching.at(0).message, "exact");
    EXPECT_EQ(matching.at(1).message, "child");
}

TEST(LogCollector, PreservesEveryMatchWhenTheLimitEqualsTheMatchCount) {
    LogCollector collector{4};
    collector.consume(record("first", LogLevel::Info, "runtime"));
    collector.consume(record("second", LogLevel::Info, "runtime"));

    const auto matching = collector.records({.limit = 2});

    ASSERT_EQ(matching.size(), 2U);
    EXPECT_EQ(matching.at(0).message, "first");
    EXPECT_EQ(matching.at(1).message, "second");
}

TEST(LogCollector, SupportsConcurrentConsumptionAndQueries) {
    LogCollector collector{128};
    std::atomic<bool> started{false};
    std::vector<std::thread> writers;
    for(int writer = 0; writer < 4; ++writer) {
        writers.emplace_back([&collector, &started, writer] {
            while(!started.load(std::memory_order_acquire)) {
            }
            for(int index = 0; index < 100; ++index) {
                collector.consume(record(std::to_string(writer) + "." + std::to_string(index)));
            }
        });
    }
    std::thread reader{[&collector, &started] {
        while(!started.load(std::memory_order_acquire)) {
        }
        for(int index = 0; index < 100; ++index) {
            EXPECT_LE(collector.records().size(), 128U);
        }
    }};

    started.store(true, std::memory_order_release);
    for(auto& writer : writers) {
        writer.join();
    }
    reader.join();

    EXPECT_EQ(collector.records().size(), 128U);
}

} // namespace
