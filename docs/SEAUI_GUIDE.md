# SeaUI — User Guide

SeaUI is the desktop application for managing your SeaDesktop projects. It
lets you create and organize projects, services and entities, run them, and
inspect them — all through a graphical interface, without editing
configuration files by hand or using a terminal.

This guide explains how to use SeaUI.

---

## 1. Getting started

### 1.1 What you can do with SeaUI

With SeaUI you can:

- browse all your projects and explore their services, entities, fields and
  routes;
- create new projects, services and entities with guided dialogs;
- rename projects, change service ports, and adjust entity options;
- import and export project files;
- start, stop, restart and reload your services;
- sign in to a running service and view its data;
- open the Swagger documentation of a running service;
- read the logs your services produce;
- switch the interface language between English and French.

### 1.2 The main window

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

## 2. Naming rules

When you name a project, a service or an entity, SeaUI tidies up what you type
so the name is always valid:

- only letters, digits and underscores are kept;
- spaces are turned into underscores;
- any other character (accents, punctuation, symbols) is removed.

For example, typing `My Blog!` gives `My_Blog`. Keep this in mind so you are
not surprised by the final name. To avoid surprises, prefer simple names made
of letters, digits and underscores from the start.

---

## 3. Working with projects

### 3.1 Create a new project

Open **File ▸ Add New Project**. Enter a project name and a service name —
both are required. SeaUI creates the project with one ready-to-use service
configured for production: a database, security, and logging are all set up
for you.

A project of a name that already exists will not be overwritten.

> **Before you start the service:** the service is configured to read its
> security key from an environment variable named `SEA_DESKTOP_JWT_SECRET`.
> Make sure this variable is defined in your environment, otherwise the
> service will not start.

### 3.2 Rename a project

Open **Edit ▸ Edit Project**, choose the project, and enter the new name.
SeaUI asks for confirmation, then renames the project. A project cannot be
renamed to a name that is already taken.

### 3.3 Import a project

Open **File ▸ Import Yaml** and pick a project file from anywhere on your
computer. It is copied into your projects and appears in the Projects panel.
If a project of the same name already exists, SeaUI asks whether to replace
it.

### 3.4 Export a project

Open **File ▸ Export Yaml**, choose the project to export, and pick where to
save it. A copy of the project file is saved at that location.

---

## 4. Working with services

### 4.1 Add a service to a project

Open **File ▸ Add New Service**, choose the project, and enter the service
name. The new service is created with the same ready-to-use production
configuration as a new project's service. A service name that is already used
in that project is refused.

### 4.2 Change a service's port

Open **Edit ▸ Edit Service**, choose the project and the service, and enter
the new port. The change is saved to the project.

### 4.3 Start, stop and restart services

You can act on a single service or on all of them at once.

**One service:** select it in the Services panel and use the Start, Stop or
Restart buttons. These buttons enable and disable themselves depending on
whether the service is running.

**All services:** the **Services Actions** menu acts on every service of
every project at once:

- **Start All Services** — starts every service that is not already running.
- **Stop All Services** — stops every running service.
- **Restart All Services** — stops and starts every service.
- **Reload All Services** — stops and starts every service so that any change
  made to the configuration is picked up.

---

## 5. Working with entities

### 5.1 Add an entity

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
available.

### 5.2 Change an entity's options

Open **Edit ▸ Edit Entity**, choose the project, the service and the entity.
A dialog shows the entity's current options — Enable CRUD, Timestamps and
Soft delete — and lets you change them. SeaUI then offers to apply the change
to the database by restarting the service.

---

## 6. Editing the project file directly

For any change the dialogs above do not cover, open **Edit ▸ Edit Yaml**.
Choose a project and its configuration opens in a built-in text editor. Make
your changes and click Save, or Cancel to discard them.

---

## 7. Signing in and viewing data

### 7.1 Sign in to a service

When a service is running, select it and click **Login**. Enter your email
and password. If they are correct, the sign-in status changes to
**Connected**. Click **Logout** to sign out.

### 7.2 View an entity's data

With a service running and an entity selected, click **Open Data**. SeaUI
fetches the entity's records and shows them in a table.

### 7.3 Open the Swagger documentation

With a service running, click **Swagger**. The service's interactive API
documentation opens in a window inside SeaUI.

---

## 8. Reading logs

There are two ways to read your services' logs.

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

---

## 9. Changing the language

SeaUI is available in English and French. Open **Edit ▸ Preferences ▸
Languages** and choose **English** or **Francais**. The interface changes
immediately — no restart needed. Your choice is remembered the next time you
open SeaUI.
