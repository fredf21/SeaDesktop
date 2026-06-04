"""
test_crud_constraints.py — Lot C : contraintes et cas limites.

Tests qui exercent les protections du backend contre les entrées
invalides ou les opérations interdites par le schéma.

Cas couverts :
  - champ 'required: true' manquant à la création
  - FK 'required: true' manquante à la création
  - contrainte 'unique: true' violée
  - on_delete=restrict : DELETE d'un Team référencé par un Project

C'est typiquement le lot qui révèle les bugs latents : un système
qui marche en cas nominal peut être permissif aux entrées invalides
ou avoir un comportement incohérent sur les contraintes inter-entités.
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


def _create_team(api, token, name_suffix: str = "") -> str:
    """Helper : crée une Team et renvoie son id."""
    name = f"Team-{name_suffix}-{uuid.uuid4().hex[:8]}"
    resp = api.post("/teams",
                    headers=_bearer(token),
                    json={"name": name})
    assert resp.status_code in (200, 201), (
        f"setup : création Team échouée : {resp.text}"
    )
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


# ─── Tests contraintes ────────────────────────────────────────

def test_create_team_sans_name_echoue(api, auth_token):
    """POST /teams sans champ 'name' (qui est required: true) doit
    être refusé en 400. PAS 201 (création silencieuse d'une Team
    sans nom), PAS 500 (erreur serveur sur un input client invalide).

    Note : on accepte aussi un body avec name vide ("") — la
    sémantique 'required' inclut souvent la non-vacuité, mais ce
    n'est pas garanti. Le test principal couvre l'absence totale
    du champ."""
    resp = api.post("/teams",
                    headers=_bearer(auth_token),
                    json={})
    assert resp.status_code == 400, (
        f"POST /teams sans name aurait dû échouer en 400, "
        f"reçu {resp.status_code} : {resp.text}"
    )


def test_create_project_sans_team_id_echoue(api, auth_token):
    """POST /projects sans 'team_id' (FK required: true) doit être
    refusé. C'est la FK 'required' qui devrait déclencher la
    validation, soit via le validateur générique (champ manquant),
    soit via la branche Restrict du fix bug 10/11
    (relation.on_delete == Restrict)."""
    project_title = f"Project-{uuid.uuid4().hex[:8]}"
    resp = api.post("/projects",
                    headers=_bearer(auth_token),
                    json={"title": project_title})
    assert resp.status_code == 400, (
        f"POST /projects sans team_id aurait dû échouer en 400, "
        f"reçu {resp.status_code} : {resp.text}"
    )


def test_create_team_nom_duplique_echoue(api, auth_token):
    """Le champ name est 'unique: true'. Deux Teams avec le même
    name doivent provoquer une erreur sur la seconde. Le code
    attendu est probablement 409 (Conflict) ou 400 (Bad Request).

    Le fix bug 11 (do_with) couvre la branche unique check du
    create — ce test exerce explicitement ce chemin."""
    shared_name = f"Team-shared-{uuid.uuid4().hex[:8]}"

    first = api.post("/teams",
                     headers=_bearer(auth_token),
                     json={"name": shared_name})
    assert first.status_code in (200, 201), first.text

    second = api.post("/teams",
                      headers=_bearer(auth_token),
                      json={"name": shared_name})
    assert second.status_code in (400, 409), (
        f"Création d'une Team avec un name déjà existant aurait dû "
        f"échouer en 400/409, reçu {second.status_code} : {second.text}"
    )
    # Tolérant sur le wording — on cherche juste un signal que
    # c'est bien le doublon qui a posé problème, pas un autre échec.
    body_lower = second.text.lower()
    assert ("duplicate" in body_lower or "unique" in body_lower
            or "name" in body_lower or "déjà" in body_lower
            or "deja" in body_lower or "exist" in body_lower), (
        f"Le message d'erreur ne semble pas mentionner le doublon : "
        f"{second.text}"
    )


def test_delete_team_referencee_par_project_echoue(api, auth_token):
    """on_delete=restrict sur Project→Team : tenter de supprimer un
    Team qui a au moins un Project doit échouer (409 idéalement,
    400 acceptable). PAS 200 (suppression silencieuse qui orpheliner
    le Project), PAS 500 (erreur serveur sur une contrainte connue),
    PAS 204 No Content (qui suggère succès).

    C'est probablement le test le plus susceptible de révéler un
    bug : le mode 'restrict' sur BelongsTo n'est jamais exercé par
    nos tests précédents, et son enforcement côté DeleteHandler
    nécessite de regarder s'il existe des records pointant vers la
    cible AVANT d'autoriser le DELETE."""
    # 1. Setup : créer une Team, puis un Project qui la référence
    team_id = _create_team(api, auth_token, "with-project")

    project_resp = api.post("/projects",
                            headers=_bearer(auth_token),
                            json={"title": "Project that blocks delete",
                                  "team_id": team_id})
    assert project_resp.status_code in (200, 201), project_resp.text
    project_id = _extract_id(project_resp)

    # 2. Tentative de DELETE de la Team — doit échouer
    delete_resp = api.delete(f"/teams/{team_id}",
                             headers=_bearer(auth_token))
    assert delete_resp.status_code in (400, 409), (
        f"DELETE d'une Team référencée par un Project (on_delete=restrict) "
        f"aurait dû échouer en 400/409, reçu {delete_resp.status_code} : "
        f"{delete_resp.text}"
    )

    # 3. Vérifier que la Team existe TOUJOURS en base — le restrict
    # doit avoir empêché la suppression, pas juste renvoyé une erreur
    # cosmétique.
    get_team = api.get(f"/teams/{team_id}", headers=_bearer(auth_token))
    assert get_team.status_code == 200, (
        f"Malgré le refus du DELETE, la Team a été supprimée : "
        f"GET /teams/{team_id} renvoie {get_team.status_code}"
    )

    # 4. Et le Project est toujours là (cohérence référentielle)
    get_project = api.get(f"/projects/{project_id}",
                          headers=_bearer(auth_token))
    assert get_project.status_code == 200, (
        f"Le Project a disparu alors qu'il bloquait la suppression : "
        f"GET /projects/{project_id} renvoie {get_project.status_code}"
    )
