# SeaDesktop — Logging

Reference documentation for configuring the log system of SeaDesktop services. This document describes each supported YAML key, its allowed values, its default behaviour, and the expected system response. All information comes from the project source code.

---

## Contents

1. [General principle](#1-general-principle)
2. [Enabling logging](#2-enabling-logging)
3. [The `logging` block](#3-the-logging-block)
4. [Global level (`level`)](#4-global-level-level)
5. [Per-module levels (`modules`)](#5-per-module-levels-modules)
6. [Sinks (log destinations)](#6-sinks-log-destinations)
7. [The `console` sink](#7-the-console-sink)
8. [The `file` sink](#8-the-file-sink)
9. [File rotation](#9-file-rotation)
10. [Log format](#10-log-format)
11. [Immediate flush (`flush_level`)](#11-immediate-flush-flush_level)
12. [Asynchronous logging (`async`)](#12-asynchronous-logging-async)
13. [The `/admin/logs` endpoint](#13-the-adminlogs-endpoint)
14. [The `/admin/logs/loggers` endpoint](#14-the-adminlogsloggers-endpoint)
15. [Duration and size formats](#15-duration-and-size-formats)
16. [Configuration examples](#16-configuration-examples)

---

## 1. General principle

The logging system traces a service's activity. Each message emitted by the code is:

- Tagged with a **module** (e.g. `sea.http`, `sea.persistence`, `sea.boot`).
- Classified by **severity level** (trace, debug, info, warn, error, critical).
- Sent to one or more independently configured **sinks** (console, file).

Logging is configured declaratively in the service's `logging:` block. Multiple sinks can coexist: each message is sent to all active sinks simultaneously.

An internal memory buffer continuously keeps the last 10,000 messages, exposed via the REST endpoint `/admin/logs` for consultation from SeaUI or any other authorized client.

---

## 2. Enabling logging

The `logging:` block is entirely optional.

### Behaviour when the block is absent

If the `logging:` section is missing from the YAML, the system applies the default values:

- Global level: `info`
- A single sink: console, format `text`, active
- Immediate flush from `error` upward
- Asynchronous mode enabled with a 8192-message queue and `overrun_oldest` policy

These defaults are reasonable for a quick start in a development environment.

### Minimal explicit activation

```yaml
services:
  - name: MyService
    logging:
      level: info
```

### Full deactivation

```yaml
logging:
  enabled: false
```

With `enabled: false`, no messages are emitted to any sink. This option is useful for automated tests or situations where log noise must be eliminated.

---

## 3. The `logging` block

### Full structure

```yaml
logging:
  enabled: true
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
        max_files: 10
        compress: false
  flush_level: error
  async:
    enabled: true
    queue_size: 8192
    overflow_policy: overrun_oldest
```

### Accepted keys

| Key | Type | Default | Description |
|---|---|---|---|
| `enabled` | boolean | `true` | Enables or disables the log system. |
| `level` | enum | `info` | Default global level. See [section 4](#4-global-level-level). |
| `modules` | block | `{}` | Override the level for specific modules. See [section 5](#5-per-module-levels-modules). |
| `sinks` | list | text console | Log destinations. See [section 6](#6-sinks-log-destinations). |
| `flush_level` | enum | `error` | Level beyond which messages are written immediately to disk. See [section 11](#11-immediate-flush-flush_level). |
| `async` | block | enabled | Configuration of asynchronous writing. See [section 12](#12-asynchronous-logging-async). |

### Behaviour when no sink is declared

If the `sinks:` key is absent, a default console sink (text format, active) is added automatically.

If the `sinks:` key is present but all sinks have `enabled: false`, no log is physically written, but the in-memory ring buffer remains fed and the `/admin/logs` endpoint remains operational.

---

## 4. Global level (`level`)

The global level determines the default verbosity for all modules not explicitly configured.

### Accepted values

| Value | Severity | Description |
|---|---|---|
| `trace` | lowest | Very detailed trace of internal operations. Disabled in production. |
| `debug` | low | Diagnostic information, internal variables. |
| `info` (default) | normal | Normal events: startup, successful operations. |
| `warn` | medium | Tolerable anomalies, unexpected but recoverable behaviour. |
| `error` | high | Exceptions, failed operations, incorrect states. |
| `critical` | very high | Service unusable, major failure. |
| `off` | total | No message emitted. |

### Effective selection

A message is emitted if its level is greater than or equal to the effective level of its module. The effective level is:

- The level configured in `modules.<module_name>` if present.
- Otherwise, the value of `level`.

### Example

```yaml
logging:
  level: info
```

With this configuration:

- Messages `info`, `warn`, `error`, `critical` are emitted.
- Messages `trace` and `debug` are dropped.

### Changing the global level

```yaml
# Verbose development mode
logging:
  level: debug

# Silent production mode
logging:
  level: warn
```

---

## 5. Per-module levels (`modules`)

Each subsystem of the service is identified by a named module. The `modules:` key allows adjusting the level of each one independently.

### Available modules

| Module | Content |
|---|---|
| `sea.boot` | Service startup, migrations, initialization. |
| `sea.http` | HTTP handlers, authorization, routes. |
| `sea.application` | Application services. |
| `sea.persistence` | Database queries, seeds, schema. |
| `sea.runtime` | Validation, serialization. |
| `sea.security` | Authentication, tokens, cleanup. |
| `seastar` | Internal logs of the network framework. |

### Configuration

```yaml
logging:
  level: info
  modules:
    sea.http: debug
    sea.persistence: warn
    seastar: error
```

### Behaviour

For each emitted message:

1. The system identifies the relevant module.
2. If an entry exists in `modules:` for that module, the associated level is applied.
3. Otherwise, the global level defined by `level:` is applied.

### Typical use cases

| Goal | Configuration |
|---|---|
| Diagnose an HTTP issue in production | `modules: { sea.http: debug }` |
| Reduce Seastar noise | `modules: { seastar: warn }` |
| Trace seeds and migrations in detail | `modules: { sea.persistence: debug, sea.boot: debug }` |
| Track authentication operations | `modules: { sea.security: debug }` |

### Complete example

```yaml
logging:
  level: info
  modules:
    sea.http: debug          # all HTTP details
    sea.persistence: info    # normal DB operations
    sea.boot: info           # standard startup logs
    seastar: warn            # only framework warnings
```

---

## 6. Sinks (log destinations)

A **sink** is a log destination. Multiple sinks can be declared simultaneously; each message is sent to all active sinks.

### Supported sink types

| Type | Description |
|---|---|
| `console` | Write to standard error output (stderr). |
| `file` | Write to a file on disk, with optional rotation. |

### Declaration

```yaml
logging:
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
        max_files: 10
```

### Common keys for all sinks

| Key | Type | Default | Description |
|---|---|---|---|
| `type` | enum | **Required** | Sink type. Values: `console`, `file`. |
| `format` | enum | `text` | Output format. Values: `text`, `json`. See [section 10](#10-log-format). |
| `enabled` | boolean | `true` | Enables or disables this sink. A disabled sink receives no messages. |

### Multiple sinks

Sinks coexist. A typical configuration combines a `console` sink for developers and a `file` sink for persistence:

```yaml
sinks:
  - type: console
    format: text
    enabled: true
  - type: file
    format: json
    enabled: true
    path: "./logs/service.log"
```

Each message is thus both displayed in the console and stored as JSON in a file.

---

## 7. The `console` sink

### Configuration

```yaml
- type: console
  format: text
  enabled: true
```

### Accepted keys

| Key | Type | Default | Description |
|---|---|---|---|
| `type` | enum | — | Exact value: `console`. |
| `format` | enum | `text` | Output format. Values: `text`, `json`. |
| `enabled` | boolean | `true` | Enables the sink. |

### Behaviour

Messages are written to **stderr**. The `text` format produces readable colored lines (depending on terminal capability). The `json` format produces line-delimited JSON.

The console sink does not support rotation: it continuously writes to stderr.

---

## 8. The `file` sink

### Configuration

```yaml
- type: file
  format: json
  enabled: true
  path: "./logs/service.log"
  rotation:
    max_size: "100MB"
    time_pattern: daily
    max_files: 10
    compress: false
```

### Accepted keys

| Key | Type | Default | Description |
|---|---|---|---|
| `type` | enum | — | Exact value: `file`. |
| `format` | enum | `text` | Output format. |
| `enabled` | boolean | `true` | Enables the sink. |
| `path` | string | **Required** | Path to the log file. The parent directory is created automatically if it does not exist. |
| `rotation` | block | defaults | Rotation configuration. See [section 9](#9-file-rotation). |

### Behaviour

Messages are written to the file indicated by `path`. The system opens the file in append mode: service restarts preserve existing logs.

If the file does not exist, it is created. If the parent directory does not exist, it is created recursively with the process's standard permissions.

### Permissions and accessibility

Log files are created with the process's default permissions. For environments where logs are read by another user (for example a centralization agent like Promtail), ensure that the directory's permissions allow reading.

---

## 9. File rotation

Rotation limits the size of log files and maintains a history.

### Configuration

```yaml
rotation:
  max_size: "100MB"
  time_pattern: daily
  max_files: 10
  compress: false
```

### Accepted keys

| Key | Type | Default | Description |
|---|---|---|---|
| `max_size` | size | `100MB` (100 * 1024 * 1024 bytes) | Maximum file size before rotation. Format: see [section 15](#15-duration-and-size-formats). The value `0` disables size rotation. |
| `time_pattern` | enum | `daily` | Time rotation pattern. Values: `none`, `hourly`, `daily`. |
| `max_files` | integer | `10` | Maximum number of archives kept. Oldest ones are deleted. |
| `compress` | boolean | `false` | Enables archive compression. See limitation below. |

### Time rotation patterns

| Value | Effect |
|---|---|
| `none` | No time rotation. Only size can trigger a rotation. |
| `hourly` | A new file is created every hour. |
| `daily` (default) | A new file is created at midnight. |

### Combining size and time

Both mechanisms can be active simultaneously: rotation triggers as soon as either criterion is met. For example, with `max_size: "100MB"` and `time_pattern: daily`, a new file is created either when 100 MB is reached or at midnight, whichever comes first.

### Current limitation on compression

The system accepts the `compress: true` key, but effective archive compression is not natively implemented in the current version. Archives remain in uncompressed text (or JSON) format. A post-rotation hook to compress archives may be added later.

### Disabling rotation

```yaml
rotation:
  max_size: 0
  time_pattern: none
```

With this configuration, the file grows indefinitely. This option should only be used for short-duration test environments.

---

## 10. Log format

The `format` field of each sink controls message formatting.

### `text` format

Human-readable lines, with ANSI colors when the output is a terminal.

**Example:**

```
[2026-05-14 10:23:45.123] [sea.http] [info] login successful: alice@example.com
[2026-05-14 10:23:45.567] [sea.persistence] [warn] migration skipped: column already exists
[2026-05-14 10:23:46.012] [sea.security] [error] JWT verification failed: token expired
```

Suitable for:

- Console consultation during development
- Direct reading via `tail -f` or `less`

### `json` format

Line-delimited JSON (one object per line).

**Example:**

```json
{"timestamp":"2026-05-14T10:23:45.123Z","logger":"sea.http","level":"info","message":"login successful: alice@example.com"}
{"timestamp":"2026-05-14T10:23:45.567Z","logger":"sea.persistence","level":"warn","message":"migration skipped: column already exists"}
{"timestamp":"2026-05-14T10:23:46.012Z","logger":"sea.security","level":"error","message":"JWT verification failed: token expired"}
```

Suitable for:

- Ingestion by centralization tools (Loki, ELK, Datadog, Splunk)
- Programmatic parsing
- Analysis via JSON tools (`jq`, `gron`)

### JSON format fields

| Field | Description |
|---|---|
| `timestamp` | UTC date/time in ISO 8601 format with milliseconds. |
| `logger` | Name of the module that emitted the message. |
| `level` | Severity level (`trace`, `debug`, `info`, `warn`, `error`, `critical`). |
| `message` | Textual content of the message. |

Special characters in `message` are escaped according to the RFC 8259 standard.

### Choosing between formats

| Use case | Recommended format |
|---|---|
| Console sink for development | `text` |
| File sink for centralization | `json` |
| File sink for manual consultation | `text` |
| Audit and post-mortem investigation | `json` |

---

## 11. Immediate flush (`flush_level`)

The `flush_level` determines from which level onward messages are written immediately to disk, bypassing the internal buffer.

### Configuration

```yaml
logging:
  flush_level: error
```

### Accepted values

Same as `level`: `trace`, `debug`, `info`, `warn`, `error`, `critical`.

### Behaviour

| Case | Behaviour |
|---|---|
| Message of level < `flush_level` | The message is buffered. Disk write is deferred. |
| Message of level ≥ `flush_level` | The message is written immediately (synchronous flush). |

### Use case

The default value `error` ensures that all error-level messages and above are written immediately, even if the service crashes abruptly. Less critical messages (info, debug) are batched for performance.

### Recommendations

| Situation | Recommended value |
|---|---|
| Standard production | `error` (default) |
| Crash investigation | `warn` or `info` |
| Performance testing | `critical` (minimizes flushes) |

---

## 12. Asynchronous logging (`async`)

Asynchronous mode offloads log writes to a dedicated thread, so I/O operations do not slow down the main service.

### Configuration

```yaml
async:
  enabled: true
  queue_size: 8192
  overflow_policy: overrun_oldest
```

### Accepted keys

| Key | Type | Default | Description |
|---|---|---|---|
| `enabled` | boolean | `true` | Enables asynchronous mode. If `false`, writes are synchronous (the service waits for each message to be written before continuing). |
| `queue_size` | integer | `8192` | Buffer size in number of messages. |
| `overflow_policy` | enum | `overrun_oldest` | Behaviour when the queue is full. Values: `block`, `overrun_oldest`. |

### Overflow policies

| Value | Behaviour |
|---|---|
| `overrun_oldest` (default) | The oldest messages in the queue are overwritten by new ones. No service blocking, but potential loss of old messages. |
| `block` | The calling code waits for a slot to free up in the queue. No message is lost, but the service may be slowed down under extreme load. |

### Recommendations

| Use case | Configuration |
|---|---|
| High-load service | `queue_size: 16384`, `overflow_policy: overrun_oldest` |
| Critical service without loss | `queue_size: 32768`, `overflow_policy: block` |
| Development / debug | `enabled: false` (immediate synchronous writing) |

### Disabling asynchronous mode

```yaml
async:
  enabled: false
```

With `enabled: false`, each call to a log function waits for the write to finish before returning. This configuration guarantees no message is lost in case of a crash but can significantly slow down the service under load.

---

## 13. The `/admin/logs` endpoint

The system continuously maintains an in-memory buffer of the last 10,000 messages, exposed via a REST endpoint. This buffer is always active, regardless of sink configuration.

### Route

```
GET /admin/logs
```

### Required authentication

The endpoint is protected by two layers:

1. Valid JWT authentication (see `auth.md`).
2. User role matching `authorization.admin_role` (default `admin`).

Any request without authentication returns 401. Any request from a non-administrator user returns 403.

### Query parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `limit` | integer | `100` | Maximum number of entries returned. Capped at 1000. |
| `level` | enum | absent | Filter by minimum level. Values: `trace`, `debug`, `info`, `warn`, `error`, `critical`. |
| `logger` | string | absent | Exact filter by module name (for example `sea.http`). |
| `since` | integer | `0` | Returns only messages whose `sequence_id` is greater than this value. Used for polling. |
| `search` | string | absent | Filters messages whose content contains the provided substring (case-insensitive search). |

### Response format

```json
{
  "logs": [
    {
      "sequence_id": 12345,
      "timestamp": "2026-05-14T10:23:45.123Z",
      "logger": "sea.http",
      "level": "info",
      "message": "login successful: alice@example.com"
    },
    {
      "sequence_id": 12346,
      "timestamp": "2026-05-14T10:23:46.456Z",
      "logger": "sea.persistence",
      "level": "warn",
      "message": "skipping migration: column already exists"
    }
  ],
  "count": 2,
  "next_sequence_id": 12347,
  "buffer_size": 7234,
  "buffer_capacity": 10000
}
```

| Key | Description |
|---|---|
| `logs` | Array of messages matching the filters. |
| `count` | Number of messages returned in this response. |
| `next_sequence_id` | Identifier to pass as `since=` in the next request to retrieve only new messages. |
| `buffer_size` | Total number of messages currently in the buffer. |
| `buffer_capacity` | Maximum buffer capacity (10,000). |

### Polling pattern

For real-time log monitoring, a client can use the incremental polling pattern:

```bash
# First request
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8081/admin/logs?limit=100"
# Response: next_sequence_id = 12345

# Subsequent requests: retrieve only new ones
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8081/admin/logs?since=12345&limit=100"
# Response: next_sequence_id = 12387

# And so on with since=12387, etc.
```

This approach avoids retransmitting the entire buffer with each request.

### Filtering examples

**Retrieve only errors:**

```bash
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8081/admin/logs?level=error&limit=50"
```

**Follow HTTP operations in debug:**

```bash
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8081/admin/logs?logger=sea.http&level=debug"
```

**Search for a user in the logs:**

```bash
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8081/admin/logs?search=alice@example.com"
```

**Combine filters:**

```bash
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8081/admin/logs?logger=sea.security&level=warn&search=token"
```

### Limits and characteristics

| Characteristic | Value |
|---|---|
| Buffer capacity | 10,000 messages (FIFO) |
| Persistence | In memory only, lost on restart |
| Maximum limit per request | 1,000 |
| Server-side filtering | Yes |
| Performance | O(N) for `search` filters, O(1) for `since` and `level` |

### Distinction from sinks

The in-memory ring buffer of `/admin/logs` is **independent** of the configured sinks. Whatever the declared sinks (or their absence), the buffer remains active. Conversely, disabling the administration endpoint does not affect the sinks.

---

## 14. The `/admin/logs/loggers` endpoint

Complementary endpoint providing the list of available modules.

### Route

```
GET /admin/logs/loggers
```

### Required authentication

Same as `/admin/logs`: JWT authentication + administrator role.

### Response format

```json
{
  "loggers": [
    "sea.boot",
    "sea.http",
    "sea.application",
    "sea.persistence",
    "sea.runtime",
    "sea.security",
    "seastar"
  ]
}
```

### Purpose

This endpoint allows an administration interface to dynamically offer the list of available modules in a filtering menu, without hardcoding the module list.

---

## 15. Duration and size formats

Size values (`max_size`) and duration values (internal parameters) follow specific conventions.

### Size format

Sizes are expressed as strings with a unit suffix.

| Suffix | Multiplier |
|---|---|
| `B` or absent | Bytes |
| `K` or `KB` | Kilobytes (× 1024) |
| `M` or `MB` | Megabytes (× 1024²) |
| `G` or `GB` | Gigabytes (× 1024³) |

### Size examples

| YAML value | Resulting bytes |
|---|---|
| `"500"` | 500 |
| `"500B"` | 500 |
| `"100KB"` | 102,400 |
| `"100K"` | 102,400 |
| `"100MB"` | 104,857,600 |
| `"1GB"` | 1,073,741,824 |

### Duration format

Durations are expressed as strings with a unit suffix.

| Suffix | Unit |
|---|---|
| `s` or absent | Seconds |
| `m` | Minutes |
| `h` | Hours |
| `d` | Days |

### Duration examples

| YAML value | Resulting duration |
|---|---|
| `"30"` | 30 seconds |
| `"30s"` | 30 seconds |
| `"15m"` | 15 minutes |
| `"24h"` | 24 hours |
| `"7d"` | 7 days |

---

## 16. Configuration examples

### Configuration 1 — Simple development

Minimal configuration for a local environment.

```yaml
logging:
  level: debug
  sinks:
    - type: console
      format: text
      enabled: true
```

Behaviour: all messages from debug level upward are displayed in the console with colors.

### Configuration 2 — Production with centralization

Dual sinks: console for system journals, JSON file for ingestion.

```yaml
logging:
  level: info
  modules:
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
    queue_size: 16384
    overflow_policy: overrun_oldest
```

Behaviour:

- Messages displayed in the console (readable by operators).
- Messages stored in JSON in `./logs/service.log` for Loki/ELK ingestion.
- Daily rotation or at 100 MB, 30 archives kept (≈ one month).
- Asynchronous mode with large queue to absorb load peaks.

### Configuration 3 — Intensive debug of a specific module

Investigating an HTTP issue without disturbing other modules.

```yaml
logging:
  level: info
  modules:
    sea.http: trace
    sea.security: debug
  sinks:
    - type: console
      format: text
      enabled: true
    - type: file
      format: json
      enabled: true
      path: "./logs/debug.log"
      rotation:
        max_size: "500MB"
        time_pattern: none
        max_files: 5
  flush_level: trace
  async:
    enabled: false
```

Behaviour:

- All HTTP messages are traced (trace level).
- Security operations in debug.
- Other modules remain in info.
- Immediate flush on all levels (no risk of loss in case of crash).
- Synchronous mode to guarantee writing.

### Configuration 4 — Critical service without log loss

Configuration guaranteeing no log is lost.

```yaml
logging:
  level: info
  sinks:
    - type: file
      format: json
      enabled: true
      path: "/var/log/seadesktop/service.log"
      rotation:
        max_size: "1GB"
        time_pattern: daily
        max_files: 90
  flush_level: warn
  async:
    enabled: true
    queue_size: 65536
    overflow_policy: block
```

Behaviour:

- Single JSON file sink for long-term archiving.
- Daily rotation, 90 days of history.
- Immediate flush from warn level upward.
- Large queue (65,536 messages) and `block` policy which slows the service rather than losing messages.

### Configuration 5 — Full deactivation

For automated tests or environments without need for logs.

```yaml
logging:
  enabled: false
```

Behaviour: no message is emitted. Performance is maximal. The `/admin/logs` endpoint returns an empty buffer.

### Configuration 6 — Production with all elements

Configuration recommended as a starting point for a production deployment.

```yaml
logging:
  enabled: true
  level: info

  modules:
    sea.http: info
    sea.persistence: info
    sea.security: info
    sea.boot: info
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
        compress: false

  flush_level: error

  async:
    enabled: true
    queue_size: 8192
    overflow_policy: overrun_oldest
```
