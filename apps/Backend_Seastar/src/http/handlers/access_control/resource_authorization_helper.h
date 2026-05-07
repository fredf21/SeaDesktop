#ifndef SEA_HTTP_HANDLERS_RESOURCE_AUTHORIZATION_HELPER_H
#define SEA_HTTP_HANDLERS_RESOURCE_AUTHORIZATION_HELPER_H

#include "access_control/access_control_config.h"
#include "access_control/policy_engine.h"
#include "access_control/policy_subject.h"
#include "access_control/policy_resource.h"
#include "access_control/policy_context.h"
#include "access_control/policy_engine.h"
#include "schema.h"

#include <nlohmann/json_fwd.hpp>
#include <seastar/core/sstring.hh>
#include <seastar/http/request.hh>

#include <nlohmann/json.hpp>

#include <memory>
#include <string>

namespace sea::http::handlers::access_control {

/**
 * Resultat d'un check de ressource (Module 6).
 *
 * - allowed : true si l'acces est autorise
 * - reason  : message d'erreur si refus (vide si allowed=true)
 */
struct ResourceCheckResult {
    bool allowed;
    std::string reason;
};

/**
 * ResourceAuthorizationHelper (Module 6 - ABAC resource-aware post-handler)
 *
 * Helper centralise utilise par les handlers CRUD pour evaluer les regles
 * ABAC qui necessitent la ressource chargee depuis la DB.
 *
 * Pipeline complet :
 *   1. Module 4 (ProtectedHandler) : verifie JWT, injecte X-User-*
 *   2. Module 5 (AuthorizationMiddleware) : check RBAC subject-only
 *      → si resource-aware, laisse passer en mode permissive
 *   3. Module 6 (ce helper) :
 *      a. Le handler charge la ressource depuis MySQL
 *      b. Appel a check_single() ou filter_collection()
 *      c. Le helper construit PolicySubject + PolicyResource
 *      d. Appelle PolicyEngine.evaluate() COMPLET
 *      e. Retourne allowed/refuse pour 403, ou filtre les records pour List
 *
 * Strategie 2 (standard industrie) :
 *   - GetById/Update/Delete : 403 si la regle refuse
 *   - List : filtre silencieux (les records refuses sont retires)
 */
class ResourceAuthorizationHelper {
public:
    ResourceAuthorizationHelper(
        std::shared_ptr<sea::application::access_control::PolicyEngine> engine,
        const sea::domain::Schema* schema,
        const sea::domain::access_control::AccessControlConfig* config
        );

    /**
     * Construit un PolicySubject depuis les headers HTTP X-User-*.
     *
     * Utilise par les handlers pour passer le subject au helper.
     * Reutilise la meme logique que le Module 5 pour la coherence.
     */
    [[nodiscard]] sea::domain::access_control::PolicySubject
    build_subject_from_headers(const seastar::http::request& req) const;

    /**
     * Construit un PolicyContext minimal depuis la requete HTTP.
     */
    [[nodiscard]] sea::domain::access_control::PolicyContext
    build_context(
        const seastar::http::request& req,
        const std::string& path
        ) const;

    /**
     * Verifie l'acces a une ressource specifique (GetById, Update, Delete).
     *
     * Comportement :
     * - Si bypass admin → allowed=true immediat
     * - Si pas de spec definie → applique default_policy
     * - Sinon evalue la condition complete (subject + resource)
     *
     * @param entity_name Le nom de l'entite (ex: "Employee")
     * @param operation L'operation CRUD a verifier
     * @param subject Le subject (depuis les headers)
     * @param record_json Le record JSON (depuis record_to_json)
     * @param context Le contexte de la requete
     */
    [[nodiscard]] ResourceCheckResult check_single(
        const std::string& entity_name,
        sea::domain::access_control::CrudOperation operation,
        const sea::domain::access_control::PolicySubject& subject,
        const std::string& record_json,
        const sea::domain::access_control::PolicyContext& context
        ) const;

    /**
     * Filtre une collection de records selon la regle d'acces (List).
     *
     * Comportement :
     * - Si bypass admin → retourne tous les records
     * - Si pas de spec ou spec sans condition resource-aware → retourne tous
     * - Sinon : pour chaque record, evalue la regle. Garde si allowed.
     *
     * @return Le JSON filtre (peut etre un array vide si tout est refuse)
     */
    [[nodiscard]] std::string filter_collection(
        const std::string& entity_name,
        sea::domain::access_control::CrudOperation operation,
        const sea::domain::access_control::PolicySubject& subject,
        const std::string& records_json,
        const sea::domain::access_control::PolicyContext& context
        ) const;

private:
    std::shared_ptr<sea::application::access_control::PolicyEngine> engine_;
    const sea::domain::Schema* schema_;
    const sea::domain::access_control::AccessControlConfig* config_;

    /**
     * Construit un PolicyResource depuis un objet JSON.
     *
     * Mapping :
     *   - record["id"] → resource.id
     *   - autres champs → resource.attributes (convertis en string)
     */
    [[nodiscard]] sea::domain::access_control::PolicyResource
    build_resource_from_json(
        const std::string& entity_name,
        const nlohmann::json& record
        ) const;

    /**
     * Verifie si le subject est admin (pour bypass).
     */
    [[nodiscard]] bool is_admin_bypass(
        const sea::domain::access_control::PolicySubject& subject
        ) const;
};

} // namespace sea::http::handlers::access_control

#endif // SEA_HTTP_HANDLERS_RESOURCE_AUTHORIZATION_HELPER_H