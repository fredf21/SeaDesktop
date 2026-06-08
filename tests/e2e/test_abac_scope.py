"""
test_abac_scope.py — Lot ABAC-3 : multi-tenant via same_scope.

Teste le shortcut `same_scope: true` qui compare un attribut du
subject (depuis le JWT additional_claims) avec le meme champ de
la ressource. Pattern multi-tenant : un user appartient a une
team, et ne peut voir/modifier que les ressources de sa team.

Configuration :
  User : ajout d'un champ team_id (additional_claim auto dans JWT)
  TeamDocument : scope_field=team_id, policies same_scope: true

Flux :
  1. User cree avec team_id='team_A'
  2. JWT contient team_id='team_A' (via additional_claims)
  3. AuthorizationMiddleware injecte X-User-Team-Id
  4. build_subject_from_headers traduit en subject.attributes.team_id
  5. Policy compare subject.attributes.team_id == resource.attributes.team_id

Tests :
  - User team_A peut voir un TeamDocument de team_A
  - User team_A ne peut PAS voir un TeamDocument de team_B
  - User team_A ne peut PAS update un TeamDocument de team_B
  - Admin peut voir tous les TeamDocument (bypass)
  - Sans team_id du tout (user lambda existant) : refus pour scope-checked ops
"""

from __future__ import annotations

import uuid

import pytest
import requests


def _register_user_in_team(api, team_id: str, role: str = "user"):
    """Cree un user avec team_id specifie + role.
    Retourne (token, user_id)."""
    email = f"abac3-{role}-{uuid.uuid4().hex[:8]}@test.local"
    password = "Sup3rSecret!"
    
    reg = api.post("/auth/register",
                   json={
                       "email": email,
                       "password": password,
                       "role": role,
                       "team_id": team_id
                   })
    assert reg.status_code in (200, 201), reg.text
    
    login = api.post("/auth/login",
                     json={"email": email, "password": password})
    assert login.status_code == 200, login.text
    token = login.json()["access_token"]
    
    me = api.get("/auth/me",
                 headers={"Authorization": f"Bearer {token}"})
    user_id = me.json()["id"]
    
    return token, user_id


def _bearer(token: str) -> dict:
    return {"Authorization": f"Bearer {token}"}


def _create_team_document(base_url: str, token: str, team_id: str,
                          title: str = None):
    """Cree un TeamDocument. Retourne l'id."""
    if title is None:
        title = f"Doc-{uuid.uuid4().hex[:6]}"
    
    resp = requests.post(
        f"{base_url}/teamdocuments",
        headers=_bearer(token),
        json={"title": title, "team_id": team_id},
        timeout=5.0
    )
    assert resp.status_code in (200, 201), (
        f"Create TeamDocument a echoue : {resp.status_code} : {resp.text}"
    )
    return resp.json()["id"]


# ─── Tests ────────────────────────────────────────────────────

def test_abac_user_voit_son_team_document(base_url, api):
    """User dans team_A peut acceder a un TeamDocument de team_A.
    subject.attributes.team_id == resource.attributes.team_id."""
    team_a = f"team-a-{uuid.uuid4().hex[:6]}"
    token, _ = _register_user_in_team(api, team_a)
    
    doc_id = _create_team_document(base_url, token, team_a)
    
    resp = requests.get(
        f"{base_url}/teamdocuments/{doc_id}",
        headers=_bearer(token),
        timeout=5.0
    )
    
    assert resp.status_code == 200, (
        f"User dans {team_a} devrait pouvoir voir un doc de {team_a}, "
        f"recu {resp.status_code} : {resp.text}"
    )


def test_abac_user_ne_voit_pas_team_document_autre_team(base_url, api):
    """User dans team_A ne peut PAS voir un TeamDocument de team_B.
    subject.attributes.team_id ('team_A') != resource.attributes.team_id ('team_B')."""
    team_a = f"team-a-{uuid.uuid4().hex[:6]}"
    team_b = f"team-b-{uuid.uuid4().hex[:6]}"
    
    token_a, _ = _register_user_in_team(api, team_a)
    token_b, _ = _register_user_in_team(api, team_b)
    
    # Bob (team_B) cree un doc dans team_B
    doc_id = _create_team_document(base_url, token_b, team_b,
                                    title="Doc de team_B")
    
    # Alice (team_A) essaie de le voir
    resp = requests.get(
        f"{base_url}/teamdocuments/{doc_id}",
        headers=_bearer(token_a),
        timeout=5.0
    )
    
    assert resp.status_code == 403, (
        f"User dans {team_a} ne devrait PAS voir un doc de {team_b}, "
        f"recu {resp.status_code} : {resp.text}"
    )


def test_abac_user_ne_peut_pas_update_document_autre_team(base_url, api):
    """User team_A ne peut pas modifier un doc de team_B."""
    team_a = f"team-a-{uuid.uuid4().hex[:6]}"
    team_b = f"team-b-{uuid.uuid4().hex[:6]}"
    
    token_a, _ = _register_user_in_team(api, team_a)
    token_b, _ = _register_user_in_team(api, team_b)
    
    doc_id = _create_team_document(base_url, token_b, team_b)
    
    resp = requests.put(
        f"{base_url}/teamdocuments/{doc_id}",
        headers=_bearer(token_a),
        json={"title": "Modifie par Alice", "team_id": team_b},
        timeout=5.0
    )
    
    assert resp.status_code == 403, (
        f"User team_A ne devrait pas pouvoir modifier doc de team_B, "
        f"recu {resp.status_code} : {resp.text}"
    )


def test_abac_admin_voit_tous_les_team_documents(base_url, api):
    """Admin bypass same_scope (default_allow_admin=true).
    Doit pouvoir voir un doc d'une team a laquelle il n'appartient pas."""
    team_a = f"team-a-{uuid.uuid4().hex[:6]}"
    
    user_token, _ = _register_user_in_team(api, team_a, role="user")
    # Admin sans team (ou avec autre team)
    admin_token, _ = _register_user_in_team(api, "team-admin", role="admin")
    
    doc_id = _create_team_document(base_url, user_token, team_a)
    
    resp = requests.get(
        f"{base_url}/teamdocuments/{doc_id}",
        headers=_bearer(admin_token),
        timeout=5.0
    )
    
    assert resp.status_code == 200, (
        f"Admin devrait voir le doc malgre une team differente "
        f"(default_allow_admin=true), recu {resp.status_code} : "
        f"{resp.text}"
    )


def test_abac_user_dans_meme_team_partage_documents(base_url, api):
    """Deux users dans la meme team voient les documents l'un de l'autre.
    Verifie que same_scope ne se base PAS sur user_id mais bien sur team_id."""
    team_shared = f"team-shared-{uuid.uuid4().hex[:6]}"
    
    alice_token, _ = _register_user_in_team(api, team_shared)
    bob_token, _   = _register_user_in_team(api, team_shared)
    
    # Alice cree le doc
    doc_id = _create_team_document(base_url, alice_token, team_shared,
                                    title="Doc partage")
    
    # Bob (meme team) peut le voir
    resp = requests.get(
        f"{base_url}/teamdocuments/{doc_id}",
        headers=_bearer(bob_token),
        timeout=5.0
    )
    
    assert resp.status_code == 200, (
        f"Bob (meme team que Alice) devrait voir le doc d'Alice, "
        f"recu {resp.status_code} : {resp.text}"
    )
