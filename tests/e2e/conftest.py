"""
conftest.py — Harnais des tests bout-en-bout SeaDesktop.

Ce fichier définit les fixtures pytest qui mettent en place
l'environnement réel testé :

  1. une base MySQL JETABLE, créée puis supprimée pour la session ;
  2. un fichier YAML de test généré à la volée (port libre + base) ;
  3. le binaire `backend_seastar` lancé en SOUS-PROCESSUS sur ce YAML ;
  4. l'attente active que le serveur réponde sur /health.

Approche : test BOÎTE NOIRE. On ne lie aucun code C++ ; on lance le
vrai backend et on l'attaque via HTTP, exactement comme un client
réel. C'est le standard pour tester une API REST.

─────────────────────────────────────────────────────────────────
PRÉ-REQUIS
─────────────────────────────────────────────────────────────────
  - Le MySQL de tests/docker-compose.test.yml doit tourner :
        docker compose -f tests/docker-compose.test.yml up -d --wait
  - Le binaire backend_seastar doit être compilé.
  - Python : pip install pytest requests mysql-connector-python

─────────────────────────────────────────────────────────────────
CONFIGURATION (variables d'environnement, défauts entre crochets)
─────────────────────────────────────────────────────────────────
  SEA_E2E_BACKEND_BIN   chemin du binaire backend_seastar
                        [build/Desktop_Qt_6_8_3-Debug/apps/Backend_Seastar/backend_seastar]
  SEA_ITEST_DB_HOST     hôte MySQL          [127.0.0.1]
  SEA_ITEST_DB_PORT     port MySQL          [13306]
  SEA_ITEST_DB_USER     utilisateur MySQL   [sea_itest]
  SEA_ITEST_DB_PASSWORD mot de passe        [sea_itest_pwd]
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

from test_yaml import SERVICE_NAME, render_test_yaml


# ───────────────────────────────────────────────────────────────
# Configuration lue depuis l'environnement
# ───────────────────────────────────────────────────────────────

def _env(name: str, default: str) -> str:
    return os.environ.get(name, default)


DB_HOST = _env("SEA_ITEST_DB_HOST", "127.0.0.1")
DB_PORT = int(_env("SEA_ITEST_DB_PORT", "13306"))
DB_USER = _env("SEA_ITEST_DB_USER", "sea_itest")
DB_PASSWORD = _env("SEA_ITEST_DB_PASSWORD", "sea_itest_pwd")

DEFAULT_BACKEND_BIN = (
    "build/Desktop_Qt_6_8_3-Debug/apps/Backend_Seastar/backend_seastar"
)
BACKEND_BIN = _env("SEA_E2E_BACKEND_BIN", DEFAULT_BACKEND_BIN)

# Délai maximal d'attente du démarrage du serveur (secondes).
BOOT_TIMEOUT_S = 30.0


# ───────────────────────────────────────────────────────────────
# Helpers
# ───────────────────────────────────────────────────────────────

def _find_free_port() -> int:
    """Réserve un port TCP libre. Le socket est fermé aussitôt : il y
    a une fenêtre de course théorique avant que le backend ne le
    reprenne, mais en pratique négligeable pour un harnais de test."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def _connect_mysql_no_schema():
    """Connexion MySQL SANS base sélectionnée — pour CREATE/DROP
    DATABASE (la base jetable n'existe pas encore au moment du
    CREATE)."""
    return mysql.connector.connect(
        host=DB_HOST,
        port=DB_PORT,
        user=DB_USER,
        password=DB_PASSWORD,
    )


def _wait_until_healthy(base_url: str, proc: subprocess.Popen) -> None:
    """Interroge GET /health en boucle jusqu'à une réponse, ou
    timeout. Si le processus backend meurt entre-temps, on échoue
    immédiatement avec sa sortie — plutôt que d'attendre 30 s pour
    rien."""
    deadline = time.monotonic() + BOOT_TIMEOUT_S
    last_error: Exception | None = None

    while time.monotonic() < deadline:
        # Le backend est-il encore vivant ?
        if proc.poll() is not None:
            raise RuntimeError(
                f"Le backend s'est arrêté pendant le démarrage "
                f"(code de sortie {proc.returncode}). "
                f"Voir la sortie ci-dessus."
            )
        try:
            resp = requests.get(f"{base_url}/health", timeout=1.0)
            if resp.status_code == 200:
                return
        except requests.RequestException as exc:
            last_error = exc
        time.sleep(0.25)

    raise RuntimeError(
        f"Le backend n'a pas répondu sur /health en {BOOT_TIMEOUT_S}s. "
        f"Dernière erreur : {last_error}"
    )


# ───────────────────────────────────────────────────────────────
# Fixture de session : le backend en marche
# ───────────────────────────────────────────────────────────────

@pytest.fixture(scope="session")
def backend_server(tmp_path_factory):
    """
    Fixture maîtresse, portée SESSION (un seul backend pour toute la
    suite — son démarrage coûte plusieurs secondes).

    Cycle de vie :
      setup   — base jetable + YAML + lancement backend + attente health
      yield   — l'URL de base, ex. http://127.0.0.1:54321
      teardown— arrêt du backend + suppression de la base jetable

    Tout est nettoyé même en cas d'échec (try/finally).
    """
    # 1. Vérifier que le binaire existe — message clair sinon.
    backend_path = Path(BACKEND_BIN)
    if not backend_path.is_file():
        pytest.fail(
            f"Binaire backend introuvable : {backend_path}\n"
            f"Compile backend_seastar, ou indique son chemin via "
            f"SEA_E2E_BACKEND_BIN."
        )

    # 2. Base MySQL jetable : nom unique (pid + uuid court).
    #
    # IMPORTANT — le préfixe `seadesktop_itest_` n'est pas cosmétique.
    # Le GRANT de tests/itest-init/01-grant.sql accorde à sea_itest
    # les droits (CREATE/DROP/INSERT/SELECT/...) UNIQUEMENT sur le
    # motif `seadesktop_itest_%`. Une base nommée autrement (ex.
    # `sea_e2e_*`) pourrait être créée, mais sea_itest n'aurait pas
    # le droit d'INSERT dans ses tables → "INSERT command denied".
    # On réutilise donc le motif déjà autorisé, plutôt que d'ajouter
    # un GRANT au conteneur. Le suffixe `e2e` reste lisible.
    db_name = f"seadesktop_itest_e2e_{os.getpid()}_{uuid.uuid4().hex[:8]}"

    conn = _connect_mysql_no_schema()
    try:
        cur = conn.cursor()
        cur.execute(f"CREATE DATABASE `{db_name}`")
        cur.close()
    finally:
        conn.close()

    http_port = _find_free_port()
    base_url = f"http://127.0.0.1:{http_port}"

    # 3. Générer le YAML de test dans un répertoire temporaire.
    work_dir = tmp_path_factory.mktemp("e2e_backend")

    # Dossier de stockage des fichiers uploadés — jetable, sous le
    # work_dir (donc nettoyé avec lui en fin de session). Le backend
    # y écrira les fichiers de la feature File.
    storage_root = work_dir / "storage"
    storage_root.mkdir(parents=True, exist_ok=True)

    yaml_path = work_dir / "itest_project.yaml"
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

    # 4. Lancer le backend en sous-processus.
    #    cwd = work_dir : les artefacts du backend (runtime/secrets,
    #    etc.) restent confinés dans le répertoire temporaire.
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
        # 5. Attendre que le serveur réponde.
        _wait_until_healthy(base_url, proc)

        # Le backend est prêt : on livre l'URL aux tests.
        yield base_url

    finally:
        # ── Teardown : arrêt du backend ──────────────────────────
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=10.0)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()

        # ── Teardown : suppression de la base jetable ────────────
        try:
            conn = _connect_mysql_no_schema()
            try:
                cur = conn.cursor()
                # TEMPORAIRE - ne PAS dropper la base pour pouvoir l'inspecter
                #print(f"\n\n>>> Base e2e laissée en place pour inspection : {db_name}\n\n")
                cur.execute(f"DROP DATABASE IF EXISTS `{db_name}`")
                cur.close()
            finally:
                conn.close()
        except mysql.connector.Error:
            # Best-effort : un échec de nettoyage ne doit pas
            # masquer le résultat des tests.
            pass


# ───────────────────────────────────────────────────────────────
# Fixtures dérivées, confort pour les tests
# ───────────────────────────────────────────────────────────────

@pytest.fixture(scope="session")
def base_url(backend_server) -> str:
    """Raccourci : l'URL de base du backend."""
    return backend_server


@pytest.fixture
def api(base_url):
    """
    Petit client HTTP centré sur l'API testée. Préfixe automatiquement
    les chemins par base_url et impose un timeout par défaut, pour que
    les tests restent concis : api.post("/auth/login", json=...).
    """
    class _Client:
        def __init__(self, root: str):
            self._root = root.rstrip("/")

        def _url(self, path: str) -> str:
            return f"{self._root}{path}"

        def get(self, path: str, **kw):
            kw.setdefault("timeout", 5.0)
            return requests.get(self._url(path), **kw)

        def post(self, path: str, **kw):
            kw.setdefault("timeout", 5.0)
            return requests.post(self._url(path), **kw)

        def put(self, path: str, **kw):
            kw.setdefault("timeout", 5.0)
            return requests.put(self._url(path), **kw)

        def delete(self, path: str, **kw):
            kw.setdefault("timeout", 5.0)
            return requests.delete(self._url(path), **kw)

    return _Client(base_url)


@pytest.fixture
def unique_email() -> str:
    """Un email unique par test — évite les collisions d'unicité
    entre tests qui créent tous des utilisateurs sur la même base de
    session."""
    return f"user_{uuid.uuid4().hex[:12]}@itest.local"
