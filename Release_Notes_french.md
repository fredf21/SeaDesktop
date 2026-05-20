# SeaDesktop Release Notes

## v0.2.0 - JWT Cookies, Token Tracking & Structured Logging (2026-05-15)

🎯 **Authentification JWT renforcée avec livraison par cookies et suivi
centralisé des tokens, plus système de logging structuré complet basé sur
spdlog.**

Cette release apporte un système d'authentification production-ready
(cookies HttpOnly, révocation immédiate, rotation des refresh tokens) et un
système de logs structuré utilisable depuis SeaUI via une API REST dédiée.
Elle s'accompagne d'une documentation utilisateur complète couvrant
l'ensemble des fonctionnalités déclaratives YAML de SeaDesktop.

---

### ✨ Added

#### Authentification JWT renforcée

**Livraison des tokens par cookies (`token_delivery`)**

Nouveau champ dans le YAML pour choisir comment les tokens sont transmis :

```yaml
security:
  authentication:
    token_delivery: cookie   # body | cookie | both
    cookie:
      domain: ".example.com"
      path: "/"
      secure: true
      same_site: lax
      access_token_name: sea_access
      refresh_token_name: sea_refresh
```

- Mode `body` : tokens dans le JSON de réponse (API mobile, CLI)
- Mode `cookie` : tokens dans des cookies HttpOnly (protection XSS native)
- Mode `both` : combinaison des deux pour migrations progressives

L'attribut `HttpOnly` est toujours `true` (non configurable, sécurité).

**Token tracking : révocation et rotation**

Système complet de suivi centralisé des tokens JWT :

```yaml
authentication:
  token_tracking:
    enabled: true
    refresh_table: RefreshToken
    revoked_table: RevokedToken
    cache:
      enabled: true
      ttl: "5m"
      max_size: 10000
    rotation:
      enabled: true
    auto_cleanup:
      enabled: true
      interval: "1h"
      keep_revoked_for: "30d"
```

- **Liste blanche refresh tokens** : chaque refresh émis est enregistré en
  base. Un refresh inconnu est rejeté.
- **Liste noire access tokens** : `/auth/logout` ajoute l'access token à
  la liste noire pour une révocation immédiate.
- **Rotation automatique** : chaque appel à `/auth/refresh` invalide
  l'ancien refresh et en émet un nouveau (détection de réutilisation).
- **Cache local** : vérification de la liste noire en ~200 ns au lieu
  d'une requête DB par appel.
- **Nettoyage automatique** : suppression périodique des tokens expirés.

#### Nouveaux handlers HTTP

| Handler | Route | Description |
|---|---|---|
| `RefreshHandler` | `POST /auth/refresh` | Renouvelle l'access token. Lit le refresh depuis le body ou le cookie. Émet de nouveaux tokens avec rotation. |
| `LogoutHandler` | `POST /auth/logout` | Révoque les tokens en cours. Insère dans la denylist, supprime de l'allowlist, efface les cookies. |

#### Login enrichi

Le `LoginHandler` existant a été étendu pour :

- Enregistrer le refresh token dans la liste blanche au login
- Émettre les tokens selon le mode `token_delivery` configuré
- Inclure les claims custom dans l'access token

#### ProtectedHandler avec fallback cookie

`ProtectedHandler` lit maintenant le token dans l'ordre :
1. Header HTTP `Authorization: Bearer <token>`
2. Cookie portant le nom configuré dans `cookie.access_token_name`

Un même service accepte ainsi simultanément les clients utilisant l'un ou
l'autre mode.

#### Tables système auto-gérées

Lorsque `token_tracking.enabled: true`, deux tables système sont créées
automatiquement au démarrage :

- `RefreshToken` : liste blanche des refresh tokens valides
- `RevokedToken` : liste noire des access tokens explicitement révoqués

Ces tables sont entièrement gérées par le système et n'ont pas à être
déclarées manuellement dans le YAML.

---

#### Logging structuré avec spdlog

**Configuration YAML complète du logging**

```yaml
logging:
  level: info
  modules:
    sea.http: debug
    sea.persistence: info
    seastar: warn
  sinks:
    - type: console
      format: text
      enabled: true
    - type: file
      format: json
      enabled: true
      path: "./logs/service.log"
      rotation:
        max_size: "100MB"
        time_pattern: daily
        max_files: 30
  flush_level: error
  async:
    enabled: true
    queue_size: 8192
    overflow_policy: overrun_oldest
```

#### Sept loggers nommés

Le système expose sept modules de logging configurables indépendamment :

| Module | Contenu |
|---|---|
| `sea.boot` | Démarrage, migrations, initialisations |
| `sea.http` | Handlers HTTP, autorisation, routes |
| `sea.application` | Services applicatifs |
| `sea.persistence` | Requêtes base, seeds, schéma |
| `sea.runtime` | Validation, sérialisation |
| `sea.security` | Authentification, tokens, cleanup |
| `seastar` | Logs internes du framework réseau |

Chaque module peut avoir son propre niveau via `modules:` dans le YAML.

#### Deux types de sinks

- **`console`** : écriture sur stderr avec couleurs ANSI
- **`file`** : écriture dans un fichier avec rotation (taille, temps, ou les deux)

Plusieurs sinks peuvent coexister : chaque message est envoyé à tous les
sinks actifs simultanément.

#### Deux formats de sortie

- **`text`** : lignes lisibles, idéal pour développement
  ```
  [2026-05-14 10:23:45.123] [sea.http] [info] login successful: alice@example.com
  ```

- **`json`** : JSON line-delimited, idéal pour ingestion (Loki, ELK, Datadog)
  ```json
  {"timestamp":"2026-05-14T10:23:45.123Z","logger":"sea.http","level":"info","message":"login successful"}
  ```

Échappement RFC 8259 conforme via `nlohmann/json` pour garantir la validité
des sorties JSON même avec des caractères spéciaux dans les messages.

#### Rotation des fichiers

- Par taille : `max_size: "100MB"` (formats `KB`, `MB`, `GB` acceptés)
- Par temps : `time_pattern: hourly` ou `daily`
- Combinaison : la rotation se déclenche au premier critère atteint
- Conservation configurable : `max_files: 10`

#### Logging asynchrone

```yaml
async:
  enabled: true
  queue_size: 8192
  overflow_policy: overrun_oldest   # block | overrun_oldest
```

Écriture des logs déchargée sur un thread dédié pour ne pas bloquer le
reactor Seastar. Deux politiques de débordement :

- **`overrun_oldest`** (défaut) : écrase les messages anciens, jamais de
  blocage du service
- **`block`** : attend la libération d'une place, aucune perte de messages

#### Hook Seastar → spdlog

Les logs internes du framework Seastar sont automatiquement capturés et
routés vers le logger `seastar` via un `std::streambuf` custom avec
bufferisation thread-local. Aucune perte de logs lors de la transition.

#### Endpoint `/admin/logs` avec ring buffer mémoire

Un buffer mémoire FIFO conserve en permanence les **10 000 derniers
messages**, indépendamment des sinks configurés. Exposé via deux endpoints
REST protégés (authentification JWT + rôle admin) :

```
GET /admin/logs                    # consultation avec filtrage
GET /admin/logs/loggers            # liste des modules disponibles
```

Filtres supportés :

| Paramètre | Description |
|---|---|
| `limit` | Nombre max d'entrées (défaut 100, max 1000) |
| `level` | Filtre par niveau minimum (trace/debug/info/warn/error/critical) |
| `logger` | Filtre exact par nom de module |
| `since` | Polling incrémental via `sequence_id` |
| `search` | Recherche insensible à la casse dans le message |

Pattern de polling incrémental pour suivi temps réel :

```bash
# Première requête
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8081/admin/logs?limit=100"
# next_sequence_id: 12345

# Suivantes : ne récupèrent que les nouveaux
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8081/admin/logs?since=12345"
```

#### Configuration de l'admin role

Le rôle administrateur permettant d'accéder à `/admin/logs` est entièrement
configurable via le YAML :

```yaml
authorization:
  admin_role: "administrateur"   # ou "superuser", "root", etc.
```

#### Documentation utilisateur complète

Quatre documents de référence rédigés et basés exclusivement sur le code
source vérifié :

| Document | Contenu |
|---|---|
| `seadesktop_user_guide.md` | Guide utilisateur global : tous les YAML keys, valeurs acceptées, défauts, comportements |
| `auth.md` | Authentification JWT : tokens, cookies, tracking, rotation |
| `pagination.md` | Pagination : 3 modes (page/offset/cursor), sortable_fields, exemples |
| `logging.md` | Logging : niveaux, modules, sinks, rotation, async, /admin/logs |

Chaque document suit le standard d'exhaustivité de `FILE_FEATURE_USER_GUIDE.md` :
tableaux exhaustifs des clés YAML, sections "Comportement attendu", exemples
concrets de configurations par cas d'usage.

---

### 🔧 Changed

#### Refactorisation massive des logs `std::cerr` → spdlog

Environ **124 appels à `std::cerr`** ont été remplacés par des appels
spdlog avec le module et le niveau appropriés dans :

- `apps/Backend_Seastar/src/main.cpp` (bootstrap)
- Tous les handlers HTTP (`src/http/handlers/`)
- Tous les middlewares (`src/http/middleware/`)
- Couche persistance MySQL (bootstrapper, repository, introspector)
- `schema_differ`
- `seed_orchestrator`

Les préfixes `[BOOT]` historiques ont été supprimés (redondants avec le nom
du module logger).

#### YAML demo complet

Le fichier `SeaDesktopDemo1.yaml` a été enrichi pour illustrer toutes les
nouvelles fonctionnalités : section `logging:` complète avec tous les sinks
et la rotation, blocs `cookie:` et `token_tracking:` activés.

---

### 🐛 Fixed

#### Génération UUID dans les seeds many-to-many

Bug dans `MySQLGenericRepository::insert_pivot()` : les UUIDs étaient
insérés en tant que chaînes brutes dans des colonnes `BINARY(16)`, causant
des erreurs `Incorrect string value`.

**Fix** : détection heuristique des valeurs UUID et wrapping automatique
avec `UUID_TO_BIN(?, 1)` lors de l'insertion dans la table pivot.

#### Ordre d'include critique pour `seastar_log_bridge.cpp`

Bug subtil : l'include `<seastar/util/log.hh>` DOIT précéder
`<spdlog/spdlog.h>`. L'ordre inverse provoque une erreur de compilation
cryptique "templates can only be declared in namespace or class scope" sur
les builds release.

**Fix** : documenté et appliqué dans tous les fichiers concernés.

---

### 📊 Tests end-to-end validés

```
✅ POST /auth/login (mode body)              → tokens dans JSON
✅ POST /auth/login (mode cookie)            → cookies HttpOnly émis
✅ POST /auth/refresh                        → rotation effective (ancien révoqué)
✅ POST /auth/logout                         → access token blacklisté immédiatement
✅ GET /protected avec token révoqué         → 401 (vérification denylist + cache)
✅ GET /admin/logs sans auth                 → 401
✅ GET /admin/logs avec user normal          → 403
✅ GET /admin/logs avec admin                → 200, logs filtrables
✅ GET /admin/logs?since=N polling           → uniquement les nouveaux
✅ Logs Seastar reactor visibles dans logger seastar
✅ Rotation fichier déclenchée à 100MB       → archive créée, max_files respecté
✅ Mode async overrun_oldest                 → service jamais bloqué en charge
```

---

### 📁 Nouveaux fichiers

```
NEW   libs/sea_domain/security_scheme/
      ├── cookie_config.{h,cpp}
      ├── token_tracking_config.{h,cpp}
      └── (extension de authentification_config)

NEW   libs/sea_domain/logging/
      └── logging_config.{h,cpp}

NEW   libs/sea_application/security/
      ├── denylist_cache.{h,cpp}
      └── token_tracking_service.{h,cpp}

NEW   libs/sea_application/logging/
      ├── logging_initializer.{h,cpp}
      ├── seastar_log_bridge.{h,cpp}
      └── ring_buffer_sink.{h,cpp}

NEW   apps/Backend_Seastar/src/http/handlers/auth/
      ├── refresh_handler.{h,cpp}
      └── logout_handler.{h,cpp}

NEW   apps/Backend_Seastar/src/http/handlers/admin/
      └── logs_handler.{h,cpp}

NEW   docs/
      ├── seadesktop_user_guide.md
      ├── auth.md
      ├── pagination.md
      └── logging.md
```

### 📁 Fichiers modifiés

```
MOD   libs/infrastructure/yaml/yaml_schema_parser.{h,cpp}
      (+ parse_cookie_config, parse_token_tracking_config,
         parse_logging_node, parse_sink_node, parse_rotation_node,
         parse_async_node, helpers parse_duration et parse_size)

MOD   apps/Backend_Seastar/src/http/handlers/auth/
      ├── login_handler.{h,cpp}      (+ token tracking, cookies)
      └── protected_handler.{h,cpp}  (+ fallback cookie + denylist check)

MOD   apps/Backend_Seastar/src/http/routing/route_registration.{h,cpp}
      (+ refresh, logout, /admin/logs routes)

MOD   apps/Backend_Seastar/src/main.cpp
      (+ LoggingInitializer setup, hook Seastar, token tracking wiring,
        cleanup timer pour auto_cleanup)

MOD   libs/infrastructure/persistence/mysql/
      ├── mysql_bootstrapper.{h,cpp}     (+ tables système RefreshToken/RevokedToken)
      ├── mysql_generic_repository.cpp   (fix UUID_TO_BIN dans insert_pivot)
      └── seed_orchestrator.cpp          (+ logs spdlog)

MOD   ~124 fichiers du backend : std::cerr → spdlog::get(...)->info/warn/error(...)

MOD   SeaDesktopDemo1.yaml
      (+ section logging complète, blocs cookie et token_tracking)
```

---

### 📈 Statistiques

```
Lignes de code ajoutées       : ~3 200
Lignes de code refactorisées  : ~1 800 (passages std::cerr → spdlog)
Nouveaux fichiers             : 14 (code + tests + docs)
Nouvelles routes              : 4 (/auth/refresh, /auth/logout,
                                   /admin/logs, /admin/logs/loggers)
Nouvelles tables système      : 2 (RefreshToken, RevokedToken)
Modules de logging            : 7 (configurables indépendamment)
Documentation utilisateur     : ~5 100 lignes (4 documents)
Bugs critiques fixés          : 2
Tests end-to-end validés      : 12+
```

---

### 🔄 Migration depuis v0.1.0

#### Configurations existantes : 100 % compatibles

Toutes les nouvelles sections (`cookie:`, `token_tracking:`, `logging:`)
sont **optionnelles**. Une configuration v0.1.0 fonctionne sans modification
en v0.2.0 :

- `token_delivery` par défaut : `body` (comportement v0.1.0 préservé)
- `token_tracking.enabled` par défaut : `false` (comportement stateless préservé)
- Section `logging:` absente : défauts appliqués (console texte info async)

#### Activation progressive recommandée

```yaml
# Étape 1 : activer le logging structuré
logging:
  level: info
  sinks:
    - type: console
      format: text
      enabled: true
    - type: file
      format: json
      enabled: true
      path: "./logs/service.log"

# Étape 2 : activer les cookies pour les clients web
authentication:
  token_delivery: both    # tokens dans le body ET dans les cookies
  cookie:
    secure: true
    same_site: lax

# Étape 3 : activer le token tracking pour la révocation
authentication:
  token_tracking:
    enabled: true
    rotation:
      enabled: true
```

---

## Versions précédentes

### v0.1.0 - Fondations : Domain, ABAC, Authentification JWT

Première release stable de SeaDesktop, regroupant l'ensemble des fondations
de la plateforme : modélisation déclarative, persistance MySQL, génération
automatique de routes CRUD et système d'autorisation ABAC complet.

#### ABAC resource-aware

- `ResourceAuthorizationHelper` centralisé évaluant les règles ABAC qui
  nécessitent la ressource chargée depuis la DB
- Intégration dans 9 handlers CRUD et relationnels :
  `ListHandler`, `GetByIdHandler`, `CreateHandler`, `UpdateHandler`,
  `DeleteHandler`, `GetOneByFkHandler`, `GetWithChildrenHandler`,
  `ListByFkHandler`, `ListByFkFieldHandler`, `ListManyToManyHandler`
- 2 nouvelles routes auto-générées par relation HasMany :
  - `GET /<parent>s_with_<children>/{id}`
  - `GET /<children>/filter/with_<parent>_name/{value}`
- Configuration `abac_mode` (permissive/strict) au niveau service et entité
- 3 bugs critiques fixés (UUID MySQL, ordre routes, parser jamais appelé)

#### AuthorizationMiddleware

- Pipeline middleware étendu avec `AuthorizationMiddleware`
- `RouteAuthorizationResolver` : 8+ patterns de routes reconnus
- Stratégie C : double check parent + child sur routes relationnelles
- Logs `[AUTHZ]` exhaustifs

#### JWT avec claims custom

- `entity_id` injecté dans le JWT au login
- Headers `X-User-*` propagés depuis ProtectedHandler
- Refresh tokens (première implémentation, sans tracking ni rotation)

#### YAML Parser ABAC

- Parsing des `access_control` blocks
- Support `own_resource`, `same_scope`, `allow_roles`
- `abac_mode` configurable

#### Domain & PolicyEngine

- Domain types : `PolicySubject`, `PolicyResource`, `PolicyContext`
- `PolicyEngine` avec stratégies (subject-only, full evaluation)
- Operators evaluator
