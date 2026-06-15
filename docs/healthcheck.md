# Health Check

The SeaDesktop API exposes two distinct endpoints to monitor service
health, following the conventions of modern orchestrators (Kubernetes,
Docker Swarm, load balancers).

## Distinction between the two endpoints

| Endpoint | Purpose | k8s Probe | Response |
|----------|---------|-----------|----------|
| `GET /health` | The process is alive | `livenessProbe` | Always 200 if the HTTP server responds |
| `GET /health/ready` | The service can handle a request | `readinessProbe` | 200 if dependencies are OK, 503 otherwise |

This separation matters: if the database goes down temporarily, the
service should not be restarted (a restart won't fix anything) but it
should be removed from the load balancing pool until the connection
is restored.

## GET /health (liveness)

Ultra-lightweight endpoint without external dependencies. Used only to
verify that the process is running and the HTTP server accepts
requests.

### Request

```http
GET /health HTTP/1.1
```

### Response

```http
HTTP/1.1 200 OK
Content-Type: application/json

{ "status": "RUNNING" }
```

### Usage

Use as a k8s `livenessProbe`:

```yaml
livenessProbe:
  httpGet:
    path: /health
    port: 8000
  initialDelaySeconds: 5
  periodSeconds: 10
```

If this endpoint stops responding, the process is dead or stuck and a
restart is justified.

## GET /health/ready (readiness)

Checks that all critical dependencies of the service are responsive.

### Checks performed

| Check | Method | Considered OK if |
|-------|--------|------------------|
| `database` | No-op transaction (`BEGIN; COMMIT;`) | The transaction commits |
| `storage` | Call `exists()` on any path | No exception is raised |

The `storage` check is only present if the schema declares at least
one `File` field. Otherwise, it does not appear in the response.

### Request

```http
GET /health/ready HTTP/1.1
```

### Response — service ready

```http
HTTP/1.1 200 OK
Content-Type: application/json

{
  "status": "ready",
  "checks": {
    "database": "ok",
    "storage": "ok"
  },
  "timestamp": "2026-06-13T15:32:00.123Z"
}
```

### Response — service not ready

```http
HTTP/1.1 503 Service Unavailable
Content-Type: application/json

{
  "status": "not_ready",
  "checks": {
    "database": "error: Lost connection to MySQL server",
    "storage": "ok"
  },
  "timestamp": "2026-06-13T15:32:00.123Z"
}
```

The `503 Service Unavailable` status is the HTTP standard for
signaling that a service is temporarily unavailable. Orchestrators and
load balancers know how to interpret it and automatically remove the
pod from the active pool until the next passing check.

### Usage

Use as a k8s `readinessProbe`:

```yaml
readinessProbe:
  httpGet:
    path: /health/ready
    port: 8000
  initialDelaySeconds: 10
  periodSeconds: 5
  failureThreshold: 3
```

With this configuration, a service with a temporarily unavailable
database will be removed from the pool after 3 consecutive failures
(15 seconds), and reinstated as soon as `database` returns to `ok`.

## Security considerations

Both endpoints are **public** by default (no authentication required).
This is intentional: load balancers and orchestrators do not have
authentication tokens.

The information exposed is intentionally generic:

- `/health` reveals nothing beyond the HTTP status
- `/health/ready` may reveal internal error messages (e.g. "Lost
  connection to MySQL server") that could inform an attacker about
  the infrastructure

If this exposure is a concern, expose these endpoints only on a
private network (separate admin interface, VPN) rather than through
the public reverse proxy.
