"""
test_middlewares_security.py — Lot F1 : security_headers middleware.

Le SecurityHeadersMiddleware applique des en-têtes HTTP de sécurité
sur chaque réponse. Avec safe_defaults (actif sans configuration
explicite dans le YAML), on attend :

  X-Frame-Options:           DENY
  X-Content-Type-Options:    nosniff
  Strict-Transport-Security: max-age=31536000; includeSubDomains
  Content-Security-Policy:   default-src 'self'
  Referrer-Policy:           strict-origin-when-cross-origin
  Permissions-Policy:        geolocation=(), microphone=(), camera=()

Ces headers protègent contre :
  - Clickjacking (X-Frame-Options)
  - MIME sniffing (X-Content-Type-Options)
  - Downgrade HTTP (HSTS)
  - XSS / injection (CSP)
  - Leak referrer cross-origin (Referrer-Policy)
  - Abus de permissions navigateur (Permissions-Policy)

Tests :
  - Présence et valeurs sur /health (pas d'auth)
  - Présence sur une route authentifiée (login)
  - Présence sur une réponse d'erreur (404)
  - Présence sur POST (création)
  - Présence sur une route protégée avec ABAC
"""

from __future__ import annotations

import uuid

import pytest


# ─── Helpers ──────────────────────────────────────────────────

EXPECTED_HEADERS = {
    "X-Frame-Options":           "DENY",
    "X-Content-Type-Options":    "nosniff",
    "Strict-Transport-Security": "max-age=31536000; includeSubDomains",
    "Content-Security-Policy":   "default-src 'self'",
    "Referrer-Policy":           "strict-origin-when-cross-origin",
    "Permissions-Policy":        "geolocation=(), microphone=(), camera=()",
}


def _assert_security_headers(response, context: str = ""):
    """Verifie que tous les security headers attendus sont présents
    avec leurs valeurs canoniques. context : description de la
    requête pour faciliter le debug."""
    for header_name, expected_value in EXPECTED_HEADERS.items():
        actual = response.headers.get(header_name)
        assert actual is not None, (
            f"[{context}] Header de sécurité manquant : {header_name}. "
            f"Headers reçus : {dict(response.headers)}"
        )
        assert actual == expected_value, (
            f"[{context}] Header {header_name} : attendu "
            f"{expected_value!r}, reçu {actual!r}"
        )


def _bearer(token: str) -> dict:
    return {"Authorization": f"Bearer {token}"}


# ─── Fixture : un user authentifié (pour les tests qui en ont besoin) ─

@pytest.fixture
def auth_token(api, unique_email):
    password = "Sup3rSecret!"
    reg = api.post("/auth/register",
                   json={"email": unique_email, "password": password})
    assert reg.status_code in (200, 201), reg.text

    login = api.post("/auth/login",
                     json={"email": unique_email, "password": password})
    assert login.status_code == 200, login.text
    return login.json()["access_token"]


# ─── Tests ────────────────────────────────────────────────────

def test_security_headers_sur_health(api):
    """/health est public, sans auth. Vérifie que les headers de
    sécurité sont quand même présents (la baseline doit s'appliquer
    avant même l'auth)."""
    resp = api.get("/health")
    assert resp.status_code == 200, resp.text
    _assert_security_headers(resp, context="GET /health (public)")


def test_security_headers_sur_login_reussi(api, unique_email):
    """Une route POST publique (login). Vérifie que les headers sont
    là même sur une réponse de création de session."""
    password = "Sup3rSecret!"
    # Setup : créer l'utilisateur
    reg = api.post("/auth/register",
                   json={"email": unique_email, "password": password})
    assert reg.status_code in (200, 201), reg.text

    # Login : doit aussi avoir les headers de sécurité
    login = api.post("/auth/login",
                     json={"email": unique_email, "password": password})
    assert login.status_code == 200, login.text
    _assert_security_headers(login, context="POST /auth/login")


def test_security_headers_sur_route_protegee(api, auth_token):
    """Une route protégée (GET /teams). Vérifie que les headers sont
    là après le passage par le middleware d'auth."""
    resp = api.get("/teams", headers=_bearer(auth_token))
    assert resp.status_code == 200, resp.text
    _assert_security_headers(resp, context="GET /teams (auth)")


def test_security_headers_sur_404(api, auth_token):
    """Réponse d'erreur (404). Les headers de sécurité ne doivent
    pas être omis sur les erreurs — un attaquant pourrait sinon
    exploiter une page d'erreur sans protection."""
    bidon_id = str(uuid.uuid4())
    resp = api.get(f"/teams/{bidon_id}", headers=_bearer(auth_token))
    assert resp.status_code == 404, resp.text
    _assert_security_headers(resp, context="GET /teams/{bidon} (404)")


def test_security_headers_sur_401(api):
    """Réponse 401 (route protégée sans auth). Les headers de sécurité
    doivent aussi y être."""
    resp = api.get("/teams")  # pas de Authorization header
    assert resp.status_code == 401, resp.text
    _assert_security_headers(resp, context="GET /teams (401)")
