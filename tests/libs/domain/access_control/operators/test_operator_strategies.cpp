#include "test_operator_strategies.h"

#include "access_control/operators/contains_strategy.h"
#include "access_control/operators/ends_with_strategy.h"
#include "access_control/operators/equals_strategy.h"
#include "access_control/operators/exists_strategy.h"
#include "access_control/operators/greater_than_strategy.h"
#include "access_control/operators/greater_than_or_equal_strategy.h"
#include "access_control/operators/in_strategy.h"
#include "access_control/operators/intersects_strategy.h"
#include "access_control/operators/less_than_or_equal_strategy.h"
#include "access_control/operators/less_than_strategy.h"
#include "access_control/operators/not_equals_strategy.h"
#include "access_control/operators/not_exists_strategy.h"
#include "access_control/operators/not_in_strategy.h"
#include "access_control/operators/numeric_helper.h"
#include "access_control/operators/operator_registry.h"
#include "access_control/operators/regex_match_strategy.h"
#include "access_control/operators/starts_with_strategy.h"
#include "access_control/policy_operator.h"

#include <QtTest>

#include <regex>
#include <string>
#include <vector>

using namespace sea::domain::access_control;

namespace {

ResolvedValue emptyValue()
{
    return ResolvedValue{};
}

ResolvedValue scalarValue(std::string value)
{
    ResolvedValue v{};
    v.scalar = std::move(value);
    return v;
}

ResolvedValue listValue(std::vector<std::string> values)
{
    ResolvedValue v{};
    v.list = std::move(values);
    return v;
}

QString qs(std::string_view value)
{
    return QString::fromStdString(std::string(value));
}

} // namespace

// ─────────────────────────────────────────────
// NumericHelper
// ─────────────────────────────────────────────

void TestOperatorStrategies::numericHelper_parseValidNumber_shouldReturnDouble()
{
    auto value = NumericHelper::parse("42.5");

    QVERIFY(value.has_value());
    QCOMPARE(*value, 42.5);
}

void TestOperatorStrategies::numericHelper_parseInvalidNumber_shouldReturnNullopt()
{
    auto value = NumericHelper::parse("abc");

    QVERIFY(!value.has_value());
}

void TestOperatorStrategies::numericHelper_parsePartialNumber_shouldReturnNullopt()
{
    auto value = NumericHelper::parse("42abc");

    QVERIFY(!value.has_value());
}

void TestOperatorStrategies::numericHelper_parseBothScalars_shouldReturnPair()
{
    auto left = scalarValue("10");
    auto right = scalarValue("5");

    auto values = NumericHelper::parse_both(left, right);

    QVERIFY(values.has_value());
    QCOMPARE(values->first, 10.0);
    QCOMPARE(values->second, 5.0);
}

void TestOperatorStrategies::numericHelper_parseBothWithNonScalar_shouldReturnNullopt()
{
    auto left = listValue({ "10" });
    auto right = scalarValue("5");

    auto values = NumericHelper::parse_both(left, right);

    QVERIFY(!values.has_value());
}

// ─────────────────────────────────────────────
// EqualsStrategy
// ─────────────────────────────────────────────

void TestOperatorStrategies::equals_scalarEqual_shouldReturnTrue()
{
    EqualsStrategy strategy;

    QVERIFY(strategy.evaluate(scalarValue("admin"), scalarValue("admin")));
}

void TestOperatorStrategies::equals_scalarDifferent_shouldReturnFalse()
{
    EqualsStrategy strategy;

    QVERIFY(!strategy.evaluate(scalarValue("admin"), scalarValue("user")));
}

void TestOperatorStrategies::equals_listEqualSameOrder_shouldReturnTrue()
{
    EqualsStrategy strategy;

    QVERIFY(strategy.evaluate(
        listValue({ "admin", "manager" }),
        listValue({ "admin", "manager" })
        ));
}

void TestOperatorStrategies::equals_listDifferentOrder_shouldReturnFalse()
{
    EqualsStrategy strategy;

    QVERIFY(!strategy.evaluate(
        listValue({ "admin", "manager" }),
        listValue({ "manager", "admin" })
        ));
}

void TestOperatorStrategies::equals_emptyValues_shouldReturnTrue()
{
    EqualsStrategy strategy;

    QVERIFY(strategy.evaluate(emptyValue(), emptyValue()));
}

void TestOperatorStrategies::equals_mixedTypes_shouldReturnFalse()
{
    EqualsStrategy strategy;

    QVERIFY(!strategy.evaluate(
        scalarValue("admin"),
        listValue({ "admin" })
        ));
}

// ─────────────────────────────────────────────
// ExistsStrategy
// ─────────────────────────────────────────────

void TestOperatorStrategies::exists_scalar_shouldReturnTrue()
{
    ExistsStrategy strategy;

    QVERIFY(strategy.evaluate(scalarValue("admin"), emptyValue()));
}

void TestOperatorStrategies::exists_list_shouldReturnTrue()
{
    ExistsStrategy strategy;

    QVERIFY(strategy.evaluate(listValue({ "admin" }), emptyValue()));
}

void TestOperatorStrategies::exists_empty_shouldReturnFalse()
{
    ExistsStrategy strategy;

    QVERIFY(!strategy.evaluate(emptyValue(), scalarValue("ignored")));
}

void TestOperatorStrategies::exists_shouldIgnoreRightOperand()
{
    ExistsStrategy strategy;

    QVERIFY(strategy.ignores_right_operand());
}

// ─────────────────────────────────────────────
// ContainsStrategy
// ─────────────────────────────────────────────

void TestOperatorStrategies::contains_listContainsScalar_shouldReturnTrue()
{
    ContainsStrategy strategy;

    QVERIFY(strategy.evaluate(
        listValue({ "admin", "manager", "user" }),
        scalarValue("manager")
        ));
}

void TestOperatorStrategies::contains_listDoesNotContainScalar_shouldReturnFalse()
{
    ContainsStrategy strategy;

    QVERIFY(!strategy.evaluate(
        listValue({ "admin", "manager", "user" }),
        scalarValue("guest")
        ));
}

void TestOperatorStrategies::contains_stringContainsSubstring_shouldReturnTrue()
{
    ContainsStrategy strategy;

    QVERIFY(strategy.evaluate(
        scalarValue("hello world"),
        scalarValue("world")
        ));
}

void TestOperatorStrategies::contains_stringDoesNotContainSubstring_shouldReturnFalse()
{
    ContainsStrategy strategy;

    QVERIFY(!strategy.evaluate(
        scalarValue("hello world"),
        scalarValue("admin")
        ));
}

void TestOperatorStrategies::contains_invalidTypes_shouldReturnFalse()
{
    ContainsStrategy strategy;

    QVERIFY(!strategy.evaluate(
        scalarValue("admin"),
        listValue({ "admin" })
        ));
}

// ─────────────────────────────────────────────
// InStrategy
// ─────────────────────────────────────────────

void TestOperatorStrategies::in_scalarInList_shouldReturnTrue()
{
    InStrategy strategy;

    QVERIFY(strategy.evaluate(
        scalarValue("manager"),
        listValue({ "admin", "manager", "user" })
        ));
}

void TestOperatorStrategies::in_scalarNotInList_shouldReturnFalse()
{
    InStrategy strategy;

    QVERIFY(!strategy.evaluate(
        scalarValue("guest"),
        listValue({ "admin", "manager", "user" })
        ));
}

void TestOperatorStrategies::in_invalidTypes_shouldReturnFalse()
{
    InStrategy strategy;

    QVERIFY(!strategy.evaluate(
        listValue({ "admin" }),
        scalarValue("admin")
        ));
}

// ─────────────────────────────────────────────
// IntersectsStrategy
// ─────────────────────────────────────────────

void TestOperatorStrategies::intersects_listsWithCommonValue_shouldReturnTrue()
{
    IntersectsStrategy strategy;

    QVERIFY(strategy.evaluate(
        listValue({ "admin", "editor" }),
        listValue({ "manager", "editor" })
        ));
}

void TestOperatorStrategies::intersects_listsWithoutCommonValue_shouldReturnFalse()
{
    IntersectsStrategy strategy;

    QVERIFY(!strategy.evaluate(
        listValue({ "admin", "editor" }),
        listValue({ "manager", "user" })
        ));
}

void TestOperatorStrategies::intersects_scalarInList_shouldReturnTrue()
{
    IntersectsStrategy strategy;

    QVERIFY(strategy.evaluate(
        scalarValue("admin"),
        listValue({ "admin", "manager" })
        ));
}

void TestOperatorStrategies::intersects_scalarNotInList_shouldReturnFalse()
{
    IntersectsStrategy strategy;

    QVERIFY(!strategy.evaluate(
        scalarValue("guest"),
        listValue({ "admin", "manager" })
        ));
}

void TestOperatorStrategies::intersects_invalidTypes_shouldReturnFalse()
{
    IntersectsStrategy strategy;

    QVERIFY(!strategy.evaluate(
        scalarValue("admin"),
        scalarValue("admin")
        ));
}

// ─────────────────────────────────────────────
// EndsWithStrategy
// ─────────────────────────────────────────────

void TestOperatorStrategies::endsWith_validSuffix_shouldReturnTrue()
{
    EndsWithStrategy strategy;

    QVERIFY(strategy.evaluate(
        scalarValue("report.pdf"),
        scalarValue(".pdf")
        ));
}

void TestOperatorStrategies::endsWith_invalidSuffix_shouldReturnFalse()
{
    EndsWithStrategy strategy;

    QVERIFY(!strategy.evaluate(
        scalarValue("report.pdf"),
        scalarValue(".png")
        ));
}

void TestOperatorStrategies::endsWith_suffixLongerThanText_shouldReturnFalse()
{
    EndsWithStrategy strategy;

    QVERIFY(!strategy.evaluate(
        scalarValue("pdf"),
        scalarValue("report.pdf")
        ));
}

void TestOperatorStrategies::endsWith_invalidTypes_shouldReturnFalse()
{
    EndsWithStrategy strategy;

    QVERIFY(!strategy.evaluate(
        listValue({ "report.pdf" }),
        scalarValue(".pdf")
        ));
}

// ─────────────────────────────────────────────
// GreaterThanStrategy
// ─────────────────────────────────────────────

void TestOperatorStrategies::greaterThan_leftGreater_shouldReturnTrue()
{
    GreaterThanStrategy strategy;

    QVERIFY(strategy.evaluate(
        scalarValue("10"),
        scalarValue("5")
        ));
}

void TestOperatorStrategies::greaterThan_leftEqual_shouldReturnFalse()
{
    GreaterThanStrategy strategy;

    QVERIFY(!strategy.evaluate(
        scalarValue("10"),
        scalarValue("10")
        ));
}

void TestOperatorStrategies::greaterThan_invalidNumber_shouldReturnFalse()
{
    GreaterThanStrategy strategy;

    QVERIFY(!strategy.evaluate(
        scalarValue("abc"),
        scalarValue("10")
        ));
}

// ─────────────────────────────────────────────
// GreaterThanOrEqualStrategy
// ─────────────────────────────────────────────

void TestOperatorStrategies::greaterThanOrEqual_leftGreater_shouldReturnTrue()
{
    GreaterThanOrEqualStrategy strategy;

    QVERIFY(strategy.evaluate(
        scalarValue("10"),
        scalarValue("5")
        ));
}

void TestOperatorStrategies::greaterThanOrEqual_leftEqual_shouldReturnTrue()
{
    GreaterThanOrEqualStrategy strategy;

    QVERIFY(strategy.evaluate(
        scalarValue("10"),
        scalarValue("10")
        ));
}

void TestOperatorStrategies::greaterThanOrEqual_leftSmaller_shouldReturnFalse()
{
    GreaterThanOrEqualStrategy strategy;

    QVERIFY(!strategy.evaluate(
        scalarValue("5"),
        scalarValue("10")
        ));
}

void TestOperatorStrategies::greaterThanOrEqual_invalidNumber_shouldReturnFalse()
{
    GreaterThanOrEqualStrategy strategy;

    QVERIFY(!strategy.evaluate(
        scalarValue("abc"),
        scalarValue("10")
        ));
}
void TestOperatorStrategies::notEquals_scalarDifferent_shouldReturnTrue()
{
    NotEqualsStrategy strategy;

    QVERIFY(strategy.evaluate(
        scalarValue("admin"),
        scalarValue("user")
        ));
}

void TestOperatorStrategies::notEquals_scalarEqual_shouldReturnFalse()
{
    NotEqualsStrategy strategy;

    QVERIFY(!strategy.evaluate(
        scalarValue("admin"),
        scalarValue("admin")
        ));
}

void TestOperatorStrategies::notEquals_listDifferent_shouldReturnTrue()
{
    NotEqualsStrategy strategy;

    QVERIFY(strategy.evaluate(
        listValue({ "admin", "manager" }),
        listValue({ "admin", "user" })
        ));
}

void TestOperatorStrategies::notEquals_emptyValues_shouldReturnFalse()
{
    NotEqualsStrategy strategy;

    QVERIFY(!strategy.evaluate(
        emptyValue(),
        emptyValue()
        ));
}

void TestOperatorStrategies::notEquals_mixedTypes_shouldReturnTrue()
{
    NotEqualsStrategy strategy;

    QVERIFY(strategy.evaluate(
        scalarValue("admin"),
        listValue({ "admin" })
        ));
}

void TestOperatorStrategies::notExists_empty_shouldReturnTrue()
{
    NotExistsStrategy strategy;

    QVERIFY(strategy.evaluate(
        emptyValue(),
        scalarValue("ignored")
        ));
}

void TestOperatorStrategies::notExists_scalar_shouldReturnFalse()
{
    NotExistsStrategy strategy;

    QVERIFY(!strategy.evaluate(
        scalarValue("admin"),
        emptyValue()
        ));
}

void TestOperatorStrategies::notExists_list_shouldReturnFalse()
{
    NotExistsStrategy strategy;

    QVERIFY(!strategy.evaluate(
        listValue({ "admin" }),
        emptyValue()
        ));
}

void TestOperatorStrategies::notExists_shouldIgnoreRightOperand()
{
    NotExistsStrategy strategy;

    QVERIFY(strategy.ignores_right_operand());
}

void TestOperatorStrategies::notIn_scalarNotInList_shouldReturnTrue()
{
    NotInStrategy strategy;

    QVERIFY(strategy.evaluate(
        scalarValue("guest"),
        listValue({ "admin", "manager", "user" })
        ));
}

void TestOperatorStrategies::notIn_scalarInList_shouldReturnFalse()
{
    NotInStrategy strategy;

    QVERIFY(!strategy.evaluate(
        scalarValue("manager"),
        listValue({ "admin", "manager", "user" })
        ));
}

void TestOperatorStrategies::notIn_invalidTypes_shouldReturnTrue()
{
    NotInStrategy strategy;

    QVERIFY(strategy.evaluate(
        listValue({ "admin" }),
        scalarValue("admin")
        ));
}

void TestOperatorStrategies::lessThan_leftSmaller_shouldReturnTrue()
{
    LessThanStrategy strategy;

    QVERIFY(strategy.evaluate(
        scalarValue("5"),
        scalarValue("10")
        ));
}

void TestOperatorStrategies::lessThan_leftEqual_shouldReturnFalse()
{
    LessThanStrategy strategy;

    QVERIFY(!strategy.evaluate(
        scalarValue("10"),
        scalarValue("10")
        ));
}

void TestOperatorStrategies::lessThan_leftGreater_shouldReturnFalse()
{
    LessThanStrategy strategy;

    QVERIFY(!strategy.evaluate(
        scalarValue("20"),
        scalarValue("10")
        ));
}

void TestOperatorStrategies::lessThan_invalidNumber_shouldReturnFalse()
{
    LessThanStrategy strategy;

    QVERIFY(!strategy.evaluate(
        scalarValue("abc"),
        scalarValue("10")
        ));
}

void TestOperatorStrategies::lessThanOrEqual_leftSmaller_shouldReturnTrue()
{
    LessThanOrEqualStrategy strategy;

    QVERIFY(strategy.evaluate(
        scalarValue("5"),
        scalarValue("10")
        ));
}

void TestOperatorStrategies::lessThanOrEqual_leftEqual_shouldReturnTrue()
{
    LessThanOrEqualStrategy strategy;

    QVERIFY(strategy.evaluate(
        scalarValue("10"),
        scalarValue("10")
        ));
}

void TestOperatorStrategies::lessThanOrEqual_leftGreater_shouldReturnFalse()
{
    LessThanOrEqualStrategy strategy;

    QVERIFY(!strategy.evaluate(
        scalarValue("20"),
        scalarValue("10")
        ));
}

void TestOperatorStrategies::lessThanOrEqual_invalidNumber_shouldReturnFalse()
{
    LessThanOrEqualStrategy strategy;

    QVERIFY(!strategy.evaluate(
        scalarValue("abc"),
        scalarValue("10")
        ));
}

void TestOperatorStrategies::startsWith_validPrefix_shouldReturnTrue()
{
    StartsWithStrategy strategy;

    QVERIFY(strategy.evaluate(
        scalarValue("admin@example.com"),
        scalarValue("admin")
        ));
}

void TestOperatorStrategies::startsWith_invalidPrefix_shouldReturnFalse()
{
    StartsWithStrategy strategy;

    QVERIFY(!strategy.evaluate(
        scalarValue("admin@example.com"),
        scalarValue("user")
        ));
}

void TestOperatorStrategies::startsWith_prefixLongerThanText_shouldReturnFalse()
{
    StartsWithStrategy strategy;

    QVERIFY(!strategy.evaluate(
        scalarValue("abc"),
        scalarValue("abcdef")
        ));
}

void TestOperatorStrategies::startsWith_invalidTypes_shouldReturnFalse()
{
    StartsWithStrategy strategy;

    QVERIFY(!strategy.evaluate(
        listValue({ "admin@example.com" }),
        scalarValue("admin")
        ));
}

void TestOperatorStrategies::regexMatch_cachedPatternShouldMatch()
{
    std::unordered_map<std::string, std::regex> cache;
    cache.emplace("email_pattern", std::regex(R"(^[^@]+@[^@]+\.[^@]+$)"));

    RegexMatchStrategy strategy(std::move(cache));

    QVERIFY(strategy.evaluate(
        scalarValue("admin@example.com"),
        scalarValue("email_pattern")
        ));
}

void TestOperatorStrategies::regexMatch_cachedPatternShouldNotMatch()
{
    std::unordered_map<std::string, std::regex> cache;
    cache.emplace("email_pattern", std::regex(R"(^[^@]+@[^@]+\.[^@]+$)"));

    RegexMatchStrategy strategy(std::move(cache));

    QVERIFY(!strategy.evaluate(
        scalarValue("not-an-email"),
        scalarValue("email_pattern")
        ));
}

void TestOperatorStrategies::regexMatch_missingPatternShouldReturnFalse()
{
    RegexMatchStrategy strategy({});

    QVERIFY(!strategy.evaluate(
        scalarValue("admin@example.com"),
        scalarValue("email_pattern")
        ));
}

void TestOperatorStrategies::regexMatch_invalidTypesShouldReturnFalse()
{
    std::unordered_map<std::string, std::regex> cache;
    cache.emplace("role_pattern", std::regex(R"(admin|manager)"));

    RegexMatchStrategy strategy(std::move(cache));

    QVERIFY(!strategy.evaluate(
        listValue({ "admin" }),
        scalarValue("role_pattern")
        ));

    QVERIFY(!strategy.evaluate(
        scalarValue("admin"),
        listValue({ "role_pattern" })
        ));
}

void TestOperatorStrategies::operatorRegistry_defaultShouldContainAllStandardOperators()
{
    using sea::domain::access_control::PolicyOperator;

    auto registry = OperatorRegistry::create_default();

    QVERIFY(registry.has(PolicyOperator::Equals));
    QVERIFY(registry.has(PolicyOperator::NotEquals));
    QVERIFY(registry.has(PolicyOperator::In));
    QVERIFY(registry.has(PolicyOperator::NotIn));
    QVERIFY(registry.has(PolicyOperator::Contains));
    QVERIFY(registry.has(PolicyOperator::Intersects));
    QVERIFY(registry.has(PolicyOperator::StartsWith));
    QVERIFY(registry.has(PolicyOperator::EndsWith));
    QVERIFY(registry.has(PolicyOperator::GreaterThan));
    QVERIFY(registry.has(PolicyOperator::GreaterThanOrEqual));
    QVERIFY(registry.has(PolicyOperator::LessThan));
    QVERIFY(registry.has(PolicyOperator::LessThanOrEqual));
    QVERIFY(registry.has(PolicyOperator::Exists));
    QVERIFY(registry.has(PolicyOperator::NotExists));
    QVERIFY(registry.has(PolicyOperator::RegexMatch));

    QVERIFY(registry.find(PolicyOperator::Equals) != nullptr);
    QVERIFY(registry.find(PolicyOperator::RegexMatch) != nullptr);
}

void TestOperatorStrategies::operatorRegistry_findUnknownOrMissingShouldReturnNullptr()
{
    using sea::domain::access_control::PolicyOperator;

    OperatorRegistry registry;

    QVERIFY(!registry.has(PolicyOperator::Equals));
    QVERIFY(registry.find(PolicyOperator::Equals) == nullptr);
}

void TestOperatorStrategies::operatorRegistry_regexMatchShouldUseProvidedCache()
{
    using sea::domain::access_control::PolicyOperator;

    std::unordered_map<std::string, std::regex> cache;
    cache.emplace("email_pattern", std::regex(R"(^[^@]+@[^@]+\.[^@]+$)"));

    auto registry = OperatorRegistry::create_default(std::move(cache));

    const auto* strategy = registry.find(PolicyOperator::RegexMatch);

    QVERIFY(strategy != nullptr);
    QCOMPARE(qs(strategy->name()), QString("regex_match"));

    QVERIFY(strategy->evaluate(
        scalarValue("admin@example.com"),
        scalarValue("email_pattern")
        ));

    QVERIFY(!strategy->evaluate(
        scalarValue("not-an-email"),
        scalarValue("email_pattern")
        ));
}
// ─────────────────────────────────────────────
// Names
// ─────────────────────────────────────────────

void TestOperatorStrategies::strategyNames_shouldReturnExpectedNames()
{
    ContainsStrategy contains;
    EndsWithStrategy endsWith;
    EqualsStrategy equals;
    ExistsStrategy exists;
    GreaterThanStrategy greaterThan;
    GreaterThanOrEqualStrategy greaterThanOrEqual;
    InStrategy in;
    IntersectsStrategy intersects;
    LessThanStrategy lessThan;
    LessThanOrEqualStrategy lessThanOrEqual;
    NotEqualsStrategy notEquals;
    NotExistsStrategy notExists;
    NotInStrategy notIn;
    StartsWithStrategy startsWith;
    RegexMatchStrategy regexMatch({});

    QCOMPARE(qs(contains.name()), QString("contains"));
    QCOMPARE(qs(endsWith.name()), QString("ends_with"));
    QCOMPARE(qs(equals.name()), QString("equals"));
    QCOMPARE(qs(exists.name()), QString("exists"));
    QCOMPARE(qs(greaterThan.name()), QString("greater_than"));
    QCOMPARE(qs(greaterThanOrEqual.name()), QString("greater_than_or_equal"));
    QCOMPARE(qs(in.name()), QString("in"));
    QCOMPARE(qs(intersects.name()), QString("intersects"));
    QCOMPARE(qs(lessThan.name()), QString("less_than"));
    QCOMPARE(qs(lessThanOrEqual.name()), QString("less_than_or_equal"));
    QCOMPARE(qs(notEquals.name()), QString("not_equals"));
    QCOMPARE(qs(notExists.name()), QString("not_exists"));
    QCOMPARE(qs(notIn.name()), QString("not_in"));
    QCOMPARE(qs(startsWith.name()), QString("starts_with"));
    QCOMPARE(qs(regexMatch.name()), QString("regex_match"));
}