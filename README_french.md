# SeaDesktop
[![License: AGPL v3](https://img.shields.io/badge/License-AGPL_v3-blue.svg)](https://www.gnu.org/licenses/agpl-3.0)
[![Commercial License Available](https://img.shields.io/badge/Commercial-Available-green.svg)](COMMERCIAL-LICENSE.md)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![Seastar](https://img.shields.io/badge/Seastar-shared--nothing-orange.svg)](https://seastar.io/)
[![Version](https://img.shields.io/badge/version-0.2.0-brightgreen.svg)](./Release_Notes.md)

> **Une plateforme low-code C++ qui transforme un fichier YAML en serveur ultra rapide API REST + GUI desktop, avec sécurité intégrée.**

---

## 🎯 Vision

SeaDesktop génère **automatiquement** depuis un seul fichier YAML :

- 🛢️ Une **base de données** en fonction des entités et de leurs relations
- 🌐 Une **API REST CRUD** complète, performante (Seastar shared-nothing SMP)
- 🖥️ Une **interface graphique** desktop multi-plateforme (Qt 6)
- 🔐 Un système d'**authentification JWT** complet avec cookies HttpOnly, rotation et révocation
- 🛡️ Une **autorisation RBAC + ABAC** déclarative au niveau opération
- 📊 Des **routes relationnelles** auto-générées (HasMany, BelongsTo, M2M)
- 📝 Un **logging structuré** spdlog avec rotation, JSON, et exposition REST
- 📄 Une **documentation OpenAPI** et un explorateur Swagger UI à `/docs`

**Pas de boilerplate. Pas de framework lourd. Juste du C++ moderne et un YAML.**

---

## 🚀 Quick Start

### 1. Définir vos entités dans un YAML

```yaml
project:
  name: SeaDesktopDemo

services:
  - name: Office
    port: 8080

    security:
      authentication:
        type: none   # pas d'auth (mode dev rapide)

    database:
      type: memory

    entities:
      - name: Department
        options:
          enable_crud: true
          public_routes: true
        fields:
          - name: id
            type: uuid
            required: true
            unique: true
          - name: name
            type: string
            required: true
            unique: true
        relations:
          - name: employees
            kind: has_many
            target_entity: Employee
            fk_column: department_id
            on_delete: cascade

      - name: Employee
        options:
          enable_crud: true
          public_routes: true
        fields:
          - name: id
            type: uuid
            required: true
            unique: true
          - name: name
            type: string
            required: true
          - name: email
            type: email
            required: true
            unique: true
          - name: age
            type: int
            required: false
          - name: department_id
            type: uuid
            required: false
        relations:
          - name: department
            kind: belongs_to
            target_entity: Department
            fk_column: department_id
            on_delete: restrict
```

### 2. Lancer le serveur

```bash
./Backend_Seastar
```

### 3. C'est prêt 🎉

Routes automatiquement générées :

```
# CRUD standard
GET    /departments               GET /departments/{id}
POST   /departments               PUT /departments/{id}
DELETE /departments/{id}
GET    /employees                 GET /employees/{id}
POST   /employees                 PUT /employees/{id}
DELETE /employees/{id}

# Routes relationnelles (auto-générées)
GET /departments/{id}/employees
GET /departments_with_employees/{id}                    Parent + children groupés
GET /employees/filter/with_department_name?name=<value> Recherche par parent.name

# Documentation et endpoints système
GET  /openapi.json                                      Specification OpenAPI 3.0
GET  /docs                                              Interface Swagger UI
GET  /health                                            Healthcheck
```

---

## 🆕 Nouveautés v0.2.0

### 🔐 Authentification JWT renforcée

#### Cookies HttpOnly (protection XSS native)

Trois modes de livraison des tokens selon vos clients :

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

- **`body`** : tokens dans le JSON de réponse (API mobile, CLI)
- **`cookie`** : tokens dans des cookies HttpOnly (applications web)
- **`both`** : combinaison des deux pour migrations progressives

#### Token tracking : révocation immédiate + rotation

```yaml
authentication:
  token_tracking:
    enabled: true
    rotation:
      enabled: true        # nouveau refresh à chaque /auth/refresh
    cache:
      enabled: true
      ttl: "5m"
      max_size: 10000
    auto_cleanup:
      enabled: true
      interval: "1h"
      keep_revoked_for: "30d"
```

- Liste blanche des refresh tokens en circulation
- Liste noire des access tokens révoqués (vérification ~200 ns via cache)
- Rotation automatique : détection de réutilisation d'un refresh token volé
- Nettoyage périodique automatique des tokens expirés

#### Routes auth complètes

| Méthode | Route | Description |
|---|---|---|
| `POST` | `/auth/register` | Inscription |
| `POST` | `/auth/login` | Connexion (tokens dans body ou cookies) |
| `POST` | `/auth/refresh` | Renouvellement avec rotation |
| `POST` | `/auth/logout` | Révocation immédiate de l'access token |
| `GET` | `/auth/me` | Informations du compte connecté |

---

### 📝 Logging structuré spdlog

#### Configuration déclarative complète

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

- **7 loggers nommés** configurables indépendamment (`sea.http`, `sea.persistence`, etc.)
- **Sinks multiples** : console + fichier(s) simultanément
- **Rotation par taille et/ou par temps** (`100MB`, `daily`, etc.)
- **Formats text et JSON** (JSON line-delimited pour ingestion Loki/ELK)
- **Mode asynchrone** non-bloquant pour le reactor Seastar
- **Capture des logs internes Seastar** routés vers le logger `seastar`

#### Endpoint REST `/admin/logs`

Un ring buffer mémoire conserve les **10 000 derniers messages**, accessibles depuis SeaUI ou tout client autorisé :

```bash
# Récupération filtrée
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8081/admin/logs?level=warn&logger=sea.http&limit=50"

# Polling incrémental temps réel
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8081/admin/logs?since=12345"
```

Protégé par authentification JWT + rôle administrateur (configurable via `authorization.admin_role`).

---

### 📖 Documentation utilisateur complète

Quatre nouveaux documents de référence, basés exclusivement sur le code source :

| Document | Contenu |
|---|---|
| [`seadesktop_user_guide.md`](./docs_french/seadesktop_user_guide.md) | Guide global : tous les YAML keys, valeurs acceptées, défauts, comportements |
| [`auth.md`](./docs_french/auth.md) | Authentification : tokens, cookies, tracking, rotation |
| [`pagination.md`](./docs_french/pagination.md) | Pagination : 3 modes (page/offset/cursor), exemples |
| [`logging.md`](./docs_french/logging.md) | Logging : niveaux, modules, sinks, rotation, async, /admin/logs |
| [`FILE_FEATURE_USER_GUIDE.md`](./docs_french/FILE_FEATURE_USER_GUIDE.md) | Stockage de fichiers : déclaration, upload, download, partage |

---

## 🔐 Sécurité enterprise-grade

SeaDesktop implémente un système de sécurité complet en plusieurs couches :

```
┌──────────────────────────────────────────────────────────────────────┐
│  Authentification JWT (access + refresh tokens, HS256/RS256/ES256)   │
│  + Livraison flexible : header Authorization OU cookies HttpOnly      │
├──────────────────────────────────────────────────────────────────────┤
│  Token Tracking : allowlist refresh + denylist access                 │
│  + Rotation automatique + cache local + cleanup périodique            │
├──────────────────────────────────────────────────────────────────────┤
│  RBAC (rôles + admin bypass configurable)                             │
├──────────────────────────────────────────────────────────────────────┤
│  ABAC déclaratif par opération :                                      │
│    • own_resource : un user accède à ses propres données              │
│    • same_scope   : un manager opère uniquement dans son périmètre    │
│    • allow_roles  : restriction par liste de rôles                    │
├──────────────────────────────────────────────────────────────────────┤
│  Filtre ABAC silencieux sur listings (records refusés exclus)         │
│  403 immédiat sur GetById/Update/Delete/Create cross-scope            │
├──────────────────────────────────────────────────────────────────────┤
│  CORS, rate limits, en-têtes HTTP de sécurité, limites de payload     │
└──────────────────────────────────────────────────────────────────────┘
```

### Exemple concret

Avec un YAML déclarant un manager du département IT :

| Action | Résultat |
|---|---|
| `GET /employees` | ✅ 200, voit uniquement employees IT (filtre silencieux) |
| `GET /employees/{Bob_IT}` | ✅ 200, accès accordé |
| `GET /employees/{David_HR}` | ❌ 403, cross-département refusé |
| `PUT /employees/{David_HR}` | ❌ 403, **avant** UPDATE SQL |
| `POST /employees {dept: HR}` | ❌ 403, création cross-dept refusée |
| `POST /auth/logout` (alice) | ✅ 200, access token blacklisté immédiatement |
| Requête suivante avec token révoqué | ❌ 401, denylist vérifiée |

---

## 🏗️ Architecture

SeaDesktop suit le **Domain-Driven Design** avec une séparation stricte des couches :

```
SeaDesktop/
│
├── apps/                                # Applications
│   ├── Backend_Seastar/                 # Serveur HTTP Seastar
│   │   └── src/
│   │       ├── http/
│   │       │   ├── handlers/            # CRUD + relations + auth + admin
│   │       │   ├── middlewares/         # Auth, CORS, RateLimit, etc.
│   │       │   ├── routing/             # Enregistrement des routes
│   │       │   └── utils/               # Helpers HTTP (cookies, multipart)
│   │       └── main.cpp                 # Bootstrap
│   │
│   └── SeaUI/                           # GUI Qt6
│       └── src/                         # Interface administrative
│
├── libs/                                # Bibliothèques DDD
│   ├── sea_domain/                      # Couche Domain
│   │   ├── access_control/              # PolicySubject, PolicyResource, etc.
│   │   ├── schema/                      # Entity, Field, Relation
│   │   ├── security_scheme/             # AuthConfig, CookieConfig, TokenTracking
│   │   └── logging/                     # LoggingConfig
│   │
│   ├── sea_application/                 # Couche Application
│   │   ├── access_control/              # PolicyEngine, evaluators
│   │   ├── auth/                        # AuthService, JWT, TokenTracking
│   │   ├── logging/                     # LoggingInitializer, RingBufferSink
│   │   └── ...
│   │
│   └── sea_infrastructure/              # Couche Infrastructure
│       ├── yaml/                        # Parser YAML
│       ├── runtime/                     # CRUD engines
│       └── persistence/                 # MySQL, Memory backends
│
└── SeaDesktopDemo1.yaml                 # Exemple de configuration complet
```

---

## 🔧 Stack technique

| Couche | Technologie |
|---|---|
| Langage | **C++20** |
| HTTP serveur | **Seastar** (shared-nothing SMP, futures/continuations) |
| GUI desktop | **Qt 6.8.3** |
| Build | **CMake** (monorepo) |
| Base de données | **MySQL 8** (via Connector/C++) |
| Auth | **JWT HS256/RS256/ES256** (access + refresh + tracking) |
| Hashing passwords | **bcrypt** |
| Logging | **spdlog 1.14** (header-only via FetchContent) |
| JSON | **nlohmann/json** |
| YAML | **yaml-cpp** |
| OS supportés | Linux (testé Ubuntu 24.04) |

---

## 🎨 Pourquoi Seastar ?

Seastar est un framework C++ pour serveurs **haute performance** :
- **Shared-nothing SMP** : 1 thread par core, pas de mutex
- **Futures/continuations** : I/O asynchrone non-bloquant
- **DPDK** : networking userspace (optionnel)
- **Linux-specific primitives** : aio, epoll, etc.

Utilisé par **ScyllaDB**, **Redpanda**, et d'autres systèmes de niveau industriel.

---

## 📦 Installation (développement)

### Prérequis

```bash
# Ubuntu 24.04
sudo apt install build-essential cmake git \
                 libmysqlcppconn-dev \
                 nlohmann-json3-dev \
                 libyaml-cpp-dev \
                 libssl-dev

# Qt 6.8.3 via Qt Online Installer
# Seastar depuis source : /opt/seastar
```

### Build

```bash
git clone github.com/fredf21/SeaDesktop.git
cd SeaDesktop
mkdir build && cd build
cmake -DCMAKE_PREFIX_PATH=/path/to/Qt6.8.3 ..
cmake --build . --target Backend_Seastar SeaUI -j$(nproc)
```

### Lancement

```bash
# Variable d'environnement requise (ou générée auto si secret: "")
export SEA_DESKTOP_JWT_SECRET="votre-secret-jwt-32-caracteres-minimum"

# Lancer le serveur
./apps/Backend_Seastar/Backend_Seastar --config=./SeaDesktopDemo1.yaml --service_name=CCNBService

# Lancer la GUI (séparément)
./apps/SeaUI/SeaUI
```

---

## 🗺️ Roadmap

### ✅ v0.1.0 - Fondations
- Modèle Domain complet (PolicySubject, PolicyResource, PolicyContext)
- PolicyEngine avec opérateurs et evaluators
- YAML Parser ABAC (`own_resource`, `same_scope`, `allow_roles`)
- JWT avec claims custom (refresh tokens basiques)
- AuthorizationMiddleware (Stratégie C double check)
- ResourceAuthorizationHelper (ABAC resource-aware)
- API CRUD auto-générée + routes relationnelles
- Filtre ABAC silencieux + 403 cross-scope

### ✅ v0.2.0 - JWT Cookies, Token Tracking & Logging (Actuel)
- Cookies HttpOnly avec configuration complète (domain, path, same_site)
- Token tracking : allowlist refresh + denylist access + rotation + cache
- Auto-cleanup périodique des tokens expirés
- Logging structuré spdlog (7 modules nommés)
- Sinks multiples (console + file) avec rotation taille/temps
- Formats text et JSON (line-delimited)
- Endpoint REST `/admin/logs` avec polling incrémental
- Hook Seastar → spdlog pour capture des logs internes
- Documentation utilisateur complète
- Refactorisation appels `std::cerr` → spdlog

### 🌟 v1.0.0 (Vision)
- Production-ready
- Documentation complète
- Hosting Cloud
- Importer et exporter les yaml a partir de interface graphique
- Preference de Langue
- Edition et logs a partir de la barre de menu

### 🌟 v1.1.0
- WebSocket pour notifications real-time
- OAuth2 providers (Google, GitHub)
- Support PostgreSQL
- Support MONGO
- Migrations versionnées (avec rollback)
- Streaming pour gros fichiers
- Stockage de fichiers étendu (multi-backend filesystem/S3)
---

## 🎓 Pour qui ?

- **Startups** qui veulent un backend + GUI en quelques heures
- **Équipes internes** qui ont besoin de tooling admin sécurisé
- **Développeurs C++** qui veulent un boilerplate moderne et performant
- **Architectes** qui valorisent une approche déclarative (Infrastructure-as-Code)

---

## 📚 Documentation

| Document | Contenu |
|---|---|
| [`Release_Notes.md`](./Release_Notes.md) | Changelog détaillé par version |
| [`docs/seadesktop_user_guide.md`](./docs/seadesktop_user_guide.md) | Guide utilisateur complet (référence YAML) |
| [`docs/auth.md`](./docs/auth.md) | Authentification : JWT, cookies, token tracking |
| [`docs/pagination.md`](./docs/pagination.md) | Pagination : page, offset, cursor |
| [`docs/logging.md`](./docs/logging.md) | Logging : modules, sinks, rotation, `/admin/logs` |
| [`docs/FILE_FEATURE_USER_GUIDE.md`](./docs/FILE_FEATURE_USER_GUIDE.md) | Stockage de fichiers |

---

## 🤝 Contribuer

Le projet est en **alpha**. Les contributions sont bienvenues, en particulier sur :
- Tests unitaires et tests d'intégration
- Support PostgreSQL et MongoDB
- Documentation et exemples YAML
- Templates de projets pour cas d'usage typiques

---

## 📜 Licence

### Open Source License (AGPL v3)

The default license is **GNU Affero General Public License v3.0**.
See the [LICENSE](LICENSE) file for the full text.

Under AGPL v3, you can use SeaDesktop freely, **provided that**:
- If you modify the code, you must publish your modifications.
- If you provide SeaDesktop as a network service (SaaS), you must publish the source code of the entire service.
- Any derivative work must also be licensed under AGPL v3.

### Commercial License (paid)

If you want to use SeaDesktop **without the AGPL constraints**, you need a commercial license. This applies if:

- You want to integrate SeaDesktop into a proprietary product.
- You want to provide SeaDesktop as a SaaS without publishing your source code.
- Your organization has a policy against AGPL/GPL software.
- You want premium support, SLA, or enterprise features.

See [COMMERCIAL-LICENSE.md](./COMMERCIAL-LICENSE.md) for details.

---

## 👤 Auteur

**Frédéric** — Architecte & développeur C++

> Si vous êtes recruteur, investisseur, ou collaborateur potentiel, n'hésitez pas à me contacter sur LinkedIn.

---

<p align="center">
  <strong>SeaDesktop v0.2.0</strong> — Du YAML à un produit complet, en quelques minutes.
</p>
