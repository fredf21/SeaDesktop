"""
test_crud_m2m.py — Lot D : relations many-to-many (Project ↔ Tag).

La relation M2M est déclarée sur Tag dans le YAML, donc seules les
routes côté Tag sont exposées :

  POST   /tags/{id}/projects/{target_id}   AttachManyToManyHandler
  DELETE /tags/{id}/projects/{target_id}   DetachManyToManyHandler
  GET    /tags/{id}/projects               ListManyToManyHandler

Convention :
  - 'id' (source) = id du Tag
  - 'target_id'   = id du Project
  - Pivot table   = project_tags(tag_id, project_id)

Tests couverts :
  - Attach nominal + vérification via list
  - Detach + vérification via list
  - Attach avec source/target inexistant → 404
  - Attach de la même paire deux fois (idempotence ou conflit ?)
"""

from __future__ import annotations

import uuid

import pytest


# ─── Helpers ──────────────────────────────────────────────────

def _bearer(token: str) -> dict:
    return {"Authorization": f"Bearer {token}"}


def _extract_id(response) -> str:
    body = response.json()
    assert "id" in body, f"pas d'id dans la réponse : {body}"
    return body["id"]


def _create_team(api, token, suffix: str = "") -> str:
    name = f"Team-m2m-{suffix}-{uuid.uuid4().hex[:8]}"
    resp = api.post("/teams", headers=_bearer(token), json={"name": name})
    assert resp.status_code in (200, 201), f"setup team: {resp.text}"
    return _extract_id(resp)


def _create_project(api, token, team_id: str, suffix: str = "") -> str:
    title = f"Project-m2m-{suffix}-{uuid.uuid4().hex[:8]}"
    resp = api.post("/projects",
                    headers=_bearer(token),
                    json={"title": title, "team_id": team_id})
    assert resp.status_code in (200, 201), f"setup project: {resp.text}"
    return _extract_id(resp)


def _create_tag(api, token, suffix: str = "") -> str:
    name = f"Tag-{suffix}-{uuid.uuid4().hex[:8]}"
    resp = api.post("/tags", headers=_bearer(token), json={"name": name})
    assert resp.status_code in (200, 201), f"setup tag: {resp.text}"
    return _extract_id(resp)


# ─── Fixture : un user authentifié ────────────────────────────

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


# ─── Tests M2M ────────────────────────────────────────────────

def test_attach_project_to_tag_reussit(api, auth_token):
    """POST /tags/{tag_id}/projects/{project_id} → 200/201.
    Cas nominal : un tag et un project existent, on les relie."""
    team_id = _create_team(api, auth_token, "attach")
    project_id = _create_project(api, auth_token, team_id, "attach")
    tag_id = _create_tag(api, auth_token, "attach")

    resp = api.post(f"/tags/{tag_id}/projects/{project_id}",
                    headers=_bearer(auth_token))
    assert resp.status_code in (200, 201, 204), (
        f"Attach nominal aurait dû réussir, "
        f"reçu {resp.status_code} : {resp.text}"
    )


def test_list_projects_of_tag_apres_attach(api, auth_token):
    """Après un attach, GET /tags/{id}/projects doit contenir le project.
    Vérifie que l'attach a effectivement créé l'entrée dans le pivot
    et que la liste M2M la fait apparaître."""
    team_id = _create_team(api, auth_token, "list")
    project_id = _create_project(api, auth_token, team_id, "list")
    tag_id = _create_tag(api, auth_token, "list")

    # Avant attach : la liste doit être vide
    list_before = api.get(f"/tags/{tag_id}/projects",
                          headers=_bearer(auth_token))
    assert list_before.status_code == 200, list_before.text
    projects_before = list_before.json()
    assert isinstance(projects_before, list), (
        f"GET M2M doit renvoyer une liste JSON, reçu : {projects_before}"
    )
    assert len(projects_before) == 0, (
        f"Avant attach, la liste devrait être vide : {projects_before}"
    )

    # Attach
    attach = api.post(f"/tags/{tag_id}/projects/{project_id}",
                      headers=_bearer(auth_token))
    assert attach.status_code in (200, 201, 204), attach.text

    # Après attach : le project doit apparaître
    list_after = api.get(f"/tags/{tag_id}/projects",
                         headers=_bearer(auth_token))
    assert list_after.status_code == 200, list_after.text
    projects_after = list_after.json()
    assert isinstance(projects_after, list), (
        f"GET M2M doit renvoyer une liste JSON après attach : {projects_after}"
    )
    assert len(projects_after) == 1, (
        f"Après 1 attach, la liste devrait avoir 1 element : {projects_after}"
    )

    # Vérifier que c'est bien LE bon project
    returned_ids = [p.get("id") for p in projects_after]
    assert project_id in returned_ids, (
        f"Le project attaché n'apparait pas dans la liste : "
        f"attendu {project_id}, reçu {returned_ids}"
    )


def test_detach_project_from_tag_reussit(api, auth_token):
    """DELETE /tags/{id}/projects/{target_id} → 200/204, puis
    GET /tags/{id}/projects ne doit plus contenir le project."""
    team_id = _create_team(api, auth_token, "detach")
    project_id = _create_project(api, auth_token, team_id, "detach")
    tag_id = _create_tag(api, auth_token, "detach")

    # Attach d'abord
    attach = api.post(f"/tags/{tag_id}/projects/{project_id}",
                      headers=_bearer(auth_token))
    assert attach.status_code in (200, 201, 204), attach.text

    # Detach
    detach = api.delete(f"/tags/{tag_id}/projects/{project_id}",
                        headers=_bearer(auth_token))
    assert detach.status_code in (200, 204), (
        f"Detach aurait dû réussir, reçu {detach.status_code} : {detach.text}"
    )

    # La liste doit être vide après detach
    list_after = api.get(f"/tags/{tag_id}/projects",
                         headers=_bearer(auth_token))
    assert list_after.status_code == 200, list_after.text
    projects_after = list_after.json()
    assert len(projects_after) == 0, (
        f"Après detach, la liste devrait être vide : {projects_after}"
    )


def test_attach_avec_tag_inexistant_404(api, auth_token):
    """POST /tags/{tag_bidon}/projects/{project_id} → 404.
    Le AttachManyToManyHandler doit vérifier l'existence du Tag (source)
    avant de toucher au pivot."""
    team_id = _create_team(api, auth_token, "bad-tag")
    project_id = _create_project(api, auth_token, team_id, "bad-tag")

    bidon_tag_id = str(uuid.uuid4())
    resp = api.post(f"/tags/{bidon_tag_id}/projects/{project_id}",
                    headers=_bearer(auth_token))
    assert resp.status_code == 404, (
        f"Attach avec tag_id inexistant aurait dû renvoyer 404, "
        f"reçu {resp.status_code} : {resp.text}"
    )


def test_attach_avec_project_inexistant_404(api, auth_token):
    """POST /tags/{tag_id}/projects/{project_bidon} → 404.
    Le AttachManyToManyHandler doit vérifier l'existence du Project
    (target) avant de toucher au pivot."""
    tag_id = _create_tag(api, auth_token, "bad-project")

    bidon_project_id = str(uuid.uuid4())
    resp = api.post(f"/tags/{tag_id}/projects/{bidon_project_id}",
                    headers=_bearer(auth_token))
    assert resp.status_code == 404, (
        f"Attach avec project_id inexistant aurait dû renvoyer 404, "
        f"reçu {resp.status_code} : {resp.text}"
    )
