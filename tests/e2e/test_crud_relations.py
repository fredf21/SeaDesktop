"""
test_crud_relations.py — Lot E : routes relationnelles inverses.

La relation `has_many: projects` déclarée sur Team expose 3 routes
via le route_generator :

  GET /teams/{id}/projects                       ListByFkHandler
  GET /teams_with_projects/{id}                  GetWithChildrenHandler
  GET /projects/filter/with_team_name/{value}    ListByFkFieldHandler

Ces handlers étaient enregistrés avec .remainder() jusqu'au fix de
ce lot. Maintenant en build_match_rule_from_template, ils
devraient se comporter proprement.
"""

from __future__ import annotations

import uuid

import pytest


# ─── Helpers ──────────────────────────────────────────────────

def _bearer(token: str) -> dict:
    return {"Authorization": f"Bearer {token}"}


def _extract_id(response) -> str:
    body = response.json()
    assert "id" in body, f"pas d'id : {body}"
    return body["id"]


def _create_team(api, token, suffix: str = ""):
    """Retourne (team_id, team_name)."""
    name = f"Team-rel-{suffix}-{uuid.uuid4().hex[:8]}"
    resp = api.post("/teams", headers=_bearer(token), json={"name": name})
    assert resp.status_code in (200, 201), f"setup team: {resp.text}"
    return _extract_id(resp), name


def _create_project(api, token, team_id: str, suffix: str = "") -> str:
    title = f"Project-rel-{suffix}-{uuid.uuid4().hex[:8]}"
    resp = api.post("/projects",
                    headers=_bearer(token),
                    json={"title": title, "team_id": team_id})
    assert resp.status_code in (200, 201), f"setup project: {resp.text}"
    return _extract_id(resp)


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


# ─── ListByFk : GET /teams/{id}/projects ──────────────────────

def test_list_projects_of_team_vide(api, auth_token):
    """Team sans aucun project → liste vide.
    Vérifie aussi que la route GET /teams/{id} (Lot A) n'a pas été
    cassée par la route /teams/{id}/projects (bug 13c fixé)."""
    team_id, _ = _create_team(api, auth_token, "vide")

    resp = api.get(f"/teams/{team_id}/projects",
                   headers=_bearer(auth_token))
    assert resp.status_code == 200, resp.text

    body = resp.json()
    assert isinstance(body, list), (
        f"GET /teams/.../projects doit renvoyer une liste : {body}"
    )
    assert len(body) == 0, f"Aucun project, liste devrait être vide : {body}"


def test_list_projects_of_team_avec_data(api, auth_token):
    """Crée 2 projects sur une team → GET /teams/{id}/projects
    renvoie les 2."""
    team_id, _ = _create_team(api, auth_token, "data")

    p1 = _create_project(api, auth_token, team_id, "p1")
    p2 = _create_project(api, auth_token, team_id, "p2")

    resp = api.get(f"/teams/{team_id}/projects",
                   headers=_bearer(auth_token))
    assert resp.status_code == 200, resp.text

    body = resp.json()
    assert isinstance(body, list), f"liste attendue : {body}"
    assert len(body) == 2, f"2 projects créés, reçu {len(body)} : {body}"

    returned_ids = sorted([p.get("id") for p in body])
    expected_ids = sorted([p1, p2])
    assert returned_ids == expected_ids, (
        f"projects attendus : {expected_ids}, reçus : {returned_ids}"
    )


def test_list_projects_filtre_par_team(api, auth_token):
    """2 teams avec leurs projects respectifs. GET sur teamA ne doit
    renvoyer QUE les projects de teamA."""
    teamA_id, _ = _create_team(api, auth_token, "A")
    teamB_id, _ = _create_team(api, auth_token, "B")

    projA = _create_project(api, auth_token, teamA_id, "in-A")
    _create_project(api, auth_token, teamB_id, "in-B")

    resp = api.get(f"/teams/{teamA_id}/projects",
                   headers=_bearer(auth_token))
    assert resp.status_code == 200, resp.text

    body = resp.json()
    returned_ids = [p.get("id") for p in body]

    assert projA in returned_ids, (
        f"Project de teamA absent : attendu {projA}, reçu {returned_ids}"
    )
    assert len(body) == 1, (
        f"teamA n'a qu'1 project, reçu {len(body)} : {returned_ids}"
    )


# ─── GetWithChildren : GET /teams_with_projects/{id} ──────────

def test_team_with_projects_inclut_les_enfants(api, auth_token):
    """GET /teams_with_projects/{id} → objet team avec ses projects
    imbriqués (clé probablement 'projects', le nom de la relation)."""
    team_id, team_name = _create_team(api, auth_token, "nested")
    p1 = _create_project(api, auth_token, team_id, "nested-1")

    resp = api.get(f"/teams_with_projects/{team_id}",
                   headers=_bearer(auth_token))
    assert resp.status_code == 200, resp.text

    body = resp.json()
    assert isinstance(body, dict), (
        f"objet team imbriqué attendu, reçu : {body}"
    )

    # La team doit être présente
    assert body.get("id") == team_id, f"id manquant : {body}"
    assert body.get("name") == team_name, f"name manquant : {body}"

    # Les projects imbriqués (clé 'projects' = nom de la relation)
    projects = body.get("projects")
    assert projects is not None, (
        f"clé 'projects' attendue dans le body : {body}"
    )
    assert isinstance(projects, list), (
        f"'projects' doit être une liste : {projects}"
    )
    assert len(projects) == 1, (
        f"1 project attaché, reçu {len(projects)} : {projects}"
    )
    assert projects[0].get("id") == p1, (
        f"id du project imbriqué incorrect : attendu {p1}, "
        f"reçu {projects[0]}"
    )


# ─── ListByFkField : GET /projects/filter/with_team_name/{value} ─

def test_list_projects_by_team_name(api, auth_token):
    """Crée une team avec un name unique + 1 project dessus.
    GET /projects/filter/with_team_name/{name} doit le renvoyer."""
    team_id, team_name = _create_team(api, auth_token, "filter")
    p1 = _create_project(api, auth_token, team_id, "for-filter")

    resp = api.get(f"/projects/filter/with_team_name/{team_name}",
                   headers=_bearer(auth_token))
    assert resp.status_code == 200, resp.text

    body = resp.json()
    assert isinstance(body, list), f"liste attendue : {body}"
    assert len(body) >= 1, (
        f"Au moins 1 project attendu, reçu {len(body)} : {body}"
    )

    returned_ids = [p.get("id") for p in body]
    assert p1 in returned_ids, (
        f"Project créé absent du filtre : attendu {p1}, reçu {returned_ids}"
    )
