#include "resource_authorization_helper.h"

#include "access_control/entity_access_control.h"
#include "access_control/evaluation_options.h"
#include "access_control/evaluation_result.h"
#include "entity.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iostream>
#include <utility>

namespace sea::http::handlers::access_control {

namespace {

using sea::domain::access_control::PolicySubject;
using sea::domain::access_control::PolicyResource;
using sea::domain::access_control::PolicyContext;
using sea::domain::access_control::CrudOperation;
using sea::application::access_control::EvaluationOptions;
using sea::application::access_control::EvaluationResult;

// ──────────────────────────────────────────────────────────────────
// Helpers strings
// ──────────────────────────────────────────────────────────────────

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

/**
 * Convertit une valeur JSON (potentiellement non-string) en string.
 * Pour les types primitifs uniquement (string, int, double, bool).
 * Les objets/arrays sont sérialisés avec dump().
 */
std::string json_value_to_string(const nlohmann::json& value)
{
    if (value.is_null()) {
        return "";
    }
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (value.is_number_integer()) {
        return std::to_string(value.get<long long>());
    }
    if (value.is_number_float()) {
        return std::to_string(value.get<double>());
    }
    if (value.is_boolean()) {
        return value.get<bool>() ? "true" : "false";
    }
    // Objet/array : on serialise
    return value.dump();
}

} // namespace anonyme

// ──────────────────────────────────────────────────────────────────
// ResourceAuthorizationHelper
// ──────────────────────────────────────────────────────────────────

ResourceAuthorizationHelper::ResourceAuthorizationHelper(
    std::shared_ptr<sea::application::access_control::PolicyEngine> engine,
    const sea::domain::Schema* schema,
    const sea::domain::access_control::AccessControlConfig* config)
    : engine_(std::move(engine))
    , schema_(schema)
    , config_(config)
{
}

// ──────────────────────────────────────────────────────────────────
// build_subject_from_headers
// ──────────────────────────────────────────────────────────────────

PolicySubject ResourceAuthorizationHelper::build_subject_from_headers(
    const seastar::http::request& req) const
{
    PolicySubject subject;

    static constexpr std::string_view prefix = "x-user-";

    for (const auto& [header_key, header_value] : req._headers) {
        const std::string_view key_view(header_key.data(), header_key.size());

        if (!starts_with_ci(key_view, prefix)) {
            continue;
        }

        const std::string suffix(
            header_key.data() + prefix.size(),
            header_key.size() - prefix.size()
            );
        const std::string attr_name = from_header_case(suffix);

        const std::string value(header_value.data(), header_value.size());

        if (attr_name == "id") {
            subject.id = value;
        } else if (attr_name == "email") {
            subject.email = value;
        } else if (attr_name == "role") {
            subject.roles.push_back(value);
        } else {
            subject.attributes[attr_name] = value;
        }
    }

    return subject;
}

// ──────────────────────────────────────────────────────────────────
// build_context
// ──────────────────────────────────────────────────────────────────

PolicyContext ResourceAuthorizationHelper::build_context(
    const seastar::http::request& req,
    const std::string& path) const
{
    PolicyContext ctx;

    ctx.method = std::string(req._method.data(), req._method.size());
    ctx.path = path;

    auto it = req._headers.find("X-Forwarded-For");
    if (it != req._headers.end()) {
        ctx.ip = std::string(it->second.data(), it->second.size());
    }

    ctx.now = std::chrono::system_clock::now();

    return ctx;
}

// ──────────────────────────────────────────────────────────────────
// build_resource_from_json
// ──────────────────────────────────────────────────────────────────

PolicyResource ResourceAuthorizationHelper::build_resource_from_json(
    const std::string& entity_name,
    const nlohmann::json& record) const
{
    PolicyResource resource;
    resource.entity_name = entity_name;

    if (!record.is_object()) {
        return resource;
    }

    // Extract id (champ standard)
    auto id_it = record.find("id");
    if (id_it != record.end() && !id_it->is_null()) {
        resource.id = json_value_to_string(*id_it);
    }

    // Extract tous les autres champs comme attributes
    for (auto it = record.begin(); it != record.end(); ++it) {
        const std::string& key = it.key();
        if (key == "id") {
            continue;  // deja extrait
        }

        // On ignore les champs sensibles qui ne devraient pas etre dans les attributes
        if (key == "password") {
            continue;
        }

        resource.attributes[key] = json_value_to_string(it.value());
    }

    return resource;
}

// ──────────────────────────────────────────────────────────────────
// is_admin_bypass
// ──────────────────────────────────────────────────────────────────

bool ResourceAuthorizationHelper::is_admin_bypass(
    const PolicySubject& subject) const
{
    if (config_ == nullptr) return false;
    if (!config_->default_allow_admin()) return false;

    const auto& admin_role = config_->admin_role();
    if (admin_role.empty()) return false;

    return std::any_of(
        subject.roles.begin(), subject.roles.end(),
        [&admin_role](const std::string& role) {
            return role == admin_role;
        }
        );
}

// ──────────────────────────────────────────────────────────────────
// check_single (pour GetById/Update/Delete)
// ──────────────────────────────────────────────────────────────────

ResourceCheckResult ResourceAuthorizationHelper::check_single(
    const std::string& entity_name,
    CrudOperation operation,
    const PolicySubject& subject,
    const std::string& record_json,
    const PolicyContext& context) const
{
    // Bypass admin
    if (is_admin_bypass(subject)) {
        std::cerr << "[AUTHZ-RES] " << entity_name << "."
                  << crud_op_to_string(operation)
                  << " : ADMIN BYPASS\n";
        return ResourceCheckResult{true, ""};
    }

    // Trouve l'entite
    const auto* entity = schema_->find_entity(entity_name);
    if (entity == nullptr) {
        return ResourceCheckResult{
            false,
            "Entity '" + entity_name + "' not found"
        };
    }

    // Trouve la spec
    const auto* spec = entity->access_control.find_spec(operation);

    if (spec == nullptr) {
        // Pas de regle definie → applique default_policy
        if (config_->default_policy() ==
            sea::domain::access_control::DefaultPolicy::Deny) {
            return ResourceCheckResult{
                false,
                "No access control rule defined for " + entity_name + "." +
                    crud_op_to_string(operation) + " (default_policy=deny)"
            };
        }
        return ResourceCheckResult{true, ""};
    }

    // Si la regle ne necessite PAS la ressource, le check du Module 5
    // a deja decide. Mais par securite, on re-evalue avec subject seul.
    if (!spec->requires_resource()) {
        const auto eval_options = EvaluationOptions::pre_handler();
        const auto result = engine_->evaluate_subject_only(
            spec->condition(),
            subject,
            context,
            eval_options
            );

        if (!result.allowed) {
            return ResourceCheckResult{
                false,
                entity_name + "." + crud_op_to_string(operation) + " : " +
                    result.reason.value_or("subject does not satisfy the policy")
            };
        }

        std::cerr << "[AUTHZ-RES] " << entity_name << "."
                  << crud_op_to_string(operation)
                  << " : SUBJECT-ONLY → ALLOW (re-check)\n";
        return ResourceCheckResult{true, ""};
    }

    // La regle a une partie resource-aware → on charge la ressource
    nlohmann::json record;
    try {
        record = nlohmann::json::parse(record_json);
    } catch (const std::exception& e) {
        return ResourceCheckResult{
            false,
            "Failed to parse resource JSON: " + std::string(e.what())
        };
    }

    const auto resource = build_resource_from_json(entity_name, record);

    // Evaluation complete
    const auto eval_options = EvaluationOptions::production();
    const auto result = engine_->evaluate(
        spec->condition(),
        subject,
        resource,
        context,
        eval_options
        );

    if (!result.allowed) {
        std::cerr << "[AUTHZ-RES] " << entity_name << "."
                  << crud_op_to_string(operation)
                  << " : RESOURCE-AWARE → DENY ("
                  << result.reason.value_or("policy refused")
                  << ")\n";

        return ResourceCheckResult{
            false,
            entity_name + "." + crud_op_to_string(operation) + " : " +
                result.reason.value_or("subject does not satisfy the policy")
        };
    }

    std::cerr << "[AUTHZ-RES] " << entity_name << "."
              << crud_op_to_string(operation)
              << " : RESOURCE-AWARE → ALLOW\n";

    return ResourceCheckResult{true, ""};
}

// ──────────────────────────────────────────────────────────────────
// filter_collection (pour List)
// ──────────────────────────────────────────────────────────────────

std::string ResourceAuthorizationHelper::filter_collection(
    const std::string& entity_name,
    CrudOperation operation,
    const PolicySubject& subject,
    const std::string& records_json,
    const PolicyContext& context) const
{
    // Bypass admin → retourne tout
    if (is_admin_bypass(subject)) {
        std::cerr << "[AUTHZ-RES] " << entity_name << "."
                  << crud_op_to_string(operation)
                  << " : ADMIN BYPASS (no filtering)\n";
        return records_json;
    }

    // Trouve l'entite
    const auto* entity = schema_->find_entity(entity_name);
    if (entity == nullptr) {
        // Si l'entite n'existe pas, on retourne tel quel (cas limite)
        return records_json;
    }

    // Trouve la spec
    const auto* spec = entity->access_control.find_spec(operation);

    // Pas de spec → applique default_policy mais on a deja decide au
    // Module 5 (sinon on serait jamais arrive ici).
    // Si pas de partie resource-aware → pas de filtrage necessaire
    if (spec == nullptr || !spec->requires_resource()) {
        return records_json;
    }

    // Parse le JSON
    nlohmann::json records;
    try {
        records = nlohmann::json::parse(records_json);
    } catch (const std::exception& e) {
        std::cerr << "[AUTHZ-RES] " << entity_name << "."
                  << crud_op_to_string(operation)
                  << " : Failed to parse collection JSON: "
                  << e.what() << "\n";
        return records_json;
    }

    if (!records.is_array()) {
        return records_json;
    }

    // Filtre chaque record
    nlohmann::json filtered = nlohmann::json::array();
    std::size_t kept = 0;
    std::size_t denied = 0;

    const auto eval_options = EvaluationOptions::production();

    for (const auto& record : records) {
        const auto resource = build_resource_from_json(entity_name, record);

        const auto result = engine_->evaluate(
            spec->condition(),
            subject,
            resource,
            context,
            eval_options
            );

        if (result.allowed) {
            filtered.push_back(record);
            ++kept;
        } else {
            ++denied;
        }
    }

    std::cerr << "[AUTHZ-RES] " << entity_name << "."
              << crud_op_to_string(operation)
              << " : FILTER → kept=" << kept
              << " denied=" << denied << "\n";

    return filtered.dump();
}

} // namespace sea::http::handlers::access_control