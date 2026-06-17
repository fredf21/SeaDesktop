# SeaDesktop — Authentication

Reference documentation for configuring and using SeaDesktop's JWT authentication system. This document describes every supported YAML key, its allowed values, its default behavior, and the expected system behavior. All information is based on the project's source code.

---

## Table of Contents

1. [General principle](#1-general-principle)
2. [Enabling authentication](#2-enabling-authentication)
3. [`security.authentication` block](#3-securityauthentication-block)
4. [Authentication source entity](#4-authentication-source-entity)
5. [Generated routes](#5-generated-routes)
6. [Registration (`/auth/register`)](#6-registration-authregister)
7. [Login (`/auth/login`)](#7-login-authlogin)
8. [Refresh (`/auth/refresh`)](#8-refresh-authrefresh)
9. [Logout (`/auth/logout`)](#9-logout-authlogout)
10. [Account information (`/auth/me`)](#10-account-information-authme)
11. [JWT signing algorithms](#11-jwt-signing-algorithms)
12. [Token delivery mode](#12-token-delivery-mode)
13. [Cookie configuration](#13-cookie-configuration)
14. [Token tracking (`token_tracking`)](#14-token-tracking-token_tracking)
15. [Using tokens in requests](#15-using-tokens-in-requests)
16. [Duration format](#16-duration-format)
17. [Response codes](#17-response-codes)
18. [Configuration examples](#18-configuration-examples)

---

## 1. General principle

Authentication allows clients to identify themselves to the service in order to access protected routes. SeaDesktop uses **JWT tokens** (JSON Web Tokens) issued by the server during login.

Two tokens are issued at the same time:

- A short-lived **access token**, used to authenticate each request to protected routes.
- A long-lived **refresh token**, used only to renew the access token without entering credentials again.

The systematic use of short-lived tokens combined with a refresh mechanism limits the impact of a potential token theft.

---

## 2. Enabling authentication

Authentication is enabled at two combined levels:

1. At the service level, in the `security.authentication` block.
2. At the entity level, by marking one entity as the source with `options.is_auth_source: true`.

If either level is not configured, the `/auth/*` routes are not exposed.

### Minimal example

```yaml
services:
  - name: MyService
    port: 8081

    security:
      authentication:
        type: jwt
        algorithm: HS256
        secret: "a_secret_key_with_at_least_32_characters"

    entities:
      - name: User
        options:
          is_auth_source: true
        fields:
          - name: id
            type: uuid
          - name: email
            type: email
            required: true
            unique: true
          - name: password
            type: password
            required: true
          - name: role
            type: string
            default: "user"
```

With this configuration, the `/auth/register`, `/auth/login`, `/auth/refresh`, `/auth/logout`, and `/auth/me` routes are automatically exposed.

---

## 3. `security.authentication` block

### Full structure

```yaml
security:
  authentication:
    type: jwt
    algorithm: HS256
    secret: ""
    public_key_path: ""
    private_key_path: ""
    issuer: ""
    audience: ""
    access_token_ttl: "15m"
    refresh_token_ttl: "14d"
    token_delivery: body
    cookie: { ... }
    token_tracking: { ... }
```

### Accepted keys

| Key | Type | Default value | Description |
|---|---|---|---|
| `type` | enum | `none` | Authentication type. Supported values: `none`, `jwt`, `oauth2`. Only `jwt` is fully implemented. |
| `algorithm` | enum | `HS256` | JWT signing algorithm. See [section 11](#11-jwt-signing-algorithms). |
| `secret` | string | `""` | Secret key for HS\* algorithms. See details below. |
| `public_key_path` | string | `""` | Path to the public key (PEM) for RS\* and ES\* algorithms. |
| `private_key_path` | string | `""` | Path to the private key (PEM) for RS\* and ES\* algorithms. |
| `issuer` | string | `""` | Value of the `iss` claim in issued tokens. |
| `audience` | string | `""` | Value of the `aud` claim in issued tokens. |
| `access_token_ttl` | duration | `15m` | Access token lifetime. See [section 16](#16-duration-format) for the format. |
| `refresh_token_ttl` | duration | `14d` | Refresh token lifetime. |
| `token_delivery` | enum | `body` | Token delivery mode. See [section 12](#12-token-delivery-mode). |
| `cookie` | block | defaults | Cookie configuration. See [section 13](#13-cookie-configuration). |
| `token_tracking` | block | disabled | Centralized token tracking. See [section 14](#14-token-tracking-token_tracking). |

### `secret` behavior

The `secret` value can be:

- A literal string, for example `secret: "my_secret_key_with_at_least_32_characters"`.
- An environment variable using the `${VARIABLE_NAME}` syntax. The system substitutes the variable value when the configuration is loaded.
- Empty (`secret: ""`). In this case, the system automatically generates a secret key on the first startup and persists it in the `./runtime/secrets/` directory. Later restarts reuse the same key.

```yaml
# Variant 1: literal value
secret: "PtR4ULvZBmQ9XnK2HsCxYwEa6FjDgN8T"

# Variant 2: from the environment
secret: "${JWT_SECRET}"

# Variant 3: automatic persisted generation
secret: ""
```

### Minimum secret length

If `secret` is provided, it must contain at least 32 characters. Otherwise, service startup fails with a validation error.

### Choosing between `secret` and asymmetric keys

| Algorithm | Fields to provide |
|---|---|
| `HS256`, `HS384`, `HS512` | `secret` only |
| `RS256`, `RS384`, `RS512`, `ES256`, `ES384`, `ES512` | `public_key_path` and `private_key_path` |

---

## 4. Authentication source entity

One service entity must be designated as the authentication source using the `is_auth_source: true` option. This entity contains user accounts.

### One entity per service

Exactly one entity can declare `is_auth_source: true`. If multiple entities declare it, service startup fails.

If no entity declares it, the service still starts and JWT verification still works for protected endpoints, but the `/auth/*` routes are not registered. Any call to `/auth/login` or `/auth/register` returns 404 in that case.

### Required fields

The source entity must contain at least:

- An `id` field (UUID or auto-incremented integer). The `id` is generated automatically by `RegisterHandler` and is required: without it, registration succeeds at the database level but returns 400 with "Missing ID on created entity".
- A unique identifier field, usually `email`.
- A `password` field containing the hashed password.
- A field used as the role, usually `role`.

The name of the field containing the role can be customized with `authorization.roles_claim_name`. The default is `role`.

### Full example

```yaml
- name: User
  options:
    is_auth_source: true
    timestamps: true
  fields:
    - name: id
      type: uuid
    - name: email
      type: email
      required: true
      unique: true
    - name: password
      type: password
      required: true
    - name: role
      type: string
      default: "user"
```

### Automatic behavior

The `password` type applies:

- Automatic bcrypt hashing on insert and update.
- Exclusion of the field from JSON responses (`serializable: false` by default).

See the main user guide for details about the `password` type.

---

## 5. Generated routes

When authentication is enabled and a source entity exists, five routes are automatically exposed:

| Method | Route | Authentication required | Description |
|---|---|---|---|
| `POST` | `/auth/register` | No | Register a new account. |
| `POST` | `/auth/login` | No | Log in with credentials. |
| `POST` | `/auth/refresh` | No (uses the refresh token) | Renew the access token. |
| `POST` | `/auth/logout` | Yes | Log out. |
| `GET` | `/auth/me` | Yes | Information about the connected account. |

---

## 6. Registration (`/auth/register`)

### Request

```bash
curl -X POST http://localhost:8081/auth/register \
  -H "Content-Type: application/json" \
  -d '{
    "email": "alice@example.com",
    "password": "PlainTextPassword",
    "role": "user"
  }'
```

### Request format

The body must contain the source entity fields considered mandatory. The exact names depend on the entity field declaration.

### Response format

```json
{
  "id": "550e8400-e29b-41d4-a716-446655440000",
  "email": "alice@example.com",
  "role": "user"
}
```

The password field never appears in the response (automatic exclusion via the `password` type).

### Expected behavior

1. The system validates the provided fields (formats, constraints).
2. The password is hashed with bcrypt before insertion.
3. The new user's UUID is generated automatically.
4. The account is created in the database with a 201 status code.

### Response codes

| Code | Cause |
|---|---|
| 201 | Account created successfully. |
| 400 | Validation failed: invalid email, missing password, etc. |
| 409 | Uniqueness constraint violated, for example email already in use. |

### Security warning — open registration

In v1.0, `POST /auth/register` accepts the `role` field as-is. Anyone with network access to the endpoint can therefore create an administrator account by posting `"role": "admin"`. This is acceptable to bootstrap the first administrator on a fresh deployment, but for a public deployment, you should either:

- disable open registration once the first admin is created (remove the route at the proxy level, or filter at the application level);
- or strip the `role` field from incoming requests and force it to `user` by default at the proxy.

A built-in hardening pass is planned for v1.1 that will optionally restrict the `role` field to existing administrators only.

---

## 7. Login (`/auth/login`)

### Request

```bash
curl -X POST http://localhost:8081/auth/login \
  -H "Content-Type: application/json" \
  -d '{
    "email": "alice@example.com",
    "password": "PlainTextPassword"
  }'
```

### Response format according to `token_delivery`

**`body` mode (default):**

```json
{
  "access_token": "eyJhbGciOiJIUzI1NiIs...",
  "refresh_token": "eyJhbGciOiJIUzI1NiIs...",
  "token_type": "Bearer",
  "expires_in": 900
}
```

**`cookie` mode:**

```
HTTP/1.1 200 OK
Set-Cookie: sea_access=eyJhbGciOiJIUzI1NiIs...; HttpOnly; Secure; SameSite=Lax; Path=/
Set-Cookie: sea_refresh=eyJhbGciOiJIUzI1NiIs...; HttpOnly; Secure; SameSite=Lax; Path=/

{
  "token_type": "Bearer",
  "expires_in": 900
}
```

**`both` mode:** tokens appear both in the JSON body and in cookies.

> **SeaUI Remote mode.** SeaUI in Remote mode reads `access_token` from the JSON body of the login response. A backend configured with `token_delivery: cookie` will cause SeaUI's login to fail with "Login response missing access_token". Use `body` (default) or `both` for backends administered remotely by SeaUI.

### Expected behavior

1. The system searches for a user whose email matches.
2. The provided password is checked against the bcrypt hash in the database.
3. If verification succeeds, two tokens are generated:
   - The access token contains the `sub` (user identifier), `role`, `iat`, and `exp` claims.
   - The refresh token contains `sub`, `jti` (unique token identifier), `iat`, and `exp`.
4. If `token_tracking.enabled: true`, the refresh token is inserted into the system table of valid refresh tokens.
5. Tokens are returned according to the `token_delivery` mode.

### Response codes

| Code | Cause |
|---|---|
| 200 | Login successful. |
| 400 | Required fields missing. |
| 401 | Unknown email or incorrect password. |

---

## 8. Refresh (`/auth/refresh`)

### Request

**`body` mode:**

```bash
curl -X POST http://localhost:8081/auth/refresh \
  -H "Content-Type: application/json" \
  -d '{ "refresh_token": "eyJhbGciOiJIUzI1NiIs..." }'
```

**`cookie` mode:** no body is required; the refresh token is read from the cookie configured in `cookie.refresh_token_name`.

```bash
curl -X POST http://localhost:8081/auth/refresh \
  --cookie "sea_refresh=eyJhbGciOiJIUzI1NiIs..."
```

### Response format

Same as the `/auth/login` response: a new access token and, depending on the rotation configuration, possibly a new refresh token.

### Expected behavior

1. The refresh token is extracted from the JSON body or from the cookie according to the configured mode.
2. Its signature and expiration are verified.
3. If `token_tracking.enabled: true`, the existence of the refresh token in the database allowlist is verified. A refresh token absent from the list is rejected.
4. A new access token is generated.
5. If `token_tracking.rotation.enabled: true` (default value), the old refresh token is removed from the allowlist and a new refresh token is issued.

### Response codes

| Code | Cause |
|---|---|
| 200 | Refresh successful. |
| 400 | Missing refresh token. |
| 401 | Refresh token invalid, expired, or absent from the allowlist if tracking is enabled. |

---

## 9. Logout (`/auth/logout`)

### Request

```bash
curl -X POST http://localhost:8081/auth/logout \
  -H "Authorization: Bearer eyJhbGciOiJIUzI1NiIs..."
```

In `cookie` mode, cookies are automatically sent by the browser.

### Expected behavior

1. The access token is extracted from the `Authorization` header or from the cookie.
2. Its validity is verified.
3. If `token_tracking.enabled: true`:
   - The access token is inserted into the denylist of revoked tokens.
   - The corresponding refresh token is removed from the allowlist.
4. In `cookie` mode, `Set-Cookie` headers with `expires=0` are emitted to clear cookies on the client side.

### Response codes

| Code | Cause |
|---|---|
| 200 | Logout successful. |
| 401 | Access token missing or invalid. |

### Behavior without tracking

If `token_tracking.enabled: false`, logout does not actually invalidate the token server-side: the access token remains usable until its natural expiration. Only cookies are cleared in `cookie` mode. For immediate revocation, token tracking must be enabled.

---

## 10. Account information (`/auth/me`)

### Request

```bash
curl -X GET http://localhost:8081/auth/me \
  -H "Authorization: Bearer eyJhbGciOiJIUzI1NiIs..."
```

### Response format

The fields of the currently connected source entity, excluding fields marked `serializable: false`, typically the hashed password.

```json
{
  "id": "550e8400-e29b-41d4-a716-446655440000",
  "email": "alice@example.com",
  "role": "user",
  "created_at": "2026-05-14T10:23:45.123Z",
  "updated_at": "2026-05-14T10:23:45.123Z"
}
```

### Response codes

| Code | Cause |
|---|---|
| 200 | Information returned. |
| 401 | Access token missing, invalid, or revoked if tracking is enabled. |

---

## 11. JWT signing algorithms

### Supported algorithms

| Algorithm | Type | Description |
|---|---|---|
| `HS256` | Symmetric | HMAC SHA-256. One shared secret key. Default. |
| `HS384` | Symmetric | HMAC SHA-384. |
| `HS512` | Symmetric | HMAC SHA-512. |
| `RS256` | Asymmetric | RSA SHA-256. Public and private keys. |
| `RS384` | Asymmetric | RSA SHA-384. |
| `RS512` | Asymmetric | RSA SHA-512. |
| `ES256` | Asymmetric | ECDSA SHA-256. |
| `ES384` | Asymmetric | ECDSA SHA-384. |
| `ES512` | Asymmetric | ECDSA SHA-512. |

### Recommended choice

| Use case | Recommended algorithm |
|---|---|
| Monolithic service with full control on both sides | `HS256` (simple, fast). |
| Multiple services sharing authentication | `RS256` or `ES256` (the public key alone is enough to verify). |
| Microservices architecture with delegation | `RS256` or `ES256`. |

### Configuration for a symmetric algorithm

```yaml
authentication:
  type: jwt
  algorithm: HS256
  secret: "${JWT_SECRET}"
```

### Configuration for an asymmetric algorithm

```yaml
authentication:
  type: jwt
  algorithm: RS256
  public_key_path: "/etc/seadesktop/keys/jwt_public.pem"
  private_key_path: "/etc/seadesktop/keys/jwt_private.pem"
```

---

## 12. Token delivery mode

The `token_delivery` field determines how tokens are transmitted between the client and the server.

### Available modes

| Mode | Description | Use case |
|---|---|---|
| `body` (default) | Tokens are returned only in the JSON response. The client is responsible for storing them. | Mobile API, CLI, desktop applications, machine-to-machine clients. |
| `cookie` | Tokens are placed in HttpOnly cookies inaccessible from JavaScript. | Web applications requiring protection against XSS attacks. |
| `both` | Tokens are sent simultaneously in JSON and in cookies. | Migration phases, services accessed by multiple client types. |

### Server-side token lookup

For each incoming request to a protected route, the server searches for the token in this order:

1. HTTP header `Authorization: Bearer <token>`.
2. Cookie with the name configured in `cookie.access_token_name`.

This strategy allows a single service to simultaneously accept clients using either mode.

---

## 13. Cookie configuration

The `cookie:` block (singular) configures the cookies used when `token_delivery` is `cookie` or `both`.

### Full block

```yaml
authentication:
  token_delivery: cookie
  cookie:
    domain: ".example.com"
    path: "/"
    secure: true
    same_site: lax
    access_token_name: sea_access
    refresh_token_name: sea_refresh
```

### Accepted keys

| Key | Type | Default value | Description |
|---|---|---|---|
| `domain` | string | `""` (request origin) | Domain for which the cookie is valid. The `.example.com` notation covers all subdomains. |
| `path` | string | `/` | Path for which the cookie is valid. |
| `secure` | boolean | `true` | If `true`, the cookie is only transmitted over HTTPS. |
| `same_site` | enum | `lax` | SameSite policy. Values: `lax`, `strict`, `none`. |
| `access_token_name` | string | `sea_access` | Name of the cookie carrying the access token. |
| `refresh_token_name` | string | `sea_refresh` | Name of the cookie carrying the refresh token. |

### HttpOnly attribute

The HTTP `HttpOnly` attribute is always `true` and is not configurable. This property prevents JavaScript from accessing cookies, protecting against XSS attacks.

### SameSite policy

| Value | Description | Use case |
|---|---|---|
| `strict` | Cookie sent only for requests originating from the same site. | Maximum protection, may break some OAuth flows. |
| `lax` (default) | Cookie sent for top-level navigations. | Recommended compromise for most cases. |
| `none` | Cookie sent cross-site. Requires `secure: true`. | Authenticated cross-domain API. |

### Development environment

In local development without HTTPS, set `secure: false` to allow cookies to be transmitted over HTTP:

```yaml
cookie:
  secure: false   # local development only
  same_site: lax
```

**This configuration must never be used in production.**

---

## 14. Token tracking (`token_tracking`)

Token tracking adds a centralized session management layer that enables:

- Immediate access token revocation through `/auth/logout`.
- Strict control of refresh tokens through a database allowlist.
- Automatic refresh token rotation at each refresh.

### Full block

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

### Main-level keys

| Key | Type | Default value | Description |
|---|---|---|---|
| `enabled` | boolean | `false` | Enables tracking. If `false`, tokens are purely stateless. |
| `refresh_table` | string | `RefreshToken` | Name of the system table containing valid refresh tokens (allowlist). |
| `revoked_table` | string | `RevokedToken` | Name of the system table containing explicitly revoked access tokens (denylist). |
| `cache` | block | enabled | Configuration for the local denylist cache. |
| `rotation` | block | enabled | Refresh token rotation configuration. |
| `auto_cleanup` | block | enabled | Periodic cleanup configuration. |

### `cache` sub-block

```yaml
cache:
  enabled: true
  ttl: "5m"
  max_size: 10000
```

| Key | Type | Default value | Description |
|---|---|---|---|
| `enabled` | boolean | `true` | Enables the local cache for revoked tokens. |
| `ttl` | duration | `5m` | How long a verification result is cached. |
| `max_size` | integer | `10000` | Maximum number of entries in the cache. |

The cache avoids querying the database for every access token check, significantly improving performance under load.

### `rotation` sub-block

```yaml
rotation:
  enabled: true
```

| Key | Type | Default value | Description |
|---|---|---|---|
| `enabled` | boolean | `true` | If `true`, each `/auth/refresh` call invalidates the old refresh token and issues a new one. |

Refresh token rotation is a security best practice: a stolen refresh token can only be used once before being detected; the second use fails, indicating compromise.

### `auto_cleanup` sub-block

```yaml
auto_cleanup:
  enabled: true
  interval: "1h"
  keep_revoked_for: "30d"
```

| Key | Type | Default value | Description |
|---|---|---|---|
| `enabled` | boolean | `true` | Enables periodic deletion of expired tokens from system tables. |
| `interval` | duration | `1h` | Interval between two cleanup executions. |
| `keep_revoked_for` | duration | `30d` | How long a revoked access token is kept in the denylist after its expiration. |

### Created system tables

When `enabled: true`, two system tables are automatically created when the service starts:

- `RefreshToken`: contains valid refresh tokens currently in circulation. Each entry contains the token `jti`, the user identifier, the expiration date, and the creation date.
- `RevokedToken`: contains explicitly revoked access tokens. Each entry contains the token `jti`, its expiration date, and the revocation date.

These tables are fully managed by the system and do not need to be declared manually in YAML.

### Expected behavior with tracking enabled

| Event | Table action |
|---|---|
| Successful `POST /auth/login` | Insert an entry into `RefreshToken`. |
| Request to a protected route | Verify that the access token is not in `RevokedToken` (with cache). |
| Successful `POST /auth/refresh` | Check in `RefreshToken`. If `rotation.enabled: true`, remove the old token and insert the new one. |
| `POST /auth/logout` | Insert the access token into `RevokedToken`, remove the corresponding refresh token from `RefreshToken`. |
| Periodic task (`auto_cleanup`) | Delete entries whose expiration is older than `keep_revoked_for`. |

---

## 15. Using tokens in requests

### Via the `Authorization` header

For each request to a protected route, pass the access token in the HTTP header:

```
Authorization: Bearer eyJhbGciOiJIUzI1NiIs...
```

Curl example:

```bash
TOKEN=$(curl -s -X POST http://localhost:8081/auth/login \
  -d '{"email":"alice@example.com","password":"..."}' | jq -r .access_token)

curl http://localhost:8081/products \
  -H "Authorization: Bearer $TOKEN"
```

### Via cookies

If `token_delivery` is `cookie` or `both`, the browser automatically sends cookies on each request to the service.

With curl, explicitly pass the cookie:

```bash
curl http://localhost:8081/products \
  --cookie "sea_access=eyJhbGciOiJIUzI1NiIs..."
```

### Responses for invalid tokens

| Case | Returned code |
|---|---|
| No token provided | 401 |
| Malformed token | 401 |
| Invalid signature | 401 |
| Expired token | 401 |
| Revoked token (if tracking is enabled) | 401 |

---

## 16. Duration format

All configuration durations (TTL, cleanup intervals, cache TTL) are declared as strings with a unit suffix.

### Accepted suffixes

| Suffix | Unit |
|---|---|
| `s` or absent | seconds |
| `m` | minutes |
| `h` | hours |
| `d` | days |

### Examples

| YAML value | Resulting duration |
|---|---|
| `"30s"` | 30 seconds |
| `"15m"` | 15 minutes |
| `"24h"` | 24 hours |
| `"7d"` | 7 days |
| `"3600"` | 3600 seconds (1 hour) |

### Typical use cases

| Parameter | Recommended value |
|---|---|
| `access_token_ttl` | `15m` to `1h` |
| `refresh_token_ttl` | `7d` to `30d` |
| `cache.ttl` | `1m` to `5m` |
| `auto_cleanup.interval` | `1h` to `6h` |
| `auto_cleanup.keep_revoked_for` | `30d` (one month) |

---

## 17. Response codes

### Authentication-specific codes

| Code | Affected routes | Cause |
|---|---|---|
| 200 | `/auth/login`, `/auth/refresh`, `/auth/logout`, `/auth/me` | Operation successful. |
| 201 | `/auth/register` | Account created. |
| 400 | All | Validation failed, missing fields. |
| 401 | `/auth/login` | Unknown email or incorrect password. |
| 401 | `/auth/refresh` | Refresh token invalid, expired, or absent from the allowlist. |
| 401 | `/auth/logout`, `/auth/me`, protected routes | Access token missing, invalid, expired, or revoked. |
| 409 | `/auth/register` | Uniqueness constraint violated (email already in use). |

### Error format

```json
{
  "error": "Authentication failed",
  "details": "Invalid email or password"
}
```

---

## 18. Configuration examples

### Configuration 1 — Simple authentication in development

```yaml
security:
  authentication:
    type: jwt
    algorithm: HS256
    secret: ""              # automatically generated and persisted
    access_token_ttl: "1h"
    refresh_token_ttl: "7d"
    token_delivery: body
```

### Configuration 2 — Web application with HttpOnly cookies

```yaml
security:
  authentication:
    type: jwt
    algorithm: HS256
    secret: "${JWT_SECRET}"
    access_token_ttl: "15m"
    refresh_token_ttl: "14d"
    token_delivery: cookie
    cookie:
      domain: ".example.com"
      path: "/"
      secure: true
      same_site: lax
      access_token_name: sea_access
      refresh_token_name: sea_refresh
```

### Configuration 3 — High-security service with full tracking

```yaml
security:
  authentication:
    type: jwt
    algorithm: RS256
    public_key_path: "/etc/seadesktop/keys/jwt_public.pem"
    private_key_path: "/etc/seadesktop/keys/jwt_private.pem"
    issuer: "https://api.example.com"
    audience: "example-api"
    access_token_ttl: "10m"
    refresh_token_ttl: "7d"
    token_delivery: both
    cookie:
      domain: ".example.com"
      path: "/"
      secure: true
      same_site: strict
    token_tracking:
      enabled: true
      cache:
        enabled: true
        ttl: "2m"
        max_size: 50000
      rotation:
        enabled: true
      auto_cleanup:
        enabled: true
        interval: "30m"
        keep_revoked_for: "90d"
```

### Configuration 4 — Mobile API with long-lived tokens

```yaml
security:
  authentication:
    type: jwt
    algorithm: HS256
    secret: "${JWT_SECRET}"
    access_token_ttl: "1h"
    refresh_token_ttl: "90d"
    token_delivery: body
    token_tracking:
      enabled: true
      rotation:
        enabled: true
      auto_cleanup:
        enabled: true
        interval: "6h"
```

### Configuration 5 — Disabling authentication

For an internal or development service without authentication:

```yaml
security:
  authentication:
    type: none
```

No `/auth/*` route is exposed. CRUD routes remain accessible without authentication.
