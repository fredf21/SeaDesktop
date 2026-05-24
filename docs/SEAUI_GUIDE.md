# SeaUI — User and Technical Guide

SeaUI is the desktop front-end of the SeaDesktop platform. It is a Qt 6
application that lets a developer manage SeaDesktop projects, services and
entities, control the lifecycle of the generated backends, and inspect their
behaviour — all without editing YAML files by hand or using a terminal.

This guide documents the SeaUI desktop application only. The Seastar backend
is out of scope.

---

## 1. Overview

### 1.1 What SeaUI is for

A SeaDesktop project is described by a declarative YAML file. Each project
contains one or more services, and each service exposes entities that become
auto-generated REST APIs. SeaUI is the graphical layer over this model. With
it you can:

- browse every project found in the `configs/` folder, and drill down into
  its services, entities, fields and generated routes;
- create new projects, services and entities through guided dialogs that
  write the YAML for you;
- edit existing projects, services and entities;
- import and export YAML files;
- start, stop, restart and reload the backend processes of your services;
- authenticate against a running service and explore its data;
- open the Swagger documentation of a running service;
- read the log files produced by the services;
- switch the interface language at runtime.

### 1.2 How SeaUI relates to the backend

SeaUI never talks to the database directly. It does two things:

1. It reads and writes YAML configuration files in the `configs/` folder.
2. It launches the Seastar backend as a separate process (via `QProcess`),
   one process per running service.

When a service is started, the backend reads its YAML file, applies any
pending migrations according to the configured `migrations.mode`, and serves
the REST API. SeaUI then communicates with that running service over HTTP
(for status polling, authentication and data browsing). Any change SeaUI
makes to a YAML file only takes effect in the database when the corresponding
service is (re)started.

### 1.3 Folder layout

SeaUI works with two folders whose location depends on the build type:

- **Configuration folder** — holds the project YAML files. In a debug build
  it points at the repository `configs/` folder so you work directly on the
  versioned files. In a release build it is a standard writable application
  data folder.
- **Logs folder** — holds one `.log` file per service process. It follows the
  same debug/release rule.

Each project is stored as a single file named `<ProjectName>.yaml`. The
project name and its file name are always kept in sync.

---

## 2. The main window

When SeaUI starts it opens maximized. The window is organised as a series of
list panels that go from the most general (projects) to the most specific
(fields and routes), plus detail panels and action buttons.

### 2.1 The panels

**Projects** — lists every `.yaml` file found in the configuration folder.
Selecting a project loads its services into the Services panel and clears the
panels further down.

**Services** — lists the services of the selected project. Selecting a service
loads its entities, displays its details (port, database type), starts status
polling, and computes the full set of generated routes.

**Entities** — lists the entities of the selected service. Selecting an entity
loads its fields and filters the route list down to the routes related to that
entity.

**Fields** — lists the fields of the selected entity.

**Routes** — lists the REST routes. When a service is selected it shows all of
its routes; when an entity is selected it shows only that entity's routes. Each
route is drawn by a custom delegate that colours the HTTP method and adds a
coloured badge for paginated routes (PAGE, OFFSET, CURSOR).

### 2.2 Detail panels

When a service is selected, the detail panels show:

- **Service Details** — the service port and database type.
- **Service Status** — `RUNNING` or `STOPPED`, refreshed automatically by a
  background poller. The label is green when running, red when stopped.
- **Service Auth Status** — `Connected` or `Disconnected`, reflecting whether
  you are currently authenticated against the service.

### 2.3 Service action buttons

A row of buttons acts on the currently selected service:

- **Start / Stop / Restart** — control the backend process of the service.
  These buttons are enabled or disabled automatically depending on whether the
  service is currently running.
- **Swagger** — opens the service's Swagger documentation in an embedded
  browser window. Only available while the service is running.
- **Logs** — opens the service's log file in the system's default application.
- **Login / Logout** — authenticate or sign out against the running service.
- **Open Data** — fetches and displays the rows of the selected entity in a
  table.

### 2.4 Status polling

Whenever a service is selected, SeaUI starts polling its `/health` style
endpoint in the background at a fixed interval. The result drives the Service
Status label and the enabled state of the action buttons: a running service
enables Stop/Restart/Swagger and disables Start; an unreachable service does
the opposite. This means the buttons always reflect the real state of the
backend, even if the service was started or stopped outside SeaUI.

---

## 3. The menu

The menu bar has four menus: **File**, **Edit**, **Audits** and
**Services Actions**.

### 3.1 File menu

#### Add New Project

Creates a brand-new project with a minimal production configuration.

A dialog asks for two values: the **project name** and the **service name**.
Both are required and are normalised automatically — leading and trailing
spaces are removed, invalid characters are stripped, and inner spaces become
underscores.

A file `<ProjectName>.yaml` is then created in the configuration folder. If a
project of that name already exists, the operation is refused. The generated
YAML contains one service with a complete production configuration:

- a MySQL database block with migrations enabled in `conservative` mode;
- a security block: JWT authentication (the secret is read from the
  `${SEA_DESKTOP_JWT_SECRET}` environment variable), restricted CORS, strict
  security headers, and HTTP limits;
- a production logging block: a console sink and a rotating JSON file sink,
  with asynchronous logging enabled.

Because the JWT secret is taken from an environment variable, the service will
not start until `SEA_DESKTOP_JWT_SECRET` is defined in the environment.

#### Add New Service

Adds a new service to an existing project. A dialog asks which project to add
to, then asks for the service name. The new service is refused if a service of
that name already exists in the project. It is generated with the same
complete production configuration as the service created by Add New Project,
and appended to the `services:` list of the project YAML. Existing content and
comments of the file are preserved.

#### Add New Entity

Adds an entity to a service. The flow is:

1. choose the project;
2. choose the service within that project;
3. enter the entity name;
4. choose the entity options — `enable_crud`, `timestamps`, `soft_delete`
   (CRUD and timestamps are on by default);
5. enter the fields one at a time. For each field a dialog asks for its name,
   its type (from the list of supported types) and its `required`, `unique`
   and `indexed` attributes. After each field SeaUI asks whether to add
   another. At least one field is required.

The entity block is then inserted into the chosen service's `entities:`
section (the section is created if it does not exist), preserving the rest of
the file and its comments.

Finally SeaUI asks whether to apply the change to the database now. Answering
yes restarts the service, which makes the backend re-read the YAML and run the
migration according to the configured `migrations.mode`.

The supported field types are: `string`, `int`, `float`, `bool`, `timestamp`,
`uuid`, `bigint`, `smallint`, `decimal`, `json`, `binary`, `password`,
`email`, `text`, `file`.

#### Import Yaml

Lets you pick a `.yaml` (or `.yml`) file anywhere on disk and copies it into
the configuration folder. If a project of the same name already exists, SeaUI
asks for confirmation before replacing it. The file is copied as-is; it is
validated when the project list is reloaded.

#### Export Yaml

Asks which project to export, then asks for a destination path, and copies the
project's YAML file there.

### 3.2 Edit menu

#### Edit Project

Renames a project. After choosing the project and entering the new name (which
is normalised the same way as on creation), SeaUI asks for confirmation, then
updates the `name:` key inside the YAML and renames the `<ProjectName>.yaml`
file to match. Renaming is refused if a project of the new name already
exists. Values derived from the project name at creation time
(`database_name`, `issuer`, log paths) are intentionally left unchanged so the
link with the existing database is not broken.

#### Edit Service

Changes the port of a service. After choosing the project and the service, a
dialog pre-filled with the current port asks for the new value (1–65535). The
`port:` key of that service is updated in the YAML, leaving the rest of the
file and its comments intact.

#### Edit Entity

Changes the options of an entity. After choosing the project, the service and
the entity, a dialog pre-filled with the entity's current options lets you
toggle `enable_crud`, `timestamps` and `soft_delete`. The `options:` section
of that entity is updated (created if absent). SeaUI then offers to apply the
change to the database by restarting the service.

#### Edit Yaml

Opens the YAML file of a chosen project in an integrated editor — a plain-text
editor in a monospace font, with Save and Cancel. Saving rewrites the file and
reloads the project. This is the catch-all editor for any change the
specialised Edit dialogs do not cover.

#### Preferences

A submenu. It currently contains the **Languages** submenu used to switch the
interface language (see section 8).

### 3.3 Audits menu

#### Show All Services Logs

Opens a tabbed window with one tab per service of every project. Each tab
shows the contents of that service's log file, scrolled to the most recent
events. A service whose log file does not exist yet (it was never started)
shows an explanatory message instead, and its tab is marked accordingly.

#### Choose a service to show Logs

Shows a list of every service (project / service / port) and opens the log
window for the single service you select.

### 3.4 Services Actions menu

These four entries act on every service of every project at once:

- **Start All Services** — starts every service that is not already running.
- **Stop All Services** — stops every running service.
- **Restart All Services** — stops then starts every service.
- **Reload All Services** — stops then starts every service. Because the
  backend re-reads the YAML on each start, a reload picks up any change made
  to the configuration files.

Each action ends with a confirmation message.

---

## 4. Service lifecycle

### 4.1 Starting and stopping a service

Each service runs as a separate backend process launched by SeaUI through
`QProcess`. A service is identified internally by a process key built from the
project name, the service name and the port, so two services never collide.

Starting a service launches the backend executable with the service's YAML
file and service name as arguments, and redirects its standard output and
error to the service's log file (in append mode). Starting a service that is
already running has no effect.

Stopping a service terminates the process gracefully, and forcibly kills it if
it does not exit within a short delay.

### 4.2 Per-service controls vs. bulk actions

The Start/Stop/Restart buttons in the service panel act on the currently
selected service. The Services Actions menu performs the same operations but
on every service of every project at once. Both share the same underlying
process-management logic.

### 4.3 Reload

Reload is currently equivalent to a restart: the process is stopped and
started again, and because the backend re-reads the YAML file on start, the
new configuration is taken into account. A true hot reload — signalling the
running backend to re-read its configuration without restarting — would
require backend support that does not exist yet.

---

## 5. Authentication

When a service is running you can authenticate against it. The Login button
opens a dialog asking for an email and a password. SeaUI sends them to the
service's `auth/login` endpoint. On success the returned JWT access token (and
refresh token, if any) is kept in memory for the session, and the Service Auth
Status label switches to `Connected`.

The Logout button clears the stored token and the status returns to
`Disconnected`. The Login and Logout buttons enable and disable themselves
according to the current authentication state.

The token is held only in memory for the running SeaUI session; it is not
persisted to disk.

---

## 6. Browsing data and Swagger

### 6.1 Open Data

With a service and an entity selected, the Open Data button fetches the rows
of that entity from the running service and shows them in a table window. The
response is expected to be a JSON array; each array element becomes a row. If
the service is unreachable or the response is not a JSON array, an explanatory
message is shown.

### 6.2 Swagger

The Swagger button opens the service's Swagger documentation (`/docs`) inside
an embedded browser window. It is only available while the service is running.

---

## 7. Logs

There are two ways to read logs:

- The **Logs button** in the service panel opens the selected service's log
  file in the system's default text application.
- The **Audits menu** opens logs inside SeaUI, in a tabbed read-only viewer —
  either for all services at once or for a single chosen service.

Logs are shown as a snapshot taken when the window is opened; they are not
refreshed live.

---

## 8. Internationalisation

SeaUI can display its interface in English or French, and the language can be
changed at runtime without restarting.

### 8.1 Changing the language

Open **Edit ▸ Preferences ▸ Languages** and pick **English** or **Francais**.
The interface is retranslated immediately. The two entries behave as a set of
mutually exclusive checkable items, so the active language is always shown
with a check mark. The chosen language is persisted and restored the next time
SeaUI starts.

### 8.2 How it works

Internationalisation is handled by the `TranslationManager` class. It owns the
Qt translators, exposes the list of available languages, applies a language,
and persists the choice through `QSettings`.

English is the source language: the strings written in the code are already
English, so no translation file is loaded for English — the French translator
is simply removed. French is provided by a compiled `.qm` file embedded in the
application resources under `:/i18n/`.

When the language changes, Qt sends a `LanguageChange` event to the main
window. The window then calls `retranslateUi()` to refresh every widget
defined in the `.ui` file, and re-applies the few texts that are set
dynamically in code (the window title, the authentication status label).

### 8.3 Adding a new language

1. Add the language to the list declared in the `TranslationManager`
   constructor (its locale code and display name).
2. Create the corresponding `SeaUI_<locale>.ts` file and add it to the
   `qt_add_translations` call in `CMakeLists.txt`.
3. Translate the strings (with Qt Linguist or by editing the `.ts` directly).
4. Rebuild — `qt_add_translations` compiles the `.ts` into a `.qm` and embeds
   it under `:/i18n/`.

### 8.4 Adding a new translatable string

Every user-visible string in the code must be wrapped in `tr("...")`, written
in English. Strings placed in the `.ui` file are translatable automatically.
After adding strings, the `.ts` files must be updated (`lupdate`, run
automatically by `qt_add_translations`) and the new entries translated.

---

## 9. Technical reference

### 9.1 File structure

The SeaUI application consists of:

- `main.cpp` — the entry point. It sets the application identity (needed by
  `QSettings`), creates the `TranslationManager`, loads the persisted language
  before building the window, and shows the main window.
- `mainwindow.{h,cpp,ui}` — the main window: panels, detail labels, action
  buttons, and every menu slot.
- `translation_manager.{h,cpp}` — the internationalisation manager.
- `projectlistmodel`, `servicelistmodel`, `entitylistmodel`,
  `fieldlistmodel` — list models backing the four main panels.
- `routelistmodel` + `routelistitemdelegate` — the route list model and its
  custom painting delegate (coloured HTTP method, pagination badges).
- `servicestatuscheck` — background HTTP poller that reports whether a service
  is running.

### 9.2 The list models

Each panel is backed by a `QAbstractListModel` subclass. `ProjectListModel`
holds the loaded projects; selecting a project feeds `ServiceListModel`;
selecting a service feeds `EntityListModel` and computes the routes;
selecting an entity feeds `FieldListModel` and filters the routes. The route
model exposes custom roles (pagination mode, HTTP method, path,
entity/operation) consumed by `RouteListItemDelegate` for rendering.

### 9.3 Editing YAML by text

The create/edit menu actions modify YAML files by **textual manipulation**
rather than by re-emitting the file through a YAML library. This is a
deliberate choice: re-emitting would discard the comments and the formatting
of the generated files. The trade-off is that the textual approach must locate
keys carefully.

The helpers follow a consistent pattern: locate the start of the relevant
block (a service by its `  - name:` line, the `project:` root section, an
entity by its `      - name:` line), determine where that block ends, then
search for the target key **at the exact indentation depth** so that, for
example, a service's `port:` (four spaces) is never confused with the database
`port:` (six spaces). Insertions add new content at the end of the relevant
block; edits replace a value in place or, when the key is absent, insert it.

Because the textual approach relies on the structure produced by SeaUI's own
generators, it is reliable for SeaUI-generated files. A heavily hand-reordered
YAML could in theory confuse the block detection; in that case the integrated
Edit Yaml editor remains the safe fallback.

### 9.4 Generated production configuration

`buildProductionServiceBlock()` generates the YAML block of a service in
production configuration; it is shared by Add New Project and Add New Service.
`buildProductionYaml()` wraps it with the `project:` header. `buildEntityBlock()`
generates the YAML block of an entity. All keys produced by these generators
are validated against the backend YAML parser.

### 9.5 Refreshing the project list

A `QFileSystemWatcher` watches the configuration folder and reloads the
project list when files are added or removed there. Because the watcher's
`directoryChanged` signal does not fire when an existing file is *modified*,
the menu actions that modify an existing file (Add New Service, Add New
Entity, Edit Service, Edit Project, Edit Entity, Edit Yaml) call the reload
explicitly after writing. Actions that create a new file (Add New Project,
Import Yaml) also reload explicitly, so the UI never depends on watcher timing.
