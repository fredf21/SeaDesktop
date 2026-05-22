#include "test_http_method.h"

#include "protocol/http_protocol/http_method.h"

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

void TestHttpMethod::toString_shouldReturnUppercaseMethods()
{
    using namespace sea::domain::http;

    QCOMPARE(qs(to_string(HttpMethod::Get)), QString("GET"));
    QCOMPARE(qs(to_string(HttpMethod::Post)), QString("POST"));
    QCOMPARE(qs(to_string(HttpMethod::Put)), QString("PUT"));
    QCOMPARE(qs(to_string(HttpMethod::Patch)), QString("PATCH"));
    QCOMPARE(qs(to_string(HttpMethod::Delete)), QString("DELETE"));
    QCOMPARE(qs(to_string(HttpMethod::Head)), QString("HEAD"));
    QCOMPARE(qs(to_string(HttpMethod::Options)), QString("OPTIONS"));
}

void TestHttpMethod::fromString_shouldParseValidMethods()
{
    using namespace sea::domain::http;

    QCOMPARE(from_string("GET"), HttpMethod::Get);
    QCOMPARE(from_string("POST"), HttpMethod::Post);
    QCOMPARE(from_string("PUT"), HttpMethod::Put);
    QCOMPARE(from_string("PATCH"), HttpMethod::Patch);
    QCOMPARE(from_string("DELETE"), HttpMethod::Delete);
    QCOMPARE(from_string("HEAD"), HttpMethod::Head);
    QCOMPARE(from_string("OPTIONS"), HttpMethod::Options);
}

void TestHttpMethod::fromString_invalidShouldThrow()
{
    using namespace sea::domain::http;

    QVERIFY_THROWS_EXCEPTION(
        std::invalid_argument,
        from_string("CONNECT")
        );
}

void TestHttpMethod::fromString_lowercaseShouldThrow()
{
    using namespace sea::domain::http;

    QVERIFY_THROWS_EXCEPTION(
        std::invalid_argument,
        from_string("get")
        );
}