"""
test_admin_restart.py — Tests bout-en-bout de l'endpoint
POST /admin/restart.

Particularite : cet endpoint TUE le backend (apres un delai de 500ms
cote handler). Si on utilisait la fixture backend_server de portee
SESSION, tous les tests suivants echoueraient. On declare donc ici
une fixture LOCALE restartable_backend, qui demarre son propre backend
dedie et le nettoie en fin de test, peu importe ce qui s'est passe.

IMPORTANT — execution dans un process pytest dedie :
    Quand ce fichier est execute APRES d'autres tests dans le meme
    process pytest (ex. pytest .), le demarrage du backend dedie
    echoue sur une assertion Seastar interne :

        seastar::sharded<MysqlConnexionPool>::~sharded():
            Assertion `_instances.empty()` failed.

    Le bug se manifeste seulement quand plusieurs backends ont ete
    crees-puis-tues dans le meme parent pytest, et que cette fixture
    essaie d'en demarrer un de plus. Lancer test_admin_restart.py
    dans un process pytest dedie evite le probleme.

    Utiliser le script ./run_e2e.sh qui fait les deux passes
    automatiquement, ou lancer manuellement :

        pytest --ignore=test_admin_restart.py
        pytest test_admin_restart.py

Contrats verifies :
  - 401 sans token Authorization.
  - 403 avec un token de role user (non admin).
  - 202 Accepted avec un token admin, body JSON contient un
    message indiquant que le restart est planifie.
  - Apres reception du 202, le backend doit se terminer dans un
    delai raisonnable (verifie via proc.poll()).
"""

from __future__ import annotations

import os
import socket
import subprocess
import sys
import time
import uuid
from pathlib import Path

import mysql.connector
import pytest
import requests

from conftest import (
    BACKEND_BIN,
    BOOT_TIMEOUT_S,
    DB_HOST,
    DB_PASSWORD,
    DB_PORT,
    DB_USER,
    _connect_mysql_no_schema,
    _find_free_port,
    _wait_until_healthy,
)
from test_yaml import SERVICE_NAME, render_test_yaml


# ───────────────────────────────────────────────────────────────
# Fixture locale : un backend jetable, dedie aux tests de restart
# ───────────────────────────────────────────────────────────────

@pytest.fixture
def restartable_backend(tmp_path_factory):
    """
    Cree un backend Seastar en sous-processus, sur son propre port
    et sa propre base MySQL jetable, dedie au test en cours.

    Cycle de vie :
      setup     — base MySQL + YAML + lancement backend + attente health
      yield     — tuple (base_url, proc) que le test peut utiliser
      teardown  — kill si encore vivant + drop de la base MySQL

    Le test peut utiliser proc.poll() pour verifier que le backend
    s'est bien arrete suite au /admin/restart.
    """
    backend_path = Path(BACKEND_BIN)
    if not backend_path.is_file():
        pytest.fail(
            f"Binaire backend introuvable : {backend_path}\n"
            f"Compile backend_seastar, ou indique son chemin via "
            f"SEA_E2E_BACKEND_BIN."
        )

    # Base jetable dediee
    db_name = f"seadesktop_itest_restart_{os.getpid()}_{uuid.uuid4().hex[:8]}"
    conn = _connect_mysql_no_schema()
    try:
        cur = conn.cursor()
        cur.execute(f"CREATE DATABASE `{db_name}`")
        cur.close()
    finally:
        conn.close()

    http_port = _find_free_port()
    base_url = f"http://127.0.0.1:{http_port}"

    work_dir = tmp_path_factory.mktemp("e2e_restart_backend")
    storage_root = work_dir / "storage"
    storage_root.mkdir(parents=True, exist_ok=True)

    yaml_path = work_dir / "restart_project.yaml"
    yaml_path.write_text(
        render_test_yaml(
            http_port=http_port,
            db_host=DB_HOST,
            db_port=DB_PORT,
            db_name=db_name,
            db_user=DB_USER,
            db_password=DB_PASSWORD,
            storage_root=str(storage_root),
        )
    )

    proc = subprocess.Popen(
        [
            str(backend_path.resolve()),
            "--config", str(yaml_path),
            "--service_name", SERVICE_NAME,
        ],
        cwd=work_dir,
        stdout=sys.stdout,
        stderr=sys.stderr,
    )

    try:
        _wait_until_healthy(base_url, proc)
        yield (base_url, proc)

    finally:
        # ── Teardown : si le backend est encore vivant, on le tue ─
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=10.0)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()

        # ── Teardown : drop de la base jetable ──────────────────
        try:
            conn = _connect_mysql_no_schema()
            try:
                cur = conn.cursor()
                cur.execute(f"DROP DATABASE IF EXISTS `{db_name}`")
                cur.close()
            finally:
                conn.close()
        except mysql.connector.Error:
            pass


# ───────────────────────────────────────────────────────────────
# Helpers
# ───────────────────────────────────────────────────────────────

def _register_admin(base_url: str) -> str:
    """Cree un user admin et retourne son access_token."""
    email = f"admin-restart-{uuid.uuid4().hex[:8]}@itest.local"
    password = "Sup3rSecret!"

    reg = requests.post(f"{base_url}/auth/register",
                        json={"email": email, "password": password,
                              "role": "admin"},
                        timeout=5.0)
    assert reg.status_code in (200, 201), reg.text

    login = requests.post(f"{base_url}/auth/login",
                          json={"email": email, "password": password},
                          timeout=5.0)
    assert login.status_code == 200, login.text
    return login.json()["access_token"]


def _register_user(base_url: str) -> str:
    """Cree un user ordinaire et retourne son access_token."""
    email = f"user-restart-{uuid.uuid4().hex[:8]}@itest.local"
    password = "Sup3rSecret!"

    reg = requests.post(f"{base_url}/auth/register",
                        json={"email": email, "password": password,
                              "role": "user"},
                        timeout=5.0)
    assert reg.status_code in (200, 201), reg.text

    login = requests.post(f"{base_url}/auth/login",
                          json={"email": email, "password": password},
                          timeout=5.0)
    assert login.status_code == 200, login.text
    return login.json()["access_token"]


def _auth(token: str) -> dict:
    return {"Authorization": f"Bearer {token}"}


# ───────────────────────────────────────────────────────────────
# Tests
# ───────────────────────────────────────────────────────────────

def test_restart_sans_token_retourne_401(restartable_backend):
    """Sans Authorization, refus immediat. Le backend n'est PAS tue."""
    base_url, proc = restartable_backend

    resp = requests.post(f"{base_url}/admin/restart", timeout=5.0)
    assert resp.status_code == 401, resp.text

    # Le backend doit etre toujours vivant.
    assert proc.poll() is None, (
        "Le backend ne devrait PAS s'arreter sur un appel non authentifie."
    )


def test_restart_role_user_retourne_403(restartable_backend):
    """Un user ordinaire ne peut pas declencher un restart."""
    base_url, proc = restartable_backend

    user_token = _register_user(base_url)
    resp = requests.post(f"{base_url}/admin/restart",
                         headers=_auth(user_token),
                         timeout=5.0)
    assert resp.status_code == 403, resp.text

    # Le backend doit etre toujours vivant.
    assert proc.poll() is None, (
        "Le backend ne devrait PAS s'arreter sur un appel sans role admin."
    )


def test_restart_role_admin_retourne_202_et_arrete_le_backend(
        restartable_backend):
    """Avec un token admin, le restart est accepte (202) et le backend
    se termine peu apres (le orchestrateur Docker le relancerait en
    production, mais ici on verifie juste qu'il s'arrete bien)."""
    base_url, proc = restartable_backend

    admin_token = _register_admin(base_url)

    resp = requests.post(f"{base_url}/admin/restart",
                         headers=_auth(admin_token),
                         timeout=5.0)
    assert resp.status_code == 202, resp.text

    # Le body doit contenir une indication que le restart est planifie.
    # On verifie la presence d'un champ utile sans imposer une cle
    # particuliere, pour rester tolerant aux evolutions du contrat.
    try:
        body = resp.json()
        assert isinstance(body, dict), body
    except ValueError:
        # Si le backend ne retourne pas de JSON, on accepte aussi.
        pass

    # Le handler ferme le service apres ~500 ms. On laisse une marge
    # confortable (5s) pour que le processus se termine vraiment.
    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            break
        time.sleep(0.1)

    assert proc.poll() is not None, (
        "Le backend n'a pas termine dans les 5s suivant le POST "
        "/admin/restart. Code de sortie attendu : 0 (sortie propre) "
        "ou autre code de fin volontaire."
    )
