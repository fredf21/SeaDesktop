# SeaDesktop — Champ de type `file`

Guide d'utilisation du type de champ `file` dans les schémas YAML SeaDesktop. Ce document décrit la déclaration des champs fichiers, leur configuration, le comportement du système associé et les modalités d'interaction via l'API HTTP.

---

## Sommaire

1. [Déclaration minimale](#1-déclaration-minimale)
2. [Configuration du stockage au niveau service](#2-configuration-du-stockage-au-niveau-service)
3. [Configuration d'un champ `file`](#3-configuration-dun-champ-file)
4. [Stratégies de suppression (`on_delete`)](#4-stratégies-de-suppression-on_delete)
5. [Envoi d'un fichier (upload)](#5-envoi-dun-fichier-upload)
6. [Récupération d'un fichier (download)](#6-récupération-dun-fichier-download)
7. [Modification et détachement](#7-modification-et-détachement)
8. [Suppression d'une entité référençant un fichier](#8-suppression-dune-entité-référençant-un-fichier)
9. [Partage d'un fichier entre plusieurs entités](#9-partage-dun-fichier-entre-plusieurs-entités)
10. [Codes de réponse et gestion des erreurs](#10-codes-de-réponse-et-gestion-des-erreurs)
11. [Exemples de configurations courantes](#11-exemples-de-configurations-courantes)

---

## 1. Déclaration minimale

La configuration la plus simple permettant d'activer la gestion de fichiers sur une entité se présente ainsi :

```yaml
project:
  name: ApplicationDocumentaire

services:
  - name: ServicePrincipal
    port: 8080

    database:
      type: mysql
      host: localhost
      port: 3306
      database_name: app
      username: root
      password: rootpassword

    entities:
      - name: Photo
        fields:
          - name: id
            type: uuid
          - name: image
            type: file
            file:
              storage_path: photos
              on_delete: cascade
```

Au démarrage du service, le système réalise automatiquement les actions suivantes :

- Création de la table `Photo` en base de données
- Création de la table système `sea_files` qui maintient les métadonnées des fichiers
- Création du répertoire `./uploads/photos/` sur disque
- Génération des routes HTTP : `POST /photos`, `GET /photos/image/{id}`, `PUT /photos/{id}`, `DELETE /photos/{id}`

La déclaration du bloc `storage:` au niveau service n'est pas obligatoire. En son absence, le système applique des valeurs par défaut (répertoire racine `./uploads`).

---

## 2. Configuration du stockage au niveau service

Pour personnaliser l'emplacement et les permissions des fichiers, déclarer un bloc `storage:` au niveau du service.

```yaml
services:
  - name: ServicePrincipal
    storage:
      backend: filesystem
      root_path: /var/lib/application/uploads
      file_mode: "0640"
      directory_mode: "0750"
```

| Clé | Valeur attendue | Valeur par défaut | Description |
|---|---|---|---|
| `backend` | `filesystem` | `filesystem` | Type de backend de stockage. Seul `filesystem` est supporté actuellement. |
| `root_path` | chemin | `./uploads` | Répertoire racine sous lequel tous les fichiers sont organisés. |
| `file_mode` | octal en string | `"0640"` | Permissions Unix appliquées aux fichiers créés. |
| `directory_mode` | octal en string | `"0750"` | Permissions Unix appliquées aux répertoires créés. |

### Note sur les modes octaux

Les modes Unix doivent être déclarés sous forme de chaînes de caractères (`"0640"`) et non comme entiers (`0640`). Le standard YAML interprète `0640` comme la valeur décimale 640, ce qui ne correspond à aucune permission Unix valide. L'usage des guillemets force une interprétation littérale, que le parser convertit ensuite en base 8.

### Comportement en cas d'omission du bloc

Lorsqu'une entité du schéma déclare un champ de type `file` mais que le bloc `storage:` est absent au niveau service, le système applique le fallback suivant :

- `backend` : `filesystem`
- `root_path` : `./uploads`

Un message d'avertissement est journalisé au démarrage afin de signaler l'usage du fallback. Une configuration explicite est recommandée en environnement de production.

---

## 3. Configuration d'un champ `file`

Un champ accepte des fichiers lorsque son type est défini à `file` et qu'un sous-bloc `file:` détaille les contraintes applicables.

```yaml
fields:
  - name: avatar
    type: file
    required: false
    file:
      max_size: 5MB
      allowed_mime_types:
        - image/png
        - image/jpeg
      allowed_extensions:
        - .png
        - .jpg
        - .jpeg
      storage_path: users/avatars
      on_delete: cascade
```

| Clé | Type | Obligatoire | Description |
|---|---|---|---|
| `max_size` | taille (chaîne) | Non | Taille maximale acceptée. Formats supportés : `500KB`, `5MB`, `1GB`. En l'absence de cette contrainte, seule la limite globale du serveur s'applique. |
| `allowed_mime_types` | liste de chaînes | Non | Liste blanche des types MIME acceptés. Si absente ou vide, tous les types sont acceptés. |
| `allowed_extensions` | liste de chaînes | Non | Liste blanche des extensions autorisées (avec le point). La comparaison est insensible à la casse. Si absente, toutes les extensions sont acceptées. |
| `storage_path` | chaîne | **Oui** | Sous-répertoire, relatif à `root_path`, où sont stockés les fichiers du champ. |
| `on_delete` | enum | **Oui** | Stratégie de gestion lors de la suppression de l'entité parente. Valeurs : `cascade`, `set_null`, `restrict`. |

### Contraintes sur `storage_path`

La valeur de `storage_path` doit respecter les règles suivantes :

- Chemin relatif uniquement (le caractère `/` initial est interdit)
- Aucune séquence `..` (interdiction de remontée hiérarchique)
- Aucun segment vide (par exemple `users//avatars` est rejeté)

Le répertoire est créé automatiquement au démarrage s'il n'existe pas.

### Formats de taille acceptés

| Notation | Valeur en octets |
|---|---|
| `100` | 100 |
| `500B` | 500 |
| `2KB` | 2 048 |
| `5MB` | 5 242 880 |
| `1GB` | 1 073 741 824 |

---

## 4. Stratégies de suppression (`on_delete`)

La stratégie `on_delete` définit le comportement du système concernant le fichier physique lorsque l'entité parente est supprimée ou modifiée. Trois stratégies sont disponibles.

### Stratégie `cascade`

Le fichier est supprimé du disque lorsque l'entité parente est supprimée, à condition qu'aucune autre entité ne le référence.

```yaml
- name: avatar
  type: file
  file:
    storage_path: users/avatars
    on_delete: cascade
```

**Usage recommandé** : avatars de profil, vignettes, données dont la durée de vie est strictement liée à celle de l'entité parente.

**Comportement** :
- Suppression de l'entité : le fichier est supprimé du disque (si non partagé)
- Modification remplaçant le fichier : l'ancien fichier est supprimé du disque (si non partagé)

### Stratégie `set_null`

Le fichier est conservé sur disque lorsque l'entité parente est supprimée, même s'il devient orphelin (plus aucune entité ne le référence).

```yaml
- name: banner
  type: file
  file:
    storage_path: articles/banners
    on_delete: set_null
```

**Usage recommandé** : illustrations d'articles à archiver, pièces jointes à conserver à des fins d'audit ou de traçabilité.

**Comportement** :
- Suppression de l'entité : le fichier est conservé sur disque
- Modification remplaçant le fichier : l'ancien fichier est conservé sur disque

Les fichiers orphelins ne sont pas nettoyés automatiquement par le système. Un nettoyage périodique manuel ou par tâche planifiée doit être mis en place si la récupération de l'espace disque est requise.

### Stratégie `restrict`

La suppression de l'entité est refusée tant qu'un fichier reste attaché au champ. Le système retourne un code 409 Conflict, et la suppression effective requiert un détachement préalable du fichier.

```yaml
- name: pdf
  type: file
  file:
    storage_path: contracts/pdfs
    on_delete: restrict
```

**Usage recommandé** : contrats signés, factures, documents légaux et plus généralement toute donnée dont la perte accidentelle doit être prévenue.

**Comportement** :
- Suppression de l'entité : refusée avec code 409 si un fichier est attaché
- Modification détachant le fichier (assignation `null`) : autorisée
- Procédure de suppression complète : détachement préalable par requête PUT, puis DELETE

---

## 5. Envoi d'un fichier (upload)

Le système accepte trois modalités d'envoi de fichier, sélectionnables selon les contraintes du client.

### Modalité 1 — Requête multipart/form-data

Modalité recommandée pour les clients disposant d'un fichier sur disque (formulaires HTML, outils en ligne de commande).

```bash
curl -X POST http://localhost:8080/users \
  -F "email=alice@example.com" \
  -F "name=Alice" \
  -F "avatar=@./photo.png;type=image/png"
```

Réponse type :

```json
{
  "id": "550e8400-e29b-41d4-a716-446655440000",
  "email": "alice@example.com",
  "name": "Alice",
  "avatar": "660f9511-f30c-52e5-b827-557766551111"
}
```

La valeur du champ `avatar` dans la réponse est l'identifiant unique attribué au fichier par le système, et non le contenu binaire.

### Modalité 2 — Requête JSON avec contenu encodé en base64

Modalité adaptée aux clients devant émettre des requêtes JSON exclusives (applications mobiles, applications web monopage).

```bash
AVATAR=$(base64 -w0 photo.png)
curl -X POST http://localhost:8080/users \
  -H "Content-Type: application/json" \
  -d "{
    \"email\": \"alice@example.com\",
    \"avatar\": {
      \"filename\": \"photo.png\",
      \"mime_type\": \"image/png\",
      \"content_base64\": \"$AVATAR\"
    }
  }"
```

### Modalité 3 — Référence à un fichier existant par UUID

Lorsque l'identifiant d'un fichier déjà présent dans le système est connu, il peut être directement assigné à une nouvelle entité, évitant un nouveau transfert.

```bash
curl -X POST http://localhost:8080/users \
  -H "Content-Type: application/json" \
  -d '{
    "email": "bob@example.com",
    "avatar": "660f9511-f30c-52e5-b827-557766551111"
  }'
```

Les cas d'usage relatifs à cette modalité sont détaillés dans la section [Partage d'un fichier](#9-partage-dun-fichier-entre-plusieurs-entités).

---

## 6. Récupération d'un fichier (download)

Une route de téléchargement est générée automatiquement pour chaque champ de type `file` selon le format :

```
GET /<entité au pluriel>/<nom du champ>/<id de l'entité>
```

Correspondance entre la déclaration et la route générée :

| Champ déclaré | Route générée |
|---|---|
| `User.avatar` | `GET /users/avatar/{id}` |
| `Article.banner` | `GET /articles/banner/{id}` |
| `Contract.pdf` | `GET /contracts/pdf/{id}` |

Exemple de récupération :

```bash
curl -o telecharge.png http://localhost:8080/users/avatar/550e8400-e29b-41d4-a716-446655440000
```

La réponse comprend :

- Le contenu binaire du fichier
- Un en-tête `Content-Type` correspondant au type MIME enregistré lors de l'upload
- Un en-tête `Content-Disposition: inline; filename="<nom_original>"` permettant l'affichage direct des images et documents PDF par les navigateurs

L'ouverture de l'URL dans un navigateur permet la visualisation directe du fichier sans téléchargement préalable.

---

## 7. Modification et détachement

### Remplacement d'un fichier

```bash
curl -X PUT http://localhost:8080/users/550e8400-... \
  -F "email=alice@example.com" \
  -F "name=Alice" \
  -F "avatar=@./nouvelle_photo.png;type=image/png"
```

Déroulement de l'opération :

1. Le nouveau fichier est enregistré et reçoit un nouvel identifiant
2. L'entité est mise à jour avec ce nouvel identifiant
3. L'ancien fichier est traité conformément à la stratégie `on_delete` du champ :
   - `cascade` : ancien fichier supprimé du disque (s'il n'est plus référencé)
   - `set_null` : ancien fichier conservé sur disque
   - `restrict` : ancien fichier déréférencé (la restriction ne s'applique qu'à la suppression de l'entité)

### Détachement d'un fichier

```bash
curl -X PUT http://localhost:8080/users/550e8400-... \
  -H "Content-Type: application/json" \
  -d '{
    "email": "alice@example.com",
    "name": "Alice",
    "avatar": null
  }'
```

L'entité ne référence plus aucun fichier sur ce champ. L'ancien fichier est traité selon les mêmes règles que pour le remplacement.

---

## 8. Suppression d'une entité référençant un fichier

```bash
curl -X DELETE http://localhost:8080/users/550e8400-...
```

Le résultat dépend de la stratégie `on_delete` configurée :

| Stratégie | Résultat |
|---|---|
| `cascade` | Entité supprimée. Fichier supprimé du disque s'il n'est plus référencé. Réponse 200. |
| `set_null` | Entité supprimée. Fichier conservé sur disque (devient orphelin si non partagé). Réponse 200. |
| `restrict` | Suppression refusée si un fichier reste attaché. Réponse 409 Conflict. Un détachement préalable est requis. |

### Procédure de suppression sous stratégie `restrict`

```bash
# Tentative de suppression directe : refusée
curl -X DELETE http://localhost:8080/contracts/550e8400-...

# Réponse : 409 Conflict
# {
#   "error": "Conflict",
#   "message": "L'entite 'Contract' ne peut pas etre supprimee :
#               le champ file 'pdf' a une regle on_delete=restrict.
#               Detacher/remplacer le fichier avant suppression."
# }

# Étape 1 : détachement du fichier
curl -X PUT http://localhost:8080/contracts/550e8400-... \
  -H "Content-Type: application/json" \
  -d '{"reference": "CTR-001", "pdf": null}'

# Étape 2 : suppression de l'entité (autorisée)
curl -X DELETE http://localhost:8080/contracts/550e8400-...
```

---

## 9. Partage d'un fichier entre plusieurs entités

Plusieurs entités peuvent référencer le même fichier physique. Les cas d'usage typiques incluent :

- Avatar partagé entre plusieurs comptes utilisateurs
- Image utilisée dans plusieurs articles
- Modèle de document réutilisé par plusieurs entités

### Mise en œuvre du partage

```bash
# 1. Upload initial du fichier
RESPONSE=$(curl -s -X POST http://localhost:8080/users \
  -F "email=alice@example.com" \
  -F "avatar=@./shared.png;type=image/png")
SHARED_UUID=$(echo $RESPONSE | jq -r '.avatar')

# 2. Référencement du même fichier par une autre entité
curl -X POST http://localhost:8080/users \
  -H "Content-Type: application/json" \
  -d "{
    \"email\": \"bob@example.com\",
    \"avatar\": \"$SHARED_UUID\"
  }"
```

### Mécanisme de comptage des références

Le système maintient un compteur interne représentant le nombre d'entités référençant chaque fichier.

- Chaque création de référence incrémente le compteur
- Chaque suppression de référence décrémente le compteur
- Le fichier physique n'est supprimé que lorsque le compteur atteint zéro et que la stratégie applicable est `cascade`

### Illustration

```bash
# Création de l'entité d'Alice avec le fichier      → compteur = 1
curl -X POST .../users -F email=alice@... -F avatar=@photo.png

# Création de l'entité de Bob avec le même fichier  → compteur = 2
curl -X POST .../users -d '{"email":"bob@...", "avatar":"<UUID>"}'

# Suppression de l'entité d'Alice (cascade)         → compteur = 1
# Le fichier est conservé sur disque (encore référencé par Bob)
curl -X DELETE .../users/<alice_id>

# Suppression de l'entité de Bob (cascade)          → compteur = 0
# Le fichier est supprimé du disque
curl -X DELETE .../users/<bob_id>
```

Ce mécanisme garantit qu'aucune entité ne peut provoquer la perte d'un fichier partagé par suppression isolée.

---

## 10. Codes de réponse et gestion des erreurs

### Opération d'upload

| Code | Description |
|---|---|
| 201 Created | Création réalisée avec succès |
| 400 Bad Request | Fichier excédant la taille maximale, type MIME non autorisé, extension non autorisée, JSON invalide, champ obligatoire manquant |
| 500 Internal Server Error | Échec d'écriture disque, échec d'insertion en base de données |

Exemples de messages d'erreur 400 :

```json
{"error": "Fichier trop volumineux (10485760 bytes ; max 5242880)."}
{"error": "Type MIME refuse: 'application/pdf'."}
{"error": "Extension refusee: '.exe'."}
```

### Opération de download

| Code | Description |
|---|---|
| 200 OK | Contenu binaire retourné |
| 400 Bad Request | Paramètre `id` manquant dans l'URL |
| 403 Forbidden | Politique d'accès refusant la lecture de l'entité parente (lorsque l'authentification est activée) |
| 404 Not Found | Entité inconnue, enregistrement introuvable, champ vide, fichier physique absent du disque |
| 500 Internal Server Error | Erreur d'entrée/sortie |

### Opération de suppression

| Code | Description |
|---|---|
| 200 OK | Suppression réalisée |
| 404 Not Found | Enregistrement introuvable |
| 409 Conflict | Stratégie `on_delete: restrict` empêche la suppression. Un fichier reste attaché. |

---

## 11. Exemples de configurations courantes

### Configuration 1 — Avatar de profil utilisateur

```yaml
- name: User
  fields:
    - name: id
      type: uuid
    - name: email
      type: email
      unique: true
    - name: avatar
      type: file
      required: false
      file:
        max_size: 2MB
        allowed_mime_types: [image/png, image/jpeg, image/webp]
        storage_path: users/avatars
        on_delete: cascade
```

Routes générées :
- `POST /users` — création avec avatar optionnel
- `PUT /users/{id}` — modification avec remplacement de l'avatar
- `DELETE /users/{id}` — suppression avec retrait de l'avatar
- `GET /users/avatar/{id}` — téléchargement de l'avatar

### Configuration 2 — Illustration d'article avec archivage

```yaml
- name: Article
  fields:
    - name: id
      type: uuid
    - name: title
      type: string
    - name: banner
      type: file
      required: false
      file:
        max_size: 10MB
        allowed_mime_types: [image/png, image/jpeg]
        storage_path: articles/banners
        on_delete: set_null
```

La stratégie `set_null` permet la conservation de l'image en cas de suppression de l'article, à des fins d'archivage ou de réutilisation ultérieure.

### Configuration 3 — Document juridique protégé

```yaml
- name: Contract
  fields:
    - name: id
      type: uuid
    - name: reference
      type: string
      unique: true
    - name: pdf
      type: file
      required: true
      file:
        max_size: 20MB
        allowed_mime_types: [application/pdf]
        allowed_extensions: [.pdf]
        storage_path: contracts
        on_delete: restrict
```

La stratégie `restrict` interdit la suppression du contrat tant qu'un document est attaché, prévenant ainsi toute perte accidentelle.

### Configuration 4 — Entité comportant plusieurs champs fichiers

```yaml
- name: Product
  fields:
    - name: id
      type: uuid
    - name: name
      type: string
    - name: thumbnail
      type: file
      file:
        max_size: 500KB
        allowed_mime_types: [image/png, image/jpeg]
        storage_path: products/thumbs
        on_delete: cascade
    - name: detail_image
      type: file
      file:
        max_size: 5MB
        allowed_mime_types: [image/png, image/jpeg]
        storage_path: products/details
        on_delete: cascade
    - name: spec_sheet
      type: file
      file:
        max_size: 10MB
        allowed_mime_types: [application/pdf]
        storage_path: products/specs
        on_delete: set_null
```

Trois routes de téléchargement distinctes sont générées :
- `GET /products/thumbnail/{id}`
- `GET /products/detail_image/{id}`
- `GET /products/spec_sheet/{id}`

---

## Synthèse

La mise en œuvre d'un champ de type `file` requiert les étapes suivantes :

1. **Déclaration du champ** : type `file` accompagné d'un sous-bloc `file:` comportant au minimum les clés `storage_path` et `on_delete`.
2. **Configuration du stockage** (facultative) : déclaration d'un bloc `storage:` au niveau service pour personnaliser le répertoire racine et les permissions Unix.
3. **Choix de la stratégie de suppression** : `cascade` pour un cycle de vie lié à l'entité, `set_null` pour une conservation systématique, `restrict` pour une protection contre la suppression accidentelle.
4. **Émission des requêtes d'upload** : trois modalités au choix selon les contraintes du client (multipart, JSON avec base64, référence UUID).
5. **Téléchargement** : utilisation des routes générées automatiquement selon le format `/<entité>s/<champ>/{id}`.
6. **Gestion du cycle de vie** : les opérations de modification et de suppression appliquent automatiquement la stratégie `on_delete` configurée.
7. **Partage entre entités** : possible par référencement direct d'un identifiant existant ; le système gère le comptage des références de manière transparente.
