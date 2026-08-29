#include <axiom/core/logging/logger.hpp>
#include <axiom/core/logging/logging_service.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using axiom::core::Value;
using axiom::core::logging::ILogSink;
using axiom::core::logging::LogFilter;
using axiom::core::logging::LogLevel;
using axiom::core::logging::LogRecord;
using axiom::core::logging::LoggingService;

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

TEST(LogFilter, MatchesOnlyWholeCategorySegments) {
    const LogFilter filter{.minimum_level = LogLevel::Info, .category_prefixes = {"runtime"}};

    EXPECT_TRUE(filter.matches(LogLevel::Info, "runtime"));
    EXPECT_TRUE(filter.matches(LogLevel::Warning, "runtime.action"));
    EXPECT_FALSE(filter.matches(LogLevel::Debug, "runtime.action"));
    EXPECT_FALSE(filter.matches(LogLevel::Info, "runtime2"));
    EXPECT_TRUE(LogFilter{}.matches(LogLevel::Trace, "any.category"));
    EXPECT_TRUE(LogFilter{.category_prefixes = {""}}.matches(LogLevel::Trace, "any.category"));
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
    std::thread worker{[logger] { logger.write(LogLevel::Info, "worker"); }};
    worker.join();
    logger.write(LogLevel::Info, "main");

    ASSERT_EQ(sink->records.size(), 2U);
    EXPECT_FALSE(sink->records.at(0).fields.contains("request"));
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

} // namespace
