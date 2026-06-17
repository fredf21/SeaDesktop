# Administration endpoints

The SeaDesktop API exposes a family of administration endpoints that
allow a remote client (typically SeaUI in remote mode) to manage
YAML projects on the server and trigger service restarts, without
accessing the server filesystem directly.

## Overview

| Endpoint | Method | Purpose | Security |
|----------|--------|---------|----------|
| `/admin/projects` | GET | List available YAML projects | JWT + admin role |
| `/admin/projects/{file}` | GET | Read the raw YAML of a project | JWT + admin role |
| `/admin/projects/{file}` | POST | Create a new YAML project | JWT + admin role |
| `/admin/projects/{file}` | PUT | Replace the content of an existing YAML project | JWT + admin role |
| `/admin/projects/{file}` | DELETE | Delete a YAML project | JWT + admin role |
| `/admin/restart` | POST | Request a graceful restart of the service | JWT + admin role |

These endpoints are exposed by every Backend_Seastar service. All
services of the same deployment respond equivalently to the
`/admin/projects/*` endpoints since they read the same `configs/`
directory on the server. The `/admin/restart` endpoint, by contrast,
only restarts the service that received the request.

## Resolving the `configs/` directory

The server determines where to look for YAML files using the
following priority order:

1. **Environment variable `SEA_DESKTOP_CONFIGS_DIR`**. If set and
   non-empty, this path is used as-is. This is the recommended mode
   for Docker deployments, CI pipelines, and multi-machine setups.

2. **Parent directory of the YAML loaded at startup**. If
   Backend_Seastar is launched with `--config configs/TestDemo.yaml`,
   the fallback returns `configs/`. This is the default mode for
   local launches without specific configuration.

3. **Current directory (`.`)**. If the loaded YAML has no parent
   (path without `/`), the server falls back to the current
   directory.

Examples:

```bash
# Mode 1: environment variable
export SEA_DESKTOP_CONFIGS_DIR=/etc/seadesktop/configs
./backend_seastar --config /etc/seadesktop/configs/MyProject.yaml \
                  --service_name MyService
# -> serves YAML files in /etc/seadesktop/configs

# Mode 2: dirname fallback
./backend_seastar --config configs/TestDemo.yaml \
                  --service_name TestService
# -> serves YAML files in configs/
```

The `configs/` directory is resolved **once at startup**. Modifying
the `SEA_DESKTOP_CONFIGS_DIR` environment variable after startup has
no effect on the already-registered endpoints; the service must be
restarted for the change to take effect.

## Common security model

All `/admin/*` endpoints share the same two-layer security mechanism:

1. **ProtectedHandler** (upstream middleware). Checks the presence
   and validity of the JWT. Without a valid token, returns 401 before
   reaching the handler.

2. **Admin guard in the handler**. Compares the `X-User-Role` header
   (injected by ProtectedHandler after JWT verification) with the
   value of `security.access_control.admin_role` from the YAML. If
   the match fails, returns 403.

This double layer ensures that an authenticated but non-administrator
user cannot access or modify the server's projects.

## Filename validation

For endpoints that take a `{file}` parameter, the server applies a
two-stage validation:

1. **Character filter**: rejects filenames containing `/`, `\`, `..`,
   or starting with `.`. The extension must be `.yaml` or `.yml`
   (case-insensitive).

2. **Canonical path check**: resolves symbolic links and verifies
   that the resulting path stays inside the configs directory. This
   protects against path-traversal attacks using symlinks or other
   filesystem tricks.

Any filename failing either check returns 400 Bad Request.

---

## GET /admin/projects

Lists the YAML files present in the projects directory.

### Request

```http
GET /admin/projects HTTP/1.1
Authorization: Bearer <jwt_token>
```

### Response — success

```http
HTTP/1.1 200 OK
Content-Type: application/json

{
  "projects": [
    { "name": "BlogDemo",  "file": "BlogDemo.yaml" },
    { "name": "FileTest",  "file": "FileTest.yaml" },
    { "name": "TestDemo",  "file": "TestDemo.yaml" }
  ]
}
```

Returned fields:

- `name`: project name, derived from the filename without extension.
  Used for client-side display.
- `file`: full filename with extension. Serves as identifier for the
  other endpoints (`GET/POST/PUT/DELETE /admin/projects/{file}`).

The list is sorted alphabetically by `name` to ensure a deterministic
response. Only `*.yaml` and `*.yml` files (case-insensitive) are
returned. Hidden files (starting with a dot) are ignored.

### Response — empty or missing directory

If `SEA_DESKTOP_CONFIGS_DIR` points to a non-existent or empty
directory, the endpoint returns 200 with an empty array:

```http
HTTP/1.1 200 OK
Content-Type: application/json

{ "projects": [] }
```

A warning is written to the logs (`sea.http`) if the directory does
not exist or is not a directory. This is not a client-side error: an
empty directory is a valid state.

### Error codes

| Code | Case |
|------|------|
| 401 | Missing or invalid JWT token |
| 403 | Authenticated user but non-admin role |
| 500 | Filesystem read error (permissions, etc.) |

---

## GET /admin/projects/{file}

Returns the raw YAML content of a specific project file.

### Request

```http
GET /admin/projects/TestDemo.yaml HTTP/1.1
Authorization: Bearer <jwt_token>
```

The `{file}` parameter is the full filename as returned by
`GET /admin/projects` in the `file` field.

### Response — success

```http
HTTP/1.1 200 OK
Content-Type: application/x-yaml

project:
  name: TestDemo

services:
  - name: TestService
    port: 8080
    database:
      type: mysql
      ...
```

The body is the raw content of the YAML file, byte-for-byte
identical to what is stored on disk. Comments and original formatting
are preserved.

### Error codes

| Code | Case |
|------|------|
| 400 | Invalid filename (path traversal, wrong extension, etc.) |
| 401 | Missing or invalid JWT token |
| 403 | Authenticated user but non-admin role |
| 404 | File not found in `configs/` |
| 500 | Filesystem read error |

---

## POST /admin/projects/{file}

Creates a new YAML project file. Refuses if a file with the same
name already exists; use `PUT` to replace an existing project.

### Request

```http
POST /admin/projects/NewProject.yaml HTTP/1.1
Authorization: Bearer <jwt_token>
Content-Type: application/x-yaml

project:
  name: NewProject

services:
  - name: MainService
    port: 8080
    ...
```

The request body must be the raw YAML content. The server validates
it before persisting:

1. **YAML parsing**: the content must be syntactically valid YAML
   that conforms to the SeaDesktop schema.
2. **Name consistency**: `project.name` in the YAML must match the
   filename without extension. For example, `POST /admin/projects/NewProject.yaml`
   requires `project.name: NewProject`.

If either check fails, the file is **not** persisted and the response
is 400 Bad Request with the parser's error message.

### Atomic write strategy

The server writes the new file using a temporary-file + rename
pattern:

1. Write the content to `<file>.tmp` in the same directory.
2. Validate the YAML by parsing `<file>.tmp`.
3. If validation succeeds, rename `<file>.tmp` to the final filename
   (atomic on POSIX filesystems).
4. If validation fails, delete `<file>.tmp` and return 400.

This guarantees that a crash at any moment leaves the filesystem in
a coherent state: either the file does not exist (crash before
rename), or it is fully written (crash after rename). No partial
file is ever persisted.

### Response — success

```http
HTTP/1.1 201 Created
Content-Type: application/json

{
  "success": true,
  "file": "NewProject.yaml"
}
```

### Error codes

| Code | Case |
|------|------|
| 400 | Invalid filename, invalid YAML, or `project.name` mismatch |
| 401 | Missing or invalid JWT token |
| 403 | Authenticated user but non-admin role |
| 409 | A file with this name already exists (use `PUT` to replace) |
| 500 | Filesystem write error |

### Limitations

This endpoint creates the YAML file on disk but does **not**
automatically start a Docker container for the new project in
multi-service deployments. The client must manually orchestrate the
deployment of a new service (this limitation is accepted for v1.0).

---

## PUT /admin/projects/{file}

Replaces the content of an existing YAML project file. Refuses if
the file does not exist; use `POST` to create a new project.

### Request

```http
PUT /admin/projects/TestDemo.yaml HTTP/1.1
Authorization: Bearer <jwt_token>
Content-Type: application/x-yaml

project:
  name: TestDemo

services:
  - name: TestService
    port: 8080
    ...
```

The validation, atomic write strategy, and name-consistency check are
identical to `POST /admin/projects/{file}`.

### Response — success

```http
HTTP/1.1 200 OK
Content-Type: application/json

{
  "success": true,
  "file": "TestDemo.yaml"
}
```

### Error codes

| Code | Case |
|------|------|
| 400 | Invalid filename, invalid YAML, or `project.name` mismatch |
| 401 | Missing or invalid JWT token |
| 403 | Authenticated user but non-admin role |
| 404 | The file does not exist (use `POST` to create) |
| 500 | Filesystem write error |

### Important — service restart

A successful `PUT` writes the new YAML to disk but does **not**
restart the service. The currently-running service continues to use
the YAML it loaded at startup, held in RAM. To apply the changes, the
client must call `POST /admin/restart` afterward.

---

## DELETE /admin/projects/{file}

Permanently deletes a YAML project file. The deletion is
**irreversible**: no trash, no automatic backup.

### Request

```http
DELETE /admin/projects/OldProject.yaml HTTP/1.1
Authorization: Bearer <jwt_token>
```

### Response — success

```http
HTTP/1.1 200 OK
Content-Type: application/json

{
  "success": true,
  "file": "OldProject.yaml"
}
```

### Error codes

| Code | Case |
|------|------|
| 400 | Invalid filename |
| 401 | Missing or invalid JWT token |
| 403 | Authenticated user but non-admin role |
| 404 | The file does not exist |
| 500 | Filesystem delete error (permissions) |

### Important — running services

If a service is currently running with the deleted YAML:

- The service continues to run with the YAML loaded in RAM at
  startup; no immediate disruption.
- On the next restart, the service will fail to load (file not
  found).

The client is responsible for stopping the corresponding container
before deleting the YAML, or for accepting that the next restart
will fail until the file is restored (this limitation is accepted
for v1.0).

---

## POST /admin/restart

Requests a graceful restart of the service. The process terminates
after a short delay, letting Docker (with `restart: unless-stopped`
or equivalent) automatically respawn the container. The new process
re-reads the YAML files from disk, applying any changes made via
`PUT` or `POST /admin/projects/{file}`.

### Request

```http
POST /admin/restart HTTP/1.1
Authorization: Bearer <jwt_token>
```

The request body is ignored.

### Response — success

```http
HTTP/1.1 202 Accepted
Content-Type: application/json

{
  "success": true,
  "message": "Service restarting"
}
```

The 202 code (instead of 200) indicates that the requested work (the
restart) is not complete when the response is sent: the service is
about to terminate.

### Behaviour

1. The handler verifies the JWT and the admin role.
2. The HTTP response is sent immediately.
3. The handler schedules a 500ms delayed `_Exit(0)`, giving the
   reactor time to finish writing the response to the socket and
   close the connection cleanly.
4. After 500ms, the process terminates.
5. Docker (or the orchestrator) detects the exit and respawns the
   container.

This endpoint **requires** a container orchestrator configured to
restart the service after exit. In local execution without Docker,
the service will terminate permanently.

### Error codes

| Code | Case |
|------|------|
| 401 | Missing or invalid JWT token |
| 403 | Authenticated user but non-admin role |

There are no other error cases: if the JWT is valid and the role is
admin, the restart is initiated unconditionally.

### Workflow — applying YAML changes

The typical flow for a remote client (SeaUI) is:

1. `PUT /admin/projects/TestDemo.yaml` — save the modified YAML.
2. `POST /admin/restart` — restart the service.
3. The Docker container is killed and respawned.
4. The new backend process re-reads the YAML file from disk and
   applies the changes (migrations, route registrations, etc.).

Without step 2, the changes remain on disk but the running service
continues to serve the previous YAML.

### Multi-service implications

`POST /admin/restart` restarts **only the service that received the
request**. To restart a different service in a multi-service
deployment, the client must send the request to the URL of that
specific service.

For example, with three services on ports 8080, 8081, and 8082:

```bash
# Restart service A only
curl -X POST -H "Authorization: Bearer $TOKEN" \
     http://localhost:8080/admin/restart

# Restart service B only (separate request)
curl -X POST -H "Authorization: Bearer $TOKEN" \
     http://localhost:8081/admin/restart
```

---

## Usage examples

### Obtain an admin token

All `/admin/*` endpoints require an authenticated admin token. The
token is obtained via `POST /auth/login`:

```bash
TOKEN=$(curl -s -X POST http://localhost:8080/auth/login \
  -H "Content-Type: application/json" \
  -d '{"email":"admin@example.com","password":"AdminPass123!"}' \
  | jq -r '.access_token')
```

### List all projects

```bash
curl -H "Authorization: Bearer $TOKEN" \
     http://localhost:8080/admin/projects
```

### Read a specific project

```bash
curl -H "Authorization: Bearer $TOKEN" \
     http://localhost:8080/admin/projects/TestDemo.yaml
```

### Create a new project

```bash
curl -X POST \
     -H "Authorization: Bearer $TOKEN" \
     -H "Content-Type: application/x-yaml" \
     --data-binary @new_project.yaml \
     http://localhost:8080/admin/projects/NewProject.yaml
```

### Update an existing project

```bash
curl -X PUT \
     -H "Authorization: Bearer $TOKEN" \
     -H "Content-Type: application/x-yaml" \
     --data-binary @modified_project.yaml \
     http://localhost:8080/admin/projects/TestDemo.yaml
```

### Delete a project

```bash
curl -X DELETE \
     -H "Authorization: Bearer $TOKEN" \
     http://localhost:8080/admin/projects/OldProject.yaml
```

### Apply changes via restart

```bash
# 1. Modify the YAML
curl -X PUT \
     -H "Authorization: Bearer $TOKEN" \
     -H "Content-Type: application/x-yaml" \
     --data-binary @modified.yaml \
     http://localhost:8080/admin/projects/TestDemo.yaml

# 2. Restart the service to apply
curl -X POST \
     -H "Authorization: Bearer $TOKEN" \
     http://localhost:8080/admin/restart

# 3. Wait for Docker to respawn the container
sleep 5

# 4. Verify the service is back up
curl http://localhost:8080/health
```

---

## Implementation notes

### Equivalence across services

The `/admin/projects/*` endpoints are exposed by every service of a
deployment. Since all services share the same `configs/` volume
(typically a Docker volume), they all see the same YAML files. A
client can therefore use any service URL to list, read, write, or
delete projects.

The `/admin/restart` endpoint, by contrast, is service-specific: it
restarts only the service that received the request.

### Multi-service architecture

In a typical multi-service deployment:

```
Host machine
├── /var/lib/seadesktop/configs/      (shared volume)
│   ├── TestDemo.yaml
│   ├── BlogDemo.yaml
│   └── FileTest.yaml
│
└── Docker
    ├── Container service A (port 8080)  -> /app/configs (mounted)
    ├── Container service B (port 8081)  -> /app/configs (same)
    └── Container service C (port 8082)  -> /app/configs (same)
```

All three services see the same YAML files. Modifying a file via
service A is immediately visible to services B and C. However:

- The change is **on disk only**; the running services still use the
  YAML they loaded at startup.
- To apply the change to service A, call `POST /admin/restart` on
  service A.
- To apply it to services B and C, call `POST /admin/restart` on
  each of them separately.

### Configuration of the admin role

The admin role is configurable per-service in the YAML:

```yaml
security:
  access_control:
    admin_role: admin   # default; can be customized
```

Users with this role in their JWT (`role` claim) are granted access
to `/admin/*` endpoints. The role is checked by the `X-User-Role`
header injected by `ProtectedHandler` after JWT verification.
