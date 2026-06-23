# SeaDesktop
[![License: AGPL v3](https://img.shields.io/badge/License-AGPL_v3-blue.svg)](https://www.gnu.org/licenses/agpl-3.0)
[![Commercial License Available](https://img.shields.io/badge/Commercial-Available-green.svg)](COMMERCIAL-LICENSE.md)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![Seastar](https://img.shields.io/badge/Seastar-shared--nothing-orange.svg)](https://seastar.io/)
[![Version](https://img.shields.io/badge/version-1.0.0-brightgreen.svg)](./Release_Notes.md)

> **A low-code C++ platform that turns a YAML file into an ultra-fast REST API server + desktop GUI, with built-in security.**

---

## 🎯 Vision

SeaDesktop automatically generates from a single YAML file:

- 🛢️ A **database** based on entities and their relationships
- 🌐 A complete, high-performance **CRUD REST API** (Seastar shared-nothing SMP)
- 🖥️ A cross-platform desktop **graphical interface** (Qt 6)
- 🔐 A complete **JWT authentication** system with HttpOnly cookies, rotation, and revocation
- 🛡️ Declarative **RBAC + ABAC authorization** at the operation level
- 📊 Auto-generated **relationship routes** (HasMany, BelongsTo, M2M)
- 📝 **Structured logging** with spdlog, rotation, JSON, and REST exposure
- 📄 **OpenAPI documentation** and a Swagger UI explorer at `/docs`

**No boilerplate. No heavy framework. Just modern C++ and YAML.**

---

## 🚀 Quick Start

### 1. Define your entities in YAML

```yaml
project:
  name: SeaDesktopDemo

services:
  - name: Office
    port: 8080

    security:
      authentication:
        type: none   # no auth (quick dev mode)

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

### 2. Start the server

**Option A — Locally (development)**:

```bash
./Backend_Seastar --config configs/SeaDesktopDemo.yaml --service_name Office
```

**Option B — Docker (recommended for testing the full stack)**:

```bash
cp .env.example .env
# Edit .env to set SEA_DESKTOP_JWT_SECRET and MYSQL_ROOT_PASSWORD
docker compose up -d
```

The Docker stack includes MySQL and one example backend service exposed on port 8080. See [`docs/docker_deployment.md`](./docs/docker_deployment.md) for production deployment, multi-service setup, and troubleshooting.

### 3. It is ready 🎉

Automatically generated routes:

```
# Standard CRUD
GET    /departments               GET /departments/{id}
POST   /departments               PUT /departments/{id}
DELETE /departments/{id}
GET    /employees                 GET /employees/{id}
POST   /employees                 PUT /employees/{id}
DELETE /employees/{id}

# Relationship routes (auto-generated)
GET /departments/{id}/employees
GET /departments_with_employees/{id}                    Parent + grouped children
GET /employees/filter/with_department_name?name=<value> Search by parent.name

# Documentation and system endpoints
GET  /openapi.json                                      OpenAPI 3.0 specification
GET  /docs                                              Swagger UI interface
GET  /health                                            Healthcheck
```

---

## 🆕 What's New in v1.0.0

### 🌐 Remote-first administration

SeaDesktop services can now be administered remotely over HTTP. SeaUI in Remote mode connects to a deployed backend (typically running in Docker) and manages projects without filesystem access:

- **`/admin/projects/*` endpoints** — full CRUD on YAML project files (list, read, create, update, delete) via REST
- **`/admin/restart` endpoint** — graceful service restart, automatically respawned by the container orchestrator
- **`/admin/logs` endpoint** — in-memory log buffer accessible from SeaUI's Remote Logs Viewer
- **SeaUI profiles** — switch between Local mode (filesystem direct) and Remote mode (HTTP-based) per profile, with a built-in profile manager

See [`docs/admin.md`](./docs/admin.md) for the endpoint reference and [`docs/SEAUI_GUIDE.md`](./docs/SEAUI_GUIDE.md) for the SeaUI user guide.

### 🐳 Docker deployment

SeaDesktop now ships with a complete Docker-based deployment:

- **Multi-stage Dockerfile** building Seastar from source (pinned commit) and the backend in optimized layers
- **docker-compose.yml** orchestrating MySQL + N backend services with a shared `configs/` volume
- **docker-compose.prod.yml** override for Docker secrets in production
- **Multi-service architecture** — each container serves one project; all containers see the same YAML files via shared volume

See [`docs/docker_deployment.md`](./docs/docker_deployment.md) for the full deployment guide.

### 🔐 Enhanced JWT Authentication

#### HttpOnly cookies (native XSS protection)

Three token delivery modes depending on your clients:

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

- **`body`**: tokens in the JSON response (mobile API, CLI)
- **`cookie`**: tokens in HttpOnly cookies (web applications)
- **`both`**: combination of both for progressive migrations

#### Token tracking: immediate revocation + rotation

```yaml
authentication:
  token_tracking:
    enabled: true
    rotation:
      enabled: true        # new refresh token on every /auth/refresh
    cache:
      enabled: true
      ttl: "5m"
      max_size: 10000
    auto_cleanup:
      enabled: true
      interval: "1h"
      keep_revoked_for: "30d"
```

- Allowlist of active refresh tokens
- Denylist of revoked access tokens (approximately 200 ns check via cache)
- Automatic rotation: detection of stolen refresh token reuse
- Automatic periodic cleanup of expired tokens

#### Complete auth routes

| Method | Route | Description |
|---|---|---|
| `POST` | `/auth/register` | Registration |
| `POST` | `/auth/login` | Login (tokens in body or cookies) |
| `POST` | `/auth/refresh` | Renewal with rotation |
| `POST` | `/auth/logout` | Immediate access token revocation |
| `GET` | `/auth/me` | Connected account information |

---

### 📝 Structured Logging with spdlog

#### Complete declarative configuration

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

- **7 named loggers** configurable independently (`sea.http`, `sea.persistence`, etc.)
- **Multiple sinks**: console + file(s) simultaneously
- **Size-based and/or time-based rotation** (`100MB`, `daily`, etc.)
- **Text and JSON formats** (line-delimited JSON for Loki/ELK ingestion)
- Non-blocking **asynchronous mode** for the Seastar reactor
- **Capture of internal Seastar logs** routed to the `seastar` logger

#### REST endpoint `/admin/logs`

An in-memory ring buffer keeps the **last 10,000 messages**, accessible from SeaUI or any authorized client:

```bash
# Filtered retrieval
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8081/admin/logs?level=warn&logger=sea.http&limit=50"

# Real-time incremental polling
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8081/admin/logs?since=12345"
```

Protected by JWT authentication + administrator role (configurable through `authorization.admin_role`).

---

### 📖 Complete User Documentation

Four new reference documents, based exclusively on the source code:

| Document | Contents |
|---|---|
| [`seadesktop_user_guide.md`](./docs/seadesktop_user_guide.md) | Global guide: all YAML keys, accepted values, defaults, behavior |
| [`auth.md`](./docs/auth.md) | Authentication: tokens, cookies, tracking, rotation |
| [`pagination.md`](./docs/pagination.md) | Pagination: 3 modes (page/offset/cursor), examples |
| [`logging.md`](./docs/logging.md) | Logging: levels, modules, sinks, rotation, async, /admin/logs |
| [`FILE_FEATURE_USER_GUIDE.md`](./docs/FILE_FEATURE_USER_GUIDE.md) | File storage: declaration, upload, download, sharing |

---

## 💻 Platform Support

SeaDesktop has two components — a Seastar-based backend and a Qt-based desktop client (SeaUI) — with different platform constraints.

| Component | Linux (x86_64) | macOS | Windows |
|---|---|---|---|
| `backend_seastar` native | ✅ Supported | ❌ Seastar requires Linux | ❌ Seastar requires Linux |
| `backend_seastar` via Docker | ✅ | ✅ Docker Desktop | ✅ Docker Desktop / WSL2 |
| SeaUI (Qt 6 desktop app) | ✅ Native | ✅ Native | ✅ Native |

### Recommended setup per platform

- **Linux** — Native backend and native SeaUI. Both Local and Remote modes are available. Docker is optional but useful for multi-service setups.
- **macOS** — Backend in Docker Desktop, SeaUI native. **Remote mode is required**: Local mode needs the `backend_seastar` binary which Seastar does not support outside Linux. Connect SeaUI to `http://localhost:8080` (Docker forwards the port to the Linux VM).
- **Windows** — Same as macOS: backend in Docker Desktop (typically with the WSL2 backend), SeaUI native, **Remote mode required**.

Why Seastar is Linux-only: Seastar relies on Linux-specific syscalls (`io_uring`, `epoll`, fine-grained CPU pinning) that are not available natively on macOS or Windows. The Docker approach runs the backend in a Linux VM, transparently from the user's perspective.

See [`docs/docker_deployment.md`](./docs/docker_deployment.md) for the full Docker setup and [`docs/SEAUI_GUIDE.md`](./docs/SEAUI_GUIDE.md) for the Remote mode workflow.

---

## 🔐 Enterprise-grade Security

SeaDesktop implements a complete multi-layer security system:

```
┌──────────────────────────────────────────────────────────────────────┐
│  JWT authentication (access + refresh tokens, HS256/RS256/ES256)     │
│  + Flexible delivery: Authorization header OR HttpOnly cookies        │
├──────────────────────────────────────────────────────────────────────┤
│  Token Tracking: refresh allowlist + access denylist                  │
│  + Automatic rotation + local cache + periodic cleanup                │
├──────────────────────────────────────────────────────────────────────┤
│  RBAC (roles + configurable admin bypass)                             │
├──────────────────────────────────────────────────────────────────────┤
│  Declarative ABAC by operation:                                      │
│    • own_resource: a user accesses their own data                     │
│    • same_scope: a manager operates only within their scope           │
│    • allow_roles: restriction by role list                            │
├──────────────────────────────────────────────────────────────────────┤
│  Silent ABAC filter on listings (denied records excluded)             │
│  Immediate 403 on cross-scope GetById/Update/Delete/Create            │
├──────────────────────────────────────────────────────────────────────┤
│  CORS, rate limits, HTTP security headers, payload limits             │
└──────────────────────────────────────────────────────────────────────┘
```

### Concrete Example

With YAML declaring a manager from the IT department:

| Action | Result |
|---|---|
| `GET /employees` | ✅ 200, sees only IT employees (silent filter) |
| `GET /employees/{Bob_IT}` | ✅ 200, access granted |
| `GET /employees/{David_HR}` | ❌ 403, cross-department access denied |
| `PUT /employees/{David_HR}` | ❌ 403, **before** SQL UPDATE |
| `POST /employees {dept: HR}` | ❌ 403, cross-department creation denied |
| `POST /auth/logout` (alice) | ✅ 200, access token immediately blacklisted |
| Next request with revoked token | ❌ 401, denylist checked |

---

## 🏗️ Architecture

SeaDesktop follows **Domain-Driven Design** with strict layer separation:

```
SeaDesktop/
│
├── apps/                                # Applications
│   ├── Backend_Seastar/                 # Seastar HTTP server
│   │   └── src/
│   │       ├── http/
│   │       │   ├── handlers/            # CRUD + relations + auth + admin
│   │       │   ├── middlewares/         # Auth, CORS, RateLimit, etc.
│   │       │   ├── routing/             # Route registration
│   │       │   └── utils/               # HTTP helpers (cookies, multipart)
│   │       └── main.cpp                 # Bootstrap
│   │
│   └── SeaUI/                           # Qt6 GUI
│       └── src/                         # Administrative interface
│
├── libs/                                # DDD libraries
│   ├── sea_domain/                      # Domain Layer
│   │   ├── access_control/              # PolicySubject, PolicyResource, etc.
│   │   ├── schema/                      # Entity, Field, Relation
│   │   ├── security_scheme/             # AuthConfig, CookieConfig, TokenTracking
│   │   └── logging/                     # LoggingConfig
│   │
│   ├── sea_application/                 # Application Layer
│   │   ├── access_control/              # PolicyEngine, evaluators
│   │   ├── auth/                        # AuthService, JWT, TokenTracking
│   │   ├── logging/                     # LoggingInitializer, RingBufferSink
│   │   └── ...
│   │
│   └── sea_infrastructure/              # Infrastructure Layer
│       ├── yaml/                        # YAML Parser
│       ├── runtime/                     # CRUD engines
│       └── persistence/                 # MySQL, Memory backends
│
└── SeaDesktopDemo1.yaml                 # Complete sample configuration
```

---

## 🔧 Technical Stack

| Layer | Technology |
|---|---|
| Language | **C++20** |
| HTTP server | **Seastar** (shared-nothing SMP, futures/continuations) |
| Desktop GUI | **Qt 6.8.3** |
| Build | **CMake** (monorepo) |
| Database | **MySQL 8** (via Connector/C++) |
| Auth | **JWT HS256/RS256/ES256** (access + refresh + tracking) |
| Password hashing | **bcrypt** |
| Logging | **spdlog 1.14** (header-only via FetchContent) |
| JSON | **nlohmann/json** |
| YAML | **yaml-cpp** |
| Supported OS | Linux (tested on Ubuntu 24.04) |

---

## 🎨 Why Seastar?

Seastar is a C++ framework for **high-performance** servers:
- **Shared-nothing SMP**: 1 thread per core, no mutexes
- **Futures/continuations**: non-blocking asynchronous I/O
- **DPDK**: userspace networking (optional)
- **Linux-specific primitives**: aio, epoll, etc.

Used by **ScyllaDB**, **Redpanda**, and other industrial-grade systems.

---

## 📦 Installation (Development)

### Prerequisites

```bash
# Ubuntu 24.04
sudo apt install build-essential cmake git \
                 libmysqlcppconn-dev \
                 nlohmann-json3-dev \
                 libyaml-cpp-dev \
                 libssl-dev

# Qt 6.8.3 via Qt Online Installer
# Seastar from source: /opt/seastar
```

### Build

```bash
git clone github.com/fredf21/SeaDesktop.git
cd SeaDesktop
mkdir build && cd build
cmake -DCMAKE_PREFIX_PATH=/path/to/Qt6.8.3 ..
cmake --build . --target Backend_Seastar SeaUI -j$(nproc)
```

### Run

```bash
# Required environment variable (or auto-generated if secret: "")
export SEA_DESKTOP_JWT_SECRET="your-jwt-secret-32-characters-minimum"

# Start the server
./apps/Backend_Seastar/Backend_Seastar --config=./SeaDesktopDemo1.yaml --service_name=CCNBService

# Start the GUI (separately)
./apps/SeaUI/SeaUI
```

---

## 🗺️ Roadmap

### ✅ v0.1.0 - Foundations
- Complete Domain model (PolicySubject, PolicyResource, PolicyContext)
- PolicyEngine with operators and evaluators
- ABAC YAML Parser (`own_resource`, `same_scope`, `allow_roles`)
- JWT with custom claims (basic refresh tokens)
- AuthorizationMiddleware (Strategy C double check)
- ResourceAuthorizationHelper (resource-aware ABAC)
- Auto-generated CRUD API + relationship routes
- Silent ABAC filter + 403 cross-scope

### ✅ v0.2.0 - JWT Cookies, Token Tracking & Logging
- HttpOnly cookies with complete configuration (domain, path, same_site)
- Token tracking: refresh allowlist + access denylist + rotation + cache
- Automatic cleanup of expired tokens
- Structured logging with spdlog (7 named modules)
- Multiple sinks (console + file) with size/time rotation
- REST endpoint `/admin/logs` with incremental polling
- Seastar → spdlog hook for internal log capture
- Complete user documentation

### ✅ v1.0.0 - Remote-first administration & Docker (Current)
- `/admin/projects/*` endpoints for full YAML CRUD over HTTP
- `/admin/restart` endpoint for graceful service restart
- SeaUI profiles: Local and Remote modes, profile manager UI
- HttpProjectRepository: SeaUI backend reads/writes via HTTP
- RemoteLogsViewer: in-app viewer for remote service logs
- Multi-stage Dockerfile (Seastar from source, ~150 MB runtime image)
- docker-compose orchestration for multi-services + MySQL
- docker-compose.prod.yml with Docker secrets support
- Production hardening of `/admin/*` endpoints (JWT + admin role)
- Full bilingual documentation (EN + FR) of every feature
- Bootstrap admin procedure documented

### 🌟 v1.1.0
- WebSocket for real-time notifications
- OAuth2 providers (Google, GitHub)
- PostgreSQL support
- MongoDB support
- Versioned migrations (with rollback)
- Streaming for large files
- Extended file storage (multi-backend filesystem/S3)
- Orchestrator daemon to auto-deploy new projects as containers
- Hardening: restrict `role: admin` in `/auth/register` to existing admins
- RemoteLogsViewer: live polling, filters by level/logger, search
---

## 🎓 Who Is It For?

- **Startups** that want a backend + GUI in a few hours
- **Internal teams** that need secure admin tooling
- **C++ developers** who want modern, high-performance boilerplate
- **Architects** who value a declarative approach (Infrastructure-as-Code)

---

## 📚 Documentation

### English

| Document | Contents |
|---|---|
| [`Release_Notes.md`](./Release_Notes.md) | Detailed changelog by version |
| [`docs/seadesktop_user_guide.md`](./docs/seadesktop_user_guide.md) | Complete user guide (YAML reference) |
| [`docs/admin.md`](./docs/admin.md) | Administration endpoints (`/admin/projects/*`, `/admin/restart`) |
| [`docs/auth.md`](./docs/auth.md) | Authentication: JWT, cookies, token tracking |
| [`docs/docker_deployment.md`](./docs/docker_deployment.md) | Docker deployment, multi-services, production |
| [`docs/errors.md`](./docs/errors.md) | Error response format and codes |
| [`docs/FILE_FEATURE_USER_GUIDE.md`](./docs/FILE_FEATURE_USER_GUIDE.md) | File storage: declaration, upload, download, sharing |
| [`docs/healthcheck.md`](./docs/healthcheck.md) | `/health` and `/health/ready` endpoints |
| [`docs/logging.md`](./docs/logging.md) | Logging: modules, sinks, rotation, `/admin/logs` |
| [`docs/pagination.md`](./docs/pagination.md) | Pagination: page, offset, cursor |
| [`docs/SEAUI_GUIDE.md`](./docs/SEAUI_GUIDE.md) | SeaUI desktop application (Local and Remote modes) |

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

---

## 🤝 Contributing

The project is in **alpha**. Contributions are welcome, especially on:
- Unit tests and integration tests
- PostgreSQL and MongoDB support
- Documentation and YAML examples
- Project templates for typical use cases

---

## 📜 License

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

## 👤 Author

**Frédéric** — Architect & C++ Developer

> Recruiters, investors, and potential collaborators are welcome to contact me on LinkedIn.

---

<p align="center">
  <strong>SeaDesktop v1.0.0</strong> — From YAML to a complete product in minutes.
</p>
