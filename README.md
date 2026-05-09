# SeaDesktop
[![License: AGPL v3](https://img.shields.io/badge/License-AGPL_v3-blue.svg)](https://www.gnu.org/licenses/agpl-3.0)
[![Commercial License Available](https://img.shields.io/badge/Commercial-Available-green.svg)](COMMERCIAL-LICENSE.md)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Seastar](https://img.shields.io/badge/Seastar-shared--nothing-orange.svg)](https://seastar.io/)

> **Une plateforme low-code C++ qui transforme un fichier YAML en serveur ultra rapide API REST + GUI desktop, avec sécurité intégrée.**

---

## 🎯 Vision

SeaDesktop génère **automatiquement** depuis un seul fichier YAML :

- 🛢️ Une **BASE DE DONNEES** en fonction des entites et de leur relation dans le Yaml
- 🌐 Une **API REST CRUD** complète, performante (Seastar shared-nothing SMP)
- 🖥️ Une **interface graphique** desktop multi-plateforme (Qt 6)
- 🔐 Un système d'**authentification JWT** + **autorisation RBAC + ABAC** complet
- 📊 Des **routes relationnelles** auto-générées (HasMany, BelongsTo, M2M)

**Pas de boilerplate. Pas de framework lourd. Juste du C++ moderne et un YAML.**

---

## 🚀 Quick Start

### 1. Définir vos entités dans un YAML

```yaml
project:
  name: SeaDesktopDemo

services:
  - name: CCNBService
    port: 8080

    security:
      authentication:
        type: none   # pas d'auth (mode dev rapide)

      # Pas d'authorization → toutes les routes ouvertes
      # Pas de rate_limit, pas de CORS, pas de headers

    database:
      type: memory

    entities:
      - name: Department
        options:
          enable_crud: true
          public_routes: true
        fields:
          - name: id
            type: uuid
            required: true
            unique: true

          - name: name
            type: string
            required: true
            unique: true

        relations:
          - name: employees
            kind: has_many
            target_entity: Employee
            fk_column: department_id
            on_delete: cascade

      - name: Employee
        options:
          enable_crud: true
          public_routes: true
        fields:
          - name: id
            type: uuid
            required: true
            unique: true

          - name: name
            type: string
            required: true

          - name: email
            type: email
            required: true
            unique: true

          - name: age
            type: int
            required: false

          - name: department_id
            type: uuid
            required: false

        relations:
          - name: department
            kind: belongs_to
            target_entity: Department
            fk_column: department_id
            on_delete: restrict

      - name: Student
        options:
          enable_crud: true
          public_routes: true
        fields:
          - name: id
            type: uuid
            required: true
            unique: true

          - name: name
            type: string
            required: true

          - name: email
            type: email
            required: true
            unique: true

          - name: age
            type: int
            required: false

      - name: Program
        options:
          enable_crud: true
          public_routes: true
        fields:
          - name: id
            type: uuid
            required: true
            unique: true

          - name: name
            type: string
            required: true
            unique: true
```

### 2. Lancer le serveur

```bash
./Backend_Seastar
```

### 3. C'est prêt 🎉

Routes automatiquement générées :
```
GET /users                                                  List + filtre ABAC
POST /users                                                 Create + check ABAC
GET /departments                                            List + filtre ABAC
POST /departments                                           Create + check ABAC
GET /employees                                              List + filtre ABAC
POST /employees                                             Create + check ABAC
GET /students                                               
POST /students
GET /programs
POST /programs
GET /studentPrograms
POST /studentPrograms
GET /departments/{id}/employees
GET /departments_with_employees/{id}                        Parent + children
GET /employees/filter/with_department_name?name=<value>     Recherche par parent.name
GET /students/{id}/programs
GET /programs/{id}/students
GET /users/{id}
PUT /users/{id}
DELETE /users/{id}
GET /departments/{id}
PUT /departments/{id}
DELETE /departments/{id}
GET /employees/{id}                                          Detail + check ABAC
PUT /employees/{id}                                          Update + check ABAC
DELETE /employees/{id}                                       Delete + check ABAC
GET /students/{id}
PUT /students/{id}
DELETE /students/{id}
GET /programs/{id}
PUT /programs/{id}
DELETE /programs/{id}
GET /studentPrograms/{id}
PUT /studentPrograms/{id}
DELETE /studentPrograms/{id}

GET    /openapi.json                                       Documentation auto
```

---

## 🔐 Sécurité enterprise-grade

SeaDesktop implémente **6 modules de sécurité** chaînés :

```
┌──────────────────────────────────────────────────────────────────┐
│  Module 1 : Domain (PolicySubject, PolicyResource, PolicyContext) │
├──────────────────────────────────────────────────────────────────┤
│  Module 2 : PolicyEngine (évaluation des règles)                  │
├──────────────────────────────────────────────────────────────────┤
│  Module 3 : YAML Parser (parsing des access_control)              │
├──────────────────────────────────────────────────────────────────┤
│  Module 4 : JWT avec claims custom (department_id, role, etc.)    │
├──────────────────────────────────────────────────────────────────┤
│  Module 5 : AuthorizationMiddleware (Stratégie C double check)    │
├──────────────────────────────────────────────────────────────────┤
│  Module 6 : ResourceAuthorizationHelper (ABAC resource-aware)     │
└──────────────────────────────────────────────────────────────────┘
```

### Capacités

✅ **Authentification JWT** (access + refresh tokens)
✅ **RBAC** (rôles + permissions)
✅ **ABAC** :
  - `own_resource` : un user peut voir/modifier ses propres données
  - `same_scope` : un manager voit uniquement son département
  - `allow_roles` : restriction par rôles
✅ **Filtre silencieux** sur listings (records refusés exclus du résultat)
✅ **403 cross-scope** sur GetById/Update/Delete
✅ **Admin bypass** automatique
✅ **Logs détaillés** `[AUTHZ]` et `[AUTHZ-RES]` pour audit

### Exemple concret

Avec ce YAML, un **manager du département IT** :

| Action | Résultat |
|---|---|
| `GET /employees` | ✅ 200, voit uniquement employees IT (filtre silencieux) |
| `GET /employees/{Bob_IT}` | ✅ 200, accès accordé |
| `GET /employees/{David_HR}` | ❌ 403, cross-département refusé |
| `PUT /employees/{David_HR}` | ❌ 403, **avant** UPDATE SQL |
| `POST /employees {dept: HR}` | ❌ 403, création cross-dept refusée |
| `DELETE /employees/{Bob_IT}` | ❌ 403, seul admin peut delete |

---

## 🏗️ Architecture

SeaDesktop suit le **Domain-Driven Design** avec une séparation stricte des couches :

```
SeaDesktop/
│
├── apps/                                # Applications
│   ├── Backend_Seastar/                 # Serveur HTTP Seastar
│   │   └── src/
│   │       ├── http/
│   │       │   ├── handlers/            # Handlers CRUD + relations
│   │       │   ├── middlewares/         # Auth, CORS, RateLimit, etc.
│   │       │   ├── routing/             # Enregistrement des routes
│   │       │   └── utils/               # Helpers HTTP
│   │       └── main.cpp                 # Bootstrap
│   │
│   └── SeaUI/                           # GUI Qt6
│       └── src/                         # Interface administrative
│
├── libs/                                # Bibliothèques DDD
│   ├── sea_domain/                      # Couche Domain (entités, règles métier)
│   │   ├── access_control/              # PolicySubject, PolicyResource, etc.
│   │   ├── schema/                      # Entity, Field, Relation
│   │   └── ...
│   │
│   ├── sea_application/                 # Couche Application (use cases)
│   │   ├── access_control/              # PolicyEngine, evaluators
│   │   ├── auth/                        # AuthService, JWT
│   │   └── ...
│   │
│   └── sea_infrastructure/              # Couche Infrastructure (DB, YAML)
│       ├── yaml/                        # Parser YAML
│       ├── runtime/                     # CRUD engines
│       └── persistence/                 # MySQL, Memory backends
│
└── SeaDesktopDemo1.yaml                 # Exemple de configuration
```

---

## 🔧 Stack technique

| Couche | Technologie |
|---|---|
| Langage | **C++20** |
| HTTP serveur | **Seastar** (shared-nothing SMP, futures/continuations) |
| GUI desktop | **Qt 6.8.3** |
| Build | **CMake** (monorepo) |
| Base de données | **MySQL 8** (via Connector/C++) |
| Auth | **JWT HS256** (access + refresh tokens) |
| Hashing passwords | **bcrypt** |
| JSON | **nlohmann/json** |
| YAML | **yaml-cpp** |
| OS supportés | Linux (testé Ubuntu 24.04) |

---

## 🎨 Pourquoi Seastar ?

Seastar est un framework C++ pour serveurs **haute performance** :
- **Shared-nothing SMP** : 1 thread par core, pas de mutex
- **Futures/continuations** : I/O asynchrone non-bloquant
- **DPDK** : networking userspace (optionnel)
- **Linux-specific primitives** : aio, epoll, etc.

Utilisé par **ScyllaDB**, **Redpanda**, et d'autres systèmes de niveau industriel.

---

## 📦 Installation (développement)

### Prérequis

```bash
# Ubuntu 24.04
sudo apt install build-essential cmake git \
                 libmysqlcppconn-dev \
                 nlohmann-json3-dev \
                 libyaml-cpp-dev \
                 libssl-dev

# Qt 6.8.3 via Qt Online Installer
# Seastar depuis source : /opt/seastar
```

### Build

```bash
git clone github.com/fredf21/SeaDesktop.git
cd SeaDesktop
mkdir build && cd build
cmake -DCMAKE_PREFIX_PATH=/path/to/Qt6.8.3 ..
cmake --build . --target Backend_Seastar SeaUI -j$(nproc)
```

### Lancement

```bash
# Variable d'environnement requise
export SEA_DESKTOP_JWT_SECRET="votre-secret-jwt-ici"

# Lancer le serveur
./apps/Backend_Seastar/Backend_Seastar

# Lancer la GUI (séparément)
./apps/SeaUI/SeaUI
```

---

## 🗺️ Roadmap

### ✅ v0.5.0 (Actuel)
- Modules 1-6 complets (Domain → ABAC)
- API CRUD auto-générée
- Routes relationnelles (HasMany, BelongsTo, M2M)
- Filtre ABAC silencieux
- Tests end-to-end validés

### 🚧 v0.6.0 (En cours)
- Plugin system (`dlopen`/`dlsym` avec C ABI)
- Custom validators
- Custom routes utilisateurs

### 📋 v0.7.0 (Planifié)
- Migrations automatiques (schema versioning)
- WebSocket pour notifications real-time
- OAuth2 providers (Google, GitHub)

### 🌟 v1.0.0 (Vision)
- Production-ready
- Documentation complète
- Marketplace de plugins
- Hosting Cloud (Oracle Cloud Always Free)

---

## 🎓 Pour qui ?

- **Startups** qui veulent un backend + GUI en quelques heures
- **Équipes internes** qui ont besoin de tooling admin sécurisé
- **Développeurs C++** qui veulent un boilerplate moderne

---

## 📚 Documentation

- [Release Notes](./RELEASE_NOTES.md) - Changelog détaillé
- [docs/ABAC.md](./docs/ABAC.md) - Guide complet du système d'autorisation
- [docs/YAML_REFERENCE.md](./docs/YAML_REFERENCE.md) - Référence YAML *(à venir)*
- [docs/ARCHITECTURE.md](./docs/ARCHITECTURE.md) - Architecture détaillée *(à venir)*

---

## 🤝 Contribuer

Le projet est en **alpha**. Les contributions sont bienvenues, en particulier sur :
- Tests unitaires
- Support PostgreSQL
- Documentation
- Examples / templates YAML

---

## 📜 Licence

À définir.

---

## 👤 Auteur

**Frédéric** - Architecte & développeur C++

> Si vous êtes recruteur, investisseur, ou collaborateur potentiel,
> n'hésitez pas à me contacter sur LinkedIn.

---

<p align="center">
  <strong>SeaDesktop</strong> — Du YAML à un produit complet, en quelques minutes.
</p>
