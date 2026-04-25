// sea_domain/web/HttpMethod.hpp
#pragma once
#include <string>
#include <string_view>
#include <stdexcept>

namespace sea::domain::http {

enum class HttpMethod {
    Get,
    Post,
    Put,
    Patch,
    Delete,
    Head,
    Options
};

constexpr std::string_view to_string(HttpMethod m) noexcept {
    switch (m) {
        case HttpMethod::Get:     return "GET";
        case HttpMethod::Post:    return "POST";
        case HttpMethod::Put:     return "PUT";
        case HttpMethod::Patch:   return "PATCH";
        case HttpMethod::Delete:  return "DELETE";
        case HttpMethod::Head:    return "HEAD";
        case HttpMethod::Options: return "OPTIONS";
    }
    return "UNKNOWN";
}

inline HttpMethod from_string(std::string_view s) {
    if (s == "GET")     return HttpMethod::Get;
    if (s == "POST")    return HttpMethod::Post;
    if (s == "PUT")     return HttpMethod::Put;
    if (s == "PATCH")   return HttpMethod::Patch;
    if (s == "DELETE")  return HttpMethod::Delete;
    if (s == "HEAD")    return HttpMethod::Head;
    if (s == "OPTIONS") return HttpMethod::Options;
    throw std::invalid_argument("Unknown HTTP method: " + std::string(s));
}

} // namespace sea::domain::web
