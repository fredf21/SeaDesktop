#include "test_logging_config.h"

#include "logging/logging_config.h"

#include <QtTest>

#include <stdexcept>
#include <string>
#include <string_view>

namespace {

QString qs(std::string_view value)
{
    return QString::fromStdString(std::string(value));
}

} // namespace

void TestLoggingConfig::logLevelFromString_shouldSupportAliases()
{
    using namespace sea::domain::logging;

    QCOMPARE(log_level_from_string("trace"), LogLevel::Trace);
    QCOMPARE(log_level_from_string("DEBUG"), LogLevel::Debug);
    QCOMPARE(log_level_from_string("info"), LogLevel::Info);
    QCOMPARE(log_level_from_string("warning"), LogLevel::Warn);
    QCOMPARE(log_level_from_string("err"), LogLevel::Error);
    QCOMPARE(log_level_from_string("crit"), LogLevel::Critical);
    QCOMPARE(log_level_from_string("none"), LogLevel::Off);

    QCOMPARE(qs(to_string(LogLevel::Trace)), QString("trace"));
    QCOMPARE(qs(to_string(LogLevel::Debug)), QString("debug"));
    QCOMPARE(qs(to_string(LogLevel::Info)), QString("info"));
    QCOMPARE(qs(to_string(LogLevel::Warn)), QString("warn"));
    QCOMPARE(qs(to_string(LogLevel::Error)), QString("error"));
    QCOMPARE(qs(to_string(LogLevel::Critical)), QString("critical"));
    QCOMPARE(qs(to_string(LogLevel::Off)), QString("off"));
}

void TestLoggingConfig::logLevelFromString_invalidShouldThrow()
{
    using namespace sea::domain::logging;

    QVERIFY_THROWS_EXCEPTION(
        std::invalid_argument,
         [[maybe_unused]] auto value = log_level_from_string("verbose")
        );
}

void TestLoggingConfig::logFormatFromString_shouldSupportAliases()
{
    using namespace sea::domain::logging;

    QCOMPARE(log_format_from_string("text"), LogFormat::Text);
    QCOMPARE(log_format_from_string("plain"), LogFormat::Text);
    QCOMPARE(log_format_from_string("JSON"), LogFormat::Json);

    QCOMPARE(qs(to_string(LogFormat::Text)), QString("text"));
    QCOMPARE(qs(to_string(LogFormat::Json)), QString("json"));

    QVERIFY_THROWS_EXCEPTION(
        std::invalid_argument,
        [[maybe_unused]] auto value = log_format_from_string("xml")
        );
}

void TestLoggingConfig::sinkTypeFromString_shouldSupportAliases()
{
    using namespace sea::domain::logging;

    QCOMPARE(sink_type_from_string("console"), SinkType::Console);
    QCOMPARE(sink_type_from_string("stderr"), SinkType::Console);
    QCOMPARE(sink_type_from_string("stdout"), SinkType::Console);
    QCOMPARE(sink_type_from_string("file"), SinkType::File);

    QCOMPARE(qs(to_string(SinkType::Console)), QString("console"));
    QCOMPARE(qs(to_string(SinkType::File)), QString("file"));

    QVERIFY_THROWS_EXCEPTION(
        std::invalid_argument,
         [[maybe_unused]] auto value = sink_type_from_string("database")
        );
}

void TestLoggingConfig::timePatternFromString_shouldParseValues()
{
    using namespace sea::domain::logging;

    QCOMPARE(time_pattern_from_string("none"), TimePattern::None);
    QCOMPARE(time_pattern_from_string("HOURLY"), TimePattern::Hourly);
    QCOMPARE(time_pattern_from_string("daily"), TimePattern::Daily);

    QCOMPARE(qs(to_string(TimePattern::None)), QString("none"));
    QCOMPARE(qs(to_string(TimePattern::Hourly)), QString("hourly"));
    QCOMPARE(qs(to_string(TimePattern::Daily)), QString("daily"));

    QVERIFY_THROWS_EXCEPTION(
        std::invalid_argument,
         [[maybe_unused]] auto value = time_pattern_from_string("weekly")
        );
}

void TestLoggingConfig::loggingConfig_safeDefaults_shouldBeValid()
{
    using namespace sea::domain::logging;

    auto cfg = LoggingConfig::safe_defaults();

    QVERIFY(cfg.is_enabled());
    QCOMPARE(cfg.level(), LogLevel::Info);
    QCOMPARE(cfg.flush_level(), LogLevel::Error);

    QCOMPARE(cfg.sinks().size(), std::size_t(1));
    QCOMPARE(cfg.sinks()[0].type, SinkType::Console);
    QCOMPARE(cfg.sinks()[0].format, LogFormat::Text);
    QVERIFY(cfg.sinks()[0].enabled);

    QVERIFY(cfg.async_config().enabled);
    QCOMPARE(cfg.async_config().queue_size, std::size_t(8192));
    QCOMPARE(cfg.async_config().overflow_policy,
             AsyncConfig::OverflowPolicy::OverrunOldest);

    QVERIFY_THROWS_NO_EXCEPTION(cfg.validate());
}

void TestLoggingConfig::loggingConfig_disabled_shouldSkipValidation()
{
    using namespace sea::domain::logging;

    auto cfg = LoggingConfig::disabled();

    QVERIFY(!cfg.is_enabled());
    QCOMPARE(cfg.level(), LogLevel::Off);

    QVERIFY_THROWS_NO_EXCEPTION(cfg.validate());
}

void TestLoggingConfig::loggingConfig_effectiveLevel_shouldUseExactOverride()
{
    using namespace sea::domain::logging;

    auto cfg = LoggingConfig::safe_defaults();

    cfg.set_level(LogLevel::Info);
    cfg.set_module_level("sea.http", LogLevel::Debug);

    QCOMPARE(cfg.effective_level_for("sea.http"), LogLevel::Debug);
    QCOMPARE(cfg.effective_level_for("sea.persistence"), LogLevel::Info);
}

void TestLoggingConfig::loggingConfig_effectiveLevel_shouldUseLongestPrefixOverride()
{
    using namespace sea::domain::logging;

    auto cfg = LoggingConfig::safe_defaults();

    cfg.set_level(LogLevel::Info);
    cfg.set_module_level("sea", LogLevel::Warn);
    cfg.set_module_level("sea.http", LogLevel::Debug);
    cfg.set_module_level("sea.http.auth", LogLevel::Trace);

    QCOMPARE(cfg.effective_level_for("sea.http.auth.login"), LogLevel::Trace);
    QCOMPARE(cfg.effective_level_for("sea.http.router"), LogLevel::Debug);
    QCOMPARE(cfg.effective_level_for("sea.persistence.mysql"), LogLevel::Warn);
    QCOMPARE(cfg.effective_level_for("other.module"), LogLevel::Info);
}

void TestLoggingConfig::loggingConfig_fileSinkWithoutPath_shouldThrow()
{
    using namespace sea::domain::logging;

    LoggingConfig cfg = LoggingConfig::safe_defaults();

    SinkConfig fileSink;
    fileSink.type = SinkType::File;
    fileSink.enabled = true;
    fileSink.path = "";

    cfg.set_sinks({ fileSink });

    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, cfg.validate());
}

void TestLoggingConfig::loggingConfig_allSinksDisabled_shouldThrow()
{
    using namespace sea::domain::logging;

    LoggingConfig cfg = LoggingConfig::safe_defaults();

    SinkConfig sink;
    sink.type = SinkType::Console;
    sink.enabled = false;

    cfg.set_sinks({ sink });

    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, cfg.validate());
}

void TestLoggingConfig::loggingConfig_asyncQueueSizeZero_shouldThrow()
{
    using namespace sea::domain::logging;

    LoggingConfig cfg = LoggingConfig::safe_defaults();

    AsyncConfig async;
    async.enabled = true;
    async.queue_size = 0;

    cfg.set_async(async);

    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, cfg.validate());
}