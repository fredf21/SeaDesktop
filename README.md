# SeaDesktop

SeaDesktop est une plateforme de génération de backend haute performance en C++ basée sur Seastar, permettant de transformer une configuration simple en un backend complet, prêt à l’emploi.

---

## Concept

SeaDesktop permet de :

- Définir un backend via une configuration structurée
- Générer automatiquement :
  - API REST complète
  - CRUD dynamique
  - Authentification (JWT, OAuth2 à venir)
  - Documentation Swagger
  - Interface admin
- Exécuter directement le backend sans écrire de code

Objectif : réduire drastiquement le temps de développement backend

---

## Philosophie

SeaDesktop repose sur deux modes :

### Runtime Engine (no-code)

- Exécution directe via un moteur générique
- Couvre la majorité des cas d’usage
- Haute performance grâce à Seastar (asynchrone, non bloquant)

### Code Generation (low-code)

- Génération complète d’un projet C++
- Personnalisation avancée possible
- Architecture propre et extensible

---

## Architecture

- Core Engine : moteur CRUD générique
- Schema Parser : parsing de configuration
- Runtime Registry : gestion dynamique des entités
- Generic Repository : abstraction de la base de données
- Auth Layer : JWT (extensible vers RBAC, MFA)
- API Layer : REST + OpenAPI
- Interface admin auto-générée

---

## Pourquoi Seastar

:contentReference[oaicite:1]{index=1} permet :

- Très hautes performances
- Architecture non bloquante
- Optimisation CPU et cache
- Gestion avancée des futures

Adapté aux systèmes backend à forte charge

---

## Fonctionnalités

- CRUD dynamique sans code
- Support des relations :
  - has_one
  - has_many
  - many_to_many (avec pivot composite)
- Authentification JWT
- Documentation Swagger
- Logging
- Code generation

À venir :
- OAuth2 / MFA
- RBAC dynamique
- Support multi-base de données

---

## Exemple

```yaml
project:
  name: <Your project name>

services:
  - name: <Your service Name>
    port: 8081

    database:
      type: memory

    entities:
      - name: User
        options:
          enable_crud: true
          enable_auth: true
        fields:
          - name: id
            type: uuid
            required: true
            unique: true

          - name: email
            type: string
            semantic: email
            required: true
            unique: true

          - name: password
            type: string
            semantic: password
            required: true
            serializable: false

          - name: full_name
            type: string
            required: true

          - name: role
            type: string
            required: false

      - name: Department
        options:
          enable_auth: true
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
          enable_auth: true
        fields:
          - name: id
            type: uuid
            required: true
            unique: true

          - name: name
            type: string
            required: true

          - name: email
            type: string
            semantic: email
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
          enable_auth: true
        fields:
          - name: id
            type: uuid
            required: true
            unique: true

          - name: name
            type: string
            required: true

          - name: email
            type: string
            semantic: email
            required: true
            unique: true

          - name: age
            type: int
            required: false

        relations:
          - name: programs
            kind: many_to_many
            target_entity: Program
            pivot_table: StudentProgram
            source_fk_column: student_id
            target_fk_column: program_id
            on_delete: cascade

      - name: Program
        options:
          enable_auth: true
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
          - name: students
            kind: many_to_many
            target_entity: Student
            pivot_table: StudentProgram
            source_fk_column: program_id
            target_fk_column: student_id
            on_delete: cascade

      - name: StudentProgram
        options:
          enable_auth: true
        fields:
          - name: id
            type: uuid
            required: true
            unique: true

          - name: student_id
            type: uuid
            required: true

          - name: program_id
            type: uuid
            required: true
