# Erreurs HTTP

Toutes les erreurs renvoyées par l'API SeaDesktop suivent un schéma JSON
unifié, accompagné d'un code HTTP approprié et d'un code d'erreur
sémantique permettant un dispatch fiable côté client.

## Schéma de réponse

Toute réponse d'erreur (status >= 400) possède la forme suivante :

```json
{
  "success": false,
  "error": {
    "code": "VALIDATION_ERROR",
    "message": "L'email est invalide."
  }
}
```

- `success` est toujours `false` pour une réponse d'erreur.
- `error.code` est une chaîne en MAJUSCULES, stable, identifiant la
  catégorie d'erreur. C'est ce champ que le client doit utiliser pour
  router son comportement.
- `error.message` est un message lisible, en français, destiné à être
  affiché à l'utilisateur final ou loggé. Son contenu peut évoluer
  sans préavis et ne doit pas faire l'objet de parsing.

Les réponses de succès (status 2xx) ne suivent pas ce schéma : elles
renvoient directement la ressource demandée ou un message minimal selon
l'endpoint.

## Codes d'erreur

| Code HTTP | Code d'erreur          | Signification                                                  |
|-----------|------------------------|----------------------------------------------------------------|
| 400       | `BAD_REQUEST`          | Requête mal formée : paramètre manquant, JSON invalide, etc.   |
| 400       | `VALIDATION_ERROR`     | Le contenu est syntaxiquement correct mais sémantiquement invalide (email mal formaté, champ requis manquant, etc.). |
| 401       | `AUTHENTICATION_ERROR` | Token d'authentification manquant, invalide, expiré, ou identifiants erronés. |
| 403       | `AUTHORIZATION_ERROR`  | L'utilisateur est authentifié mais n'a pas le droit d'effectuer l'opération demandée. |
| 404       | `NOT_FOUND`            | La ressource demandée n'existe pas.                            |
| 409       | `CONFLICT`             | L'opération entre en conflit avec l'état actuel (doublon, contrainte de clé étrangère, ressource verrouillée). |
| 429       | `RATE_LIMIT_EXCEEDED`  | Trop de requêtes dans une fenêtre de temps donnée.             |
| 500       | `INTERNAL_SERVER_ERROR`| Erreur interne du serveur. Le détail est consigné côté serveur uniquement. |

### Distinction entre `BAD_REQUEST` et `VALIDATION_ERROR`

`BAD_REQUEST` indique un problème de **format** : un champ obligatoire
manque, le JSON est mal formé, un paramètre d'URL est absent.

`VALIDATION_ERROR` indique un problème de **contenu** : tous les champs
attendus sont présents mais leur valeur ne respecte pas les règles
métier (format d'email invalide, longueur insuffisante, valeur
non autorisée pour une énumération, etc.).

Dans les deux cas, le code HTTP est `400`. Le client peut utiliser le
code d'erreur pour différencier la cause et adapter son affichage
(message générique pour `BAD_REQUEST`, mise en surbrillance des champs
fautifs pour `VALIDATION_ERROR`).

## Exemples

### Création d'utilisateur avec email manquant

```http
POST /api/auth/register HTTP/1.1
Content-Type: application/json

{
  "password": "secret123"
}
```

Réponse :

```http
HTTP/1.1 400 Bad Request
Content-Type: application/json

{
  "success": false,
  "error": {
    "code": "VALIDATION_ERROR",
    "message": "Champ email manquant."
  }
}
```

### Login avec identifiants invalides

```http
POST /api/auth/login HTTP/1.1
Content-Type: application/json

{
  "email": "user@example.com",
  "password": "wrong_password"
}
```

Réponse :

```http
HTTP/1.1 401 Unauthorized
Content-Type: application/json

{
  "success": false,
  "error": {
    "code": "AUTHENTICATION_ERROR",
    "message": "Identifiants invalides."
  }
}
```

Le message est identique que l'email existe ou non, et que le mot de
passe soit incorrect ou que l'utilisateur soit introuvable. Cela évite
de divulguer la liste des comptes existants à un attaquant.

### Accès à une ressource d'un autre utilisateur (ABAC)

```http
GET /api/projects/abc-123 HTTP/1.1
Authorization: Bearer eyJ...
```

Réponse si la policy refuse l'accès :

```http
HTTP/1.1 403 Forbidden
Content-Type: application/json

{
  "success": false,
  "error": {
    "code": "AUTHORIZATION_ERROR",
    "message": "Le projet n'appartient pas à l'utilisateur courant."
  }
}
```

Le message peut être plus précis selon la règle ABAC qui s'est
appliquée. Si la règle ne fournit pas de raison, le message par défaut
est `"Acces refuse."`.

### Lecture d'une ressource inexistante

```http
GET /api/projects/does-not-exist HTTP/1.1
```

Réponse :

```http
HTTP/1.1 404 Not Found
Content-Type: application/json

{
  "success": false,
  "error": {
    "code": "NOT_FOUND",
    "message": "Enregistrement introuvable."
  }
}
```

### Création d'un email déjà utilisé

```http
POST /api/auth/register HTTP/1.1
Content-Type: application/json

{
  "email": "existing@example.com",
  "password": "secret123"
}
```

Réponse :

```http
HTTP/1.1 409 Conflict
Content-Type: application/json

{
  "success": false,
  "error": {
    "code": "CONFLICT",
    "message": "Cet email existe deja."
  }
}
```

### Suppression d'une ressource référencée ailleurs

Si une `Team` contient des `Project`s avec une règle `on_delete: restrict`
sur la clé étrangère, la suppression de la `Team` est refusée :

```http
DELETE /api/teams/team-123 HTTP/1.1
```

Réponse :

```http
HTTP/1.1 409 Conflict
Content-Type: application/json

{
  "success": false,
  "error": {
    "code": "CONFLICT",
    "message": "Suppression refusee : entite referencee (la table 'projects' referencé cette ligne)."
  }
}
```

### Création avec plusieurs erreurs de validation

Lorsque plusieurs erreurs sont détectées en même temps, les messages
sont concaténés et séparés par `; ` :

```http
HTTP/1.1 400 Bad Request
Content-Type: application/json

{
  "success": false,
  "error": {
    "code": "VALIDATION_ERROR",
    "message": "Le champ 'name' est requis; La longueur du champ 'description' depasse 500 caracteres"
  }
}
```

### Dépassement de la limite de requêtes

```http
HTTP/1.1 429 Too Many Requests
Content-Type: application/json
X-RateLimit-Limit: 100
X-RateLimit-Remaining: 0
X-RateLimit-Reset: 1721392800
Retry-After: 30

{
  "success": false,
  "error": {
    "code": "RATE_LIMIT_EXCEEDED",
    "message": "Trop de requetes. Reessayez plus tard."
  }
}
```

Les en-têtes `X-RateLimit-*` et `Retry-After` indiquent quand réessayer.

### Erreur interne

```http
HTTP/1.1 500 Internal Server Error
Content-Type: application/json

{
  "success": false,
  "error": {
    "code": "INTERNAL_SERVER_ERROR",
    "message": "Erreur interne du serveur"
  }
}
```

Le message est volontairement générique et identique pour toutes les
erreurs internes. Le détail technique (trace, message d'exception,
contexte de l'opération) est uniquement consigné dans les logs du
serveur, accessibles via l'endpoint `/api/logs` aux administrateurs.

## Traitement côté client

### Dispatch par code d'erreur

Le client doit s'appuyer sur `error.code` (et non sur le message ou le
code HTTP seul) pour décider de son comportement :

```javascript
async function callApi(url, options) {
  const response = await fetch(url, options);
  const body = await response.json();

  if (!response.ok) {
    switch (body.error.code) {
      case 'AUTHENTICATION_ERROR':
        redirectToLogin();
        break;
      case 'AUTHORIZATION_ERROR':
        showForbiddenPage(body.error.message);
        break;
      case 'VALIDATION_ERROR':
        showFormErrors(body.error.message);
        break;
      case 'NOT_FOUND':
        show404();
        break;
      case 'CONFLICT':
        showConflictDialog(body.error.message);
        break;
      case 'RATE_LIMIT_EXCEEDED':
        const retryAfter = response.headers.get('Retry-After');
        retryLater(retryAfter);
        break;
      case 'INTERNAL_SERVER_ERROR':
        showGenericError();
        reportToSupport();
        break;
      default:
        showGenericError(body.error.message);
    }
    throw new ApiError(body.error.code, body.error.message);
  }

  return body;
}
```

### Affichage du message

Le champ `error.message` peut contenir du texte traduit. Il convient pour
un affichage direct à l'utilisateur final, mais ne doit pas être analysé
ou utilisé pour prendre des décisions logiques. Utilisez `error.code`
pour cela.

### Distinction entre erreur réseau et erreur API

Une absence de réponse, un timeout, ou un statut HTTP sans corps JSON
valide indiquent une erreur réseau ou serveur. Le schéma documenté ici
ne s'applique qu'aux réponses dont le `Content-Type` est
`application/json` et qui contiennent un objet avec `success: false`.

## Contrat de stabilité

- Les **codes** (`BAD_REQUEST`, `VALIDATION_ERROR`, etc.) sont stables
  et ne seront pas renommés ni supprimés sans une version majeure.
- De nouveaux codes peuvent être ajoutés. Le client doit avoir une
  branche par défaut (`default:`) pour les codes inconnus.
- Les **messages** sont susceptibles d'évoluer (corrections
  typographiques, précisions, traductions) sans préavis.
- Les **codes HTTP** associés à chaque `error.code` sont stables.
