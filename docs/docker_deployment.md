# Docker deployment

SeaDesktop ships with a Docker-based deployment that lets you run one
or several backend services (with a shared MySQL database) on any
machine that has Docker installed. This is the recommended way to
expose a SeaDesktop instance to remote SeaUI clients.

This document covers the quick-start path, the configuration knobs,
the multi-service architecture, the move from development to
production, day-to-day maintenance, and the most common issues.

---

## 1. Overview

A typical SeaDesktop Docker deployment looks like this:

```
Host machine (Linux, Mac or Windows with Docker)
│
├─ /var/lib/seadesktop/configs/   ← shared volume on the host
│   ├─ TestDemo.yaml
│   ├─ BlogDemo.yaml
│   └─ FileTest.yaml
│
└─ Docker
    ├─ Container mysql            ← shared database
    ├─ Container service_a        ← serves TestDemo.yaml on port 8080
    ├─ Container service_b        ← serves BlogDemo.yaml on port 8081
    └─ Container service_c        ← serves FileTest.yaml on port 8082
```

Several Backend_Seastar containers share a common `configs/` volume,
so all of them see every YAML project file. Each container runs one
project (selected with `--config` and `--service_name`) and exposes
its REST API on its own port. SeaUI can administer all the projects
by connecting to any of the services, since `/admin/projects/*` is
equivalent across services. The `/admin/restart` endpoint, however,
restarts only the service that received the call.

---

## 2. Platform support

The Docker-based deployment is the **recommended setup on macOS and Windows**, where Seastar (the backend's reactor framework) does not have native support.

| Platform | Native backend | Native SeaUI | Recommended workflow |
|---|---|---|---|
| **Linux (x86_64)** | ✅ Yes | ✅ Yes | Either native build or Docker. Local mode in SeaUI works. |
| **macOS** | ❌ No (Seastar requires Linux) | ✅ Yes | Backend in Docker Desktop, SeaUI native, **Remote mode required**. |
| **Windows** | ❌ No (Seastar requires Linux) | ✅ Yes | Backend in Docker Desktop with WSL2 backend, SeaUI native, **Remote mode required**. |

### Why is Seastar Linux-only?

Seastar relies on Linux-specific syscalls and kernel features that are not available on other platforms:
- `io_uring` for async I/O (Linux 5.1+)
- Fine-grained CPU pinning and NUMA awareness
- Low-level `epoll` and `eventfd` integration
- Direct hugepage allocation

The Seastar maintainers have explicitly chosen to focus on Linux to keep the performance characteristics that make the framework attractive. Porting to macOS or Windows would require rewriting large portions of the I/O layer.

### macOS workflow

1. Install **Docker Desktop for Mac**.
2. Clone the SeaDesktop repository.
3. Follow section 3 below (Quick start) — `docker compose up -d` works identically on macOS.
4. Build SeaUI natively for macOS using Qt Creator.
5. In SeaUI, create a Remote profile pointing to `http://localhost:8080`. Docker Desktop forwards the port from the Linux VM to the host.

The Local mode in SeaUI is **not usable on macOS** because it requires the `backend_seastar` binary on the local filesystem, which cannot be built natively. The Add/Edit/Import operations in SeaUI work on the YAML files served by the remote backend instead.

### Windows workflow

1. Install **Docker Desktop for Windows** with the **WSL2 backend** enabled (recommended over the legacy Hyper-V backend for performance).
2. Clone the SeaDesktop repository inside the WSL2 filesystem (faster than from `/mnt/c/`).
3. Follow section 3 below (Quick start) from a WSL2 terminal.
4. Build SeaUI natively for Windows using Qt Creator.
5. In SeaUI, create a Remote profile pointing to `http://localhost:8080`.

The same constraint applies as on macOS: Local mode is not usable, only Remote.

### Linux workflow

Linux users have the full choice between native and Docker workflows:

- **Native** — build `backend_seastar` directly with CMake, run it on the host. SeaUI in Local mode reads/writes the YAML files on disk. Best for development and small setups.
- **Docker** — follow section 3 below. Useful for multi-service deployments, production, or matching the macOS/Windows workflow when developing cross-platform tooling.

---

## 3. Prerequisites

You need:

- **Docker Engine 24+** with the Compose plugin (`docker compose`,
  not `docker-compose`). Verify with `docker compose version`.
- **A Linux/Mac/Windows host** with at least **4 GB of RAM** for the
  build, and **2 GB of free RAM** at runtime per service. Less is
  possible but the build will take longer or may fail.
- **About 20 GB of free disk** for the first build (Seastar is
  compiled from source as part of the image). Subsequent rebuilds
  are much smaller thanks to Docker layer caching.
- **`openssl`** installed locally (or any tool that can generate a
  random secret) to produce the JWT signing key.

---

## 4. Quick start

The following sequence brings up the full stack from a fresh clone.

### 4.1 Clone and prepare the environment

```bash
git clone https://github.com/yourorg/SeaDesktop.git
cd SeaDesktop

# Create the .env file from the template
cp .env.example .env
```

### 4.2 Fill in the .env file

Open `.env` in your editor and set:

```env
# Generate with: openssl rand -base64 48
SEA_DESKTOP_JWT_SECRET=your-long-random-secret-at-least-32-chars

# Choose a strong password
MYSQL_ROOT_PASSWORD=your-strong-mysql-password

# Path to the configs/ folder on the host. Default: ./configs
SEA_DESKTOP_CONFIGS_HOST_DIR=./configs
```

### 4.3 Build the image

```bash
docker compose build service_a
```

The first build compiles Seastar from source and takes 20-30 minutes
on a typical machine. Be patient and watch the `[N/total]` progress
counter. Subsequent builds, after a source code change, take 2-5
minutes since Seastar stays in the Docker layer cache.

### 4.4 Start the stack

```bash
docker compose up -d
```

This brings up MySQL first, then the three example services. You can
verify everything is up with:

```bash
docker compose ps
```

You should see four containers in the `running` state. Each backend
service replies on its own port:

```bash
curl http://localhost:8080/health
curl http://localhost:8081/health
curl http://localhost:8082/health
```

Each should return `{"status":"RUNNING"}`.

### 4.5 First time setup: create an administrator

The backend services start with no users. Before you can administer
them via SeaUI (Remote mode) or `/admin/*` endpoints, create an
administrator account.

For this to work, your YAML must declare at least one entity with
`is_auth_source: true` and a `password` field. The shipped
`TestDemo.yaml` does this. If you write your own YAML, make sure
you include something like:

```yaml
entities:
  - name: User
    options:
      enable_crud: true
      is_auth_source: true
      timestamps: true
    fields:
      - name: id
        type: uuid
        required: true
        unique: true
      - name: email
        type: email
        required: true
        unique: true
      - name: password
        type: password
        required: true
      - name: role
        type: string
        required: true
```

Then create the admin via the `/auth/register` endpoint:

```bash
curl -X POST http://localhost:8080/auth/register \
  -H "Content-Type: application/json" \
  -d '{
    "email":    "admin@example.com",
    "password": "ChangeMe123!",
    "role":     "admin"
  }'
```

You now have an administrator account. Sign in via SeaUI's Connect
dialog (Remote profile) or by calling `/auth/login` directly.

> **Security note.** In v1.0, the `role` field is accepted as-is
> during registration, which means anyone with network access to
> `/auth/register` can create an administrator account. For a public
> deployment, disable open registration once the first admin is
> created (a hardening pass is planned for v1.1).

---

## 5. Configuration

### 5.1 Environment variables

The `.env` file feeds the following variables to `docker-compose.yml`:

| Variable | Default | Purpose |
|---|---|---|
| `SEA_DESKTOP_JWT_SECRET` | (none) | Signing key for JWT tokens. Must be at least 32 characters, randomly generated. **Must not change** once users exist, otherwise their tokens become invalid. |
| `MYSQL_ROOT_PASSWORD` | (none) | Root password for the MySQL container. |
| `SEA_DESKTOP_CONFIGS_HOST_DIR` | `./configs` | Path on the host to the folder containing the YAML projects. Mounted read-write into every backend container at `/app/configs`. |

### 5.2 The configs volume

Every backend container mounts the same configs folder at
`/app/configs`. This is what makes the `/admin/projects/*` endpoints
equivalent across services: a file written by one service is
immediately readable by all the others.

For development, the default `./configs` (relative to your
docker-compose folder) is convenient. For production, point
`SEA_DESKTOP_CONFIGS_HOST_DIR` to a persistent location:

```env
SEA_DESKTOP_CONFIGS_HOST_DIR=/var/lib/seadesktop/configs
```

This way, removing and recreating the containers does not affect the
project files.

### 5.3 Ports

In the example `docker-compose.yml`, the three services bind to host
ports 8080, 8081 and 8082. To change this, edit the `ports:` section
of each service. The container always listens on port 8080
internally; only the host mapping changes.

### 5.4 The MariaDB plugin path

The `mariadb-connector-cpp` library shipped with SeaDesktop has a
hard-coded plugin directory baked in at build time. The
`docker-compose.yml` overrides this with the environment variable
`MARIADB_PLUGIN_DIR: /usr/local/lib/mariadb/plugin`, which points to
the location where the runtime image installs the plugins. Do not
remove this variable.

---

## 6. Multi-service architecture

### 6.1 Adding a service

The example `docker-compose.yml` has three services (`service_a`,
`service_b`, `service_c`). To add a fourth, duplicate one of the
blocks and adapt:

```yaml
  service_d:
    image: seadesktop/backend:latest
    container_name: seadesktop_service_d
    restart: unless-stopped
    depends_on:
      mysql:
        condition: service_healthy
      service_a:
        condition: service_started
    environment:
      SEA_DESKTOP_JWT_SECRET: ${SEA_DESKTOP_JWT_SECRET}
      MYSQL_HOST: mysql
      MYSQL_USER: root
      MYSQL_PASSWORD: ${MYSQL_ROOT_PASSWORD}
      SEA_DESKTOP_CONFIGS_DIR: /app/configs
      MARIADB_PLUGIN_DIR: /usr/local/lib/mariadb/plugin
    command: >
      --config /app/configs/MyNewProject.yaml
      --service_name MyServiceName
    volumes:
      - ${SEA_DESKTOP_CONFIGS_HOST_DIR:-./configs}:/app/configs
      - ./logs/service_d:/app/logs
    networks:
      - seadesktop_net
    ports:
      - "8083:8080"
```

Then `docker compose up -d service_d`.

### 6.2 Why are all services on internal port 8080?

Inside each container, the backend always listens on 8080. The
`ports:` mapping rewrites the host-side port (8080, 8081, 8082, …).
This keeps the internal configuration uniform: the same image and
the same YAML can run as any service simply by passing different
`--config` and `--service_name` arguments.

### 6.3 Restart semantics

The `/admin/restart` endpoint restarts **only the container that
received the request**. To restart another service, send the request
to that service's URL.

### 6.4 Limitation: new projects do not auto-deploy

Creating a new YAML project via SeaUI (or `POST /admin/projects/...`)
writes the file to the shared volume but does **not** start a new
container for it. You must manually add a new service block in
`docker-compose.yml` and run `docker compose up -d`.

This limitation is intentional in v1.0: a fully automated workflow
would require either Docker socket access from inside a container
(security risk) or a dedicated orchestrator daemon on the host. The
manual step is the safe compromise. A v1.1 orchestrator daemon is
planned.

---

## 7. Production deployment

For production, use `docker-compose.prod.yml` as an override of the
base `docker-compose.yml`. It replaces the `.env`-based secrets with
Docker secrets, which are stored encrypted (in Swarm) or as
plain-text files with strict permissions (in single-node Compose).

### 7.1 Create the secret files

```bash
mkdir -p secrets
chmod 700 secrets

# JWT secret
openssl rand -base64 48 > secrets/jwt_secret.txt

# MySQL password
echo 'your-strong-password' > secrets/mysql_password.txt

chmod 600 secrets/*.txt
```

### 7.2 Bring up the stack with the production override

```bash
docker compose \
  -f docker-compose.yml \
  -f docker-compose.prod.yml \
  up -d
```

The override:

- removes the host port mapping for MySQL (so MySQL is only reachable
  from inside the Docker network);
- reads `SEA_DESKTOP_JWT_SECRET` and `MYSQL_PASSWORD` from files
  mounted under `/run/secrets/` instead of environment variables.

### 7.3 Reverse proxy and HTTPS

The Docker image exposes plain HTTP. For HTTPS, put a reverse proxy
(nginx, Caddy, Traefik) in front of the services. The proxy
terminates TLS, then forwards to the backend containers over the
internal Docker network.

**Production setup with Let's Encrypt:**

```nginx
server {
    listen 443 ssl http2;
    server_name api.example.com;

    ssl_certificate     /etc/letsencrypt/live/api.example.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/api.example.com/privkey.pem;

    location / {
        proxy_pass http://service_a:8080;
        proxy_set_header Host              $host;
        proxy_set_header X-Real-IP         $remote_addr;
        proxy_set_header X-Forwarded-For   $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }
}
```

When using a reverse proxy, configure your Remote profile in SeaUI
with the HTTPS URL (`https://api.example.com`) rather than the
internal HTTP port. SeaUI uses Qt's built-in HTTPS support, which
validates the server certificate against the system truststore —
nothing to configure on the client side when the certificate is
signed by a recognized CA.

#### Testing HTTPS locally with a self-signed certificate

For development and staging, the repository includes a ready-to-use
test setup under `tests/https/` and a Docker Compose override
`docker-compose.https.yml` at the root.

```bash
# 1. Generate a self-signed certificate (one-time)
openssl req -x509 -newkey rsa:2048 -nodes -days 365 \
    -keyout tests/https/key.pem -out tests/https/cert.pem \
    -subj "/CN=localhost" \
    -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"

# 2. Start the stack with the HTTPS override
docker compose \
    -f docker-compose.yml \
    -f docker-compose.https.yml \
    up -d --wait

# 3. Test: -k accepts the self-signed cert
curl -k https://localhost/health
# → {"status":"RUNNING"}
```

To make Qt (and therefore SeaUI) trust the self-signed certificate
without modifying the application code, add it to the system
truststore:

```bash
# Ubuntu / Debian
sudo cp tests/https/cert.pem /usr/local/share/ca-certificates/seadesktop-test.crt
sudo update-ca-certificates
```

After this, restart SeaUI and create a Remote profile with the URL
`https://localhost`. The connection should succeed without any
certificate warning.

> **Why the truststore step is needed.** Qt's `QNetworkAccessManager`
> validates server certificates against the system truststore. A
> self-signed certificate not in the truststore is rejected with a
> clear error message ("SSL handshake failed: self-signed
> certificate"). Production certificates issued by Let's Encrypt or
> any other recognized CA work out of the box, with no client-side
> configuration.

---

## 8. Maintenance

### 8.1 Viewing logs

For all containers:
```bash
docker compose logs -f
```

For one service:
```bash
docker compose logs -f service_a
```

The backend also writes structured logs to `/app/logs` inside the
container, which is mounted from `./logs/service_X` on the host.

### 8.2 Restarting a service

To restart a single service:

```bash
docker compose restart service_a
```

To trigger a restart from a remote SeaUI client without SSH access,
use the `/admin/restart` endpoint (see `admin.md`). SeaUI's Restart
button in Remote mode calls this endpoint automatically.

### 8.3 Updating

To pick up a new version of the backend:

```bash
git pull
docker compose build service_a
docker compose up -d
```

Compose detects the new image and recreates the containers without
losing the MySQL data or the configs volume.

### 8.4 Backup

Two things to back up:

- **MySQL data** — the named volume `mysql_data`. Use
  `docker compose exec mysql mysqldump ...` or a regular volume
  backup tool.
- **The configs folder** — whatever path `SEA_DESKTOP_CONFIGS_HOST_DIR`
  points to. Plain file backup, since the YAMLs are text.

### 8.5 Cleanup

To stop everything without losing data:
```bash
docker compose down
```

To stop **and erase all MySQL data** (rarely what you want):
```bash
docker compose down -v
```

---

## 9. Troubleshooting

### 9.1 The build runs out of memory

If the Seastar build phase ends with `Killed signal terminated
program cc1plus` and `cannot allocate memory`, lower the parallelism
in the Dockerfile. Find this line in the seastar stage:

```dockerfile
RUN cd /opt/seastar \
    && ./configure.py --mode=release --without-tests --without-demos --without-apps \
    && ninja -C build/release -j 2 \
    && cmake --install build/release
```

Lower the `-j 2` to `-j 1` (slower but uses less RAM).

### 9.2 The container exits immediately

Run `docker compose logs service_a` to see the cause. The most
common reasons:

- **`error while loading shared libraries`** — the runtime image is
  missing a `.so`. Check that the library is listed in the runtime
  stage `apt-get install` and that the `mariadb/` plugin folder is
  copied. See the Dockerfile.

- **`Plugin caching_sha2_password could not be loaded`** — the
  `MARIADB_PLUGIN_DIR` variable is not set on the service in
  `docker-compose.yml`. Add it.

- **`Service introuvable: XXX`** — the `--service_name` argument
  does not match any service declared in the YAML's `services:`
  list. Fix the `command:` in `docker-compose.yml` or rename the
  service in the YAML.

### 9.3 `/auth/login` returns 404

The `/auth/*` routes are registered only if the YAML declares at
least one entity with `is_auth_source: true`. Without it, the
backend can still verify JWTs (for protected endpoints) but does
not expose any way to obtain one. Add the marker to the User entity,
restart the service.

### 9.4 SeaUI Remote login fails with "Login response missing access_token"

The backend is configured with `token_delivery: cookie`. SeaUI does
not support cookie-based authentication in v1.0. Change the YAML
to `token_delivery: body` or `token_delivery: both`, restart the
service.

### 9.5 Cannot list projects: "Admin role required"

The user you logged in with does not have the administrator role.
Either re-register a user with `"role": "admin"`, or update an
existing user directly in the database:

```bash
docker compose exec mysql mysql -uroot -p<password> <database> -e \
  "UPDATE User SET role='admin' WHERE email='you@example.com';"
```

The table name follows your YAML's entity name with case adjusted
by your MySQL configuration (usually lowercased: `user`).

### 9.6 The host user owns the bind-mounted files

Inside the container, the backend runs as UID 1000 (`seadesktop`).
On the host, files written by the backend (logs, generated YAML
files) will be owned by your local UID 1000 user. If your host user
has a different UID, you may see permission issues.

To align UIDs, either:

- run `chown -R 1000:1000 ./configs ./logs` on the host;
- or edit the Dockerfile to use the same UID as your host user (and
  rebuild).

---

## 10. Implementation notes

This section is for contributors who want to understand the build
choices. Regular users do not need to read it.

### 10.1 Multi-stage Dockerfile

The Dockerfile has three logical stages:

| Stage | Purpose | Cache invalidation |
|---|---|---|
| `seastar` | Clone and compile Seastar from source at the pinned commit. Heavy (~25 min on first build) but cached for as long as `SEASTAR_COMMIT` does not change. | Only if `SEASTAR_COMMIT` is changed in the Dockerfile. |
| `backend` | Compile `backend_seastar` against the Seastar libraries from the previous stage. Light (~2-5 min). | On every source code change. |
| `runtime` | Final image. Ubuntu 24.04 minimal, runtime libraries only, no compiler, no headers. ~150 MB. | When the backend stage rebuilds. |

The Seastar commit is pinned (`a2dd373e` at the time of writing,
based on tag `seastar-25.05.0`). To upgrade Seastar, change the
`SEASTAR_COMMIT` ARG at the top of the Dockerfile and rebuild with
`--no-cache` on the seastar stage.

### 10.2 Why `--without-tests --without-demos --without-apps`?

Without these flags, Seastar's build compiles ~400 test executables.
This adds 20+ minutes of CPU time and is the main cause of OOM
failures on smaller machines. SeaDesktop does not use Seastar's
tests, so we skip them.

### 10.3 Why `-j 2`?

The default for `ninja` (which is what `make -j` becomes) is to use
all available cores. Each parallel C++ compilation can consume
1.5-2 GB of RAM. On a machine with 8 GB and 8 cores, 8 simultaneous
`cc1plus` instances easily exhaust memory. The hardcoded `-j 2`
trades build speed for reliability across a wide range of hardware.

### 10.4 The `BUILD_SEAUI` CMake flag

The root `CMakeLists.txt` declares
`option(BUILD_SEAUI "Build the SeaUI desktop application" ON)`.
The Dockerfile passes `-DBUILD_SEAUI=OFF` because the server image
has no use for Qt6 and saving ~500 MB of dependencies is worth it.
The flag defaults to `ON` for local development builds, which need
SeaUI.

### 10.5 The mariadb-connector-cpp plugin path

The `libmariadbcpp.so` shipped under `third_party/` is built from
source by the local developer and embeds a hard-coded plugin
directory path that points to the developer's home folder
(`/home/.../third_party/mariadb-connector-cpp/install/lib/mariadb/plugin/`).
This path does not exist in the container.

The Dockerfile copies the plugin folder to
`/usr/local/lib/mariadb/plugin/` and `docker-compose.yml` sets
`MARIADB_PLUGIN_DIR` to that path. The environment variable
overrides the hard-coded path at runtime.

A v1.1 chantier could either statically link the connector or build
it inside the container with a fixed path. For now, the override is
the simplest fix.
