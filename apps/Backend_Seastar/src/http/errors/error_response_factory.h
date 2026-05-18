#pragma once

#include <seastar/http/reply.hh>
#include <nlohmann/json.hpp>

#include <memory>
#include <string>

#include "http_exception.h"

namespace sea::http::errors {

inline std::unique_ptr<seastar::http::reply> make_error_reply(
    seastar::http::reply::status_type status,
    const std::string& code,
    const std::string& message)
{
    auto rep = std::make_unique<seastar::http::reply>();

    rep->set_status(status);

    rep->write_body("application/json", nlohmann::json{
                                            {"success", false},
                                            {"error", {
                                                          {"code", code},
                                                          {"message", message}
                                                      }}
                                        }.dump());

    return rep;
}

inline std::unique_ptr<seastar::http::reply> make_error_reply(
    const HttpException& e)
{
    return make_error_reply(
        e.status(),
        e.code(),
        e.what()
        );
}

inline std::unique_ptr<seastar::http::reply> make_internal_error_reply()
{
    return make_error_reply(
        seastar::http::reply::status_type::internal_server_error,
        "INTERNAL_SERVER_ERROR",
        "Erreur interne du serveur"
        );
}

} // namespace sea::http::errors