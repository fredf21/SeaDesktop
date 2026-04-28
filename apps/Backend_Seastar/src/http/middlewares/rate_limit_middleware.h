#ifndef RATE_LIMIT_MIDDLEWARE_H
#define RATE_LIMIT_MIDDLEWARE_H

#include "rate_limit_store.h"
#include "security_scheme/rate_limit_rule.h"

#include <seastar/core/sharded.hh>
#include <seastar/http/handlers.hh>
#include <seastar/http/reply.hh>
#include <seastar/http/request.hh>

#include <memory>
#include <string>
#include <vector>

namespace sea::http::middlewares {

class RateLimitMiddleware : public seastar::httpd::handler_base {
public:
    RateLimitMiddleware(
        std::unique_ptr<seastar::httpd::handler_base> inner,
        std::vector<sea::domain::security::RateLimitRule> rules,
        seastar::sharded<RateLimitStore>& store
        );

    seastar::future<std::unique_ptr<seastar::http::reply>> handle(
        const seastar::sstring& path,
        std::unique_ptr<seastar::http::request> req,
        std::unique_ptr<seastar::http::reply> rep
        ) override;

private:
    // Identifie le client selon le scope (IP, user, api_key, global)
    std::string identify_client(
        const seastar::http::request& req,
        sea::domain::security::RateLimitScope scope) const;

    // Construit la clé de bucket : "scope_name:client_identifier"
    static std::string make_bucket_key(
        sea::domain::security::RateLimitScope scope,
        const std::string& client_id);

    std::unique_ptr<seastar::httpd::handler_base> inner_;
    std::vector<sea::domain::security::RateLimitRule> rules_;
    seastar::sharded<RateLimitStore>& store_;
};

// Helper de chaînage
std::unique_ptr<seastar::httpd::handler_base> apply_rate_limit(
    std::unique_ptr<seastar::httpd::handler_base> handler,
    std::vector<sea::domain::security::RateLimitRule> rules,
    seastar::sharded<RateLimitStore>& store
    );

} // namespace sea::http::middlewares

#endif // RATE_LIMIT_MIDDLEWARE_H