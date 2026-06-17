# Endpoints d'administration

L'API SeaDesktop expose une famille d'endpoints d'administration
qui permettent a un client distant (typiquement SeaUI en mode
remote) de gerer les projets YAML sur le serveur et de declencher
des redemarrages de service, sans acceder directement au systeme
de fichiers.

## Vue d'ensemble

| Endpoint | Methode | Role | Securite |
|----------|---------|------|----------|
| `/admin/projects` | GET | Lister les projets YAML disponibles | JWT + role admin |
| `/admin/projects/{file}` | GET | Lire le YAML brut d'un projet | JWT + role admin |
| `/admin/projects/{file}` | POST | Creer un nouveau projet YAML | JWT + role admin |
| `/admin/projects/{file}` | PUT | Remplacer le contenu d'un projet YAML existant | JWT + role admin |
| `/admin/projects/{file}` | DELETE | Supprimer un projet YAML | JWT + role admin |
| `/admin/restart` | POST | Demander un redemarrage propre du service | JWT + role admin |

Ces endpoints sont exposes par chaque service Backend_Seastar. Tous
les services d'un meme deploiement repondent equivalemment aux
endpoints `/admin/projects/*` puisqu'ils lisent le meme dossier
`configs/` sur le serveur. L'endpoint `/admin/restart`, en revanche,
redemarre uniquement le service qui a recu la requete.

## Resolution du dossier `configs/`

Le serveur determine ou chercher les fichiers YAML selon l'ordre de
priorite suivant :

1. **Variable d'environnement `SEA_DESKTOP_CONFIGS_DIR`**. Si elle
   est definie et non vide, ce chemin est utilise tel quel. C'est le
   mode recommande pour les deploiements Docker, les pipelines CI,
   et les configurations multi-machines.

2. **Dossier parent du YAML charge au demarrage**. Si Backend_Seastar
   est lance avec `--config configs/TestDemo.yaml`, le fallback
   retourne `configs/`. C'est le mode par defaut pour les lancements
   locaux sans configuration specifique.

3. **Dossier courant (`.`)**. Si le YAML charge n'a pas de parent
   (chemin sans `/`), le serveur retombe sur le dossier courant.

Exemples :

```bash
# Mode 1 : variable d'environnement
export SEA_DESKTOP_CONFIGS_DIR=/etc/seadesktop/configs
./backend_seastar --config /etc/seadesktop/configs/MyProject.yaml \
                  --service_name MyService
# -> sert les fichiers YAML de /etc/seadesktop/configs

# Mode 2 : fallback dirname
./backend_seastar --config configs/TestDemo.yaml \
                  --service_name TestService
# -> sert les fichiers YAML de configs/
```

Le dossier `configs/` est resolu **une seule fois au demarrage**.
Modifier la variable d'environnement `SEA_DESKTOP_CONFIGS_DIR` apres
le demarrage n'a aucun effet sur les endpoints deja enregistres ; le
service doit etre redemarre pour que le changement prenne effet.

## Modele de securite commun

Tous les endpoints `/admin/*` partagent le meme mecanisme de
securite a deux couches :

1. **ProtectedHandler** (middleware en amont). Verifie la presence
   et la validite du JWT. Sans un token valide, retourne 401 avant
   meme d'atteindre le handler.

2. **Garde admin dans le handler**. Compare le header `X-User-Role`
   (injecte par ProtectedHandler apres verification du JWT) avec la
   valeur de `security.access_control.admin_role` du YAML. Si la
   comparaison echoue, retourne 403.

Cette double couche garantit qu'un utilisateur authentifie mais non
administrateur ne peut pas acceder aux projets du serveur ni les
modifier.

## Validation du nom de fichier

Pour les endpoints qui prennent un parametre `{file}`, le serveur
applique une validation en deux etapes :

1. **Filtre de caracteres** : refuse les noms contenant `/`, `\`,
   `..`, ou commencant par `.`. L'extension doit etre `.yaml` ou
   `.yml` (insensible a la casse).

2. **Verification de chemin canonique** : resout les liens
   symboliques et verifie que le chemin obtenu reste dans le dossier
   configs. Protege contre les attaques de path-traversal via
   symlinks ou autres astuces filesystem.

Tout nom de fichier qui echoue a l'une des deux verifications
retourne 400 Bad Request.

---

## GET /admin/projects

Liste les fichiers YAML presents dans le dossier des projets.

### Requete

```http
GET /admin/projects HTTP/1.1
Authorization: Bearer <jwt_token>
```

### Reponse — succes

```http
HTTP/1.1 200 OK
Content-Type: application/json

{
  "projects": [
    { "name": "BlogDemo",  "file": "BlogDemo.yaml" },
    { "name": "FileTest",  "file": "FileTest.yaml" },
    { "name": "TestDemo",  "file": "TestDemo.yaml" }
  ]
}
```

Champs retournes :

- `name` : nom du projet, derive du nom de fichier sans extension.
  Utilise pour l'affichage cote client.
- `file` : nom de fichier complet avec extension. Sert d'identifiant
  pour les autres endpoints (`GET/POST/PUT/DELETE /admin/projects/{file}`).

La liste est triee alphabetiquement par `name` pour garantir une
reponse deterministe. Seuls les fichiers `*.yaml` et `*.yml`
(insensible a la casse) sont retournes. Les fichiers caches
(commencant par un point) sont ignores.

### Reponse — dossier vide ou inexistant

Si `SEA_DESKTOP_CONFIGS_DIR` pointe vers un dossier inexistant ou
vide, l'endpoint retourne 200 avec un tableau vide :

```http
HTTP/1.1 200 OK
Content-Type: application/json

{ "projects": [] }
```

Un warning est ecrit dans les logs (`sea.http`) si le dossier
n'existe pas ou n'est pas un dossier. Ce n'est pas une erreur cote
client : un dossier vide est un etat valide.

### Codes d'erreur

| Code | Cas |
|------|-----|
| 401 | Token JWT manquant ou invalide |
| 403 | Utilisateur authentifie mais role non-admin |
| 500 | Erreur de lecture filesystem (permissions, etc.) |

---

## GET /admin/projects/{file}

Retourne le contenu YAML brut d'un fichier projet specifique.

### Requete

```http
GET /admin/projects/TestDemo.yaml HTTP/1.1
Authorization: Bearer <jwt_token>
```

Le parametre `{file}` est le nom de fichier complet tel que retourne
par `GET /admin/projects` dans le champ `file`.

### Reponse — succes

```http
HTTP/1.1 200 OK
Content-Type: application/x-yaml

project:
  name: TestDemo

services:
  - name: TestService
    port: 8080
    database:
      type: mysql
      ...
```

Le body est le contenu brut du fichier YAML, octet pour octet
identique a ce qui est stocke sur disque. Les commentaires et le
formatage d'origine sont preserves.

### Codes d'erreur

| Code | Cas |
|------|-----|
| 400 | Nom de fichier invalide (path traversal, mauvaise extension, etc.) |
| 401 | Token JWT manquant ou invalide |
| 403 | Utilisateur authentifie mais role non-admin |
| 404 | Fichier introuvable dans `configs/` |
| 500 | Erreur de lecture filesystem |

---

## POST /admin/projects/{file}

Cree un nouveau fichier YAML projet. Refuse si un fichier du meme
nom existe deja ; utiliser `PUT` pour remplacer un projet existant.

### Requete

```http
POST /admin/projects/NewProject.yaml HTTP/1.1
Authorization: Bearer <jwt_token>
Content-Type: application/x-yaml

project:
  name: NewProject

services:
  - name: MainService
    port: 8080
    ...
```

Le body de la requete doit etre le contenu YAML brut. Le serveur le
valide avant persistance :

1. **Parsing YAML** : le contenu doit etre du YAML syntaxiquement
   valide conforme au schema SeaDesktop.
2. **Coherence du nom** : `project.name` dans le YAML doit
   correspondre au nom de fichier sans extension. Par exemple,
   `POST /admin/projects/NewProject.yaml` exige `project.name: NewProject`.

Si l'une des deux verifications echoue, le fichier n'est **pas**
persiste et la reponse est 400 Bad Request avec le message d'erreur
du parser.

### Strategie d'ecriture atomique

Le serveur ecrit le nouveau fichier via un pattern fichier
temporaire + rename :

1. Ecriture du contenu dans `<file>.tmp` dans le meme dossier.
2. Validation du YAML en parsant `<file>.tmp`.
3. Si la validation reussit, rename `<file>.tmp` vers le nom final
   (atomique sur les filesystems POSIX).
4. Si la validation echoue, suppression de `<file>.tmp` et retour
   400.

Cela garantit qu'un crash a n'importe quel moment laisse le
filesystem dans un etat coherent : soit le fichier n'existe pas
(crash avant rename), soit il est entierement ecrit (crash apres
rename). Aucun fichier partiel n'est jamais persiste.

### Reponse — succes

```http
HTTP/1.1 201 Created
Content-Type: application/json

{
  "success": true,
  "file": "NewProject.yaml"
}
```

### Codes d'erreur

| Code | Cas |
|------|-----|
| 400 | Nom de fichier invalide, YAML invalide, ou `project.name` ne correspond pas |
| 401 | Token JWT manquant ou invalide |
| 403 | Utilisateur authentifie mais role non-admin |
| 409 | Un fichier de ce nom existe deja (utiliser `PUT` pour remplacer) |
| 500 | Erreur d'ecriture filesystem |

### Limitations

Cet endpoint cree le fichier YAML sur disque mais **ne demarre pas**
automatiquement un container Docker pour le nouveau projet dans les
deploiements multi-services. Le client doit orchestrer manuellement
le deploiement d'un nouveau service (cette limitation est acceptee
pour v1.0).

---

## PUT /admin/projects/{file}

Remplace le contenu d'un fichier YAML projet existant. Refuse si le
fichier n'existe pas ; utiliser `POST` pour creer un nouveau projet.

### Requete

```http
PUT /admin/projects/TestDemo.yaml HTTP/1.1
Authorization: Bearer <jwt_token>
Content-Type: application/x-yaml

project:
  name: TestDemo

services:
  - name: TestService
    port: 8080
    ...
```

La validation, la strategie d'ecriture atomique et la verification de
coherence du nom sont identiques a `POST /admin/projects/{file}`.

### Reponse — succes

```http
HTTP/1.1 200 OK
Content-Type: application/json

{
  "success": true,
  "file": "TestDemo.yaml"
}
```

### Codes d'erreur

| Code | Cas |
|------|-----|
| 400 | Nom de fichier invalide, YAML invalide, ou `project.name` ne correspond pas |
| 401 | Token JWT manquant ou invalide |
| 403 | Utilisateur authentifie mais role non-admin |
| 404 | Le fichier n'existe pas (utiliser `POST` pour creer) |
| 500 | Erreur d'ecriture filesystem |

### Important — redemarrage du service

Un `PUT` reussi ecrit le nouveau YAML sur disque mais **ne redemarre
pas** le service. Le service en cours d'execution continue d'utiliser
le YAML charge au demarrage, conserve en RAM. Pour appliquer les
changements, le client doit ensuite appeler `POST /admin/restart`.

---

## DELETE /admin/projects/{file}

Supprime definitivement un fichier YAML projet. La suppression est
**irreversible** : pas de corbeille, pas de backup automatique.

### Requete

```http
DELETE /admin/projects/OldProject.yaml HTTP/1.1
Authorization: Bearer <jwt_token>
```

### Reponse — succes

```http
HTTP/1.1 200 OK
Content-Type: application/json

{
  "success": true,
  "file": "OldProject.yaml"
}
```

### Codes d'erreur

| Code | Cas |
|------|-----|
| 400 | Nom de fichier invalide |
| 401 | Token JWT manquant ou invalide |
| 403 | Utilisateur authentifie mais role non-admin |
| 404 | Le fichier n'existe pas |
| 500 | Erreur de suppression filesystem (permissions) |

### Important — services en cours d'execution

Si un service tourne actuellement avec le YAML supprime :

- Le service continue de tourner avec le YAML charge en RAM au
  demarrage ; pas de perturbation immediate.
- Au prochain redemarrage, le service echouera a charger (fichier
  introuvable).

Le client est responsable d'arreter le container correspondant
avant de supprimer le YAML, ou d'accepter que le prochain
redemarrage echouera jusqu'a ce que le fichier soit restaure (cette
limitation est acceptee pour v1.0).

---

## POST /admin/restart

Demande un redemarrage propre du service. Le processus se termine
apres un court delai, laissant a Docker (avec `restart: unless-stopped`
ou equivalent) le soin de redemarrer automatiquement le container. Le
nouveau processus relit les fichiers YAML depuis le disque, appliquant
les changements faits via `PUT` ou `POST /admin/projects/{file}`.

### Requete

```http
POST /admin/restart HTTP/1.1
Authorization: Bearer <jwt_token>
```

Le body de la requete est ignore.

### Reponse — succes

```http
HTTP/1.1 202 Accepted
Content-Type: application/json

{
  "success": true,
  "message": "Service restarting"
}
```

Le code 202 (au lieu de 200) indique que le travail demande (le
redemarrage) n'est pas termine au moment ou la reponse est envoyee :
le service est sur le point de se terminer.

### Comportement

1. Le handler verifie le JWT et le role admin.
2. La reponse HTTP est envoyee immediatement.
3. Le handler planifie un `_Exit(0)` avec un delai de 500ms,
   laissant au reactor le temps de finir d'ecrire la reponse sur le
   socket et de fermer la connexion proprement.
4. Apres 500ms, le processus se termine.
5. Docker (ou l'orchestrateur) detecte la sortie et redemarre le
   container.

Cet endpoint **necessite** un orchestrateur de container configure
pour redemarrer le service apres exit. En execution locale sans
Docker, le service se terminera definitivement.

### Codes d'erreur

| Code | Cas |
|------|-----|
| 401 | Token JWT manquant ou invalide |
| 403 | Utilisateur authentifie mais role non-admin |

Il n'y a pas d'autres cas d'erreur : si le JWT est valide et le role
admin, le redemarrage est initie sans condition.

### Workflow — application des changements YAML

Le flow typique pour un client distant (SeaUI) est :

1. `PUT /admin/projects/TestDemo.yaml` — sauvegarder le YAML modifie.
2. `POST /admin/restart` — redemarrer le service.
3. Le container Docker est tue puis redemarre.
4. Le nouveau processus backend relit le fichier YAML depuis le
   disque et applique les changements (migrations, enregistrement
   des routes, etc.).

Sans l'etape 2, les changements restent sur disque mais le service
en cours continue de servir le YAML precedent.

### Implications multi-services

`POST /admin/restart` redemarre **uniquement le service qui a recu
la requete**. Pour redemarrer un autre service dans un deploiement
multi-services, le client doit envoyer la requete a l'URL de ce
service specifique.

Par exemple, avec trois services sur les ports 8080, 8081 et 8082 :

```bash
# Redemarre le service A uniquement
curl -X POST -H "Authorization: Bearer $TOKEN" \
     http://localhost:8080/admin/restart

# Redemarre le service B uniquement (requete separee)
curl -X POST -H "Authorization: Bearer $TOKEN" \
     http://localhost:8081/admin/restart
```

---

## Exemples d'utilisation

### Obtenir un token admin

Tous les endpoints `/admin/*` necessitent un token admin authentifie.
Le token est obtenu via `POST /auth/login` :

```bash
TOKEN=$(curl -s -X POST http://localhost:8080/auth/login \
  -H "Content-Type: application/json" \
  -d '{"email":"admin@example.com","password":"AdminPass123!"}' \
  | jq -r '.access_token')
```

### Lister tous les projets

```bash
curl -H "Authorization: Bearer $TOKEN" \
     http://localhost:8080/admin/projects
```

### Lire un projet specifique

```bash
curl -H "Authorization: Bearer $TOKEN" \
     http://localhost:8080/admin/projects/TestDemo.yaml
```

### Creer un nouveau projet

```bash
curl -X POST \
     -H "Authorization: Bearer $TOKEN" \
     -H "Content-Type: application/x-yaml" \
     --data-binary @new_project.yaml \
     http://localhost:8080/admin/projects/NewProject.yaml
```

### Mettre a jour un projet existant

```bash
curl -X PUT \
     -H "Authorization: Bearer $TOKEN" \
     -H "Content-Type: application/x-yaml" \
     --data-binary @modified_project.yaml \
     http://localhost:8080/admin/projects/TestDemo.yaml
```

### Supprimer un projet

```bash
curl -X DELETE \
     -H "Authorization: Bearer $TOKEN" \
     http://localhost:8080/admin/projects/OldProject.yaml
```

### Appliquer les changements via redemarrage

```bash
# 1. Modifier le YAML
curl -X PUT \
     -H "Authorization: Bearer $TOKEN" \
     -H "Content-Type: application/x-yaml" \
     --data-binary @modified.yaml \
     http://localhost:8080/admin/projects/TestDemo.yaml

# 2. Redemarrer le service pour appliquer
curl -X POST \
     -H "Authorization: Bearer $TOKEN" \
     http://localhost:8080/admin/restart

# 3. Attendre que Docker redemarre le container
sleep 5

# 4. Verifier que le service est revenu
curl http://localhost:8080/health
```

---

## Notes d'implementation

### Equivalence entre services

Les endpoints `/admin/projects/*` sont exposes par chaque service
d'un deploiement. Comme tous les services partagent le meme volume
`configs/` (typiquement un volume Docker), ils voient tous les memes
fichiers YAML. Un client peut donc utiliser n'importe quelle URL de
service pour lister, lire, ecrire ou supprimer des projets.

L'endpoint `/admin/restart`, en revanche, est specifique a chaque
service : il ne redemarre que le service qui a recu la requete.

### Architecture multi-services

Dans un deploiement multi-services typique :

```
Machine hote
├── /var/lib/seadesktop/configs/      (volume partage)
│   ├── TestDemo.yaml
│   ├── BlogDemo.yaml
│   └── FileTest.yaml
│
└── Docker
    ├── Container service A (port 8080)  -> /app/configs (monte)
    ├── Container service B (port 8081)  -> /app/configs (meme)
    └── Container service C (port 8082)  -> /app/configs (meme)
```

Les trois services voient les memes fichiers YAML. Modifier un
fichier via le service A est immediatement visible des services B et
C. Toutefois :

- Le changement est **sur disque uniquement** ; les services en
  cours d'execution utilisent toujours le YAML qu'ils ont charge au
  demarrage.
- Pour appliquer le changement au service A, appeler
  `POST /admin/restart` sur le service A.
- Pour l'appliquer aux services B et C, appeler
  `POST /admin/restart` sur chacun d'eux separement.

### Configuration du role admin

Le role admin est configurable par service dans le YAML :

```yaml
security:
  access_control:
    admin_role: admin   # defaut ; peut etre personnalise
```

Les utilisateurs ayant ce role dans leur JWT (claim `role`) recoivent
l'acces aux endpoints `/admin/*`. Le role est verifie par le header
`X-User-Role` injecte par `ProtectedHandler` apres verification du
JWT.
