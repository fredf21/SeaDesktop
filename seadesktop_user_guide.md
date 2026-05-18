# SeaDesktop — Guide utilisateur complet

Documentation de référence pour la rédaction d'un fichier de configuration YAML SeaDesktop. Ce guide décrit chaque section, chaque clé, ses valeurs autorisées, son comportement par défaut, ainsi que la réaction attendue du système face à chaque configuration. Toutes les informations présentées proviennent du code source du projet et ont été vérifiées par lecture directe des parseurs YAML.

---

## Sommaire

1. [Concepts fondamentaux](#1-concepts-fondamentaux)
2. [Structure d'un fichier YAML](#2-structure-dun-fichier-yaml)
3. [Section `database`](#3-section-database)
4. [Section `entities`](#4-section-entities)
5. [Les champs (`fields`)](#5-les-champs-fields)
6. [Types de champs](#6-types-de-champs)
7. [Attributs communs aux champs](#7-attributs-communs-aux-champs)
8. [Le champ `password`](#8-le-champ-password)
9. [Le champ `email`](#9-le-champ-email)
10. [Le champ `file`](#10-le-champ-file)
11. [Le champ `native`](#11-le-champ-native)
12. [Section `options` d'une entité](#12-section-options-dune-entité)
13. [Section `relations`](#13-section-relations)
14. [Section `pagination`](#14-section-pagination)
15. [Section `seeds` d'une entité](#15-section-seeds-dune-entité)
16. [Section `security.authentication`](#16-section-securityauthentication)
17. [Cookies et livraison des tokens](#17-cookies-et-livraison-des-tokens)
18. [Suivi des tokens (`token_tracking`)](#18-suivi-des-tokens-token_tracking)
19. [Section `security.authorization`](#19-section-securityauthorization)
20. [Règles d'accès (`access_control`) par entité](#20-règles-daccès-access_control-par-entité)
21. [Section `security.cors`](#21-section-securitycors)
22. [Section `security.rate_limits`](#22-section-securityrate_limits)
23. [Section `security.security_headers`](#23-section-securitysecurity_headers)
24. [Section `security.http_limits`](#24-section-securityhttp_limits)
25. [Section `storage`](#25-section-storage)
26. [Section `logging`](#26-section-logging)
27. [Endpoints système générés](#27-endpoints-système-générés)
28. [Codes de réponse HTTP](#28-codes-de-réponse-http)
29. [Documentation complémentaire](#29-documentation-complémentaire)

---

## 1. Concepts fondamentaux

### Qu'est-ce que SeaDesktop ?

SeaDesktop est une plateforme déclarative permettant de générer automatiquement une API REST à partir d'un fichier YAML. L'utilisateur ne rédige aucun code applicatif. Toute la configuration tient dans le fichier YAML, qui constitue la source unique de vérité de l'API exposée.

### Qu'est-ce qu'un projet ?

Un **projet** est l'unité de plus haut niveau dans le fichier YAML. Il porte un nom et contient une ou plusieurs déclarations de services. Un fichier YAML décrit un et un seul projet.

### Qu'est-ce qu'un service ?

Un **service** est une application autonome qui s'exécute indépendamment des autres services du projet. Chaque service écoute sur son propre port, dispose de sa propre base de données, de ses propres entités, et de sa propre configuration de sécurité.

Au démarrage, l'utilisateur précise quel service lancer parmi ceux déclarés dans le projet :

```
./backend --config=./config.yaml --service_name=NomDuService
```

### Qu'est-ce qu'une entité ?

Une **entité** représente une notion métier persistée en base de données. Chaque entité déclarée produit automatiquement :

- Une table en base de données
- Cinq routes REST CRUD : `GET /<entité>`, `GET /<entité>/{id}`, `POST /<entité>`, `PUT /<entité>/{id}`, `DELETE /<entité>/{id}`
- Une entrée dans la documentation OpenAPI accessible à `/docs`
- L'application des règles de validation et d'autorisation déclarées

### Qu'est-ce qu'un champ ?

Un **champ** (field) est un attribut d'une entité. Il correspond à une colonne dans la table associée. Chaque champ porte un nom, un type, et des attributs de contrainte (obligatoire, unique, indexé, valeur par défaut, etc.).

### Qu'est-ce qu'une relation ?

Une **relation** déclare un lien entre deux entités. Quatre types sont supportés : `belongs_to`, `has_many`, `has_one`, `many_to_many`. La déclaration d'une relation génère des routes REST supplémentaires permettant de naviguer entre les entités.

### Convention de nommage

| Élément | Convention | Exemple |
|---|---|---|
| Nom d'entité | PascalCase, singulier | `User`, `Product`, `OrderLine` |
| Nom de table | Calculé automatiquement : pluriel en minuscules | `User` → `users`, `Category` → `categories` |
| Nom de route | Identique au nom de table | `/users`, `/categories` |
| Nom de champ | snake_case | `email`, `created_at`, `user_id` |

---

## 2. Structure d'un fichier YAML

### Hiérarchie générale

```yaml
project:
  name: NomDuProjet

services:
  - name: PremierService
    port: 8081
    database: { ... }
    security: { ... }
    entities: [ ... ]
    logging: { ... }
    storage: { ... }

  - name: SecondService
    port: 8082
    # ... configuration distincte ...
```

### Bloc `project`

| Clé | Type | Obligatoire | Description |
|---|---|---|---|
| `name` | chaîne | **Oui** | Nom du projet. Utilisé dans les logs et la documentation OpenAPI. |

### Liste `services`

Liste de services à exposer. Chaque service est indépendant. Un seul service est lancé par exécution du backend (sélectionné par `--service_name`).

### Sections d'un service

| Section | Type | Obligatoire | Description |
|---|---|---|---|
| `name` | chaîne | **Oui** | Identifiant du service. |
| `port` | entier | **Oui** | Port HTTP d'écoute. |
| `database` | bloc | **Oui** | Configuration de la base. Voir [section 3](#3-section-database). |
| `entities` | liste | **Oui** | Définition des entités métier. Voir [section 4](#4-section-entities). |
| `security` | bloc | Non | Authentification, autorisation, CORS, rate limits, en-têtes. |
| `storage` | bloc | Non | Configuration du stockage de fichiers. Voir [section 25](#25-section-storage). |
| `logging` | bloc | Non | Configuration des logs. Voir [section 26](#26-section-logging). |

L'ordre des sections au sein d'un service n'a pas d'importance.

### Exemple minimal complet

Le fichier YAML le plus simple permettant d'exposer une API REST fonctionnelle :

```yaml
project:
  name: DemoMinimaliste

services:
  - name: ServicePrincipal
    port: 8081

    database:
      type: mysql
      host: localhost
      port: 3306
      database_name: demo_db
      username: root
      password: rootpassword

    entities:
      - name: Product
        fields:
          - name: id
            type: uuid
          - name: name
            type: string
            required: true
          - name: price
            type: float
```

Au démarrage, cette configuration produit :

- La création de la base `demo_db` si elle n'existe pas (selon `migrations.create_database_if_missing`)
- La création de la table `products` avec trois colonnes
- L'exposition des cinq routes CRUD standard
- La documentation OpenAPI sur `/docs`

---

## 3. Section `database`

Configure la connexion à la base de données ainsi que le comportement des migrations automatiques.

### Clés acceptées

```yaml
database:
  type: mysql
  host: localhost
  port: 3306
  database_name: mon_application
  username: root
  password: rootpassword
  migrations:
    enabled: true
    create_database_if_missing: true
    mode: modified
    dry_run: false
    seeds:
      enabled: true
      mode: once
      on_error: continue
```

| Clé | Type | Valeur par défaut | Description |
|---|---|---|---|
| `type` | enum | `memory` | Moteur de base. Valeurs : `mysql`, `postgres`, `mongo`, `memory`. Seul `mysql` est pleinement supporté. |
| `host` | chaîne | `localhost` | Adresse du serveur de base de données. |
| `port` | entier | `0` | Port d'écoute du serveur. Pour MySQL, valeur usuelle : `3306`. |
| `database_name` | chaîne | `""` | Nom de la base de données. |
| `username` | chaîne | `""` | Identifiant de connexion. |
| `password` | chaîne | `""` | Mot de passe de connexion. |
| `migrations` | bloc | voir ci-dessous | Configuration des migrations. |

### Bloc `migrations`

| Clé | Type | Valeur par défaut | Description |
|---|---|---|---|
| `enabled` | booléen | `false` | Active les migrations automatiques au démarrage. |
| `mode` | enum | `conservative` | Niveau de prudence. Valeurs : `conservative`, `modified`, `aggressive`. |
| `create_database_if_missing` | booléen | `true` | Crée la base si elle n'existe pas. |
| `dry_run` | booléen | `false` | Si `true`, affiche les SQL sans les exécuter. |
| `seeds` | bloc | voir [section 15](#15-section-seeds-dune-entité) | Configuration des seeds. |

### Modes de migration

Le mode contrôle quelles modifications du schéma le système accepte d'appliquer :

| Mode | Opérations autorisées | Opérations refusées |
|---|---|---|
| `conservative` | CREATE TABLE, ADD COLUMN, ADD INDEX | MODIFY COLUMN, DROP COLUMN, DROP INDEX, RENAME COLUMN |
| `modified` | Conservative + MODIFY COLUMN compatible, ADD UNIQUE, RENAME COLUMN | DROP COLUMN, DROP INDEX |
| `aggressive` | Toutes les opérations, y compris DROP COLUMN et DROP TABLE | aucune |

Le mode `aggressive` peut entraîner des pertes de données. Il est déconseillé en production.

### Comportement attendu au démarrage

1. Le système tente de se connecter à la base avec les paramètres fournis.
2. Si `create_database_if_missing: true` et que la base n'existe pas, le système la crée.
3. Le système introspecte le schéma actuel (tables, colonnes, index).
4. Le système compare avec le schéma déclaré dans le YAML.
5. Les modifications nécessaires sont identifiées et filtrées selon le `mode`.
6. Les modifications autorisées sont appliquées dans l'ordre.
7. Un rapport détaillé est journalisé.

Si une migration échoue, un avertissement est journalisé et le service tente de démarrer malgré tout.

---

## 4. Section `entities`

La section `entities` est une liste de définitions d'entités. Chaque entité produit une table en base et un ensemble de routes REST.

### Structure d'une entité

```yaml
entities:
  - name: User
    options:
      enable_crud: true
      timestamps: true
      is_auth_source: false
      soft_delete: false
      public_routes: false
    fields:
      - name: id
        type: uuid
      - name: email
        type: email
        required: true
        unique: true
    relations:
      - name: posts
        target_entity: Post
        kind: has_many
        fk_column: user_id
    pagination: { ... }
    access_control: { ... }
    seeds: [ ... ]
```

### Clés acceptées

| Clé | Type | Obligatoire | Description |
|---|---|---|---|
| `name` | chaîne | **Oui** | Nom de l'entité (PascalCase recommandé). |
| `fields` | liste | **Oui** | Liste des champs. Voir [section 5](#5-les-champs-fields). |
| `options` | bloc | Non | Options de l'entité. Voir [section 12](#12-section-options-dune-entité). |
| `relations` | liste | Non | Relations avec d'autres entités. Voir [section 13](#13-section-relations). |
| `pagination` | bloc | Non | Pagination. Voir [section 14](#14-section-pagination). |
| `access_control` | bloc | Non | Règles d'accès. Voir [section 20](#20-règles-daccès-access_control-par-entité). |
| `seeds` | liste | Non | Données initiales. Voir [section 15](#15-section-seeds-dune-entité). |

### Routes générées automatiquement

Pour chaque entité avec `enable_crud: true` (valeur par défaut) :

| Méthode | Route | Code succès | Description |
|---|---|---|---|
| `GET` | `/<entité>` | 200 | Liste les enregistrements |
| `GET` | `/<entité>/{id}` | 200 | Récupère un enregistrement par identifiant |
| `POST` | `/<entité>` | 201 | Crée un nouvel enregistrement |
| `PUT` | `/<entité>/{id}` | 200 | Met à jour un enregistrement |
| `DELETE` | `/<entité>/{id}` | 204 | Supprime un enregistrement |

Le segment `<entité>` est calculé automatiquement : nom en minuscules au pluriel anglais.

---

## 5. Les champs (`fields`)

Un champ représente une colonne en base et un attribut dans les réponses JSON.

### Anatomie d'un champ

```yaml
- name: email
  type: email
  required: true
  unique: true
  indexed: true
  max_length: 255
  default: ""
  serializable: true
  unsigned_value: false
  previous_name: ancien_email
```

### Liste exhaustive des clés acceptées

| Clé | Type | Valeur par défaut | Description |
|---|---|---|---|
| `name` | chaîne | — | Nom du champ. **Obligatoire.** |
| `type` | enum | — | Type de donnée. **Obligatoire.** Voir [section 6](#6-types-de-champs). |
| `required` | booléen | `true` | Si `true`, la colonne est `NOT NULL` et la validation rejette les valeurs nulles à l'insertion. |
| `unique` | booléen | `false` | Si `true`, ajoute une contrainte d'unicité en base. |
| `indexed` | booléen | `false` | Si `true`, crée un index en base sur cette colonne. |
| `serializable` | booléen | `true` | Si `false`, le champ est exclu des réponses JSON GET. |
| `unsigned_value` | booléen | `false` | Pour les types numériques, marque la colonne comme `UNSIGNED`. |
| `max_length` | entier | absent | Longueur maximale pour `string` ou `text`. |
| `min_value` | nombre | absent | Borne minimale pour les types numériques. Vérifiée à la validation. |
| `max_value` | nombre | absent | Borne maximale pour les types numériques. Vérifiée à la validation. |
| `default` | tout type | absent | Valeur par défaut appliquée si le champ est omis à l'insertion. |
| `previous_name` | chaîne | absent | Ancien nom de colonne pour détecter un renommage lors d'une migration. |
| `file` | bloc | absent | Configuration du fichier. **Obligatoire si `type: file`.** Voir [section 10](#10-le-champ-file). |
| `native` | bloc | absent | Type natif spécifique au SGBD. **Obligatoire si `type: native`.** Voir [section 11](#11-le-champ-native). |

### Cas particulier : `default` interdit pour certains types

Les types `binary` et `file` ne supportent pas l'attribut `default`. Le système rejette la configuration avec une erreur de parsing si un `default` est déclaré sur un champ de ces types.

### Cas particulier : nullabilité

Par défaut, `required: true`. Ce comportement diffère de SQL standard où les colonnes sont nullables par défaut. Pour rendre une colonne nullable, déclarer explicitement `required: false`.

```yaml
- name: deleted_at
  type: timestamp
  required: false
```

---

## 6. Types de champs

Liste exhaustive des types acceptés dans la clé `type` d'un champ.

### Types simples

| Type | Description | Stockage MySQL |
|---|---|---|
| `string` | Chaîne de caractères courte. Longueur configurable via `max_length`. | `VARCHAR(n)` |
| `text` | Chaîne de caractères longue. | `TEXT` |
| `int` | Entier signé. | `INT` |
| `bigint` | Entier signé sur 64 bits. | `BIGINT` |
| `smallint` | Entier signé sur 16 bits. | `SMALLINT` |
| `float` | Nombre à virgule flottante. | `FLOAT` |
| `decimal` | Nombre à précision fixe (adapté aux montants monétaires). | `DECIMAL` |
| `bool` | Valeur logique vrai/faux. | `TINYINT(1)` |
| `timestamp` | Horodatage (date + heure). | `TIMESTAMP` ou `DATETIME` |
| `uuid` | Identifiant universellement unique. Généré automatiquement à la création. | `BINARY(16)` |
| `json` | Document JSON natif. | `JSON` |
| `binary` | Données binaires. | `BLOB` ou `BINARY` |

### Types métier (validation supplémentaire)

| Type | Description | Comportement |
|---|---|---|
| `email` | Adresse de courriel. | Validation automatique du format à l'insertion. Voir [section 9](#9-le-champ-email). |
| `password` | Mot de passe. | Hashage automatique en bcrypt à l'insertion. Exclu par défaut des réponses JSON. Voir [section 8](#8-le-champ-password). |
| `file` | Référence vers un fichier. | Sous-bloc `file:` obligatoire. Le système gère le stockage du fichier séparément. Voir [section 10](#10-le-champ-file). |
| `native` | Type spécifique au SGBD. | Sous-bloc `native:` obligatoire. Permet d'utiliser un type natif non couvert par les types standard. Voir [section 11](#11-le-champ-native). |

### Exemple d'utilisation de chaque type

```yaml
fields:
  - name: id
    type: uuid

  - name: nom
    type: string
    max_length: 100
    required: true

  - name: description
    type: text
    required: false

  - name: age
    type: int
    min_value: 0
    max_value: 150

  - name: vues
    type: bigint
    unsigned_value: true
    default: 0

  - name: numero_secteur
    type: smallint

  - name: poids_kg
    type: float
    min_value: 0.0

  - name: prix
    type: decimal

  - name: actif
    type: bool
    default: true

  - name: cree_le
    type: timestamp

  - name: metadata
    type: json
    required: false

  - name: photo_binaire
    type: binary
    required: false

  - name: email
    type: email
    required: true
    unique: true

  - name: mot_de_passe
    type: password
    required: true

  - name: avatar
    type: file
    required: false
    file:
      max_size: 2MB
      allowed_mime_types: [image/png, image/jpeg]
      storage_path: users/avatars
      on_delete: cascade

  - name: coord_pg
    type: native
    native:
      dialect: PostgreSQL
      type: POINT
```

### Comportement par type

**Type `uuid`** : Le système génère automatiquement un UUID v4 à la création de l'enregistrement. L'utilisateur n'a pas à fournir de valeur lors d'un `POST`.

**Type `timestamp`** : Lorsque l'option `timestamps: true` est active sur l'entité (valeur par défaut), les champs `created_at` et `updated_at` sont gérés automatiquement par le système. Voir [section 12](#12-section-options-dune-entité).

**Type `password`** : Hashage bcrypt transparent à l'insertion et à la mise à jour. Le champ n'apparaît jamais dans les réponses JSON. Voir [section 8](#8-le-champ-password).

**Type `email`** : Validation du format conforme à RFC 5322. Une valeur invalide retourne un code 400. Voir [section 9](#9-le-champ-email).

**Type `file`** : Le contenu binaire est stocké dans le système de fichiers selon la configuration `storage`. La colonne en base contient un identifiant UUID pointant vers une table système `sea_files`. Voir [section 10](#10-le-champ-file).

---

## 7. Attributs communs aux champs

Cette section détaille la signification précise de chaque attribut applicable à un champ.

### `required` (booléen, défaut `true`)

Détermine si le champ est obligatoire à l'insertion.

| Valeur | Effet en base | Effet à la validation |
|---|---|---|
| `true` (défaut) | Colonne `NOT NULL` | Le champ doit être fourni dans le payload `POST`. Une valeur nulle ou absente retourne 400. |
| `false` | Colonne nullable | Le champ peut être omis ou explicitement nul. |

**Exemple :**

```yaml
- name: telephone
  type: string
  required: false   # téléphone optionnel
```

### `unique` (booléen, défaut `false`)

Ajoute une contrainte d'unicité en base.

| Valeur | Effet |
|---|---|
| `true` | Contrainte `UNIQUE` créée. Une insertion en double retourne 409. |
| `false` (défaut) | Aucune contrainte. Les doublons sont autorisés. |

**Exemple :**

```yaml
- name: email
  type: email
  unique: true     # deux utilisateurs ne peuvent avoir le même email
```

### `indexed` (booléen, défaut `false`)

Crée un index en base sur ce champ pour accélérer les recherches.

| Valeur | Effet |
|---|---|
| `true` | Index `INDEX` créé. Recommandé sur les champs fréquemment utilisés en filtre ou tri. |
| `false` (défaut) | Aucun index. |

**Exemple :**

```yaml
- name: status
  type: string
  indexed: true    # accélère GET /products?status=active
```

### `serializable` (booléen, défaut `true`)

Contrôle si le champ apparaît dans les réponses JSON.

| Valeur | Effet |
|---|---|
| `true` (défaut) | Le champ est inclus dans les réponses `GET`. |
| `false` | Le champ est stocké en base et acceptable en entrée, mais jamais retourné dans les réponses JSON. |

**Cas particulier** : pour le type `password`, la valeur par défaut est automatiquement `false` (le mot de passe hashé n'apparaît jamais dans les réponses).

**Exemple :**

```yaml
- name: token_interne
  type: string
  serializable: false   # stocké mais jamais exposé
```

### `unsigned_value` (booléen, défaut `false`)

Pour les types numériques, marque la colonne comme `UNSIGNED` en base (n'accepte que des valeurs positives).

```yaml
- name: nombre_de_vues
  type: bigint
  unsigned_value: true
```

### `max_length` (entier)

Pour les types `string` et `text`, fixe la longueur maximale.

```yaml
- name: code_produit
  type: string
  max_length: 50      # VARCHAR(50)
```

Une valeur d'insertion dépassant la longueur déclarée retourne 400.

### `min_value` et `max_value` (nombre)

Pour les types numériques, fixent les bornes acceptées à la validation. Les types acceptés sont `int`, `bigint`, `smallint`, `float`, `decimal`.

```yaml
- name: age
  type: int
  min_value: 0
  max_value: 150
```

Une valeur hors bornes retourne 400.

### `default` (tout type)

Valeur appliquée automatiquement à l'insertion si le champ est omis dans le payload.

```yaml
- name: status
  type: string
  default: "active"

- name: vues
  type: int
  default: 0

- name: actif
  type: bool
  default: true
```

**Restrictions** : les types `binary` et `file` ne supportent pas `default`. Le système rejette la configuration au démarrage si un `default` est déclaré sur ces types.

### `previous_name` (chaîne)

Permet de détecter un renommage de colonne lors d'une migration sans perdre les données.

```yaml
- name: nom_complet
  previous_name: nom    # ancienne colonne "nom" sera renommée
  type: string
```

Le système détecte le renommage si :

- Une colonne `nom` existe en base
- Aucune déclaration de champ `nom` n'est présente dans le YAML
- Une déclaration `nom_complet` mentionne `previous_name: nom`
- Les caractéristiques (type, longueur) sont compatibles

Le mode de migration doit être `modified` ou `aggressive` pour que le renommage soit effectivement appliqué.

---

## 8. Le champ `password`

Le type `password` est un type métier enrichi qui applique automatiquement un hashage bcrypt et exclut le champ des réponses JSON.

### Déclaration

```yaml
- name: mot_de_passe
  type: password
  required: true
```

### Comportement automatique

| Opération | Comportement |
|---|---|
| `POST /<entité>` avec un mot de passe | Le mot de passe en clair est hashé en bcrypt avant insertion en base. |
| `PUT /<entité>/{id}` avec un mot de passe | Le nouveau mot de passe est re-hashé. |
| `GET /<entité>` ou `GET /<entité>/{id}` | Le champ est exclu des réponses (équivaut à `serializable: false`). |

### Attribut `serializable` par défaut

Pour les champs `password`, l'attribut `serializable` vaut **`false` par défaut**, contrairement aux autres types où il vaut `true`. Cette protection est explicite et évite toute exposition accidentelle du hash en réponse.

Si l'utilisateur souhaite forcer la sérialisation (cas d'usage extrêmement rare), il doit le déclarer explicitement :

```yaml
- name: mot_de_passe
  type: password
  serializable: true    # déconseillé : expose le hash bcrypt
```

### Exemple complet

```yaml
- name: User
  options:
    is_auth_source: true
  fields:
    - name: id
      type: uuid
    - name: email
      type: email
      required: true
      unique: true
    - name: mot_de_passe
      type: password
      required: true
    - name: role
      type: string
      default: "user"
```

### Requête de création

```bash
curl -X POST http://localhost:8081/users \
  -H "Content-Type: application/json" \
  -d '{
    "email": "alice@example.com",
    "mot_de_passe": "MotDePasseEnClair",
    "role": "user"
  }'
```

### Réponse

```json
{
  "id": "550e8400-e29b-41d4-a716-446655440000",
  "email": "alice@example.com",
  "role": "user"
}
```

Le champ `mot_de_passe` n'apparaît pas dans la réponse.

---

## 9. Le champ `email`

Le type `email` valide automatiquement que la valeur fournie respecte le format d'une adresse de courriel.

### Déclaration

```yaml
- name: email
  type: email
  required: true
  unique: true
```

### Comportement

| Opération | Comportement |
|---|---|
| Insertion avec un email valide | Acceptée, l'enregistrement est créé. |
| Insertion avec une chaîne ne respectant pas le format email | Rejet avec code 400. |
| Stockage | Identique à `string` (`VARCHAR`). |
| Sérialisation | Inclus normalement dans les réponses JSON. |

### Exemple de validation

```bash
# Email valide : 201 Created
curl -X POST http://localhost:8081/users \
  -d '{"email": "alice@example.com"}'

# Email invalide : 400 Bad Request
curl -X POST http://localhost:8081/users \
  -d '{"email": "pas-un-email"}'
```

---

## 10. Le champ `file`

Le type `file` permet à un champ d'accepter le contenu d'un fichier uploadé. Le système gère le stockage physique séparément et stocke en base une référence vers le fichier.

### Déclaration minimale

```yaml
- name: avatar
  type: file
  file:
    storage_path: users/avatars
    on_delete: cascade
```

### Le sous-bloc `file` est obligatoire

Toute déclaration `type: file` doit comporter un sous-bloc `file:`. Une déclaration sans ce sous-bloc est rejetée au parsing avec une erreur.

Inversement, déclarer un sous-bloc `file:` sur un type autre que `file` est également rejeté.

### Clés du sous-bloc `file`

| Clé | Type | Valeur par défaut | Description |
|---|---|---|---|
| `storage_path` | chaîne | requis | Sous-répertoire (relatif à `storage.root_path`) où sont stockés les fichiers de ce champ. |
| `on_delete` | enum | `cascade` | Comportement lors de la suppression. Valeurs : `cascade`, `set_null`, `restrict`. |
| `max_size` | taille | absent (pas de limite) | Taille maximale acceptée. Formats : `500KB`, `5MB`, `1GB`. |
| `allowed_mime_types` | liste | vide (tous types) | Liste blanche des types MIME acceptés. |
| `allowed_extensions` | liste | vide (toutes extensions) | Liste blanche d'extensions (avec le point initial). Insensible à la casse. |

### Stratégies `on_delete`

| Stratégie | Effet à la suppression de l'entité parente | Effet au remplacement du fichier |
|---|---|---|
| `cascade` (défaut) | Le fichier est supprimé du disque s'il n'est plus référencé. | L'ancien fichier est supprimé s'il n'est plus référencé. |
| `set_null` | Le fichier est conservé sur disque. | L'ancien fichier est conservé sur disque. |
| `restrict` | La suppression est refusée avec code 409 tant qu'un fichier est attaché. | Le remplacement est autorisé. |

### Routes générées pour les champs `file`

Pour chaque champ de type `file` déclaré sur une entité, le système ajoute automatiquement une route de téléchargement :

```
GET /<entité au pluriel>/<nom du champ>/{id}
```

Exemples :

| Champ déclaré | Route de téléchargement |
|---|---|
| `User.avatar` | `GET /users/avatar/{id}` |
| `Article.banner` | `GET /articles/banner/{id}` |
| `Contract.pdf` | `GET /contracts/pdf/{id}` |

### Trois modalités d'upload

1. **Multipart/form-data** (recommandé pour les formulaires) :

```bash
curl -X POST http://localhost:8081/users \
  -F "email=alice@example.com" \
  -F "avatar=@./photo.png;type=image/png"
```

2. **JSON avec base64** (adapté aux clients ne pouvant émettre que du JSON) :

```bash
curl -X POST http://localhost:8081/users \
  -H "Content-Type: application/json" \
  -d '{
    "email": "alice@example.com",
    "avatar": {
      "filename": "photo.png",
      "mime_type": "image/png",
      "content_base64": "..."
    }
  }'
```

3. **Référence à un fichier existant par UUID** (partage entre entités) :

```bash
curl -X POST http://localhost:8081/users \
  -H "Content-Type: application/json" \
  -d '{
    "email": "bob@example.com",
    "avatar": "660f9511-f30c-52e5-b827-557766551111"
  }'
```

### Configuration globale du stockage

Le bloc `storage:` au niveau service détermine où sont placés les fichiers. Voir [section 25](#25-section-storage).

### Documentation détaillée

Le sujet complet (partage de fichiers, comptage de références, suppression en cascade) est traité dans le document `FILE_FEATURE_USER_GUIDE.md`. Voir [section 29](#29-documentation-complémentaire).

---

## 11. Le champ `native`

Le type `native` permet d'utiliser un type spécifique au SGBD qui n'est pas couvert par les types standard de SeaDesktop.

### Déclaration

```yaml
- name: coordonnees
  type: native
  native:
    dialect: PostgreSQL
    type: POINT
```

### Clés du sous-bloc `native`

| Clé | Type | Obligatoire | Description |
|---|---|---|---|
| `dialect` | enum | **Oui** | SGBD ciblé. Valeurs : `MySQL`, `PostgreSQL`, `SQLite`, `SQLServer`. |
| `type` | chaîne | **Oui** | Nom exact du type natif tel qu'utilisé par le SGBD. |

### Comportement

Le type natif est passé tel quel au SGBD lors de la création de la table. SeaDesktop n'effectue aucune validation supplémentaire sur les valeurs : la responsabilité de la cohérence revient à l'utilisateur.

### Restrictions

- Le sous-bloc `native:` est obligatoire si `type: native`. Sans lui, le parsing échoue.
- Le sous-bloc `native:` est interdit pour les autres types.
- Le type natif n'est compatible qu'avec des bases SQL. Il ne s'applique pas aux bases NoSQL.

### Exemples

```yaml
# PostgreSQL : type point géographique
- name: position
  type: native
  native:
    dialect: PostgreSQL
    type: POINT

# MySQL : type entier moyen non signé
- name: compteur
  type: native
  native:
    dialect: MySQL
    type: "MEDIUMINT UNSIGNED"

# SQL Server : type monétaire
- name: prix_devise
  type: native
  native:
    dialect: SQLServer
    type: MONEY
```

---

## 12. Section `options` d'une entité

Le bloc `options:` configure des comportements automatiques pour l'entité.

### Clés acceptées

```yaml
- name: User
  options:
    enable_crud: true
    is_auth_source: false
    enable_websocket: false
    soft_delete: false
    timestamps: true
    public_routes: false
```

| Clé | Type | Valeur par défaut | Description |
|---|---|---|---|
| `enable_crud` | booléen | `true` | Active la génération des routes CRUD standard. |
| `is_auth_source` | booléen | `false` | Désigne cette entité comme source d'authentification. Voir détails ci-dessous. |
| `enable_websocket` | booléen | `false` | Active une route WebSocket de notifications (non implémenté actuellement). |
| `soft_delete` | booléen | `false` | Ajoute un champ `deleted_at` et masque les enregistrements supprimés au lieu de les effacer. |
| `timestamps` | booléen | `true` | Ajoute automatiquement les champs `created_at` et `updated_at` gérés par le système. |
| `public_routes` | booléen | `false` | Si `true`, les routes de cette entité sont publiques (non protégées par le middleware d'authentification). |

### Comportement de `is_auth_source`

Une seule entité par service peut porter `is_auth_source: true`. Cette entité doit comporter :

- Un champ utilisé comme identifiant unique (typiquement `email`)
- Un champ de type `password` pour le mot de passe
- Un champ utilisé comme rôle (typiquement `role`)

Activer `is_auth_source` génère automatiquement les routes `/auth/register`, `/auth/login`, `/auth/refresh`, `/auth/logout`, `/auth/me`. Voir [section 16](#16-section-securityauthentication).

### Comportement de `timestamps`

Lorsque `timestamps: true` (valeur par défaut), le système ajoute automatiquement à l'entité deux champs :

- `created_at` : horodatage de création, défini à l'insertion et jamais modifié ensuite.
- `updated_at` : horodatage de dernière modification, mis à jour à chaque `PUT`.

Ces champs apparaissent dans les réponses JSON. L'utilisateur n'a pas à les déclarer manuellement dans la liste des `fields`.

Pour désactiver ce comportement :

```yaml
- name: Configuration
  options:
    timestamps: false
```

### Comportement de `soft_delete`

Lorsque `soft_delete: true`, le système ajoute un champ `deleted_at` à l'entité. Les requêtes `DELETE` ne suppriment pas les enregistrements en base : elles remplissent le champ `deleted_at` avec l'horodatage actuel. Les routes `GET` filtrent automatiquement les enregistrements ayant un `deleted_at` non nul.

### Comportement de `public_routes`

Par défaut, lorsque l'authentification est activée au niveau du service, toutes les routes CRUD générées sont protégées : un token JWT est requis. Avec `public_routes: true`, les routes de cette entité spécifique restent accessibles sans authentification.

---

## 13. Section `relations`

Une relation établit un lien entre deux entités et génère des routes REST supplémentaires permettant de naviguer entre elles.

### Quatre types de relations

| Kind | Description | Clé étrangère placée dans |
|---|---|---|
| `belongs_to` | L'entité courante référence une autre entité. | Cette entité (colonne locale). |
| `has_many` | L'entité courante est référencée par plusieurs autres. | L'entité cible. |
| `has_one` | L'entité courante est référencée par exactement une autre. | L'entité cible. |
| `many_to_many` | Relation à cardinalité multiple des deux côtés. | Table pivot dédiée. |

### Structure d'une relation

```yaml
relations:
  - name: author
    target_entity: User
    kind: belongs_to
    fk_column: author_id
    on_delete: cascade
```

### Clés acceptées

| Clé | Type | Obligatoire | Description |
|---|---|---|---|
| `name` | chaîne | **Oui** | Nom logique de la relation. Utilisé dans les routes générées. |
| `target_entity` | chaîne | **Oui** | Nom de l'entité référencée. |
| `kind` | enum | **Oui** | Type de relation : `belongs_to`, `has_many`, `has_one`, `many_to_many`. |
| `on_delete` | enum | `restrict` | Comportement lors de la suppression de l'entité référencée. Valeurs : `cascade`, `set_null`, `restrict`. |
| `fk_column` | chaîne | Conditionnel | Nom de la colonne clé étrangère. Obligatoire pour `belongs_to`, `has_many`, `has_one`. |
| `pivot_table` | chaîne | Conditionnel | Nom de la table pivot. Obligatoire pour `many_to_many`. |
| `source_fk_column` | chaîne | Conditionnel | Nom de la colonne référençant l'entité courante dans la table pivot. Obligatoire pour `many_to_many`. |
| `target_fk_column` | chaîne | Conditionnel | Nom de la colonne référençant l'entité cible dans la table pivot. Obligatoire pour `many_to_many`. |

### Exemple : relation `belongs_to`

Une commande appartient à un utilisateur.

```yaml
- name: Order
  fields:
    - name: id
      type: uuid
    - name: author_id
      type: uuid
      required: true
    - name: total
      type: decimal
  relations:
    - name: author
      target_entity: User
      kind: belongs_to
      fk_column: author_id
      on_delete: cascade
```

**Routes générées** :

| Méthode | Route | Description |
|---|---|---|
| `GET` | `/orders/{id}/author` | Récupère l'utilisateur associé à une commande. |

### Exemple : relation `has_many`

Un utilisateur a plusieurs commandes. Cette relation se déclare côté parent et est l'inverse de `belongs_to`.

```yaml
- name: User
  fields:
    - name: id
      type: uuid
  relations:
    - name: orders
      target_entity: Order
      kind: has_many
      fk_column: author_id
```

**Routes générées** :

| Méthode | Route | Description |
|---|---|---|
| `GET` | `/users/{id}/orders` | Liste les commandes de l'utilisateur. |

### Exemple : relation `has_one`

Un utilisateur a un profil unique.

```yaml
- name: User
  relations:
    - name: profile
      target_entity: Profile
      kind: has_one
      fk_column: user_id
```

**Routes générées** :

| Méthode | Route | Description |
|---|---|---|
| `GET` | `/users/{id}/profile` | Récupère le profil de l'utilisateur. |

### Exemple : relation `many_to_many`

Un utilisateur peut avoir plusieurs rôles, un rôle peut être attribué à plusieurs utilisateurs.

```yaml
- name: User
  relations:
    - name: roles
      target_entity: Role
      kind: many_to_many
      pivot_table: user_roles
      source_fk_column: user_id
      target_fk_column: role_id
```

**Routes générées** :

| Méthode | Route | Description |
|---|---|---|
| `GET` | `/users/{id}/roles` | Liste les rôles d'un utilisateur. |
| `POST` | `/users/{id}/roles/{role_id}` | Associe un rôle à un utilisateur. |
| `DELETE` | `/users/{id}/roles/{role_id}` | Supprime l'association. |

La table pivot `user_roles` est créée automatiquement avec les colonnes `user_id` et `role_id` (selon `source_fk_column` et `target_fk_column`).

### Stratégies `on_delete` pour les relations

| Valeur | Effet sur l'entité courante lorsque l'entité référencée est supprimée |
|---|---|
| `cascade` | L'entité courante est supprimée en cascade. |
| `set_null` | La colonne `fk_column` est mise à `NULL`. Requiert que le champ accepte les valeurs nulles (`required: false`). |
| `restrict` (défaut) | La suppression de l'entité référencée est refusée si des enregistrements y font référence. Retourne 409. |

---

## 14. Section `pagination`

La pagination s'active **par opération** sur chaque entité. Trois modes indépendants sont disponibles : `page`, `offset`, `cursor`. Chacun produit ses propres routes et son propre format de réponse.

### Structure générale

```yaml
- name: Product
  pagination:
    page:
      default_page_size: 20
      max_page_size: 100
      default_sort: "created_at:desc"
      sortable_fields: [name, price, created_at]
    offset:
      default_limit: 20
      max_limit: 100
      default_sort: "id:asc"
      sortable_fields: [id, name]
    cursor:
      default_limit: 20
      max_limit: 100
      cursor_field: id
      sort: "id:asc"
```

Au moins l'un des trois sous-blocs (`page`, `offset`, `cursor`) doit être présent.

### Mode `page` (pagination par numéro de page)

```yaml
pagination:
  page:
    default_page_size: 20
    max_page_size: 100
    default_sort: "created_at:desc"
    sortable_fields: [name, price, created_at]
```

| Clé | Type | Valeur par défaut | Description |
|---|---|---|---|
| `default_page_size` | entier | `20` | Taille de page utilisée si le client n'en précise pas. |
| `max_page_size` | entier | `100` | Taille de page maximale. Une valeur client supérieure est plafonnée. |
| `default_sort` | chaîne | absent | Tri par défaut au format `"champ:direction"`. Direction : `asc` ou `desc`. |
| `sortable_fields` | liste | vide | Liste blanche des champs autorisés au tri. Si vide, seul `default_sort` est utilisable. |

**Route générée** : `GET /<entité>/page`

**Query params** : `page`, `page_size`, `sort`

**Exemple de requête** :

```
GET /products/page?page=2&page_size=20&sort=name:asc
```

**Format de réponse** :

```json
{
  "items": [ ... ],
  "page": 2,
  "page_size": 20,
  "total": 532,
  "total_pages": 27,
  "sort": "name:asc"
}
```

### Mode `offset` (pagination par décalage)

```yaml
pagination:
  offset:
    default_limit: 20
    max_limit: 100
    default_sort: "id:asc"
    sortable_fields: [id, name, created_at]
```

| Clé | Type | Valeur par défaut | Description |
|---|---|---|---|
| `default_limit` | entier | `20` | Limite par défaut. |
| `max_limit` | entier | `100` | Limite maximale. |
| `default_sort` | chaîne | absent | Tri par défaut. |
| `sortable_fields` | liste | vide | Champs autorisés au tri. |

**Route générée** : `GET /<entité>/offset`

**Query params** : `offset`, `limit`, `sort`

**Exemple de requête** :

```
GET /products/offset?offset=40&limit=20&sort=created_at:desc
```

**Format de réponse** :

```json
{
  "items": [ ... ],
  "offset": 40,
  "limit": 20,
  "total": 532,
  "sort": "created_at:desc"
}
```

### Mode `cursor` (pagination par curseur)

```yaml
pagination:
  cursor:
    default_limit: 20
    max_limit: 100
    cursor_field: id
    sort: "id:asc"
```

| Clé | Type | Valeur par défaut | Description |
|---|---|---|---|
| `default_limit` | entier | `20` | Taille de page par défaut. |
| `max_limit` | entier | `100` | Taille maximale. |
| `cursor_field` | chaîne | **Obligatoire** | Champ utilisé comme curseur. Doit être unique et trié. |
| `sort` | chaîne | **Obligatoire** | Tri figé. Format `"champ:direction"`. Doit correspondre au `cursor_field`. |

**Route générée** : `GET /<entité>/cursor`

**Query params** : `after`, `limit`

**Exemple de requête** :

```
GET /products/cursor?limit=20
```

**Format de réponse** :

```json
{
  "items": [ ... ],
  "limit": 20,
  "next_cursor": "eyJpZCI6IjU1MGU4LS4uLiJ9",
  "prev_cursor": null
}
```

Requête suivante :

```
GET /products/cursor?after=eyJpZCI6IjU1MGU4LS4uLiJ9&limit=20
```

Le curseur est opaque pour le client : il doit être passé tel quel sans modification.

### Comportement attendu pour les trois modes

1. Le système valide les paramètres de la requête.
2. Si `sort` est absent et `default_sort` est défini, le tri par défaut est appliqué.
3. Si `sort` est fourni, le champ doit appartenir à `sortable_fields`, sous peine d'un 400.
4. Si `limit` ou `page_size` dépasse `max_limit` / `max_page_size`, la valeur est plafonnée.
5. La requête SQL appropriée est construite selon le mode.
6. La réponse JSON inclut les enregistrements et les métadonnées du mode utilisé.

### Comparaison des trois modes

| Mode | Avantage principal | Inconvénient principal | Cas d'usage |
|---|---|---|---|
| `page` | Affichage classique avec numéros de page. | Performance dégradée sur grandes profondeurs (LIMIT/OFFSET). | Interfaces utilisateur affichant pages 1, 2, 3... |
| `offset` | Compatibilité SQL standard, sauts arbitraires. | Même inconvénient que `page`. | Outils d'administration, exports. |
| `cursor` | Performance constante quelle que soit la profondeur. | Pas de saut arbitraire, ordre figé. | Listes temps réel, flux infinis, mobile. |

---

## 15. Section `seeds` d'une entité

Les seeds permettent d'insérer automatiquement des données initiales au démarrage du service.

### Activation globale

Les seeds doivent être activés au niveau de la configuration des migrations :

```yaml
database:
  migrations:
    enabled: true
    seeds:
      enabled: true
      mode: once
      on_error: continue
```

| Clé | Type | Valeur par défaut | Description |
|---|---|---|---|
| `enabled` | booléen | `false` | Active l'insertion des seeds au démarrage. |
| `mode` | enum | `once` | Stratégie d'insertion. Valeurs : `once`, `always`. |
| `on_error` | enum | `continue` | Comportement en cas d'erreur. Valeurs : `continue`, `abort`. |

### Modes d'insertion

| Mode | Comportement |
|---|---|
| `once` | Les seeds sont insérés uniquement si la table cible est vide. Si la table contient déjà au moins un enregistrement, les seeds sont sautés. |
| `always` | Les seeds sont insérés à chaque démarrage avec un mécanisme UPSERT (les enregistrements existants sont mis à jour selon leur clé). |

### Comportement en cas d'erreur

| Valeur | Comportement |
|---|---|
| `continue` | L'erreur est journalisée, les seeds suivants continuent. |
| `abort` | Le démarrage du service échoue. |

### Déclaration des seeds sur une entité

```yaml
- name: Role
  seeds:
    - alias: role_admin
      values:
        id: "{{uuid}}"
        name: admin
        description: "Administrateur"

    - alias: role_user
      values:
        id: "{{uuid}}"
        name: user
        description: "Utilisateur standard"

- name: User
  seeds:
    - alias: admin_account
      values:
        id: "{{uuid}}"
        email: admin@example.com
        mot_de_passe: "{{hash:AdminPassword123}}"
      m2m_relations:
        roles: [role_admin]
```

### Structure d'un seed

| Clé | Type | Obligatoire | Description |
|---|---|---|---|
| `alias` | chaîne | Non | Identifiant logique du seed, utilisable comme référence par d'autres seeds. |
| `values` | bloc | **Oui** | Valeurs des champs de l'entité. Les macros sont interprétées avant insertion. |
| `m2m_relations` | bloc | Non | Associations many-to-many à créer en parallèle de l'insertion. |

### Macros disponibles dans les valeurs

| Macro | Effet |
|---|---|
| `{{uuid}}` | Génère un UUID v4 aléatoire. |
| `{{hash:valeur}}` | Hash la valeur avec bcrypt. Utilisé exclusivement pour les champs `password`. |
| `${REF:alias}` | Référence à l'identifiant d'un autre seed via son alias. |

### Références entre seeds

L'utilisation de `${REF:alias}` permet d'établir des liens entre seeds :

```yaml
- name: User
  seeds:
    - alias: alice
      values:
        id: "{{uuid}}"
        email: alice@example.com

- name: Order
  seeds:
    - alias: order_alice_1
      values:
        id: "{{uuid}}"
        author_id: "${REF:alice}"
        total: 99.99
```

L'ordre d'insertion est déterminé automatiquement : les seeds référencés sont insérés avant ceux qui les référencent.

### Seeds avec relations many-to-many

Pour créer des associations dans une table pivot :

```yaml
- name: User
  seeds:
    - alias: alice
      values:
        id: "{{uuid}}"
        email: alice@example.com
      m2m_relations:
        roles: [role_admin, role_user]
```

Pour chaque alias listé dans `roles`, une entrée est créée dans la table pivot avec la combinaison (`alice.id`, `role_xxx.id`).

Le nom de la clé (`roles`) doit correspondre au nom d'une relation `many_to_many` déclarée sur l'entité `User`.

### Comportement attendu

1. Après la phase de migration, le système collecte tous les seeds déclarés.
2. Un graphe de dépendances est construit à partir des références `${REF:...}`.
3. Les seeds sont insérés dans l'ordre topologique de ce graphe.
4. Les macros sont résolues juste avant l'insertion.
5. Les associations m2m sont insérées dans une seconde passe.
6. Un rapport détaillé est journalisé.

---

## 16. Section `security.authentication`

Configure l'authentification JWT du service.

### Bloc complet

```yaml
security:
  authentication:
    type: jwt
    algorithm: HS256
    jwt_secret: ""
    jwt_issuer: ""
    jwt_audience: ""
    access_token_ttl: 900
    refresh_token_ttl: 1209600
    token_delivery: body
    cookies: { ... }
    token_tracking: { ... }
```

### Clés acceptées

| Clé | Type | Valeur par défaut | Description |
|---|---|---|---|
| `type` | enum | `none` | Type d'authentification. Valeurs : `none`, `jwt`, `api_key`, `basic`, `oauth2`. Seul `jwt` est pleinement supporté. |
| `algorithm` | enum | `HS256` | Algorithme de signature. Valeurs : `HS256`, `HS384`, `HS512`, `RS256`, `RS384`, `RS512`, `ES256`, `ES384`, `ES512`. |
| `jwt_secret` | chaîne | `""` | Clé secrète pour les algorithmes HS*. Si vide, génération et persistance automatique. Doit faire au moins 32 caractères si fournie. |
| `jwt_public_key_path` | chaîne | `""` | Chemin vers la clé publique pour les algorithmes RS*/ES*. |
| `jwt_private_key_path` | chaîne | `""` | Chemin vers la clé privée pour les algorithmes RS*/ES*. |
| `jwt_issuer` | chaîne | `""` | Valeur du claim `iss` dans les tokens émis. |
| `jwt_audience` | chaîne | `""` | Valeur du claim `aud` dans les tokens émis. |
| `access_token_ttl` | entier (secondes) | `900` | Durée de vie de l'access token. La valeur par défaut correspond à 15 minutes. |
| `refresh_token_ttl` | entier (secondes) | `1209600` | Durée de vie du refresh token. La valeur par défaut correspond à 14 jours. |
| `token_delivery` | enum | `body` | Mode de livraison. Valeurs : `body`, `cookie`, `both`. Voir [section 17](#17-cookies-et-livraison-des-tokens). |
| `cookies` | bloc | défauts | Configuration des cookies. Voir [section 17](#17-cookies-et-livraison-des-tokens). |
| `token_tracking` | bloc | désactivé | Suivi des tokens. Voir [section 18](#18-suivi-des-tokens-token_tracking). |

### Entité source d'authentification

L'authentification nécessite qu'une entité soit désignée comme source via `is_auth_source: true`. Cette entité doit comporter :

- Un champ identifiant unique (typiquement `email`)
- Un champ de type `password`
- Un champ utilisé comme rôle (typiquement `role`)

```yaml
- name: User
  options:
    is_auth_source: true
  fields:
    - name: id
      type: uuid
    - name: email
      type: email
      required: true
      unique: true
    - name: mot_de_passe
      type: password
      required: true
    - name: role
      type: string
      default: "user"
```

### Routes générées automatiquement

Lorsque l'authentification est activée et qu'une entité source existe, les routes suivantes sont exposées :

| Méthode | Route | Description |
|---|---|---|
| `POST` | `/auth/register` | Inscription d'un nouveau compte. |
| `POST` | `/auth/login` | Connexion avec identifiants. |
| `POST` | `/auth/refresh` | Renouvellement de l'access token. |
| `POST` | `/auth/logout` | Déconnexion. |
| `GET` | `/auth/me` | Récupération des informations du compte connecté. |

### Format des requêtes

**Inscription** :

```json
POST /auth/register
{
  "email": "alice@example.com",
  "mot_de_passe": "MotDePasse123",
  "role": "user"
}
```

**Connexion** :

```json
POST /auth/login
{
  "email": "alice@example.com",
  "mot_de_passe": "MotDePasse123"
}
```

**Réponse de connexion** (mode `body`) :

```json
{
  "access_token": "eyJhbGciOiJIUzI1NiIs...",
  "refresh_token": "eyJhbGciOiJIUzI1NiIs...",
  "token_type": "Bearer",
  "expires_in": 900
}
```

### Utilisation du token

Les requêtes vers les routes protégées doivent inclure l'access token dans l'en-tête HTTP :

```
Authorization: Bearer eyJhbGciOiJIUzI1NiIs...
```

Si le token est absent, malformé ou expiré, la réponse est 401.

---

## 17. Cookies et livraison des tokens

Le mode `token_delivery` détermine comment les tokens JWT sont transmis entre le client et le serveur.

### Modes de livraison

| Mode | Description | Cas d'usage |
|---|---|---|
| `body` (défaut) | Les tokens sont retournés dans le JSON de réponse uniquement. | API mobile, CLI, applications desktop. |
| `cookie` | Les tokens sont placés dans des cookies HttpOnly inaccessibles depuis JavaScript. | Applications web (protection XSS). |
| `both` | Les tokens sont transmis simultanément dans le JSON et dans les cookies. | Phases de migration, services accédés par plusieurs types de clients. |

### Bloc `cookies`

Lorsque `token_delivery` vaut `cookie` ou `both`, ce bloc configure les cookies :

```yaml
authentication:
  token_delivery: cookie
  cookies:
    domain: ""
    path: "/"
    secure: true
    same_site: lax
    access_token_name: sea_access
    refresh_token_name: sea_refresh
```

| Clé | Type | Valeur par défaut | Description |
|---|---|---|---|
| `domain` | chaîne | `""` (origine de la requête) | Domaine pour lequel le cookie est valide. La notation `.example.com` couvre tous les sous-domaines. |
| `path` | chaîne | `/` | Chemin pour lequel le cookie est valide. |
| `secure` | booléen | `true` | Si `true`, le cookie n'est transmis qu'en HTTPS. |
| `same_site` | enum | `lax` | Politique SameSite. Valeurs : `lax`, `strict`, `none`. |
| `access_token_name` | chaîne | `sea_access` | Nom du cookie portant l'access token. |
| `refresh_token_name` | chaîne | `sea_refresh` | Nom du cookie portant le refresh token. |

### Politique SameSite

| Valeur | Description |
|---|---|
| `strict` | Cookie envoyé uniquement pour les requêtes émanant du même site. Maximum de protection. |
| `lax` (défaut) | Cookie envoyé pour les navigations top-level. Compromis recommandé. |
| `none` | Cookie envoyé en cross-site. Nécessite `secure: true`. |

### Attribut HttpOnly

L'attribut `HttpOnly` est toujours `true` et n'est pas configurable. Cette propriété empêche JavaScript d'accéder aux cookies, protégeant contre les attaques XSS.

### Lecture des tokens côté serveur

Pour chaque requête entrante vers une route protégée, le serveur recherche le token dans l'ordre :

1. En-tête HTTP `Authorization: Bearer <token>`
2. Cookie portant le nom configuré dans `access_token_name`

Cette stratégie permet à un même service d'accepter simultanément des clients utilisant l'un ou l'autre mode.

---

## 18. Suivi des tokens (`token_tracking`)

Le suivi des tokens ajoute une couche de gestion centralisée des sessions permettant la révocation précise des access tokens et le contrôle strict des refresh tokens.

### Bloc complet

```yaml
authentication:
  token_tracking:
    enabled: true
    refresh_table: RefreshToken
    revoked_table: RevokedToken
    cache:
      enabled: true
      ttl: 300
      max_size: 10000
    rotation:
      enabled: true
    auto_cleanup:
      enabled: true
      interval: 3600
      keep_revoked_for: 2592000
```

### Clés du bloc principal

| Clé | Type | Valeur par défaut | Description |
|---|---|---|---|
| `enabled` | booléen | `false` | Active le suivi. Si `false`, les tokens sont purement stateless. |
| `refresh_table` | chaîne | `RefreshToken` | Nom de la table système contenant les refresh tokens valides (liste blanche). |
| `revoked_table` | chaîne | `RevokedToken` | Nom de la table système contenant les access tokens révoqués (liste noire). |
| `cache` | bloc | activé | Configuration du cache local. |
| `rotation` | bloc | activé | Configuration de la rotation des refresh tokens. |
| `auto_cleanup` | bloc | activé | Configuration du nettoyage périodique. |

### Sous-bloc `cache`

```yaml
cache:
  enabled: true
  ttl: 300
  max_size: 10000
```

| Clé | Type | Valeur par défaut | Description |
|---|---|---|---|
| `enabled` | booléen | `true` | Active le cache local des tokens révoqués. |
| `ttl` | entier (secondes) | `300` | Durée de mise en cache d'un résultat (5 minutes par défaut). |
| `max_size` | entier | `10000` | Nombre maximal d'entrées dans le cache. |

### Sous-bloc `rotation`

```yaml
rotation:
  enabled: true
```

| Clé | Type | Valeur par défaut | Description |
|---|---|---|---|
| `enabled` | booléen | `true` | Si `true`, chaque appel à `/auth/refresh` invalide l'ancien refresh token et en émet un nouveau. |

### Sous-bloc `auto_cleanup`

```yaml
auto_cleanup:
  enabled: true
  interval: 3600
  keep_revoked_for: 2592000
```

| Clé | Type | Valeur par défaut | Description |
|---|---|---|---|
| `enabled` | booléen | `true` | Active la suppression périodique des tokens expirés. |
| `interval` | entier (secondes) | `3600` | Intervalle entre deux nettoyages (1 heure par défaut). |
| `keep_revoked_for` | entier (secondes) | `2592000` | Durée de conservation des entrées révoquées (30 jours par défaut). |

### Tables système créées

Lorsque `enabled: true`, deux tables système sont créées automatiquement :

- La table `RefreshToken` contient les refresh tokens valides actuellement en circulation.
- La table `RevokedToken` contient les access tokens explicitement révoqués (via `/auth/logout`).

Ces tables sont automatiquement maintenues par le système et n'ont pas à être déclarées manuellement.

### Comportement attendu

1. À la connexion, un refresh token est généré et inséré dans `RefreshToken`.
2. À chaque requête vers une route protégée, l'access token est vérifié contre `RevokedToken` (avec cache).
3. À chaque appel à `/auth/refresh`, le refresh token est validé contre `RefreshToken`. Si `rotation.enabled: true`, l'ancien est supprimé et un nouveau est inséré.
4. À l'appel à `/auth/logout`, l'access token est ajouté à `RevokedToken` et le refresh token est supprimé de `RefreshToken`.
5. Toutes les `interval` secondes, le système supprime de ces tables les entrées dont l'expiration est passée.

---

## 19. Section `security.authorization`

Configure le système d'autorisation : qui peut faire quoi sur quelle entité.

### Bloc complet

```yaml
security:
  authorization:
    enabled: true
    default_policy: deny
    roles_claim_name: role
    admin_role: admin
    default_allow_admin: true
    default_scope_field: ""
    roles:
      - admin
      - manager
      - user
    abac_mode: permissive
```

### Clés acceptées

| Clé | Type | Valeur par défaut | Description |
|---|---|---|---|
| `enabled` | booléen | `false` | Active le système d'autorisation. Si `false`, toutes les routes authentifiées sont accessibles. |
| `default_policy` | enum | `deny` | Politique appliquée quand aucune règle ne correspond. Valeurs : `deny`, `allow`. |
| `roles_claim_name` | chaîne | `role` | Nom du claim JWT contenant le rôle de l'utilisateur. |
| `admin_role` | chaîne | `admin` | Nom du rôle qui bypasse toutes les règles. |
| `default_allow_admin` | booléen | `true` | Si `true`, le rôle admin bypasse également les contrôles ABAC. |
| `default_scope_field` | chaîne | `""` | Nom du champ utilisé par défaut pour `same_scope: true` lorsque l'entité ne précise pas de `scope_field`. |
| `roles` | liste | `[]` | Catalogue des rôles déclarés. Les rôles utilisés dans `allow_roles` doivent appartenir à cette liste. |
| `abac_mode` | enum | `permissive` | Mode d'évaluation ABAC. Valeurs : `permissive`, `strict`. |

### Politique par défaut

Quand aucune règle ne correspond à une combinaison entité/opération/rôle :

| Valeur | Effet |
|---|---|
| `deny` (défaut) | Refus systématique. Recommandé en production. |
| `allow` | Acceptation systématique. Réservé au développement. |

### Rôle administrateur

Le rôle déclaré dans `admin_role` bénéficie d'un bypass complet du système d'autorisation. Sa valeur est entièrement configurable :

```yaml
authorization:
  admin_role: "administrateur"   # ou "superuser", "root", etc.
```

Un utilisateur dont le claim JWT `role` correspond à `admin_role` peut accéder à toutes les routes du service sans restriction.

### Catalogue des rôles

La liste `roles` énumère les rôles valides du service. Si cette liste est définie et non vide, toute règle utilisant un rôle non listé est rejetée au démarrage.

```yaml
authorization:
  roles:
    - admin
    - manager
    - user
    - guest
```

### Mode ABAC

| Valeur | Comportement |
|---|---|
| `permissive` (défaut) | Une règle qui ne peut être évaluée (champ manquant) est considérée comme satisfaite. |
| `strict` | Toutes les règles doivent pouvoir être évaluées. Un échec d'évaluation provoque un refus. |

---

## 20. Règles d'accès (`access_control`) par entité

Chaque entité peut déclarer ses propres règles d'accès dans une section `access_control`. Ces règles définissent quels rôles peuvent effectuer chaque opération CRUD et sous quelles conditions.

### Structure générale

```yaml
- name: Document
  access_control:
    scope_field: department_id
    owner_field: created_by
    abac_mode: strict

    list:
      allow_roles: [manager, user]
      same_scope: true
    get_by_id:
      allow_roles: [manager, user]
      own_resource: true
    create:
      allow_roles: [user, manager]
    update:
      allow_roles: [manager]
      same_scope: true
    delete:
      allow_roles: [manager]
      same_scope: true
```

### Clés au niveau du bloc `access_control`

| Clé | Type | Valeur par défaut | Description |
|---|---|---|---|
| `scope_field` | chaîne | hérite de `default_scope_field` | Champ utilisé pour `same_scope: true`. |
| `owner_field` | chaîne | `""` | Champ identifiant le propriétaire, utilisé pour `own_resource: true`. |
| `abac_mode` | enum | hérite du service | Override du mode ABAC pour cette entité spécifique. Valeurs : `permissive`, `strict`. |
| `list` | bloc | absent | Règles pour `GET /<entité>`. |
| `get_by_id` | bloc | absent | Règles pour `GET /<entité>/{id}`. |
| `create` | bloc | absent | Règles pour `POST /<entité>`. |
| `update` | bloc | absent | Règles pour `PUT /<entité>/{id}`. |
| `delete` | bloc | absent | Règles pour `DELETE /<entité>/{id}`. |

Les cinq noms d'opérations sont **exacts** : `list`, `get_by_id`, `create`, `update`, `delete`. Aucun alias n'est accepté.

### Clés acceptées dans un bloc d'opération

| Clé | Type | Description |
|---|---|---|
| `allow_roles` | liste | Liste des rôles autorisés à effectuer cette opération. |
| `same_scope` | booléen ou chaîne | Active la vérification de scope. Voir détails ci-dessous. |
| `own_resource` | booléen ou chaîne | Active la vérification de propriété. Voir détails ci-dessous. |

Lorsque plusieurs clés sont déclarées dans le même bloc d'opération, elles sont liées par un **ET logique** : toutes doivent être satisfaites.

### `allow_roles`

Liste des rôles qui ont le droit d'effectuer cette opération.

```yaml
update:
  allow_roles: [manager, supervisor]
```

L'utilisateur doit avoir l'un des rôles listés. Sinon, la requête retourne 403.

**Validation** : si la section `authorization.roles` est définie, chaque rôle listé dans `allow_roles` doit appartenir à cette liste. Sinon, le démarrage du service échoue.

### `same_scope`

Vérifie que l'utilisateur opère uniquement sur des ressources de son propre périmètre (multi-tenant, département, organisation).

#### Forme booléenne

```yaml
update:
  allow_roles: [manager]
  same_scope: true
```

Avec `same_scope: true`, le système compare la valeur du `scope_field` de la ressource avec celle de l'utilisateur authentifié. Si elles diffèrent, la requête retourne 403.

Le champ utilisé est :

1. `access_control.scope_field` s'il est déclaré sur l'entité ;
2. sinon `authorization.default_scope_field` ;
3. si aucun n'est défini, la configuration est rejetée au démarrage.

#### Forme chaîne

```yaml
update:
  allow_roles: [manager]
  same_scope: organization_id
```

Avec une chaîne, le champ utilisé est celui passé explicitement, indépendamment des défauts.

#### Exemple complet

```yaml
- name: Document
  fields:
    - name: id
      type: uuid
    - name: department_id
      type: uuid
      required: true
    - name: title
      type: string
  access_control:
    scope_field: department_id

    list:
      allow_roles: [manager, employee]
      same_scope: true
    update:
      allow_roles: [manager]
      same_scope: true
```

**Comportement** :

- Un manager dont `department_id` vaut `dept-A` voit uniquement les documents du département A en `GET /documents`.
- Un manager dont `department_id` vaut `dept-A` ne peut modifier que les documents du département A. Une tentative sur un document de `dept-B` retourne 403.

### `own_resource`

Vérifie que l'utilisateur opère uniquement sur les ressources dont il est propriétaire.

#### Forme booléenne

```yaml
update:
  allow_roles: [user]
  own_resource: true
```

Avec `own_resource: true`, le système compare la valeur du `owner_field` de la ressource avec l'identifiant de l'utilisateur authentifié. Si elles diffèrent, la requête retourne 403.

Le champ utilisé est `access_control.owner_field`. Si cet attribut n'est pas déclaré sur l'entité, la configuration est rejetée au démarrage.

#### Forme chaîne

```yaml
update:
  allow_roles: [user]
  own_resource: author_id
```

Le champ utilisé est celui passé explicitement.

#### Exemple complet

```yaml
- name: Article
  fields:
    - name: id
      type: uuid
    - name: author_id
      type: uuid
      required: true
    - name: title
      type: string
  access_control:
    owner_field: author_id

    list:
      allow_roles: [user]
    get_by_id:
      allow_roles: [user]
      own_resource: true
    update:
      allow_roles: [user]
      own_resource: true
    delete:
      allow_roles: [user]
      own_resource: true
```

**Comportement** :

- Un utilisateur peut lister tous les articles (`list` sans `own_resource`).
- Il ne peut consulter, modifier ou supprimer que les articles dont il est l'auteur.

### Combinaison `same_scope` + `own_resource`

Les deux mécanismes peuvent être combinés. Toutes les conditions doivent alors être satisfaites :

```yaml
update:
  allow_roles: [user]
  same_scope: true
  own_resource: true
```

L'utilisateur doit appartenir au bon scope **et** être propriétaire de la ressource.

### Bypass administrateur

Lorsque `authorization.default_allow_admin: true` (valeur par défaut), un utilisateur ayant le rôle `admin_role` bypasse l'ensemble des conditions `same_scope` et `own_resource`. Il peut accéder à toutes les ressources.

### Comportement attendu pour chaque opération

| Opération | Effet de `same_scope` / `own_resource` |
|---|---|
| `list` | Filtre la requête SQL : seuls les enregistrements satisfaisant les conditions sont retournés. |
| `get_by_id` | Vérifie après chargement : un enregistrement ne satisfaisant pas les conditions retourne 403. |
| `create` | Force automatiquement la valeur du champ scope/owner à celle de l'utilisateur. |
| `update`, `delete` | Vérifie après chargement : un enregistrement ne satisfaisant pas les conditions retourne 403. |

### Exemple complet avec deux entités

```yaml
authorization:
  enabled: true
  default_policy: deny
  admin_role: admin
  default_scope_field: organization_id
  roles: [admin, manager, employee]

entities:
  - name: Project
    fields:
      - name: id
        type: uuid
      - name: organization_id
        type: uuid
        required: true
      - name: name
        type: string
      - name: created_by
        type: uuid
        required: true

    access_control:
      owner_field: created_by

      list:
        allow_roles: [manager, employee]
        same_scope: true
      get_by_id:
        allow_roles: [manager, employee]
        same_scope: true
      create:
        allow_roles: [manager, employee]
      update:
        allow_roles: [manager]
        same_scope: true
      delete:
        allow_roles: [manager]
        same_scope: true

  - name: Note
    fields:
      - name: id
        type: uuid
      - name: project_id
        type: uuid
        required: true
      - name: organization_id
        type: uuid
        required: true
      - name: author_id
        type: uuid
        required: true
      - name: content
        type: text

    access_control:
      owner_field: author_id

      list:
        allow_roles: [manager, employee]
        same_scope: true
      get_by_id:
        allow_roles: [manager, employee]
        same_scope: true
      create:
        allow_roles: [employee, manager]
      update:
        allow_roles: [employee]
        own_resource: true
      delete:
        allow_roles: [employee, manager]
        own_resource: true
```

**Comportement résultant** :

| Action | Manager (org-A) | Employee Alice (org-A, auteur) | Employee Bob (org-A) | Employee (org-B) | Admin |
|---|---|---|---|---|---|
| Lister les projets org-A | autorisé | autorisé | autorisé | refusé | autorisé |
| Modifier un projet org-A | autorisé | refusé | refusé | refusé | autorisé |
| Modifier sa note (Alice) | refusé (pas auteur) | autorisé | refusé | refusé | autorisé |
| Supprimer la note d'Alice | refusé (pas auteur) | autorisé | refusé | refusé | autorisé |
| Supprimer la note de Bob | refusé (pas auteur) | refusé | autorisé | refusé | autorisé |

---

## 21. Section `security.cors`

Configure le partage de ressources entre origines pour permettre aux applications web hébergées sur un domaine différent de consommer l'API.

### Bloc complet

```yaml
security:
  cors:
    enabled: true
    allowed_origins:
      - "https://app.example.com"
      - "https://admin.example.com"
    allowed_methods: [GET, POST, PUT, DELETE, OPTIONS]
    allowed_headers: [Authorization, Content-Type]
    expose_headers: [X-Request-Id]
    allow_credentials: true
    max_age: 3600
```

### Clés acceptées

| Clé | Type | Valeur par défaut | Description |
|---|---|---|---|
| `enabled` | booléen | `false` | Active la gestion CORS. |
| `allowed_origins` | liste | `[]` | Origines autorisées. La valeur `["*"]` autorise toutes les origines (incompatible avec `allow_credentials: true`). |
| `allowed_methods` | liste | `[GET, POST, PUT, DELETE, OPTIONS]` | Méthodes HTTP autorisées. |
| `allowed_headers` | liste | `[Authorization, Content-Type]` | En-têtes HTTP que le client peut envoyer. |
| `expose_headers` | liste | `[]` | En-têtes que le client peut lire dans les réponses. |
| `allow_credentials` | booléen | `false` | Si `true`, le client peut envoyer des cookies et utiliser l'authentification. |
| `max_age` | entier (secondes) | `3600` | Durée de mise en cache des réponses preflight `OPTIONS`. |

### Comportement attendu

- Pour chaque requête, si CORS est activé et l'origine du client est dans `allowed_origins`, les en-têtes CORS sont ajoutés à la réponse.
- Les requêtes preflight `OPTIONS` sont traitées automatiquement.
- Si l'origine n'est pas autorisée, la requête est traitée normalement mais le navigateur la bloque côté client.

---

## 22. Section `security.rate_limits`

Plafonne le nombre de requêtes acceptées par client sur une période donnée.

### Bloc complet

```yaml
security:
  rate_limits:
    - name: auth_login
      pattern: "/auth/login"
      method: POST
      max_requests: 5
      window_seconds: 60
      key_strategy: ip

    - name: global_api
      pattern: "/*"
      max_requests: 1000
      window_seconds: 60
      key_strategy: token
```

### Clés acceptées pour une règle

| Clé | Type | Obligatoire | Description |
|---|---|---|---|
| `name` | chaîne | **Oui** | Identifiant unique de la règle, utilisé dans les logs. |
| `pattern` | chaîne | **Oui** | Motif d'URL à matcher. |
| `method` | enum ou liste | Non | Méthodes HTTP concernées. Si absent, toutes. |
| `max_requests` | entier | **Oui** | Nombre maximal de requêtes autorisées sur la fenêtre. |
| `window_seconds` | entier | **Oui** | Durée de la fenêtre glissante. |
| `key_strategy` | enum | `ip` | Stratégie de regroupement. Valeurs : `ip`, `token`, `ip_and_token`. |

### Motifs supportés

| Motif | Correspondance |
|---|---|
| `/auth/login` | Cette route exacte. |
| `/users/*` | Une route directement sous `/users/`. |
| `/*` | Toutes les routes du service. |

### Stratégies de regroupement

| Valeur | Comportement |
|---|---|
| `ip` (défaut) | Compteur par adresse IP. Adapté aux routes publiques (`/auth/login`). |
| `token` | Compteur par token JWT. Adapté aux routes authentifiées. |
| `ip_and_token` | Compteur par combinaison IP + token. Plus restrictif. |

### Comportement attendu

- Pour chaque requête, le système identifie la règle la plus spécifique correspondant au chemin et à la méthode.
- Un compteur est incrémenté selon `key_strategy`.
- Si le compteur dépasse `max_requests` dans la fenêtre, la réponse est 429 avec un en-tête `Retry-After`.

---

## 23. Section `security.security_headers`

Ajoute des en-têtes HTTP de sécurité standard à toutes les réponses du service.

### Bloc complet

```yaml
security:
  security_headers:
    enabled: true
    x_content_type_options: nosniff
    x_frame_options: DENY
    strict_transport_security: "max-age=31536000; includeSubDomains"
    content_security_policy: "default-src 'self'"
    referrer_policy: strict-origin-when-cross-origin
```

| Clé | Description | Valeur recommandée |
|---|---|---|
| `enabled` | Active l'injection automatique. | `true` |
| `x_content_type_options` | Empêche le navigateur de deviner le type MIME. | `nosniff` |
| `x_frame_options` | Empêche l'inclusion dans une iframe (anti-clickjacking). | `DENY` ou `SAMEORIGIN` |
| `strict_transport_security` | Force HTTPS pour les futures connexions. | `"max-age=31536000; includeSubDomains"` |
| `content_security_policy` | Restreint les origines des ressources. | Variable selon contexte. |
| `referrer_policy` | Contrôle l'en-tête Referer. | `strict-origin-when-cross-origin` |

### Comportement

Lorsque `enabled: true`, les en-têtes déclarés sont ajoutés à toutes les réponses HTTP du service, y compris les erreurs.

---

## 24. Section `security.http_limits`

Applique des limites sur le contenu des requêtes entrantes.

### Bloc complet

```yaml
security:
  http_limits:
    max_body_size: "10MB"
    max_header_size: "16KB"
    request_timeout_seconds: 30
```

| Clé | Type | Valeur par défaut | Description |
|---|---|---|---|
| `max_body_size` | taille | `"10MB"` | Taille maximale du corps de requête. Formats : `500KB`, `10MB`, `1GB`. |
| `max_header_size` | taille | `"16KB"` | Taille maximale totale des en-têtes. |
| `request_timeout_seconds` | entier | `30` | Délai maximum pour qu'une requête soit complètement reçue. |

### Comportement

| Dépassement | Code retourné |
|---|---|
| Corps trop volumineux | 413 |
| En-têtes trop volumineux | 431 |
| Délai dépassé | 408 |

---

## 25. Section `storage`

Configure le backend de stockage de fichiers pour les champs de type `file`.

### Bloc complet

```yaml
services:
  - name: MonService
    storage:
      backend: filesystem
      root_path: "/var/lib/seadesktop/uploads"
      file_mode: "0640"
      directory_mode: "0750"
```

| Clé | Type | Valeur par défaut | Description |
|---|---|---|---|
| `backend` | enum | `filesystem` | Type de backend. Seul `filesystem` est implémenté actuellement. |
| `root_path` | chaîne | `./uploads` | Répertoire racine où tous les fichiers sont organisés. |
| `file_mode` | chaîne octale | `"0640"` | Permissions Unix appliquées aux fichiers créés. **Doit être déclarée entre guillemets.** |
| `directory_mode` | chaîne octale | `"0750"` | Permissions Unix appliquées aux répertoires créés. **Doit être déclarée entre guillemets.** |

### Note critique sur les modes octaux

Les valeurs `file_mode` et `directory_mode` doivent impérativement être déclarées sous forme de **chaînes** entre guillemets (`"0640"`). Le standard YAML interprète `0640` non quoté comme la valeur décimale 640, ce qui ne correspond à aucune permission Unix valide.

### Comportement en l'absence du bloc

- Si aucun champ de type `file` n'est déclaré dans le schéma, le bloc `storage` est facultatif et le système ne crée pas de stockage.
- Si au moins un champ `file` est déclaré et que le bloc `storage` est absent, le système applique un fallback : `backend: filesystem`, `root_path: ./uploads`, avec un avertissement journalisé.

### Documentation détaillée

Le stockage de fichiers est traité en détail dans le document `FILE_FEATURE_USER_GUIDE.md`.

---

## 26. Section `logging`

Configure le système de logs structurés du service.

### Bloc complet

```yaml
services:
  - name: MonService
    logging:
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
            max_files: 30

      flush_level: error

      async:
        enabled: true
        queue_size: 8192
        overflow_policy: overrun_oldest
```

### Clés du bloc principal

| Clé | Type | Valeur par défaut | Description |
|---|---|---|---|
| `level` | enum | `info` | Niveau global par défaut. Valeurs : `trace`, `debug`, `info`, `warn`, `error`, `critical`, `off`. |
| `modules` | bloc | `{}` | Niveau par module (override de `level`). |
| `sinks` | liste | console texte | Destinations des logs. |
| `flush_level` | enum | `error` | Niveau au-delà duquel l'écriture sur disque est immédiate. |
| `async` | bloc | activé | Configuration asynchrone. |

### Niveaux de log

| Niveau | Usage typique |
|---|---|
| `trace` | Trace très détaillée. Désactivé en production. |
| `debug` | Diagnostic, variables internes. |
| `info` | Événements normaux : démarrage, opérations réussies. |
| `warn` | Anomalies tolérables. |
| `error` | Exceptions, opérations échouées. |
| `critical` | Service inutilisable. |
| `off` | Module entièrement désactivé. |

### Modules disponibles

| Module | Contenu |
|---|---|
| `sea.boot` | Démarrage, migrations. |
| `sea.http` | Handlers HTTP, autorisation. |
| `sea.application` | Services applicatifs. |
| `sea.persistence` | Requêtes base de données. |
| `sea.runtime` | Validation, sérialisation. |
| `sea.security` | Authentification, tokens. |
| `seastar` | Logs internes du framework réseau. |

### Sinks

Chaque sink est une destination de log. Plusieurs sinks peuvent coexister ; chaque log est envoyé à tous les sinks actifs.

#### Sink `console`

```yaml
- type: console
  format: text
  enabled: true
```

Écrit sur stderr avec couleurs ANSI.

#### Sink `file`

```yaml
- type: file
  format: json
  enabled: true
  path: "./logs/service.log"
  rotation:
    max_size: "100MB"
    time_pattern: daily
    max_files: 30
```

| Clé | Type | Description |
|---|---|---|
| `path` | chaîne | Chemin du fichier. Le dossier parent est créé automatiquement. |
| `rotation.max_size` | taille | Rotation par taille. Format : `100KB`, `100MB`, `1GB`. |
| `rotation.time_pattern` | enum | Rotation par temps. Valeurs : `none`, `daily`. |
| `rotation.max_files` | entier | Nombre maximum d'archives. |

### Formats

| Format | Sortie exemple |
|---|---|
| `text` | `[2026-05-14 10:23:45.123] [sea.http] [info] Message` |
| `json` | `{"timestamp":"2026-05-14T10:23:45.123Z","logger":"sea.http","level":"info","message":"Message"}` |

### Endpoints de visualisation

Activer le logging expose automatiquement deux endpoints REST :

| Méthode | Route | Description |
|---|---|---|
| `GET` | `/admin/logs` | Lecture des derniers logs en mémoire avec filtrage. |
| `GET` | `/admin/logs/loggers` | Liste des loggers disponibles. |

Ces endpoints sont protégés : authentification + rôle correspondant à `authorization.admin_role`.

### Documentation détaillée

Le système de logging est documenté en détail dans `logging.md`. Voir [section 29](#29-documentation-complémentaire).

---

## 27. Endpoints système générés

En plus des routes générées pour chaque entité, le système expose plusieurs endpoints fonctionnels sans configuration supplémentaire.

| Méthode | Route | Authentification | Description |
|---|---|---|---|
| `GET` | `/health` | Aucune | Vérification que le service est opérationnel. |
| `GET` | `/openapi.json` | Aucune | Spécification OpenAPI 3.0 complète. |
| `GET` | `/docs` | Aucune | Interface Swagger UI. |
| `GET` | `/admin/logs` | Admin uniquement | Lecture des logs récents. |
| `GET` | `/admin/logs/loggers` | Admin uniquement | Liste des loggers. |
| `POST` | `/auth/register` | Aucune | Inscription d'un nouveau compte (si authentification activée). |
| `POST` | `/auth/login` | Aucune | Connexion (si authentification activée). |
| `POST` | `/auth/refresh` | Aucune | Renouvellement d'access token. |
| `POST` | `/auth/logout` | Authentification requise | Déconnexion. |
| `GET` | `/auth/me` | Authentification requise | Informations du compte connecté. |

---

## 28. Codes de réponse HTTP

L'ensemble des routes générées suit les conventions HTTP standard.

### Codes de succès

| Code | Signification | Opérations |
|---|---|---|
| 200 | Succès avec corps de réponse | GET, PUT |
| 201 | Ressource créée | POST |
| 204 | Succès sans corps | DELETE |

### Codes d'erreur client

| Code | Causes typiques |
|---|---|
| 400 | Validation échouée, format JSON incorrect, paramètres manquants ou invalides |
| 401 | Token absent, expiré, signature invalide |
| 403 | Rôle insuffisant, `same_scope` ou `own_resource` non satisfait |
| 404 | Identifiant inexistant, route inconnue |
| 409 | Contrainte d'unicité violée, `on_delete: restrict` empêche la suppression |
| 413 | Corps de requête trop volumineux |
| 429 | Rate limit dépassé |

### Codes d'erreur serveur

| Code | Causes typiques |
|---|---|
| 500 | Exception non capturée, erreur d'I/O |
| 503 | Base de données injoignable, service en surcharge |

### Format des réponses d'erreur

```json
{
  "error": "Validation failed",
  "details": {
    "email": "must be a valid email address"
  }
}
```

---

## 29. Documentation complémentaire

Plusieurs documents complémentaires détaillent des fonctionnalités spécifiques :

| Document | Sujet |
|---|---|
| `auth.md` | Authentification complète : JWT, cookies, token tracking, rotation, sécurité. |
| `pagination.md` | Pagination : trois modes, syntaxe complète, exemples par cas d'usage. |
| `logging.md` | Logging : architecture spdlog, sinks, formats, endpoint `/admin/logs`. |
| `FILE_FEATURE_USER_GUIDE.md` | Stockage de fichiers : déclaration, upload, download, partage entre entités, suppression. |

Ces documents approfondissent les sections correspondantes de ce guide.

---

## Annexe : exemple de fichier complet

L'exemple suivant illustre la combinaison cohérente des principales fonctionnalités.

```yaml
project:
  name: BlogPlatform

services:
  - name: BlogService
    port: 8080

    database:
      type: mysql
      host: localhost
      port: 3306
      database_name: blog_db
      username: root
      password: rootpassword
      migrations:
        enabled: true
        create_database_if_missing: true
        mode: modified
        seeds:
          enabled: true
          mode: once
          on_error: continue

    storage:
      backend: filesystem
      root_path: ./uploads
      file_mode: "0640"
      directory_mode: "0750"

    security:
      authentication:
        type: jwt
        algorithm: HS256
        access_token_ttl: 900
        refresh_token_ttl: 1209600
        token_delivery: both
        cookies:
          secure: false
          same_site: lax
        token_tracking:
          enabled: true
          rotation:
            enabled: true
          auto_cleanup:
            enabled: true
            interval: 3600

      authorization:
        enabled: true
        default_policy: deny
        admin_role: admin
        roles: [admin, author, reader]

      cors:
        enabled: true
        allowed_origins: ["http://localhost:3000"]
        allowed_methods: [GET, POST, PUT, DELETE, OPTIONS]
        allow_credentials: true

      rate_limits:
        - name: auth_login
          pattern: "/auth/login"
          method: POST
          max_requests: 5
          window_seconds: 60
          key_strategy: ip

    logging:
      level: info
      modules:
        sea.http: debug
        seastar: warn
      sinks:
        - type: console
          format: text
          enabled: true
        - type: file
          format: json
          enabled: true
          path: ./logs/blog.log
          rotation:
            max_size: "100MB"
            time_pattern: daily
            max_files: 14
      flush_level: error
      async:
        enabled: true
        queue_size: 8192

    entities:
      - name: User
        options:
          is_auth_source: true
          timestamps: true
        fields:
          - name: id
            type: uuid
          - name: email
            type: email
            required: true
            unique: true
          - name: mot_de_passe
            type: password
            required: true
          - name: nom
            type: string
            required: true
            max_length: 100
          - name: avatar
            type: file
            required: false
            file:
              max_size: 2MB
              allowed_mime_types: [image/png, image/jpeg]
              storage_path: users/avatars
              on_delete: cascade
          - name: role
            type: string
            default: "reader"
        access_control:
          owner_field: id
          get_by_id:
            allow_roles: [author, reader]
            own_resource: true
          update:
            allow_roles: [author, reader]
            own_resource: true

      - name: Article
        options:
          timestamps: true
        fields:
          - name: id
            type: uuid
          - name: author_id
            type: uuid
            required: true
          - name: title
            type: string
            required: true
            max_length: 200
          - name: content
            type: text
          - name: published
            type: bool
            default: false
          - name: banner
            type: file
            required: false
            file:
              max_size: 10MB
              allowed_mime_types: [image/png, image/jpeg]
              storage_path: articles/banners
              on_delete: set_null
        relations:
          - name: author
            target_entity: User
            kind: belongs_to
            fk_column: author_id
            on_delete: cascade
        pagination:
          page:
            default_page_size: 10
            max_page_size: 50
            default_sort: "created_at:desc"
            sortable_fields: [created_at, title]
        access_control:
          owner_field: author_id
          list:
            allow_roles: [author, reader]
          get_by_id:
            allow_roles: [author, reader]
          create:
            allow_roles: [author]
          update:
            allow_roles: [author]
            own_resource: true
          delete:
            allow_roles: [author]
            own_resource: true
        seeds:
          - alias: welcome_article
            values:
              id: "{{uuid}}"
              author_id: "${REF:admin_user}"
              title: "Bienvenue"
              content: "Premier article"
              published: true
```

---

*Cette documentation reflète l'état du code source au moment de sa rédaction. Les fonctionnalités à venir (support PostgreSQL et MongoDB, backends de stockage additionnels, compression des archives de logs) seront documentées au fur et à mesure de leur intégration.*
