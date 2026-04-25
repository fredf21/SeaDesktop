#include "http_utils.h"

#include <seastar/core/temporary_buffer.hh>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <random>
#include <sstream>
#include <variant>

namespace sea::http::utils {

using sea::infrastructure::runtime::DynamicRecord;
using sea::infrastructure::runtime::DynamicValue;
using sea::infrastructure::runtime::GenericCrudEngine;

seastar::future<std::string>
read_request_body(seastar::http::request& req)
{
    std::string body;

    while (true) {
        seastar::temporary_buffer<char> chunk = co_await req.content_stream->read();
        if (chunk.empty()) {
            break;
        }

        body.append(chunk.get(), chunk.size());
    }

    co_return body;
}

std::string json_escape(const std::string& input)
{
    std::string out;
    out.reserve(input.size());

    for (char c : input) {
    switch (c) {
    case '"': out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\b': out += "\\b"; break;
    case '\f': out += "\\f"; break;
    case '\n': out += "\\n"; break;
    case '\r': out += "\\r"; break;
    case '\t': out += "\\t"; break;
    default: out += c; break;
    }
    }

    return out;
}

static std::string dynamic_value_to_json(const DynamicValue& value)
{
    if (std::holds_alternative<std::monostate>(value)) {
        return "null";
    }

    if (std::holds_alternative<std::string>(value)) {
        return "\"" + json_escape(std::get<std::string>(value)) + "\"";
    }

    if (std::holds_alternative<std::int64_t>(value)) {
        return std::to_string(std::get<std::int64_t>(value));
    }

    if (std::holds_alternative<double>(value)) {
        std::ostringstream oss;
        oss << std::get<double>(value);
        return oss.str();
    }

    if (std::holds_alternative<bool>(value)) {
        return std::get<bool>(value) ? "true" : "false";
    }

    if (std::holds_alternative<std::vector<std::string>>(value)) {
        std::ostringstream oss;
        oss << "[";

        const auto& values = std::get<std::vector<std::string>>(value);
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (i > 0) {
                oss << ",";
            }
            oss << "\"" << json_escape(values[i]) << "\"";
        }

        oss << "]";
        return oss.str();
    }

    return "null";
}

std::string record_to_json(const DynamicRecord& record)
{
    std::ostringstream oss;
    oss << "{";

    bool first = true;
    for (const auto& [key, value] : record) {
        if (!first) {
            oss << ",";
        }

        first = false;
        oss << "\"" << json_escape(key) << "\":" << dynamic_value_to_json(value);
    }

    oss << "}";
    return oss.str();
}

std::string records_to_json(const std::vector<DynamicRecord>& records)
{
    std::ostringstream oss;
    oss << "[";

    bool first = true;
    for (const auto& record : records) {
        if (!first) {
            oss << ",";
        }

        first = false;
        oss << record_to_json(record);
    }

    oss << "]";
    return oss.str();
}

std::optional<std::string> dynamic_value_to_string(const DynamicValue& value)
{
    if (std::holds_alternative<std::string>(value)) {
        return std::get<std::string>(value);
    }

    if (std::holds_alternative<std::int64_t>(value)) {
        return std::to_string(std::get<std::int64_t>(value));
    }

    if (std::holds_alternative<double>(value)) {
        std::ostringstream oss;
        oss << std::get<double>(value);
        return oss.str();
    }

    if (std::holds_alternative<bool>(value)) {
        return std::get<bool>(value) ? "true" : "false";
    }

    return std::nullopt;
}

std::optional<std::string> dynamic_value_to_string_id(const DynamicValue& value)
{
    if (std::holds_alternative<std::string>(value)) {
        return std::get<std::string>(value);
    }

    if (std::holds_alternative<std::int64_t>(value)) {
        return std::to_string(std::get<std::int64_t>(value));
    }

    return std::nullopt;
}

bool dynamic_value_matches_string(const DynamicValue& value, const std::string& expected)
{
    if (std::holds_alternative<std::string>(value)) {
        return std::get<std::string>(value) == expected;
    }

    if (std::holds_alternative<std::int64_t>(value)) {
        return std::to_string(std::get<std::int64_t>(value)) == expected;
    }

    if (std::holds_alternative<double>(value)) {
        std::ostringstream oss;
        oss << std::get<double>(value);
        return oss.str() == expected;
    }

    if (std::holds_alternative<bool>(value)) {
        if (expected == "true") return std::get<bool>(value);
        if (expected == "false") return !std::get<bool>(value);
    }

    return false;
}

std::optional<DynamicRecord>
find_record_by_field(const std::vector<DynamicRecord>& records,
                     const std::string& field_name,
                     const std::string& expected_value)
{
    for (const auto& record : records) {
        const auto it = record.find(field_name);
        if (it == record.end()) {
            continue;
        }

        if (dynamic_value_matches_string(it->second, expected_value)) {
            return record;
        }
    }

    return std::nullopt;
}

std::optional<std::string>
extract_bearer_token(const seastar::http::request& req)
{
    const auto auth_header = req.get_header("Authorization");
    if (auth_header.empty()) {
        return std::nullopt;
    }

    static const std::string prefix = "Bearer ";
    const std::string header_value = auth_header;

    if (header_value.rfind(prefix, 0) != 0) {
        return std::nullopt;
    }

    return header_value.substr(prefix.size());
}

std::string generate_uuid()
{
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<std::uint64_t> dis;

    std::uint64_t part1 = dis(gen);
    std::uint64_t part2 = dis(gen);

    part1 = (part1 & 0xffffffffffff0fffULL) | 0x0000000000004000ULL;
    part2 = (part2 & 0x3fffffffffffffffULL) | 0x8000000000000000ULL;

    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(8) << (part1 >> 32) << "-"
        << std::setw(4) << ((part1 >> 16) & 0xffff) << "-"
        << std::setw(4) << (part1 & 0xffff) << "-"
        << std::setw(4) << (part2 >> 48) << "-"
        << std::setw(12) << (part2 & 0xffffffffffffULL);

    return oss.str();
}

seastar::future<std::int64_t>
generate_int_id(
    const std::string& entity_name,
    const std::shared_ptr<GenericCrudEngine>& crud_engine)
{
    const auto records = co_await crud_engine->list(entity_name);

    std::int64_t max_id = 0;

    for (const auto& record : records) {
        const auto it = record.find("id");

        if (it != record.end() &&
            std::holds_alternative<std::int64_t>(it->second)) {
            max_id = std::max(max_id, std::get<std::int64_t>(it->second));
        }
    }

    co_return max_id + 1;
}

std::string lower_first(std::string value)
{
    if (!value.empty()) {
        value[0] = static_cast<char>(
            std::tolower(static_cast<unsigned char>(value[0]))
        );
    }

    return value;
}

std::string base_path_without_id_suffix(const std::string& path)
{
    static constexpr std::string_view suffix = "/{id}";
    const auto pos = path.rfind(suffix);

    if (pos == std::string::npos) {
        return path;
    }

    return path.substr(0, pos);
}

} // namespace sea::http::utils
