"""
test_crud_project.py — Lot B : CRUD Project avec BelongsTo Team.

Project a une relation BelongsTo vers Team via `team_id` avec
on_delete=restrict. Ce fichier exerce :

  - le chemin CRUD nominal sur une entité AVEC FK (que les tests
    Team ne couvrent pas)
  - le fix bug 9 (GenericCrudEngine::update) sur la branche
    do_for_each : avec BelongsTo, on doit itérer les relations,
    vérifier la cible, puis appeler l'UPDATE SQL
  - la vérification de FK invalide (POST/PUT avec team_id bidon)
  - le comportement on_delete=restrict (testé dans le Lot C)
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
    """Helper : crée une Team et renvoie son id. Utilisé par presque
    tous les tests Project (qui ont besoin d'une FK valide)."""
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


# ─── Tests CRUD Project ───────────────────────────────────────

def test_create_project_avec_fk_valide_reussit(api, auth_token):
    """POST /projects avec un team_id valide → 201 + id."""
    team_id = _create_team(api, auth_token, "for-project")

    project_title = f"Project-{uuid.uuid4().hex[:8]}"
    resp = api.post("/projects",
                    headers=_bearer(auth_token),
                    json={"title": project_title, "team_id": team_id})
    assert resp.status_code in (200, 201), resp.text

    body = resp.json()
    assert "id" in body
    assert body.get("title") == project_title
    # Selon comment ton serializer renvoie la FK, ça peut être
    # 'team_id' ou inclus dans une structure 'team'. On vérifie
    # juste l'un des deux.
    assert (body.get("team_id") == team_id
            or (isinstance(body.get("team"), dict)
                and body["team"].get("id") == team_id)), (
        f"team_id absent ou mal renvoyé : {body}"
    )


def test_create_project_avec_fk_invalide_400(api, auth_token):
    """POST /projects avec un team_id qui n'existe pas en base
    doit renvoyer 400 (ou 404) avec un message expliquant que
    l'entité cible n'a pas été trouvée. Le fix bug 9 a aussi
    introduit ce contrôle dans la branche do_for_each."""
    bidon_team_id = str(uuid.uuid4())
    project_title = f"Project-bad-fk-{uuid.uuid4().hex[:8]}"

    resp = api.post("/projects",
                    headers=_bearer(auth_token),
                    json={"title": project_title,
                          "team_id": bidon_team_id})
    # On accepte 400 ou 404 — c'est une erreur d'input/validation,
    # pas une erreur serveur. Surtout PAS 500 et PAS 201.
    assert resp.status_code in (400, 404), (
        f"FK invalide aurait dû échouer en 400/404, "
        f"reçu {resp.status_code} : {resp.text}"
    )
    # Idéalement le message mentionne 'Team' ou 'not found'.
    # On reste tolérant sur le wording exact.
    body_lower = resp.text.lower()
    assert ("team" in body_lower or "not found" in body_lower
            or "introuvable" in body_lower or "target" in body_lower), (
        f"Le message d'erreur ne mentionne pas la cible : {resp.text}"
    )


def test_update_project_modifie_titre_seulement(api, auth_token):
    """PUT /projects/{id} avec un nouveau title → 200, title modifié,
    team_id inchangé. Exerce la branche do_for_each du fix bug 9 :
    la relation Project→Team EXISTE, donc le code itère, vérifie
    (la FK n'est pas dans le record d'update donc skip restrict),
    puis appelle l'UPDATE SQL.

    Note : on ne touche PAS à team_id dans le payload de l'update.
    L'update partiel doit modifier le title seulement."""
    team_id = _create_team(api, auth_token, "for-update")

    create = api.post("/projects",
                      headers=_bearer(auth_token),
                      json={"title": "Titre original", "team_id": team_id})
    project_id = _extract_id(create)

    update = api.put(f"/projects/{project_id}",
                     headers=_bearer(auth_token),
                     json={"title": "Titre modifié"})
    assert update.status_code == 200, update.text

    # Re-GET : confirmer que le title est modifié ET que la FK
    # n'a pas été corrompue par l'update partiel.
    get_resp = api.get(f"/projects/{project_id}",
                       headers=_bearer(auth_token))
    assert get_resp.status_code == 200, get_resp.text

    body = get_resp.json()
    assert body.get("title") == "Titre modifié", (
        f"Update n'a pas modifié le title : {body}"
    )
    # Vérifie que team_id n'a pas été effacée par l'update partiel.
    # (Bug latent classique : un update qui oublie les champs non
    # fournis et les remet à null.)
    team_id_after = body.get("team_id") or (
        body.get("team", {}).get("id") if isinstance(body.get("team"), dict)
        else None
    )
    assert team_id_after == team_id, (
        f"L'update partiel a corrompu team_id : avant={team_id}, "
        f"après={team_id_after}, body={body}"
    )


def test_update_project_avec_fk_invalide_echoue(api, auth_token):
    """PUT /projects/{id} en changeant team_id vers un id qui n'existe
    pas → 400 (ou 404). On NE doit PAS pouvoir orpheliner un projet
    via update."""
    team_id = _create_team(api, auth_token, "for-bad-update")

    create = api.post("/projects",
                      headers=_bearer(auth_token),
                      json={"title": "Project", "team_id": team_id})
    project_id = _extract_id(create)

    bidon_team_id = str(uuid.uuid4())
    update = api.put(f"/projects/{project_id}",
                     headers=_bearer(auth_token),
                     json={"team_id": bidon_team_id})

    # Comportement attendu : refus avec 400/404. Surtout PAS 200
    # (qui voudrait dire qu'on a pointé le projet vers une cible
    # inexistante) et PAS 500.
    assert update.status_code in (400, 404), (
        f"PUT avec FK invalide aurait dû échouer en 400/404, "
        f"reçu {update.status_code} : {update.text}"
    )

    # Vérifie que team_id n'a pas été modifié en base malgré
    # le refus (sécurité défensive : un échec partiel laisserait
    # potentiellement la base dans un état incohérent).
    get_resp = api.get(f"/projects/{project_id}",
                       headers=_bearer(auth_token))
    assert get_resp.status_code == 200
    body = get_resp.json()
    team_id_after = body.get("team_id") or (
        body.get("team", {}).get("id") if isinstance(body.get("team"), dict)
        else None
    )
    assert team_id_after == team_id, (
        f"Malgré le refus, team_id a été modifié : "
        f"avant={team_id}, après={team_id_after}"
    )


def test_delete_project_seul_reussit(api, auth_token):
    """DELETE /projects/{id} → 200/204. Ce test couvre le cas
    SIMPLE (Project sans dépendant). Le cas du Team référencé par
    un Project (on_delete=restrict) est dans le Lot C."""
    team_id = _create_team(api, auth_token, "for-delete")

    create = api.post("/projects",
                      headers=_bearer(auth_token),
                      json={"title": "Doomed project", "team_id": team_id})
    project_id = _extract_id(create)

    delete_resp = api.delete(f"/projects/{project_id}",
                             headers=_bearer(auth_token))
    assert delete_resp.status_code in (200, 204), delete_resp.text

    # Vérifie que le DELETE a vraiment supprimé.
    get_resp = api.get(f"/projects/{project_id}",
                       headers=_bearer(auth_token))
    assert get_resp.status_code == 404
