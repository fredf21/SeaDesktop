# Installation and packaging

This guide covers two distinct audiences:

- **End users** who just want to install SeaDesktop and start using it
  → see [§1 End user installation](#1-end-user-installation).
- **Developers and contributors** who want to build SeaDesktop from
  source and produce distribution packages themselves → see
  [§2 Components overview](#2-components-and-platform-overview) and
  subsequent sections.

The companion file [`docker_deployment.md`](./docker_deployment.md)
covers backend deployment via Docker in detail.

---

## 1. End user installation

You don't need to compile anything to install SeaDesktop. Pre-built
packages are available for the three major platforms.

### 1.1 Linux (Ubuntu 24.04 and derivatives)

SeaDesktop is distributed as two `.deb` packages: one for the backend,
one for SeaUI. Both can be installed side-by-side on the same machine
or independently on different machines.

**Backend (REST API server)**

```bash
# Download from GitHub Releases
wget https://github.com/fredf21/SeaDesktop/releases/download/v1.0.1/seadesktop-backend-1.0.1-Linux.deb

# Install (pulls dependencies automatically)
sudo apt install ./seadesktop-backend-1.0.1-Linux.deb
```

The package installs:
- The backend binary at `/opt/seadesktop/bin/backend_seastar` (Seastar
  is statically linked - no need to install Seastar yourself)
- A wrapper script at `/usr/bin/seadesktop-backend`
- A systemd unit at `/lib/systemd/system/seadesktop-backend.service`
- An example configuration at `/usr/share/seadesktop/default.yaml.example`

After install, the post-install script prints the next three steps:

```bash
# 1. Create a config from the example template
sudo cp /usr/share/seadesktop/default.yaml.example /etc/seadesktop/default.yaml
sudo vim /etc/seadesktop/default.yaml
# Edit MySQL credentials, project name, services, etc.

# 2. Create the env file with the JWT secret
echo "SEA_DESKTOP_JWT_SECRET=$(openssl rand -hex 32)" | sudo tee /etc/seadesktop/seadesktop.env
sudo chmod 600 /etc/seadesktop/seadesktop.env
sudo chown root:seadesktop /etc/seadesktop/seadesktop.env

# 3. Enable and start the service
sudo systemctl enable --now seadesktop-backend
```
**Note**: the steps above (steps 1-3) are for running the backend
as a system service via systemd. If you only want to use SeaUI in
Local mode to manage backends interactively, you can skip these
steps — SeaUI's **first-time setup** (see §1.4) creates the necessary
configuration automatically in your user directory and launches the
backend on demand through `QProcess`. Use systemd only for unattended,
always-on deployments.

Verify the service is running:

```bash
sudo systemctl status seadesktop-backend
sudo journalctl -u seadesktop-backend -f
curl http://localhost:8080/health
# Returns: {"status":"RUNNING"}
```

**SeaUI (desktop client)**

```bash
# Download
wget https://github.com/fredf21/SeaDesktop/releases/download/v1.0.1/seaui-1.0.1-Linux.deb

# Install (pulls Qt6 dependencies from apt)
sudo apt install ./seaui-1.0.1-Linux.deb
```

The package installs:
- `/usr/bin/SeaUI` (the binary, linked against Qt 6.4 from Ubuntu)
- `/usr/share/applications/SeaUI.desktop` (menu entry)
- `/usr/share/icons/hicolor/256x256/apps/seaui.png` (application icon)

Launch SeaUI from your application menu (Super key, type "SeaUI") or
from the terminal:

```bash
SeaUI
```

On first launch, choose between **Local mode** (read/write YAML files
directly from your filesystem) or **Remote mode** (connect to a
deployed backend over HTTP/HTTPS).

### 1.2 macOS

> v1.0 binaries for macOS will be published when the macOS build is
> finalized. Track the release status at the GitHub Releases page.

When available, install with:

```
Download SeaUI-1.0.1.dmg, open it, drag SeaUI.app to Applications.
```

The backend on macOS runs in **Docker Desktop** (Seastar requires
Linux). See [`docker_deployment.md`](./docker_deployment.md) for the
Docker setup. SeaUI then connects to the backend via Remote mode at
`http://localhost:8080`.

### 1.3 Windows

> v1.0 binaries for Windows will be published when the Windows build
> is finalized. Track the release status at the GitHub Releases page.

When available, install with:

```
Double-click SeaUI-1.0.1-win64.exe and follow the installer wizard.
```

The backend on Windows runs in **Docker Desktop with WSL2**. SeaUI
connects to it via Remote mode at `http://localhost:8080`.

### 1.4 First launch - choosing your mode

When SeaUI opens for the first time, you see a **Connect to
SeaDesktop** dialog with two profile options:

- **Local (built-in)** - only usable on Linux with the `.deb`
  installed. SeaUI reads/writes YAML files on your filesystem and
  manages backend services through native processes.
- **Remote** - connects to a deployed backend over HTTP/HTTPS. On
  macOS and Windows this is the only available mode (the backend runs
  in Docker). Click **Manage Profiles, then + Add Remote** to register
  a backend URL (e.g. `http://localhost:8080` for local Docker,
  `https://api.example.com` for production).

#### First-time Local setup

When you select **Local** for the first time, SeaUI opens the
**Welcome to SeaUI** dialog with three configuration sections:

**1. Configuration folder**

Choose where SeaUI will store your YAML project files. The default
is `~/.local/share/SeaDesktop/SeaUI/configs/`, but you can pick any
folder — for example, a Git-versioned directory shared with your
team. Use the **Browse** button to navigate visually.

A checkbox lets you copy an example project (`BlogDemo.yaml`) into
the folder to get started. Recommended on first install.

**2. MySQL credentials**

The backend needs MySQL credentials to start. Fill in:
- **Host** (default: `127.0.0.1`)
- **Port** (default: `3306`)
- **User** (default: `root`)
- **Password** (use **Show/Hide** to toggle visibility)

If your MySQL root user has no password, leave the password empty.

**3. JWT secret**

A cryptographically secure 256-bit JWT secret is auto-generated for
you. Use **Regenerate** to replace it if needed. This secret signs
authentication tokens for projects that use auth.

#### Files created on the filesystem

When you click **Continue**, SeaUI creates this structure:
<parent>/
├── configs/                   # Your YAML project files
│   └── BlogDemo.yaml          # (if you kept the example checked)
└── environment/               # Secrets (never version this)
└── seadesktop.env         # MySQL + JWT (permissions 0600)

The separation between `configs/` and `environment/` lets you
safely version `configs/` in Git while keeping credentials local.

The `seadesktop.env` file has format:
MYSQL_HOST=127.0.0.1
MYSQL_PORT=3306
MYSQL_USER=root
MYSQL_PASSWORD=...
SEA_DESKTOP_JWT_SECRET=...

SeaUI loads this file every time it starts a backend service and
injects the variables into the backend process environment. This
means the backend can resolve `${MYSQL_PASSWORD:-root}` references
in your YAML projects regardless of how SeaUI was launched (terminal,
GNOME menu, etc.).

#### Reconfiguring later

To change credentials or move the configs folder after the first
launch, edit `~/.config/SeaDesktop/SeaUI.conf` and update the
`[local]/configsDir` key, or edit `<parent>/environment/seadesktop.env`
directly. A dedicated Preferences dialog is planned for a later
release.

For full SeaUI usage details, see
[`SEAUI_GUIDE.md`](./SEAUI_GUIDE.md).

---
### 1.5 System routes security

SeaDesktop exposes five system routes that behave differently
depending on whether authentication is enabled in your YAML:

| Route | When `auth=none` | When `auth=jwt` |
|---|---|---|
| `GET /health` | Public | Admin role required |
| `GET /health/ready` | Public | Admin role required |
| `GET /openapi.json` | Public | Admin role required |
| `GET /docs` (Swagger UI) | Public | Admin role required |
| `GET /assets/swagger-ui/*` | Public | Admin role required |

In **development mode** (no auth, typical for local exploration),
these routes are open so you can navigate the OpenAPI spec and try
endpoints in Swagger UI without authentication.

In **production mode** (auth enabled), these routes return 401
without a valid JWT and 403 if the JWT does not carry the admin role
configured in `authorization.admin_role`.

**Important for load-balancers**: if your YAML has `auth=jwt`, the
LB health checks must include a valid admin JWT in the
`Authorization` header. Alternatively, expose `/health` on a
separate internal port not protected by auth.

The rate limiting middleware (configured via
`service.security.rate_limits` in your YAML) is applied to **all**
routes including system routes.

---

## 2. Components and platform overview

For developers who want to build from source: SeaDesktop has two
distinct components with different build and distribution stories.

| Component | Linux native | macOS native | Windows native |
|---|---|---|---|
| `backend_seastar` | Yes | No (Seastar requires Linux) | No (Seastar requires Linux) |
| `SeaUI` (Qt 6 desktop) | Yes | Yes | Yes |

In practice:

- **The backend can ship two ways**: as a Linux `.deb` (recommended
  for Linux servers - the package bundles libmariadbcpp and statically
  links Seastar so it has no runtime dependencies beyond standard
  Ubuntu libraries), or as a Docker image (recommended for macOS,
  Windows, and multi-service Linux deployments).
- **SeaUI ships as a native desktop application** on each platform:
  `.deb` for Linux, `.dmg` for macOS, NSIS installer for Windows.

The rest of this document covers building SeaUI from source on the
three platforms, and the backend `.deb` on Linux. For the backend
Docker image build, see [`docker_deployment.md`](./docker_deployment.md).

---

## 3. Linux

### 3.1 Build dependencies

Install the toolchain and Qt 6.4 from Ubuntu 24.04 official repos:

```bash
sudo apt install build-essential cmake ninja-build pkg-config \
    libboost-program-options-dev libboost-thread-dev libboost-filesystem-dev \
    libssl-dev libyaml-cpp-dev libfmt-dev libgnutls28-dev \
    libhwloc-dev liburing-dev libsystemd-dev \
    qt6-base-dev qt6-tools-dev qt6-tools-dev-tools \
    qt6-webengine-dev linguist-qt6 \
    libmariadb-dev
```

For Seastar (required to build the backend natively), follow the
[official Seastar build instructions](https://github.com/scylladb/seastar/blob/master/HACKING.md).
The version known to work with SeaDesktop v1.0 is pinned at commit
`a2dd373e`.

### 3.2 Build SeaUI from the command line

SeaUI builds against Qt 6.4 from Ubuntu (no need for the Qt
installer):

```bash
cd /path/to/SeaDesktop

# Configure for SeaUI .deb (Release, no backend)
cmake -B build/debian-seaui \
    -DCMAKE_BUILD_TYPE=Release \
    -DPACKAGE_SEAUI_ONLY=ON

# Build SeaUI
cmake --build build/debian-seaui -j --target SeaUI
```

The binary lands at `build/debian-seaui/apps/SeaUI/SeaUI`. To run it
directly:

```bash
build/debian-seaui/apps/SeaUI/SeaUI
```

### 3.3 Build the backend from the command line

```bash
# Configure for backend .deb (Release, no SeaUI)
cmake -B build/debian-backend \
    -DCMAKE_BUILD_TYPE=Release \
    -DPACKAGE_BACKEND_ONLY=ON \
    -DBUILD_SEAUI=OFF

# Build (takes 15-20 minutes in Release with -O3)
cmake --build build/debian-backend -j --target backend_seastar
```

The binary lands at
`build/debian-backend/apps/Backend_Seastar/backend_seastar` (~15 MB
Release, stripped).

### 3.4 Build with Qt Creator

For day-to-day development, open `CMakeLists.txt` in Qt Creator and
configure the project with the Qt 6.8 Desktop kit (or Qt 6.4 from
Ubuntu - both work). Default build configuration is Debug; switch to
Release in **Projects, Build** for distribution builds.

For producing `.deb` packages, it's easier to use the command-line
workflow above, because the Release builds use distinct build
directories with specific CMake flags (`PACKAGE_BACKEND_ONLY`,
`PACKAGE_SEAUI_ONLY`).

### 3.5 Build .deb packages

The project uses CPack to produce Debian packages directly from the
CMake install rules. Two separate build directories produce two
separate packages.

**Backend `.deb`**:

```bash
cd /path/to/SeaDesktop

# Configure and build (15-20 min in Release)
cmake -B build/debian-backend \
    -DCMAKE_BUILD_TYPE=Release \
    -DPACKAGE_BACKEND_ONLY=ON \
    -DBUILD_SEAUI=OFF
cmake --build build/debian-backend -j

# Generate .deb
cd build/debian-backend
cpack -G DEB
# Produces seadesktop-backend-1.0.1-Linux.deb (~5.7 MB)
```

The package layout:

```
/opt/seadesktop/bin/backend_seastar       (binary)
/opt/seadesktop/lib/libmariadbcpp.so      (bundled)
/opt/seadesktop/lib/mariadb/plugin/*.so   (5 MariaDB plugins)
/usr/bin/seadesktop-backend               (wrapper)
/lib/systemd/system/seadesktop-backend.service
/usr/share/seadesktop/default.yaml.example
```

The wrapper sets `LD_LIBRARY_PATH=/opt/seadesktop/lib` so the bundled
`libmariadbcpp.so` is picked up at runtime. The postinst script
creates the `seadesktop` system user and the necessary directories.

**SeaUI `.deb`**:

```bash
# Configure and build (3-5 min in Release)
cmake -B build/debian-seaui \
    -DCMAKE_BUILD_TYPE=Release \
    -DPACKAGE_SEAUI_ONLY=ON
cmake --build build/debian-seaui -j

# Generate .deb
cd build/debian-seaui
cpack -G DEB
# Produces seaui-1.0.1-Linux.deb (~2.3 MB)
```

The package layout:

```
/usr/bin/SeaUI
/usr/share/applications/SeaUI.desktop
/usr/share/icons/hicolor/256x256/apps/seaui.png
```

The package depends on `libqt6core6t64`, `libqt6widgets6t64`,
`libqt6network6t64`, `libqt6webenginewidgets6` and others - all
provided by Ubuntu's official `qt6-*` packages.

### 3.6 Build an AppImage (alternative distribution)

If you prefer a single-file distribution that works on any modern
Linux without sudo:

```bash
# Install linuxdeploy and the Qt plugin (one-time)
wget https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
wget https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
chmod +x linuxdeploy-*.AppImage

# Build AppImage from the install tree
cmake --install build/debian-seaui --prefix AppDir/usr
./linuxdeploy-x86_64.AppImage --appdir AppDir --plugin qt --output appimage
```

The result is `SeaUI-x86_64.AppImage`, executable on any Linux
distribution with glibc >= 2.31.

---

## 4. macOS

### 4.1 Build dependencies

Install Qt 6.8+ via the open-source installer from [qt.io](https://qt.io)
(the Homebrew Qt is usable but harder to keep in sync with developer
machines).

Install Xcode command-line tools:

```bash
xcode-select --install
```

### 4.2 Build SeaUI from the command line

The backend cannot be built natively on macOS (Seastar is Linux-only).
Only build SeaUI on macOS, and run the backend in Docker.

```bash
cd /path/to/SeaDesktop

cmake -B build/release \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SEAUI=ON \
    -DCMAKE_PREFIX_PATH=~/Qt/6.8.3/macos \
    -G Ninja

cmake --build build/release --target SeaUI
```

The output is a macOS bundle at
`build/release/apps/SeaUI/SeaUI.app`.

### 4.3 Build with Qt Creator

Open `CMakeLists.txt`, configure with the Qt 6.8 macOS kit, switch to
Release, and build. Qt Creator handles the bundle generation
automatically.

### 4.4 Make the bundle self-contained with macdeployqt

The bundle as built depends on Qt frameworks at their original install
path. To make it portable, run `macdeployqt`:

```bash
~/Qt/6.8.3/macos/bin/macdeployqt \
    build/release/apps/SeaUI/SeaUI.app
```

This copies all required Qt frameworks into the bundle's
`Contents/Frameworks/` and rewrites the rpath. The bundle is now
self-contained and can be moved to any Mac.

### 4.5 Sign the application

For distribution outside the Mac App Store, sign with your Apple
Developer ID:

```bash
codesign --deep --force --verbose \
    --sign "Developer ID Application: Your Name (TEAMID)" \
    build/release/apps/SeaUI/SeaUI.app
```

Verify:

```bash
codesign --verify --deep --strict build/release/apps/SeaUI/SeaUI.app
```

### 4.6 Notarize and staple

For macOS 10.15+ Gatekeeper acceptance:

```bash
# Build a zip for notarization upload
ditto -c -k --keepParent build/release/apps/SeaUI/SeaUI.app SeaUI.zip

# Submit
xcrun notarytool submit SeaUI.zip \
    --apple-id "your@email.com" \
    --team-id "TEAMID" \
    --password "@keychain:AC_PASSWORD" \
    --wait

# Once accepted, staple the result
xcrun stapler staple build/release/apps/SeaUI/SeaUI.app
```

The `AC_PASSWORD` keychain entry must hold an app-specific password
generated from appleid.apple.com.

### 4.7 Build a DMG installer

Use `create-dmg` (install via `brew install create-dmg`):

```bash
create-dmg \
    --volname "SeaUI 1.0.1" \
    --window-pos 200 120 \
    --window-size 600 400 \
    --icon-size 100 \
    --icon "SeaUI.app" 175 190 \
    --hide-extension "SeaUI.app" \
    --app-drop-link 425 190 \
    "SeaUI-1.0.1.dmg" \
    build/release/apps/SeaUI/SeaUI.app
```

The result, `SeaUI-1.0.1.dmg`, is the file you distribute to users.
On first open, they see a drag-to-Applications dialog.

---

## 5. Windows

### 5.1 Build dependencies

Install one of these toolchains:

- **MSVC** - Visual Studio 2022 with the C++ workload (recommended
  for Qt6 official binaries).
- **MinGW** - MinGW-w64 11+ (works but slightly slower link times).

Install Qt 6.8+ via the open-source installer from [qt.io](https://qt.io),
selecting the appropriate toolchain (e.g. MSVC 2022 64-bit).

Install CMake 3.19+ and Ninja, both bundled with the Qt installer or
available as standalone downloads.

### 5.2 Build SeaUI from the command line

Like macOS, the backend cannot be built natively on Windows. Only
SeaUI builds; run the backend in Docker Desktop with the WSL2 backend.

Open a "Developer Command Prompt for VS 2022" (this sets up the MSVC
environment) and run:

```cmd
cd C:\path\to\SeaDesktop

cmake -B build\release ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DBUILD_SEAUI=ON ^
    -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64 ^
    -G Ninja

cmake --build build\release --target SeaUI
```

The binary is at `build\release\apps\SeaUI\SeaUI.exe`. The icon is
embedded in the EXE via `packaging\SeaUI.rc` and the multi-resolution
`icons\seaui.ico`.

### 5.3 Build with Qt Creator

Open `CMakeLists.txt`, select the Qt 6.8 MSVC kit, switch to Release,
build. Qt Creator handles the MSVC environment automatically.

### 5.4 Make the EXE self-contained with windeployqt

The EXE depends on Qt DLLs at their install path. Use `windeployqt` to
copy them next to the EXE:

```cmd
C:\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe ^
    --release ^
    --no-translations ^
    build\release\apps\SeaUI\SeaUI.exe
```

`windeployqt` analyzes the EXE, copies all required Qt DLLs
(`Qt6Core.dll`, `Qt6Widgets.dll`, etc.) and platform plugins into the
same folder. The folder is now portable: copy it to another Windows
machine and the EXE runs.

For translations, drop the `--no-translations` flag.

### 5.5 Build an NSIS installer

[NSIS](https://nsis.sourceforge.io/) produces native Windows
installers. From the project root:

```cmd
cd build\release
cpack -G NSIS
```

This produces `SeaUI-1.0.1-win64.exe`, an installer that:

- Lets the user pick an install directory (default
  `C:\Program Files\SeaUI`)
- Copies the EXE, all DLLs from `windeployqt`, and the icon
- Creates a Start Menu shortcut and an optional Desktop shortcut
- Registers an uninstaller in Add/Remove Programs

CPack reads the metadata (icon path, install dir, shortcut name,
license file) from the root `CMakeLists.txt` `CPACK_NSIS_*`
variables.

### 5.6 Sign the installer

For Windows SmartScreen acceptance, sign with a code-signing
certificate from a trusted CA (DigiCert, Sectigo, etc.):

```cmd
signtool sign /fd SHA256 /a /tr http://timestamp.digicert.com /td SHA256 ^
    SeaUI-1.0.1-win64.exe
```

The `/a` flag picks the first valid signing certificate from your
Windows certificate store; alternatively use `/f path\to\cert.pfx /p
password` for an explicit certificate file.

---

## 6. Post-install verification

Regardless of platform, validate that the installation works
end-to-end:

1. Launch SeaUI from the system menu (Start menu on Windows, Launchpad
   on macOS, Activities on GNOME).
2. The **Connect to SeaDesktop** dialog appears.
3. Select the **Local (built-in)** profile and click Connect - only on
   Linux with the `.deb` installed. On macOS/Windows, switch directly
   to step 4.
4. Use **Manage Profiles, then + Add Remote** to register a Remote
   profile pointing to your Docker backend (typically
   `http://localhost:8080` if Docker Desktop is running on the same
   machine, or `https://api.yourserver.com` for a remote deployment).
5. Connect with your administrator credentials.
6. Confirm the project list loads. The application is functional.

The application icon should appear in:

- **GNOME (Ubuntu)** - the dock (taskbar). The window title bar is
  empty by design; the icon shows up as a small logo in the
  application's top toolbar instead.
- **KDE, XFCE** - the title bar, taskbar, and Alt+Tab.
- **Windows** - title bar, taskbar, Alt+Tab, Start Menu.
- **macOS** - Dock, Launchpad, and Spotlight (no title bar icon on
  macOS by convention).

If any of these are missing, see the troubleshooting section of
[`SEAUI_GUIDE.md`](./SEAUI_GUIDE.md).
