# SeaDesktop — Complete User Guide

Reference documentation for writing a SeaDesktop YAML configuration file. This guide describes each section, each key, its allowed values, its default behavior, and the expected system response for each configuration. The information presented here comes from the project source code and was verified by directly reading the YAML parsers.

---

## Table of contents

1. [Fundamental concepts](#1-fundamental-concepts)
2. [YAML file structure](#2-yaml-file-structure)
3. [`database` section](#3-database-section)
4. [`entities` section](#4-entities-section)
5. [Fields](#5-fields)
6. [Field types](#6-field-types)
7. [Common field attributes](#7-common-field-attributes)
8. [`password` field](#8-password-field)
9. [`email` field](#9-email-field)
10. [`file` field](#10-file-field)
11. [`native` field](#11-native-field)
12. [Entity `options` section](#12-entity-options-section)
13. [`relations` section](#13-relations-section)
14. [`pagination` section](#14-pagination-section)
15. [Entity `seeds` section](#15-entity-seeds-section)
16. [`security.authentication` section](#16-securityauthentication-section)
17. [Cookies and token delivery](#17-cookies-and-token-delivery)
18. [Token tracking](#18-token-tracking)
19. [`security.authorization` section](#19-securityauthorization-section)
20. [Per-entity `access_control` rules](#20-per-entity-access_control-rules)
21. [`security.cors` section](#21-securitycors-section)
22. [`security.rate_limits` section](#22-securityrate_limits-section)
23. [`security.security_headers` section](#23-securitysecurity_headers-section)
24. [`security.http_limits` section](#24-securityhttp_limits-section)
25. [`storage` section](#25-storage-section)
26. [`logging` section](#26-logging-section)
27. [Generated system endpoints](#27-generated-system-endpoints)
28. [HTTP response codes](#28-http-response-codes)
29. [Additional documentation](#29-additional-documentation)

---

## 1. Fundamental concepts

### What is SeaDesktop?

SeaDesktop is a declarative platform that automatically generates a REST API from a YAML file. The user does not write application code. The entire configuration lives in the YAML file, which acts as the single source of truth for the exposed API.

### What is a project?

A **project** is the top-level unit in the YAML file. It has a name and contains one or more service declarations. A YAML file describes exactly one project.

### What is a service?

A **service** is an independent application that runs separately from the other services in the project. Each service listens on its own port, has its own database, its own entities, and its own security configuration.

At startup, the user specifies which service to run among those declared in the project:

```text
./backend --config=./config.yaml --service_name=ServiceName
```

### What is an entity?

An **entity** represents a business concept persisted in the database. Each declared entity automatically produces:

- A database table
- Five REST CRUD routes: `GET /<entity>`, `GET /<entity>/{id}`, `POST /<entity>`, `PUT /<entity>/{id}`, `DELETE /<entity>/{id}`
- An OpenAPI documentation entry available at `/docs`
- Validation and authorization rules declared in the YAML file

### What is a field?

A **field** is an attribute of an entity. It corresponds to a column in the associated table. Each field has a name, a type, and constraint attributes such as required, unique, indexed, default value, and so on.

### What is a relation?

A **relation** declares a link between two entities. Four kinds are supported: `belongs_to`, `has_many`, `has_one`, and `many_to_many`. Declaring a relation generates additional REST routes used to navigate between entities.

### Naming convention

| Element | Convention | Example |
|---|---|---|
| Entity name | PascalCase, singular | `User`, `Product`, `OrderLine` |
| Table name | Automatically calculated: lowercase plural | `User` → `users`, `Category` → `categories` |
| Route name | Same as the table name | `/users`, `/categories` |
| Field name | snake_case | `email`, `created_at`, `user_id` |

---

## 2. YAML file structure

### General hierarchy

```yaml
project:
  name: ProjectName

services:
  - name: FirstService
    port: 8081
    database: { ... }
    security: { ... }
    entities: [ ... ]
    logging: { ... }
    storage: { ... }

  - name: SecondService
    port: 8082
    # ... separate configuration ...
```

### `project` block

| Key | Type | Required | Description |
|---|---|---|---|
| `name` | string | **Yes** | Project name. Used in logs and OpenAPI documentation. |

### `services` list

List of services to expose. Each service is independent. Only one service is launched per backend execution, selected through `--service_name`.

### Service sections

| Section | Type | Required | Description |
|---|---|---|---|
| `name` | string | **Yes** | Service identifier. |
| `port` | integer | **Yes** | HTTP listening port. |
| `database` | block | **Yes** | Database configuration. See [section 3](#3-database-section). |
| `entities` | list | **Yes** | Business entity definitions. See [section 4](#4-entities-section). |
| `security` | block | No | Authentication, authorization, CORS, rate limits, headers. |
| `storage` | block | No | File storage configuration. See [section 25](#25-storage-section). |
| `logging` | block | No | Logging configuration. See [section 26](#26-logging-section). |

The order of the sections inside a service does not matter.

### Minimal complete example

The simplest YAML file exposing a functional REST API:

```yaml
project:
  name: MinimalDemo

services:
  - name: MainService
    port: 8081

    database:
      type: mysql
      host: localhost
      port: 3306
      database_name: demo_db
      username: root
      password: rootpassword

    entities:
      - name: Product
        fields:
          - name: id
            type: uuid
          - name: name
            type: string
            required: true
          - name: price
            type: float
```

At startup, this configuration produces:

- Creation of the `demo_db` database if it does not exist, depending on `migrations.create_database_if_missing`
- Creation of the `products` table with three columns
- Exposure of the five standard CRUD routes
- OpenAPI documentation at `/docs`

---

## 3. `database` section

Configures the database connection and the behavior of automatic migrations.

### Accepted keys

```yaml
database:
  type: mysql
  host: localhost
  port: 3306
  database_name: my_application
  username: root
  password: rootpassword
  migrations:
    enabled: true
    create_database_if_missing: true
    mode: modified
    dry_run: false
    seeds:
      enabled: true
      mode: once
      on_error: continue
```

| Key | Type | Default value | Description |
|---|---|---|---|
| `type` | enum | `memory` | Database engine. Values: `mysql`, `postgres`, `mongo`, `memory`. Only `mysql` is fully supported. |
| `host` | string | `localhost` | Database server address. |
| `port` | integer | `0` | Database server listening port. For MySQL, the usual value is `3306`. |
| `database_name` | string | `""` | Database name. |
| `username` | string | `""` | Login username. |
| `password` | string | `""` | Login password. |
| `migrations` | block | see below | Migration configuration. |

### `migrations` block

| Key | Type | Default value | Description |
|---|---|---|---|
| `enabled` | boolean | `false` | Enables automatic migrations at startup. |
| `mode` | enum | `conservative` | Safety level. Values: `conservative`, `modified`, `aggressive`. |
| `create_database_if_missing` | boolean | `true` | Creates the database if it does not exist. |
| `dry_run` | boolean | `false` | If `true`, prints SQL without executing it. |
| `seeds` | block | see [section 15](#15-entity-seeds-section) | Seed configuration. |

### Migration modes

The mode controls which schema changes the system is allowed to apply:

| Mode | Allowed operations | Refused operations |
|---|---|---|
| `conservative` | CREATE TABLE, ADD COLUMN, ADD INDEX | MODIFY COLUMN, DROP COLUMN, DROP INDEX, RENAME COLUMN |
| `modified` | Conservative + compatible MODIFY COLUMN, ADD UNIQUE, RENAME COLUMN | DROP COLUMN, DROP INDEX |
| `aggressive` | All operations, including DROP COLUMN and DROP TABLE | none |

The `aggressive` mode can cause data loss and is not recommended in production.

### Expected startup behavior

1. The system attempts to connect to the database using the provided parameters.
2. If `create_database_if_missing: true` and the database does not exist, the system creates it.
3. The current schema is introspected: tables, columns, indexes.
4. The current schema is compared with the schema declared in YAML.
5. Required modifications are identified and filtered according to `mode`.
6. Allowed modifications are applied in order.
7. A detailed report is logged.

If a migration fails, a warning is logged and the service still attempts to start.

---

## 4. `entities` section

The `entities` section is a list of entity definitions. Each entity produces a database table and a set of REST routes.

### Entity structure

```yaml
entities:
  - name: User
    options:
      enable_crud: true
      timestamps: true
      is_auth_source: false
      soft_delete: false
      public_routes: false
    fields:
      - name: id
        type: uuid
      - name: email
        type: email
        required: true
        unique: true
    relations:
      - name: posts
        target_entity: Post
        kind: has_many
        fk_column: user_id
    pagination: { ... }
    access_control: { ... }
    seeds: [ ... ]
```

### Accepted keys

| Key | Type | Required | Description |
|---|---|---|---|
| `name` | string | **Yes** | Entity name. PascalCase is recommended. |
| `fields` | list | **Yes** | Field list. See [section 5](#5-fields). |
| `options` | block | No | Entity options. See [section 12](#12-entity-options-section). |
| `relations` | list | No | Relations to other entities. See [section 13](#13-relations-section). |
| `pagination` | block | No | Pagination. See [section 14](#14-pagination-section). |
| `access_control` | block | No | Access rules. See [section 20](#20-per-entity-access_control-rules). |
| `seeds` | list | No | Initial data. See [section 15](#15-entity-seeds-section). |

### Automatically generated routes

For each entity with `enable_crud: true`, which is the default:

| Method | Route | Success code | Description |
|---|---|---|---|
| `GET` | `/<entity>` | 200 | Lists records. |
| `GET` | `/<entity>/{id}` | 200 | Retrieves a record by identifier. |
| `POST` | `/<entity>` | 201 | Creates a new record. |
| `PUT` | `/<entity>/{id}` | 200 | Updates a record. |
| `DELETE` | `/<entity>/{id}` | 204 | Deletes a record. |

The `<entity>` segment is automatically calculated: lowercase English plural name.

---

## 5. Fields

A field represents a database column and an attribute in JSON responses.

### Field anatomy

```yaml
- name: email
  type: email
  required: true
  unique: true
  indexed: true
  max_length: 255
  default: ""
  serializable: true
  unsigned_value: false
  previous_name: old_email
```

### Full list of accepted keys

| Key | Type | Default value | Description |
|---|---|---|---|
| `name` | string | — | Field name. **Required.** |
| `type` | enum | — | Data type. **Required.** See [section 6](#6-field-types). |
| `required` | boolean | `true` | If `true`, the column is `NOT NULL` and validation rejects null values on insert. |
| `unique` | boolean | `false` | If `true`, adds a uniqueness constraint in the database. |
| `indexed` | boolean | `false` | If `true`, creates an index on this column. |
| `serializable` | boolean | `true` | If `false`, the field is excluded from JSON GET responses. |
| `unsigned_value` | boolean | `false` | For numeric types, marks the column as `UNSIGNED`. |
| `max_length` | integer | absent | Maximum length for `string` or `text`. |
| `min_value` | number | absent | Minimum bound for numeric types. Checked during validation. |
| `max_value` | number | absent | Maximum bound for numeric types. Checked during validation. |
| `default` | any type | absent | Default value applied when the field is omitted on insert. |
| `previous_name` | string | absent | Previous column name used to detect renames during migration. |
| `file` | block | absent | File configuration. **Required if `type: file`.** See [section 10](#10-file-field). |
| `native` | block | absent | Database-specific native type. **Required if `type: native`.** See [section 11](#11-native-field). |

### Special case: `default` is forbidden for some types

The `binary` and `file` types do not support the `default` attribute. The system rejects the configuration with a parsing error if a `default` is declared on a field of one of these types.

### Special case: nullability

By default, `required: true`. This differs from standard SQL, where columns are nullable by default. To make a column nullable, explicitly declare `required: false`.

```yaml
- name: deleted_at
  type: timestamp
  required: false
```

---

## 6. Field types

Full list of accepted values for a field's `type` key.

### Simple types

| Type | Description | MySQL storage |
|---|---|---|
| `string` | Short character string. Length configurable through `max_length`. | `VARCHAR(n)` |
| `text` | Long character string. | `TEXT` |
| `int` | Signed integer. | `INT` |
| `bigint` | Signed 64-bit integer. | `BIGINT` |
| `smallint` | Signed 16-bit integer. | `SMALLINT` |
| `float` | Floating-point number. | `FLOAT` |
| `decimal` | Fixed-precision number, suitable for money amounts. | `DECIMAL` |
| `bool` | Boolean true/false value. | `TINYINT(1)` |
| `timestamp` | Date and time. | `TIMESTAMP` or `DATETIME` |
| `uuid` | Universally unique identifier. Generated automatically on creation. | `BINARY(16)` |
| `json` | Native JSON document. | `JSON` |
| `binary` | Binary data. | `BLOB` or `BINARY` |

### Business types with extra validation

| Type | Description | Behavior |
|---|---|---|
| `email` | Email address. | Automatic format validation on insert. See [section 9](#9-email-field). |
| `password` | Password. | Automatic bcrypt hashing on insert. Excluded from JSON responses by default. See [section 8](#8-password-field). |
| `file` | Reference to a file. | Requires a `file:` sub-block. The system manages file storage separately. See [section 10](#10-file-field). |
| `native` | Database-specific type. | Requires a `native:` sub-block. Allows using a native type not covered by standard types. See [section 11](#11-native-field). |

### Example using every type

```yaml
fields:
  - name: id
    type: uuid

  - name: name
    type: string
    max_length: 100
    required: true

  - name: description
    type: text
    required: false

  - name: age
    type: int
    min_value: 0
    max_value: 150

  - name: views
    type: bigint
    unsigned_value: true
    default: 0

  - name: sector_number
    type: smallint

  - name: weight_kg
    type: float
    min_value: 0.0

  - name: price
    type: decimal

  - name: active
    type: bool
    default: true

  - name: created_at
    type: timestamp

  - name: metadata
    type: json
    required: false

  - name: binary_photo
    type: binary
    required: false

  - name: email
    type: email
    required: true
    unique: true

  - name: password
    type: password
    required: true

  - name: avatar
    type: file
    required: false
    file:
      max_size: 2MB
      allowed_mime_types: [image/png, image/jpeg]
      storage_path: users/avatars
      on_delete: cascade

  - name: pg_coordinates
    type: native
    native:
      dialect: PostgreSQL
      type: POINT
```

### Behavior by type

**`uuid` type**: The system automatically generates a UUID v4 when the record is created. The user does not have to provide a value during `POST`.

**`timestamp` type**: When `timestamps: true` is enabled on the entity, which is the default, the `created_at` and `updated_at` fields are managed automatically by the system. See [section 12](#12-entity-options-section).

**`password` type**: Transparent bcrypt hashing on insert and update. The field never appears in JSON responses. See [section 8](#8-password-field).

**`email` type**: Format validation according to RFC 5322. An invalid value returns a 400 status code. See [section 9](#9-email-field).

**`file` type**: Binary content is stored in the filesystem according to the `storage` configuration. The database column contains a UUID pointing to the `sea_files` system table. See [section 10](#10-file-field).

**Type `decimal`**: The `decimal` type stores an exact fixed-point number, suitable for
monetary amounts.

**Important**: `decimal` values must be sent as **JSON strings**, not as numbers:

```json
✅ Correct
{ "price": "19.99" }

❌ Incorrect — returns a 400 error
{ "price": 19.99 }
```
---

## 7. Common field attributes

This section details the exact meaning of each attribute applicable to a field.

### `required` boolean, default `true`

Determines whether the field is required on insert.

| Value | Database effect | Validation effect |
|---|---|---|
| `true` default | `NOT NULL` column | The field must be provided in the `POST` payload. Missing or null values return 400. |
| `false` | Nullable column | The field can be omitted or explicitly set to null. |

```yaml
- name: phone
  type: string
  required: false
```

### `unique` boolean, default `false`

Adds a uniqueness constraint in the database.

| Value | Effect |
|---|---|
| `true` | A `UNIQUE` constraint is created. Duplicate insert returns 409. |
| `false` default | No constraint. Duplicates are allowed. |

```yaml
- name: email
  type: email
  unique: true
```

### `indexed` boolean, default `false`

Creates a database index on the field to speed up searches.

| Value | Effect |
|---|---|
| `true` | An `INDEX` is created. Recommended on fields often used for filtering or sorting. |
| `false` default | No index. |

```yaml
- name: status
  type: string
  indexed: true
```

### `serializable` boolean, default `true`

Controls whether the field appears in JSON responses.

| Value | Effect |
|---|---|
| `true` default | The field is included in `GET` responses. |
| `false` | The field is stored in the database and accepted as input, but never returned in JSON responses. |

Special case: for `password`, the default value is automatically `false`.

```yaml
- name: internal_token
  type: string
  serializable: false
```

### `unsigned_value` boolean, default `false`

For numeric types, marks the database column as `UNSIGNED`.

```yaml
- name: view_count
  type: bigint
  unsigned_value: true
```

### `max_length` integer

For `string` and `text`, sets the maximum length.

```yaml
- name: product_code
  type: string
  max_length: 50
```

An inserted value exceeding the declared length returns 400.

### `min_value` and `max_value` number

For numeric types, define accepted validation bounds. Supported types are `int`, `bigint`, `smallint`, `float`, and `decimal`.

```yaml
- name: age
  type: int
  min_value: 0
  max_value: 150
```

An out-of-range value returns 400.

### `default` any type

Value automatically applied on insert when the field is omitted from the payload.

```yaml
- name: status
  type: string
  default: "active"

- name: views
  type: int
  default: 0

- name: active
  type: bool
  default: true
```

Restrictions: `binary` and `file` do not support `default`. The system rejects the configuration at startup if a default is declared on these types.

### `previous_name` string

Used to detect a column rename during migration without losing data.

```yaml
- name: full_name
  previous_name: name
  type: string
```

The system detects the rename if:

- A column named `name` exists in the database
- No field named `name` is declared in the YAML file
- A declaration named `full_name` mentions `previous_name: name`
- Type and length characteristics are compatible

The migration mode must be `modified` or `aggressive` for the rename to actually be applied.

---

## 8. `password` field

The `password` type is an enriched business type that automatically applies bcrypt hashing and excludes the field from JSON responses.

### Declaration

```yaml
- name: password
  type: password
  required: true
```

### Automatic behavior

| Operation | Behavior |
|---|---|
| `POST /<entity>` with a password | The plain text password is hashed with bcrypt before insertion. |
| `PUT /<entity>/{id}` with a password | The new password is re-hashed. |
| `GET /<entity>` or `GET /<entity>/{id}` | The field is excluded from responses, equivalent to `serializable: false`. |

### Default `serializable` attribute

For `password` fields, `serializable` is **`false` by default**, unlike other types where it is `true`. This prevents accidental exposure of the hash in responses.

If the user wants to force serialization, which is strongly discouraged, it must be declared explicitly:

```yaml
- name: password
  type: password
  serializable: true
```

### Complete example

```yaml
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

### Creation request

```bash
curl -X POST http://localhost:8081/users \
  -H "Content-Type: application/json" \
  -d '{
    "email": "alice@example.com",
    "password": "PlainTextPassword",
    "role": "user"
  }'
```

### Response

```json
{
  "id": "550e8400-e29b-41d4-a716-446655440000",
  "email": "alice@example.com",
  "role": "user"
}
```

The `password` field does not appear in the response.

---

## 9. `email` field

The `email` type automatically validates that the provided value follows an email address format.

### Declaration

```yaml
- name: email
  type: email
  required: true
  unique: true
```

### Behavior

| Operation | Behavior |
|---|---|
| Insert with a valid email | Accepted; the record is created. |
| Insert with a string that does not match email format | Rejected with status code 400. |
| Storage | Same as `string`, stored as `VARCHAR`. |
| Serialization | Normally included in JSON responses. |

### Validation example

```bash
# Valid email: 201 Created
curl -X POST http://localhost:8081/users \
  -d '{"email": "alice@example.com"}'

# Invalid email: 400 Bad Request
curl -X POST http://localhost:8081/users \
  -d '{"email": "not-an-email"}'
```

---

## 10. `file` field

The `file` type allows a field to accept uploaded file content. The system manages physical storage separately and stores a database reference to the file.

### Minimal declaration

```yaml
- name: avatar
  type: file
  file:
    storage_path: users/avatars
    on_delete: cascade
```

### The `file` sub-block is required

Every `type: file` declaration must include a `file:` sub-block. A declaration without this sub-block is rejected during parsing.

Conversely, declaring a `file:` sub-block on a type other than `file` is also rejected.

### Keys in the `file` sub-block

| Key | Type | Default value | Description |
|---|---|---|---|
| `storage_path` | string | required | Subdirectory relative to `storage.root_path` where files for this field are stored. |
| `on_delete` | enum | `cascade` | Delete behavior. Values: `cascade`, `set_null`, `restrict`. |
| `max_size` | size | absent, no specific limit | Maximum accepted size. Formats: `500KB`, `5MB`, `1GB`. |
| `allowed_mime_types` | list | empty, all types | Whitelist of accepted MIME types. |
| `allowed_extensions` | list | empty, all extensions | Whitelist of extensions, including the leading dot. Case-insensitive. |

### `on_delete` strategies

| Strategy | Effect when deleting the parent entity | Effect when replacing the file |
|---|---|---|
| `cascade` default | File is deleted from disk if no longer referenced. | Old file is deleted if no longer referenced. |
| `set_null` | File is kept on disk. | Old file is kept on disk. |
| `restrict` | Deletion is refused with 409 as long as a file is attached. | Replacement is allowed. |

### Generated routes for `file` fields

For each field of type `file` declared on an entity, the system automatically adds a download route:

```text
GET /<plural entity>/<field name>/{id}
```

Examples:

| Declared field | Download route |
|---|---|
| `User.avatar` | `GET /users/avatar/{id}` |
| `Article.banner` | `GET /articles/banner/{id}` |
| `Contract.pdf` | `GET /contracts/pdf/{id}` |

### Three upload modes

1. **Multipart/form-data**, recommended for forms:

```bash
curl -X POST http://localhost:8081/users \
  -F "email=alice@example.com" \
  -F "avatar=@./photo.png;type=image/png"
```

2. **JSON with base64**, suitable for clients that can only send JSON:

```bash
curl -X POST http://localhost:8081/users \
  -H "Content-Type: application/json" \
  -d '{
    "email": "alice@example.com",
    "avatar": {
      "filename": "photo.png",
      "mime_type": "image/png",
      "content_base64": "..."
    }
  }'
```

3. **Reference to an existing file by UUID**, used for sharing files between entities:

```bash
curl -X POST http://localhost:8081/users \
  -H "Content-Type: application/json" \
  -d '{
    "email": "bob@example.com",
    "avatar": "660f9511-f30c-52e5-b827-557766551111"
  }'
```

### Global storage configuration

The service-level `storage:` block determines where files are stored. See [section 25](#25-storage-section).

### Detailed documentation

The full topic, including file sharing, reference counting, and cascade deletion, is covered in `FILE_FEATURE_USER_GUIDE.md`.

---

## 11. `native` field

The `native` type allows using a database-specific type that is not covered by SeaDesktop standard types.

### Declaration

```yaml
- name: coordinates
  type: native
  native:
    dialect: PostgreSQL
    type: POINT
```

### Keys in the `native` sub-block

| Key | Type | Required | Description |
|---|---|---|---|
| `dialect` | enum | **Yes** | Target DBMS. Values: `MySQL`, `PostgreSQL`, `SQLite`, `SQLServer`. |
| `type` | string | **Yes** | Exact native type name as used by the DBMS. |

### Behavior

The native type is passed as-is to the DBMS when creating the table. SeaDesktop does not perform extra validation on values. The user is responsible for consistency.

### Restrictions

- The `native:` sub-block is required if `type: native`. Without it, parsing fails.
- The `native:` sub-block is forbidden for other types.
- The native type is only compatible with SQL databases. It does not apply to NoSQL databases.

### Examples

```yaml
# PostgreSQL: geographic point type
- name: position
  type: native
  native:
    dialect: PostgreSQL
    type: POINT

# MySQL: unsigned medium integer type
- name: counter
  type: native
  native:
    dialect: MySQL
    type: "MEDIUMINT UNSIGNED"

# SQL Server: money type
- name: currency_price
  type: native
  native:
    dialect: SQLServer
    type: MONEY
```

---

## 12. Entity `options` section

The `options:` block configures automatic behavior for the entity.

### Accepted keys

```yaml
- name: User
  options:
    enable_crud: true
    is_auth_source: false
    enable_websocket: false
    soft_delete: false
    timestamps: true
    public_routes: false
```

| Key | Type | Default value | Description |
|---|---|---|---|
| `enable_crud` | boolean | `true` | Enables generation of standard CRUD routes. |
| `is_auth_source` | boolean | `false` | Marks this entity as the authentication source. |
| `enable_websocket` | boolean | `false` | Enables a notification WebSocket route; not currently implemented. |
| `soft_delete` | boolean | `false` | Adds a `deleted_at` field and hides deleted records instead of physically deleting them. |
| `timestamps` | boolean | `true` | Automatically adds `created_at` and `updated_at` fields managed by the system. |
| `public_routes` | boolean | `false` | If `true`, routes for this entity are public and not protected by authentication middleware. |

### Behavior of `is_auth_source`

Only one entity per service can have `is_auth_source: true`. This entity must include:

- A field used as a unique identifier, typically `email`
- A field of type `password`
- A field used as role, typically `role`

Enabling `is_auth_source` automatically generates the `/auth/register`, `/auth/login`, `/auth/refresh`, `/auth/logout`, and `/auth/me` routes. See [section 16](#16-securityauthentication-section).

### Behavior of `timestamps`

When `timestamps: true`, which is the default, the system automatically adds two fields to the entity:

- `created_at`: creation timestamp, set on insert and never modified afterwards.
- `updated_at`: last modification timestamp, updated on every `PUT`.

These fields appear in JSON responses. The user does not have to manually declare them in `fields`.

To disable this behavior:

```yaml
- name: Configuration
  options:
    timestamps: false
```

### Behavior of `soft_delete`

When `soft_delete: true`, the system adds a `deleted_at` field to the entity. `DELETE` requests do not remove records from the database. Instead, they fill `deleted_at` with the current timestamp. `GET` routes automatically filter out records whose `deleted_at` is not null.

### Behavior of `public_routes`

By default, when authentication is enabled at service level, all generated CRUD routes are protected and require a JWT token. With `public_routes: true`, routes for that specific entity remain accessible without authentication.

---

## 13. `relations` section

A relation links two entities and generates additional REST routes to navigate between them.

### Four relation kinds

| Kind | Description | Foreign key stored in |
|---|---|---|
| `belongs_to` | The current entity references another entity. | Current entity, local column. |
| `has_many` | The current entity is referenced by several other entities. | Target entity. |
| `has_one` | The current entity is referenced by exactly one other entity. | Target entity. |
| `many_to_many` | Multiple cardinality on both sides. | Dedicated pivot table. |

### Relation structure

```yaml
relations:
  - name: author
    target_entity: User
    kind: belongs_to
    fk_column: author_id
    on_delete: cascade
```

### Accepted keys

| Key | Type | Required | Description |
|---|---|---|---|
| `name` | string | **Yes** | Logical relation name. Used in generated routes. |
| `target_entity` | string | **Yes** | Name of the referenced entity. |
| `kind` | enum | **Yes** | Relation type: `belongs_to`, `has_many`, `has_one`, `many_to_many`. |
| `on_delete` | enum | `restrict` | Behavior when the referenced entity is deleted. Values: `cascade`, `set_null`, `restrict`. |
| `fk_column` | string | Conditional | Foreign key column name. Required for `belongs_to`, `has_many`, and `has_one`. |
| `pivot_table` | string | Conditional | Pivot table name. Required for `many_to_many`. |
| `source_fk_column` | string | Conditional | Column referencing the current entity in the pivot table. Required for `many_to_many`. |
| `target_fk_column` | string | Conditional | Column referencing the target entity in the pivot table. Required for `many_to_many`. |

### Example: `belongs_to` relation

An order belongs to a user.

```yaml
- name: Order
  fields:
    - name: id
      type: uuid
    - name: user_id
      type: uuid
      required: true

  relations:
    - name: user
      kind: belongs_to
      target_entity: User
      fk_column: user_id
      on_delete: restrict
```

This allows the order to reference a user through `user_id`.

### Example: `has_many` relation

A department has many employees.

```yaml
- name: Department
  fields:
    - name: id
      type: uuid
    - name: name
      type: string

  relations:
    - name: employees
      kind: has_many
      target_entity: Employee
      fk_column: department_id
      on_delete: cascade
```

The generated relationship routes allow employees to be listed from a department.

### Example: `has_one` relation

A user has one profile.

```yaml
- name: User
  fields:
    - name: id
      type: uuid

  relations:
    - name: profile
      kind: has_one
      target_entity: Profile
      fk_column: user_id
      on_delete: cascade
```

### Example: `many_to_many` relation

A student can belong to many programs and a program can contain many students.

```yaml
- name: Student
  fields:
    - name: id
      type: uuid

  relations:
    - name: programs
      kind: many_to_many
      target_entity: Program
      pivot_table: student_programs
      source_fk_column: student_id
      target_fk_column: program_id
      on_delete: cascade
```

### `on_delete` strategies for relations

| Strategy | Behavior |
|---|---|
| `cascade` | Related records are deleted when the referenced record is deleted. |
| `set_null` | Foreign key is set to null when the referenced record is deleted. |
| `restrict` | Deletion is refused while dependent records exist. |

---

## 14. `pagination` section

The `pagination:` block enables paginated routes for an entity. SeaDesktop supports three modes: `page`, `offset`, and `cursor`.

### General structure

```yaml
- name: Product
  fields: [ ... ]
  pagination:
    page:
      default_page_size: 20
      max_page_size: 100
      default_sort: "created_at:desc"
      sortable_fields: [created_at, name]
    offset:
      default_limit: 20
      max_limit: 100
      default_sort: "id:asc"
      sortable_fields: [id, created_at]
    cursor:
      default_limit: 20
      max_limit: 100
      cursor_field: id
      sort: "id:asc"
```

If the `pagination:` block is absent, no paginated route is generated. If it is present but empty, parsing fails.

### `page` mode

Generates `GET /<entity>/page`. Query parameters include `page`, `page_size`, and `sort`. The response contains `items`, `page`, `page_size`, `total`, `total_pages`, and `sort`.

### `offset` mode

Generates `GET /<entity>/offset`. Query parameters include `offset`, `limit`, and `sort`. The response contains `items`, `offset`, `limit`, `total`, and `sort`.

### `cursor` mode

Generates `GET /<entity>/cursor`. Query parameters include `after` and `limit`. Sorting is fixed by configuration and is not configurable by the client. The response contains `items`, `limit`, `next_cursor`, and `prev_cursor`.

### Expected behavior for the three modes

- Invalid pagination parameters return 400.
- Values above `max_page_size` or `max_limit` are silently capped.
- Sort fields must belong to `sortable_fields` for `page` and `offset` modes.
- Cursor fields must be stable and unique.

### Comparison of the three modes

| Criterion | `page` | `offset` | `cursor` |
|---|---|---|---|
| Arbitrary navigation | yes | yes | no |
| Constant performance at deep pagination levels | no | no | yes |
| Total counter | yes | yes | no |
| Client-side dynamic sorting | yes | yes | no |
| Suitable for real-time feeds | no | no | yes |

---

## 15. Entity `seeds` section

Seeds allow initial data to be inserted automatically.

### Global activation

Seed execution is configured in `database.migrations.seeds`:

```yaml
database:
  migrations:
    seeds:
      enabled: true
      mode: once
      on_error: continue
```

| Key | Description |
|---|---|
| `enabled` | Enables or disables seed execution. |
| `mode` | Controls insertion mode. |
| `on_error` | Controls behavior when a seed insertion fails. |

### Insertion modes

| Mode | Behavior |
|---|---|
| `once` | Inserts seeds once and avoids reinserting existing data. |
| `always` | Attempts to insert seeds at each startup. |

### Error behavior

| Value | Behavior |
|---|---|
| `continue` | Logs the error and continues processing remaining seeds. |
| `stop` | Stops seed execution when an error occurs. |

### Declaring seeds on an entity

```yaml
- name: Department
  fields:
    - name: id
      type: uuid
    - name: name
      type: string

  seeds:
    - ref: dept_it
      values:
        id: $uuid
        name: IT
    - ref: dept_hr
      values:
        id: $uuid
        name: Human Resources
```

### Seed structure

| Key | Description |
|---|---|
| `ref` | Optional logical reference used by other seeds. |
| `values` | Field values to insert. |

### Available macros in values

| Macro | Description |
|---|---|
| `$uuid` | Generates a UUID. |
| `$now` | Uses the current timestamp. |

### References between seeds

Seeds can reference previously declared seed records through their `ref` value. This is useful for foreign keys.

```yaml
- name: Employee
  seeds:
    - ref: alice
      values:
        id: $uuid
        name: Alice
        department_id: $ref:dept_it
```

### Seeds with many-to-many relations

Many-to-many seed data can be inserted through the pivot entity or pivot table configuration, depending on the model declared in the YAML.

### Expected behavior

1. The system checks whether seed execution is enabled.
2. Seeds are read in entity order.
3. Macros are resolved.
4. References are resolved from previously inserted seed references.
5. Records are inserted according to the selected mode.
6. Errors are handled according to `on_error`.

---

## 16. `security.authentication` section

This section configures authentication.

### Complete block

```yaml
security:
  authentication:
    type: jwt
    access_token_ttl: 15m
    refresh_token_ttl: 7d
    issuer: SeaDesktop
    audience: SeaDesktopUsers
    password_field: password
    identifier_field: email
    role_field: role
```

### Accepted keys

| Key | Description |
|---|---|
| `type` | Authentication type. Values include `none` and `jwt`. |
| `access_token_ttl` | Access token lifetime. |
| `refresh_token_ttl` | Refresh token lifetime. |
| `issuer` | JWT issuer. |
| `audience` | JWT audience. |
| `password_field` | Field containing the password. |
| `identifier_field` | Field used as login identifier. |
| `role_field` | Field containing the role. |

### Authentication source entity

An entity must be marked with `is_auth_source: true` to act as the authentication source. It must provide the fields needed by the authentication configuration, typically `email`, `password`, and `role`.

```yaml
- name: User
  options:
    is_auth_source: true
  fields:
    - name: id
      type: uuid
    - name: email
      type: email
      unique: true
    - name: password
      type: password
    - name: role
      type: string
```

### Automatically generated routes

| Route | Description |
|---|---|
| `POST /auth/register` | Registers a user. |
| `POST /auth/login` | Authenticates a user and returns tokens. |
| `POST /auth/refresh` | Refreshes an access token. |
| `POST /auth/logout` | Logs out the current user. |
| `GET /auth/me` | Returns information about the authenticated user. |

### Request formats

Login example:

```bash
curl -X POST http://localhost:8081/auth/login \
  -H "Content-Type: application/json" \
  -d '{
    "email": "alice@example.com",
    "password": "secret"
  }'
```

Typical response:

```json
{
  "access_token": "...",
  "refresh_token": "...",
  "token_type": "Bearer"
}
```

### Token usage

Protected routes expect the access token in the `Authorization` header:

```text
Authorization: Bearer <access_token>
```

---

## 17. Cookies and token delivery

### Delivery modes

Tokens can be delivered in the JSON response, in cookies, or in both depending on the authentication configuration.

### `cookies` block

```yaml
security:
  authentication:
    cookies:
      enabled: true
      http_only: true
      secure: true
      same_site: Lax
      access_cookie_name: access_token
      refresh_cookie_name: refresh_token
```

| Key | Description |
|---|---|
| `enabled` | Enables token delivery through cookies. |
| `http_only` | Prevents JavaScript from reading the cookie. |
| `secure` | Sends the cookie only over HTTPS. |
| `same_site` | Controls cross-site behavior. |
| `access_cookie_name` | Cookie name for the access token. |
| `refresh_cookie_name` | Cookie name for the refresh token. |

### SameSite policy

`SameSite` controls how cookies are sent in cross-site contexts. Supported values are typically `Strict`, `Lax`, and `None`.

### HttpOnly attribute

When `http_only: true`, the cookie cannot be read by client-side JavaScript. This reduces exposure to XSS token theft.

### Server-side token reading

When cookie delivery is enabled, the server can read tokens from the configured cookies in addition to the `Authorization` header.

---

## 18. Token tracking

Token tracking allows the system to keep state about issued tokens, refresh tokens, revocation, rotation, and cleanup.

### Complete block

```yaml
security:
  authentication:
    token_tracking:
      enabled: true
      backend: database
      cache:
        enabled: true
        max_entries: 10000
        ttl: 5m
      rotation:
        enabled: true
        reuse_detection: true
      auto_cleanup:
        enabled: true
        interval: 1h
```

### Main block keys

| Key | Description |
|---|---|
| `enabled` | Enables token tracking. |
| `backend` | Storage backend for token state, such as database. |
| `cache` | Cache configuration. |
| `rotation` | Refresh token rotation behavior. |
| `auto_cleanup` | Expired token cleanup behavior. |

### `cache` sub-block

| Key | Description |
|---|---|
| `enabled` | Enables an in-memory cache. |
| `max_entries` | Maximum number of cache entries. |
| `ttl` | Cache entry lifetime. |

### `rotation` sub-block

| Key | Description |
|---|---|
| `enabled` | Enables refresh token rotation. |
| `reuse_detection` | Detects reuse of an old refresh token. |

### `auto_cleanup` sub-block

| Key | Description |
|---|---|
| `enabled` | Enables automatic cleanup. |
| `interval` | Cleanup interval. |

### Created system tables

When database-backed token tracking is enabled, SeaDesktop creates the system tables required to store token metadata and revocation state.

### Expected behavior

- Login creates token tracking entries.
- Refresh can rotate refresh tokens when rotation is enabled.
- Logout can revoke active tokens.
- Expired tokens can be cleaned automatically.

---

## 19. `security.authorization` section

This section configures authorization rules at service level.

### Complete block

```yaml
security:
  authorization:
    enabled: true
    default_policy: deny
    admin_role: admin
    abac_mode: permissive
    roles:
      - name: admin
        permissions: ["*"]
      - name: user
        permissions: ["read:self"]
```

### Accepted keys

| Key | Description |
|---|---|
| `enabled` | Enables or disables authorization. |
| `default_policy` | Policy applied when no rule matches. Usually `allow` or `deny`. |
| `admin_role` | Role that bypasses authorization checks. |
| `roles` | Role catalog and permissions. |
| `abac_mode` | Controls resource-aware ABAC handling. |

### Default policy

`default_policy` determines what happens when no explicit rule grants access. A `deny` policy is safer because it refuses access by default.

### Administrator role

The configured `admin_role` bypasses authorization checks. This role is typically used for administrative users.

### Role catalog

The `roles` list documents and configures available roles and their permissions.

### ABAC mode

| Mode | Behavior |
|---|---|
| `permissive` | Allows resource-aware checks to be evaluated later by handlers. |
| `strict` | Immediately rejects resource-aware rules at middleware level if the resource is not available yet. |

---

## 20. Per-entity `access_control` rules

The `access_control` block defines access rules for one entity.

### General structure

```yaml
- name: Employee
  access_control:
    abac_mode: permissive
    list:
      allow_roles: [admin, manager]
      same_scope: department_id
    get:
      allow_roles: [admin, manager, user]
      same_scope: department_id
      own_resource: id
    create:
      allow_roles: [admin, manager]
    update:
      allow_roles: [admin, manager]
      same_scope: department_id
    delete:
      allow_roles: [admin]
```

### Keys at `access_control` block level

| Key | Description |
|---|---|
| `abac_mode` | Optional entity-level override for ABAC behavior. |
| `list` | Rules for collection listing. |
| `get` | Rules for retrieving one record. |
| `create` | Rules for creation. |
| `update` | Rules for update. |
| `delete` | Rules for deletion. |

### Accepted keys inside an operation block

| Key | Description |
|---|---|
| `allow_roles` | List of roles allowed to perform the operation. |
| `same_scope` | Restricts access to records sharing the same scope as the user. |
| `own_resource` | Restricts access to records owned by the user. |

### `allow_roles`

```yaml
get:
  allow_roles: [admin, manager]
```

Only users whose role is listed are allowed by this rule.

### `same_scope`

`same_scope` checks that a user claim matches a field on the target resource.

#### Boolean form

```yaml
list:
  same_scope: true
```

The system uses the default scope mapping.

#### String form

```yaml
list:
  same_scope: department_id
```

The resource field `department_id` must match the corresponding user claim.

#### Complete example

```yaml
- name: Employee
  fields:
    - name: id
      type: uuid
    - name: department_id
      type: uuid

  access_control:
    list:
      allow_roles: [manager]
      same_scope: department_id
```

### `own_resource`

`own_resource` checks that the resource belongs to the authenticated user.

#### Boolean form

```yaml
get:
  own_resource: true
```

The system uses the default ownership mapping.

#### String form

```yaml
get:
  own_resource: user_id
```

The resource field `user_id` must match the authenticated user id.

#### Complete example

```yaml
- name: UserProfile
  fields:
    - name: id
      type: uuid
    - name: user_id
      type: uuid

  access_control:
    get:
      allow_roles: [user]
      own_resource: user_id
    update:
      allow_roles: [user]
      own_resource: user_id
```

### Combining `same_scope` and `own_resource`

Both rules can be used together to express more precise authorization logic. For example, a user may access their own resource, while a manager may access resources in the same department.

### Administrator bypass

The configured admin role bypasses access-control checks automatically.

### Expected behavior for each operation

| Operation | Behavior |
|---|---|
| `list` | Can silently filter records denied by ABAC. |
| `get` | Returns 403 if the single resource is denied. |
| `create` | Checks the submitted payload before insertion. |
| `update` | Checks the current resource before SQL UPDATE. |
| `delete` | Checks the current resource before SQL DELETE. |

### Complete example with two entities

```yaml
entities:
  - name: Department
    fields:
      - name: id
        type: uuid
      - name: name
        type: string

  - name: Employee
    fields:
      - name: id
        type: uuid
      - name: name
        type: string
      - name: department_id
        type: uuid
      - name: user_id
        type: uuid

    access_control:
      list:
        allow_roles: [admin, manager]
        same_scope: department_id
      get:
        allow_roles: [admin, manager, user]
        same_scope: department_id
        own_resource: user_id
      create:
        allow_roles: [admin, manager]
        same_scope: department_id
      update:
        allow_roles: [admin, manager]
        same_scope: department_id
      delete:
        allow_roles: [admin]
```

---

## 21. `security.cors` section

CORS controls which external origins are allowed to call the API from a browser.

### Complete block
```yaml
security:
  cors:
    enabled: true
    allowed_origins:
      - http://localhost:3000
    allowed_methods: [GET, POST, PUT, DELETE, OPTIONS]
    allowed_headers: [Content-Type, Authorization]
    allow_credentials: true
    origin_policy: permissive
```
### Accepted keys

| Key | Description |
|---|---|
| `enabled` | Enables CORS handling. |
| `allowed_origins` | List of allowed origins. |
| `allowed_methods` | List of allowed HTTP methods. |
| `allowed_headers` | List of allowed request headers. |
| `allow_credentials` | Allows cookies and credentials when true. |
| `origin_policy` | How to handle requests from non-allowed origins: `permissive` (default) or `strict`. |

### Expected behavior
When enabled, SeaDesktop adds the required CORS headers and handles preflight requests according to the declared configuration.

### `origin_policy`: permissive vs strict

Controls how the server responds when a request comes from an `Origin` that is **not** in `allowed_origins`:

- **`permissive`** (default, spec-compliant): the server processes the request normally but **omits** the `Access-Control-Allow-Origin` header from the response. The browser blocks the response on the client side. Non-browser clients (server-to-server, curl, mobile apps) that ignore CORS receive the data. This is the standard CORS behavior recommended by the spec.

- **`strict`**: the server **immediately refuses** the request with `403 Forbidden` and a clear error message mentioning the rejected Origin. The request never reaches the handler. This is helpful in development to make CORS misconfigurations visible server-side rather than as a silent client-side block.

| Use case | Recommended policy |
|---|---|
| Public-facing API serving browsers + B2B backends | `permissive` |
| Internal API with a strict list of frontend clients | `strict` |
| Development environment | `strict` (faster diagnostic) |

Note: `permissive` is the spec-conformant behavior. Choose `strict` only if you control all your clients and want explicit refusal of unknown origins.

---

## 22. `security.rate_limits` section

Rate limits restrict request volume to protect the service.

### Complete block

```yaml
security:
  rate_limits:
    enabled: true
    rules:
      - name: login_limit
        pattern: /auth/login
        max_requests: 5
        window: 1m
        strategy: ip
```

### Accepted keys for a rule

| Key | Description |
|---|---|
| `name` | Rule name. |
| `pattern` | Route pattern to match. |
| `max_requests` | Maximum number of requests allowed during the window. |
| `window` | Time window. |
| `strategy` | Grouping strategy. |

### Supported patterns

Patterns can target exact routes or groups of routes depending on the matcher implemented by the server.

### Grouping strategies

| Strategy | Description |
|---|---|
| `ip` | Groups requests by client IP. |
| `user` | Groups requests by authenticated user. |
| `global` | Applies a single global counter. |

### Expected behavior

When a request exceeds the configured limit, the server returns a rate-limit error response, typically 429.

---

## 23. `security.security_headers` section

This section configures additional HTTP security headers.

### Complete block

```yaml
security:
  security_headers:
    enabled: true
    content_security_policy: "default-src 'self'"
    x_frame_options: DENY
    x_content_type_options: nosniff
    referrer_policy: no-referrer
```

### Behavior

When enabled, SeaDesktop adds the configured security headers to HTTP responses. These headers help reduce common web risks such as clickjacking, MIME sniffing, and unsafe content loading.

---

## 24. `security.http_limits` section

This section configures HTTP request limits.

### Complete block

```yaml
security:
  http_limits:
    max_body_size: 10MB
    max_header_size: 16KB
    max_multipart_file_size: 20MB
```

### Behavior

The server rejects requests exceeding the configured limits. These limits protect the service against oversized bodies, oversized headers, and excessive uploads.

---

## 25. `storage` section

The `storage:` block configures the filesystem backend used by `file` fields.

### Complete block

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
| `backend` | `filesystem` | `filesystem` | Storage backend type. Only `filesystem` is currently supported. |
| `root_path` | path | `./uploads` | Root directory under which all files are organized. |
| `file_mode` | octal string | `"0640"` | Unix permissions applied to created files. |
| `directory_mode` | octal string | `"0750"` | Unix permissions applied to created directories. |

### Critical note about octal modes

Unix modes must be declared as strings, for example `"0640"`, not as integers like `0640`. YAML may interpret `0640` as a numeric value, which does not represent the intended Unix permission. Quotes force literal interpretation before the parser converts the value as base 8.

### Behavior when the block is absent

If an entity declares a field of type `file` but the service has no `storage:` block, the system applies default values:

- `backend`: `filesystem`
- `root_path`: `./uploads`

A warning is logged at startup to indicate that fallback values are being used. Explicit configuration is recommended in production.

### Detailed documentation

The full file storage behavior is documented in `FILE_FEATURE_USER_GUIDE.md`.

---

## 26. `logging` section

The `logging:` block configures log level, modules, sinks, formats, and log visualization endpoints.

### Complete block

```yaml
logging:
  enabled: true
  level: info
  format: text
  modules:
    auth: debug
    authz: debug
    sql: info
  sinks:
    console:
      enabled: true
    file:
      enabled: true
      path: ./logs/seadesktop.log
      rotation:
        max_size: 10MB
        max_files: 5
  endpoints:
    enabled: true
    route: /logs
```

### Main block keys

| Key | Description |
|---|---|
| `enabled` | Enables or disables logging. |
| `level` | Global log level. |
| `format` | Log format. |
| `modules` | Per-module log levels. |
| `sinks` | Output destinations. |
| `endpoints` | Log visualization endpoints. |

### Log levels

Typical levels are `trace`, `debug`, `info`, `warn`, `error`, and `critical`.

### Available modules

Modules may include authentication, authorization, SQL, YAML parsing, runtime, HTTP, and other internal components depending on implementation.

### Sinks

A sink is an output destination for logs.

#### `console` sink

```yaml
console:
  enabled: true
```

Writes logs to the console.

#### `file` sink

```yaml
file:
  enabled: true
  path: ./logs/seadesktop.log
  rotation:
    max_size: 10MB
    max_files: 5
```

Writes logs to a file and can rotate them based on size and file count.

### Formats

Logs can be emitted in text format or structured format depending on the configured value.

### Visualization endpoints

When enabled, log endpoints allow the service to expose recent logs through HTTP routes. Access to these endpoints should be protected in production.

### Detailed documentation

Logging behavior may be documented in a dedicated logging reference if present in the project documentation.

---

## 27. Generated system endpoints

Depending on the enabled features, SeaDesktop can generate system endpoints such as:

| Endpoint | Description |
|---|---|
| `/docs` | Interactive API documentation. |
| `/openapi.json` | OpenAPI schema. |
| `/auth/register` | User registration when authentication is enabled. |
| `/auth/login` | User login. |
| `/auth/refresh` | Token refresh. |
| `/auth/logout` | Logout. |
| `/auth/me` | Current authenticated user information. |
| `/logs` | Log visualization when enabled. |

---

## 28. HTTP response codes

### Success codes

| Code | Meaning |
|---|---|
| 200 | Successful request. |
| 201 | Resource created. |
| 204 | Resource deleted with no response body. |

### Client error codes

| Code | Meaning |
|---|---|
| 400 | Invalid request, validation error, invalid YAML-derived constraint. |
| 401 | Authentication required or invalid token. |
| 403 | Authenticated but not authorized. |
| 404 | Resource not found. |
| 409 | Conflict, such as uniqueness violation or restrict rule. |
| 429 | Rate limit exceeded. |

### Server error codes

| Code | Meaning |
|---|---|
| 500 | Internal server error. |
| 503 | Service temporarily unavailable. |

### Error response format

Typical error responses contain an `error` field and may include details:

```json
{
  "error": "Validation failed",
  "details": {
    "email": "invalid email format"
  }
}
```

---

## 29. Additional documentation

The following complementary documents provide more detail on specific topics:

- `README.md`: project overview and quick start
- `Release_Notes.md`: version changelog
- `pagination.md`: detailed pagination documentation
- `FILE_FEATURE_USER_GUIDE.md`: detailed documentation for `file` fields
- `COMMERCIAL-LICENSE.MD`: commercial license information

---

## Appendix: complete file example

```yaml
project:
  name: SeaDesktopDemo

services:
  - name: MainService
    port: 8081

    database:
      type: mysql
      host: localhost
      port: 3306
      database_name: demo_db
      username: root
      password: rootpassword
      migrations:
        enabled: true
        create_database_if_missing: true
        mode: modified
        dry_run: false
        seeds:
          enabled: true
          mode: once
          on_error: continue

    storage:
      backend: filesystem
      root_path: ./uploads
      file_mode: "0640"
      directory_mode: "0750"

    security:
      authentication:
        type: jwt
        identifier_field: email
        password_field: password
        role_field: role
      authorization:
        enabled: true
        default_policy: deny
        admin_role: admin
        abac_mode: permissive

    entities:
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
          - name: avatar
            type: file
            required: false
            file:
              max_size: 2MB
              allowed_mime_types: [image/png, image/jpeg]
              storage_path: users/avatars
              on_delete: cascade

      - name: Department
        fields:
          - name: id
            type: uuid
          - name: name
            type: string
            required: true
            unique: true
        relations:
          - name: employees
            kind: has_many
            target_entity: Employee
            fk_column: department_id
            on_delete: restrict

      - name: Employee
        fields:
          - name: id
            type: uuid
          - name: name
            type: string
            required: true
          - name: email
            type: email
            required: true
            unique: true
          - name: department_id
            type: uuid
            required: false
        relations:
          - name: department
            kind: belongs_to
            target_entity: Department
            fk_column: department_id
            on_delete: restrict
        pagination:
          page:
            default_page_size: 20
            max_page_size: 100
            default_sort: "name:asc"
            sortable_fields: [name, email]
        access_control:
          list:
            allow_roles: [admin, manager]
            same_scope: department_id
          get:
            allow_roles: [admin, manager, user]
            same_scope: department_id
          create:
            allow_roles: [admin, manager]
          update:
            allow_roles: [admin, manager]
            same_scope: department_id
          delete:
            allow_roles: [admin]
```
