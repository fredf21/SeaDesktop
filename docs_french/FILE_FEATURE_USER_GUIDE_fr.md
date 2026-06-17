# SeaDesktop — Type de champ `file`

SeaDesktop fournit un type de champ `file` natif qui permet aux
entités de stocker, gérer et servir des fichiers binaires via les
endpoints CRUD générés automatiquement. Ce guide couvre la
déclaration des champs, la configuration du stockage, le cycle de
vie des fichiers, le téléversement et le téléchargement, le partage
entre entités, et le déploiement.

## Table des matières

1. Déclaration minimale
2. Configuration du stockage au niveau service
3. Configuration d'un champ `file`
4. Stratégies de suppression (`on_delete`)
5. Téléversement d'un fichier
6. Téléchargement d'un fichier
7. Mise à jour et détachement d'un fichier
8. Suppression d'une entité qui référence un fichier
9. Partage d'un fichier entre plusieurs entités
10. Codes de réponse et gestion des erreurs
11. Exemples de configuration courants
12. Déploiement avec Docker

---

## 1. Déclaration minimale

La configuration la plus simple pour activer la gestion des fichiers
sur une entité est montrée ci-dessous :

```yaml
project:
  name: ApplicationDocument

services:
  - name: MainService
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

Au démarrage du service, le système effectue automatiquement les
actions suivantes :

- Crée la table `Photo` dans la base de données
- Crée la table système `sea_files`, qui stocke les métadonnées des fichiers
- Crée le dossier `./uploads/photos/` sur le disque
- Génère les routes HTTP : `POST /photos`, `GET /photos/image/{id}`, `PUT /photos/{id}`, `DELETE /photos/{id}`

Déclarer le bloc `storage:` au niveau service est optionnel. S'il est
absent, le système applique les valeurs par défaut, dont le dossier
racine `./uploads`.

---

## 2. Configuration du stockage au niveau service

Pour personnaliser l'emplacement des fichiers et les permissions,
déclarez un bloc `storage:` au niveau service.

```yaml
services:
  - name: MainService
    storage:
      backend: filesystem
      root_path: /var/lib/application/uploads
      file_mode: "0640"
      directory_mode: "0750"
```

| Clé | Valeur attendue | Valeur par défaut | Description |
|---|---|---|---|
| `backend` | `filesystem` | `filesystem` | Type de backend de stockage. Actuellement, seul `filesystem` est supporté. |
| `root_path` | chemin | `./uploads` | Dossier racine sous lequel tous les fichiers sont organisés. |
| `file_mode` | chaîne octale | `"0640"` | Permissions Unix appliquées aux fichiers créés. |
| `directory_mode` | chaîne octale | `"0750"` | Permissions Unix appliquées aux dossiers créés. |

### Note sur les modes octaux

Les modes Unix doivent être déclarés comme des chaînes, par exemple
`"0640"`, et non comme des entiers comme `0640`. YAML peut
interpréter `0640` comme la valeur décimale `640`, qui ne
représente pas des permissions Unix valides. Mettre la valeur entre
guillemets force une interprétation littérale, que le parseur
convertit ensuite depuis la base 8.

### Comportement lorsque le bloc est omis

Lorsqu'une entité du schéma déclare un champ de type `file` mais que
le bloc `storage:` au niveau service est absent, le système applique
les valeurs de fallback suivantes :

- `backend` : `filesystem`
- `root_path` : `./uploads`

Un avertissement est journalisé au démarrage pour indiquer que le
fallback est utilisé. Une configuration explicite est recommandée
dans les environnements de production.

---

## 3. Configuration d'un champ `file`

Le bloc `file:` permet de configurer finement le comportement d'un
champ de type `file`.

```yaml
- name: pdf
  type: file
  file:
    storage_path: documents/contracts
    on_delete: restrict
    max_size: 5MB
    allowed_mime_types:
      - application/pdf
      - application/x-pdf
    allowed_extensions:
      - .pdf
```

| Clé | Type | Requis | Description |
|---|---|---|---|
| `storage_path` | chaîne | Oui | Sous-chemin relatif au `root_path` configuré. |
| `on_delete` | enum | Oui | Stratégie de suppression. Valeurs : `cascade`, `set_null`, `restrict`. |
| `max_size` | chaîne taille | Non | Taille maximale acceptée. Formats supportés : `500KB`, `5MB`, `1GB`. Si cette contrainte est omise, seule la limite globale du serveur s'applique. |
| `allowed_mime_types` | liste de chaînes | Non | Liste blanche des types MIME acceptés. Si absente ou vide, tous les types MIME sont acceptés. |
| `allowed_extensions` | liste de chaînes | Non | Liste blanche des extensions acceptées, point inclus. La comparaison est insensible à la casse. Si absente, toutes les extensions sont acceptées. |

### Contraintes sur `storage_path`

Le `storage_path` doit :

- Être un chemin relatif (pas de slash initial)
- Ne pas contenir de séquences `..` (protection path-traversal)
- Ne pas contenir de caractères invalides pour le filesystem cible
- Être unique parmi les champs de l'entité (deux champs ne peuvent
  pas pointer vers le même sous-dossier)

### Formats de taille acceptés

| Format | Valeur |
|---|---|
| `500B` ou `500` | 500 octets |
| `500KB` | 500 × 1024 octets |
| `5MB` | 5 × 1024 × 1024 octets |
| `1GB` | 1 × 1024 × 1024 × 1024 octets |

La casse est ignorée. `5mb`, `5MB`, `5Mb` sont équivalents.

---

## 4. Stratégies de suppression (`on_delete`)

Trois stratégies sont disponibles pour contrôler le sort d'un fichier
quand l'entité qui le référence est supprimée.

### Stratégie `cascade`

Le fichier est supprimé du disque en même temps que l'entité. Si le
fichier est partagé avec d'autres entités (voir section 9), il n'est
supprimé que lorsque la dernière référence disparaît.

```yaml
- name: thumbnail
  type: file
  file:
    storage_path: thumbnails
    on_delete: cascade
```

Cas d'usage typiques : pièces jointes temporaires, miniatures,
fichiers personnels d'utilisateur sans valeur archivistique.

### Stratégie `set_null`

Le fichier est conservé sur disque, mais sa référence dans l'entité
est mise à `null` lorsque l'entité est supprimée. Utile pour
préserver des artefacts qui doivent survivre à leur entité d'origine.

```yaml
- name: invoice
  type: file
  file:
    storage_path: invoices
    on_delete: set_null
```

Cas d'usage typiques : facturation, archives légales,
journaux d'audit.

### Stratégie `restrict`

La suppression de l'entité est refusée tant qu'un fichier est
attaché. Le client doit détacher ou remplacer le fichier avant de
supprimer l'entité.

```yaml
- name: contract_pdf
  type: file
  file:
    storage_path: contracts
    on_delete: restrict
```

Cas d'usage typiques : documents juridiques critiques, contrats
signés, fichiers dont la perte serait irrémédiable.

---

## 5. Téléversement d'un fichier

Trois modes de téléversement sont supportés par les endpoints
générés. Choisissez celui qui convient à votre client.

### Mode 1 — Requête `multipart/form-data`

Le mode le plus courant pour les téléversements web.

```bash
curl -X POST http://localhost:8080/photos \
  -H "Authorization: Bearer ${TOKEN}" \
  -F 'image=@my_picture.jpg' \
  -F 'description=Vacation photo'
```

Le serveur extrait le fichier et les champs textuels du body
multipart, valide les contraintes (taille, MIME, extension), stocke
le fichier sur disque, crée une entrée dans `sea_files` et crée
l'entité avec une référence vers cette entrée.

### Mode 2 — Requête JSON avec contenu Base64

Utile pour les clients qui préfèrent un format JSON uniforme.

```bash
curl -X POST http://localhost:8080/photos \
  -H "Authorization: Bearer ${TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{
    "image": {
      "filename":  "picture.png",
      "mime_type": "image/png",
      "content":   "iVBORw0KGgoAAAANSUhEUgAA..."
    },
    "description": "Vacation photo"
  }'
```

Le contenu Base64 est décodé côté serveur avant validation et
stockage. Cette approche convient aux fichiers de petite ou moyenne
taille ; pour les fichiers volumineux, le mode multipart est plus
efficace.

### Mode 3 — Référence vers un fichier existant par UUID

Pour partager un fichier déjà téléversé entre plusieurs entités.

```bash
curl -X POST http://localhost:8080/photos \
  -H "Authorization: Bearer ${TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{
    "image":       "550e8400-e29b-41d4-a716-446655440000",
    "description": "Same photo, different album"
  }'
```

L'UUID référencé doit correspondre à une entrée existante dans
`sea_files`. Voir la section 9 pour la mécanique de comptage de
références.

---

## 6. Téléchargement d'un fichier

Une route `GET /<entités>/<champ>/{id}` est générée automatiquement
pour chaque champ de type `file`.

```bash
curl -O http://localhost:8080/photos/image/550e8400-e29b-41d4-a716-446655440000 \
  -H "Authorization: Bearer ${TOKEN}"
```

Le serveur retourne le fichier avec les en-têtes appropriés :

- `Content-Type` : type MIME stocké
- `Content-Length` : taille du fichier
- `Content-Disposition` : `inline; filename="picture.png"` (le nom
  d'origine est préservé)

L'authentification et l'autorisation s'appliquent comme pour toute
route générée. Un fichier ne peut être téléchargé que par les
utilisateurs autorisés à lire l'entité parente.

---

## 7. Mise à jour et détachement d'un fichier

### Remplacement d'un fichier

Pour remplacer un fichier attaché à une entité, faites un `PUT` avec
un nouveau fichier (n'importe lequel des trois modes de la section 5).

```bash
curl -X PUT http://localhost:8080/photos/123 \
  -H "Authorization: Bearer ${TOKEN}" \
  -F 'image=@new_picture.jpg'
```

L'ancien fichier est traité selon la stratégie `on_delete`
configurée : si `cascade`, il est supprimé du disque (sauf s'il est
partagé avec d'autres entités). Si `set_null` ou `restrict`,
l'ancien fichier est conservé et son lien est rompu.

### Détachement d'un fichier

Pour retirer le fichier sans le remplacer, faites un `PUT` avec la
valeur explicite `null` :

```bash
curl -X PUT http://localhost:8080/photos/123 \
  -H "Authorization: Bearer ${TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{"image": null}'
```

- En `cascade` : le fichier est supprimé du disque (sauf partage).
- En `set_null` : la mise à jour détachant le fichier en assignant `null` est autorisée.
- En `restrict` : le détachement est autorisé (c'est même le
  préalable à la suppression de l'entité, voir section 8).

---

## 8. Suppression d'une entité qui référence un fichier

Le comportement dépend de la stratégie `on_delete` configurée sur le
champ.

| Stratégie | Comportement à la suppression |
|---|---|
| `cascade` | Le fichier est supprimé du disque (sauf partage), puis l'entité. |
| `set_null` | Sans effet : la suppression de l'entité fait disparaître la référence, mais l'entité n'existe plus pour porter le `null`. Le fichier reste sur disque. |
| `restrict` | La suppression de l'entité est refusée tant qu'un fichier est attaché. |

### Procédure de suppression avec `restrict`

```bash
# Tentative directe : refusée
curl -X DELETE http://localhost:8080/contracts/456 \
  -H "Authorization: Bearer ${TOKEN}"

# Réponse : 409 Conflict
# {
#   "error": "Conflict",
#   "message": "The entity 'Contract' cannot be deleted:
#               file field 'pdf' has on_delete=restrict.
#               Detach or replace the file before deletion."
# }

# Étape 1 : détacher le fichier
curl -X PUT http://localhost:8080/contracts/456 \
  -H "Authorization: Bearer ${TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{"pdf": null}'

# Étape 2 : supprimer l'entité, maintenant autorisée
curl -X DELETE http://localhost:8080/contracts/456 \
  -H "Authorization: Bearer ${TOKEN}"
```

---

## 9. Partage d'un fichier entre plusieurs entités

Un même fichier physique peut être référencé par plusieurs entités
sans duplication sur disque. Le système utilise un compteur de
références pour gérer le cycle de vie.

### Implémentation du partage

```bash
# 1. Téléversement initial du fichier
curl -X POST http://localhost:8080/photos \
  -H "Authorization: Bearer ${TOKEN}" \
  -F 'image=@picture.jpg' \
  -F 'description=Original'

# Réponse : { "id": "abc-123", "image": "550e8400-..." }

# 2. Référencer le même fichier depuis une autre entité
curl -X POST http://localhost:8080/photos \
  -H "Authorization: Bearer ${TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{
    "image":       "550e8400-...",
    "description": "Same photo, different entry"
  }'
```

### Mécanisme de comptage de références

Chaque entrée de `sea_files` porte un compteur `reference_count`. Il
est incrémenté à chaque nouvelle référence créée (entité créée ou
mise à jour pointant vers ce fichier) et décrémenté quand une
référence disparaît (entité supprimée en `cascade`, ou champ détaché).

Le fichier physique n'est supprimé du disque que lorsque le
compteur atteint zéro.

### Illustration

```
# Créer l'entité d'Alice avec le fichier      → compteur = 1
# Créer l'entité de Bob avec le même fichier  → compteur = 2
# Supprimer l'entité d'Alice (cascade)        → compteur = 1
# Le fichier reste sur disque car Bob le référence encore
# Supprimer l'entité de Bob (cascade)         → compteur = 0
# Le fichier est supprimé du disque
```

---

## 10. Codes de réponse et gestion des erreurs

### Opération de téléversement

| Code | Cause |
|---|---|
| `201 Created` | Téléversement réussi. La réponse contient l'entité créée avec l'UUID du fichier. |
| `400 Bad Request` | Fichier dépassant la taille maximale, type MIME non autorisé, extension non autorisée, JSON invalide, ou champ requis manquant. |
| `401 Unauthorized` | Token manquant ou invalide. |
| `403 Forbidden` | Utilisateur authentifié mais non autorisé à créer cette entité. |
| `413 Payload Too Large` | La requête dépasse la limite globale du serveur. |

### Opération de téléchargement

| Code | Cause |
|---|---|
| `200 OK` | Téléchargement réussi. |
| `401 Unauthorized` | Token manquant ou invalide. |
| `403 Forbidden` | Utilisateur authentifié mais non autorisé à lire cette entité. |
| `404 Not Found` | UUID inexistant, ou entité parente inexistante. |

### Opération de suppression

| Code | Cause |
|---|---|
| `200 OK` | Suppression réussie. |
| `401 Unauthorized` | Token manquant ou invalide. |
| `403 Forbidden` | Utilisateur authentifié mais non autorisé à supprimer cette entité. |
| `404 Not Found` | Entité inexistante. |
| `409 Conflict` | Stratégie `restrict` : un fichier est encore attaché. |

---

## 11. Exemples de configuration courants

### Configuration 1 — Avatar de profil utilisateur

Un avatar par utilisateur, supprimé avec l'utilisateur.

```yaml
- name: User
  options:
    enable_crud: true
    is_auth_source: true
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
    - name: avatar
      type: file
      file:
        storage_path: avatars
        on_delete: cascade
        max_size: 2MB
        allowed_mime_types:
          - image/jpeg
          - image/png
          - image/webp
        allowed_extensions:
          - .jpg
          - .jpeg
          - .png
          - .webp
```

### Configuration 2 — Illustration d'article avec archivage

L'illustration survit à la suppression de l'article.

```yaml
- name: Article
  options:
    enable_crud: true
  fields:
    - name: id
      type: uuid
    - name: title
      type: string
    - name: cover_image
      type: file
      file:
        storage_path: articles/covers
        on_delete: set_null
        max_size: 10MB
        allowed_mime_types:
          - image/jpeg
          - image/png
```

### Configuration 3 — Document juridique protégé

Le PDF doit être détaché manuellement avant suppression du contrat.

```yaml
- name: Contract
  options:
    enable_crud: true
  fields:
    - name: id
      type: uuid
    - name: signed_at
      type: timestamp
    - name: pdf
      type: file
      file:
        storage_path: contracts/signed
        on_delete: restrict
        max_size: 20MB
        allowed_mime_types:
          - application/pdf
        allowed_extensions:
          - .pdf
```

### Configuration 4 — Entité avec plusieurs champs de fichier

Plusieurs champs de fichier sur la même entité, chacun avec sa
propre stratégie.

```yaml
- name: Product
  options:
    enable_crud: true
  fields:
    - name: id
      type: uuid
    - name: name
      type: string
    - name: main_photo
      type: file
      file:
        storage_path: products/main
        on_delete: cascade
        max_size: 5MB
    - name: technical_sheet
      type: file
      file:
        storage_path: products/datasheets
        on_delete: set_null
        max_size: 10MB
        allowed_mime_types:
          - application/pdf
```

---

## 12. Déploiement avec Docker

Lorsque SeaDesktop tourne dans un container Docker, le dossier
`uploads/` (ou ce vers quoi pointe `storage.root_path`) se trouve à
l'intérieur du filesystem du container. Sans volume, tous les
fichiers téléversés sont perdus lorsque le container est redémarré
ou remplacé.

Pour un stockage persistant, montez le dossier uploads en tant que
volume Docker. Dans `docker-compose.yml` :

```yaml
services:
  service_a:
    image: seadesktop/backend:latest
    volumes:
      - ${SEA_DESKTOP_CONFIGS_HOST_DIR:-./configs}:/app/configs
      - ./uploads/service_a:/app/uploads
```

Le même volume `uploads/` peut être partagé entre plusieurs
containers backend si votre déploiement l'exige, mais soyez
conscient que les suppressions de fichiers ne sont plus détectées
comme doublons entre services puisque chaque service écrit dans sa
propre table `sea_files`. Pour un déploiement simple, un volume
uploads par service est le défaut sûr.

Assurez-vous que le dossier hôte a les bonnes permissions pour
l'UID qui exécute le container (1000 par défaut) :

```bash
mkdir -p uploads/service_a
chown 1000:1000 uploads/service_a
```

Voir `docker_deployment.md` pour le guide complet de déploiement
Docker.

---

## Récapitulatif

L'implémentation d'un champ `file` requiert les étapes suivantes :

1. **Déclarer le champ** : utiliser `type: file` avec un sous-bloc
   `file:` contenant au minimum `storage_path` et `on_delete`.
2. **Configurer le stockage** (optionnel) : déclarer un bloc
   `storage:` au niveau service pour personnaliser le dossier
   racine et les permissions Unix.
3. **Choisir une stratégie de suppression** : `cascade` pour un
   cycle de vie lié à l'entité, `set_null` pour une préservation
   systématique, ou `restrict` pour empêcher les suppressions
   accidentelles.
4. **Téléverser les fichiers** : choisir l'un des trois modes selon
   les contraintes du client : multipart, JSON avec base64, ou
   référence par UUID existant.
5. **Télécharger les fichiers** : utiliser les routes générées
   automatiquement suivant le format `/<entités>/<champ>/{id}`.
6. **Gérer le cycle de vie** : les opérations de mise à jour et de
   suppression appliquent automatiquement la stratégie `on_delete`
   configurée.
7. **Partager des fichiers entre entités** : référencer directement
   un identifiant de fichier existant ; le système gère de manière
   transparente le comptage de références.
