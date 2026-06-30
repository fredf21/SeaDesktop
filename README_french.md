# SeaDesktop
[![License: AGPL v3](https://img.shields.io/badge/License-AGPL_v3-blue.svg)](https://www.gnu.org/licenses/agpl-3.0)
[![Commercial License Available](https://img.shields.io/badge/Commercial-Available-green.svg)](COMMERCIAL-LICENSE.md)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![Seastar](https://img.shields.io/badge/Seastar-shared--nothing-orange.svg)](https://seastar.io/)
[![Version](https://img.shields.io/badge/version-1.0.1-brightgreen.svg)](./Release_Notes.md)

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

**Option A — En local (développement)** :

```bash
./Backend_Seastar --config configs/SeaDesktopDemo.yaml --service_name Office
```

**Option B — Docker (recommandé pour tester la stack complète)** :

```bash
cp .env.example .env
# Éditer .env pour définir SEA_DESKTOP_JWT_SECRET et MYSQL_ROOT_PASSWORD
docker compose up -d
```

La stack Docker inclut MySQL et un service backend d'exemple exposé sur le port 8080. Voir [`docs_french/docker_deployment_fr.md`](./docs_french/docker_deployment_fr.md) pour le déploiement de production, la configuration multi-services et la résolution de problèmes.

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

## 🆕 Nouveautés v1.0.1

### 🚀 Configuration guidée au premier lancement Local

Quand SeaUI est lancé pour la première fois en mode Local, un dialog
**Bienvenue dans SeaUI** guide maintenant l'utilisateur à travers
trois sections de configuration en une étape :

- **Dossier de configuration** — choix d'où vivront les fichiers YAML de projet (avec option de copier un projet d'exemple BlogDemo)
- **Identifiants MySQL** — hôte, port, utilisateur, mot de passe (avec afficher/masquer)
- **Secret JWT** — secret 256 bits auto-généré avec bouton Régénérer

Les identifiants sont stockés dans un fichier `seadesktop.env` dans
un dossier voisin `environment/`, séparé de `configs/`. Cette
séparation rend sûr le versionnage de `configs/` dans Git tout en
gardant les secrets en local.

### 📊 Visualiseur de données d'entité haute performance

L'action **Open Data** sur chaque entité ouvre maintenant un
visualiseur de données à rendu paresseux qui passe à l'échelle
sur des dizaines de milliers de lignes. Seules les lignes visibles
à l'écran sont dessinées ; le scroll reste fluide sur les grandes
tables. Le visualiseur détecte aussi la pagination configurée dans
le YAML et adapte sa stratégie de fetch : OFFSET (avec totaux exacts),
CURSOR (basé sur token), ou PAGE (concaténation de pages), avec un
scroll infini déclenchant le batch suivant à 85 % de la barre de
défilement.

### 🔒 Durcissement des routes système quand l'auth est activée

Cinq routes système (`/health`, `/health/ready`, `/openapi.json`,
`/docs`, `/assets/swagger-ui/*`) deviennent maintenant **réservées
aux administrateurs** quand l'authentification est activée sur le
service. En mode développement (auth désactivée), elles restent
publiques pour l'exploration sans authentification. Cela empêche
les visiteurs anonymes d'énumérer la surface de l'API via Swagger
UI en production.

### 🛠 Améliorations qualité de vie côté backend

- Le nom de service par défaut codé en dur `CCNBService` est supprimé.
  `--service_name` est maintenant optionnel ; le backend sélectionne
  le premier service déclaré dans le YAML.
- `--config` est maintenant `required()` pour un message d'erreur
  clair en cas d'oubli.
- SeaUI résout le binaire backend en trois niveaux de priorité
  (env override → `/usr/bin/seadesktop-backend` en mode `.deb` →
  chemin relatif dev).
- Les variables du `seadesktop.env` sont injectées à la fois dans le
  processus SeaUI lui-même (pour que le parsing YAML les trouve) et
  dans le QProcessEnvironment du backend.

Voir [`Release_Notes_french.md`](./Release_Notes_french.md) pour le
changelog v1.0.1 complet et les notes de migration.

---

## 🆕 Nouveautés v1.0.0

### 🌐 Administration à distance (remote-first)

Les services SeaDesktop peuvent désormais être administrés à distance par HTTP. SeaUI en mode Remote se connecte à un backend déployé (typiquement dans Docker) et gère les projets sans accès au système de fichiers :

- **Endpoints `/admin/projects/*`** — CRUD complet sur les fichiers YAML (lister, lire, créer, modifier, supprimer) via REST
- **Endpoint `/admin/restart`** — redémarrage propre du service, relancé automatiquement par l'orchestrateur de conteneurs
- **Endpoint `/admin/logs`** — buffer mémoire de logs accessible depuis le visualiseur de journaux distants de SeaUI
- **Profils SeaUI** — bascule entre mode Local (filesystem direct) et mode Remote (HTTP) par profil, avec un gestionnaire de profils intégré

Voir [`docs_french/admin_fr.md`](./docs_french/admin_fr.md) pour la référence des endpoints et [`docs_french/SEAUI_GUIDE_fr.md`](./docs_french/SEAUI_GUIDE_fr.md) pour le guide utilisateur SeaUI.

### 🐳 Déploiement Docker

SeaDesktop est maintenant livré avec un déploiement Docker complet :

- **Dockerfile multi-stage** qui compile Seastar depuis les sources (commit épinglé) et le backend en couches optimisées
- **docker-compose.yml** orchestrant MySQL + N services backend avec un volume `configs/` partagé
- **docker-compose.prod.yml** en override pour les secrets Docker en production
- **Architecture multi-services** — chaque conteneur sert un projet ; tous les conteneurs voient les mêmes fichiers YAML via un volume partagé

Voir [`docs_french/docker_deployment_fr.md`](./docs_french/docker_deployment_fr.md) pour le guide complet.

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

## 💻 Support des plateformes

SeaDesktop a deux composants — un backend basé sur Seastar et un client desktop Qt (SeaUI) — avec des contraintes de plateforme différentes.

| Composant | Linux (x86_64) | macOS | Windows |
|---|---|---|---|
| `backend_seastar` natif | ✅ Supporté | ❌ Seastar nécessite Linux | ❌ Seastar nécessite Linux |
| `backend_seastar` via Docker | ✅ | ✅ Docker Desktop | ✅ Docker Desktop / WSL2 |
| SeaUI (application desktop Qt 6) | ✅ Natif | ✅ Natif | ✅ Natif |

### Configuration recommandée par plateforme

- **Linux** — Backend natif et SeaUI natif. Les modes Local et Remote sont tous deux disponibles. Docker est optionnel mais utile pour les déploiements multi-services.
- **macOS** — Backend dans Docker Desktop, SeaUI natif. **Le mode Remote est requis** : le mode Local a besoin du binaire `backend_seastar` que Seastar ne supporte pas en dehors de Linux. Connectez SeaUI à `http://localhost:8080` (Docker redirige le port vers la VM Linux).
- **Windows** — Idem macOS : backend dans Docker Desktop (typiquement avec le backend WSL2), SeaUI natif, **mode Remote requis**.

Pourquoi Seastar est Linux-only : Seastar s'appuie sur des appels système spécifiques à Linux (`io_uring`, `epoll`, pinning fin du CPU) qui ne sont pas disponibles nativement sur macOS ou Windows. L'approche Docker exécute le backend dans une VM Linux, de manière transparente pour l'utilisateur.

Voir [`docs_french/docker_deployment_fr.md`](./docs_french/docker_deployment_fr.md) pour la configuration Docker complète et [`docs_french/SEAUI_GUIDE_fr.md`](./docs_french/SEAUI_GUIDE_fr.md) pour le workflow en mode Remote.

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

### ✅ v0.2.0 - JWT Cookies, Token Tracking & Logging
- Cookies HttpOnly avec configuration complète (domain, path, same_site)
- Token tracking : allowlist refresh + denylist access + rotation + cache
- Auto-cleanup périodique des tokens expirés
- Logging structuré spdlog (7 modules nommés)
- Sinks multiples (console + file) avec rotation taille/temps
- Endpoint REST `/admin/logs` avec polling incrémental
- Hook Seastar → spdlog pour capture des logs internes
- Documentation utilisateur complète

### ✅ v1.0.0 - Administration à distance et Docker (actuel)
- Endpoints `/admin/projects/*` pour le CRUD YAML complet en HTTP
- Endpoint `/admin/restart` pour le redémarrage propre du service
- Profils SeaUI : modes Local et Remote, UI de gestion de profils
- HttpProjectRepository : SeaUI lit/écrit le backend par HTTP
- RemoteLogsViewer : visualiseur intégré pour les logs distants
- Dockerfile multi-stage (Seastar depuis les sources, image runtime ~150 Mo)
- Orchestration docker-compose pour multi-services + MySQL
- docker-compose.prod.yml avec support des secrets Docker
- Durcissement production des endpoints `/admin/*` (JWT + rôle admin)
- Documentation bilingue complète (EN + FR) de chaque fonctionnalité
- Procédure de bootstrap admin documentée

### ✅ v1.0.1 - Configuration au premier lancement Local, viseur paresseux et durcissement routes système (actuel)
- LocalSetupDialog au premier lancement Local (dossier configs + MySQL + JWT)
- EnvFileLoader : `.env` injecté à la fois dans le processus SeaUI et le QProcess backend
- EntityDataDialog : rendu paresseux avec pagination conditionnelle (OFFSET > CURSOR > PAGE > None)
- AdminGuardHandler : 5 routes système admin-only quand l'auth est activée
- Suppression du `CCNBService` codé en dur ; `--service_name` maintenant optionnel
- Résolution du binaire backend en 3 niveaux de priorité (env, wrapper `.deb`, relatif dev)
- Documentation mise à jour (Release Notes, installation, guide SEAUI) en EN et FR

### 🌟 v1.1.0
- WebSocket pour notifications temps réel
- OAuth2 providers (Google, GitHub)
- Support PostgreSQL
- Support MongoDB
- Migrations versionnées (avec rollback)
- Streaming pour gros fichiers
- Stockage de fichiers étendu (multi-backend filesystem/S3)
- Daemon orchestrateur pour auto-déployer les nouveaux projets en conteneurs
- Durcissement : restreindre `role: admin` dans `/auth/register` aux admins existants
- RemoteLogsViewer : polling temps réel, filtres par niveau/logger, recherche
---

## 🎓 Pour qui ?

- **Startups** qui veulent un backend + GUI en quelques heures
- **Équipes internes** qui ont besoin de tooling admin sécurisé
- **Développeurs C++** qui veulent un boilerplate moderne et performant
- **Architectes** qui valorisent une approche déclarative (Infrastructure-as-Code)

---

## 📚 Documentation

### Français

| Document | Contenu |
|---|---|
| [`Release_Notes_french.md`](./Release_Notes_french.md) | Journal détaillé des versions |
| [`docs_french/seadesktop_user_guide_fr.md`](./docs_french/seadesktop_user_guide_fr.md) | Guide utilisateur complet (référence YAML) |
| [`docs_french/admin_fr.md`](./docs_french/admin_fr.md) | Endpoints d'administration |
| [`docs_french/auth_fr.md`](./docs_french/auth_fr.md) | Authentification : JWT, cookies, suivi de tokens |
| [`docs_french/docker_deployment_fr.md`](./docs_french/docker_deployment_fr.md) | Déploiement Docker, multi-services, production |
| [`docs_french/errors_fr.md`](./docs_french/errors_fr.md) | Format et codes des réponses d'erreur |
| [`docs_french/FILE_FEATURE_USER_GUIDE_fr.md`](./docs_french/FILE_FEATURE_USER_GUIDE_fr.md) | Stockage de fichiers |
| [`docs_french/healthcheck_fr.md`](./docs_french/healthcheck_fr.md) | Endpoints `/health` et `/health/ready` |
| [`docs_french/logging_fr.md`](./docs_french/logging_fr.md) | Journalisation : modules, sinks, rotation, `/admin/logs` |
| [`docs_french/pagination_fr.md`](./docs_french/pagination_fr.md) | Pagination : page, offset, cursor |
| [`docs_french/SEAUI_GUIDE_fr.md`](./docs_french/SEAUI_GUIDE_fr.md) | Application desktop SeaUI (modes Local et Remote) |

### English

| Document | Contents |
|---|---|
| [`Release_Notes.md`](./Release_Notes.md) | Detailed changelog by version |
| [`docs/seadesktop_user_guide.md`](./docs/seadesktop_user_guide.md) | Complete user guide (YAML reference) |
| [`docs/admin.md`](./docs/admin.md) | Administration endpoints |
| [`docs/auth.md`](./docs/auth.md) | Authentication: JWT, cookies, token tracking |
| [`docs/docker_deployment.md`](./docs/docker_deployment.md) | Docker deployment, multi-services, production |
| [`docs/errors.md`](./docs/errors.md) | Error response format and codes |
| [`docs/FILE_FEATURE_USER_GUIDE.md`](./docs/FILE_FEATURE_USER_GUIDE.md) | File storage |
| [`docs/healthcheck.md`](./docs/healthcheck.md) | `/health` and `/health/ready` endpoints |
| [`docs/logging.md`](./docs/logging.md) | Logging: modules, sinks, rotation, `/admin/logs` |
| [`docs/pagination.md`](./docs/pagination.md) | Pagination: page, offset, cursor |
| [`docs/SEAUI_GUIDE.md`](./docs/SEAUI_GUIDE.md) | SeaUI desktop application (Local and Remote modes) |

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
  <strong>SeaDesktop v1.0.1</strong> — Du YAML à un produit complet, en quelques minutes.
</p>
