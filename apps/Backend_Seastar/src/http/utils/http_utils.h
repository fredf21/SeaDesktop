#pragma once

#include <seastar/http/httpd.hh>

#include "runtime/dynamic_record.h"
#include "runtime/generic_crud_engine.h"

#include <optional>
#include <string>
#include <vector>

namespace sea::http::utils {

seastar::future<std::string>
read_request_body(seastar::http::request& req);

std::string json_escape(const std::string& input);

std::string record_to_json(
    const sea::infrastructure::runtime::DynamicRecord& record);

std::string records_to_json(
    const std::vector<sea::infrastructure::runtime::DynamicRecord>& records);

std::optional<std::string> dynamic_value_to_string(
    const sea::infrastructure::runtime::DynamicValue& value);

std::optional<std::string> dynamic_value_to_string_id(
    const sea::infrastructure::runtime::DynamicValue& value);

bool dynamic_value_matches_string(
    const sea::infrastructure::runtime::DynamicValue& value,
    const std::string& expected);

std::optional<sea::infrastructure::runtime::DynamicRecord>
find_record_by_field(
    const std::vector<sea::infrastructure::runtime::DynamicRecord>& records,
    const std::string& field_name,
    const std::string& expected_value);

std::optional<std::string>
extract_bearer_token(const seastar::http::request& req);

std::string generate_uuid();

seastar::future<std::int64_t>
generate_int_id(
    const std::string& entity_name,
    const std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine>& crud_engine);

std::string lower_first(std::string value);

std::string base_path_without_id_suffix(const std::string& path);

} // namespace sea::http::utils
