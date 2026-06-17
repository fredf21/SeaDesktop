# SeaDesktop — Pagination

Documentation de référence pour la pagination des routes CRUD générées par SeaDesktop. Ce document décrit chaque clé YAML supportée, ses valeurs autorisées, son comportement par défaut, ainsi que la réaction attendue du système. Toutes les informations proviennent du code source du projet.

---

## Sommaire

1. [Principe général](#1-principe-général)
2. [Activation de la pagination](#2-activation-de-la-pagination)
3. [Routes générées](#3-routes-générées)
4. [Mode `page`](#4-mode-page)
5. [Mode `offset`](#5-mode-offset)
6. [Mode `cursor`](#6-mode-cursor)
7. [Tri des résultats](#7-tri-des-résultats)
8. [Liste blanche des champs triables](#8-liste-blanche-des-champs-triables)
9. [Combinaison des trois modes](#9-combinaison-des-trois-modes)
10. [Comportement en cas d'erreur](#10-comportement-en-cas-derreur)
11. [Comparaison des trois modes](#11-comparaison-des-trois-modes)
12. [Exemples de configurations](#12-exemples-de-configurations)

---

## 1. Principe général

Par défaut, la route `GET /<entité>` retourne tous les enregistrements d'une entité. Pour les tables volumineuses, ce comportement devient inefficace. La pagination permet de fragmenter les résultats en plusieurs requêtes.

SeaDesktop propose trois modes de pagination indépendants :

- **`page`** : pagination par numéro de page et taille de page
- **`offset`** : pagination par décalage et limite
- **`cursor`** : pagination par curseur opaque

Les trois modes peuvent être activés simultanément sur une même entité. Chaque mode produit alors sa propre route distincte, avec son propre format de réponse.

La route `GET /<entité>` (sans suffixe) reste toujours accessible et retourne l'intégralité des enregistrements, quels que soient les modes activés.

---

## 2. Activation de la pagination

La pagination s'active dans le bloc `pagination:` d'une entité. Au moins l'un des trois sous-blocs (`page`, `offset`, `cursor`) doit être présent.

### Structure générale

```yaml
entities:
  - name: Product
    fields: [ ... ]
    pagination:
      page:    { ... }
      offset:  { ... }
      cursor:  { ... }
```

### Comportement en cas d'absence du bloc

Si le bloc `pagination:` est absent d'une entité, aucune route paginée n'est générée. Seule la route `GET /<entité>` standard est exposée.

### Comportement si `pagination:` est présent mais vide

Si la clé `pagination:` est déclarée sans contenir au moins un des trois sous-blocs, le démarrage du service échoue avec l'erreur suivante :

```
[YAML PARSING EXCEPTION] 'pagination' de l'entite '<nom>' doit contenir
au moins un mode parmi 'page', 'offset' ou 'cursor'.
```

---

## 3. Routes générées

Pour chaque mode activé, une route distincte est créée :

| Mode activé | Route générée |
|---|---|
| `page` | `GET /<entité>/page` |
| `offset` | `GET /<entité>/offset` |
| `cursor` | `GET /<entité>/cursor` |

Le segment `<entité>` correspond au nom de l'entité au pluriel en minuscules. Par exemple, une entité `Product` produit les routes `/products/page`, `/products/offset`, `/products/cursor`.

La route `GET /<entité>` reste accessible et retourne tous les enregistrements. Les routes paginées sont des routes additionnelles, jamais des remplacements.

---

## 4. Mode `page`

Le mode `page` fournit une pagination classique avec numéros de page et taille de page. C'est le mode adapté aux interfaces affichant des liens de navigation entre pages.

### Configuration

```yaml
pagination:
  page:
    default_page_size: 20
    max_page_size: 100
    default_sort: "created_at:desc"
    sortable_fields:
      - created_at
      - name
      - price
```

### Clés acceptées

| Clé | Type | Valeur par défaut | Description |
|---|---|---|---|
| `default_page_size` | entier | `20` | Taille de page appliquée si le client n'en précise pas dans la requête. |
| `max_page_size` | entier | `100` | Taille maximale autorisée. Une valeur client supérieure est plafonnée à cette borne. |
| `default_sort` | chaîne | absent | Tri par défaut. Format : `"<champ>:<direction>"` où direction vaut `asc` ou `desc`. |
| `sortable_fields` | liste de chaînes | `[]` | Liste blanche des champs autorisés au tri. Voir [section 8](#8-liste-blanche-des-champs-triables). |

### Route et paramètres

```
GET /<entité>/page
```

| Query parameter | Type | Description |
|---|---|---|
| `page` | entier | Numéro de page. L'index commence à 1. |
| `page_size` | entier | Taille de la page. Plafonné à `max_page_size`. |
| `sort` | chaîne | Tri à appliquer. Format `"<champ>:<direction>"`. |

### Format de la réponse

```json
{
  "items": [
    { "id": "...", "name": "...", ... }
  ],
  "page": 2,
  "page_size": 20,
  "total": 532,
  "total_pages": 27,
  "sort": "created_at:desc"
}
```

| Clé | Description |
|---|---|
| `items` | Enregistrements de la page demandée. |
| `page` | Numéro de page courant. |
| `page_size` | Taille de page effective (après plafonnement éventuel). |
| `total` | Nombre total d'enregistrements dans la table. |
| `total_pages` | Nombre total de pages disponibles. |
| `sort` | Tri appliqué. |

### Exemple

```bash
curl "http://localhost:8081/products/page?page=2&page_size=20&sort=name:asc"
```

### Comportement attendu

1. Validation des paramètres : `page` ≥ 1, `page_size` ≥ 1.
2. Si `page_size` est absent, `default_page_size` est utilisée.
3. Si `page_size` dépasse `max_page_size`, la valeur est plafonnée silencieusement.
4. Si `sort` est absent et `default_sort` est défini, le tri par défaut est appliqué.
5. Si `sort` est fourni, son champ doit appartenir à `sortable_fields`, sinon code 400.
6. Une requête `SELECT COUNT(*)` est exécutée pour calculer `total`.
7. Une requête `SELECT ... ORDER BY ... LIMIT page_size OFFSET ((page-1) * page_size)` retourne les enregistrements.

---

## 5. Mode `offset`

Le mode `offset` fournit une pagination par décalage arbitraire. C'est le mode le plus proche du SQL standard, adapté aux outils d'administration et aux exports.

### Configuration

```yaml
pagination:
  offset:
    default_limit: 20
    max_limit: 100
    default_sort: "id:asc"
    sortable_fields:
      - id
      - name
      - created_at
```

### Clés acceptées

| Clé | Type | Valeur par défaut | Description |
|---|---|---|---|
| `default_limit` | entier | `20` | Limite appliquée si le client n'en précise pas. |
| `max_limit` | entier | `100` | Limite maximale autorisée. |
| `default_sort` | chaîne | absent | Tri par défaut. |
| `sortable_fields` | liste de chaînes | `[]` | Liste blanche des champs triables. |

### Route et paramètres

```
GET /<entité>/offset
```

| Query parameter | Type | Description |
|---|---|---|
| `offset` | entier | Nombre d'enregistrements à sauter avant de commencer. |
| `limit` | entier | Nombre maximal d'enregistrements à retourner. |
| `sort` | chaîne | Tri à appliquer. |

### Format de la réponse

```json
{
  "items": [
    { "id": "...", "name": "...", ... }
  ],
  "offset": 40,
  "limit": 20,
  "total": 532,
  "sort": "created_at:desc"
}
```

### Exemple

```bash
curl "http://localhost:8081/products/offset?offset=40&limit=20&sort=created_at:desc"
```

### Comportement attendu

1. Validation des paramètres : `offset` ≥ 0, `limit` ≥ 1.
2. Si `limit` est absent, `default_limit` est utilisée.
3. Si `limit` dépasse `max_limit`, la valeur est plafonnée silencieusement.
4. Si `offset` est absent, `0` est utilisé.
5. Requête SQL : `SELECT ... ORDER BY <sort> LIMIT <limit> OFFSET <offset>`.

---

## 6. Mode `cursor`

Le mode `cursor` fournit une pagination par curseur opaque. Les performances restent constantes quelle que soit la profondeur de pagination. Ce mode est adapté aux listes temps réel, aux flux infinis et aux applications mobiles.

### Configuration

```yaml
pagination:
  cursor:
    default_limit: 20
    max_limit: 100
    cursor_field: id
    sort: "id:asc"
```

### Clés acceptées

| Clé | Type | Obligatoire | Valeur par défaut | Description |
|---|---|---|---|---|
| `default_limit` | entier | Non | `20` | Limite appliquée si le client n'en précise pas. |
| `max_limit` | entier | Non | `100` | Limite maximale autorisée. |
| `cursor_field` | chaîne | **Oui** | — | Champ utilisé comme curseur. Doit être unique et triable de façon stable. |
| `sort` | chaîne | **Oui** | — | Tri figé du curseur. Format : `"<champ>:<direction>"`. Le champ doit correspondre à `cursor_field`. |

### Route et paramètres

```
GET /<entité>/cursor
```

| Query parameter | Type | Description |
|---|---|---|
| `after` | chaîne | Curseur opaque retourné par la requête précédente. Si absent, la première page est retournée. |
| `limit` | entier | Nombre maximal d'enregistrements à retourner. |

Le tri n'est **pas** paramétrable côté client : il est figé par la valeur de `sort` dans la configuration. Tout paramètre `sort` envoyé par le client est ignoré.

### Format de la réponse

```json
{
  "items": [
    { "id": "...", "name": "...", ... }
  ],
  "limit": 20,
  "next_cursor": "eyJpZCI6IjU1MGU4LS4uLiJ9",
  "prev_cursor": null
}
```

| Clé | Description |
|---|---|
| `items` | Enregistrements retournés. |
| `limit` | Limite effective. |
| `next_cursor` | Curseur à utiliser pour récupérer la page suivante. `null` si la fin est atteinte. |
| `prev_cursor` | Curseur de la page précédente. `null` si on est en première page. |

### Exemple de séquence de requêtes

**Première page :**

```bash
curl "http://localhost:8081/products/cursor?limit=20"
```

Réponse :

```json
{
  "items": [ ... 20 produits ... ],
  "limit": 20,
  "next_cursor": "eyJpZCI6IjU1MGU4LS4uLiJ9",
  "prev_cursor": null
}
```

**Page suivante :**

```bash
curl "http://localhost:8081/products/cursor?after=eyJpZCI6IjU1MGU4LS4uLiJ9&limit=20"
```

### Comportement attendu

1. Le curseur est une chaîne opaque : le client doit le réutiliser tel quel sans modification ni interprétation.
2. Si `after` est absent, la requête démarre du début (premier enregistrement selon `sort`).
3. La requête SQL utilise une condition `WHERE <cursor_field> > <valeur_décodée>` (ou `<` selon la direction de tri).
4. Le tri est figé : tout paramètre `sort` côté client est ignoré.
5. Le champ `cursor_field` doit être unique. Sinon, le curseur peut produire des résultats incohérents.

### Pourquoi le tri est figé

Avec un curseur, le tri ne peut pas être modifié dynamiquement. Si le client demandait à mi-pagination de changer de tri, les valeurs des curseurs précédemment retournés deviendraient invalides : elles encodent une position dans le tri actuel. C'est pourquoi `sort` est défini une seule fois dans la configuration et n'est pas paramétrable côté client.

---

## 7. Tri des résultats

Les modes `page` et `offset` acceptent un paramètre `sort` côté client. Le mode `cursor` ignore ce paramètre.

### Format

Le tri suit le format `"<champ>:<direction>"` :

| Exemple | Effet |
|---|---|
| `created_at:desc` | Tri décroissant par date de création. |
| `name:asc` | Tri croissant par nom. |
| `price:desc` | Tri décroissant par prix. |

### Direction par défaut

Si la direction est omise (`sort=name`), le tri est ascendant (`asc`).

### Combinaison de tris

Le système accepte un seul champ de tri à la fois. Pour des tris multi-champs, utiliser un tri par défaut composite déclaré dans `default_sort`.

---

## 8. Liste blanche des champs triables

Le champ `sortable_fields` est une mesure de sécurité contre l'injection SQL et contre les requêtes coûteuses sur des champs non indexés.

### Comportement

Si un client envoie `sort=<champ>` :

| Cas | Comportement |
|---|---|
| `<champ>` appartient à `sortable_fields` | Le tri est appliqué. |
| `<champ>` n'appartient pas à `sortable_fields` | Code 400 retourné avec un message indiquant que le champ n'est pas triable. |
| `sortable_fields` est vide et `default_sort` est absent | Aucun tri n'est possible. Toute valeur de `sort` est rejetée. |

### Recommandation

Pour chaque entité paginée, déclarer explicitement les champs autorisés au tri. Ces champs devraient idéalement être indexés en base (`indexed: true` au niveau du champ) pour garantir de bonnes performances.

```yaml
entities:
  - name: Order
    fields:
      - name: id
        type: uuid
      - name: created_at
        type: timestamp
        indexed: true
      - name: total
        type: decimal
        indexed: true
      - name: customer_name
        type: string

    pagination:
      page:
        default_page_size: 20
        max_page_size: 100
        default_sort: "created_at:desc"
        sortable_fields:
          - created_at
          - total
        # customer_name volontairement absent : pas d'index, tri trop coûteux
```

---

## 9. Combinaison des trois modes

Les trois modes peuvent coexister sur une même entité. Chacun produit alors sa propre route avec son propre format de réponse.

### Exemple

```yaml
- name: Article
  pagination:
    page:
      default_page_size: 10
      max_page_size: 50
      default_sort: "published_at:desc"
      sortable_fields: [published_at, title]
    offset:
      default_limit: 10
      max_limit: 100
      default_sort: "id:asc"
      sortable_fields: [id, published_at]
    cursor:
      default_limit: 20
      max_limit: 100
      cursor_field: id
      sort: "id:asc"
```

Routes exposées simultanément :

| Route | Mode |
|---|---|
| `GET /articles` | Sans pagination, retourne tout. |
| `GET /articles/page` | Pagination par page. |
| `GET /articles/offset` | Pagination par offset. |
| `GET /articles/cursor` | Pagination par curseur. |

Le client choisit le mode adapté à son cas d'usage.

---

## 10. Comportement en cas d'erreur

### Validation des paramètres

| Erreur | Code retourné | Cause |
|---|---|---|
| `page < 1` | 400 | Numéro de page invalide. |
| `page_size < 1` ou `limit < 1` | 400 | Taille de page invalide. |
| `offset < 0` | 400 | Décalage invalide. |
| `sort=champ` avec champ non listé dans `sortable_fields` | 400 | Champ non autorisé au tri. |
| `sort` avec direction inconnue (autre que `asc` ou `desc`) | 400 | Direction invalide. |
| `after=<curseur invalide>` | 400 | Curseur opaque illisible. |

### Plafonnement automatique

Les valeurs `page_size` ou `limit` supérieures à `max_page_size` / `max_limit` sont **plafonnées sans erreur**. Le système applique silencieusement la valeur maximale et le champ `page_size` ou `limit` dans la réponse reflète la valeur effective.

```bash
# Configuration : max_page_size = 100
# Requête : page_size = 500
curl "http://localhost:8081/products/page?page_size=500"

# Réponse contient : "page_size": 100 (plafonné, pas d'erreur)
```

### Format des erreurs

```json
{
  "error": "Validation failed",
  "details": {
    "sort": "field 'description' is not sortable"
  }
}
```

---

## 11. Comparaison des trois modes

### Tableau récapitulatif

| Critère | `page` | `offset` | `cursor` |
|---|---|---|---|
| Navigation arbitraire (saut à la page 50) | ✓ | ✓ | ✗ |
| Performance constante en profondeur | ✗ | ✗ | ✓ |
| Compteur total disponible | ✓ | ✓ | ✗ |
| Tri dynamique côté client | ✓ | ✓ | ✗ |
| Adapté aux flux temps réel | ✗ | ✗ | ✓ |

### Quand utiliser `page`

- Interfaces utilisateur affichant des liens de navigation (page 1, 2, 3...).
- Volumes modérés (< 100 000 enregistrements) où la dégradation reste acceptable.
- Cas où le compteur total est utile pour afficher « 532 résultats ».

### Quand utiliser `offset`

- Outils d'administration permettant des sauts arbitraires.
- Exports paginés où le client connaît l'offset à partir d'un état applicatif.
- Comportement SQL standard attendu.

### Quand utiliser `cursor`

- Listes infinies en mobile (scroll vers le bas).
- Flux temps réel où les enregistrements sont ajoutés en continu.
- Très gros volumes (> 1 million) où les performances de `page`/`offset` deviennent prohibitives.

---

## 12. Exemples de configurations

### Configuration 1 — Catalogue produit avec pagination par page

```yaml
- name: Product
  fields:
    - name: id
      type: uuid
    - name: name
      type: string
      required: true
      max_length: 200
    - name: price
      type: decimal
    - name: created_at
      type: timestamp
      indexed: true

  pagination:
    page:
      default_page_size: 24
      max_page_size: 96
      default_sort: "created_at:desc"
      sortable_fields:
        - created_at
        - name
        - price
```

### Configuration 2 — Flux temps réel avec curseur

```yaml
- name: Event
  fields:
    - name: id
      type: uuid
    - name: occurred_at
      type: timestamp
      indexed: true
    - name: payload
      type: json

  pagination:
    cursor:
      default_limit: 50
      max_limit: 200
      cursor_field: id
      sort: "id:desc"
```

### Configuration 3 — Tous les modes activés

```yaml
- name: Order
  fields:
    - name: id
      type: uuid
    - name: customer_id
      type: uuid
      indexed: true
    - name: total
      type: decimal
    - name: created_at
      type: timestamp
      indexed: true

  pagination:
    page:
      default_page_size: 25
      max_page_size: 100
      default_sort: "created_at:desc"
      sortable_fields: [created_at, total]

    offset:
      default_limit: 50
      max_limit: 500
      default_sort: "id:asc"
      sortable_fields: [id, created_at, total]

    cursor:
      default_limit: 30
      max_limit: 100
      cursor_field: id
      sort: "id:desc"
```

### Configuration 4 — Pagination minimale

```yaml
- name: Comment
  fields:
    - name: id
      type: uuid
    - name: content
      type: text
    - name: created_at
      type: timestamp
      indexed: true

  pagination:
    page:
      default_sort: "created_at:desc"
      sortable_fields: [created_at]
```

Les autres clés (`default_page_size`, `max_page_size`) prennent leurs valeurs par défaut (`20` et `100`).
