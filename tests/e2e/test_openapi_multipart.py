"""
test_openapi_multipart.py — Verifie que la spec OpenAPI declare
correctement multipart/form-data pour les endpoints qui acceptent
des uploads de fichier.

L'entite Document a un champ File (de la fixture YAML). Ses endpoints
POST /documents et PUT /documents/{id} doivent donc declarer leur
requestBody en multipart/form-data, pas en application/json.

Les autres entites (Team, Project, Tag, User) n'ont pas de champ File,
leur requestBody reste en application/json.
"""

from __future__ import annotations

import requests


def test_openapi_document_post_est_multipart(base_url):
    """POST /documents : l'entite Document a un champ File 'attachment',
    donc le requestBody doit etre en multipart/form-data."""
    resp = requests.get(f"{base_url}/openapi.json", timeout=5.0)
    assert resp.status_code == 200, resp.text
    spec = resp.json()

    # Chemin documents (singular dans la spec OpenAPI)
    post_op = spec["paths"]["/documents"]["post"]
    content = post_op["requestBody"]["content"]

    assert "multipart/form-data" in content, (
        f"POST /documents devrait accepter multipart/form-data (champ "
        f"File 'attachment'), reçu : {list(content.keys())}"
    )
    assert "application/json" not in content, (
        f"POST /documents ne devrait PAS proposer application/json "
        f"quand un champ File est present (sinon le client ne saura "
        f"pas comment uploader le fichier)"
    )


def test_openapi_document_put_est_multipart(base_url):
    """PUT /documents/{id} : meme entite Document avec File, donc
    le update doit aussi etre en multipart."""
    resp = requests.get(f"{base_url}/openapi.json", timeout=5.0)
    assert resp.status_code == 200, resp.text
    spec = resp.json()

    put_op = spec["paths"]["/documents/{id}"]["put"]
    content = put_op["requestBody"]["content"]

    assert "multipart/form-data" in content, (
        f"PUT /documents/{{id}} devrait etre en multipart, "
        f"reçu : {list(content.keys())}"
    )


def test_openapi_team_post_reste_json(base_url):
    """POST /teams : Team n'a pas de champ File, doit rester en
    application/json (non-regression)."""
    resp = requests.get(f"{base_url}/openapi.json", timeout=5.0)
    assert resp.status_code == 200, resp.text
    spec = resp.json()

    post_op = spec["paths"]["/teams"]["post"]
    content = post_op["requestBody"]["content"]

    assert "application/json" in content, (
        f"POST /teams devrait rester en application/json (pas de "
        f"champ File), reçu : {list(content.keys())}"
    )
    assert "multipart/form-data" not in content, (
        f"POST /teams ne devrait PAS etre en multipart (pas de File)"
    )


def test_openapi_multipart_schema_a_les_proprietes(base_url):
    """Le schema multipart de POST /documents doit contenir les
    champs de Document, dont 'attachment' en string/binary."""
    resp = requests.get(f"{base_url}/openapi.json", timeout=5.0)
    spec = resp.json()

    post_op = spec["paths"]["/documents"]["post"]
    schema = post_op["requestBody"]["content"]["multipart/form-data"]["schema"]

    assert schema["type"] == "object", (
        f"Le schema multipart devrait etre 'type: object', reçu : "
        f"{schema.get('type')}"
    )
    assert "properties" in schema, (
        f"Le schema multipart devrait avoir un bloc 'properties' : "
        f"{schema}"
    )

    properties = schema["properties"]
    
    # Le champ File 'attachment' doit etre present avec format=binary
    assert "attachment" in properties, (
        f"Le schema devrait inclure le champ 'attachment'. Properties : "
        f"{list(properties.keys())}"
    )
    attachment = properties["attachment"]
    assert attachment.get("type") == "string", (
        f"'attachment' devrait etre type=string, reçu : {attachment}"
    )
    assert attachment.get("format") == "binary", (
        f"'attachment' devrait etre format=binary (signal d'upload "
        f"binaire dans multipart), reçu : {attachment}"
    )

    # L'id ne doit PAS etre dans le schema (genere cote serveur)
    assert "id" not in properties, (
        f"Le schema multipart ne devrait PAS contenir 'id' (genere "
        f"cote serveur), properties : {list(properties.keys())}"
    )
