# SeaUI — User Guide

SeaUI is the desktop application for managing your SeaDesktop projects. It
lets you create and organize projects, services and entities, run them, and
inspect them — all through a graphical interface, without editing
configuration files by hand or using a terminal.

This guide explains how to use SeaUI.

---

## 1. Getting started

### 1.1 Two ways to use SeaUI

SeaUI can talk to your projects in two different ways:

- **Local mode** — SeaUI reads and writes the YAML project files directly
  on your computer's disk, in a configuration folder you choose at first
  launch. It also starts and stops the services on your machine as
  background processes. This is the classic mode and is best for
  development work on a single machine.

- **Remote mode** — SeaUI talks to a SeaDesktop backend running on another
  machine (typically in a Docker container) over HTTP. It reads, modifies
  and creates YAML files through admin endpoints, and triggers restarts on
  the remote service. This mode is best for administering a deployed
  SeaDesktop instance from your desk.

You choose the mode at startup through a profile. SeaUI ships with one
built-in profile called **Local** that is always available and never needs
configuration. You can create additional **Remote** profiles, one per
remote service you want to administer.

The two modes share the same user interface; only a few actions behave
differently or are disabled in Remote mode. The differences are pointed
out throughout this guide.

### 1.2 What you can do with SeaUI

With SeaUI you can:

- browse all your projects and explore their services, entities, fields and
  routes;
- create new projects, services and entities with guided dialogs;
- rename projects, change service ports, and adjust entity options;
- import and export project files;
- start, stop, restart and reload your services (start/stop in Local mode
  only);
- sign in to a running service and view its data;
- open the Swagger documentation of a running service;
- read the logs your services produce (locally or remotely);
- switch between Local and Remote profiles at any time;
- switch the interface language between English and French.

### 1.3 The main window

When SeaUI opens, you see a series of panels arranged from the most general
to the most detailed:

- **Projects** — all your projects. Select one to see its services.
- **Services** — the services of the selected project. Select one to see its
  entities and details.
- **Entities** — the entities of the selected service. Select one to see its
  fields.
- **Fields** — the fields of the selected entity.
- **Routes** — the REST routes available. Paginated routes are marked with a
  coloured badge (PAGE, OFFSET, CURSOR).

Next to these, detail panels show the selected service's port, database type,
running status, and sign-in status. A row of buttons lets you act on the
selected service: Start, Stop, Restart, Swagger, Logs, Login, Logout and
Open Data.

The running status of a service refreshes on its own, so it always reflects
reality even if you started or stopped the service elsewhere.

---

## 2. Connecting to SeaDesktop

Every time SeaUI starts, it shows a **Connect to SeaDesktop** dialog
asking which profile to use.

### 2.1 Connecting in Local mode

Select the **Local (built-in)** profile in the dropdown and click
**Connect**.

#### First-time Local setup

The very first time you select Local, SeaUI opens a **Welcome to SeaUI**
dialog that walks you through three configuration sections:

**1. Configuration folder**

Choose where SeaUI will store your YAML project files. The default is
`~/.local/share/SeaDesktop/SeaUI/configs/`, but the Browse button lets you
pick any folder — for example a Git-versioned directory shared with your
team. A checkbox lets you copy an example project (`BlogDemo.yaml`) into
the folder so you have something to look at right away. Recommended on
first install.

**2. MySQL credentials**

Enter the host, port, user and password the backend will use to connect
to MySQL. The password field has a Show/Hide toggle to help you check
what you typed. If your MySQL root user has no password, leave the field
empty.

**3. JWT secret**

A secure secret is generated for you automatically. You can replace it
by clicking Regenerate if needed. This secret is used to sign
authentication tokens for projects that require sign-in.

When you click **Continue**, SeaUI saves your credentials in a secure
file next to your configuration folder (in a sibling `environment/`
folder, readable only by you) and opens the main window. From now on,
choosing **Local** at startup goes straight to the main window — the
Welcome dialog is shown only once.

#### Changing your settings later

If you need to change the configuration folder or update your MySQL
credentials later, edit the file `~/.config/SeaDesktop/SeaUI.conf`
(for the folder location) or the `seadesktop.env` file inside the
`environment/` folder (for credentials). A dedicated Preferences
dialog is planned for a future release.

### 2.2 Connecting in Remote mode

Select a Remote profile in the dropdown. The Authentication area becomes
active and shows:

- **Email** — your administrator user's email on the remote backend.
- **Password** — your administrator user's password.

The email is pre-filled with the last one you used for that profile.

Click **Connect**. SeaUI sends a login request to the remote backend and,
on success, opens the main window with the remote projects loaded. On
failure, an error dialog explains what went wrong (wrong credentials,
network unreachable, etc.) and you can try again or cancel.

> **Administrator role required.** To use a Remote profile, your account
> on the remote backend must have the administrator role. A regular user
> account can authenticate but cannot read the admin endpoints, so SeaUI
> will display "Admin role required" when trying to list the projects.

### 2.3 Cancelling

Clicking **Cancel** exits SeaUI without opening the main window.

### 2.4 Switching connection during a session

You can change profile at any time without restarting SeaUI: open
**File ▸ Switch Connection...**. The same Connect dialog appears. If you
choose a different profile and authenticate successfully, SeaUI:

- replaces the current connection with the new one;
- clears the currently selected project, service and entity;
- reloads the project list from the new connection.

If you cancel the Connect dialog, the current connection is kept.

---

## 3. Managing connection profiles

A profile saves the connection details for a Remote backend so you don't
have to retype the URL every time. Click **Manage Profiles...** in the
Connect dialog to open the profile manager.

### 3.1 The profile list

The manager lists all profiles, with **Local (built-in)** always at the
top. Selecting a profile shows its details (type, base URL, last user)
in the lower panel.

### 3.2 Add a Remote profile

Click **+ Add Remote** and fill in:

- **Name** — a label of your choice, for example "Production server" or
  "Staging". Must be unique among your profiles.
- **Base URL** — the URL where the remote backend is reachable, for
  example `https://api.example.com` or `http://192.168.1.50:8080`. No
  trailing slash.

Click OK. The new profile is added to the list.

### 3.3 Edit a Remote profile

Select a profile and click **Edit**. The same dialog appears, pre-filled
with the current values. Change what you need and click OK. The Local
profile cannot be edited (Edit is disabled when it is selected).

### 3.4 Remove a Remote profile

Select a profile and click **Remove**. SeaUI asks for confirmation
before deleting the profile. The Local profile cannot be removed
(Remove is disabled when it is selected).

### 3.5 Saving

Changes are saved when you click **Close**. They take effect the next
time you open the Connect dialog.

> **About the token.** The authentication token obtained when you log
> in to a Remote profile is **not saved** between sessions. You will
> need to re-enter your password each time you start SeaUI or switch
> to that profile. This protects your credentials in case your settings
> file is read by someone else.

---

## 4. Naming rules

When you name a project, a service or an entity, SeaUI tidies up what you type
so the name is always valid:

- only letters, digits and underscores are kept;
- spaces are turned into underscores;
- any other character (accents, punctuation, symbols) is removed.

For example, typing `My Blog!` gives `My_Blog`. Keep this in mind so you are
not surprised by the final name. To avoid surprises, prefer simple names made
of letters, digits and underscores from the start.

---

## 5. Working with projects

The actions in this section work in both Local and Remote mode. In Remote
mode, the changes are written to the remote backend's filesystem (via the
admin endpoints), not to your local disk.

### 5.1 Create a new project

Open **File ▸ Add New Project**. Enter a project name and a service name —
both are required. SeaUI creates the project with one ready-to-use service
configured for production: a database, security, and logging are all set up
for you.

A project of a name that already exists will not be overwritten.

> **Before you start the service:** the service is configured to read its
> security key and database credentials from a secure file that SeaUI
> created for you during the Welcome dialog at first launch. If you changed
> these credentials in your YAML manually, make sure they match what is in
> your `seadesktop.env` file, otherwise the service will not start.

### 5.2 Rename a project

Open **Edit ▸ Edit Project**, choose the project, and enter the new name.
SeaUI asks for confirmation, then renames the project. A project cannot be
renamed to a name that is already taken.

### 5.3 Import a project

Open **File ▸ Import Yaml** and pick a project file from anywhere on your
computer. It is copied into your projects and appears in the Projects panel.
If a project of the same name already exists, SeaUI asks whether to replace
it.

In Remote mode, the imported file is uploaded to the remote backend.

### 5.4 Export a project

Open **File ▸ Export Yaml**, choose the project to export, and pick where to
save it. A copy of the project file is saved at that location.

In Remote mode, SeaUI downloads the file from the remote backend before
saving it.

---

## 6. Working with services

### 6.1 Add a service to a project

Open **File ▸ Add New Service**, choose the project, and enter the service
name. The new service is created with the same ready-to-use production
configuration as a new project's service. A service name that is already used
in that project is refused.

### 6.2 Change a service's port

Open **Edit ▸ Edit Service**, choose the project and the service, and enter
the new port. The change is saved to the project.

### 6.3 Start, stop and restart services

The available actions depend on the mode.

**Local mode — one service:** select it in the Services panel and use the
Start, Stop or Restart buttons. These buttons enable and disable themselves
depending on whether the service is running.

**Local mode — all services:** the **Services Actions** menu acts on every
service of every project at once:

- **Start All Services** — starts every service that is not already running.
- **Stop All Services** — stops every running service.
- **Restart All Services** — stops and starts every service.
- **Reload All Services** — stops and starts every service so that any change
  made to the configuration is picked up.

**Remote mode:** the Start and Stop buttons are disabled, and the Services
Actions menu is disabled. The Restart button remains active and works
differently: instead of stopping and starting a local process, it sends a
restart request to the remote backend. The remote service terminates
gracefully and is automatically restarted by its container orchestrator
(typically Docker). A "Service is restarting, please wait..." dialog is
shown while SeaUI waits for the service to come back online.

> **Why are Start and Stop disabled in Remote mode?** The remote backend
> runs as a Docker container that is managed by the host system, not by
> SeaUI. Starting or stopping it requires direct access to Docker on the
> remote machine, which SeaUI does not have. Use SSH or your orchestrator
> for these actions.

---

## 7. Working with entities

### 7.1 Add an entity

Open **File ▸ Add New Entity**, then:

1. choose the project;
2. choose the service the entity belongs to;
3. enter the entity name;
4. choose the entity options — Enable CRUD, Timestamps and Soft delete
   (CRUD and Timestamps are on by default);
5. add the fields one by one. For each field, enter its name, pick its type,
   and choose whether it is Required, Unique or Indexed. After each field
   SeaUI asks if you want to add another. At least one field is required.

The available field types are: string, int, float, bool, timestamp, uuid,
bigint, smallint, decimal, json, binary, password, email, text and file.

Once the entity is added, SeaUI asks whether to apply the change to the
database now. Answering yes restarts the service so the new entity becomes
available. In Remote mode, the restart goes through the remote backend.

### 7.2 Change an entity's options

Open **Edit ▸ Edit Entity**, choose the project, the service and the entity.
A dialog shows the entity's current options — Enable CRUD, Timestamps and
Soft delete — and lets you change them. SeaUI then offers to apply the change
to the database by restarting the service.

---

## 8. Editing the project file directly

For any change the dialogs above do not cover, open **Edit ▸ Edit Yaml**.
Choose a project and its configuration opens in a built-in text editor. Make
your changes and click Save, or Cancel to discard them.

In Remote mode, the YAML is fetched from the remote backend when you open
the editor and uploaded back when you click Save.

---

## 9. Signing in and viewing data

### 9.1 Sign in to a service

When a service is running, select it and click **Login**. Enter your email
and password. If they are correct, the sign-in status changes to
**Connected**. Click **Logout** to sign out.

> **Note for Remote mode.** This Login is separate from the authentication
> you did to connect SeaUI to the backend. The Login button signs you in
> as a regular API user to view data; the Connect dialog signs you in as
> an administrator to manage projects. The two sessions are independent.

### 9.2 View an entity's data

With a service running and an entity selected, click **Open Data**. SeaUI
opens a data viewer that displays your records in a table.

The viewer is built for performance. Even when an entity holds tens of
thousands of rows, the table remains responsive while you scroll — only
the rows visible on screen are drawn at any moment. You can scroll
through the whole table without freezes, and column widths are adjusted
automatically to fit your data while keeping a reasonable maximum size.

A small banner at the top of the viewer tells you how many rows are
shown. If your entity uses pagination (configured in its YAML), the
viewer takes advantage of it: rows are fetched in batches as you scroll
near the bottom, so you do not download everything at once. The banner
tells you which pagination mode is active and how many rows have been
loaded so far. When the table has no pagination configured and contains
more than a thousand rows, an amber notice suggests enabling pagination
in the YAML for better performance.

When the viewer reaches the end of the collection, "End of collection"
is appended to the banner.

### 9.3 Open the Swagger documentation

With a service running, click **Swagger**. The service's interactive API
documentation opens in a window inside SeaUI.

---

## 10. Reading logs

The way you read logs depends on the mode.

### 10.1 Local mode

**A single service:** select it and click the **Logs** button. Its log file
opens in your computer's default text application.

**Inside SeaUI:** use the **Audits** menu.

- **Show All Services Logs** opens a window with one tab per service, each tab
  showing that service's log. A service that has never been started shows a
  short message instead.
- **Choose a service to show Logs** lets you pick one service and shows just
  its log.

Logs are shown as they were when you opened the window; they do not refresh on
their own.

### 10.2 Remote mode

**A single service:** click the **Logs** button. The Remote Logs Viewer
opens and shows the logs of the connected backend, retrieved from its
in-memory log buffer. Each entry is formatted as
`[timestamp] [level] [logger] message`. Click **Refresh** to fetch the
latest entries, or **Close** to dismiss.

**Inside SeaUI:** use the **Audits** menu.

- **Show All Services Logs** is disabled in Remote mode (a Remote profile
  is connected to a single backend, so there is nothing to combine).
- **Choose a service to show Logs** opens the Remote Logs Viewer directly
  for the connected backend.

The Remote Logs Viewer reads from the backend's in-memory ring buffer,
which keeps a configurable number of recent entries. Older entries that
have rolled out of the buffer are not retrievable through this viewer.

---

## 11. Changing the language

SeaUI is available in English and French. Open **Edit ▸ Preferences ▸
Languages** and choose **English** or **Francais**. The interface changes
immediately — no restart needed. Your choice is remembered the next time you
open SeaUI.

---

## 12. System routes and authentication

When you enable authentication on a service (by setting the
authentication type to `jwt` in the YAML), a few backend routes that
were previously open to anyone become administrator-only:

- the health and readiness endpoints
- the OpenAPI specification
- the Swagger documentation page and its assets

In day-to-day development with authentication disabled, these routes
stay open so you can browse Swagger and inspect the API freely. As
soon as you turn authentication on for production, they automatically
require an administrator account, hiding your API surface from
unauthenticated visitors.

This is worth knowing in two situations:

- **Sharing Swagger with non-administrators.** Once authentication is
  enabled, regular users will no longer be able to open the Swagger
  page. If your team needs to share the API documentation, export the
  OpenAPI spec from a development environment instead.

- **Load balancers and health checks.** If you put your backend behind
  a load balancer, the load balancer's health checks normally hit
  `/health` without any credentials. When authentication is enabled
  this will fail. Either configure the load balancer with an
  administrator token, or expose `/health` through a separate internal
  channel that bypasses authentication.

---

## 13. Known limitations in Remote mode

Remote mode is new in v1.0 and a few limitations are intentionally accepted
for this release:

- **One profile = one service.** Each Remote profile points to a single
  backend. If you administer multiple services running side by side (for
  example three containers on the same host), create a separate profile
  for each.

- **Start and Stop are disabled.** Only Restart is available remotely (via
  the backend's admin restart endpoint). Starting a stopped service or
  stopping a running one requires SSH access to the host machine.

- **The Remote Logs Viewer is read-only and not live.** No automatic
  refresh, no filtering by level or logger, no search. Use Refresh to
  fetch the latest entries. More advanced features are planned for v1.1.

- **The authentication token is not stored.** You must re-authenticate
  each time you open SeaUI or switch to a Remote profile. This is a
  security choice, not an oversight.

- **Creating a new project does not automatically start a new container.**
  In multi-service Docker deployments, deploying a new project requires
  updating the docker-compose configuration manually. Editing existing
  projects is fully supported.
