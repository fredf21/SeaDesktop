#include "schema_validator.h"

#include <cctype>
#include <string_view>
#include <unordered_set>

namespace sea::domain {
namespace {

// Découpe "field:direction" en (field, direction) ; retourne nullopt si format incorrect.
struct SortToken {
    std::string field;
    std::string direction;   // "asc" ou "desc"
};

[[nodiscard]] std::optional<SortToken> parse_sort_token(std::string_view raw) {
    const auto colon = raw.find(':');
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }

    SortToken tok;
    tok.field     = std::string(raw.substr(0, colon));
    tok.direction = std::string(raw.substr(colon + 1));

    if (tok.field.empty() || tok.direction.empty()) {
        return std::nullopt;
    }

    // direction insensible à la casse
    std::transform(tok.direction.begin(), tok.direction.end(), tok.direction.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (tok.direction != "asc" && tok.direction != "desc") {
        return std::nullopt;
    }

    return tok;
}

// Découpe une expression multi-tri "f1:asc,f2:desc" en tokens.
// Retourne std::nullopt si l'un des tokens est mal formé.
[[nodiscard]] std::optional<std::vector<SortToken>>
parse_sort_expression(const std::string& expression) {
    std::vector<SortToken> tokens;
    std::string_view sv(expression);

    while (!sv.empty()) {
        const auto comma = sv.find(',');
        std::string_view part = (comma == std::string_view::npos) ? sv : sv.substr(0, comma);

        // trim simple des espaces
        while (!part.empty() && std::isspace(static_cast<unsigned char>(part.front()))) {
            part.remove_prefix(1);
        }
        while (!part.empty() && std::isspace(static_cast<unsigned char>(part.back()))) {
            part.remove_suffix(1);
        }

        if (part.empty()) {
            return std::nullopt;
        }

        auto tok = parse_sort_token(part);
        if (!tok.has_value()) {
            return std::nullopt;
        }
        tokens.push_back(*tok);

        if (comma == std::string_view::npos) {
            break;
        }
        sv.remove_prefix(comma + 1);
    }

    return tokens;
}

} // namespace

std::vector<std::string> SchemaValidator::validate(const Schema& schema) const {
    std::vector<std::string> errors;

    // Un schéma vide n’a pas de sens pour ton MVP
    if (schema.empty()) {
        errors.push_back("The schema does not contain any entity.");
        return errors;
    }

    validate_entities(schema, errors);

    return errors;
}

void SchemaValidator::validate_entities(const Schema& schema,
                                        std::vector<std::string>& errors) const {
    std::unordered_set<std::string> entity_names;

    for (const auto& entity : schema.entities) {
        // Nom d'entité obligatoire
        if (is_blank(entity.name)) {
            errors.push_back("An entity has an empty name.");
            continue;
        }

        // Vérifie un identifiant raisonnable
        if (!is_valid_identifier(entity.name)) {
            errors.push_back("Invalid entity name: '" + entity.name + "'.");
        }

        // Noms d'entités uniques
        if (!entity_names.insert(entity.name).second) {
            errors.push_back("Duplicate entity name: '" + entity.name + "'.");
        }

        validate_entity(entity, schema, errors);
    }
}

void SchemaValidator::validate_entity(const Entity& entity,
                                      const Schema& schema,
                                      std::vector<std::string>& errors) const {
    // Pour le MVP, une entité sans champ est considérée invalide
    if (entity.fields.empty()) {
        errors.push_back("The entity '" + entity.name + "' does not contain any field.");
    }

    validate_fields(entity, errors);
    validate_relations(entity, schema, errors);
}

void SchemaValidator::validate_fields(const Entity& entity,
                                      std::vector<std::string>& errors) const {
    std::unordered_set<std::string> field_names;

    // Détection de collision de storage_path entre champs File de la
    // MÊME entité. Deux champs qui pointent vers le même sous-dossier
    // n'est pas un bug en soi, mais c'est une source de confusion :
    // un même fichier physique pourrait servir deux champs sans qu'on
    // sache lequel "le possède" pour le reference counting.
    // (Une entité différente peut sans problème réutiliser le même path.)
    std::unordered_set<std::string> file_storage_paths;

    for (const auto& field : entity.fields) {
        if (is_blank(field.name)) {
            errors.push_back("The entity '" + entity.name + "' contains an unnamed field.");
            continue;
        }

        if (!is_valid_identifier(field.name)) {
            errors.push_back("Invalid field name '" + field.name +
                             "' in entity '" + entity.name + "'.");
        }

        if (!field_names.insert(field.name).second) {
            errors.push_back("Duplicate field '" + field.name +
                             "' in entity '" + entity.name + "'.");
        }

        // Password ne doit jamais être sérialisable par défaut
        if (field.type == FieldType::Password && field.serializable) {
            errors.push_back("The password field '" + field.name +
                             "' in entity '" + entity.name +
                             "' should not be serializable.");
        }

        // Un champ Password ne devrait pas avoir de valeur par défaut
        if (field.type == FieldType::Password && field.has_default()) {
            errors.push_back("The password field '" + field.name +
                             "' in entity '" + entity.name +
                             "' cannot have a default value.");
        }

        // Ce champ de type Bynary ne devrait pas avoir de valeur par défaut
        if (field.type == FieldType::Binary && field.has_default()) {
            errors.push_back("The field '" + field.name +
                             "' of type binary in entity '" + entity.name +
                             "' cannot have a default value.");
        }


        // max_length doit rester réservé aux String/Text
        if (field.max_length.has_value()) {
            if (field.type != FieldType::String && field.type != FieldType::Text) {
                errors.push_back("The field '" + field.name + "' in entity '" +
                                 entity.name +
                                 "' uses max_length with an incompatible type.");
            }

            if (*field.max_length == 0) {
                errors.push_back("The field '" + field.name + "' in entity '" +
                                 entity.name +
                                 "' cannot have max_length = 0.");
            }
        }

        // min/max cohérents
        if (field.min_value.has_value() && field.max_value.has_value()) {
            // comparaison uniquement si les deux variantes ont le même type
            if (field.min_value->index() == field.max_value->index()) {
                if (std::holds_alternative<int64_t>(*field.min_value)) {
                    const auto min_v = std::get<int64_t>(*field.min_value);
                    const auto max_v = std::get<int64_t>(*field.max_value);

                    if (min_v > max_v) {
                        errors.push_back("The field '" + field.name + "' in entity '" +
                                         entity.name +
                                         "' a min_value > max_value.");
                    }
                } else if (std::holds_alternative<double>(*field.min_value)) {
                    const auto min_v = std::get<double>(*field.min_value);
                    const auto max_v = std::get<double>(*field.max_value);

                    if (min_v > max_v) {
                        errors.push_back("The field '" + field.name + "' in entity '" +
                                         entity.name +
                                         "' a min_value > max_value.");
                    }
                }
            }
        }

        // ── Champs de type File : règles dédiées ───────────────────
        // On délègue à validate_file_field tous les checks spécifiques.
        // La détection de collision de storage_path reste ici car elle
        // doit voir l'ensemble des champs de l'entité.
        if (field.type == FieldType::File) {
            validate_file_field(entity, field, errors);

            // Collision de storage_path dans la même entité
            if (field.file_config.has_value() &&
                !field.file_config->storage_path.empty()) {
                const auto& sp = field.file_config->storage_path;
                if (!file_storage_paths.insert(sp).second) {
                    errors.push_back("The file field '" + field.name +
                                     "' in entity '" + entity.name +
                                     "' partage son storage_path '" + sp +
                                     "' with another file field in the same entity "
                                     "(source of confusion for reference counting).");
                }
            }
        }
    }
}

void SchemaValidator::validate_file_field(const Entity& entity,
                                          const Field& field,
                                          std::vector<std::string>& errors) const {
    // Limite "soft" au-delà de laquelle on émet un warning : un fichier
    // de plus de 10 GiB suggère presque toujours un typo (`50GB` au lieu
    // de `50MB`). On n'interdit pas, on alerte.
    static constexpr std::size_t kSoftMaxSizeWarn = 10ULL * 1024 * 1024 * 1024;

    const std::string ctx = "The file field '" + field.name +
                            "' in entity '" + entity.name + "'";

    // ── 1. Cohérence type/config ─────────────────────────────
    // Le parser garantit déjà ce check pour les schémas chargés depuis
    // YAML, mais un Field construit en C++ directement (style fluide)
    // peut omettre la config. Filet de sécurité avant que le codegen
    // ou les handlers ne segfault.
    if (!field.file_config.has_value()) {
        errors.push_back(ctx + " has no file_config (required for type=file).");
        return;   // Aucun autre check ne peut s'exécuter sans config
    }

    const auto& cfg = *field.file_config;

    // ── 2. Contraintes incompatibles ─────────────────────────

    // unique : interdit. Un fichier physique peut légitimement être
    // référencé par plusieurs entités (c'est même la raison d'être du
    // reference_count). Forcer l'unicité va à l'encontre du design.
    if (field.unique) {
        errors.push_back(ctx + " cannot be 'unique' "
                               "(files can be shared between entities "
                               "via reference_count).");
    }

    // indexed : interdit. La colonne est un FK BINARY(16) vers sea_files
    // dont l'usage est de retrouver le fichier à partir de l'entité, pas
    // l'inverse. Indexer cette colonne n'a aucun intérêt en lecture.
    if (field.indexed) {
        errors.push_back(ctx + " cannot be 'indexed' "
                               "(the FK to sea_files has no search use case).");
    }

    // max_length : non applicable (pas une string textuelle).
    if (field.max_length.has_value()) {
        errors.push_back(ctx + " cannot have 'max_length' "
                               "(not applicable to a file field; use file.max_size).");
    }

    // min_value / max_value : non applicables non plus.
    if (field.min_value.has_value() || field.max_value.has_value()) {
        errors.push_back(ctx + " cannot have 'min_value' or 'max_value' "
                               "(not applicable to a file field).");
    }

    // ── 3. storage_path ──────────────────────────────────────

    // storage_path obligatoire en validation (le parser le laisse passer
    // vide pour permettre des cas d'usage avancés en C++ direct, mais en
    // pratique on veut toujours un sous-dossier dédié).
    if (cfg.storage_path.empty()) {
        errors.push_back(ctx + " must define a storage_path "
                               "(otherwise all files go to the storage root).");
    } else {
        const auto& sp = cfg.storage_path;

        // Pas de chemin absolu.
        if (sp.front() == '/') {
            errors.push_back(ctx + " has an absolute storage_path '" + sp +
                             "' (must be relative to the storage root).");
        }

        // Pas de segment '..' (path traversal).
        // Décompose le chemin et vérifie chaque segment.
        std::size_t start = 0;
        bool empty_segment = false;
        for (std::size_t i = 0; i <= sp.size(); ++i) {
            const bool is_sep = (i == sp.size()) ||
                                sp[i] == '/';
            if (is_sep) {
                const std::string segment = sp.substr(start, i - start);

                if (segment == "..") {
                    errors.push_back(ctx + " has a storage_path containing '..': '" + sp +
                                     "' (path traversal is forbidden).");
                }
                if (segment.empty() && i != sp.size()) {
                    // Tolère un trailing slash mais pas un '//' interne.
                    empty_segment = true;
                }
                start = i + 1;
            }
        }
        if (empty_segment) {
            errors.push_back(ctx + " has a storage_path with an empty segment '" + sp +
                             "' (consecutive separators are forbidden).");
        }

        // Pas de caractères de contrôle dans le path.
        for (char c : sp) {
            if (std::iscntrl(static_cast<unsigned char>(c))) {
                errors.push_back(ctx + " has a storage_path containing a control character.");
                break;
            }
        }
    }

    // ── 4. max_size_bytes ────────────────────────────────────

    // Si la valeur est définie, elle doit être > 0.
    // (Le parser rejette déjà 0, mais protège l'API directe en C++.)
    if (cfg.max_size_bytes.has_value() && *cfg.max_size_bytes == 0) {
        errors.push_back(ctx + " has max_size_bytes = 0 "
                               "(zero size = no upload accepted, not useful).");
    }

    // Warning soft sur taille déraisonnable. On ajoute quand même
    // dans `errors` car le validator n'a pas de canal "warning" séparé ;
    // le préfixe rend la nature de l'alerte visible.
    if (cfg.max_size_bytes.has_value() && *cfg.max_size_bytes > kSoftMaxSizeWarn) {
        errors.push_back("[warning] " + ctx + " has max_size > 10 GiB ("
                         + std::to_string(*cfg.max_size_bytes) +
                         " bytes): verify that this is not a typo.");
    }
}

void SchemaValidator::validate_relations(const Entity& entity,
                                         const Schema& schema,
                                         std::vector<std::string>& errors) const {
    std::unordered_set<std::string> relation_names;

    for (const auto& relation : entity.relations) {
        if (is_blank(relation.name)) {
            errors.push_back("The entity '" + entity.name + "' contains an unnamed relation.");
            continue;
        }

        if (!relation_names.insert(relation.name).second) {
            errors.push_back("Duplicate relation '" + relation.name +
                             "' in entity '" + entity.name + "'.");
        }

        if (is_blank(relation.target_entity)) {
            errors.push_back("The relation '" + relation.name + "' in entity '" +
                             entity.name + "' has no target_entity.");
            continue;
        }

        // La cible doit exister dans le schéma
        if (!schema.has_entity(relation.target_entity)) {
            errors.push_back("The relation '" + relation.name + "' in entity '" +
                             entity.name + "' targets an unknown entity: '" +
                             relation.target_entity + "'.");
        }

        // ManyToMany doit avoir une table pivot si elle est renseignée explicitement
        if (relation.kind == RelationKind::ManyToMany) {
            if (is_blank(relation.pivot_table)) {
                errors.push_back(
                    "The relation many_to_many '" + relation.name +
                    "' in entity '" + entity.name +
                    "' must define a pivot_table."
                    );
            }
        }

        // BelongsTo a souvent besoin d'une fk locale
        if (relation.kind == RelationKind::BelongsTo) {
            if (is_blank(relation.fk_column)) {
                errors.push_back(
                    "The relation belongs_to '" + relation.name +
                    "' in entity '" + entity.name +
                    "' must define an fk_column."
                    );
            }
        }
    }
}
void SchemaValidator::validate_pagination(const Entity& entity,
                                          std::vector<std::string>& errors) const {
    if (!entity.pagination.has_value()) {
        return;   // pas de bloc pagination -> rien à valider
    }

    const auto& cfg = *entity.pagination;

    // Au moins un mode doit être actif
    if (!cfg.any()) {
        errors.push_back("The 'pagination' block of entity '" + entity.name +
                         "' does not enable any mode (page, offset, or cursor).");
        return;
    }

    // ── Mode page-based ─────────────────────────────────────────
    if (cfg.has_page()) {
        const auto& p = *cfg.page;
        const std::string ctx = "pagination.page of entity '" + entity.name + "'";

        if (p.default_page_size == 0) {
            errors.push_back("The " + ctx + " has default_page_size = 0.");
        }
        if (p.max_page_size == 0) {
            errors.push_back("The " + ctx + " has max_page_size = 0.");
        }
        if (p.default_page_size > p.max_page_size) {
            errors.push_back("The " + ctx + " has default_page_size > max_page_size.");
        }

        // sortable_fields référencent des champs réels
        for (const auto& fname : p.sortable_fields) {
            if (!entity.has_field(fname)) {
                errors.push_back("The " + ctx + " lists an unknown sortable_field: '" + fname + "'.");
            }
        }

        // default_sort doit être valide et n'utiliser que des sortable_fields
        if (p.default_sort.has_value()) {
            if (auto err = validate_sort_expression(
                    *p.default_sort, p.sortable_fields, entity, ctx)) {
                errors.push_back(*err);
            }
        }
    }

    // ── Mode offset/limit ───────────────────────────────────────
    if (cfg.has_offset()) {
        const auto& o = *cfg.offset;
        const std::string ctx = "pagination.offset of entity '" + entity.name + "'";

        if (o.default_limit == 0) {
            errors.push_back("The " + ctx + " has default_limit = 0.");
        }
        if (o.max_limit == 0) {
            errors.push_back("The " + ctx + " has max_limit = 0.");
        }
        if (o.default_limit > o.max_limit) {
            errors.push_back("The " + ctx + " has default_limit > max_limit.");
        }

        for (const auto& fname : o.sortable_fields) {
            if (!entity.has_field(fname)) {
                errors.push_back("The " + ctx + " lists an unknown sortable_field: '" + fname + "'.");
            }
        }

        if (o.default_sort.has_value()) {
            if (auto err = validate_sort_expression(
                    *o.default_sort, o.sortable_fields, entity, ctx)) {
                errors.push_back(*err);
            }
        }
    }

    // ── Mode cursor ─────────────────────────────────────────────
    if (cfg.has_cursor()) {
        const auto& c = *cfg.cursor;
        const std::string ctx = "pagination.cursor of entity '" + entity.name + "'";

        if (c.default_limit == 0) {
            errors.push_back("The " + ctx + " has default_limit = 0.");
        }
        if (c.max_limit == 0) {
            errors.push_back("The " + ctx + " has max_limit = 0.");
        }
        if (c.default_limit > c.max_limit) {
            errors.push_back("The " + ctx + " has default_limit > max_limit.");
        }

        if (c.cursor_field.empty()) {
            errors.push_back("The " + ctx + " has no cursor_field.");
        } else if (!entity.has_field(c.cursor_field)) {
            errors.push_back("The " + ctx + " references an unknown cursor_field: '" +
                             c.cursor_field + "'.");
        }

        if (c.sort.empty()) {
            errors.push_back("The " + ctx + " has no 'sort' (required for stability).");
        } else {
            // Le sort cursor n'est PAS limité aux sortable_fields (c'est un tri figé,
            // imposé par le YAML). On vérifie seulement le format et l'existence du champ.
            const auto tokens = parse_sort_expression(c.sort);
            if (!tokens.has_value()) {
                errors.push_back("The " + ctx + " has a malformed 'sort': '" + c.sort +
                                 "' (expected: 'field:asc' or 'field:desc').");
            } else {
                for (const auto& t : *tokens) {
                    if (!entity.has_field(t.field)) {
                        errors.push_back("The " + ctx + " sorts on an unknown field: '" +
                                         t.field + "'.");
                    }
                }
            }
        }
    }
}

std::optional<std::string>
SchemaValidator::validate_sort_expression(const std::string& expression,
                                          const std::vector<std::string>& allowed,
                                          const Entity& entity,
                                          const std::string& context) const {
    const auto tokens = parse_sort_expression(expression);
    if (!tokens.has_value()) {
        return "The " + context + " has a malformed default_sort: '" + expression +
               "' (expected: 'field:asc' or 'field:desc'[,'field:asc']*).";
    }

    for (const auto& t : *tokens) {
        // Le champ doit exister dans l'entité
        if (!entity.has_field(t.field)) {
            return "The " + context + " sorts on an unknown field: '" + t.field + "'.";
        }
        // Et il doit être dans la whitelist sortable_fields
        const bool whitelisted =
            std::find(allowed.begin(), allowed.end(), t.field) != allowed.end();
        if (!whitelisted) {
            return "The " + context + " sorts on field '" + t.field +
                   "' that is not in sortable_fields.";
        }
    }

    return std::nullopt;
}

bool SchemaValidator::is_blank(std::string_view value) const noexcept {
    return value.empty();
}

bool SchemaValidator::is_valid_identifier(std::string_view value) const noexcept {
    if (value.empty()) {
        return false;
    }

    const unsigned char first = static_cast<unsigned char>(value.front());

    // Premier caractère : lettre ou underscore
    if (!(std::isalpha(first) || first == '_')) {
        return false;
    }

    // Le reste : lettre, chiffre ou underscore
    for (char c : value) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (!(std::isalnum(uc) || uc == '_')) {
            return false;
        }
    }

    return true;
}

} // namespace sea::domain