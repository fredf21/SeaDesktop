"""
test_middlewares_cors.py — Lot F2 : CORS middleware.

Le middleware CORS applique les règles du Cross-Origin Resource
Sharing aux requêtes ayant un header Origin. Avec la config :

  cors:
    allowed_origins:   [http://localhost:3000, https://app.example.com]
    allowed_methods:   [GET, POST, PUT, DELETE, OPTIONS]
    allowed_headers:   [Content-Type, Authorization]
    exposed_headers:   [X-Total-Count]
    allow_credentials: true
    max_age: 1h

On attend :
  - Requête simple (GET, POST, etc.) avec Origin autorisée :
      → response contient Access-Control-Allow-Origin: <origin>
      → Access-Control-Allow-Credentials: true
      → Access-Control-Expose-Headers: X-Total-Count
      → Vary: Origin (pour cache CDN)
  - Requête simple avec Origin NON autorisée :
      → response SANS header Access-Control-Allow-Origin
      → (le navigateur bloquera, mais le serveur ne refuse pas
         spécialement la requête côté HTTP — comportement standard)
  - Requête preflight (OPTIONS + Access-Control-Request-Method) avec
    origin/méthode autorisées :
      → 200 ou 204
      → Access-Control-Allow-Origin: <origin>
      → Access-Control-Allow-Methods: <liste>
      → Access-Control-Allow-Headers: <liste>
      → Access-Control-Max-Age: 3600
  - Requête sans Origin (cas backend-to-backend, curl) :
      → pas de headers CORS dans la réponse

CORS, c'est la première ligne de défense d'un navigateur contre les
attaques cross-origin. Si le middleware se trompe (par exemple
écho de n'importe quelle Origin), n'importe quel site malveillant
peut faire des requêtes authentifiées vers ton API au nom d'un
utilisateur connecté.
"""

from __future__ import annotations

import pytest
import requests


# ─── Constantes ───────────────────────────────────────────────

ALLOWED_ORIGIN_DEV  = "http://localhost:3000"
ALLOWED_ORIGIN_PROD = "https://app.example.com"
EVIL_ORIGIN         = "http://evil.com"


# ─── Tests requêtes simples ───────────────────────────────────

def test_cors_get_avec_origin_autorisee(api):
    """GET /health avec Origin autorisée → response contient
    Access-Control-Allow-Origin: <l'origine demandée>."""
    resp = api.get("/health", headers={"Origin": ALLOWED_ORIGIN_DEV})
    assert resp.status_code == 200, resp.text

    acao = resp.headers.get("Access-Control-Allow-Origin")
    assert acao is not None, (
        f"Origin {ALLOWED_ORIGIN_DEV} autorisée mais Access-Control-"
        f"Allow-Origin absent. Headers : {dict(resp.headers)}"
    )
    # La spec autorise soit l'écho exact de Origin, soit "*"
    # (mais "*" est incompatible avec allow_credentials=true).
    assert acao == ALLOWED_ORIGIN_DEV, (
        f"Access-Control-Allow-Origin devrait être {ALLOWED_ORIGIN_DEV} "
        f"(echo de Origin), reçu : {acao!r}"
    )


def test_cors_get_avec_origin_prod_autorisee(api):
    """Même test que ci-dessus mais avec la 2e origine autorisée
    (la liste contient plusieurs origines, on vérifie que chacune
    matche individuellement)."""
    resp = api.get("/health", headers={"Origin": ALLOWED_ORIGIN_PROD})
    assert resp.status_code == 200, resp.text

    acao = resp.headers.get("Access-Control-Allow-Origin")
    assert acao == ALLOWED_ORIGIN_PROD, (
        f"Access-Control-Allow-Origin devrait être {ALLOWED_ORIGIN_PROD}, "
        f"reçu : {acao!r}"
    )


def test_cors_get_avec_origin_non_autorisee(api):
    """GET /health avec Origin évil → response sans header CORS.
    Le serveur ne refuse pas la requête HTTP (pas 403), c'est le
    navigateur qui bloquera la réponse côté client. Mais l'API
    NE DOIT PAS faire écho à l'origine non autorisée — sinon les
    règles CORS ne servent à rien."""
    resp = api.get("/health", headers={"Origin": EVIL_ORIGIN})

    # La requête doit toujours réussir au niveau HTTP
    assert resp.status_code == 200, resp.text

    acao = resp.headers.get("Access-Control-Allow-Origin")
    # Comportement attendu : ABSENCE du header (ou alors une valeur
    # qui n'autorise PAS l'origine évil)
    assert acao != EVIL_ORIGIN, (
        f"FAILLE CORS : le serveur a écho de l'origine non autorisée "
        f"{EVIL_ORIGIN!r} dans Access-Control-Allow-Origin. Cela "
        f"permet à n'importe quel site malveillant d'accéder à l'API."
    )
    assert acao != "*", (
        f"FAILLE CORS : Access-Control-Allow-Origin = '*' alors qu'une "
        f"liste explicite est configurée. Incompatible avec credentials."
    )

def test_cors_get_avec_origin_non_autorisee(api):
    """Mode strict configuré dans le YAML (origin_policy: strict) :
    une Origin évil reçoit 403 immédiat avec un message clair.
    
    En mode permissive (défaut), le serveur renverrait 200 sans
    header CORS, laissant le navigateur bloquer. Le mode strict
    refuse direct et offre un meilleur DX pour le frontend dev
    (l'erreur est visible côté serveur, pas seulement un mystère
    côté navigateur)."""
    resp = api.get("/health", headers={"Origin": EVIL_ORIGIN})
    
    assert resp.status_code == 403, (
        f"Mode strict configuré dans YAML : Origin évil aurait dû "
        f"être refusée en 403, reçu {resp.status_code} : {resp.text}"
    )
    
    body = resp.json()
    assert "cors_forbidden" in body.get("error", "") or \
           "origin" in body.get("message", "").lower(), (
        f"Le message d'erreur 403 ne mentionne pas CORS/Origin : {body}"
    )
           
def test_cors_credentials_avec_origin_autorisee(api):
    """Avec allow_credentials=true, le header Access-Control-Allow-
    Credentials doit être renvoyé pour les origines autorisées (sinon
    le navigateur ne transmet pas les cookies/Authorization)."""
    resp = api.get("/health", headers={"Origin": ALLOWED_ORIGIN_DEV})
    assert resp.status_code == 200, resp.text

    acac = resp.headers.get("Access-Control-Allow-Credentials")
    assert acac is not None and acac.lower() == "true", (
        f"allow_credentials=true mais Access-Control-Allow-"
        f"Credentials absent ou faux. Reçu : {acac!r}"
    )


def test_cors_vary_origin_sur_reponse(api):
    """Quand la response varie selon l'Origin, le header Vary doit
    inclure 'Origin'. Sans ça, un CDN/proxy pourrait servir la
    response d'un utilisateur à un autre avec une Origin différente
    (faille d'isolation)."""
    resp = api.get("/health", headers={"Origin": ALLOWED_ORIGIN_DEV})
    assert resp.status_code == 200, resp.text

    vary = resp.headers.get("Vary", "")
    assert "origin" in vary.lower(), (
        f"Vary devrait inclure 'Origin' pour éviter le cache "
        f"cross-origin. Reçu : Vary={vary!r}"
    )


# ─── Tests preflight (OPTIONS) ────────────────────────────────

def test_cors_preflight_methode_autorisee(api, base_url):
    """OPTIONS preflight avec Access-Control-Request-Method: PUT
    et Origin autorisée → réponse 200/204 avec les headers preflight.
    Note : on utilise requests directement parce que l'API helper
    ne fait pas OPTIONS."""
    resp = requests.options(
        f"{base_url}/teams",
        headers={
            "Origin": ALLOWED_ORIGIN_DEV,
            "Access-Control-Request-Method": "PUT",
            "Access-Control-Request-Headers": "Content-Type, Authorization",
        },
        timeout=5.0
    )

    # Preflight : 200 ou 204 selon le serveur
    assert resp.status_code in (200, 204), (
        f"Preflight devait renvoyer 200/204, reçu {resp.status_code} : "
        f"{resp.text}"
    )

    # Headers attendus
    acao = resp.headers.get("Access-Control-Allow-Origin")
    assert acao == ALLOWED_ORIGIN_DEV, (
        f"Preflight : ACAO attendu {ALLOWED_ORIGIN_DEV!r}, reçu {acao!r}"
    )

    acam = resp.headers.get("Access-Control-Allow-Methods", "")
    assert "PUT" in acam.upper(), (
        f"Preflight : PUT devrait être dans Access-Control-Allow-"
        f"Methods. Reçu : {acam!r}"
    )

    acah = resp.headers.get("Access-Control-Allow-Headers", "")
    # On a demandé "Content-Type, Authorization" — au moins l'un des
    # deux doit y figurer (insensible à la casse)
    assert "content-type" in acah.lower() or "authorization" in acah.lower(), (
        f"Preflight : headers demandés non reconnus. Reçu Access-"
        f"Control-Allow-Headers : {acah!r}"
    )


def test_cors_preflight_max_age(api, base_url):
    """Avec max_age: 1h configuré, le preflight doit renvoyer
    Access-Control-Max-Age: 3600 (le navigateur cachera la réponse
    preflight 1h, évitant des OPTIONS répétés)."""
    resp = requests.options(
        f"{base_url}/teams",
        headers={
            "Origin": ALLOWED_ORIGIN_DEV,
            "Access-Control-Request-Method": "GET",
        },
        timeout=5.0
    )
    assert resp.status_code in (200, 204), resp.text

    max_age = resp.headers.get("Access-Control-Max-Age")
    assert max_age is not None, (
        f"max_age=1h configuré mais Access-Control-Max-Age absent. "
        f"Headers : {dict(resp.headers)}"
    )
    assert max_age == "3600", (
        f"max_age=1h devrait donner Access-Control-Max-Age=3600, "
        f"reçu {max_age!r}"
    )


def test_cors_preflight_origin_non_autorisee(api, base_url):
    """Preflight avec Origin évil → le serveur peut renvoyer 200,
    mais pas les headers d'autorisation. Sans ces headers, le
    navigateur bloquera la requête réelle qui suivrait."""
    resp = requests.options(
        f"{base_url}/teams",
        headers={
            "Origin": EVIL_ORIGIN,
            "Access-Control-Request-Method": "GET",
        },
        timeout=5.0
    )

    acao = resp.headers.get("Access-Control-Allow-Origin")
    assert acao != EVIL_ORIGIN, (
        f"Preflight évil : le serveur a écho de l'origine non "
        f"autorisée {EVIL_ORIGIN!r} dans ACAO. Faille CORS."
    )
    assert acao != "*", (
        f"Preflight : ACAO='*' incompatible avec credentials. Faille."
    )


# ─── Test sans Origin (cas backend-to-backend) ────────────────

def test_cors_pas_de_header_sans_origin(api):
    """Une requête sans header Origin (curl, backend-to-backend,
    test direct) ne doit recevoir AUCUN header CORS. Les CORS sont
    là pour le navigateur, pas pour les autres clients."""
    resp = api.get("/health")
    assert resp.status_code == 200, resp.text

    acao = resp.headers.get("Access-Control-Allow-Origin")
    assert acao is None, (
        f"Requête sans Origin : Access-Control-Allow-Origin devrait "
        f"être absent, reçu : {acao!r}"
    )
