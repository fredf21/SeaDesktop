# SeaDesktop — Authentification

Documentation de référence pour la configuration et l'utilisation du système d'authentification JWT de SeaDesktop. Ce document décrit chaque clé YAML supportée, ses valeurs autorisées, son comportement par défaut, ainsi que la réaction attendue du système. Toutes les informations proviennent du code source du projet.

---

## Sommaire

1. [Principe général](#1-principe-général)
2. [Activation de l'authentification](#2-activation-de-lauthentification)
3. [Bloc `security.authentication`](#3-bloc-securityauthentication)
4. [Entité source d'authentification](#4-entité-source-dauthentification)
5. [Routes générées](#5-routes-générées)
6. [Inscription (`/auth/register`)](#6-inscription-authregister)
7. [Connexion (`/auth/login`)](#7-connexion-authlogin)
8. [Renouvellement (`/auth/refresh`)](#8-renouvellement-authrefresh)
9. [Déconnexion (`/auth/logout`)](#9-déconnexion-authlogout)
10. [Informations du compte (`/auth/me`)](#10-informations-du-compte-authme)
11. [Algorithmes de signature JWT](#11-algorithmes-de-signature-jwt)
12. [Mode de livraison des tokens](#12-mode-de-livraison-des-tokens)
13. [Configuration des cookies](#13-configuration-des-cookies)
14. [Suivi des tokens (`token_tracking`)](#14-suivi-des-tokens-token_tracking)
15. [Utilisation des tokens dans les requêtes](#15-utilisation-des-tokens-dans-les-requêtes)
16. [Format des durées](#16-format-des-durées)
17. [Codes de réponse](#17-codes-de-réponse)
18. [Exemples de configurations](#18-exemples-de-configurations)

---

## 1. Principe général

L'authentification permet aux clients de s'identifier auprès du service afin d'accéder aux routes protégées. SeaDesktop utilise des **tokens JWT** (JSON Web Tokens) émis par le serveur lors de la connexion.

Deux tokens sont émis simultanément :

- Un **access token** de courte durée, utilisé pour authentifier chaque requête vers les routes protégées.
- Un **refresh token** de longue durée, utilisé uniquement pour renouveler l'access token sans saisir à nouveau les identifiants.

L'utilisation systématique de tokens à courte durée combinée à un mécanisme de renouvellement permet de limiter l'impact d'un éventuel vol de token.

---

## 2. Activation de l'authentification

L'authentification s'active à deux niveaux conjoints :

1. Au niveau du service, dans le bloc `security.authentication`.
2. Au niveau d'une entité, en désignant celle-ci comme source via `options.is_auth_source: true`.

Si l'un des deux niveaux n'est pas configuré, les routes `/auth/*` ne sont pas exposées.

### Exemple minimal

```yaml
services:
  - name: MonService
    port: 8081

    security:
      authentication:
        type: jwt
        algorithm: HS256
        secret: "une_cle_secrete_de_32_caracteres_minimum"

    entities:
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

Avec cette configuration, les routes `/auth/register`, `/auth/login`, `/auth/refresh`, `/auth/logout`, `/auth/me` sont automatiquement exposées.

---

## 3. Bloc `security.authentication`

### Structure complète

```yaml
security:
  authentication:
    type: jwt
    algorithm: HS256
    secret: ""
    public_key_path: ""
    private_key_path: ""
    issuer: ""
    audience: ""
    access_token_ttl: "15m"
    refresh_token_ttl: "14d"
    token_delivery: body
    cookie: { ... }
    token_tracking: { ... }
```

### Clés acceptées

| Clé | Type | Valeur par défaut | Description |
|---|---|---|---|
| `type` | enum | `none` | Type d'authentification. Valeurs supportées : `none`, `jwt`, `oauth2`. Seul `jwt` est pleinement implémenté. |
| `algorithm` | enum | `HS256` | Algorithme de signature des tokens JWT. Voir [section 11](#11-algorithmes-de-signature-jwt). |
| `secret` | chaîne | `""` | Clé secrète pour les algorithmes HS\*. Voir détails ci-dessous. |
| `public_key_path` | chaîne | `""` | Chemin vers la clé publique (PEM) pour les algorithmes RS\* et ES\*. |
| `private_key_path` | chaîne | `""` | Chemin vers la clé privée (PEM) pour les algorithmes RS\* et ES\*. |
| `issuer` | chaîne | `""` | Valeur du claim `iss` (issuer) dans les tokens émis. |
| `audience` | chaîne | `""` | Valeur du claim `aud` (audience) dans les tokens émis. |
| `access_token_ttl` | durée | `15m` | Durée de vie de l'access token. Voir [section 16](#16-format-des-durées) pour le format. |
| `refresh_token_ttl` | durée | `14d` | Durée de vie du refresh token. |
| `token_delivery` | enum | `body` | Mode de livraison des tokens. Voir [section 12](#12-mode-de-livraison-des-tokens). |
| `cookie` | bloc | défauts | Configuration des cookies. Voir [section 13](#13-configuration-des-cookies). |
| `token_tracking` | bloc | désactivé | Suivi centralisé des tokens. Voir [section 14](#14-suivi-des-tokens-token_tracking). |

### Comportement de `secret`

La valeur de `secret` peut être :

- Une chaîne littérale, par exemple `secret: "ma_cle_secrete_de_32_caracteres_minimum"`.
- Une variable d'environnement, en utilisant la syntaxe `${NOM_VARIABLE}`. Le système substitue la valeur de la variable au moment du chargement.
- Vide (`secret: ""`). Dans ce cas, le système génère automatiquement une clé secrète au premier démarrage et la persiste dans le répertoire `./runtime/secrets/`. Les redémarrages ultérieurs réutilisent la même clé.

```yaml
# Variante 1 : valeur littérale
secret: "PtR4ULvZBmQ9XnK2HsCxYwEa6FjDgN8T"

# Variante 2 : depuis l'environnement
secret: "${JWT_SECRET}"

# Variante 3 : génération automatique persistée
secret: ""
```

### Longueur minimale du secret

Si `secret` est fourni, il doit faire au moins 32 caractères. Sinon, le démarrage du service échoue avec une erreur de validation.

### Choix entre `secret` et clés asymétriques

| Algorithme | Champs à fournir |
|---|---|
| `HS256`, `HS384`, `HS512` | `secret` uniquement |
| `RS256`, `RS384`, `RS512`, `ES256`, `ES384`, `ES512` | `public_key_path` et `private_key_path` |

---

## 4. Entité source d'authentification

Une entité du service doit être désignée comme source d'authentification via l'option `is_auth_source: true`. Cette entité contient les comptes utilisateurs.

### Une seule entité par service

Une et une seule entité peut porter `is_auth_source: true`. Si plusieurs entités le déclarent, le démarrage du service échoue.

Si aucune entité ne la déclare, le service démarre quand même et la vérification JWT continue de fonctionner pour les endpoints protégés, mais les routes `/auth/*` ne sont pas enregistrées. Tout appel à `/auth/login` ou `/auth/register` retourne 404 dans ce cas.

### Champs requis

L'entité source doit comporter au minimum :

- Un champ `id` (UUID ou entier auto-incrémenté). L'`id` est généré automatiquement par `RegisterHandler` et est obligatoire : sans lui, l'inscription réussit au niveau base de données mais retourne 400 avec « Missing ID on created entity ».
- Un champ identifiant unique (généralement `email`).
- Un champ de type `password` contenant le mot de passe hashé.
- Un champ utilisé comme rôle (généralement `role`).

Le nom du champ contenant le rôle peut être personnalisé via `authorization.roles_claim_name`. Le défaut est `role`.

### Exemple complet

```yaml
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
    - name: role
      type: string
      default: "user"
```

### Comportements automatiques

Le type `password` applique :

- Un hashage bcrypt automatique à l'insertion et à la mise à jour.
- L'exclusion du champ des réponses JSON (`serializable: false` par défaut).

Voir le guide utilisateur principal pour les détails du type `password`.

---

## 5. Routes générées

Lorsque l'authentification est activée et qu'une entité source existe, cinq routes sont automatiquement exposées :

| Méthode | Route | Authentification requise | Description |
|---|---|---|---|
| `POST` | `/auth/register` | Non | Inscription d'un nouveau compte. |
| `POST` | `/auth/login` | Non | Connexion avec identifiants. |
| `POST` | `/auth/refresh` | Non (utilise le refresh token) | Renouvellement de l'access token. |
| `POST` | `/auth/logout` | Oui | Déconnexion. |
| `GET` | `/auth/me` | Oui | Informations du compte connecté. |

---

## 6. Inscription (`/auth/register`)

### Requête

```bash
curl -X POST http://localhost:8081/auth/register \
  -H "Content-Type: application/json" \
  -d '{
    "email": "alice@example.com",
    "mot_de_passe": "MotDePasseEnClair",
    "role": "user"
  }'
```

### Format de la requête

Le corps doit contenir les champs de l'entité source jugés obligatoires. Les noms exacts dépendent de la déclaration des champs de l'entité.

### Format de la réponse

```json
{
  "id": "550e8400-e29b-41d4-a716-446655440000",
  "email": "alice@example.com",
  "role": "user"
}
```

Le champ de mot de passe n'apparaît jamais dans la réponse (exclusion automatique via le type `password`).

### Comportement attendu

1. Le système valide les champs fournis (formats, contraintes).
2. Le mot de passe est hashé en bcrypt avant insertion.
3. L'UUID du nouvel utilisateur est généré automatiquement.
4. Le compte est créé en base avec un code 201.

### Codes de réponse

| Code | Cause |
|---|---|
| 201 | Compte créé avec succès. |
| 400 | Validation échouée : email invalide, mot de passe absent, etc. |
| 409 | Contrainte d'unicité violée (par exemple, email déjà utilisé). |

### Avertissement de sécurité — inscription ouverte

En v1.0, `POST /auth/register` accepte le champ `role` tel quel. Quiconque ayant accès réseau à l'endpoint peut donc créer un compte administrateur en envoyant `"role": "admin"`. C'est acceptable pour amorcer le premier administrateur d'un déploiement neuf, mais pour un déploiement public, vous devriez soit :

- désactiver l'inscription ouverte une fois le premier admin créé (retirer la route au niveau du proxy, ou filtrer au niveau applicatif) ;
- soit retirer le champ `role` des requêtes entrantes et le forcer à `user` par défaut au niveau du proxy.

Un durcissement natif est prévu pour la v1.1 qui restreindra optionnellement le champ `role` aux seuls administrateurs existants.

---

## 7. Connexion (`/auth/login`)

### Requête

```bash
curl -X POST http://localhost:8081/auth/login \
  -H "Content-Type: application/json" \
  -d '{
    "email": "alice@example.com",
    "mot_de_passe": "MotDePasseEnClair"
  }'
```

### Format de la réponse selon `token_delivery`

**Mode `body` (défaut) :**

```json
{
  "access_token": "eyJhbGciOiJIUzI1NiIs...",
  "refresh_token": "eyJhbGciOiJIUzI1NiIs...",
  "token_type": "Bearer",
  "expires_in": 900
}
```

**Mode `cookie` :**

```
HTTP/1.1 200 OK
Set-Cookie: sea_access=eyJhbGciOiJIUzI1NiIs...; HttpOnly; Secure; SameSite=Lax; Path=/
Set-Cookie: sea_refresh=eyJhbGciOiJIUzI1NiIs...; HttpOnly; Secure; SameSite=Lax; Path=/

{
  "token_type": "Bearer",
  "expires_in": 900
}
```

**Mode `both` :** les tokens apparaissent à la fois dans le corps JSON et dans les cookies.

> **Mode Remote de SeaUI.** SeaUI en mode Remote lit `access_token` depuis le corps JSON de la réponse de connexion. Un backend configuré avec `token_delivery: cookie` provoquera l'échec du login SeaUI avec le message « Login response missing access_token ». Utilisez `body` (défaut) ou `both` pour les backends administrés à distance par SeaUI.

### Comportement attendu

1. Le système recherche un utilisateur dont l'email correspond.
2. Le mot de passe fourni est vérifié contre le hash bcrypt en base.
3. Si la vérification réussit, deux tokens sont générés :
   - L'access token contient les claims `sub` (identifiant utilisateur), `role`, `iat`, `exp`.
   - Le refresh token contient `sub`, `jti` (identifiant unique du token), `iat`, `exp`.
4. Si `token_tracking.enabled: true`, le refresh token est inséré dans la table système des refresh tokens valides.
5. Les tokens sont retournés selon le mode `token_delivery`.

### Codes de réponse

| Code | Cause |
|---|---|
| 200 | Connexion réussie. |
| 400 | Champs requis manquants. |
| 401 | Email inconnu ou mot de passe incorrect. |

---

## 8. Renouvellement (`/auth/refresh`)

### Requête

**Mode `body` :**

```bash
curl -X POST http://localhost:8081/auth/refresh \
  -H "Content-Type: application/json" \
  -d '{ "refresh_token": "eyJhbGciOiJIUzI1NiIs..." }'
```

**Mode `cookie` :** aucun corps requis ; le refresh token est lu depuis le cookie configuré dans `cookie.refresh_token_name`.

```bash
curl -X POST http://localhost:8081/auth/refresh \
  --cookie "sea_refresh=eyJhbGciOiJIUzI1NiIs..."
```

### Format de la réponse

Identique à la réponse de `/auth/login` : un nouvel access token et, selon la configuration de rotation, éventuellement un nouveau refresh token.

### Comportement attendu

1. Le refresh token est extrait du corps JSON ou du cookie selon le mode configuré.
2. Sa signature et son expiration sont vérifiées.
3. Si `token_tracking.enabled: true`, l'existence du refresh token dans la liste blanche en base est vérifiée. Un refresh token absent de la liste est rejeté.
4. Un nouvel access token est généré.
5. Si `token_tracking.rotation.enabled: true` (valeur par défaut), l'ancien refresh token est supprimé de la liste blanche et un nouveau refresh token est émis.

### Codes de réponse

| Code | Cause |
|---|---|
| 200 | Renouvellement réussi. |
| 400 | Refresh token absent. |
| 401 | Refresh token invalide, expiré, ou absent de la liste blanche (si tracking activé). |

---

## 9. Déconnexion (`/auth/logout`)

### Requête

```bash
curl -X POST http://localhost:8081/auth/logout \
  -H "Authorization: Bearer eyJhbGciOiJIUzI1NiIs..."
```

En mode `cookie`, les cookies sont automatiquement transmis par le navigateur.

### Comportement attendu

1. L'access token est extrait de l'en-tête `Authorization` ou du cookie.
2. Sa validité est vérifiée.
3. Si `token_tracking.enabled: true` :
   - L'access token est inséré dans la liste noire des tokens révoqués.
   - Le refresh token correspondant est supprimé de la liste blanche.
4. En mode `cookie`, des en-têtes `Set-Cookie` avec `expires=0` sont émis pour effacer les cookies côté client.

### Codes de réponse

| Code | Cause |
|---|---|
| 200 | Déconnexion réussie. |
| 401 | Access token absent ou invalide. |

### Comportement sans tracking

Si `token_tracking.enabled: false`, la déconnexion n'invalide pas réellement le token côté serveur : l'access token reste utilisable jusqu'à son expiration naturelle. Seuls les cookies sont effacés en mode `cookie`. Pour une révocation immédiate, le suivi des tokens doit être activé.

---

## 10. Informations du compte (`/auth/me`)

### Requête

```bash
curl -X GET http://localhost:8081/auth/me \
  -H "Authorization: Bearer eyJhbGciOiJIUzI1NiIs..."
```

### Format de la réponse

Les champs de l'entité source actuellement connectée, à l'exclusion des champs `serializable: false` (typiquement le mot de passe hashé).

```json
{
  "id": "550e8400-e29b-41d4-a716-446655440000",
  "email": "alice@example.com",
  "role": "user",
  "created_at": "2026-05-14T10:23:45.123Z",
  "updated_at": "2026-05-14T10:23:45.123Z"
}
```

### Codes de réponse

| Code | Cause |
|---|---|
| 200 | Informations retournées. |
| 401 | Access token absent, invalide, ou révoqué (si tracking activé). |

---

## 11. Algorithmes de signature JWT

### Algorithmes supportés

| Algorithme | Type | Description |
|---|---|---|
| `HS256` | Symétrique | HMAC SHA-256. Une seule clé secrète partagée. Défaut. |
| `HS384` | Symétrique | HMAC SHA-384. |
| `HS512` | Symétrique | HMAC SHA-512. |
| `RS256` | Asymétrique | RSA SHA-256. Clés publique et privée. |
| `RS384` | Asymétrique | RSA SHA-384. |
| `RS512` | Asymétrique | RSA SHA-512. |
| `ES256` | Asymétrique | ECDSA SHA-256. |
| `ES384` | Asymétrique | ECDSA SHA-384. |
| `ES512` | Asymétrique | ECDSA SHA-512. |

### Choix recommandé

| Cas d'usage | Algorithme recommandé |
|---|---|
| Service monolithique, contrôle total des deux côtés | `HS256` (simple, performant). |
| Plusieurs services partageant l'authentification | `RS256` ou `ES256` (la clé publique seule suffit à vérifier). |
| Architecture micro-services avec délégation | `RS256` ou `ES256`. |

### Configuration pour algorithme symétrique

```yaml
authentication:
  type: jwt
  algorithm: HS256
  secret: "${JWT_SECRET}"
```

### Configuration pour algorithme asymétrique

```yaml
authentication:
  type: jwt
  algorithm: RS256
  public_key_path: "/etc/seadesktop/keys/jwt_public.pem"
  private_key_path: "/etc/seadesktop/keys/jwt_private.pem"
```

---

## 12. Mode de livraison des tokens

Le champ `token_delivery` détermine comment les tokens sont transmis entre le client et le serveur.

### Modes disponibles

| Mode | Description | Cas d'usage |
|---|---|---|
| `body` (défaut) | Les tokens sont retournés dans le JSON de réponse uniquement. Le client est responsable de leur stockage. | API mobile, CLI, applications desktop, clients machine-à-machine. |
| `cookie` | Les tokens sont placés dans des cookies HttpOnly inaccessibles depuis JavaScript. | Applications web nécessitant une protection contre les attaques XSS. |
| `both` | Les tokens sont transmis simultanément dans le JSON et dans les cookies. | Phases de migration, services accédés par plusieurs types de clients. |

### Lecture des tokens côté serveur

Pour chaque requête entrante vers une route protégée, le serveur recherche le token dans l'ordre :

1. En-tête HTTP `Authorization: Bearer <token>`.
2. Cookie portant le nom configuré dans `cookie.access_token_name`.

Cette stratégie permet à un même service d'accepter simultanément les clients utilisant l'un ou l'autre mode.

---

## 13. Configuration des cookies

Le bloc `cookie:` (singulier) configure les cookies utilisés lorsque `token_delivery` vaut `cookie` ou `both`.

### Bloc complet

```yaml
authentication:
  token_delivery: cookie
  cookie:
    domain: ".example.com"
    path: "/"
    secure: true
    same_site: lax
    access_token_name: sea_access
    refresh_token_name: sea_refresh
```

### Clés acceptées

| Clé | Type | Valeur par défaut | Description |
|---|---|---|---|
| `domain` | chaîne | `""` (origine de la requête) | Domaine pour lequel le cookie est valide. La notation `.example.com` couvre tous les sous-domaines. |
| `path` | chaîne | `/` | Chemin pour lequel le cookie est valide. |
| `secure` | booléen | `true` | Si `true`, le cookie n'est transmis qu'en HTTPS. |
| `same_site` | enum | `lax` | Politique SameSite. Valeurs : `lax`, `strict`, `none`. |
| `access_token_name` | chaîne | `sea_access` | Nom du cookie portant l'access token. |
| `refresh_token_name` | chaîne | `sea_refresh` | Nom du cookie portant le refresh token. |

### Attribut HttpOnly

L'attribut HTTP `HttpOnly` est toujours `true` et n'est pas configurable. Cette propriété empêche JavaScript d'accéder aux cookies, protégeant ainsi contre les attaques XSS.

### Politique SameSite

| Valeur | Description | Cas d'usage |
|---|---|---|
| `strict` | Cookie envoyé uniquement pour les requêtes émanant du même site. | Maximum de protection, peut casser certains flux OAuth. |
| `lax` (défaut) | Cookie envoyé pour les navigations top-level. | Compromis recommandé pour la majorité des cas. |
| `none` | Cookie envoyé en cross-site. Nécessite `secure: true`. | API cross-domain authentifiée. |

### Environnement de développement

En développement local sans HTTPS, mettre `secure: false` pour autoriser la transmission des cookies en HTTP :

```yaml
cookie:
  secure: false   # uniquement en développement local
  same_site: lax
```

**Cette configuration ne doit jamais être utilisée en production.**

---

## 14. Suivi des tokens (`token_tracking`)

Le suivi des tokens ajoute une couche de gestion centralisée des sessions permettant :

- La révocation immédiate des access tokens via `/auth/logout`.
- Le contrôle strict des refresh tokens via une liste blanche en base.
- La rotation automatique des refresh tokens à chaque renouvellement.

### Bloc complet

```yaml
authentication:
  token_tracking:
    enabled: true
    refresh_table: RefreshToken
    revoked_table: RevokedToken
    cache:
      enabled: true
      ttl: "5m"
      max_size: 10000
    rotation:
      enabled: true
    auto_cleanup:
      enabled: true
      interval: "1h"
      keep_revoked_for: "30d"
```

### Clés au niveau principal

| Clé | Type | Valeur par défaut | Description |
|---|---|---|---|
| `enabled` | booléen | `false` | Active le suivi. Si `false`, les tokens sont purement stateless. |
| `refresh_table` | chaîne | `RefreshToken` | Nom de la table système contenant les refresh tokens valides (liste blanche). |
| `revoked_table` | chaîne | `RevokedToken` | Nom de la table système contenant les access tokens explicitement révoqués (liste noire). |
| `cache` | bloc | activé | Configuration du cache local de la liste noire. |
| `rotation` | bloc | activé | Configuration de la rotation des refresh tokens. |
| `auto_cleanup` | bloc | activé | Configuration du nettoyage périodique. |

### Sous-bloc `cache`

```yaml
cache:
  enabled: true
  ttl: "5m"
  max_size: 10000
```

| Clé | Type | Valeur par défaut | Description |
|---|---|---|---|
| `enabled` | booléen | `true` | Active le cache local des tokens révoqués. |
| `ttl` | durée | `5m` | Durée de mise en cache d'un résultat de vérification. |
| `max_size` | entier | `10000` | Nombre maximal d'entrées dans le cache. |

Le cache évite de requêter la base à chaque vérification d'access token, améliorant significativement les performances en charge.

### Sous-bloc `rotation`

```yaml
rotation:
  enabled: true
```

| Clé | Type | Valeur par défaut | Description |
|---|---|---|---|
| `enabled` | booléen | `true` | Si `true`, chaque appel à `/auth/refresh` invalide l'ancien refresh token et en émet un nouveau. |

La rotation des refresh tokens est une bonne pratique de sécurité : un refresh token volé ne peut être utilisé qu'une seule fois avant d'être détecté (le second usage échouera, indiquant une compromission).

### Sous-bloc `auto_cleanup`

```yaml
auto_cleanup:
  enabled: true
  interval: "1h"
  keep_revoked_for: "30d"
```

| Clé | Type | Valeur par défaut | Description |
|---|---|---|---|
| `enabled` | booléen | `true` | Active la suppression périodique des tokens expirés des tables système. |
| `interval` | durée | `1h` | Intervalle entre deux exécutions du nettoyage. |
| `keep_revoked_for` | durée | `30d` | Durée pendant laquelle un access token révoqué est conservé dans la liste noire après son expiration. |

### Tables système créées

Lorsque `enabled: true`, deux tables système sont automatiquement créées au démarrage du service :

- `RefreshToken` : contient les refresh tokens valides actuellement en circulation. Chaque entrée comporte le `jti` du token, l'identifiant de l'utilisateur, la date d'expiration et la date de création.
- `RevokedToken` : contient les access tokens explicitement révoqués. Chaque entrée comporte le `jti` du token, sa date d'expiration et la date de révocation.

Ces tables sont entièrement gérées par le système et n'ont pas à être déclarées manuellement dans le YAML.

### Comportement attendu avec tracking activé

| Évènement | Action sur les tables |
|---|---|
| `POST /auth/login` réussi | Insertion d'une entrée dans `RefreshToken`. |
| Requête vers route protégée | Vérification que l'access token n'est pas dans `RevokedToken` (avec cache). |
| `POST /auth/refresh` réussi | Vérification dans `RefreshToken`. Si `rotation.enabled: true`, suppression de l'ancien et insertion du nouveau. |
| `POST /auth/logout` | Insertion de l'access token dans `RevokedToken`, suppression du refresh token correspondant dans `RefreshToken`. |
| Tâche périodique (`auto_cleanup`) | Suppression des entrées dont l'expiration est passée depuis plus de `keep_revoked_for`. |

---

## 15. Utilisation des tokens dans les requêtes

### Via en-tête `Authorization`

Pour chaque requête vers une route protégée, transmettre l'access token dans l'en-tête HTTP :

```
Authorization: Bearer eyJhbGciOiJIUzI1NiIs...
```

Exemple en curl :

```bash
TOKEN=$(curl -s -X POST http://localhost:8081/auth/login \
  -d '{"email":"alice@example.com","mot_de_passe":"..."}' | jq -r .access_token)

curl http://localhost:8081/products \
  -H "Authorization: Bearer $TOKEN"
```

### Via cookies

Si `token_delivery` vaut `cookie` ou `both`, le navigateur transmet automatiquement les cookies à chaque requête vers le service.

Avec curl, transmettre explicitement le cookie :

```bash
curl http://localhost:8081/products \
  --cookie "sea_access=eyJhbGciOiJIUzI1NiIs..."
```

### Réponses en cas de token invalide

| Cas | Code retourné |
|---|---|
| Aucun token fourni | 401 |
| Token mal formé | 401 |
| Signature invalide | 401 |
| Token expiré | 401 |
| Token révoqué (si tracking activé) | 401 |

---

## 16. Format des durées

Toutes les durées de configuration (TTL, intervalles de cleanup, TTL du cache) sont déclarées sous forme de chaînes avec un suffixe d'unité.

### Suffixes acceptés

| Suffixe | Unité |
|---|---|
| `s` ou absent | secondes |
| `m` | minutes |
| `h` | heures |
| `d` | jours |

### Exemples

| Valeur YAML | Durée résultante |
|---|---|
| `"30s"` | 30 secondes |
| `"15m"` | 15 minutes |
| `"24h"` | 24 heures |
| `"7d"` | 7 jours |
| `"3600"` | 3600 secondes (1 heure) |

### Cas d'usage typiques

| Paramètre | Valeur recommandée |
|---|---|
| `access_token_ttl` | `15m` à `1h` |
| `refresh_token_ttl` | `7d` à `30d` |
| `cache.ttl` | `1m` à `5m` |
| `auto_cleanup.interval` | `1h` à `6h` |
| `auto_cleanup.keep_revoked_for` | `30d` (un mois) |

---

## 17. Codes de réponse

### Codes spécifiques à l'authentification

| Code | Routes concernées | Cause |
|---|---|---|
| 200 | `/auth/login`, `/auth/refresh`, `/auth/logout`, `/auth/me` | Opération réussie. |
| 201 | `/auth/register` | Compte créé. |
| 400 | Toutes | Validation échouée, champs manquants. |
| 401 | `/auth/login` | Email inconnu ou mot de passe incorrect. |
| 401 | `/auth/refresh` | Refresh token invalide, expiré, ou absent de la liste blanche. |
| 401 | `/auth/logout`, `/auth/me`, routes protégées | Access token absent, invalide, expiré, ou révoqué. |
| 409 | `/auth/register` | Contrainte d'unicité violée (email déjà utilisé). |

### Format des erreurs

```json
{
  "error": "Authentication failed",
  "details": "Invalid email or password"
}
```

---

## 18. Exemples de configurations

### Configuration 1 — Authentification simple en développement

```yaml
security:
  authentication:
    type: jwt
    algorithm: HS256
    secret: ""              # généré automatiquement et persisté
    access_token_ttl: "1h"
    refresh_token_ttl: "7d"
    token_delivery: body
```

### Configuration 2 — Application web avec cookies HttpOnly

```yaml
security:
  authentication:
    type: jwt
    algorithm: HS256
    secret: "${JWT_SECRET}"
    access_token_ttl: "15m"
    refresh_token_ttl: "14d"
    token_delivery: cookie
    cookie:
      domain: ".example.com"
      path: "/"
      secure: true
      same_site: lax
      access_token_name: sea_access
      refresh_token_name: sea_refresh
```

### Configuration 3 — Service à haute sécurité avec tracking complet

```yaml
security:
  authentication:
    type: jwt
    algorithm: RS256
    public_key_path: "/etc/seadesktop/keys/jwt_public.pem"
    private_key_path: "/etc/seadesktop/keys/jwt_private.pem"
    issuer: "https://api.example.com"
    audience: "example-api"
    access_token_ttl: "10m"
    refresh_token_ttl: "7d"
    token_delivery: both
    cookie:
      domain: ".example.com"
      path: "/"
      secure: true
      same_site: strict
    token_tracking:
      enabled: true
      cache:
        enabled: true
        ttl: "2m"
        max_size: 50000
      rotation:
        enabled: true
      auto_cleanup:
        enabled: true
        interval: "30m"
        keep_revoked_for: "90d"
```

### Configuration 4 — API mobile avec tokens longue durée

```yaml
security:
  authentication:
    type: jwt
    algorithm: HS256
    secret: "${JWT_SECRET}"
    access_token_ttl: "1h"
    refresh_token_ttl: "90d"
    token_delivery: body
    token_tracking:
      enabled: true
      rotation:
        enabled: true
      auto_cleanup:
        enabled: true
        interval: "6h"
```

### Configuration 5 — Désactivation de l'authentification

Pour un service interne ou de développement sans authentification :

```yaml
security:
  authentication:
    type: none
```

Aucune route `/auth/*` n'est exposée. Les routes CRUD restent accessibles sans authentification.
