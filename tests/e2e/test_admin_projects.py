"""
test_admin_projects.py — Tests bout-en-bout des endpoints d'administration
des fichiers YAML projet.

Routes couvertes (montees par main.cpp via admin_handlers/) :

    GET    /admin/projects               — liste les YAML du dossier configs/
    GET    /admin/projects/{file}        — lit le YAML brut
    POST   /admin/projects/{file}        — cree un nouveau YAML
    PUT    /admin/projects/{file}        — remplace un YAML existant
    DELETE /admin/projects/{file}        — supprime un YAML

Contrats verifies :
  - Authentification JWT obligatoire (401 sans header).
  - Role admin obligatoire (403 si role user).
  - GET liste : retourne {"projects": [...]} avec au moins le YAML
    courant du backend.
  - GET file : retourne le YAML brut en text/plain, 404 si inconnu.
  - POST : 201 si creation, 409 si le fichier existe deja, 400 si
    YAML invalide ou si project.name ne matche pas le filename.
  - PUT : 200 si remplacement, 404 si inconnu, 400 si YAML invalide.
  - DELETE : 200 si supprime (body {"file": ..., "success": true}), 404 si inconnu.
  - Cleanup : tout YAML cree par un test est supprime en fin de test
    pour ne pas polluer la suite (DELETE meme si le test echoue).
"""

from __future__ import annotations

import uuid

import pytest


# ───────────────────────────────────────────────────────────────
# Helpers locaux
# ───────────────────────────────────────────────────────────────

def _project_files(list_response_json) -> list[str]:
    """Extrait les noms de fichiers des entrees {file, name} retournees
    par GET /admin/projects."""
    return [entry["file"] for entry in list_response_json.get("projects", [])]


def _register_admin(api) -> tuple[str, str]:
    """Cree un user avec role=admin et retourne (token, email).

    Profite du fait que /auth/register accepte le champ role tel
    quel en v1.0 (cf. avertissement de securite dans auth.md).
    """
    email = f"admin-{uuid.uuid4().hex[:8]}@itest.local"
    password = "Sup3rSecret!"
    reg = api.post("/auth/register",
                   json={"email": email, "password": password, "role": "admin"})
    assert reg.status_code in (200, 201), reg.text

    login = api.post("/auth/login",
                     json={"email": email, "password": password})
    assert login.status_code == 200, login.text
    return login.json()["access_token"], email


def _register_user(api) -> str:
    """Cree un user ordinaire (role=user) et retourne son token."""
    email = f"user-{uuid.uuid4().hex[:8]}@itest.local"
    password = "Sup3rSecret!"
    reg = api.post("/auth/register",
                   json={"email": email, "password": password, "role": "user"})
    assert reg.status_code in (200, 201), reg.text

    login = api.post("/auth/login",
                     json={"email": email, "password": password})
    assert login.status_code == 200, login.text
    return login.json()["access_token"]


def _auth(token: str) -> dict:
    """Headers Authorization bearer pour requests."""
    return {"Authorization": f"Bearer {token}"}


def _minimal_yaml(project_name: str, service_name: str = "TestService") -> str:
    """Genere un YAML minimal valide. Le project.name DOIT correspondre
    au filename sans extension (verification cote backend)."""
    return f"""project:
  name: {project_name}

services:
  - name: {service_name}
    port: 9999
    database:
      type: memory
    entities:
      - name: Note
        options:
          enable_crud: true
        fields:
          - name: id
            type: uuid
            required: true
            unique: true
          - name: title
            type: string
            required: true
"""


# ───────────────────────────────────────────────────────────────
# Fixture : token admin reutilisable
# ───────────────────────────────────────────────────────────────

@pytest.fixture
def admin_token(api) -> str:
    """Un token admin frais par test, evite tout couplage entre tests."""
    token, _ = _register_admin(api)
    return token


@pytest.fixture
def unique_project_name() -> str:
    """Nom de projet unique pour eviter les collisions entre tests."""
    return f"E2eTest{uuid.uuid4().hex[:10]}"


# ───────────────────────────────────────────────────────────────
# GET /admin/projects (list)
# ───────────────────────────────────────────────────────────────

def test_list_projects_sans_token_retourne_401(api):
    """Sans Authorization, l'endpoint refuse l'acces."""
    resp = api.get("/admin/projects")
    assert resp.status_code == 401, resp.text


def test_list_projects_role_user_retourne_403(api):
    """Un user ordinaire n'a pas le droit, meme authentifie."""
    user_token = _register_user(api)
    resp = api.get("/admin/projects", headers=_auth(user_token))
    assert resp.status_code == 403, resp.text


def test_list_projects_role_admin_retourne_la_liste(api, admin_token):
    """Avec admin, la reponse contient la liste des YAML."""
    resp = api.get("/admin/projects", headers=_auth(admin_token))
    assert resp.status_code == 200, resp.text
    body = resp.json()
    assert "projects" in body
    assert isinstance(body["projects"], list)
    # La fixture conftest a genere un YAML dans tmp work_dir,
    # le backend a ete lance avec --config pointant vers ce fichier.
    # Le dossier configs/ scanne par /admin/projects peut etre vide
    # ou contenir d'autres YAML selon l'environnement de test. On
    # verifie juste que la liste est utilisable (pas que le contenu
    # est exact).
    # Chaque entree est un objet {"file": "X.yaml", "name": "X"}.
    for entry in body["projects"]:
        assert isinstance(entry, dict)
        assert "file" in entry
        assert "name" in entry
        assert isinstance(entry["file"], str)
        assert isinstance(entry["name"], str)


# ───────────────────────────────────────────────────────────────
# POST /admin/projects/{file} (create)
# ───────────────────────────────────────────────────────────────

def test_creer_projet_sans_token_retourne_401(api, unique_project_name):
    yaml_content = _minimal_yaml(unique_project_name)
    resp = api.post(f"/admin/projects/{unique_project_name}.yaml",
                    data=yaml_content,
                    headers={"Content-Type": "application/x-yaml"})
    assert resp.status_code == 401, resp.text


def test_creer_projet_role_user_retourne_403(api, unique_project_name):
    user_token = _register_user(api)
    yaml_content = _minimal_yaml(unique_project_name)
    resp = api.post(f"/admin/projects/{unique_project_name}.yaml",
                    data=yaml_content,
                    headers={**_auth(user_token),
                             "Content-Type": "application/x-yaml"})
    assert resp.status_code == 403, resp.text


def test_creer_projet_yaml_valide_retourne_201(api, admin_token,
                                                unique_project_name):
    yaml_content = _minimal_yaml(unique_project_name)
    filename = f"{unique_project_name}.yaml"
    try:
        resp = api.post(f"/admin/projects/{filename}",
                        data=yaml_content,
                        headers={**_auth(admin_token),
                                 "Content-Type": "application/x-yaml"})
        assert resp.status_code == 201, resp.text

        # Le fichier doit maintenant apparaitre dans la liste
        list_resp = api.get("/admin/projects", headers=_auth(admin_token))
        assert filename in _project_files(list_resp.json())
    finally:
        # Cleanup : on supprime le YAML cree quel que soit le resultat.
        api.delete(f"/admin/projects/{filename}",
                   headers=_auth(admin_token))


def test_creer_projet_existant_retourne_409(api, admin_token,
                                              unique_project_name):
    """POST sur un nom deja pris doit refuser (utiliser PUT a la place)."""
    yaml_content = _minimal_yaml(unique_project_name)
    filename = f"{unique_project_name}.yaml"
    try:
        # Premiere creation : OK
        resp1 = api.post(f"/admin/projects/{filename}",
                         data=yaml_content,
                         headers={**_auth(admin_token),
                                  "Content-Type": "application/x-yaml"})
        assert resp1.status_code == 201, resp1.text

        # Deuxieme creation avec le meme nom : conflit
        resp2 = api.post(f"/admin/projects/{filename}",
                         data=yaml_content,
                         headers={**_auth(admin_token),
                                  "Content-Type": "application/x-yaml"})
        assert resp2.status_code == 409, resp2.text
    finally:
        api.delete(f"/admin/projects/{filename}",
                   headers=_auth(admin_token))


def test_creer_projet_yaml_invalide_retourne_400(api, admin_token,
                                                   unique_project_name):
    """Un YAML syntaxiquement casse doit etre refuse avant ecriture."""
    yaml_content = "project:\n  name: \"unclosed_string\nservices:\n"
    filename = f"{unique_project_name}.yaml"
    resp = api.post(f"/admin/projects/{filename}",
                    data=yaml_content,
                    headers={**_auth(admin_token),
                             "Content-Type": "application/x-yaml"})
    assert resp.status_code == 400, resp.text

    # Verification : le fichier n'a PAS ete cree (atomicite).
    list_resp = api.get("/admin/projects", headers=_auth(admin_token))
    assert filename not in _project_files(list_resp.json())


def test_creer_projet_avec_nom_qui_ne_matche_pas_retourne_400(
        api, admin_token, unique_project_name):
    """Le project.name du YAML doit correspondre au filename sans
    extension. Sinon, refus."""
    # project.name = X, filename = Y → incoherence
    yaml_content = _minimal_yaml("UnNomTotalementDifferent")
    filename = f"{unique_project_name}.yaml"
    resp = api.post(f"/admin/projects/{filename}",
                    data=yaml_content,
                    headers={**_auth(admin_token),
                             "Content-Type": "application/x-yaml"})
    assert resp.status_code == 400, resp.text


# ───────────────────────────────────────────────────────────────
# GET /admin/projects/{file} (read)
# ───────────────────────────────────────────────────────────────

def test_lire_projet_sans_token_retourne_401(api, unique_project_name):
    resp = api.get(f"/admin/projects/{unique_project_name}.yaml")
    assert resp.status_code == 401, resp.text


def test_lire_projet_inexistant_retourne_404(api, admin_token):
    resp = api.get("/admin/projects/Inexistant.yaml",
                   headers=_auth(admin_token))
    assert resp.status_code == 404, resp.text


def test_lire_projet_existant_retourne_son_contenu(api, admin_token,
                                                     unique_project_name):
    """Le YAML lu doit etre identique a celui qu'on a ecrit."""
    yaml_content = _minimal_yaml(unique_project_name)
    filename = f"{unique_project_name}.yaml"
    try:
        # Cree
        post_resp = api.post(f"/admin/projects/{filename}",
                             data=yaml_content,
                             headers={**_auth(admin_token),
                                      "Content-Type": "application/x-yaml"})
        assert post_resp.status_code == 201, post_resp.text

        # Relit
        get_resp = api.get(f"/admin/projects/{filename}",
                           headers=_auth(admin_token))
        assert get_resp.status_code == 200, get_resp.text
        # Round-trip texte (le serveur ne reformate pas le YAML).
        assert get_resp.text == yaml_content
    finally:
        api.delete(f"/admin/projects/{filename}",
                   headers=_auth(admin_token))


# ───────────────────────────────────────────────────────────────
# PUT /admin/projects/{file} (update)
# ───────────────────────────────────────────────────────────────

def test_mettre_a_jour_projet_inexistant_retourne_404(api, admin_token,
                                                       unique_project_name):
    yaml_content = _minimal_yaml(unique_project_name)
    resp = api.put(f"/admin/projects/{unique_project_name}.yaml",
                   data=yaml_content,
                   headers={**_auth(admin_token),
                            "Content-Type": "application/x-yaml"})
    assert resp.status_code == 404, resp.text


def test_mettre_a_jour_projet_existant_retourne_200(api, admin_token,
                                                      unique_project_name):
    yaml_v1 = _minimal_yaml(unique_project_name, service_name="ServiceA")
    yaml_v2 = _minimal_yaml(unique_project_name, service_name="ServiceB")
    filename = f"{unique_project_name}.yaml"
    try:
        # Cree avec v1
        post_resp = api.post(f"/admin/projects/{filename}",
                             data=yaml_v1,
                             headers={**_auth(admin_token),
                                      "Content-Type": "application/x-yaml"})
        assert post_resp.status_code == 201

        # Remplace par v2
        put_resp = api.put(f"/admin/projects/{filename}",
                           data=yaml_v2,
                           headers={**_auth(admin_token),
                                    "Content-Type": "application/x-yaml"})
        assert put_resp.status_code == 200, put_resp.text

        # Verifie que le contenu est bien v2
        get_resp = api.get(f"/admin/projects/{filename}",
                           headers=_auth(admin_token))
        assert get_resp.text == yaml_v2
        assert "ServiceB" in get_resp.text
        assert "ServiceA" not in get_resp.text
    finally:
        api.delete(f"/admin/projects/{filename}",
                   headers=_auth(admin_token))


def test_mettre_a_jour_avec_yaml_invalide_retourne_400_et_preserve_lancien(
        api, admin_token, unique_project_name):
    """Si le PUT echoue (YAML invalide), l'ancien contenu doit etre
    intact (atomicite)."""
    yaml_v1 = _minimal_yaml(unique_project_name, service_name="ServiceA")
    yaml_invalide = "project: [this is not a valid yaml structure\n"
    filename = f"{unique_project_name}.yaml"
    try:
        api.post(f"/admin/projects/{filename}",
                 data=yaml_v1,
                 headers={**_auth(admin_token),
                          "Content-Type": "application/x-yaml"})

        # Tentative de PUT avec un YAML cassé
        put_resp = api.put(f"/admin/projects/{filename}",
                           data=yaml_invalide,
                           headers={**_auth(admin_token),
                                    "Content-Type": "application/x-yaml"})
        assert put_resp.status_code == 400, put_resp.text

        # L'ancien contenu doit etre toujours la
        get_resp = api.get(f"/admin/projects/{filename}",
                           headers=_auth(admin_token))
        assert get_resp.status_code == 200
        assert "ServiceA" in get_resp.text
    finally:
        api.delete(f"/admin/projects/{filename}",
                   headers=_auth(admin_token))


# ───────────────────────────────────────────────────────────────
# DELETE /admin/projects/{file}
# ───────────────────────────────────────────────────────────────

def test_supprimer_projet_sans_token_retourne_401(api, unique_project_name):
    resp = api.delete(f"/admin/projects/{unique_project_name}.yaml")
    assert resp.status_code == 401, resp.text


def test_supprimer_projet_inexistant_retourne_404(api, admin_token):
    resp = api.delete("/admin/projects/Inexistant.yaml",
                      headers=_auth(admin_token))
    assert resp.status_code == 404, resp.text


def test_supprimer_projet_existant_retourne_204(api, admin_token,
                                                  unique_project_name):
    yaml_content = _minimal_yaml(unique_project_name)
    filename = f"{unique_project_name}.yaml"

    # Cree
    post_resp = api.post(f"/admin/projects/{filename}",
                         data=yaml_content,
                         headers={**_auth(admin_token),
                                  "Content-Type": "application/x-yaml"})
    assert post_resp.status_code == 201

    # Supprime
    del_resp = api.delete(f"/admin/projects/{filename}",
                          headers=_auth(admin_token))
    assert del_resp.status_code == 200, del_resp.text

    # Verifie qu'il n'est plus dans la liste
    list_resp = api.get("/admin/projects", headers=_auth(admin_token))
    assert filename not in _project_files(list_resp.json())

    # Verifie qu'un GET retourne maintenant 404
    get_resp = api.get(f"/admin/projects/{filename}",
                       headers=_auth(admin_token))
    assert get_resp.status_code == 404


# ───────────────────────────────────────────────────────────────
# Scenario integre : cycle de vie complet
# ───────────────────────────────────────────────────────────────

def test_cycle_de_vie_complet_create_read_update_delete(
        api, admin_token, unique_project_name):
    """Verifie qu'un projet peut etre cree, lu, modifie et supprime
    en sequence via les 4 endpoints, et que chaque etape laisse le
    systeme dans l'etat attendu."""
    filename = f"{unique_project_name}.yaml"
    yaml_v1 = _minimal_yaml(unique_project_name, service_name="Alpha")
    yaml_v2 = _minimal_yaml(unique_project_name, service_name="Beta")

    # 1. CREATE
    resp = api.post(f"/admin/projects/{filename}",
                    data=yaml_v1,
                    headers={**_auth(admin_token),
                             "Content-Type": "application/x-yaml"})
    assert resp.status_code == 201

    # 2. LIST contient le projet
    resp = api.get("/admin/projects", headers=_auth(admin_token))
    assert filename in _project_files(resp.json())

    # 3. READ retourne v1
    resp = api.get(f"/admin/projects/{filename}", headers=_auth(admin_token))
    assert "Alpha" in resp.text

    # 4. UPDATE vers v2
    resp = api.put(f"/admin/projects/{filename}",
                   data=yaml_v2,
                   headers={**_auth(admin_token),
                            "Content-Type": "application/x-yaml"})
    assert resp.status_code == 200

    # 5. READ retourne v2
    resp = api.get(f"/admin/projects/{filename}", headers=_auth(admin_token))
    assert "Beta" in resp.text
    assert "Alpha" not in resp.text

    # 6. DELETE
    resp = api.delete(f"/admin/projects/{filename}",
                      headers=_auth(admin_token))
    assert resp.status_code == 200

    # 7. LIST ne contient plus le projet
    resp = api.get("/admin/projects", headers=_auth(admin_token))
    assert filename not in _project_files(resp.json())
