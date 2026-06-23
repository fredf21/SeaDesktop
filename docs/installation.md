# Installation and packaging

This guide explains how to build SeaDesktop from source and produce
distribution-ready packages for each supported platform. It covers
both the command-line and Qt Creator workflows. The companion file
[`docker_deployment.md`](./docker_deployment.md) covers the backend
deployment in detail.

---

## 1. Components and platform overview

SeaDesktop has two distinct components, with different build and
distribution stories:

| Component | Linux native | macOS native | Windows native |
|---|---|---|---|
| `backend_seastar` | ✅ | ❌ Seastar requires Linux | ❌ Seastar requires Linux |
| `SeaUI` (Qt 6 desktop) | ✅ | ✅ | ✅ |

In practice this means:

- **The backend ships as a Docker image** on all platforms. Even on
  Linux, Docker is the recommended distribution channel because it
  bundles MySQL and the runtime environment. macOS and Windows users
  run the backend in Docker Desktop, which transparently uses a
  Linux VM under the hood. The build and distribution of the
  backend image is fully described in
  [`docker_deployment.md`](./docker_deployment.md).
- **SeaUI ships as a native desktop application** on each platform.
  The remainder of this document covers SeaUI build and packaging.
  In Remote mode (the default in v1.0 for non-Linux users) SeaUI
  connects to the Dockerized backend over HTTP, so the two
  components never have to share an OS.

The rest of this guide focuses on building and distributing SeaUI
for the three supported platforms.

---

## 2. Linux

### 2.1 Build dependencies

Install Qt 6.8+ and the build toolchain. On Ubuntu 24.04:

```bash
sudo apt install build-essential cmake ninja-build pkg-config \
    libgl1-mesa-dev libxkbcommon-dev \
    libqt6core6 libqt6widgets6 libqt6network6 \
    qt6-base-dev qt6-tools-dev qt6-l10n-tools \
    qt6-webengine-dev
```

For the Qt installer route (recommended for matching Qt versions
between developer machines), grab the open-source installer from
qt.io and install Qt 6.8.3 to `~/Qt/`.

### 2.2 Build from the command line

```bash
cd /path/to/SeaDesktop

# Configure : disable backend (Linux-only and heavy to build),
# only build SeaUI.
cmake -B build/release \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SEAUI=ON \
    -G Ninja

# Build SeaUI
cmake --build build/release --target SeaUI
```

The binary is at `build/release/apps/SeaUI/SeaUI`. To run it
directly:

```bash
build/release/apps/SeaUI/SeaUI
```

### 2.3 Build with Qt Creator

1. Open `CMakeLists.txt` in Qt Creator.
2. Configure the project with the Qt 6.8 Desktop kit.
3. Switch the build configuration to **Release**.
4. In **Build Settings ▸ CMake**, set `BUILD_SEAUI=ON`.
5. Build via Build ▸ Build All.

### 2.4 Install locally

The CMakeLists has install rules for the binary, the icon
(`/usr/share/icons/hicolor/256x256/apps/seaui.png`), and the
`.desktop` file (`/usr/share/applications/SeaUI.desktop`). Install
under `/usr/local` by default:

```bash
sudo cmake --install build/release
```

After install, refresh the desktop database so the new entry
appears in your menus immediately:

```bash
sudo update-desktop-database /usr/local/share/applications
sudo gtk-update-icon-cache /usr/local/share/icons/hicolor
```

### 2.5 Build a .deb package

The project uses CPack to produce a Debian package directly from
the CMake install rules. From the build directory:

```bash
cd build/release
cpack -G DEB
```

This produces `SeaUI-1.0.0-Linux.deb` in the build directory. The
package installs:

- `/usr/bin/SeaUI` (the binary)
- `/usr/share/applications/SeaUI.desktop`
- `/usr/share/icons/hicolor/256x256/apps/seaui.png`

Install it with:

```bash
sudo apt install ./SeaUI-1.0.0-Linux.deb
```

The package metadata (dependencies, maintainer, etc.) is declared
in the root `CMakeLists.txt` `CPACK_*` variables. Adapt the version
number, maintainer name, and description before publishing.

### 2.6 Build an AppImage (alternative distribution)

If you prefer a single-file distribution that works on any modern
Linux without sudo, build an AppImage with `linuxdeploy` and the
Qt plugin:

```bash
# Install linuxdeploy and the Qt plugin (one-time setup)
wget https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
wget https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
chmod +x linuxdeploy-*.AppImage

# Build AppImage from the install tree
cmake --install build/release --prefix AppDir/usr
./linuxdeploy-x86_64.AppImage --appdir AppDir --plugin qt --output appimage
```

The result is `SeaUI-x86_64.AppImage`, executable on any Linux
distribution with glibc >= 2.31.

---

## 3. macOS

### 3.1 Build dependencies

Install Qt 6.8+ via the open-source installer from qt.io (the
Homebrew Qt is usable but harder to keep in sync with developer
machines).

Install Xcode command-line tools:

```bash
xcode-select --install
```

### 3.2 Build from the command line

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

### 3.3 Build with Qt Creator

Open `CMakeLists.txt`, configure with the Qt 6.8 macOS kit, switch
to Release, and build. Qt Creator handles the bundle generation
automatically.

### 3.4 Make the bundle self-contained with macdeployqt

The bundle as built depends on Qt frameworks at their original
install path. To make it portable, run `macdeployqt`:

```bash
~/Qt/6.8.3/macos/bin/macdeployqt \
    build/release/apps/SeaUI/SeaUI.app
```

This copies all required Qt frameworks into the bundle's
`Contents/Frameworks/` and rewrites the rpath. The bundle is now
self-contained and can be moved to any Mac.

### 3.5 Sign the application

For distribution outside the Mac App Store, sign with your Apple
Developer ID:

```bash
codesign --deep --force --verbose --sign "Developer ID Application: Your Name (TEAMID)" \
    build/release/apps/SeaUI/SeaUI.app
```

Verify:

```bash
codesign --verify --deep --strict build/release/apps/SeaUI/SeaUI.app
```

### 3.6 Notarize and staple

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

### 3.7 Build a DMG installer

Use `create-dmg` (install via `brew install create-dmg`) for a
nicely styled disk image:

```bash
create-dmg \
    --volname "SeaUI 1.0.0" \
    --window-pos 200 120 \
    --window-size 600 400 \
    --icon-size 100 \
    --icon "SeaUI.app" 175 190 \
    --hide-extension "SeaUI.app" \
    --app-drop-link 425 190 \
    "SeaUI-1.0.0.dmg" \
    build/release/apps/SeaUI/SeaUI.app
```

The result, `SeaUI-1.0.0.dmg`, is the file you distribute to users.
On first open, they see a drag-to-Applications dialog.

---

## 4. Windows

### 4.1 Build dependencies

Install one of these toolchains:

- **MSVC** — Visual Studio 2022 with the C++ workload (recommended
  for Qt6 official binaries).
- **MinGW** — MinGW-w64 11+ (works but slightly slower link times).

Install Qt 6.8+ via the open-source installer from qt.io, selecting
the appropriate toolchain (e.g., MSVC 2022 64-bit).

Install CMake 3.19+ and Ninja, both bundled with Qt installer or
available as standalone downloads.

### 4.2 Build from the command line

Open a "Developer Command Prompt for VS 2022" (this sets up the
MSVC environment) and run:

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

### 4.3 Build with Qt Creator

Open `CMakeLists.txt`, select the Qt 6.8 MSVC kit, switch to
Release, build. Qt Creator handles the MSVC environment
automatically.

### 4.4 Make the EXE self-contained with windeployqt

The EXE depends on Qt DLLs at their install path. Use `windeployqt`
to copy them next to the EXE:

```cmd
C:\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe ^
    --release ^
    --no-translations ^
    build\release\apps\SeaUI\SeaUI.exe
```

`windeployqt` analyzes the EXE, copies all required Qt DLLs
(`Qt6Core.dll`, `Qt6Widgets.dll`, etc.) and platform plugins into
the same folder. The folder is now portable: copy it to another
Windows machine and the EXE runs.

For translations, drop the `--no-translations` flag to also copy
the localization files.

### 4.5 Build an NSIS installer

[NSIS](https://nsis.sourceforge.io/) (Nullsoft Scriptable Install
System) produces native Windows installers. From the project root:

```cmd
cd build\release
cpack -G NSIS
```

This produces `SeaUI-1.0.0-win64.exe`, an installer that:

- Lets the user pick an install directory (default `C:\Program Files\SeaUI`).
- Copies the EXE, all DLLs from `windeployqt`, and the icon.
- Creates a Start Menu shortcut and an optional Desktop shortcut.
- Registers an uninstaller in Add/Remove Programs.

CPack reads the metadata (icon path, install dir, shortcut name,
license file) from the root `CMakeLists.txt` `CPACK_NSIS_*`
variables. Adapt them to your branding before publishing.

### 4.6 Sign the installer

For Windows SmartScreen acceptance, sign with a code-signing
certificate from a trusted CA (DigiCert, Sectigo, etc.):

```cmd
signtool sign /fd SHA256 /a /tr http://timestamp.digicert.com /td SHA256 ^
    SeaUI-1.0.0-win64.exe
```

The `/a` flag picks the first valid signing certificate from your
Windows certificate store; alternatively use `/f path\to\cert.pfx
/p password` for an explicit certificate file.

---

## 5. Post-install verification

Regardless of platform, validate that the installation works
end-to-end:

1. Launch SeaUI from the system menu (Start menu on Windows,
   Launchpad on macOS, Activities on GNOME).
2. The Connect to SeaDesktop dialog appears.
3. Select the **Local (built-in)** profile and click Connect — only
   on Linux. On macOS/Windows, the binary cannot run a local
   backend; switch directly to step 4.
4. Use **Manage Profiles ▸ + Add Remote** to register a Remote
   profile pointing to your Docker backend (typically
   `http://localhost:8080` if Docker Desktop is running on the same
   machine, or `https://api.yourserver.com` for a remote
   deployment).
5. Connect with your administrator credentials.
6. Confirm the project list loads. The application is functional.

The application icon should appear in:

- **GNOME (Ubuntu)** — the dock (taskbar). The window title bar is
  empty by design; the icon shows up as a small logo in the
  application's top toolbar instead.
- **KDE, XFCE** — the title bar, taskbar, and Alt+Tab.
- **Windows** — title bar, taskbar, Alt+Tab, Start Menu.
- **macOS** — Dock, Launchpad, and Spotlight (no title bar icon on
  macOS by convention).

If any of these are missing, see the troubleshooting section of
[`SEAUI_GUIDE.md`](./SEAUI_GUIDE.md).
