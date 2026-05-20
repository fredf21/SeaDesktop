# SeaDesktop Release Notes

## v0.2.0 - JWT Cookies, Token Tracking & Structured Logging (2026-05-15)

🎯 **Enhanced JWT authentication with cookie-based delivery and centralized
token tracking, plus a complete structured logging system based on spdlog.**

This release adds a production-ready authentication system
(HttpOnly cookies, immediate revocation, refresh token rotation) and a
structured logging system usable from SeaUI through a dedicated REST API.
It also comes with complete user documentation covering all of SeaDesktop's
declarative YAML features.

---

### ✨ Added

#### Enhanced JWT authentication

**Token delivery through cookies (`token_delivery`)**

New YAML field to choose how tokens are transmitted:

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

- `body` mode: tokens are returned in the JSON response (mobile API, CLI)
- `cookie` mode: tokens are sent through HttpOnly cookies (native XSS protection)
- `both` mode: combination of both for progressive migrations

The `HttpOnly` attribute is always `true` (not configurable, for security).

**Token tracking: revocation and rotation**

Complete centralized JWT token tracking system:

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

- **Refresh token allowlist**: every issued refresh token is stored in the database. An unknown refresh token is rejected.
- **Access token denylist**: `/auth/logout` adds the access token to the denylist for immediate revocation.
- **Automatic rotation**: each `/auth/refresh` call invalidates the old refresh token and issues a new one (reuse detection).
- **Local cache**: denylist checks take about ~200 ns instead of a DB query per request.
- **Automatic cleanup**: periodic deletion of expired tokens.

#### New HTTP handlers

| Handler | Route | Description |
|---|---|---|
| `RefreshHandler` | `POST /auth/refresh` | Renews the access token. Reads the refresh token from the body or cookie. Issues new tokens with rotation. |
| `LogoutHandler` | `POST /auth/logout` | Revokes the current tokens. Inserts into the denylist, removes from the allowlist, and clears cookies. |

#### Enhanced login

The existing `LoginHandler` has been extended to:

- Store the refresh token in the allowlist on login
- Issue tokens according to the configured `token_delivery` mode
- Include custom claims in the access token

#### ProtectedHandler with cookie fallback

`ProtectedHandler` now reads the token in this order:
1. HTTP header `Authorization: Bearer <token>`
2. Cookie using the name configured in `cookie.access_token_name`

A single service can therefore simultaneously support clients using either mode.

#### Auto-managed system tables

When `token_tracking.enabled: true`, two system tables are created
automatically at startup:

- `RefreshToken`: allowlist of valid refresh tokens
- `RevokedToken`: denylist of explicitly revoked access tokens

These tables are fully managed by the system and do not need to be declared
manually in the YAML.

---

#### Structured logging with spdlog

**Complete YAML logging configuration**

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

#### Seven named loggers

The system exposes seven independently configurable logging modules:

| Module | Content |
|---|---|
| `sea.boot` | Startup, migrations, initialization |
| `sea.http` | HTTP handlers, authorization, routes |
| `sea.application` | Application services |
| `sea.persistence` | Database queries, seeds, schema |
| `sea.runtime` | Validation, serialization |
| `sea.security` | Authentication, tokens, cleanup |
| `seastar` | Internal network framework logs |

Each module can have its own level through `modules:` in the YAML.

#### Two sink types

- **`console`**: writes to stderr with ANSI colors
- **`file`**: writes to a file with rotation (size, time, or both)

Multiple sinks can coexist: each message is sent to all active sinks at the same time.

#### Two output formats

- **`text`**: human-readable lines, ideal for development
  ```
  [2026-05-14 10:23:45.123] [sea.http] [info] login successful: alice@example.com
  ```

- **`json`**: line-delimited JSON, ideal for ingestion (Loki, ELK, Datadog)
  ```json
  {"timestamp":"2026-05-14T10:23:45.123Z","logger":"sea.http","level":"info","message":"login successful"}
  ```

RFC 8259-compliant escaping through `nlohmann/json` ensures valid JSON output
even when messages contain special characters.

#### File rotation

- By size: `max_size: "100MB"` (`KB`, `MB`, `GB` formats accepted)
- By time: `time_pattern: hourly` or `daily`
- Combined: rotation is triggered by the first criterion reached
- Configurable retention: `max_files: 10`

#### Asynchronous logging

```yaml
async:
  enabled: true
  queue_size: 8192
  overflow_policy: overrun_oldest   # block | overrun_oldest
```

Log writes are offloaded to a dedicated thread so the Seastar reactor is not blocked.
Two overflow policies are available:

- **`overrun_oldest`** (default): overwrites older messages, never blocks the service
- **`block`**: waits until a slot is available, no message loss

#### Seastar → spdlog hook

Internal Seastar framework logs are automatically captured and routed to the
`seastar` logger through a custom `std::streambuf` with thread-local buffering.
No log loss during the transition.

#### `/admin/logs` endpoint with in-memory ring buffer

An in-memory FIFO buffer permanently keeps the **last 10,000 messages**,
independently of configured sinks. It is exposed through two protected REST
endpoints (JWT authentication + admin role):

```
GET /admin/logs                    # view with filtering
GET /admin/logs/loggers            # list available modules
```

Supported filters:

| Parameter | Description |
|---|---|
| `limit` | Maximum number of entries (default 100, max 1000) |
| `level` | Filter by minimum level (trace/debug/info/warn/error/critical) |
| `logger` | Exact filter by module name |
| `since` | Incremental polling through `sequence_id` |
| `search` | Case-insensitive search in the message |

Incremental polling pattern for real-time monitoring:

```bash
# First request
curl -H "Authorization: Bearer $TOKEN"   "http://localhost:8081/admin/logs?limit=100"
# next_sequence_id: 12345

# Next requests: only fetch new entries
curl -H "Authorization: Bearer $TOKEN"   "http://localhost:8081/admin/logs?since=12345"
```

#### Admin role configuration

The administrator role allowed to access `/admin/logs` is fully configurable
through YAML:

```yaml
authorization:
  admin_role: "administrator"   # or "superuser", "root", etc.
```

#### Complete user documentation

Four reference documents were written based exclusively on verified source code:

| Document | Content |
|---|---|
| `seadesktop_user_guide.md` | Global user guide: all YAML keys, accepted values, defaults, behavior |
| `auth.md` | JWT authentication: tokens, cookies, tracking, rotation |
| `pagination.md` | Pagination: 3 modes (page/offset/cursor), sortable_fields, examples |
| `logging.md` | Logging: levels, modules, sinks, rotation, async, /admin/logs |

Each document follows the same completeness standard as `FILE_FEATURE_USER_GUIDE.md`:
comprehensive YAML key tables, "Expected behavior" sections, and concrete
configuration examples by use case.

---

### 🔧 Changed

#### Massive log refactor from `std::cerr` to spdlog

About **calls to `std::cerr`** were replaced with spdlog calls using the
appropriate module and level in:

- `apps/Backend_Seastar/src/main.cpp` (bootstrap)
- All HTTP handlers (`src/http/handlers/`)
- All middlewares (`src/http/middleware/`)
- MySQL persistence layer (bootstrapper, repository, introspector)
- `schema_differ`
- `seed_orchestrator`

Historical `[BOOT]` prefixes were removed (redundant with the logger module name).

#### Complete YAML demo

The `SeaDesktopDemo1.yaml` file was enhanced to illustrate all new features:
complete `logging:` section with all sinks and rotation, plus enabled `cookie:`
and `token_tracking:` blocks.

---

### 🐛 Fixed

#### UUID generation in many-to-many seeds

Bug in `MySQLGenericRepository::insert_pivot()`: UUIDs were inserted as raw
strings into `BINARY(16)` columns, causing `Incorrect string value` errors.

**Fix**: heuristic detection of UUID values and automatic wrapping with
`UUID_TO_BIN(?, 1)` when inserting into the pivot table.

#### Critical include order for `seastar_log_bridge.cpp`

Subtle bug: `<seastar/util/log.hh>` MUST be included before `<spdlog/spdlog.h>`.
The reverse order causes a cryptic compilation error, "templates can only be
declared in namespace or class scope", in release builds.

**Fix**: documented and applied in all affected files.

---

### 📊 Validated end-to-end tests

```
✅ POST /auth/login (body mode)              → tokens in JSON
✅ POST /auth/login (cookie mode)            → HttpOnly cookies issued
✅ POST /auth/refresh                        → effective rotation (old token revoked)
✅ POST /auth/logout                         → access token immediately blacklisted
✅ GET /protected with revoked token         → 401 (denylist + cache check)
✅ GET /admin/logs without auth              → 401
✅ GET /admin/logs with normal user          → 403
✅ GET /admin/logs with admin                → 200, filterable logs
✅ GET /admin/logs?since=N polling           → only new entries
✅ Seastar reactor logs visible in seastar logger
✅ File rotation triggered at 100MB          → archive created, max_files respected
✅ async overrun_oldest mode                 → service never blocked under load
```

---

### 📁 New files

```
NEW   libs/sea_domain/security_scheme/
      ├── cookie_config.{h,cpp}
      ├── token_tracking_config.{h,cpp}
      └── (extension of authentification_config)

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

### 📁 Modified files

```
MOD   libs/infrastructure/yaml/yaml_schema_parser.{h,cpp}
      (+ parse_cookie_config, parse_token_tracking_config,
         parse_logging_node, parse_sink_node, parse_rotation_node,
         parse_async_node, helpers parse_duration and parse_size)

MOD   apps/Backend_Seastar/src/http/handlers/auth/
      ├── login_handler.{h,cpp}      (+ token tracking, cookies)
      └── protected_handler.{h,cpp}  (+ cookie fallback + denylist check)

MOD   apps/Backend_Seastar/src/http/routing/route_registration.{h,cpp}
      (+ refresh, logout, /admin/logs routes)

MOD   apps/Backend_Seastar/src/main.cpp
      (+ LoggingInitializer setup, Seastar hook, token tracking wiring,
        cleanup timer for auto_cleanup)

MOD   libs/infrastructure/persistence/mysql/
      ├── mysql_bootstrapper.{h,cpp}     (+ RefreshToken/RevokedToken system tables)
      ├── mysql_generic_repository.cpp   (UUID_TO_BIN fix in insert_pivot)
      └── seed_orchestrator.cpp          (+ spdlog logs)

MOD   ~124 backend files: std::cerr → spdlog::get(...)->info/warn/error(...)

MOD   SeaDesktopDemo1.yaml
      (+ complete logging section, cookie and token_tracking blocks)
```

---

### 📈 Statistics

```
Lines of code added        : ~3,200
Lines of code refactored   : ~1,800 (std::cerr → spdlog changes)
New files                  : 14 (code + tests + docs)
New routes                 : 4 (/auth/refresh, /auth/logout,
                                /admin/logs, /admin/logs/loggers)
New system tables          : 2 (RefreshToken, RevokedToken)
Logging modules            : 7 (independently configurable)
User documentation         : ~5,100 lines (4 documents)
Critical bugs fixed        : 2
Validated end-to-end tests : 12+
```

---

### 🔄 Migration from v0.1.0

#### Existing configurations: 100% compatible

All new sections (`cookie:`, `token_tracking:`, `logging:`) are **optional**.
A v0.1.0 configuration works without modification in v0.2.0:

- Default `token_delivery`: `body` (v0.1.0 behavior preserved)
- Default `token_tracking.enabled`: `false` (stateless behavior preserved)
- Missing `logging:` section: defaults applied (text console info async)

#### Recommended progressive activation

```yaml
# Step 1: enable structured logging
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

# Step 2: enable cookies for web clients
authentication:
  token_delivery: both    # tokens in body AND cookies
  cookie:
    secure: true
    same_site: lax

# Step 3: enable token tracking for revocation
authentication:
  token_tracking:
    enabled: true
    rotation:
      enabled: true
```

---

## Previous versions

### v0.1.0 - Foundations: Domain, ABAC, JWT Authentication

First stable release of SeaDesktop, bringing together the platform foundations:
declarative modeling, MySQL persistence, automatic CRUD route generation, and
a complete ABAC authorization system.

#### Resource-aware ABAC

- Centralized `ResourceAuthorizationHelper` evaluating ABAC rules that require the resource loaded from the DB
- Integration in 9 CRUD and relational handlers:
  `ListHandler`, `GetByIdHandler`, `CreateHandler`, `UpdateHandler`,
  `DeleteHandler`, `GetOneByFkHandler`, `GetWithChildrenHandler`,
  `ListByFkHandler`, `ListByFkFieldHandler`, `ListManyToManyHandler`
- 2 new auto-generated routes per HasMany relation:
  - `GET /<parent>s_with_<children>/{id}`
  - `GET /<children>/filter/with_<parent>_name/{value}`
- `abac_mode` configuration (permissive/strict) at service and entity level
- 3 critical bugs fixed (MySQL UUID, route order, parser never called)

#### AuthorizationMiddleware

- Extended middleware pipeline with `AuthorizationMiddleware`
- `RouteAuthorizationResolver`: 8+ recognized route patterns
- Strategy C: double-check parent + child on relational routes
- Comprehensive `[AUTHZ]` logs

#### JWT with custom claims

- `entity_id` injected into the JWT at login
- `X-User-*` headers propagated from ProtectedHandler
- Refresh tokens (first implementation, without tracking or rotation)

#### ABAC YAML Parser

- Parsing of `access_control` blocks
- Support for `own_resource`, `same_scope`, `allow_roles`
- Configurable `abac_mode`

#### Domain & PolicyEngine

- Domain types: `PolicySubject`, `PolicyResource`, `PolicyContext`
- `PolicyEngine` with strategies (subject-only, full evaluation)
- Operator evaluator
