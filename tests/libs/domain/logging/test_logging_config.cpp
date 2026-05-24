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
// ═════════════════════════════════════════════════════════════
// COUVERTURE ADDITIONNELLE
// ═════════════════════════════════════════════════════════════

void TestLoggingConfig::loggingConfig_effectiveLevel_partialNameIsNotAPrefix()
{
    using namespace sea::domain::logging;

    // Un override "sea" ne doit matcher "seahttp" que s'il est suivi
    // d'un '.'. "seahttp" n'est donc PAS couvert par l'override "sea".
    auto cfg = LoggingConfig::safe_defaults();
    cfg.set_level(LogLevel::Info);
    cfg.set_module_level("sea", LogLevel::Warn);

    QCOMPARE(cfg.effective_level_for("sea.http"), LogLevel::Warn);
    // "seahttp" ne contient pas le séparateur '.' -> niveau global.
    QCOMPARE(cfg.effective_level_for("seahttp"), LogLevel::Info);
}

void TestLoggingConfig::loggingConfig_effectiveLevel_noOverride_shouldUseGlobalLevel()
{
    using namespace sea::domain::logging;

    auto cfg = LoggingConfig::safe_defaults();
    cfg.set_level(LogLevel::Warn);
    // Aucun module_level déclaré.

    QCOMPARE(cfg.effective_level_for("any.module"), LogLevel::Warn);
}

void TestLoggingConfig::loggingConfig_asyncDisabledWithZeroQueue_shouldNotThrow()
{
    using namespace sea::domain::logging;

    // queue_size == 0 n'est une erreur QUE si async est activé.
    LoggingConfig cfg = LoggingConfig::safe_defaults();

    AsyncConfig async;
    async.enabled    = false;
    async.queue_size = 0;
    cfg.set_async(async);

    QVERIFY_THROWS_NO_EXCEPTION(cfg.validate());
}

void TestLoggingConfig::loggingConfig_fileSinkMaxFilesZero_shouldThrow()
{
    using namespace sea::domain::logging;

    // Un sink File avec rotation.max_files == 0 est rejeté.
    LoggingConfig cfg = LoggingConfig::safe_defaults();

    SinkConfig fileSink;
    fileSink.type    = SinkType::File;
    fileSink.enabled = true;
    fileSink.path    = "./logs/service.log";
    fileSink.rotation.max_files = 0;

    cfg.set_sinks({ fileSink });

    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, cfg.validate());
}

void TestLoggingConfig::loggingConfig_validFileSink_shouldPass()
{
    using namespace sea::domain::logging;

    // Un sink File correctement configuré (path + max_files >= 1)
    // passe la validation.
    LoggingConfig cfg = LoggingConfig::safe_defaults();

    SinkConfig fileSink;
    fileSink.type    = SinkType::File;
    fileSink.enabled = true;
    fileSink.path    = "./logs/service.log";
    fileSink.rotation.max_files = 5;

    cfg.set_sinks({ fileSink });

    QVERIFY_THROWS_NO_EXCEPTION(cfg.validate());
}

void TestLoggingConfig::loggingConfig_disabledFileSinkWithoutPath_shouldBeIgnored()
{
    using namespace sea::domain::logging;

    // validate() ignore les sinks désactivés : un sink File sans
    // path mais enabled=false ne déclenche pas d'erreur, tant qu'au
    // moins un autre sink est actif.
    LoggingConfig cfg = LoggingConfig::safe_defaults();

    SinkConfig consoleSink;
    consoleSink.type    = SinkType::Console;
    consoleSink.enabled = true;

    SinkConfig badFileSink;
    badFileSink.type    = SinkType::File;
    badFileSink.enabled = false;       // désactivé -> ignoré
    badFileSink.path    = "";          // path vide, mais peu importe

    cfg.set_sinks({ consoleSink, badFileSink });

    QVERIFY_THROWS_NO_EXCEPTION(cfg.validate());
}

// ── builder fluide ───────────────────────────────────────────

void TestLoggingConfig::loggingConfig_builder_addSink_shouldAppend()
{
    using namespace sea::domain::logging;

    // add_sink ajoute à la suite des sinks existants (safe_defaults
    // en a déjà un : console).
    LoggingConfig cfg = LoggingConfig::safe_defaults();
    QCOMPARE(cfg.sinks().size(), std::size_t(1));

    SinkConfig fileSink;
    fileSink.type    = SinkType::File;
    fileSink.enabled = true;
    fileSink.path    = "./logs/extra.log";

    cfg.add_sink(fileSink);

    QCOMPARE(cfg.sinks().size(), std::size_t(2));
    QCOMPARE(cfg.sinks()[1].type, SinkType::File);
}

void TestLoggingConfig::loggingConfig_builder_setFlushAndEnabled_shouldApply()
{
    using namespace sea::domain::logging;

    LoggingConfig cfg = LoggingConfig::safe_defaults();

    cfg.set_flush_level(LogLevel::Critical);
    cfg.set_enabled(false);

    QCOMPARE(cfg.flush_level(), LogLevel::Critical);
    QVERIFY(!cfg.is_enabled());
}

// ── RotationConfig helpers ───────────────────────────────────

void TestLoggingConfig::rotationConfig_sizeRotationEnabled_dependsOnMaxSize()
{
    using namespace sea::domain::logging;

    RotationConfig rotation;

    rotation.max_size_bytes = 1024;
    QVERIFY(rotation.is_size_rotation_enabled());

    rotation.max_size_bytes = 0;
    QVERIFY(!rotation.is_size_rotation_enabled());
}

void TestLoggingConfig::rotationConfig_timeRotationEnabled_dependsOnPattern()
{
    using namespace sea::domain::logging;

    RotationConfig rotation;

    rotation.time_pattern = TimePattern::Daily;
    QVERIFY(rotation.is_time_rotation_enabled());

    rotation.time_pattern = TimePattern::Hourly;
    QVERIFY(rotation.is_time_rotation_enabled());

    rotation.time_pattern = TimePattern::None;
    QVERIFY(!rotation.is_time_rotation_enabled());
}