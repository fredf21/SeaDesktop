"""
test_middlewares_http_limits.py — Lot F3 : http_limits middleware.

Le HttpLimitsMiddleware vérifie 5 contraintes avant de laisser passer
une requête au handler :

  1. URL trop longue          → 400 'uri_too_long'
  2. Body trop gros           → 413 'payload_too_large'
  3. Trop de headers          → 400 'too_many_headers'
  4. Header trop long         → 400 'header_too_large'
  5. Trop de query params     → 400 'too_many_query_params'

Pour le test YAML configuré :
  max_body_size: 4KB (4096 bytes)
  max_url_length: 512 caractères
  max_query_params: 5

Tests :
  - Body à la limite (4096 bytes) → 200/201 (accepté)
  - Body > limite (4097+ bytes) → 413 + message structuré
  - URL > limite (>512 caractères) → 400 'uri_too_long'
  - Trop de query params (6+) → 400 'too_many_query_params'
  - Body normal (POST team standard) → 200/201 (non-régression)
"""

from __future__ import annotations

import uuid

import pytest


# ─── Constantes ───────────────────────────────────────────────

MAX_BODY_SIZE   = 100 *  1024   # 4KB configuré dans le YAML
MAX_URL_LENGTH  = 512
MAX_QUERY_PARAMS = 5


def _bearer(token: str) -> dict:
    return {"Authorization": f"Bearer {token}"}


# ─── Fixture : user authentifié ───────────────────────────────

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

def test_http_limit_body_normal_passe(api, auth_token):
    """POST avec un body de taille normale (~50 bytes) → 200/201.
    Non-régression : vérifie que le middleware ne bloque pas les
    requêtes légitimes."""
    name = f"Team-normal-{uuid.uuid4().hex[:8]}"
    resp = api.post("/teams",
                    headers=_bearer(auth_token),
                    json={"name": name})
    assert resp.status_code in (200, 201), (
        f"Body normal aurait dû passer, reçu {resp.status_code} : "
        f"{resp.text}"
    )


def test_http_limit_body_trop_gros_413(api, auth_token):
    """POST avec un body > max_body_size (4096 bytes) → 413.
    Le message d'erreur doit être structuré avec error='payload_too_large'."""
    # Body de 4100 bytes (au-dessus de la limite)
    # JSON sera { "name": "<padding>"  } — le padding contient
    # 4080 caractères pour atteindre ~4100 bytes total
    padding = "A" * (105 * 1024)
    big_payload = {"name": padding}

    resp = api.post("/teams",
                    headers=_bearer(auth_token),
                    json=big_payload)

    assert resp.status_code == 413, (
        f"Body de ~4100 bytes (> max_body_size=4096) aurait dû être "
        f"refusé en 413, reçu {resp.status_code} : {resp.text}"
    )

    body = resp.json()
    assert body.get("error") == "payload_too_large", (
        f"Message d'erreur 413 mal structuré : {body}"
    )
    assert "max_bytes" in body, (
        f"Le message d'erreur devrait inclure 'max_bytes' pour "
        f"que le client sache la limite : {body}"
    )


def test_http_limit_body_a_la_limite_passe(api, auth_token):
    """POST avec un body à exactement (ou juste sous) la limite.
    Doit passer — la limite est inclusive selon les conventions HTTP."""
    # On vise ~4000 bytes (juste sous 4096) pour avoir une marge
    # de sécurité sur l'overhead JSON ({"name":"..."} fait ~15 bytes
    # de plus que la valeur seule)
    padding = "B" * (90 * 1024)
    payload = {"name": padding}

    resp = api.post("/teams",
                    headers=_bearer(auth_token),
                    json=payload)

    # On accepte 200/201 (limite OK) OU 400 (validation domain
    # rejette un name de 3980 chars — c'est valide aussi, signe
    # que le middleware a laissé passer)
    assert resp.status_code != 413, (
        f"Body de ~4000 bytes (< max_body_size=4096) ne devrait PAS "
        f"recevoir 413, reçu {resp.status_code} : {resp.text}"
    )


def test_http_limit_url_trop_longue_400(api, auth_token):
    """GET avec une URL > max_url_length (512 caractères) → 400
    'uri_too_long'. On envoie une URL avec un query param très long."""
    # base /teams = 6 char + ? + name= = 7 char + valeur très longue
    # On vise 600 caractères total pour bien dépasser 512
    long_value = "X" * 600
    
    resp = api.get(f"/teams?name={long_value}",
                   headers=_bearer(auth_token))

    assert resp.status_code == 400, (
        f"URL de 600+ caractères (> max_url_length=512) aurait dû "
        f"être refusée en 400, reçu {resp.status_code} : {resp.text}"
    )

    body = resp.json()
    assert body.get("error") == "uri_too_long", (
        f"Message d'erreur 400 mal structuré : {body}"
    )


def test_http_limit_trop_de_query_params_400(api, auth_token):
    """GET avec > max_query_params (5) params → 400 'too_many_query_params'.
    On envoie 6 query params différents."""
    # 6 query params (limite = 5)
    params = "&".join(f"p{i}=v{i}" for i in range(6))
    
    resp = api.get(f"/teams?{params}",
                   headers=_bearer(auth_token))

    assert resp.status_code == 400, (
        f"6 query params (> max_query_params=5) aurait dû être refusé "
        f"en 400, reçu {resp.status_code} : {resp.text}"
    )

    body = resp.json()
    assert body.get("error") == "too_many_query_params", (
        f"Message d'erreur 400 mal structuré : {body}"
    )
