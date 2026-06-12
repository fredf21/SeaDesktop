# HTTP Errors

All errors returned by the SeaDesktop API follow a unified JSON schema,
accompanied by an appropriate HTTP status code and a semantic error
code enabling reliable client-side dispatch.

## Response schema

Every error response (status >= 400) has the following shape:

```json
{
  "success": false,
  "error": {
    "code": "VALIDATION_ERROR",
    "message": "Invalid email address."
  }
}
```

- `success` is always `false` for an error response.
- `error.code` is an UPPERCASE, stable string identifying the error
  category. This is the field clients should use to route their
  behavior.
- `error.message` is a human-readable message intended for display to
  end users or logging. Its content may evolve without notice and must
  not be parsed.

Success responses (2xx status codes) do not follow this schema: they
return the requested resource directly or a minimal message depending
on the endpoint.

## Error codes

| HTTP Status | Error Code             | Meaning                                                     |
|-------------|------------------------|-------------------------------------------------------------|
| 400         | `BAD_REQUEST`          | Malformed request: missing parameter, invalid JSON, etc.    |
| 400         | `VALIDATION_ERROR`     | The content is syntactically correct but semantically invalid (malformed email, missing required field, etc.). |
| 401         | `AUTHENTICATION_ERROR` | Missing, invalid, or expired authentication token, or incorrect credentials. |
| 403         | `AUTHORIZATION_ERROR`  | The user is authenticated but not allowed to perform the requested operation. |
| 404         | `NOT_FOUND`            | The requested resource does not exist.                      |
| 409         | `CONFLICT`             | The operation conflicts with the current state (duplicate, foreign key constraint, locked resource). |
| 429         | `RATE_LIMIT_EXCEEDED`  | Too many requests within a given time window.               |
| 500         | `INTERNAL_SERVER_ERROR`| Internal server error. The details are logged server-side only. |

### Distinction between `BAD_REQUEST` and `VALIDATION_ERROR`

`BAD_REQUEST` indicates a **format** problem: a required field is
missing, the JSON is malformed, a URL parameter is absent.

`VALIDATION_ERROR` indicates a **content** problem: all expected fields
are present but their value does not satisfy business rules (invalid
email format, insufficient length, unauthorized value for an enum,
etc.).

In both cases, the HTTP status is `400`. Clients can use the error code
to differentiate the cause and adapt their display (generic message for
`BAD_REQUEST`, field-level highlighting for `VALIDATION_ERROR`).

## Examples

### User creation with missing email

```http
POST /api/auth/register HTTP/1.1
Content-Type: application/json

{
  "password": "secret123"
}
```

Response:

```http
HTTP/1.1 400 Bad Request
Content-Type: application/json

{
  "success": false,
  "error": {
    "code": "VALIDATION_ERROR",
    "message": "Email field is missing."
  }
}
```

### Login with invalid credentials

```http
POST /api/auth/login HTTP/1.1
Content-Type: application/json

{
  "email": "user@example.com",
  "password": "wrong_password"
}
```

Response:

```http
HTTP/1.1 401 Unauthorized
Content-Type: application/json

{
  "success": false,
  "error": {
    "code": "AUTHENTICATION_ERROR",
    "message": "Invalid credentials."
  }
}
```

The message is identical whether the email exists or not, and whether
the password is incorrect or the user is not found. This avoids
disclosing the list of existing accounts to an attacker.

### Accessing another user's resource (ABAC)

```http
GET /api/projects/abc-123 HTTP/1.1
Authorization: Bearer eyJ...
```

Response when the policy denies access:

```http
HTTP/1.1 403 Forbidden
Content-Type: application/json

{
  "success": false,
  "error": {
    "code": "AUTHORIZATION_ERROR",
    "message": "The project does not belong to the current user."
  }
}
```

The message may be more specific depending on the ABAC rule that
applied. If the rule provides no reason, the default message is
`"Access denied."`.

### Reading a non-existent resource

```http
GET /api/projects/does-not-exist HTTP/1.1
```

Response:

```http
HTTP/1.1 404 Not Found
Content-Type: application/json

{
  "success": false,
  "error": {
    "code": "NOT_FOUND",
    "message": "Record not found."
  }
}
```

### Creating with an already-used email

```http
POST /api/auth/register HTTP/1.1
Content-Type: application/json

{
  "email": "existing@example.com",
  "password": "secret123"
}
```

Response:

```http
HTTP/1.1 409 Conflict
Content-Type: application/json

{
  "success": false,
  "error": {
    "code": "CONFLICT",
    "message": "This email already exists."
  }
}
```

### Deleting a referenced resource

If a `Team` contains `Project`s with an `on_delete: restrict` rule on
the foreign key, deleting the `Team` is refused:

```http
DELETE /api/teams/team-123 HTTP/1.1
```

Response:

```http
HTTP/1.1 409 Conflict
Content-Type: application/json

{
  "success": false,
  "error": {
    "code": "CONFLICT",
    "message": "Delete refused: entity is referenced (table 'projects' references this row)."
  }
}
```

### Creation with multiple validation errors

When multiple errors are detected at once, the messages are
concatenated and separated by `; `:

```http
HTTP/1.1 400 Bad Request
Content-Type: application/json

{
  "success": false,
  "error": {
    "code": "VALIDATION_ERROR",
    "message": "Field 'name' is required; Field 'description' exceeds 500 characters"
  }
}
```

### Rate limit exceeded

```http
HTTP/1.1 429 Too Many Requests
Content-Type: application/json
X-RateLimit-Limit: 100
X-RateLimit-Remaining: 0
X-RateLimit-Reset: 1721392800
Retry-After: 30

{
  "success": false,
  "error": {
    "code": "RATE_LIMIT_EXCEEDED",
    "message": "Too many requests. Try again later."
  }
}
```

The `X-RateLimit-*` and `Retry-After` headers indicate when to retry.

### Internal error

```http
HTTP/1.1 500 Internal Server Error
Content-Type: application/json

{
  "success": false,
  "error": {
    "code": "INTERNAL_SERVER_ERROR",
    "message": "Internal server error"
  }
}
```

The message is intentionally generic and identical for all internal
errors. The technical details (trace, exception message, operation
context) are logged on the server only, accessible via the
`/api/logs` endpoint to administrators.

## Client-side handling

### Dispatch by error code

Clients should rely on `error.code` (not on the message or HTTP status
alone) to decide their behavior:

```javascript
async function callApi(url, options) {
  const response = await fetch(url, options);
  const body = await response.json();

  if (!response.ok) {
    switch (body.error.code) {
      case 'AUTHENTICATION_ERROR':
        redirectToLogin();
        break;
      case 'AUTHORIZATION_ERROR':
        showForbiddenPage(body.error.message);
        break;
      case 'VALIDATION_ERROR':
        showFormErrors(body.error.message);
        break;
      case 'NOT_FOUND':
        show404();
        break;
      case 'CONFLICT':
        showConflictDialog(body.error.message);
        break;
      case 'RATE_LIMIT_EXCEEDED':
        const retryAfter = response.headers.get('Retry-After');
        retryLater(retryAfter);
        break;
      case 'INTERNAL_SERVER_ERROR':
        showGenericError();
        reportToSupport();
        break;
      default:
        showGenericError(body.error.message);
    }
    throw new ApiError(body.error.code, body.error.message);
  }

  return body;
}
```

### Message display

The `error.message` field may contain translated text. It is suitable
for direct display to end users, but must not be parsed or used to make
logical decisions. Use `error.code` for that.

### Distinguishing network errors from API errors

A missing response, timeout, or HTTP status without a valid JSON body
indicates a network or server error. The schema documented here only
applies to responses with `Content-Type: application/json` containing
an object with `success: false`.

## Stability contract

- The **codes** (`BAD_REQUEST`, `VALIDATION_ERROR`, etc.) are stable
  and will not be renamed or removed without a major version.
- New codes may be added. Clients must have a default branch
  (`default:`) for unknown codes.
- **Messages** are subject to change (typo fixes, refinements,
  translations) without notice.
- The **HTTP status** associated with each `error.code` is stable.
