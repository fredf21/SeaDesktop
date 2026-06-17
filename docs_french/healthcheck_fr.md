# Vérification de santé (Healthcheck)

L'API SeaDesktop expose deux endpoints distincts pour surveiller la
santé du service, conformément aux conventions des orchestrateurs
modernes (Kubernetes, Docker Swarm, load balancers).

## Distinction entre les deux endpoints

| Endpoint | Rôle | Probe k8s | Réponse |
|----------|------|-----------|---------|
| `GET /health` | Le process est vivant | `livenessProbe` | Toujours 200 si le serveur HTTP répond |
| `GET /health/ready` | Le service peut traiter une requête | `readinessProbe` | 200 si dépendances OK, 503 sinon |

Cette séparation est importante : si la base de données tombe
temporairement, le service ne doit pas être redémarré (le redémarrer
ne résoudra rien) mais il doit être retiré du pool de load balancing
jusqu'à ce que la connexion soit rétablie.

## GET /health (liveness)

Endpoint ultra-léger sans dépendance externe. Sert uniquement à
vérifier que le process tourne et que le serveur HTTP accepte les
requêtes.

### Requête

```http
GET /health HTTP/1.1
```

### Réponse

```http
HTTP/1.1 200 OK
Content-Type: application/json

{ "status": "RUNNING" }
```

### Usage

À utiliser comme `livenessProbe` k8s :

```yaml
livenessProbe:
  httpGet:
    path: /health
    port: 8000
  initialDelaySeconds: 5
  periodSeconds: 10
```

Si cet endpoint ne répond plus, c'est que le process est mort ou
bloqué et un redémarrage est justifié.

## GET /health/ready (readiness)

Vérifie que toutes les dépendances critiques du service répondent.

### Checks effectués

| Check | Méthode | Considéré OK si |
|-------|---------|-----------------|
| `database` | Transaction no-op (`BEGIN; COMMIT;`) | La transaction est commitée |
| `storage` | Appel `exists()` sur un path quelconque | Aucune exception levée |

Le check `storage` n'est présent que si le schéma déclare au moins
un champ `File`. Sinon, il n'apparaît pas dans la réponse.

### Requête

```http
GET /health/ready HTTP/1.1
```

### Réponse — service prêt

```http
HTTP/1.1 200 OK
Content-Type: application/json

{
  "status": "ready",
  "checks": {
    "database": "ok",
    "storage": "ok"
  },
  "timestamp": "2026-06-13T15:32:00.123Z"
}
```

### Réponse — service non prêt

```http
HTTP/1.1 503 Service Unavailable
Content-Type: application/json

{
  "status": "not_ready",
  "checks": {
    "database": "error: Lost connection to MySQL server",
    "storage": "ok"
  },
  "timestamp": "2026-06-13T15:32:00.123Z"
}
```

Le code `503 Service Unavailable` est le standard HTTP pour signaler
qu'un service est temporairement indisponible. Les orchestrateurs et
load balancers savent l'interpréter et retirent automatiquement le
pod du pool actif jusqu'au prochain check passant.

### Usage

À utiliser comme `readinessProbe` k8s :

```yaml
readinessProbe:
  httpGet:
    path: /health/ready
    port: 8000
  initialDelaySeconds: 10
  periodSeconds: 5
  failureThreshold: 3
```

Avec cette configuration, un service avec base de données temporairement
indisponible sera retiré du pool après 3 échecs consécutifs (15
secondes), et réintégré dès que `database` redevient `ok`.

## Considérations de sécurité

Les deux endpoints sont **publics** par défaut (pas d'authentification
requise). C'est intentionnel : les load balancers et orchestrateurs ne
disposent pas de tokens d'authentification.

Les informations exposées sont volontairement génériques :

- `/health` ne révèle rien d'autre que le statut HTTP
- `/health/ready` peut révéler des messages d'erreur internes (ex.
  "Lost connection to MySQL server") qui peuvent informer un attaquant
  sur l'infrastructure

Si cette exposition pose problème, exposez ces endpoints uniquement
sur un réseau privé (interface d'administration séparée, VPN) plutôt
qu'à travers le reverse proxy public.
