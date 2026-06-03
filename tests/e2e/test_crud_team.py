"""
test_crud_team.py — Lot A : CRUD nominal sur l'entité Team.

Team est l'entité la plus simple du YAML de test :
  - pas de relation
  - pas de champ File
  - juste id (uuid) + name (string, unique)

L'objectif de ce lot est de poser une fondation CRUD sur une entité
simple, AVANT de tester les cas plus complexes (BelongsTo, M2M,
restrictions on_delete).

Verbes couverts :
  POST   /teams           → 201 + record créé
  GET    /teams/{id}      → 200 + record (bug 7 routing)
  PUT    /teams/{id}      → 200 + record modifié (bug 9 update SQL)
  DELETE /teams/{id}      → 200 ou 204
  GET    /teams/{id}      → 404 après delete
"""

from __future__ import annotations

import uuid

import pytest


# ─── Helpers ──────────────────────────────────────────────────

def _bearer(token: str) -> dict:
    return {"Authorization": f"Bearer {token}"}


def _extract_id(response) -> str:
    """Récupère l'id du record créé. Le CreateHandler renvoie le
    record entier ; l'id y est sous 'id'."""
    body = response.json()
    assert "id" in body, f"pas d'id dans la réponse : {body}"
    return body["id"]


# ─── Fixture : un user authentifié ────────────────────────────

@pytest.fixture
def auth_token(api, unique_email):
    """Register + login, renvoie un access_token utilisable. Les
    routes CRUD sont protégées : un token est requis."""
    password = "Sup3rSecret!"
    reg = api.post("/auth/register",
                   json={"email": unique_email, "password": password})
    assert reg.status_code in (200, 201), reg.text

    login = api.post("/auth/login",
                     json={"email": unique_email, "password": password})
    assert login.status_code == 200, login.text
    return login.json()["access_token"]


# ─── Tests CRUD ───────────────────────────────────────────────

def test_create_team_reussit(api, auth_token):
    """POST /teams avec un name valide → 201 (ou 200) + id généré."""
    team_name = f"Team-{uuid.uuid4().hex[:8]}"
    resp = api.post("/teams",
                    headers=_bearer(auth_token),
                    json={"name": team_name})
    assert resp.status_code in (200, 201), resp.text

    body = resp.json()
    assert "id" in body, f"id absent : {body}"
    assert body.get("name") == team_name, f"name mal renvoyé : {body}"


def test_get_team_by_id_reussit(api, auth_token):
    """POST puis GET /teams/{id} → 200 + le même name."""
    team_name = f"Team-{uuid.uuid4().hex[:8]}"
    create = api.post("/teams",
                      headers=_bearer(auth_token),
                      json={"name": team_name})
    assert create.status_code in (200, 201), create.text
    team_id = _extract_id(create)

    get_resp = api.get(f"/teams/{team_id}", headers=_bearer(auth_token))
    assert get_resp.status_code == 200, get_resp.text

    body = get_resp.json()
    assert body.get("id") == team_id
    assert body.get("name") == team_name


def test_update_team_modifie_le_name(api, auth_token):
    """POST puis PUT /teams/{id} avec un nouveau name → 200 + name
    modifié. Exerce le fix bug 9 (GenericCrudEngine::update qui
    appelait l'UPDATE seulement si BelongsTo) sur entité sans
    BelongsTo. Ce cas est déjà couvert indirectement par les tests
    Document, mais on le réaffirme ici sur une entité 'pure'."""
    original_name = f"Team-{uuid.uuid4().hex[:8]}"
    create = api.post("/teams",
                      headers=_bearer(auth_token),
                      json={"name": original_name})
    team_id = _extract_id(create)

    new_name = f"Team-renamed-{uuid.uuid4().hex[:8]}"
    update = api.put(f"/teams/{team_id}",
                     headers=_bearer(auth_token),
                     json={"name": new_name})
    assert update.status_code == 200, update.text

    # Re-GET pour confirmer que la modification est persistée
    # (l'update aurait pu renvoyer 200 sans rien faire en base).
    get_resp = api.get(f"/teams/{team_id}", headers=_bearer(auth_token))
    assert get_resp.status_code == 200, get_resp.text
    assert get_resp.json().get("name") == new_name, (
        f"PUT a renvoyé 200 mais le record n'est pas modifié : {get_resp.json()}"
    )


def test_delete_team_supprime_et_get_404(api, auth_token):
    """POST puis DELETE /teams/{id} → 200/204, puis GET /teams/{id} → 404.
    Vérifie que le delete est bien effectif (pas juste un soft-delete
    silencieux ou un no-op qui renverrait 200)."""
    team_name = f"Team-{uuid.uuid4().hex[:8]}"
    create = api.post("/teams",
                      headers=_bearer(auth_token),
                      json={"name": team_name})
    team_id = _extract_id(create)

    delete_resp = api.delete(f"/teams/{team_id}",
                             headers=_bearer(auth_token))
    assert delete_resp.status_code in (200, 204), delete_resp.text

    # Le GET suivant doit donner 404 — sinon le delete n'a pas vraiment
    # supprimé.
    get_resp = api.get(f"/teams/{team_id}", headers=_bearer(auth_token))
    assert get_resp.status_code == 404, (
        f"Team supprimée toujours accessible : status={get_resp.status_code} "
        f"body={get_resp.text}"
    )
