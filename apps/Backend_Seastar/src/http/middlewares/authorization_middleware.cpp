#include "authorization_middleware.h"

#include "security_scheme//abac_mode.h"
#include "access_control/access_control_spec.h"
#include "access_control/entity_access_control.h"
#include "access_control/crud_operation.h"
#include "access_control/evaluation_options.h"
#include "access_control/evaluation_result.h"
#include "access_control/policy_subject.h"
#include "access_control/policy_context.h"
#include "access_control/evaluation_options.h"
#include "access_control/evaluation_result.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iostream>
#include <nlohmann/json_fwd.hpp>
#include <utility>

namespace sea::http::middlewares {

namespace {

using sea::domain::access_control::AbacMode;
using sea::domain::access_control::CrudOperation;
using sea::domain::access_control::PolicySubject;
using sea::domain::access_control::PolicyContext;
using sea::application::access_control::EvaluationOptions;
using sea::application::access_control::EvaluationResult;

// ──────────────────────────────────────────────────────────────────
// Helpers strings
// ──────────────────────────────────────────────────────────────────

/**
 * Convertit un Header-Case en snake_case.
 *  "Department-Id" → "department_id"
 *  "Mfa-Verified"  → "mfa_verified"
 */
std::string from_header_case(const std::string& header_part)
{
    std::string result;
    result.reserve(header_part.size());

    for (char c : header_part) {
        if (c == '-') {
            result += '_';
        } else {
            result += static_cast<char>(
                std::tolower(static_cast<unsigned char>(c))
                );
        }
    }
    return result;
}

bool starts_with_ci(std::string_view s, std::string_view prefix)
{
    if (s.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

std::string crud_op_to_string(CrudOperation op)
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

// ──────────────────────────────────────────────────────────────────
// Construction du PolicySubject depuis les headers X-User-*
// ──────────────────────────────────────────────────────────────────

/**
 * Construit un PolicySubject depuis les headers HTTP injectes par
 * ProtectedHandler (Module 4).
 *
 * Mapping :
 *   X-User-Id            → subject.id
 *   X-User-Email         → subject.email
 *   X-User-Role          → subject.roles[]
 *   X-User-Department-Id → subject.attributes["department_id"]
 *   X-User-XXX           → subject.attributes["xxx"] (snake_case)
 */
PolicySubject build_subject_from_headers(const seastar::http::request& req)
{
    PolicySubject subject;

    static constexpr std::string_view prefix = "x-user-";

    for (const auto& [header_key, header_value] : req._headers) {
        const std::string_view key_view(header_key.data(), header_key.size());

        if (!starts_with_ci(key_view, prefix)) {
            continue;
        }

        // Extrait la partie apres "X-User-" et la convertit en snake_case
        const std::string suffix(
            header_key.data() + prefix.size(),
            header_key.size() - prefix.size()
            );
        const std::string attr_name = from_header_case(suffix);

        const std::string value(header_value.data(), header_value.size());

        // Mapping vers les champs standards du subject
        if (attr_name == "id") {
            subject.id = value;
        } else if (attr_name == "email") {
            subject.email = value;
        } else if (attr_name == "role") {
            // Le role est mis dans roles[] (singleton pour l'instant)
            subject.roles.push_back(value);
        } else {
            // Sinon, c'est un attribut custom (department_id, manager_id, ...)
            subject.attributes[attr_name] = value;
        }
    }

    return subject;
}

// ──────────────────────────────────────────────────────────────────
// Construction du PolicyContext
// ──────────────────────────────────────────────────────────────────

PolicyContext build_context(const seastar::http::request& req,
                            const std::string& path)
{
    PolicyContext ctx;

    ctx.method = std::string(req._method.data(), req._method.size());
    ctx.path = path;

    // IP du client (best effort)
    auto it = req._headers.find("X-Forwarded-For");
    if (it != req._headers.end()) {
        ctx.ip = std::string(it->second.data(), it->second.size());
    }

    ctx.now = std::chrono::system_clock::now();

    return ctx;
}

// ──────────────────────────────────────────────────────────────────
// Resolution du mode ABAC (entity > service > defaut)
// ──────────────────────────────────────────────────────────────────

AbacMode resolve_abac_mode(
    const sea::domain::access_control::AccessControlConfig& config,
    const sea::domain::access_control::EntityAccessControl& entity_ac)
{
    if (const auto override = entity_ac.abac_mode_override();
        override.has_value()) {
        return *override;
    }
    return config.abac_mode();
}

// ──────────────────────────────────────────────────────────────────
// Construction d'une reponse 403
// ──────────────────────────────────────────────────────────────────

std::unique_ptr<seastar::http::reply> make_forbidden_response(
    std::unique_ptr<seastar::http::reply> rep,
    const std::string& reason)
{
    nlohmann::json body = {
        {"error", "Forbidden"},
        {"message", reason}
    };

    rep->set_status(seastar::http::reply::status_type::forbidden);
    rep->_headers["Content-Type"] = "application/json";
    rep->_content = body.dump();
    return rep;
}

// ──────────────────────────────────────────────────────────────────
// Bypass admin
// ──────────────────────────────────────────────────────────────────

bool is_admin_bypass(
    const sea::domain::access_control::AccessControlConfig& config,
    const PolicySubject& subject)
{
    if (!config.default_allow_admin()) {
        return false;
    }

    const auto& admin_role = config.admin_role();
    if (admin_role.empty()) {
        return false;
    }

    return std::any_of(
        subject.roles.begin(), subject.roles.end(),
        [&admin_role](const std::string& role) {
            return role == admin_role;
        }
        );
}

// ──────────────────────────────────────────────────────────────────
// Resultat d'evaluation d'un check
// ──────────────────────────────────────────────────────────────────

enum class CheckOutcome {
    Allow,           // Le check passe, on continue
    Deny,            // Le check echoue → 403
    PassToHandler    // Subject-only OK + resource-aware en mode permissive
};

struct CheckResult {
    CheckOutcome outcome;
    std::string reason;
};

/**
 * Evalue un seul check d'autorisation.
 */
CheckResult evaluate_single_check(
    const AuthorizationCheck& check,
    const sea::domain::Schema& schema,
    const sea::domain::access_control::AccessControlConfig& config,
    const sea::application::access_control::PolicyEngine& engine,
    const PolicySubject& subject,
    const PolicyContext& context)
{
    // 1. Trouve l'entite
    const auto* entity = schema.find_entity(check.entity_name);
    if (entity == nullptr) {
        return CheckResult{
            CheckOutcome::Deny,
            "Entity '" + check.entity_name + "' not found"
        };
    }

    // 2. Trouve l'AccessControlSpec
    const auto* spec = entity->access_control.find_spec(check.operation);

    if (spec == nullptr) {
        // Pas de regle definie → applique default_policy
        if (config.default_policy() ==
            sea::domain::access_control::DefaultPolicy::Deny) {
            return CheckResult{
                CheckOutcome::Deny,
                "No access control rule defined for " + check.description +
                    " (default_policy=deny)"
            };
        }
        // default_policy=allow → laisse passer
        return CheckResult{ CheckOutcome::Allow, "" };
    }

    // 3. Evalue la condition (subject-only fast path)
    const auto eval_options = EvaluationOptions::pre_handler();
    const auto result = engine.evaluate_subject_only(
        spec->condition(),
        subject,
        context,
        eval_options
        );

    // 4. Decide selon le resultat
    if (!result.allowed) {
        return CheckResult{
            CheckOutcome::Deny,
            check.description + " : " +
                result.reason.value_or("subject does not satisfy the policy")
        };
    }

    // Subject-only OK
    if (spec->requires_resource()) {
        // La regle a une partie resource-aware → consulte le mode ABAC
        const auto abac_mode = resolve_abac_mode(config, entity->access_control);

        if (abac_mode == AbacMode::Strict) {
            return CheckResult{
                CheckOutcome::Deny,
                check.description +
                    " : strict ABAC mode blocks resource-aware rules"
            };
        }
        // Mode permissive : laisse passer
        return CheckResult{ CheckOutcome::PassToHandler, "" };
    }

    // Pas de partie resource-aware → decision finale OK
    return CheckResult{ CheckOutcome::Allow, "" };
}

} // namespace anonyme

// ──────────────────────────────────────────────────────────────────
// AuthorizationMiddleware
// ──────────────────────────────────────────────────────────────────

AuthorizationMiddleware::AuthorizationMiddleware(
    std::unique_ptr<seastar::httpd::handler_base> inner,
    const sea::domain::Schema* schema,
    const sea::domain::access_control::AccessControlConfig* config,
    std::shared_ptr<sea::application::access_control::PolicyEngine> policy_engine)
    : inner_(std::move(inner))
    , schema_(schema)
    , config_(config)
    , policy_engine_(std::move(policy_engine))
    , resolver_(*schema)
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
AuthorizationMiddleware::handle(
    const seastar::sstring& path,
    std::unique_ptr<seastar::http::request> req,
    std::unique_ptr<seastar::http::reply> rep)
{
    // Le path passe en parametre peut etre vide selon le routing Seastar.
    // On utilise req->_url qui contient toujours le path complet.
    const seastar::sstring& real_path = !req->_url.empty() ? req->_url : path;
    const std::string real_path_str(real_path.data(), real_path.size());
    const std::string method_str(req->_method.data(), req->_method.size());

    // 1. Resolve la route → plan d'autorisation
    const auto plan = resolver_.resolve(method_str, real_path_str);

    if (plan.unknown_route) {
        // Fail closed : toute route non identifiee est refusee
        std::cerr << "[AUTHZ] " << method_str << " " << real_path_str
                  << " : UNKNOWN ROUTE → 403\n";
        rep = make_forbidden_response(std::move(rep),
                                      "Route not recognized for authorization");
        co_return std::move(rep);
    }

    if (plan.checks.empty()) {
        // Aucun check defini (cas de figure rare mais on laisse passer)
        co_return co_await inner_->handle(path, std::move(req), std::move(rep));
    }

    // 2. Construit le PolicySubject une seule fois (reutilise pour tous les checks)
    const PolicySubject subject = build_subject_from_headers(*req);
    const PolicyContext context = build_context(*req, real_path_str);

    // 3. Bypass admin si configure
    if (is_admin_bypass(*config_, subject)) {
        std::cerr << "[AUTHZ] " << method_str << " " << real_path_str
                  << " : ADMIN BYPASS (role=" << config_->admin_role() << ")\n";
        co_return co_await inner_->handle(path, std::move(req), std::move(rep));
    }

    // 4. Execute tous les checks (fail-fast hybride : 403 immediat sur le 1er
    //    refus, mais log les checks restants pour audit)
    std::optional<std::string> first_deny_reason;

    for (std::size_t i = 0; i < plan.checks.size(); ++i) {
        const auto& check = plan.checks[i];

        const auto result = evaluate_single_check(
            check, *schema_, *config_, *policy_engine_, subject, context
            );

        switch (result.outcome) {
        case CheckOutcome::Allow:
            std::cerr << "[AUTHZ] " << method_str << " " << real_path_str
                      << " : check[" << i << "] " << check.entity_name << "."
                      << crud_op_to_string(check.operation)
                      << " → ALLOW (subject-only)\n";
            break;

        case CheckOutcome::PassToHandler:
            std::cerr << "[AUTHZ] " << method_str << " " << real_path_str
                      << " : check[" << i << "] " << check.entity_name << "."
                      << crud_op_to_string(check.operation)
                      << " → PASS (resource-aware, permissive)\n";
            break;

        case CheckOutcome::Deny:
            std::cerr << "[AUTHZ] " << method_str << " " << real_path_str
                      << " : check[" << i << "] " << check.entity_name << "."
                      << crud_op_to_string(check.operation)
                      << " → DENY (" << result.reason << ")\n";

            if (!first_deny_reason.has_value()) {
                first_deny_reason = result.reason;
            }
            break;
        }
    }

    if (first_deny_reason.has_value()) {
        // 403 fail-fast hybride : on a evalue tous les checks pour les logs,
        // mais on retourne juste le 1er refus dans le body
        rep = make_forbidden_response(std::move(rep), *first_deny_reason);
        co_return std::move(rep);
    }

    // 5. Tous les checks passent → delegue au handler interne
    co_return co_await inner_->handle(path, std::move(req), std::move(rep));
}

// ──────────────────────────────────────────────────────────────────
// Helper apply_authorization
// ──────────────────────────────────────────────────────────────────

std::unique_ptr<seastar::httpd::handler_base> apply_authorization(
    std::unique_ptr<seastar::httpd::handler_base> handler,
    const sea::domain::Schema& schema,
    const sea::domain::access_control::AccessControlConfig& config,
    std::shared_ptr<sea::application::access_control::PolicyEngine> policy_engine)
{
    if (!config.enabled()) {
        return handler;
    }

    return std::make_unique<AuthorizationMiddleware>(
        std::move(handler),
        &schema,
        &config,
        std::move(policy_engine)
        );
}

} // namespace sea::http::middlewares