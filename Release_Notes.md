# SeaDesktop Release Notes

## v1.0.0 - Production-ready release (2026-06-23)

🎯 **First production-ready release.** Brings the platform to feature
completion: containerized backend deployment via Docker, multi-service
orchestration, SeaUI Remote mode for managing deployed backends from
any OS, validated HTTPS, native Linux packaging via `.deb`, and
extensive documentation in English and French.

This release closes the v0.x development cycle by consolidating
everything into a stable, distributable product. The backend can now
ship as either a Linux `.deb` (with statically-linked Seastar and
bundled libmariadbcpp) or a Docker image. SeaUI ships as a Linux
`.deb` with bundled icon and `.desktop` integration; macOS `.dmg` and
Windows NSIS installers are documented and ready to produce.

---

### ✨ Added

#### Docker containerization and multi-service orchestration

**Dockerfile for the backend** based on Ubuntu 24.04 with Seastar
built from source (pinned to commit `a2dd373e`) and the MariaDB C++
connector bundled in. The image runs as the non-root `seadesktop`
system user with `CAP_IPC_LOCK` and `CAP_SYS_NICE` for Seastar
scheduling. Configuration is mounted read-only from the host and
selected via a `--config` argument per container.

**docker-compose.yml**: production-grade orchestration with one MySQL
container and N backend containers, one per service definition.
Health checks, depends_on, and volume bindings are pre-configured.
Adding a new service is a matter of duplicating a service block in
the YAML.

**docker-compose.override.yml**: development override with debug-level
logging, source mounts, and live reload of YAML configs without
rebuilding the image.

#### SeaUI Remote mode for managing deployed backends

SeaUI gains a **Connection dialog** at startup that lets the user
choose between the **Local (built-in)** mode (read/write YAML files
directly on the filesystem, Linux only) and a **Remote** mode that
talks to a deployed backend over HTTP/HTTPS. Multiple Remote profiles
can be registered through the **Profile Manager**, each with its own
base URL, project list, and remembered credentials.

In Remote mode, SeaUI uses the new `IProjectRepository` abstraction
to delegate all project management operations to the backend's
`/admin/projects/*` REST endpoints. The repository is fully
asynchronous (`QFuture<T>`) and switches implementations at runtime
based on the active profile, with no behavior change in the UI layer.

A `RemoteLogsViewer` window streams the backend's `sea.persistence`,
`sea.application`, `sea.http`, and `sea.boot` loggers in real time
through Server-Sent Events on `/admin/logs/stream`. Logs are
filterable by logger, level, and free-text search.

#### Validated HTTPS support end-to-end

**`tests/https/`** ships a self-contained HTTPS test harness: a
`generate_certs.sh` script that produces a self-signed cert with
`localhost` and `127.0.0.1` SANs, an `nginx.conf` reverse proxy
configuration that fronts the backend on port 443, and a
`docker-compose.https.yml` overlay that wires nginx in front of any
service from the main compose file.

**Tested workflow**:
1. Run `bash tests/https/generate_certs.sh` to produce
   `tests/https/cert.pem` and `tests/https/key.pem` (ignored by git).
2. Start the stack with `docker compose -f docker-compose.yml -f
   docker-compose.https.yml up -d`.
3. Curl with `-k` works; curl without `-k` correctly fails on the
   self-signed cert (expected behavior).
4. Add the cert to the system truststore (`sudo cp tests/https/cert.pem
   /usr/local/share/ca-certificates/seadesktop-test.crt && sudo
   update-ca-certificates`) and SeaUI Remote connects via
   `https://localhost` natively, using the system CA store.

In production with a real cert (Let's Encrypt, commercial CA), no
SeaUI configuration is needed - Qt uses the system truststore
automatically. The same applies to macOS and Windows.

#### Linux native packaging via `.deb`

Two `.deb` packages are produced via CPack in Release mode:

**`seadesktop-backend-1.0.0-Linux.deb`** (5.7 MB):
- `/opt/seadesktop/bin/backend_seastar` (14.7 MB Release binary,
  stripped, Seastar statically linked)
- `/opt/seadesktop/lib/libmariadbcpp.so` (bundled)
- `/opt/seadesktop/lib/mariadb/plugin/*.so` (5 MariaDB plugins)
- `/usr/bin/seadesktop-backend` (wrapper that sets
  `LD_LIBRARY_PATH=/opt/seadesktop/lib`)
- `/lib/systemd/system/seadesktop-backend.service` (hardened unit
  with `NoNewPrivileges`, `ProtectSystem=strict`, capabilities)
- `/usr/share/seadesktop/default.yaml.example` (configuration
  template)

The `postinst` script creates the `seadesktop` system user, the
`/var/lib/seadesktop`, `/var/log/seadesktop`, and `/etc/seadesktop`
directories with proper ownership, reloads systemd, and prints the
three remaining steps for the admin (create
`/etc/seadesktop/default.yaml`, generate `SEA_DESKTOP_JWT_SECRET`,
`systemctl enable --now`).

Dependencies are resolved through standard Ubuntu 24.04 packages
(`libssl3t64`, `libboost-program-options1.83.0`, `libhwloc15`,
`liburing2`, `libyaml-cpp0.8`, `libfmt9`, `libmariadb3`,
`libsystemd0`). `mysql-server` and `mariadb-server` are recommended;
`nginx`, `caddy`, and `traefik` are suggested for HTTPS termination.

**`seaui-1.0.0-Linux.deb`** (2.3 MB):
- `/usr/bin/SeaUI` (6.3 MB Release binary, linked against Qt 6.4 from
  Ubuntu)
- `/usr/share/applications/SeaUI.desktop` (menu entry with
  `StartupWMClass=SeaUI` for Wayland)
- `/usr/share/icons/hicolor/256x256/apps/seaui.png` (256x256 icon)

Dependencies are the standard Ubuntu Qt6 packages (`libqt6core6t64`,
`libqt6widgets6t64`, `libqt6network6t64`, `libqt6webenginewidgets6`,
`libqt6webenginecore6-bin`). `seadesktop-backend` is recommended for
users who want both components on the same machine.

#### Multi-platform SeaUI icon

SeaUI now ships with a proper application icon in three formats:

- `apps/SeaUI/icons/seaui.png` (256x256, 57 KB)
- `apps/SeaUI/icons/seaui.ico` (multi-resolution 16/32/48/64/128/256
  for Windows)
- `apps/SeaUI/icons/seaui.icns` (multi-resolution macOS bundle)

The icon is embedded in the Qt resource system via `seaui_icons.qrc`
(prefix `/`), referenced by `QApplication::setWindowIcon`. On
Windows, the EXE includes the `.ico` via `packaging/SeaUI.rc`. On
macOS, the bundle declares the `.icns` via `MACOSX_BUNDLE_ICON_FILE`
in CMake.

To work around GNOME/Wayland's design choice of hiding title bar
icons, `MainWindow` now adds a `QToolBar` named "LogoToolBar" at the
top with the application logo (32x32) and the "SeaDesktop" title in
bold. This makes the application visually identifiable on every
platform regardless of window manager.

`main.cpp` calls `QGuiApplication::setDesktopFileName("SeaUI")`
between `setApplicationName` and the `ConnectionDialog` to keep the
Wayland app_id stable across the dialog-to-main-window transition.
Without this, the icon would briefly disappear in the GNOME dock.

#### Multi-platform documentation

**`docs/installation.md`** and **`docs_french/installation_fr.md`**
provide a complete installation guide split into:
- End-user installation (`apt install` commands, first launch
  walkthrough)
- Component overview (which platforms support what natively)
- Linux (build deps, Qt Creator and CLI workflows, `.deb` generation,
  AppImage alternative)
- macOS (build, `macdeployqt`, codesign, notarytool, `create-dmg`)
- Windows (MSVC/MinGW, `windeployqt`, NSIS, signtool)
- Post-install verification across platforms

**Platform Support sections** added to `README.md`,
`README_french.md`, `docs/docker_deployment.md`, and
`docs_french/docker_deployment_fr.md`. Each clarifies which workflow
applies per OS: Linux can run the backend natively or in Docker;
macOS and Windows must run the backend in Docker Desktop and connect
SeaUI via Remote mode.

#### E2E test coverage

83 C++ integration tests plus 115 Python end-to-end tests now cover:
- Full CRUD lifecycle on all field types including binary
  (`UUID`, `Binary`, `File`)
- Pagination (offset and cursor-based)
- Many-to-many relations (`/attach` and `/detach`)
- File upload with multipart parsing
- Authentication: login, register, logout, refresh, /me
- Authorization: ABAC policies, route-level checks, resource-level
  ownership
- Admin: project listing, fetch, save, create, delete, restart
- Streaming logs via SSE

The new `tests/e2e/run_e2e.sh` orchestrates two pytest passes
because the `test_admin_restart.py` test spawns backends sequentially
and would otherwise hit a Seastar `sharded<MysqlConnexionPool>` assert
on the second startup. The two-pass run keeps all 115 tests green
without process-level isolation hacks inside the test bodies.

---

### 🔧 Changed

#### Admin endpoints centralization

The `/admin/projects/*` endpoints (list, get, save, create, delete)
and `/admin/restart` are now first-class REST endpoints exposed by the
backend, replacing the previous filesystem-only access. SeaUI in
Remote mode uses these exclusively. The endpoints enforce admin role
through the existing JWT middleware.

#### YAML manipulation in SeaUI

All YAML edits performed by SeaUI now go through textual manipulation
(line-based patching) instead of `yaml-cpp` round-trip, preserving
comments, formatting, and trailing whitespace exactly. This is
important because `default.yaml` files are version-controlled by
admins and changes from the UI should produce minimal diffs.

#### Translations (FR)

All user-facing strings added in Phase 5 (Pagination, Many-to-many,
File handling) and Phase 7-bis (Remote mode, Admin endpoints) are now
translated to French through `apps/SeaUI/SeaUI_fr_FR.ts`. The
`TranslationManager` enables live language switching without restart.

---

### 🐛 Fixed

#### Seastar `sharded<MysqlConnexionPool>` startup assertion

The backend would assert on the second startup within the same
process tree (e.g. parallel pytest restart tests) because the
`sharded` smart pointer was not stopped before destruction. Every
code path between `mysql_pool->start()` and process exit is now
wrapped in a try/catch that guarantees `stop()` is called, including
shutdown signal handlers.

#### SeaUI icon disappearance on Wayland

On GNOME with Wayland, the application icon would disappear from the
dock when the `ConnectionDialog` was closed and `MainWindow` opened
(the Wayland app_id would change). Fixed by calling
`setDesktopFileName("SeaUI")` after `setApplicationName` but before
constructing any window, locking the app_id to the installed
`.desktop` file.

#### `qt_add_resources` PREFIX double-path bug

Initial icon resource was at `:/icons/icons/seaui.png` (double
`icons/`) instead of `:/icons/seaui.png` because CMake's
`qt_add_resources(... PREFIX "/icons" FILES icons/seaui.png ...)`
concatenates the prefix and the file path. Fixed by setting
`PREFIX "/"` so the final path is correctly `:/icons/seaui.png`.

#### Swagger `ErrorResponse` reference

`ErrorResponse` was defined only inside `add_auth_schemas()` (which
runs only when authentication is enabled in the YAML), but it was
referenced from all CRUD path operations regardless. This caused the
Swagger UI to crash with a "reference not found" error on projects
without auth. Moved the definition to the unconditional schema
section.

#### Various small fixes

- `mysql_uses_binary_storage()` now correctly handles `UUID` and
  `File` field types (both stored as `BINARY(16)`).
- `check_single` returns a `ResourceCheckResult` struct (with
  `.allowed` and `.reason` fields) instead of a bare bool, which was
  misleading the access control layer.
- Pivot table `insert_pivot()` now correctly wraps 36-char UUID
  strings with `UUID_TO_BIN(?, 1)` when the pivot columns are
  `BINARY(16)` (detected via a heuristic on dash positions).

---

### 📊 Validated end-to-end tests

- **565 Qt unit tests** (SeaUI side)
- **83 C++ integration tests** (`sea_integration_tests` against a
  real backend + MySQL)
- **115 Python end-to-end tests** (93 base + 19 admin_projects + 3
  admin_restart)
- **Total: 763 tests**, all green via `tests/e2e/run_e2e.sh`

HTTPS validation: SeaUI Remote successfully connects to
`https://localhost` against the self-signed cert when the cert is
added to the system truststore. Production-grade Let's Encrypt certs
work without any configuration.

`.deb` install validation: both `seadesktop-backend-1.0.0-Linux.deb`
and `seaui-1.0.0-Linux.deb` install via `sudo apt install ./*.deb` on
a clean Ubuntu 24.04, resolve all dependencies, and run successfully.

---

### 📁 New files (highlights)

```
apps/Backend_Seastar/packaging/        # Wrapper, systemd unit, postinst, prerm
apps/SeaUI/icons/                      # PNG, ICO, ICNS
apps/SeaUI/packaging/                  # .desktop.in, .rc
apps/SeaUI/seaui_icons.qrc             # Qt resource file
apps/SeaUI/connectiondialog.*          # Profile selection
apps/SeaUI/profilemanagerdialog.*      # Manage Remote profiles
apps/SeaUI/httpprojectrepository.*     # HTTP backend implementation
apps/SeaUI/remotelogsviewer.*          # SSE logs streaming UI
cmake/cpack_clean_dependencies.cmake   # Remove third-party install artifacts
tests/https/                           # HTTPS test harness
tests/e2e/test_admin_projects.py
tests/e2e/test_admin_restart.py
tests/e2e/run_e2e.sh
docker-compose.yml, .override.yml, .https.yml
docs/installation.md, docs_french/installation_fr.md
docs/docker_deployment.md, docs_french/docker_deployment_fr.md
docs/SEAUI_GUIDE.md, docs_french/SEAUI_GUIDE_fr.md
docs/FILE_FEATURE.md, docs_french/FILE_FEATURE_fr.md
docs/auth.md, docs_french/auth_fr.md
docs/admin.md, docs_french/admin_fr.md
docs/user_guide.md, docs_french/user_guide_fr.md
```

---

### 📁 Modified files (highlights)

- `CMakeLists.txt` (root): CPack conditional block, `PACKAGE_SEAUI_ONLY`
  option, `EXCLUDE_FROM_ALL` for third-party deps
- `apps/Backend_Seastar/CMakeLists.txt`: install rules wrapped in
  `if(NOT PACKAGE_SEAUI_ONLY)`, absolute install paths
- `apps/SeaUI/CMakeLists.txt`: install rules wrapped in
  `if(NOT PACKAGE_BACKEND_ONLY)`, Qt resource setup, Windows .rc /
  macOS bundle / Linux .desktop conditional blocks, `qt_add_translations`
  compatible with Qt 6.4 and Qt 6.7+
- `apps/SeaUI/main.cpp`: `setDesktopFileName`, profile loading,
  `IProjectRepository` factory selection
- `apps/SeaUI/mainwindow.cpp`: QToolBar with logo, repository-based
  project operations
- `apps/SeaUI/localprojectrepository.cpp`: Qt 6.4 compatibility shim
  for `QtFuture::makeReadyValueFuture`
- `apps/Backend_Seastar/src/main.cpp`: SeedOrchestrator integration,
  signal handler cleanup, mysql pool stop guarantees
- `libs/infrastructure/CMakeLists.txt`: `EXCLUDE_FROM_ALL` for jwt-cpp
- `tests/CMakeLists.txt`: `EXCLUDE_FROM_ALL` for doctest
- `README.md`, `README_french.md`: Platform Support section

---

### 📈 Statistics

- **9 new commits** since v0.2.0
- **~12 000 new lines** across source, tests, and documentation
- **~3 000 modified lines**
- **5 new packaging artifacts** (.deb backend, .deb SeaUI, Docker
  image, planned .dmg, planned .exe NSIS)
- **2 supported delivery models** for the backend (native .deb + Docker)
- **3 platforms documented** (Linux, macOS, Windows) for SeaUI
  packaging
- **Total LoC**: ~85 000 (excluding third-party and generated files)

---

### 🔄 Migration from v0.2.0

For users running the backend natively from source:
- Optionally switch to the `.deb` for simpler updates:
  `sudo apt install ./seadesktop-backend-1.0.0-Linux.deb` instead of
  rebuilding from source. The `.deb` installs to `/opt/seadesktop/`
  so it does not conflict with a `/usr/local/bin/backend_seastar`
  from a prior `make install`.
- Move the existing config to `/etc/seadesktop/default.yaml` (the
  path expected by the new systemd unit) and create
  `/etc/seadesktop/seadesktop.env` with `SEA_DESKTOP_JWT_SECRET`.
- Replace any custom systemd unit with the one installed by the `.deb`
  (`/lib/systemd/system/seadesktop-backend.service`), then
  `sudo systemctl daemon-reload && sudo systemctl restart seadesktop-backend`.

For users running the backend in Docker:
- Pull the new Docker image (built from the same commit as the v1.0.0
  source).
- Add the `docker-compose.https.yml` overlay if you want
  HTTPS termination via nginx (optional, you can also keep your
  existing reverse proxy).
- The `docker-compose.yml` structure is unchanged - existing
  deployments continue to work.

For SeaUI users:
- Install the new `.deb` on Linux:
  `sudo apt install ./seaui-1.0.0-Linux.deb`. Previous SeaUI builds
  from source in `/usr/local/bin/SeaUI` should be removed manually
  first to avoid PATH conflicts.
- Profiles created in v0.2.0 (Local mode) are preserved in
  `~/.config/SeaDesktop/SeaUI.conf`. The new Remote mode profiles can
  be added through the Profile Manager.
- The `Connection` dialog at startup is new. Users who only use Local
  mode can check "Skip and always use Local" to bypass it.

---

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
