"""
test_abac_rbac.py — Lot ABAC-1 : RBAC pur via allow_roles.

Teste l'AuthorizationMiddleware avec des policies basees uniquement
sur les roles du subject. Utilise l'entite SecretResource (isolee
des autres entites CRUD) avec ces policies :

  access_control:
    list:        allow_roles: [admin, manager, user]
    get_by_id:   allow_roles: [admin, manager, user]
    create:      allow_roles: [admin, manager]
    update:      allow_roles: [admin, manager]
    delete:      allow_roles: [admin]

Plus :
  default_policy: allow (les entites sans access_control passent)
  admin_role: admin
  default_allow_admin: true (admin bypass tout)

Tests :
  - user lambda (role=user) ne peut pas create → 403
  - manager peut create → 201
  - manager ne peut pas delete → 403
  - admin peut delete → 200
  - user lambda peut quand meme lister → 200
"""

from __future__ import annotations

import uuid

import pytest
import requests


def _register_with_role(api, role: str):
    """Cree un user avec le role specifie via POST /auth/register
    (qui accepte un champ 'role' dans le body, default 'user')."""
    email = f"abac-{role}-{uuid.uuid4().hex[:8]}@test.local"
    password = "Sup3rSecret!"
    
    reg = api.post("/auth/register",
                   json={"email": email, "password": password, "role": role})
    assert reg.status_code in (200, 201), (
        f"Register avec role={role} a echoue : {reg.text}"
    )
    
    login = api.post("/auth/login",
                     json={"email": email, "password": password})
    assert login.status_code == 200, login.text
    return login.json()["access_token"]


def _bearer(token: str) -> dict:
    return {"Authorization": f"Bearer {token}"}


# ─── Tests ────────────────────────────────────────────────────

def test_abac_user_normal_ne_peut_pas_creer_secret(base_url, api):
    """SecretResource.create : allow_roles=[admin, manager].
    Un user avec role='user' doit recevoir 403, avec un message
    explicite mentionnant la condition non satisfaite."""
    token = _register_with_role(api, "user")
    
    resp = requests.post(
        f"{base_url}/secretresources",
        headers=_bearer(token),
        json={"name": "test-resource"},
        timeout=5.0
    )
    
    assert resp.status_code == 403, (
        f"User role=user aurait du etre refuse pour create, "
        f"recu {resp.status_code} : {resp.text}"
    )
    
    body = resp.json()
    assert "error" in body or "message" in body, (
        f"Le 403 devrait avoir un body structure : {body}"
    )


def test_abac_manager_peut_creer_secret(base_url, api):
    """SecretResource.create : allow_roles=[admin, manager].
    Un manager doit pouvoir creer."""
    token = _register_with_role(api, "manager")
    
    resp = requests.post(
        f"{base_url}/secretresources",
        headers=_bearer(token),
        json={"name": f"manager-resource-{uuid.uuid4().hex[:6]}"},
        timeout=5.0
    )
    
    assert resp.status_code in (200, 201), (
        f"Manager aurait du pouvoir creer un SecretResource, "
        f"recu {resp.status_code} : {resp.text}"
    )


def test_abac_admin_peut_supprimer_secret(base_url, api):
    """SecretResource.delete : allow_roles=[admin].
    Un admin doit pouvoir supprimer."""
    admin_token = _register_with_role(api, "admin")
    
    # Crée d'abord un SecretResource (admin peut creer aussi)
    create = requests.post(
        f"{base_url}/secretresources",
        headers=_bearer(admin_token),
        json={"name": f"to-delete-{uuid.uuid4().hex[:6]}"},
        timeout=5.0
    )
    assert create.status_code in (200, 201), create.text
    resource_id = create.json()["id"]
    
    # Admin supprime
    delete_resp = requests.delete(
        f"{base_url}/secretresources/{resource_id}",
        headers=_bearer(admin_token),
        timeout=5.0
    )
    assert delete_resp.status_code in (200, 204), (
        f"Admin aurait du pouvoir supprimer, recu "
        f"{delete_resp.status_code} : {delete_resp.text}"
    )


def test_abac_manager_ne_peut_pas_supprimer_secret(base_url, api):
    """SecretResource.delete : allow_roles=[admin] (manager exclu).
    Un manager doit recevoir 403 sur DELETE."""
    # Admin crée d'abord un SecretResource (manager ne peut pas
    # le supprimer s'il existe)
    admin_token = _register_with_role(api, "admin")
    create = requests.post(
        f"{base_url}/secretresources",
        headers=_bearer(admin_token),
        json={"name": f"protected-{uuid.uuid4().hex[:6]}"},
        timeout=5.0
    )
    assert create.status_code in (200, 201), create.text
    resource_id = create.json()["id"]
    
    # Manager tente de supprimer
    manager_token = _register_with_role(api, "manager")
    delete_resp = requests.delete(
        f"{base_url}/secretresources/{resource_id}",
        headers=_bearer(manager_token),
        timeout=5.0
    )
    
    assert delete_resp.status_code == 403, (
        f"Manager n'aurait PAS du pouvoir supprimer un "
        f"SecretResource, recu {delete_resp.status_code} : "
        f"{delete_resp.text}"
    )


def test_abac_user_normal_peut_lister_secrets(base_url, api):
    """SecretResource.list : allow_roles=[admin, manager, user].
    Un user lambda doit pouvoir lister (read public).
    
    Verifie qu'on ne refuse pas TOUT pour les users normaux —
    juste les operations protegees (create/update/delete)."""
    token = _register_with_role(api, "user")
    
    resp = requests.get(
        f"{base_url}/secretresources",
        headers=_bearer(token),
        timeout=5.0
    )
    
    assert resp.status_code == 200, (
        f"User role=user aurait du pouvoir lister, recu "
        f"{resp.status_code} : {resp.text}"
    )
    
    # Le body doit etre une liste (potentiellement vide)
    body = resp.json()
    assert isinstance(body, list), (
        f"Le list devrait retourner une liste, recu : {type(body)}"
    )
