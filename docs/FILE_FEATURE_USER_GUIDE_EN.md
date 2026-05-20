# SeaDesktop — `file` Field Type

User guide for the `file` field type in SeaDesktop YAML schemas. This document explains how to declare file fields, configure storage, understand the associated system behavior, and interact with files through the HTTP API.

---

## Table of Contents

1. [Minimal Declaration](#1-minimal-declaration)
2. [Service-Level Storage Configuration](#2-service-level-storage-configuration)
3. [Configuring a `file` Field](#3-configuring-a-file-field)
4. [Delete Strategies (`on_delete`)](#4-delete-strategies-on_delete)
5. [Uploading a File](#5-uploading-a-file)
6. [Downloading a File](#6-downloading-a-file)
7. [Updating and Detaching a File](#7-updating-and-detaching-a-file)
8. [Deleting an Entity That References a File](#8-deleting-an-entity-that-references-a-file)
9. [Sharing a File Between Multiple Entities](#9-sharing-a-file-between-multiple-entities)
10. [Response Codes and Error Handling](#10-response-codes-and-error-handling)
11. [Common Configuration Examples](#11-common-configuration-examples)

---

## 1. Minimal Declaration

The simplest configuration for enabling file management on an entity is shown below:

```yaml
project:
  name: DocumentApplication

services:
  - name: MainService
    port: 8080

    database:
      type: mysql
      host: localhost
      port: 3306
      database_name: app
      username: root
      password: rootpassword

    entities:
      - name: Photo
        fields:
          - name: id
            type: uuid
          - name: image
            type: file
            file:
              storage_path: photos
              on_delete: cascade
```

When the service starts, the system automatically performs the following actions:

- Creates the `Photo` table in the database
- Creates the `sea_files` system table, which stores file metadata
- Creates the `./uploads/photos/` directory on disk
- Generates the HTTP routes: `POST /photos`, `GET /photos/image/{id}`, `PUT /photos/{id}`, `DELETE /photos/{id}`

Declaring the `storage:` block at service level is optional. If it is missing, the system applies default values, including the default root directory `./uploads`.

---

## 2. Service-Level Storage Configuration

To customize the file location and permissions, declare a `storage:` block at service level.

```yaml
services:
  - name: MainService
    storage:
      backend: filesystem
      root_path: /var/lib/application/uploads
      file_mode: "0640"
      directory_mode: "0750"
```

| Key | Expected value | Default value | Description |
|---|---|---|---|
| `backend` | `filesystem` | `filesystem` | Storage backend type. Currently, only `filesystem` is supported. |
| `root_path` | path | `./uploads` | Root directory under which all files are organized. |
| `file_mode` | octal string | `"0640"` | Unix permissions applied to created files. |
| `directory_mode` | octal string | `"0750"` | Unix permissions applied to created directories. |

### Note About Octal Modes

Unix modes must be declared as strings, such as `"0640"`, not as integers such as `0640`. YAML may interpret `0640` as the decimal value `640`, which does not represent valid Unix permissions. Quoting the value forces a literal interpretation, which the parser then converts from base 8.

### Behavior When the Block Is Omitted

When a schema entity declares a field of type `file` but the service-level `storage:` block is missing, the system applies the following fallback values:

- `backend`: `filesystem`
- `root_path`: `./uploads`

A warning is logged during startup to indicate that the fallback is being used. An explicit configuration is recommended in production environments.

---

## 3. Configuring a `file` Field

A field accepts files when its type is set to `file` and a `file:` sub-block defines the applicable constraints.

```yaml
fields:
  - name: avatar
    type: file
    required: false
    file:
      max_size: 5MB
      allowed_mime_types:
        - image/png
        - image/jpeg
      allowed_extensions:
        - .png
        - .jpg
        - .jpeg
      storage_path: users/avatars
      on_delete: cascade
```

| Key | Type | Required | Description |
|---|---|---|---|
| `max_size` | size string | No | Maximum accepted size. Supported formats: `500KB`, `5MB`, `1GB`. If this constraint is omitted, only the server's global limit applies. |
| `allowed_mime_types` | list of strings | No | Allowlist of accepted MIME types. If missing or empty, all MIME types are accepted. |
| `allowed_extensions` | list of strings | No | Allowlist of accepted extensions, including the dot. Comparison is case-insensitive. If missing, all extensions are accepted. |
| `storage_path` | string | **Yes** | Subdirectory, relative to `root_path`, where files for this field are stored. |
| `on_delete` | enum | **Yes** | Strategy applied when the parent entity is deleted. Values: `cascade`, `set_null`, `restrict`. |

### Constraints on `storage_path`

The `storage_path` value must follow these rules:

- Relative path only; a leading `/` is forbidden
- No `..` sequence; directory traversal is forbidden
- No empty path segment; for example, `users//avatars` is rejected

The directory is created automatically at startup if it does not exist.

### Accepted Size Formats

| Notation | Value in bytes |
|---|---:|
| `100` | 100 |
| `500B` | 500 |
| `2KB` | 2,048 |
| `5MB` | 5,242,880 |
| `1GB` | 1,073,741,824 |

---

## 4. Delete Strategies (`on_delete`)

The `on_delete` strategy defines how the system handles the physical file when the parent entity is deleted or modified. Three strategies are available.

### `cascade` Strategy

The file is deleted from disk when the parent entity is deleted, provided that no other entity references it.

```yaml
- name: avatar
  type: file
  file:
    storage_path: users/avatars
    on_delete: cascade
```

**Recommended use:** profile avatars, thumbnails, and data whose lifetime is strictly tied to the parent entity.

**Behavior:**

- Entity deletion: the file is deleted from disk, if it is not shared
- Update replacing the file: the old file is deleted from disk, if it is not shared

### `set_null` Strategy

The file is kept on disk when the parent entity is deleted, even if it becomes orphaned and is no longer referenced by any entity.

```yaml
- name: banner
  type: file
  file:
    storage_path: articles/banners
    on_delete: set_null
```

**Recommended use:** article illustrations to archive, attachments to keep for audit or traceability purposes.

**Behavior:**

- Entity deletion: the file is kept on disk
- Update replacing the file: the old file is kept on disk

Orphaned files are not automatically cleaned up by the system. If disk space recovery is required, a manual or scheduled cleanup process must be implemented.

### `restrict` Strategy

Deleting the entity is rejected as long as a file remains attached to the field. The system returns a `409 Conflict` response. To delete the entity, the file must first be detached.

```yaml
- name: pdf
  type: file
  file:
    storage_path: contracts/pdfs
    on_delete: restrict
```

**Recommended use:** signed contracts, invoices, legal documents, and any data where accidental loss must be prevented.

**Behavior:**

- Entity deletion: rejected with `409 Conflict` if a file is attached
- Update detaching the file by assigning `null`: allowed
- Full deletion procedure: detach the file with a `PUT` request, then delete the entity with `DELETE`

---

## 5. Uploading a File

The system accepts three file upload modes. The appropriate mode depends on the client constraints.

### Mode 1 — `multipart/form-data` Request

Recommended for clients that have a file on disk, such as HTML forms or command-line tools.

```bash
curl -X POST http://localhost:8080/users \
  -F "email=alice@example.com" \
  -F "name=Alice" \
  -F "avatar=@./photo.png;type=image/png"
```

Example response:

```json
{
  "id": "550e8400-e29b-41d4-a716-446655440000",
  "email": "alice@example.com",
  "name": "Alice",
  "avatar": "660f9511-f30c-52e5-b827-557766551111"
}
```

The `avatar` field value in the response is the unique identifier assigned to the file by the system, not the binary content.

### Mode 2 — JSON Request With Base64-Encoded Content

Suitable for clients that must send JSON-only requests, such as mobile applications or single-page web applications.

```bash
AVATAR=$(base64 -w0 photo.png)
curl -X POST http://localhost:8080/users \
  -H "Content-Type: application/json" \
  -d "{
    \"email\": \"alice@example.com\",
    \"avatar\": {
      \"filename\": \"photo.png\",
      \"mime_type\": \"image/png\",
      \"content_base64\": \"$AVATAR\"
    }
  }"
```

### Mode 3 — Reference to an Existing File by UUID

When the identifier of an existing file is known, it can be assigned directly to a new entity, avoiding another upload.

```bash
curl -X POST http://localhost:8080/users \
  -H "Content-Type: application/json" \
  -d '{
    "email": "bob@example.com",
    "avatar": "660f9511-f30c-52e5-b827-557766551111"
  }'
```

Use cases for this mode are detailed in the section [Sharing a File Between Multiple Entities](#9-sharing-a-file-between-multiple-entities).

---

## 6. Downloading a File

A download route is automatically generated for each field of type `file` using the following format:

```text
GET /<plural entity>/<field name>/<entity id>
```

Mapping between declaration and generated route:

| Declared field | Generated route |
|---|---|
| `User.avatar` | `GET /users/avatar/{id}` |
| `Article.banner` | `GET /articles/banner/{id}` |
| `Contract.pdf` | `GET /contracts/pdf/{id}` |

Example download:

```bash
curl -o downloaded.png http://localhost:8080/users/avatar/550e8400-e29b-41d4-a716-446655440000
```

The response includes:

- The binary file content
- A `Content-Type` header matching the MIME type recorded during upload
- A `Content-Disposition: inline; filename="<original_name>"` header, allowing browsers to display images and PDF documents directly

Opening the URL in a browser allows direct file preview without requiring a separate download step.

---

## 7. Updating and Detaching a File

### Replacing a File

```bash
curl -X PUT http://localhost:8080/users/550e8400-... \
  -F "email=alice@example.com" \
  -F "name=Alice" \
  -F "avatar=@./new_photo.png;type=image/png"
```

Operation flow:

1. The new file is stored and receives a new identifier.
2. The entity is updated with this new identifier.
3. The old file is handled according to the field's `on_delete` strategy:
   - `cascade`: the old file is deleted from disk if it is no longer referenced.
   - `set_null`: the old file is kept on disk.
   - `restrict`: the old file is dereferenced; the restriction only applies when deleting the entity.

### Detaching a File

```bash
curl -X PUT http://localhost:8080/users/550e8400-... \
  -H "Content-Type: application/json" \
  -d '{
    "email": "alice@example.com",
    "name": "Alice",
    "avatar": null
  }'
```

The entity no longer references any file on this field. The previous file is handled according to the same rules as file replacement.

---

## 8. Deleting an Entity That References a File

```bash
curl -X DELETE http://localhost:8080/users/550e8400-...
```

The result depends on the configured `on_delete` strategy:

| Strategy | Result |
|---|---|
| `cascade` | Entity deleted. File deleted from disk if it is no longer referenced. Response `200`. |
| `set_null` | Entity deleted. File kept on disk; it becomes orphaned if it is not shared. Response `200`. |
| `restrict` | Deletion rejected if a file remains attached. Response `409 Conflict`. Prior detachment is required. |

### Deletion Procedure With `restrict`

```bash
# Direct deletion attempt: rejected
curl -X DELETE http://localhost:8080/contracts/550e8400-...

# Response: 409 Conflict
# {
#   "error": "Conflict",
#   "message": "The entity 'Contract' cannot be deleted:
#               file field 'pdf' has on_delete=restrict.
#               Detach or replace the file before deletion."
# }

# Step 1: detach the file
curl -X PUT http://localhost:8080/contracts/550e8400-... \
  -H "Content-Type: application/json" \
  -d '{"reference": "CTR-001", "pdf": null}'

# Step 2: delete the entity, now allowed
curl -X DELETE http://localhost:8080/contracts/550e8400-...
```

---

## 9. Sharing a File Between Multiple Entities

Multiple entities can reference the same physical file. Typical use cases include:

- An avatar shared between several user accounts
- An image reused in several articles
- A document template reused by several entities

### Sharing Implementation

```bash
# 1. Initial file upload
RESPONSE=$(curl -s -X POST http://localhost:8080/users \
  -F "email=alice@example.com" \
  -F "avatar=@./shared.png;type=image/png")
SHARED_UUID=$(echo $RESPONSE | jq -r '.avatar')

# 2. Reference the same file from another entity
curl -X POST http://localhost:8080/users \
  -H "Content-Type: application/json" \
  -d "{
    \"email\": \"bob@example.com\",
    \"avatar\": \"$SHARED_UUID\"
  }"
```

### Reference Counting Mechanism

The system maintains an internal counter representing the number of entities referencing each file.

- Each new reference increments the counter.
- Each removed reference decrements the counter.
- The physical file is deleted only when the counter reaches zero and the applicable strategy is `cascade`.

### Illustration

```bash
# Create Alice's entity with the file       → counter = 1
curl -X POST .../users -F email=alice@... -F avatar=@photo.png

# Create Bob's entity with the same file    → counter = 2
curl -X POST .../users -d '{"email":"bob@...", "avatar":"<UUID>"}'

# Delete Alice's entity (cascade)           → counter = 1
# The file remains on disk because Bob still references it
curl -X DELETE .../users/<alice_id>

# Delete Bob's entity (cascade)             → counter = 0
# The file is deleted from disk
curl -X DELETE .../users/<bob_id>
```

This mechanism ensures that no single entity can accidentally delete a file that is still shared by another entity.

---

## 10. Response Codes and Error Handling

### Upload Operation

| Code | Description |
|---|---|
| `201 Created` | Resource created successfully. |
| `400 Bad Request` | File exceeds maximum size, MIME type not allowed, extension not allowed, invalid JSON, or missing required field. |
| `500 Internal Server Error` | Disk write failure or database insertion failure. |

Examples of `400` error messages:

```json
{"error": "File too large (10485760 bytes; max 5242880)."}
{"error": "MIME type rejected: 'application/pdf'."}
{"error": "Extension rejected: '.exe'."}
```

### Download Operation

| Code | Description |
|---|---|
| `200 OK` | Binary content returned. |
| `400 Bad Request` | Missing `id` parameter in the URL. |
| `403 Forbidden` | Access policy denies reading the parent entity, when authentication is enabled. |
| `404 Not Found` | Unknown entity, missing record, empty field, or physical file missing from disk. |
| `500 Internal Server Error` | Input/output error. |

### Delete Operation

| Code | Description |
|---|---|
| `200 OK` | Deletion completed. |
| `404 Not Found` | Record not found. |
| `409 Conflict` | The `on_delete: restrict` strategy prevents deletion because a file remains attached. |

---

## 11. Common Configuration Examples

### Configuration 1 — User Profile Avatar

```yaml
- name: User
  fields:
    - name: id
      type: uuid
    - name: email
      type: email
      unique: true
    - name: avatar
      type: file
      required: false
      file:
        max_size: 2MB
        allowed_mime_types: [image/png, image/jpeg, image/webp]
        storage_path: users/avatars
        on_delete: cascade
```

Generated routes:

- `POST /users` — create with optional avatar
- `PUT /users/{id}` — update with avatar replacement
- `DELETE /users/{id}` — delete and remove avatar
- `GET /users/avatar/{id}` — download avatar

### Configuration 2 — Article Illustration With Archiving

```yaml
- name: Article
  fields:
    - name: id
      type: uuid
    - name: title
      type: string
    - name: banner
      type: file
      required: false
      file:
        max_size: 10MB
        allowed_mime_types: [image/png, image/jpeg]
        storage_path: articles/banners
        on_delete: set_null
```

The `set_null` strategy keeps the image when the article is deleted, for archiving or future reuse.

### Configuration 3 — Protected Legal Document

```yaml
- name: Contract
  fields:
    - name: id
      type: uuid
    - name: reference
      type: string
      unique: true
    - name: pdf
      type: file
      required: true
      file:
        max_size: 20MB
        allowed_mime_types: [application/pdf]
        allowed_extensions: [.pdf]
        storage_path: contracts
        on_delete: restrict
```

The `restrict` strategy prevents the contract from being deleted while a document is attached, reducing the risk of accidental data loss.

### Configuration 4 — Entity With Multiple File Fields

```yaml
- name: Product
  fields:
    - name: id
      type: uuid
    - name: name
      type: string
    - name: thumbnail
      type: file
      file:
        max_size: 500KB
        allowed_mime_types: [image/png, image/jpeg]
        storage_path: products/thumbs
        on_delete: cascade
    - name: detail_image
      type: file
      file:
        max_size: 5MB
        allowed_mime_types: [image/png, image/jpeg]
        storage_path: products/details
        on_delete: cascade
    - name: spec_sheet
      type: file
      file:
        max_size: 10MB
        allowed_mime_types: [application/pdf]
        storage_path: products/specs
        on_delete: set_null
```

Three distinct download routes are generated:

- `GET /products/thumbnail/{id}`
- `GET /products/detail_image/{id}`
- `GET /products/spec_sheet/{id}`

---

## Summary

Implementing a `file` field requires the following steps:

1. **Declare the field**: use `type: file` with a `file:` sub-block containing at least `storage_path` and `on_delete`.
2. **Configure storage** (optional): declare a service-level `storage:` block to customize the root directory and Unix permissions.
3. **Choose a delete strategy**: use `cascade` for a lifecycle tied to the entity, `set_null` for systematic preservation, or `restrict` to prevent accidental deletion.
4. **Upload files**: choose one of three modes depending on the client constraints: multipart, JSON with base64, or existing UUID reference.
5. **Download files**: use the automatically generated routes following the format `/<entities>/<field>/{id}`.
6. **Manage lifecycle**: update and delete operations automatically apply the configured `on_delete` strategy.
7. **Share files between entities**: reference an existing file identifier directly; the system transparently manages reference counting.
