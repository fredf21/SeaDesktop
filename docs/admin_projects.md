# Project administration endpoints

The SeaDesktop API exposes an administration endpoint that lists the
YAML projects available on the server. This is the first building
block for letting a remote client (typically SeaUI in remote mode)
discover and manage projects without accessing the server filesystem
directly.

## Overview

| Endpoint | Method | Purpose | Security |
|----------|--------|---------|----------|
| `/admin/projects` | GET | Lists available YAML projects | JWT + admin role |

This endpoint is exposed by every Backend_Seastar service. All
services of the same project respond equivalently: they read the same
`configs/` directory on the server.

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
# -> lists YAML files in /etc/seadesktop/configs

# Mode 2: dirname fallback
./backend_seastar --config configs/TestDemo.yaml \
                  --service_name TestService
# -> lists YAML files in configs/
```

## GET /admin/projects

Lists the YAML files present in the projects directory.

### Request

```http
GET /admin/projects HTTP/1.1
Authorization: Bearer <jwt_token>
```

The JWT must belong to a user with the administrator role configured
in the YAML (`security.access_control.admin_role`, default `admin`).

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
- `file`: full filename with extension. Serves as identifier for
  future endpoints (`GET /admin/projects/{file}`).

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

| Code | Case | Response |
|------|------|----------|
| 401 | Missing or invalid JWT token | `{"success":false,"error":{"code":"AUTHENTICATION_ERROR","message":"Token manquant."}}` |
| 403 | Authenticated user but non-admin role | `{"success":false,"error":{"code":"AUTHORIZATION_ERROR","message":"Admin role required."}}` |
| 500 | Filesystem read error (permissions, etc.) | `{"success":false,"error":{"code":"INTERNAL_ERROR","message":"..."}}` |

### Security

The endpoint is protected by a **two-layer** mechanism:

1. **ProtectedHandler** (upstream middleware). Checks the presence
   and validity of the JWT. Without a valid token, returns 401 before
   reaching the handler.

2. **Admin guard in the handler**. Compares the `X-User-Role` header
   (injected by ProtectedHandler after JWT verification) with the
   value of `security.access_control.admin_role` from the YAML. If
   the match fails, returns 403.

This double layer ensures that an authenticated but non-administrator
user cannot list the server's projects.

## Usage examples

### Test without authentication

```bash
curl -i http://localhost:8080/admin/projects
```

Expected result: 401 Unauthorized.

### Test with admin token

```bash
TOKEN=$(curl -s -X POST http://localhost:8080/auth/login \
  -H "Content-Type: application/json" \
  -d '{"email":"admin@example.com","password":"AdminPass123!"}' \
  | jq -r '.access_token')

curl -i -H "Authorization: Bearer $TOKEN" \
     http://localhost:8080/admin/projects
```

### Override the configs directory

```bash
SEA_DESKTOP_CONFIGS_DIR=/srv/seadesktop/projects \
  ./backend_seastar \
    --config /srv/seadesktop/projects/MyProject.yaml \
    --service_name MyService
```

The endpoint will list YAML files in `/srv/seadesktop/projects`,
regardless of where the boot YAML was read from.

## Implementation notes

This endpoint is exposed by **every service** of a project. If a
project contains three services running on ports 8080, 8081 and 8082,
all three endpoints respond equivalently since they read the same
`configs/` directory. A client can therefore address any service of
the project to get the list.

The `configs/` directory is resolved **once at startup** of
Backend_Seastar. Modifying the `SEA_DESKTOP_CONFIGS_DIR` environment
variable after startup has no effect on the already-registered
endpoints; the service must be restarted for the change to take
effect.
