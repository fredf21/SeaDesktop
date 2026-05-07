#include "crud_operation.h"

#include <algorithm>
#include <cctype>

namespace sea::domain::access_control {

namespace {

std::string to_lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

} // namespace anonyme

std::string_view to_string(CrudOperation op) noexcept
{
    switch (op) {
    case CrudOperation::List:    return "list";
    case CrudOperation::GetById: return "get_by_id";
    case CrudOperation::Create:  return "create";
    case CrudOperation::Update:  return "update";
    case CrudOperation::Delete:  return "delete";
    }
    return "unknown";
}

std::optional<CrudOperation> crud_operation_from_string(
    const std::string& s) noexcept
{
    const auto lower = to_lower(s);

    if (lower == "list")                              return CrudOperation::List;
    if (lower == "get_by_id" || lower == "get")       return CrudOperation::GetById;
    if (lower == "create")                            return CrudOperation::Create;
    if (lower == "update")                            return CrudOperation::Update;
    if (lower == "delete")                            return CrudOperation::Delete;

    return std::nullopt;
}

} // namespace sea::domain::access_control