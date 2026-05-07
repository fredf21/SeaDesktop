# SeaDesktop Release Notes

## v0.5.0 - Module 6 : ABAC resource-aware (2026-05-06)

🎯 **Module 6 complet : autorisation ABAC fine basée sur la ressource**

Cette release apporte le dernier morceau du système d'autorisation enterprise-grade
de SeaDesktop : le filtrage et le contrôle d'accès basé sur les **attributs de la
ressource** (et non plus seulement sur le rôle du user).

---

### ✨ Added

#### `ResourceAuthorizationHelper` (Module 6)
- Nouveau helper centralisé : `apps/Backend_Seastar/src/http/handlers/access_control/`
- Évalue les règles ABAC qui nécessitent la ressource chargée depuis la DB
- 2 méthodes principales :
  - `check_single()` : pour GetById/Update/Delete (retourne 403 si refus)
  - `filter_collection()` : pour List (filtre silencieux des records refusés)
- Logs détaillés `[AUTHZ-RES]` distincts du Module 5 (`[AUTHZ]`)

#### Intégration dans 9 handlers CRUD + relationnels

Tous modifiés pour accepter le helper en paramètre optionnel :

| Handler | Type de check |
|---|---|
| `ListHandler` | `filter_collection` (filtre silencieux) |
| `GetByIdHandler` | `check_single` (403 si refus) |
| `CreateHandler` | `check_single` sur le payload (avant INSERT SQL) |
| `UpdateHandler` | `check_single` sur la ressource actuelle (avant UPDATE SQL) |
| `DeleteHandler` | `check_single` sur la ressource actuelle (avant DELETE SQL) |
| `GetOneByFkHandler` | `check_single` |
| `GetWithChildrenHandler` | `check_single` (parent) + `filter_collection` (children) |
| `ListByFkHandler` | `filter_collection` |
| `ListByFkFieldHandler` | `filter_collection` |
| `ListManyToManyHandler` | `filter_collection` |

#### 2 nouvelles routes auto-générées par relation HasMany

Pour chaque relation `HasMany` (ex: `Department → Employee`), le serveur enregistre
maintenant **3 routes** au lieu d'1 :

```
GET /<parent>s/{id}/<children>                          (existante)
GET /<parent>s_with_<children>/{id}                     (NOUVEAU)
GET /<children>/filter/with_<parent>_name/{value}       (NOUVEAU, si parent a un champ "name")
```

Exemples concrets :
```
GET /departments/{id}/employees
GET /departments_with_employees/{id}
GET /employees/filter/with_department_name/IT
```

#### Configuration `abac_mode` au niveau service et entité

Nouveau champ dans le YAML pour contrôler le comportement ABAC :

```yaml
authorization:
  abac_mode: permissive   # service-level

# OU au niveau entité (override)
- name: StudentProgram
  access_control:
    abac_mode: strict     # override pour cette entité
```

- `permissive` (défaut) : laisse passer les règles resource-aware au handler/Module 6
- `strict` : refuse 403 immédiat les règles resource-aware au middleware

---

### 🐛 Fixed

#### Génération UUID pour MySQL dans `CreateHandler`
Bug pré-existant (CRITIQUE) : la génération d'UUID était limitée à
`db_type == Memory`, ce qui faisait planter MySQL avec
`Incorrect string value` car le record n'avait pas d'`id`.

**Fix** : la génération s'applique maintenant à **tous** les types de DB.

#### Ordre d'enregistrement des routes (`main.cpp`)
Bug : les routes spécifiques (`/<entity>s/filter/...`) étaient enregistrées
**après** les routes génériques (`/<entity>s/{id}`). Seastar matchait alors
`filter` comme un id, retournant `404 "Enregistrement introuvable"`.

**Fix** : les routes relationnelles sont maintenant enregistrées **avant** les
routes item (`{id}`). Principe : *routes spécifiques avant routes catch-all*.

#### Parser YAML : `parse_entity_access_control_node()` jamais appelé
Bug critique du Module 3 : le parser définissait
`parse_entity_access_control_node()` mais ne l'appelait jamais depuis
`parse_entity_node()`. Résultat : toutes les `EntityAccessControl` étaient
vides → `find_spec()` retournait `nullptr` → `default_policy=deny` →
**403 universel sur toutes les routes**.

**Fix** : appel ajouté dans `parse_entity_node()` avec passage du
`global_config` pour les overrides.

---

### 🔧 Changed

#### `MiddlewareContext` étendu

```cpp
struct MiddlewareContext {
    // ... champs existants ...
    
    std::shared_ptr<sea::application::access_control::PolicyEngine> policy_engine;
    
    // ✨ NOUVEAU
    std::shared_ptr<
        sea::http::handlers::access_control::ResourceAuthorizationHelper
    > resource_auth_helper;
};
```

#### `register_has_many_routes()` étendu

La fonction enregistre maintenant 3 routes par relation HasMany au lieu d'1
(voir section "Added").

---

### 📊 Tests end-to-end validés

Tous les tests passent en mode production (MySQL + Seastar) :

```
✅ Manager IT  GET  /employees                       → 200, [Alice IT, Bob IT]
✅ Manager IT  GET  /employees/{David_HR}            → 403 (cross-dept refused)
✅ Manager IT  PUT  /employees/{David_HR}            → 403 (avant UPDATE SQL)
✅ Manager IT  POST /employees {dept: HR}            → 403 (cross-dept create blocked)
✅ Manager IT  GET  /departments/{HR}/employees      → 200, [] (filtre silencieux)
✅ Manager IT  GET  /departments_with_employees/{HR} → 200, {employees: []}
✅ Manager IT  GET  /employees/filter/with_department_name/Human%20Resources → []
✅ User        GET  /users/{son_id}                  → 200 (own_resource OK)
✅ User        GET  /users/{autre_id}                → 403 (own_resource refused)
✅ Admin       GET  /employees                       → 200, 4 employees (bypass)
```

---

### 📁 Fichiers modifiés

```
NEW    apps/Backend_Seastar/src/http/handlers/access_control/
       ├── resource_authorization_helper.h
       └── resource_authorization_helper.cpp

MOD    apps/Backend_Seastar/src/http/handlers/crud_handlers/
       ├── list_handler.{h,cpp}
       ├── get_by_id_handler.{h,cpp}
       ├── create_handler.{h,cpp}     (+ fix UUID)
       ├── update_handler.{h,cpp}
       └── delete_handler.{h,cpp}

MOD    apps/Backend_Seastar/src/http/handlers/relation_handlers/
       ├── get_one_by_fk_handler.{h,cpp}
       ├── get_with_children_handler.{h,cpp}
       ├── list_by_fk_handler.{h,cpp}
       ├── list_by_fk_field_handler.{h,cpp}
       └── list_many_to_many_handler.{h,cpp}

MOD    apps/Backend_Seastar/src/http/routing/
       ├── route_registration.h       (+ resource_auth_helper)
       └── route_registration.cpp     (+ helper passé aux 9 handlers,
                                        + 2 nouvelles routes auto-générées)

MOD    apps/Backend_Seastar/src/main.cpp
       (+ création du helper + ordre routes corrigé)

MOD    libs/infrastructure/yaml/yaml_schema_parser.cpp
       (+ appel à parse_entity_access_control_node)
```

---

### 📈 Statistiques

```
Lignes de code ajoutées       : ~600
Lignes de code modifiées      : ~250
Handlers modifiés             : 9
Routes auto-générées (par HasMany) : 3 (au lieu de 1)
Bugs critiques fixés          : 3
Tests end-to-end validés      : 10+
```

---

## Versions précédentes

### v0.4.0 - Module 5 : AuthorizationMiddleware (Stratégie C)
- Pipeline middleware étendu avec `AuthorizationMiddleware`
- `RouteAuthorizationResolver` : 8+ patterns de routes reconnus
- Stratégie C : double check parent + child sur routes relationnelles
- Logs `[AUTHZ]` exhaustifs

### v0.3.0 - Module 4 : JWT avec claims custom
- `department_id` injecté dans le JWT au login
- Headers `X-User-*` propagés depuis ProtectedHandler
- Refresh tokens

### v0.2.0 - Module 3 : YAML Parser ABAC
- Parsing des `access_control` blocks
- Support `own_resource`, `same_scope`, `allow_roles`, etc.
- `abac_mode` configurable

### v0.1.0 - Modules 1-2 : Domain & PolicyEngine
- Domain types : `PolicySubject`, `PolicyResource`, `PolicyContext`
- `PolicyEngine` avec stratégies (subject-only, full evaluation)
- Operators evaluator
