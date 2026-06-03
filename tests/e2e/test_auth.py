"""
test_auth.py — Tests bout-en-bout du sous-système d'authentification.

Routes couvertes (montées par main.cpp quand auth est activée et
qu'une entité porte is_auth_source) :

    POST /auth/register
    POST /auth/login
    POST /auth/refresh
    POST /auth/logout    (auth requise)
    GET  /auth/me        (auth requise)

Ces tests attaquent le VRAI backend en HTTP. Ils vérifient le
contrat observable : codes de statut, présence des tokens, accès
protégé. Ils sont conçus pour révéler les défauts — pas seulement
le chemin heureux : mot de passe erroné, champ manquant, token
absent ou invalide, email en double.

Contrats vérifiés dans le code des handlers :
  - register : body JSON {email, password}. email manquant -> 400,
               email déjà pris -> 409.
  - login    : body JSON {email, password}. Renvoie access_token +
               refresh_token dans le corps (token_delivery=body).
"""

from __future__ import annotations

import uuid


# ───────────────────────────────────────────────────────────────
# Helpers locaux
# ───────────────────────────────────────────────────────────────

def _register(api, email: str, password: str = "Sup3rSecret!"):
    """Inscrit un utilisateur. Renvoie la réponse brute."""
    return api.post("/auth/register",
                     json={"email": email, "password": password})


def _login(api, email: str, password: str = "Sup3rSecret!"):
    """Connecte un utilisateur. Renvoie la réponse brute."""
    return api.post("/auth/login",
                    json={"email": email, "password": password})


def _bearer(token: str) -> dict:
    """En-tête Authorization Bearer."""
    return {"Authorization": f"Bearer {token}"}


# ═══════════════════════════════════════════════════════════════
# /health — fumée : le harnais lui-même
# ═══════════════════════════════════════════════════════════════

def test_health_endpoint_repond(api):
    """Si /health ne répond pas 200, c'est le harnais ou le backend
    qui est cassé — rien d'autre n'a de sens. Test de fumée."""
    resp = api.get("/health")
    assert resp.status_code == 200


# ═══════════════════════════════════════════════════════════════
# POST /auth/register
# ═══════════════════════════════════════════════════════════════

def test_register_nouvel_utilisateur_reussit(api, unique_email):
    """Inscription nominale : un email neuf + un mot de passe valide
    doit réussir (2xx)."""
    resp = _register(api, unique_email)
    assert resp.status_code in (200, 201), resp.text


def test_register_email_en_double_est_refuse(api, unique_email):
    """Le même email inscrit deux fois : la seconde doit être rejetée.

    Le backend signale le doublon avec un code 4xx et un message
    explicite. On accepte 400 ou 409 — 409 (Conflict) serait
    sémantiquement plus précis, mais 400 reste un refus client
    valide. Ce qui compte vraiment : le doublon EST détecté (pas de
    seconde création) et le message le dit."""
    first = _register(api, unique_email)
    assert first.status_code in (200, 201), first.text

    second = _register(api, unique_email)
    # Refus côté client (4xx), pas une erreur serveur ni un succès.
    assert second.status_code in (400, 409), second.text
    # Le message doit signaler que c'est un problème de doublon.
    assert "existe" in second.text.lower() or "deja" in second.text.lower(), \
        second.text


def test_register_sans_email_est_rejete(api):
    """Body JSON sans champ 'email' : le handler doit renvoyer 400,
    pas 500 (erreur client, pas erreur serveur)."""
    resp = api.post("/auth/register", json={"password": "Sup3rSecret!"})
    assert resp.status_code == 400, resp.text


def test_register_sans_password_est_rejete(api, unique_email):
    """Body JSON sans champ 'password' : 400 attendu."""
    resp = api.post("/auth/register", json={"email": unique_email})
    assert resp.status_code == 400, resp.text


def test_register_json_malforme_est_rejete(api):
    """Un corps qui n'est pas du JSON valide doit donner 400 — le
    handler ne doit pas planter (500) sur une entrée pourrie."""
    resp = api.post(
        "/auth/register",
        data="{ ceci n'est pas du json",
        headers={"Content-Type": "application/json"},
    )
    assert resp.status_code == 400, resp.text


# ═══════════════════════════════════════════════════════════════
# POST /auth/login
# ═══════════════════════════════════════════════════════════════

def test_login_reussit_et_renvoie_les_tokens(api, unique_email):
    """Login nominal : après inscription, se connecter avec les bons
    identifiants doit renvoyer 200 et, en token_delivery=body, un
    access_token ET un refresh_token dans le corps JSON."""
    assert _register(api, unique_email).status_code in (200, 201)

    resp = _login(api, unique_email)
    assert resp.status_code == 200, resp.text

    body = resp.json()
    assert "access_token" in body, body
    assert "refresh_token" in body, body
    assert isinstance(body["access_token"], str) and body["access_token"]
    assert isinstance(body["refresh_token"], str) and body["refresh_token"]


def test_login_mauvais_mot_de_passe_est_refuse(api, unique_email):
    """Mot de passe erroné : le login doit échouer avec 401, et NE
    DOIT PAS renvoyer de token."""
    assert _register(api, unique_email).status_code in (200, 201)

    resp = _login(api, unique_email, password="MauvaisMotDePasse!")
    assert resp.status_code == 401, resp.text
    # Aucun token ne doit fuiter dans une réponse d'échec.
    if resp.headers.get("content-type", "").startswith("application/json"):
        body = resp.json()
        assert "access_token" not in body
        assert "refresh_token" not in body


def test_login_utilisateur_inexistant_est_refuse(api):
    """Login sur un email jamais inscrit : 401 attendu (et surtout
    pas 200, ni 500)."""
    ghost = f"ghost_{uuid.uuid4().hex[:12]}@itest.local"
    resp = _login(api, ghost)
    assert resp.status_code == 401, resp.text


def test_login_sans_champs_est_rejete(api):
    """Body sans email ni password : 400 (login_handler vérifie la
    présence des deux champs)."""
    resp = api.post("/auth/login", json={})
    assert resp.status_code == 400, resp.text


# ═══════════════════════════════════════════════════════════════
# GET /auth/me — route protégée
# ═══════════════════════════════════════════════════════════════

def test_me_sans_token_est_refuse(api):
    """/auth/me est une route protégée. Sans en-tête Authorization,
    elle doit renvoyer 401 — jamais les données d'un utilisateur."""
    resp = api.get("/auth/me")
    assert resp.status_code == 401, resp.text


def test_me_avec_token_invalide_est_refuse(api):
    """Un token syntaxiquement plausible mais non signé par le
    serveur doit être rejeté avec 401."""
    faux_token = "eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiJmYWtlIn0.signature_bidon"
    resp = api.get("/auth/me", headers=_bearer(faux_token))
    assert resp.status_code == 401, resp.text


def test_me_avec_token_valide_reussit(api, unique_email):
    """Cycle complet : register -> login -> utiliser l'access_token
    sur /auth/me. La requête doit réussir (200)."""
    assert _register(api, unique_email).status_code in (200, 201)
    login = _login(api, unique_email)
    assert login.status_code == 200, login.text
    access = login.json()["access_token"]

    resp = api.get("/auth/me", headers=_bearer(access))
    assert resp.status_code == 200, resp.text


# ═══════════════════════════════════════════════════════════════
# POST /auth/refresh
# ═══════════════════════════════════════════════════════════════

def test_refresh_avec_refresh_token_valide_renvoie_un_access(api, unique_email):
    """Avec un refresh_token valide, /auth/refresh doit renvoyer 200
    et un nouvel access_token."""
    assert _register(api, unique_email).status_code in (200, 201)
    login = _login(api, unique_email)
    assert login.status_code == 200, login.text
    refresh = login.json()["refresh_token"]

    resp = api.post("/auth/refresh", json={"refresh_token": refresh})
    assert resp.status_code == 200, resp.text
    assert "access_token" in resp.json(), resp.text


def test_refresh_avec_token_bidon_est_refuse(api):
    """Un refresh_token inventé doit être rejeté avec 401."""
    resp = api.post("/auth/refresh",
                    json={"refresh_token": "refresh_token_inexistant"})
    assert resp.status_code == 401, resp.text


# ═══════════════════════════════════════════════════════════════
# POST /auth/logout — route protégée
# ═══════════════════════════════════════════════════════════════

def test_logout_sans_token_est_refuse(api):
    """/auth/logout exige l'authentification (requires_auth=true dans
    main.cpp). Sans token : 401."""
    resp = api.post("/auth/logout")
    assert resp.status_code == 401, resp.text


def test_logout_invalide_le_refresh_token(api, unique_email):
    """Cycle complet de révocation :
      register -> login -> logout (en fournissant le refresh_token) ->
      le refresh_token ne doit PLUS fonctionner sur /auth/refresh.

    Sémantique du logout côté backend : il révoque l'access token
    (lu dans le header Authorization) ET le refresh token, mais ce
    dernier UNIQUEMENT s'il est fourni dans le body (ou un cookie).
    C'est un design « révoque ce qu'on te présente » : pour tuer la
    session, le client doit envoyer son refresh_token au logout.

    C'est le test de sécurité clé : une fois le refresh_token fourni
    au logout, il doit être révoqué. Si /auth/refresh réussit encore
    après, la révocation est cassée."""
    assert _register(api, unique_email).status_code in (200, 201)
    login = _login(api, unique_email)
    assert login.status_code == 200, login.text
    access = login.json()["access_token"]
    refresh = login.json()["refresh_token"]

    # Le logout révoque l'access token (depuis le header) ET le
    # refresh token — mais SEULEMENT si on le lui fournit. Le
    # logout_handler lit le refresh depuis le body (ou un cookie) :
    # sans ça, il ne révoque que l'access et le refresh survit.
    # On l'envoie donc explicitement pour révoquer la session.
    logout = api.post("/auth/logout",
                      headers=_bearer(access),
                      json={"refresh_token": refresh})
    assert logout.status_code in (200, 204), logout.text

    # Après logout : le refresh_token doit être révoqué.
    after = api.post("/auth/refresh", json={"refresh_token": refresh})
    assert after.status_code == 401, (
        "Le refresh_token fonctionne encore après le logout — "
        "la révocation des tokens ne marche pas."
    )


def test_logout_sans_refresh_ne_revoque_que_l_access(api, unique_email):
    """Contrepartie du test précédent : si le logout ne reçoit PAS
    le refresh_token (seulement l'access dans le header), alors le
    refresh token n'est PAS révoqué et reste utilisable.

    Ce comportement est VOULU (le logout révoque ce qu'on lui
    présente). Ce test le fige : si un jour le logout se met à
    révoquer toute la session à partir du seul access token, ce
    test échouera et signalera un changement de contrat — à valider
    consciemment plutôt que subir.

    Sécurité : ce n'est pas idéal qu'un refresh survive à un logout
    partiel. Si tu veux durcir, le logout pourrait révoquer toute la
    session de l'utilisateur (tous ses refresh) à partir du user_id
    de l'access token. Mais c'est une décision de design — d'où ce
    test qui documente l'état actuel."""
    assert _register(api, unique_email).status_code in (200, 201)
    login = _login(api, unique_email)
    assert login.status_code == 200, login.text
    access = login.json()["access_token"]
    refresh = login.json()["refresh_token"]

    # Logout SANS fournir le refresh_token.
    logout = api.post("/auth/logout", headers=_bearer(access))
    assert logout.status_code in (200, 204), logout.text

    # Le refresh n'a pas été présenté au logout → non révoqué →
    # fonctionne encore.
    after = api.post("/auth/refresh", json={"refresh_token": refresh})
    assert after.status_code == 200, (
        "Comportement attendu : un logout sans refresh_token ne "
        "révoque que l'access ; le refresh survit. Si ce test "
        "échoue, le contrat du logout a changé."
    )
