"""
test_middlewares_rate_limit.py — Lot F4 : rate_limit middleware.

Config YAML :
  rate_limits:
    - scope: per_user
      requests: 5
      window: 10s
      burst: 20

Le burst est genereux (20) pour ne pas declencher 429 dans les tests
CRUD multi-requetes de la suite. Pour declencher le 429 dans nos
tests F4, on epuise activement le bucket dans une boucle qui peut
aller au-dela de la capacite (le rate_limit_middleware consomme
aussi des tokens via les fixtures auth_token, donc le nombre exact
de requetes a envoyer est imprevisible).

Strategie : on envoie jusqu'a 40 requetes (largement > burst*2), et
on s'attend a voir un 429 en cours de route. La premiere fois qu'on
recoit 429, on capture les details pour les verifications.
"""

from __future__ import annotations

import uuid

import pytest
import requests


# ─── Constantes ───────────────────────────────────────────────

BUCKET_BURST    = 20   # doit correspondre au burst dans le YAML
MAX_ATTEMPTS    = 40   # tentatives max avant d'abandonner


# ─── Fixtures ─────────────────────────────────────────────────

@pytest.fixture
def auth_token(api, unique_email):
    """Cree un user unique et retourne son access_token."""
    password = "Sup3rSecret!"
    reg = api.post("/auth/register",
                   json={"email": unique_email, "password": password})
    assert reg.status_code in (200, 201), reg.text

    login = api.post("/auth/login",
                     json={"email": unique_email, "password": password})
    assert login.status_code == 200, login.text
    return login.json()["access_token"]


def _bearer(token: str) -> dict:
    return {"Authorization": f"Bearer {token}"}


def _send_until_429(base_url: str, headers: dict, max_attempts: int = MAX_ATTEMPTS):
    """Envoie des requetes /auth/me jusqu'a recevoir un 429.
    Retourne (responses_200_count, response_429) si trouve, sinon
    lance AssertionError."""
    successes = 0
    for i in range(max_attempts):
        resp = requests.get(f"{base_url}/auth/me",
                            headers=headers, timeout=2.0)
        if resp.status_code == 429:
            return successes, resp
        if resp.status_code == 200:
            successes += 1
        else:
            raise AssertionError(
                f"Requete {i+1} a recu un statut inattendu : "
                f"{resp.status_code} : {resp.text}"
            )
    raise AssertionError(
        f"Apres {max_attempts} requetes, jamais recu 429. "
        f"Le bucket aurait du etre epuise (burst={BUCKET_BURST})."
    )


# ─── Tests ────────────────────────────────────────────────────

def test_rate_limit_sous_la_limite_passe(base_url, auth_token):
    """3 requetes auth sous burst : toutes 2xx + headers X-RateLimit-*."""
    headers = _bearer(auth_token)

    for i in range(3):
        resp = requests.get(f"{base_url}/auth/me", headers=headers, timeout=2.0)
        assert resp.status_code == 200, (
            f"Requete {i+1}/3 sous la limite aurait du passer, "
            f"recu {resp.status_code} : {resp.text}"
        )

        limit = resp.headers.get("X-RateLimit-Limit")
        assert limit == str(BUCKET_BURST), (
            f"X-RateLimit-Limit attendu {BUCKET_BURST}, recu {limit!r}"
        )

        remaining = resp.headers.get("X-RateLimit-Remaining")
        assert remaining is not None, (
            f"X-RateLimit-Remaining absent. Headers : {dict(resp.headers)}"
        )


def test_rate_limit_depasse_renvoie_429(base_url, auth_token):
    """Epuise le bucket : on doit finir par recevoir un 429."""
    headers = _bearer(auth_token)
    successes, resp_429 = _send_until_429(base_url, headers)

    # On a fait au moins 1 requete reussie avant le 429
    assert successes >= 1, (
        f"Aucune requete n'a reussi avant le 429 (suspect)"
    )

    body = resp_429.json()
    assert body.get("error") == "rate_limit_exceeded", (
        f"Message d'erreur 429 mal structure : {body}"
    )
    assert "retry_after_seconds" in body, (
        f"Le body 429 devrait contenir 'retry_after_seconds' : {body}"
    )


def test_rate_limit_429_contient_les_headers_attendus(base_url, auth_token):
    """Au 429 : headers Retry-After / X-RateLimit-Limit /
    X-RateLimit-Remaining=0 / X-RateLimit-Reset."""
    headers = _bearer(auth_token)
    _, resp = _send_until_429(base_url, headers)

    assert resp.headers.get("Retry-After") is not None, (
        f"Retry-After absent du 429. Headers : {dict(resp.headers)}"
    )
    assert resp.headers.get("X-RateLimit-Limit") == str(BUCKET_BURST), (
        f"X-RateLimit-Limit incorrect : {resp.headers.get('X-RateLimit-Limit')}"
    )
    assert resp.headers.get("X-RateLimit-Remaining") == "0", (
        f"X-RateLimit-Remaining devrait etre '0' au 429, recu "
        f"{resp.headers.get('X-RateLimit-Remaining')!r}"
    )
    assert resp.headers.get("X-RateLimit-Reset") is not None, (
        f"X-RateLimit-Reset absent du 429"
    )


def test_rate_limit_decremente_remaining(base_url, auth_token):
    """X-RateLimit-Remaining decremente a chaque requete."""
    headers = _bearer(auth_token)

    remaining_values = []
    for i in range(3):
        resp = requests.get(f"{base_url}/auth/me", headers=headers, timeout=2.0)
        assert resp.status_code == 200, resp.text

        rem = resp.headers.get("X-RateLimit-Remaining")
        assert rem is not None
        remaining_values.append(int(rem))

    assert remaining_values[0] > remaining_values[1] > remaining_values[2], (
        f"X-RateLimit-Remaining devrait decrementer, recu : "
        f"{remaining_values}"
    )


def test_rate_limit_isolation_entre_users(base_url, api):
    """Deux users → deux buckets isoles."""
    
    # User 1
    email_1 = f"u1-{uuid.uuid4().hex[:8]}@test.local"
    api.post("/auth/register",
             json={"email": email_1, "password": "Sup3rSecret!"})
    login_1 = api.post("/auth/login",
                       json={"email": email_1, "password": "Sup3rSecret!"})
    token_1 = login_1.json()["access_token"]

    # User 2
    email_2 = f"u2-{uuid.uuid4().hex[:8]}@test.local"
    api.post("/auth/register",
             json={"email": email_2, "password": "Sup3rSecret!"})
    login_2 = api.post("/auth/login",
                       json={"email": email_2, "password": "Sup3rSecret!"})
    token_2 = login_2.json()["access_token"]

    # Epuise le bucket de user 1
    _send_until_429(base_url, _bearer(token_1))

    # User 2 : doit pouvoir faire au moins 1 requete reussie
    r2 = requests.get(f"{base_url}/auth/me",
                      headers=_bearer(token_2), timeout=2.0)
    assert r2.status_code == 200, (
        f"User 2 a son propre bucket, sa requete aurait du passer, "
        f"recu {r2.status_code} : {r2.text}"
    )
