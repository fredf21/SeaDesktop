# Endpoints d'administration des projets

L'API SeaDesktop expose un endpoint d'administration permettant de
lister les projets YAML disponibles sur le serveur. C'est la première
brique pour permettre à un client distant (typiquement SeaUI en mode
remote) de découvrir et gérer les projets sans accéder directement au
filesystem du serveur.

## Vue d'ensemble

| Endpoint | Méthode | Rôle | Sécurité |
|----------|---------|------|----------|
| `/admin/projects` | GET | Liste les projets YAML disponibles | JWT + rôle admin |

Cet endpoint est exposé par chaque service Backend_Seastar. Tous les
services d'un même projet répondent de manière équivalente : ils lisent
le même dossier `configs/` sur le serveur.

## Résolution du dossier `configs/`

Le serveur détermine où chercher les fichiers YAML selon l'ordre
suivant :

1. **Variable d'environnement `SEA_DESKTOP_CONFIGS_DIR`**. Si définie
   et non vide, ce chemin est utilisé tel quel. C'est le mode
   recommandé pour les déploiements Docker, les pipelines CI et les
   setups multi-machines.

2. **Dossier parent du YAML chargé au démarrage**. Si Backend_Seastar
   est lancé avec `--config configs/TestDemo.yaml`, le fallback
   retourne `configs/`. C'est le mode par défaut pour les lancements
   locaux sans configuration spécifique.

3. **Dossier courant (`.`)**. Si le YAML chargé n'a pas de parent
   (chemin sans `/`), le serveur retombe sur le dossier courant.

Exemples :

```bash
# Mode 1 : variable d'environnement
export SEA_DESKTOP_CONFIGS_DIR=/etc/seadesktop/configs
./backend_seastar --config /etc/seadesktop/configs/MyProject.yaml \
                  --service_name MyService
# -> liste les YAML de /etc/seadesktop/configs

# Mode 2 : fallback dirname
./backend_seastar --config configs/TestDemo.yaml \
                  --service_name TestService
# -> liste les YAML de configs/
```

## GET /admin/projects

Liste les fichiers YAML présents dans le dossier des projets.

### Requête

```http
GET /admin/projects HTTP/1.1
Authorization: Bearer <jwt_token>
```

Le JWT doit appartenir à un utilisateur ayant le rôle administrateur
configuré dans le YAML (`security.access_control.admin_role`, par
défaut `admin`).

### Réponse — succès

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

Champs retournés :

- `name` : nom du projet, dérivé du nom de fichier sans extension.
  Utilisé pour l'affichage côté client.
- `file` : nom de fichier complet avec extension. Sert d'identifiant
  pour les endpoints futurs (`GET /admin/projects/{file}`).

La liste est triée alphabétiquement par `name` pour garantir une
réponse déterministe. Seuls les fichiers `*.yaml` et `*.yml`
(insensibles à la casse) sont retournés. Les fichiers cachés
(commençant par un point) sont ignorés.

### Réponse — dossier vide ou inexistant

Si `SEA_DESKTOP_CONFIGS_DIR` pointe vers un dossier inexistant ou
vide, l'endpoint retourne 200 avec un tableau vide :

```http
HTTP/1.1 200 OK
Content-Type: application/json

{ "projects": [] }
```

Un avertissement est inscrit dans les logs (`sea.http`) si le dossier
n'existe pas ou n'est pas un dossier. Ce n'est pas une erreur côté
client : un dossier vide est un état valide.

### Codes d'erreur

| Code | Cas | Réponse |
|------|-----|---------|
| 401 | Token JWT manquant ou invalide | `{"success":false,"error":{"code":"AUTHENTICATION_ERROR","message":"Token manquant."}}` |
| 403 | Utilisateur authentifié mais rôle non-admin | `{"success":false,"error":{"code":"AUTHORIZATION_ERROR","message":"Admin role required."}}` |
| 500 | Erreur de lecture du filesystem (permissions, etc.) | `{"success":false,"error":{"code":"INTERNAL_ERROR","message":"..."}}` |

### Sécurité

L'endpoint est protégé par une **double couche** :

1. **ProtectedHandler** (middleware en amont). Vérifie la présence et
   la validité du JWT. Sans token valide, retourne 401 avant d'arriver
   au handler.

2. **Garde admin dans le handler**. Compare le header `X-User-Role`
   (injecté par ProtectedHandler après vérification du JWT) avec la
   valeur de `security.access_control.admin_role` du YAML. Si la
   correspondance échoue, retourne 403.

Cette double couche garantit qu'un utilisateur authentifié mais non
administrateur ne peut pas lister les projets du serveur.

## Exemples d'utilisation

### Test sans authentification

```bash
curl -i http://localhost:8080/admin/projects
```

Résultat attendu : 401 Unauthorized.

### Test avec token admin

```bash
TOKEN=$(curl -s -X POST http://localhost:8080/auth/login \
  -H "Content-Type: application/json" \
  -d '{"email":"admin@example.com","password":"AdminPass123!"}' \
  | jq -r '.access_token')

curl -i -H "Authorization: Bearer $TOKEN" \
     http://localhost:8080/admin/projects
```

### Override du dossier configs

```bash
SEA_DESKTOP_CONFIGS_DIR=/srv/seadesktop/projects \
  ./backend_seastar \
    --config /srv/seadesktop/projects/MyProject.yaml \
    --service_name MyService
```

L'endpoint listera les YAML de `/srv/seadesktop/projects`, peu importe
d'où a été lu le YAML de boot.

## Notes d'implémentation

Cet endpoint est exposé par **chaque service** d'un projet. Si un
projet contient trois services tournant sur les ports 8080, 8081 et
8082, les trois endpoints répondent de manière équivalente puisqu'ils
lisent le même dossier `configs/`. Un client peut donc s'adresser à
n'importe quel service du projet pour obtenir la liste.

Le dossier `configs/` est résolu **une seule fois au démarrage** de
Backend_Seastar. Modifier la variable d'environnement
`SEA_DESKTOP_CONFIGS_DIR` après le démarrage n'a aucun effet sur les
endpoints déjà enregistrés ; il faut redémarrer le service pour que
le changement soit pris en compte.
