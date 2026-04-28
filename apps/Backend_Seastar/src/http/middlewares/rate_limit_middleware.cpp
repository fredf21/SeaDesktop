#include "rate_limit_middleware.h"

#include <nlohmann/json.hpp>

#include <utility>

namespace sea::http::middlewares {

using json = nlohmann::json;
using namespace sea::domain::security;

RateLimitMiddleware::RateLimitMiddleware(
    std::unique_ptr<seastar::httpd::handler_base> inner,
    std::vector<RateLimitRule> rules,
    seastar::sharded<RateLimitStore>& store)
    : inner_(std::move(inner))
    , rules_(std::move(rules))
    , store_(store)
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
RateLimitMiddleware::handle(
    const seastar::sstring& path,
    std::unique_ptr<seastar::http::request> req,
    std::unique_ptr<seastar::http::reply> rep)
{
    // ─────────────────────────────────────────
    // ÉTAPE 1 : Identifier les clients pour TOUTES les règles
    // (avant de consommer le req dans le downstream)
    // ─────────────────────────────────────────
    struct RuleContext {
        std::string bucket_key;
        unsigned owner_shard;
        double capacity;
        double refill_rate;
        bool active;  // true si le client a été identifié
    };

    std::vector<RuleContext> contexts;
    contexts.reserve(rules_.size());

    for (const auto& rule : rules_) {
        RuleContext ctx;
        const std::string client_id = identify_client(*req, rule.scope());

        if (client_id.empty()) {
            ctx.active = false;
            contexts.push_back(std::move(ctx));
            continue;
        }

        ctx.bucket_key = make_bucket_key(rule.scope(), client_id);
        ctx.owner_shard = RateLimitStore::shard_of_key(ctx.bucket_key);
        ctx.capacity = static_cast<double>(rule.burst());
        ctx.refill_rate = rule.refill_rate_per_second();
        ctx.active = true;

        contexts.push_back(std::move(ctx));
    }

    // ─────────────────────────────────────────
    // ÉTAPE 2 : Tentative de consommation (peut court-circuiter avec 429)
    // ─────────────────────────────────────────
    for (std::size_t i = 0; i < contexts.size(); ++i) {
        const auto& ctx = contexts[i];
        if (!ctx.active) {
            continue;
        }

        const auto result = co_await store_.invoke_on(
            ctx.owner_shard,
            [bucket_key = ctx.bucket_key,
             capacity = ctx.capacity,
             refill_rate = ctx.refill_rate](RateLimitStore& s) {
                return s.consume(bucket_key, capacity, refill_rate);
            }
            );

        if (!result.allowed) {
            const auto& rule = rules_[i];

            rep->set_status(seastar::http::reply::status_type::too_many_requests);
            rep->add_header("Retry-After",
                            std::to_string(result.retry_after_seconds));
            rep->add_header("X-RateLimit-Limit",
                            std::to_string(static_cast<int>(result.capacity)));
            rep->add_header("X-RateLimit-Remaining", "0");
            rep->add_header("X-RateLimit-Reset",
                            std::to_string(result.retry_after_seconds));

            rep->write_body("application/json", json{
                                                    {"error", "rate_limit_exceeded"},
                                                    {"message", "Trop de requetes, veuillez patienter"},
                                                    {"retry_after_seconds", result.retry_after_seconds},
                                                    {"scope", std::string(to_string(rule.scope()))},
                                                    {"limit", static_cast<int>(result.capacity)}
                                                }.dump());

            co_return std::move(rep);
        }
    }

    // ─────────────────────────────────────────
    // ÉTAPE 3 : Appel du downstream (req est consumed ici)
    // ─────────────────────────────────────────
    auto reply = co_await inner_->handle(path, std::move(req), std::move(rep));

    // ─────────────────────────────────────────
    // ÉTAPE 4 : Peek sur la première règle active pour informer le client
    // ─────────────────────────────────────────
    // On lit l'état SANS consommer (peek), ce qui donne au client une vue
    // honnête de son quota restant.
    for (const auto& ctx : contexts) {
        if (!ctx.active) {
            continue;
        }

        const auto info = co_await store_.invoke_on(
            ctx.owner_shard,
            [bucket_key = ctx.bucket_key,
             capacity = ctx.capacity,
             refill_rate = ctx.refill_rate](RateLimitStore& s) {
                return s.peek(bucket_key, capacity, refill_rate);
            }
            );

        reply->add_header("X-RateLimit-Limit",
                          std::to_string(static_cast<int>(info.capacity)));
        reply->add_header("X-RateLimit-Remaining",
                          std::to_string(static_cast<int>(info.remaining)));

        // On ne fait le peek que sur la première règle active (la plus
        // restrictive en général). Si tu veux tous les niveaux, tu peux
        // ajouter X-RateLimit-Limit-PerIp, X-RateLimit-Limit-PerUser, etc.
        break;
    }

    co_return std::move(reply);
}

std::string RateLimitMiddleware::identify_client(
    const seastar::http::request& req,
    RateLimitScope scope) const
{
    switch (scope) {
    case RateLimitScope::PerIp: {
        // Priorité : X-Forwarded-For > X-Real-IP > "unknown"
        auto fwd = req._headers.find("X-Forwarded-For");
        if (fwd != req._headers.end() && !fwd->second.empty()) {
            const std::string value(fwd->second);
            const auto comma = value.find(',');
            return (comma != std::string::npos)
                       ? value.substr(0, comma)
                       : value;
        }
        auto real_ip = req._headers.find("X-Real-IP");
        if (real_ip != req._headers.end()) {
            return std::string(real_ip->second);
        }
        return "unknown";
    }

    case RateLimitScope::PerUser: {
        // Le user_id doit être injecté par ProtectedHandler dans X-User-Id
        auto it = req._headers.find("X-User-Id");
        if (it != req._headers.end()) {
            return std::string(it->second);
        }
        return "";  // pas authentifié, on skip
    }

    case RateLimitScope::PerApiKey: {
        auto it = req._headers.find("X-API-Key");
        if (it != req._headers.end()) {
            return std::string(it->second);
        }
        return "";
    }

    case RateLimitScope::Global: {
        return "global";
    }
    }
    return "";
}

std::string RateLimitMiddleware::make_bucket_key(
    RateLimitScope scope,
    const std::string& client_id)
{
    return std::string(to_string(scope)) + ":" + client_id;
}

std::unique_ptr<seastar::httpd::handler_base> apply_rate_limit(
    std::unique_ptr<seastar::httpd::handler_base> handler,
    std::vector<RateLimitRule> rules,
    seastar::sharded<RateLimitStore>& store)
{
    return std::make_unique<RateLimitMiddleware>(
        std::move(handler),
        std::move(rules),
        store
        );
}

} // namespace sea::http::middlewares