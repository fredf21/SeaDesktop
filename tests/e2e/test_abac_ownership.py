"""
test_abac_ownership.py — Lot ABAC-2 : ownership via own_resource.

Teste le shortcut `own_resource: true` qui compare subject.id avec
le champ owner_field de la ressource. Utilise l'entite UserNote
avec :

  owner_field: user_id
  access_control:
    update:
      allow_roles: [admin, manager, user]
      own_resource: true       # subject.id == note.user_id
    delete:
      allow_roles: [admin, manager, user]
      own_resource: true

default_allow_admin: true → l'admin bypass own_resource et peut
modifier les notes de n'importe quel user.

Tests :
  - User peut update/delete sa propre note
  - User ne peut PAS update/delete la note d'un autre user (403)
  - Admin peut update la note de n'importe quel user (bypass)
"""

from __future__ import annotations

import uuid

import pytest
import requests


def _register_user_with_role(api, role: str = "user"):
    """Cree un user avec le role specifie et retourne (token, user_id).
    Le user_id est recupere via /auth/me apres login."""
    email = f"abac2-{role}-{uuid.uuid4().hex[:8]}@test.local"
    password = "Sup3rSecret!"
    
    reg = api.post("/auth/register",
                   json={"email": email, "password": password, "role": role})
    assert reg.status_code in (200, 201), reg.text
    
    login = api.post("/auth/login",
                     json={"email": email, "password": password})
    assert login.status_code == 200, login.text
    token = login.json()["access_token"]
    
    # Recupere le user_id via /auth/me
    me = api.get("/auth/me",
                 headers={"Authorization": f"Bearer {token}"})
    assert me.status_code == 200, me.text
    user_id = me.json()["id"]
    
    return token, user_id


def _bearer(token: str) -> dict:
    return {"Authorization": f"Bearer {token}"}


def _create_note(base_url: str, token: str, user_id: str, title: str = None):
    """Cree une UserNote avec le user_id donne, retourne l'id."""
    if title is None:
        title = f"Note-{uuid.uuid4().hex[:6]}"
    
    resp = requests.post(
        f"{base_url}/usernotes",
        headers=_bearer(token),
        json={"title": title, "user_id": user_id},
        timeout=5.0
    )
    assert resp.status_code in (200, 201), (
        f"Create UserNote a echoue : {resp.status_code} : {resp.text}"
    )
    return resp.json()["id"]


# ─── Tests ────────────────────────────────────────────────────

def test_abac_user_peut_update_sa_propre_note(base_url, api):
    """User cree une note avec user_id=son_id, puis update.
    subject.id == note.user_id → autorise."""
    token, user_id = _register_user_with_role(api, "user")
    note_id = _create_note(base_url, token, user_id)
    
    resp = requests.put(
        f"{base_url}/usernotes/{note_id}",
        headers=_bearer(token),
        json={"title": "Note modifiee", "user_id": user_id},
        timeout=5.0
    )
    
    assert resp.status_code == 200, (
        f"User aurait du pouvoir modifier sa propre note, "
        f"recu {resp.status_code} : {resp.text}"
    )


def test_abac_user_ne_peut_pas_update_note_d_autrui(base_url, api):
    """Alice cree une note. Bob essaie de l'update.
    subject.id (Bob) != note.user_id (Alice) → 403."""
    alice_token, alice_id = _register_user_with_role(api, "user")
    bob_token, bob_id     = _register_user_with_role(api, "user")
    
    # Alice cree sa note
    note_id = _create_note(base_url, alice_token, alice_id,
                           title="Note d'Alice")
    
    # Bob tente de la modifier
    resp = requests.put(
        f"{base_url}/usernotes/{note_id}",
        headers=_bearer(bob_token),
        json={"title": "Bob essaie", "user_id": alice_id},
        timeout=5.0
    )
    
    assert resp.status_code == 403, (
        f"Bob n'aurait PAS du pouvoir modifier la note d'Alice, "
        f"recu {resp.status_code} : {resp.text}"
    )


def test_abac_user_peut_delete_sa_propre_note(base_url, api):
    """User cree + delete sa propre note → autorise."""
    token, user_id = _register_user_with_role(api, "user")
    note_id = _create_note(base_url, token, user_id)
    
    resp = requests.delete(
        f"{base_url}/usernotes/{note_id}",
        headers=_bearer(token),
        timeout=5.0
    )
    
    assert resp.status_code in (200, 204), (
        f"User aurait du pouvoir supprimer sa propre note, "
        f"recu {resp.status_code} : {resp.text}"
    )


def test_abac_user_ne_peut_pas_delete_note_d_autrui(base_url, api):
    """Alice cree, Bob essaie de delete → 403."""
    alice_token, alice_id = _register_user_with_role(api, "user")
    bob_token, _          = _register_user_with_role(api, "user")
    
    note_id = _create_note(base_url, alice_token, alice_id)
    
    resp = requests.delete(
        f"{base_url}/usernotes/{note_id}",
        headers=_bearer(bob_token),
        timeout=5.0
    )
    
    assert resp.status_code == 403, (
        f"Bob n'aurait PAS du pouvoir supprimer la note d'Alice, "
        f"recu {resp.status_code} : {resp.text}"
    )


def test_abac_admin_peut_update_note_d_autrui(base_url, api):
    """Alice (user lambda) cree une note. Admin l'update sans souci.
    default_allow_admin: true → admin bypass own_resource."""
    alice_token, alice_id = _register_user_with_role(api, "user")
    admin_token, _        = _register_user_with_role(api, "admin")
    
    note_id = _create_note(base_url, alice_token, alice_id,
                           title="Note d'Alice")
    
    # Admin modifie la note d'Alice
    resp = requests.put(
        f"{base_url}/usernotes/{note_id}",
        headers=_bearer(admin_token),
        json={"title": "Note modifiee par l'admin", "user_id": alice_id},
        timeout=5.0
    )
    
    assert resp.status_code == 200, (
        f"Admin aurait du pouvoir modifier la note d'Alice "
        f"(default_allow_admin=true), recu {resp.status_code} : "
        f"{resp.text}"
    )
