# SeaDesktop — Logging

Documentation de référence pour la configuration du système de logs des services SeaDesktop. Ce document décrit chaque clé YAML supportée, ses valeurs autorisées, son comportement par défaut, ainsi que la réaction attendue du système. Toutes les informations proviennent du code source du projet.

---

## Sommaire

1. [Principe général](#1-principe-général)
2. [Activation du logging](#2-activation-du-logging)
3. [Bloc `logging`](#3-bloc-logging)
4. [Niveau global (`level`)](#4-niveau-global-level)
5. [Niveaux par module (`modules`)](#5-niveaux-par-module-modules)
6. [Sinks (destinations des logs)](#6-sinks-destinations-des-logs)
7. [Sink `console`](#7-sink-console)
8. [Sink `file`](#8-sink-file)
9. [Rotation des fichiers](#9-rotation-des-fichiers)
10. [Format des logs](#10-format-des-logs)
11. [Flush immédiat (`flush_level`)](#11-flush-immédiat-flush_level)
12. [Logging asynchrone (`async`)](#12-logging-asynchrone-async)
13. [Endpoint `/admin/logs`](#13-endpoint-adminlogs)
14. [Endpoint `/admin/logs/loggers`](#14-endpoint-adminlogsloggers)
15. [Format des durées et des tailles](#15-format-des-durées-et-des-tailles)
16. [Exemples de configurations](#16-exemples-de-configurations)

---

## 1. Principe général

Le système de logs permet de tracer l'activité d'un service. Chaque message émis par le code est :

- Étiqueté par un **module** (par exemple `sea.http`, `sea.persistence`, `sea.boot`).
- Classé par **niveau de sévérité** (trace, debug, info, warn, error, critical).
- Envoyé vers un ou plusieurs **sinks** (console, fichier) configurés indépendamment.

Le logging est configuré déclarativement dans le bloc `logging:` du service. Plusieurs sinks peuvent coexister : chaque message est envoyé à tous les sinks actifs simultanément.

Un buffer mémoire interne conserve en permanence les 10 000 derniers messages, exposés via l'endpoint REST `/admin/logs` pour consultation depuis SeaUI ou tout autre client autorisé.

---

## 2. Activation du logging

Le bloc `logging:` est entièrement optionnel.

### Comportement en l'absence du bloc

Si la section `logging:` est absente du YAML, le système applique les valeurs par défaut :

- Niveau global : `info`
- Un sink unique : console, format `text`, actif
- Flush immédiat à partir de `error`
- Mode asynchrone actif avec queue de 8 192 messages et politique `overrun_oldest`

Ces défauts sont sensés pour un démarrage rapide en environnement de développement.

### Activation explicite minimale

```yaml
services:
  - name: MonService
    logging:
      level: info
```

### Désactivation totale

```yaml
logging:
  enabled: false
```

Avec `enabled: false`, aucun message n'est émis sur quelque sink que ce soit. Cette option est utile pour des tests automatisés ou des situations où le bruit des logs doit être éliminé.

---

## 3. Bloc `logging`

### Structure complète

```yaml
logging:
  enabled: true
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
        max_files: 10
        compress: false
  flush_level: error
  async:
    enabled: true
    queue_size: 8192
    overflow_policy: overrun_oldest
```

### Clés acceptées

| Clé | Type | Valeur par défaut | Description |
|---|---|---|---|
| `enabled` | booléen | `true` | Active ou désactive le système de logs. |
| `level` | enum | `info` | Niveau global par défaut. Voir [section 4](#4-niveau-global-level). |
| `modules` | bloc | `{}` | Override du niveau pour des modules spécifiques. Voir [section 5](#5-niveaux-par-module-modules). |
| `sinks` | liste | console texte | Destinations des logs. Voir [section 6](#6-sinks-destinations-des-logs). |
| `flush_level` | enum | `error` | Niveau au-delà duquel les messages sont écrits immédiatement sur disque. Voir [section 11](#11-flush-immédiat-flush_level). |
| `async` | bloc | activé | Configuration de l'écriture asynchrone. Voir [section 12](#12-logging-asynchrone-async). |

### Comportement si aucun sink n'est déclaré

Si la clé `sinks:` est absente, un sink console par défaut (format texte, actif) est ajouté automatiquement.

Si la clé `sinks:` est présente mais que tous les sinks ont `enabled: false`, aucun log n'est écrit physiquement, mais le ring buffer mémoire reste alimenté et l'endpoint `/admin/logs` reste opérationnel.

---

## 4. Niveau global (`level`)

Le niveau global détermine la verbosité par défaut pour tous les modules non explicitement configurés.

### Valeurs acceptées

| Valeur | Sévérité | Description |
|---|---|---|
| `trace` | la plus basse | Trace très détaillée des opérations internes. Désactivé en production. |
| `debug` | basse | Information de diagnostic, variables internes. |
| `info` (défaut) | normale | Événements normaux : démarrage, opérations réussies. |
| `warn` | moyenne | Anomalies tolérables, comportements inattendus mais récupérables. |
| `error` | haute | Exceptions, opérations échouées, états incorrects. |
| `critical` | très haute | Service inutilisable, défaillance majeure. |
| `off` | totale | Aucun message émis. |

### Sélection effective

Un message est émis si son niveau est supérieur ou égal au niveau effectif de son module. Le niveau effectif est :

- Le niveau configuré dans `modules.<nom_module>` si présent.
- Sinon, la valeur de `level`.

### Exemple

```yaml
logging:
  level: info
```

Avec cette configuration :

- Les messages `info`, `warn`, `error`, `critical` sont émis.
- Les messages `trace` et `debug` sont supprimés.

### Modifier le niveau global

```yaml
# Mode développement verbeux
logging:
  level: debug

# Mode production silencieux
logging:
  level: warn
```

---

## 5. Niveaux par module (`modules`)

Chaque sous-système du service est identifié par un module nommé. La clé `modules:` permet d'ajuster le niveau de chacun indépendamment.

### Modules disponibles

| Module | Contenu |
|---|---|
| `sea.boot` | Démarrage du service, migrations, initialisation. |
| `sea.http` | Handlers HTTP, autorisation, routes. |
| `sea.application` | Services applicatifs. |
| `sea.persistence` | Requêtes base de données, seeds, schéma. |
| `sea.runtime` | Validation, sérialisation. |
| `sea.security` | Authentification, tokens, cleanup. |
| `seastar` | Logs internes du framework réseau. |

### Configuration

```yaml
logging:
  level: info
  modules:
    sea.http: debug
    sea.persistence: warn
    seastar: error
```

### Comportement

Pour chaque message émis :

1. Le système identifie le module concerné.
2. Si une entrée existe dans `modules:` pour ce module, le niveau associé est appliqué.
3. Sinon, le niveau global défini par `level:` est appliqué.

### Cas d'usage typiques

| Objectif | Configuration |
|---|---|
| Diagnostiquer un problème HTTP en production | `modules: { sea.http: debug }` |
| Réduire le bruit de Seastar | `modules: { seastar: warn }` |
| Tracer en détail les seeds et migrations | `modules: { sea.persistence: debug, sea.boot: debug }` |
| Suivre les opérations d'authentification | `modules: { sea.security: debug }` |

### Exemple complet

```yaml
logging:
  level: info
  modules:
    sea.http: debug          # tous les détails HTTP
    sea.persistence: info    # opérations DB normales
    sea.boot: info           # logs de démarrage standards
    seastar: warn            # uniquement les warnings du framework
```

---

## 6. Sinks (destinations des logs)

Un **sink** est une destination de logs. Plusieurs sinks peuvent être déclarés simultanément ; chaque message est envoyé à tous les sinks actifs.

### Types de sinks supportés

| Type | Description |
|---|---|
| `console` | Écriture sur la sortie d'erreur standard (stderr). |
| `file` | Écriture dans un fichier sur disque, avec rotation optionnelle. |

### Déclaration

```yaml
logging:
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
        max_files: 10
```

### Clés communes à tous les sinks

| Clé | Type | Valeur par défaut | Description |
|---|---|---|---|
| `type` | enum | **Obligatoire** | Type de sink. Valeurs : `console`, `file`. |
| `format` | enum | `text` | Format de sortie. Valeurs : `text`, `json`. Voir [section 10](#10-format-des-logs). |
| `enabled` | booléen | `true` | Active ou désactive ce sink. Un sink désactivé ne reçoit aucun message. |

### Sinks multiples

Les sinks coexistent. Une configuration typique combine un sink `console` pour les développeurs et un sink `file` pour la persistance :

```yaml
sinks:
  - type: console
    format: text
    enabled: true
  - type: file
    format: json
    enabled: true
    path: "./logs/service.log"
```

Chaque message est ainsi à la fois affiché en console et stocké en JSON dans un fichier.

---

## 7. Sink `console`

### Configuration

```yaml
- type: console
  format: text
  enabled: true
```

### Clés acceptées

| Clé | Type | Valeur par défaut | Description |
|---|---|---|---|
| `type` | enum | — | Valeur exacte : `console`. |
| `format` | enum | `text` | Format de sortie. Valeurs : `text`, `json`. |
| `enabled` | booléen | `true` | Active le sink. |

### Comportement

Les messages sont écrits sur **stderr**. Le format `text` produit des lignes lisibles colorées (selon la capacité du terminal). Le format `json` produit du JSON line-delimited.

Le sink console ne supporte pas la rotation : il écrit en continu sur stderr.

---

## 8. Sink `file`

### Configuration

```yaml
- type: file
  format: json
  enabled: true
  path: "./logs/service.log"
  rotation:
    max_size: "100MB"
    time_pattern: daily
    max_files: 10
    compress: false
```

### Clés acceptées

| Clé | Type | Valeur par défaut | Description |
|---|---|---|---|
| `type` | enum | — | Valeur exacte : `file`. |
| `format` | enum | `text` | Format de sortie. |
| `enabled` | booléen | `true` | Active le sink. |
| `path` | chaîne | **Obligatoire** | Chemin du fichier de log. Le dossier parent est créé automatiquement s'il n'existe pas. |
| `rotation` | bloc | défauts | Configuration de la rotation. Voir [section 9](#9-rotation-des-fichiers). |

### Comportement

Les messages sont écrits dans le fichier indiqué par `path`. Le système ouvre le fichier en mode append : les redémarrages du service préservent les logs existants.

Si le fichier n'existe pas, il est créé. Si le dossier parent n'existe pas, il est créé récursivement avec les permissions standard du processus.

### Permissions et accessibilité

Les fichiers de logs sont créés avec les permissions par défaut du processus. Pour des environnements où les logs sont lus par un autre utilisateur (par exemple un agent de centralisation comme Promtail), s'assurer que les permissions du dossier permettent la lecture.

---

## 9. Rotation des fichiers

La rotation permet de limiter la taille des fichiers de logs et de conserver un historique.

### Configuration

```yaml
rotation:
  max_size: "100MB"
  time_pattern: daily
  max_files: 10
  compress: false
```

### Clés acceptées

| Clé | Type | Valeur par défaut | Description |
|---|---|---|---|
| `max_size` | taille | `100MB` (100 * 1024 * 1024 octets) | Taille maximale du fichier avant rotation. Format : voir [section 15](#15-format-des-durées-et-des-tailles). La valeur `0` désactive la rotation par taille. |
| `time_pattern` | enum | `daily` | Pattern de rotation temporelle. Valeurs : `none`, `hourly`, `daily`. |
| `max_files` | entier | `10` | Nombre maximal d'archives conservées. Les plus anciennes sont supprimées. |
| `compress` | booléen | `false` | Active la compression des archives. Voir limitation ci-dessous. |

### Patterns de rotation temporelle

| Valeur | Effet |
|---|---|
| `none` | Pas de rotation temporelle. Seule la taille peut déclencher une rotation. |
| `hourly` | Un nouveau fichier est créé à chaque heure. |
| `daily` (défaut) | Un nouveau fichier est créé à minuit. |

### Combinaison taille et temps

Les deux mécanismes peuvent être actifs simultanément : la rotation se déclenche dès qu'un des critères est atteint. Par exemple, avec `max_size: "100MB"` et `time_pattern: daily`, un nouveau fichier est créé soit lorsque les 100 Mo sont atteints, soit à minuit, selon ce qui arrive en premier.

### Limitation actuelle sur la compression

Le système accepte la clé `compress: true` mais la compression effective des archives n'est pas implémentée nativement dans la version actuelle. Les archives restent au format texte (ou JSON) non compressé. Un hook post-rotation pour compresser les archives peut être ajouté ultérieurement.

### Désactivation de la rotation

```yaml
rotation:
  max_size: 0
  time_pattern: none
```

Avec cette configuration, le fichier croît indéfiniment. Cette option ne doit être utilisée que pour des environnements de test de courte durée.

---

## 10. Format des logs

Le champ `format` de chaque sink contrôle la mise en forme des messages.

### Format `text`

Lignes lisibles par un humain, avec couleurs ANSI lorsque la sortie est un terminal.

**Exemple :**

```
[2026-05-14 10:23:45.123] [sea.http] [info] login successful: alice@example.com
[2026-05-14 10:23:45.567] [sea.persistence] [warn] migration skipped: column already exists
[2026-05-14 10:23:46.012] [sea.security] [error] JWT verification failed: token expired
```

Adapté pour :

- Consultation en console de développement
- Lecture directe via `tail -f` ou `less`

### Format `json`

JSON line-delimited (un objet par ligne).

**Exemple :**

```json
{"timestamp":"2026-05-14T10:23:45.123Z","logger":"sea.http","level":"info","message":"login successful: alice@example.com"}
{"timestamp":"2026-05-14T10:23:45.567Z","logger":"sea.persistence","level":"warn","message":"migration skipped: column already exists"}
{"timestamp":"2026-05-14T10:23:46.012Z","logger":"sea.security","level":"error","message":"JWT verification failed: token expired"}
```

Adapté pour :

- Ingestion par des outils de centralisation (Loki, ELK, Datadog, Splunk)
- Parsing programmatique
- Analyse via outils JSON (`jq`, `gron`)

### Champs du format JSON

| Champ | Description |
|---|---|
| `timestamp` | Date/heure UTC au format ISO 8601 avec millisecondes. |
| `logger` | Nom du module ayant émis le message. |
| `level` | Niveau de sévérité (`trace`, `debug`, `info`, `warn`, `error`, `critical`). |
| `message` | Contenu textuel du message. |

L'échappement des caractères spéciaux dans `message` respecte le standard RFC 8259.

### Choix entre formats

| Cas d'usage | Format recommandé |
|---|---|
| Sink console pour développement | `text` |
| Sink fichier pour centralisation | `json` |
| Sink fichier pour consultation manuelle | `text` |
| Audit et investigation a posteriori | `json` |

---

## 11. Flush immédiat (`flush_level`)

Le `flush_level` détermine à partir de quel niveau les messages sont écrits immédiatement sur disque, sans passer par le buffer interne.

### Configuration

```yaml
logging:
  flush_level: error
```

### Valeurs acceptées

Les mêmes que `level` : `trace`, `debug`, `info`, `warn`, `error`, `critical`.

### Comportement

| Cas | Comportement |
|---|---|
| Message de niveau < `flush_level` | Le message est mis en buffer. L'écriture sur disque est différée. |
| Message de niveau ≥ `flush_level` | Le message est écrit immédiatement (flush synchrone). |

### Cas d'usage

La valeur par défaut `error` garantit que tous les messages d'erreur et de niveau supérieur sont écrits immédiatement, même si le service crashe brutalement. Les messages moins critiques (info, debug) sont écrits par lots pour optimiser les performances.

### Recommandations

| Situation | Valeur recommandée |
|---|---|
| Production standard | `error` (défaut) |
| Investigation d'un crash | `warn` ou `info` |
| Tests de performance | `critical` (minimise les flushes) |

---

## 12. Logging asynchrone (`async`)

Le mode asynchrone décharge l'écriture des logs sur un thread dédié, afin que les opérations d'I/O ne ralentissent pas le service principal.

### Configuration

```yaml
async:
  enabled: true
  queue_size: 8192
  overflow_policy: overrun_oldest
```

### Clés acceptées

| Clé | Type | Valeur par défaut | Description |
|---|---|---|---|
| `enabled` | booléen | `true` | Active le mode asynchrone. Si `false`, les écritures sont synchrones (le service attend que chaque message soit écrit avant de continuer). |
| `queue_size` | entier | `8192` | Taille du buffer en nombre de messages. |
| `overflow_policy` | enum | `overrun_oldest` | Comportement lorsque la queue est pleine. Valeurs : `block`, `overrun_oldest`. |

### Politiques de débordement

| Valeur | Comportement |
|---|---|
| `overrun_oldest` (défaut) | Les messages les plus anciens dans la queue sont écrasés par les nouveaux. Aucun blocage du service, mais perte potentielle de messages anciens. |
| `block` | Le code appelant attend qu'une place se libère dans la queue. Aucun message n'est perdu, mais le service peut être ralenti en charge extrême. |

### Recommandations

| Cas d'usage | Configuration |
|---|---|
| Service haute charge | `queue_size: 16384`, `overflow_policy: overrun_oldest` |
| Service critique sans perte | `queue_size: 32768`, `overflow_policy: block` |
| Développement / debug | `enabled: false` (écriture synchrone immédiate) |

### Désactivation du mode asynchrone

```yaml
async:
  enabled: false
```

Avec `enabled: false`, chaque appel à une fonction de log attend la fin de l'écriture avant de retourner. Cette configuration garantit qu'aucun message n'est perdu en cas de crash, mais peut significativement ralentir le service en charge.

---

## 13. Endpoint `/admin/logs`

Le système maintient en permanence un buffer mémoire des 10 000 derniers messages, exposé via un endpoint REST. Ce buffer est toujours actif, indépendamment de la configuration des sinks.

### Route

```
GET /admin/logs
```

### Authentification requise

L'endpoint est protégé en double couche :

1. Authentification JWT valide (voir `auth.md`).
2. Rôle de l'utilisateur correspondant à `authorization.admin_role` (par défaut `admin`).

Toute requête sans authentification retourne 401. Toute requête avec un utilisateur non administrateur retourne 403.

### Query parameters

| Paramètre | Type | Valeur par défaut | Description |
|---|---|---|---|
| `limit` | entier | `100` | Nombre maximum d'entrées retournées. Plafonné à 1000. |
| `level` | enum | absent | Filtre par niveau minimum. Valeurs : `trace`, `debug`, `info`, `warn`, `error`, `critical`. |
| `logger` | chaîne | absent | Filtre exact par nom de module (par exemple `sea.http`). |
| `since` | entier | `0` | Retourne uniquement les messages dont le `sequence_id` est supérieur à cette valeur. Utilisé pour le polling. |
| `search` | chaîne | absent | Filtre les messages dont le contenu contient la sous-chaîne fournie (recherche insensible à la casse). |

### Format de la réponse

```json
{
  "logs": [
    {
      "sequence_id": 12345,
      "timestamp": "2026-05-14T10:23:45.123Z",
      "logger": "sea.http",
      "level": "info",
      "message": "login successful: alice@example.com"
    },
    {
      "sequence_id": 12346,
      "timestamp": "2026-05-14T10:23:46.456Z",
      "logger": "sea.persistence",
      "level": "warn",
      "message": "skipping migration: column already exists"
    }
  ],
  "count": 2,
  "next_sequence_id": 12347,
  "buffer_size": 7234,
  "buffer_capacity": 10000
}
```

| Clé | Description |
|---|---|
| `logs` | Tableau des messages correspondant aux filtres. |
| `count` | Nombre de messages retournés dans cette réponse. |
| `next_sequence_id` | Identifiant à passer comme `since=` à la requête suivante pour ne récupérer que les nouveaux messages. |
| `buffer_size` | Nombre total de messages actuellement dans le buffer. |
| `buffer_capacity` | Capacité maximale du buffer (10 000). |

### Pattern de polling

Pour un suivi en temps réel des logs, un client peut utiliser le pattern de polling incrémental :

```bash
# Première requête
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8081/admin/logs?limit=100"
# Réponse : next_sequence_id = 12345

# Requêtes suivantes : ne récupère que les nouveaux
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8081/admin/logs?since=12345&limit=100"
# Réponse : next_sequence_id = 12387

# Et ainsi de suite avec since=12387, etc.
```

Cette approche évite de retransmettre l'intégralité du buffer à chaque requête.

### Exemples de filtrage

**Récupérer uniquement les erreurs :**

```bash
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8081/admin/logs?level=error&limit=50"
```

**Suivre les opérations HTTP en debug :**

```bash
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8081/admin/logs?logger=sea.http&level=debug"
```

**Rechercher un utilisateur dans les logs :**

```bash
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8081/admin/logs?search=alice@example.com"
```

**Combiner les filtres :**

```bash
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8081/admin/logs?logger=sea.security&level=warn&search=token"
```

### Limites et caractéristiques

| Caractéristique | Valeur |
|---|---|
| Capacité du buffer | 10 000 messages (FIFO) |
| Persistance | En mémoire uniquement, perdu au redémarrage |
| Limit maximum par requête | 1 000 |
| Filtrage côté serveur | Oui |
| Performance | O(N) sur les filtres `search`, O(1) sur `since` et `level` |

### Distinction avec les sinks

Le ring buffer mémoire de `/admin/logs` est **indépendant** des sinks configurés. Quels que soient les sinks déclarés (ou même leur absence), le buffer reste actif. À l'inverse, désactiver l'endpoint d'administration n'affecte pas les sinks.

---

## 14. Endpoint `/admin/logs/loggers`

Endpoint complémentaire fournissant la liste des modules disponibles.

### Route

```
GET /admin/logs/loggers
```

### Authentification requise

Identique à `/admin/logs` : authentification JWT + rôle administrateur.

### Format de la réponse

```json
{
  "loggers": [
    "sea.boot",
    "sea.http",
    "sea.application",
    "sea.persistence",
    "sea.runtime",
    "sea.security",
    "seastar"
  ]
}
```

### Utilité

Cet endpoint permet à une interface d'administration de proposer dynamiquement la liste des modules disponibles dans un menu de filtrage, sans avoir à coder en dur la liste des modules.

---

## 15. Format des durées et des tailles

Les valeurs de taille (`max_size`) et de durée (paramètres internes) suivent des conventions spécifiques.

### Format des tailles

Les tailles sont exprimées sous forme de chaînes avec un suffixe d'unité.

| Suffixe | Multiplicateur |
|---|---|
| `B` ou absent | Octets |
| `K` ou `KB` | Kilo-octets (× 1024) |
| `M` ou `MB` | Méga-octets (× 1024²) |
| `G` ou `GB` | Giga-octets (× 1024³) |

### Exemples de tailles

| Valeur YAML | Octets résultants |
|---|---|
| `"500"` | 500 |
| `"500B"` | 500 |
| `"100KB"` | 102 400 |
| `"100K"` | 102 400 |
| `"100MB"` | 104 857 600 |
| `"1GB"` | 1 073 741 824 |

### Format des durées

Les durées sont exprimées sous forme de chaînes avec un suffixe d'unité.

| Suffixe | Unité |
|---|---|
| `s` ou absent | Secondes |
| `m` | Minutes |
| `h` | Heures |
| `d` | Jours |

### Exemples de durées

| Valeur YAML | Durée résultante |
|---|---|
| `"30"` | 30 secondes |
| `"30s"` | 30 secondes |
| `"15m"` | 15 minutes |
| `"24h"` | 24 heures |
| `"7d"` | 7 jours |

---

## 16. Exemples de configurations

### Configuration 1 — Développement simple

Configuration minimale pour environnement local.

```yaml
logging:
  level: debug
  sinks:
    - type: console
      format: text
      enabled: true
```

Comportement : tous les messages dès le niveau debug sont affichés en console avec couleurs.

### Configuration 2 — Production avec centralisation

Sinks doubles : console pour journaux système, fichier JSON pour ingestion.

```yaml
logging:
  level: info
  modules:
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
    queue_size: 16384
    overflow_policy: overrun_oldest
```

Comportement :

- Messages affichés en console (lisibles par les opérateurs).
- Messages stockés en JSON dans `./logs/service.log` pour ingestion par Loki/ELK.
- Rotation quotidienne ou à 100 MB, 30 archives conservées (≈ un mois).
- Mode asynchrone avec queue large pour absorber les pics de charge.

### Configuration 3 — Debug intensif d'un module spécifique

Investigation d'un problème HTTP sans perturber les autres modules.

```yaml
logging:
  level: info
  modules:
    sea.http: trace
    sea.security: debug
  sinks:
    - type: console
      format: text
      enabled: true
    - type: file
      format: json
      enabled: true
      path: "./logs/debug.log"
      rotation:
        max_size: "500MB"
        time_pattern: none
        max_files: 5
  flush_level: trace
  async:
    enabled: false
```

Comportement :

- Tous les messages HTTP sont tracés (niveau trace).
- Les opérations de sécurité sont en debug.
- Autres modules restent en info.
- Flush immédiat sur tous les niveaux (pas de risque de perte en cas de crash).
- Mode synchrone pour garantie de l'écriture.

### Configuration 4 — Service critique sans perte de logs

Configuration garantissant qu'aucun log n'est perdu.

```yaml
logging:
  level: info
  sinks:
    - type: file
      format: json
      enabled: true
      path: "/var/log/seadesktop/service.log"
      rotation:
        max_size: "1GB"
        time_pattern: daily
        max_files: 90
  flush_level: warn
  async:
    enabled: true
    queue_size: 65536
    overflow_policy: block
```

Comportement :

- Sink unique en fichier JSON pour archivage long terme.
- Rotation quotidienne, 90 jours d'historique.
- Flush immédiat dès le niveau warn.
- Queue importante (65 536 messages) et politique `block` qui ralentit le service plutôt que de perdre des messages.

### Configuration 5 — Désactivation totale

Pour tests automatisés ou environnements sans besoin de logs.

```yaml
logging:
  enabled: false
```

Comportement : aucun message n'est émis. Les performances sont maximales. L'endpoint `/admin/logs` retourne un buffer vide.

### Configuration 6 — Production avec tous les éléments

Configuration recommandée comme point de départ pour un déploiement de production.

```yaml
logging:
  enabled: true
  level: info

  modules:
    sea.http: info
    sea.persistence: info
    sea.security: info
    sea.boot: info
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
        compress: false

  flush_level: error

  async:
    enabled: true
    queue_size: 8192
    overflow_policy: overrun_oldest
```
