"""
test_files.py — Tests bout-en-bout de la feature File (multipart).

Cycle complet, en boîte noire HTTP :
    POST /documents          (multipart : title + attachment)
    GET  /documents/{id}/attachment   (download du fichier)

Ces tests verrouillent les corrections faites sur la feature File :
  - is_multipart_request (le multipart est bien reconnu)
  - sea_files enregistrée au registry (l'INSERT métadonnée réussit)
  - le round-trip upload → download rend le contenu intact

Pré-requis : le YAML de test déclare une entité Document avec un
champ File 'attachment' et un bloc storage (cf. test_yaml.py), donc
les routes /documents (POST multipart) et /documents/{id}/attachment
(GET download) sont montées.
"""

from __future__ import annotations

import io
import uuid

import pytest


# ───────────────────────────────────────────────────────────────
# Fixture : un utilisateur authentifié (token prêt à l'emploi)
# ───────────────────────────────────────────────────────────────

@pytest.fixture
def auth_token(api, unique_email):
    """Register + login, renvoie un access_token utilisable.

    L'upload exige l'authentification (la route /documents est
    protégée quand l'auth est active). Cette fixture fournit un
    token frais par test."""
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


# ───────────────────────────────────────────────────────────────
# Helpers d'upload / download
# ───────────────────────────────────────────────────────────────

def _upload(api, token: str, *, title: str, filename: str,
            content: bytes, mime: str = "application/octet-stream"):
    """POST /documents en multipart. 'files' déclenche le
    Content-Type multipart/form-data côté requests ; 'data' porte
    les champs texte (title)."""
    return api.post(
        "/documents",
        headers=_bearer(token),
        data={"title": title},
        files={"attachment": (filename, io.BytesIO(content), mime)},
    )


def _extract_id(create_response) -> str:
    """Récupère l'id du Document créé dans la réponse JSON. Le
    CreateHandler renvoie le record créé ; l'id est sous 'id'."""
    body = create_response.json()
    assert "id" in body, f"pas d'id dans la réponse create : {body}"
    return body["id"]


# ═══════════════════════════════════════════════════════════════
# Upload
# ═══════════════════════════════════════════════════════════════

def test_upload_multipart_reussit(api, auth_token):
    """Upload nominal : un POST multipart avec un fichier doit
    réussir (201) et renvoyer le Document créé avec son id."""
    resp = _upload(
        api, auth_token,
        title="Mon document",
        filename="hello.txt",
        content=b"contenu de test",
    )
    assert resp.status_code in (200, 201), resp.text
    doc_id = _extract_id(resp)
    assert doc_id  # non vide


def test_upload_sans_token_est_refuse(api):
    """La route /documents est protégée. Un upload sans token doit
    être rejeté (401), pas traité."""
    resp = api.post(
        "/documents",
        data={"title": "Sans auth"},
        files={"attachment": ("x.txt", io.BytesIO(b"data"), "text/plain")},
    )
    assert resp.status_code == 401, resp.text


def test_upload_json_pur_sans_fichier_reussit(api, auth_token):
    """Document est utilisable SANS fichier (attachment non requis).
    Un POST JSON classique (pas multipart) doit donc réussir : la
    feature File ne casse pas le CRUD JSON normal."""
    resp = api.post(
        "/documents",
        headers=_bearer(auth_token),
        json={"title": "Document sans pièce jointe"},
    )
    assert resp.status_code in (200, 201), resp.text


# ═══════════════════════════════════════════════════════════════
# Round-trip : upload puis download
# ═══════════════════════════════════════════════════════════════

def test_round_trip_upload_puis_download(api, auth_token):
    """Le test clé : on uploade un fichier, puis on le télécharge via
    /documents/{id}/attachment, et le contenu récupéré doit être
    IDENTIQUE à ce qu'on a envoyé.

    C'est ce test qui verrouille toute la chaîne corrigée :
    multipart reconnu → fichier écrit sur disque → métadonnée en
    base → référence dans documents → download qui relit tout."""
    original = b"Le contenu exact qui doit revenir intact.\n"

    up = _upload(
        api, auth_token,
        title="Round trip",
        filename="payload.bin",
        content=original,
    )
    assert up.status_code in (200, 201), up.text

    # ── DEBUG : capturer dans le message d'erreur ce que renvoie
    # vraiment l'upload (pour voir la structure du JSON et l'id
    # exact). Sera visible dans le message d'AssertionError.
    upload_body_dump = up.text
    upload_json_dump = up.json() if up.headers.get(
        "content-type", "").startswith("application/json") else None

    doc_id = _extract_id(up)

    # Download : GET /documents/{id}/attachment
    dl = api.get(f"/documents/{doc_id}/attachment",
                 headers=_bearer(auth_token))
    assert dl.status_code == 200, (
        f"download a échoué ; status={dl.status_code} body={dl.text}\n"
        f"  doc_id extrait : {doc_id!r}\n"
        f"  upload response text : {upload_body_dump}\n"
        f"  upload response json : {upload_json_dump}\n"
        f"  URL download : /documents/{doc_id}/attachment"
    )

    # Le corps de la réponse doit être le fichier, octet pour octet.
    assert dl.content == original, (
        f"contenu altéré : envoyé {len(original)} octets, "
        f"reçu {len(dl.content)} octets"
    )


def test_round_trip_contenu_binaire(api, auth_token):
    """Round-trip d'un contenu binaire couvrant les 256 valeurs
    d'octets (pas seulement du texte ASCII). Vérifie qu'aucune
    couche (multipart, storage, base, HTTP) ne corrompt les octets
    nuls ou hauts."""
    original = bytes(range(256)) * 16   # 4096 octets, toutes valeurs

    up = _upload(
        api, auth_token,
        title="Binaire",
        filename="blob.bin",
        content=original,
    )
    assert up.status_code in (200, 201), up.text
    doc_id = _extract_id(up)

    dl = api.get(f"/documents/{doc_id}/attachment",
                 headers=_bearer(auth_token))
    assert dl.status_code == 200, dl.text
    assert dl.content == original
    assert len(dl.content) == 4096


# ═══════════════════════════════════════════════════════════════
# Download : cas d'erreur
# ═══════════════════════════════════════════════════════════════

def test_download_document_inexistant_404(api, auth_token):
    """GET sur l'attachment d'un Document qui n'existe pas → 404."""
    ghost_id = str(uuid.uuid4())
    resp = api.get(f"/documents/{ghost_id}/attachment",
                   headers=_bearer(auth_token))
    assert resp.status_code == 404, resp.text


def test_download_document_sans_fichier_404(api, auth_token):
    """Un Document créé SANS attachment : tenter de télécharger son
    fichier doit donner 404 (aucun fichier attaché), pas une erreur
    serveur."""
    # Créer un Document sans fichier.
    create = api.post(
        "/documents",
        headers=_bearer(auth_token),
        json={"title": "Sans fichier"},
    )
    assert create.status_code in (200, 201), create.text
    doc_id = _extract_id(create)

    # Tenter le download de son attachment inexistant.
    dl = api.get(f"/documents/{doc_id}/attachment",
                 headers=_bearer(auth_token))
    assert dl.status_code == 404, dl.text

def test_update_document_marche(api, auth_token):
    # Créer un Document
    create = api.post("/documents",
                      headers=_bearer(auth_token),
                      json={"title": "Original"})
    doc_id = _extract_id(create)
    
    # Le modifier
    update = api.put(f"/documents/{doc_id}",
                     headers=_bearer(auth_token),
                     json={"title": "Modifié"})
    assert update.status_code == 200, update.text

def test_delete_document_marche(api, auth_token):
    create = api.post("/documents",
                      headers=_bearer(auth_token),
                      json={"title": "À supprimer"})
    doc_id = _extract_id(create)
    
    delete_resp = api.delete(f"/documents/{doc_id}",
                              headers=_bearer(auth_token))
    assert delete_resp.status_code in (200, 204), delete_resp.text
    
    # Vérifier que ce n'est plus accessible
    get = api.get(f"/documents/{doc_id}", headers=_bearer(auth_token))
    assert get.status_code == 404
