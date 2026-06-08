"""
test_yaml.py — Génération du fichier YAML de configuration pour les
tests bout-en-bout.

Le backend SeaDesktop (`backend_seastar`) est piloté entièrement par
un fichier YAML : port d'écoute, connexion MySQL, schéma, sécurité.
Le harnais e2e génère ce YAML à la volée pour chaque session de test,
en y injectant :

  - un port libre (trouvé dynamiquement) ;
  - le nom d'une base MySQL jetable ;
  - un secret JWT fixe de test (>= 32 caractères, exigence du boot).

La STRUCTURE de ce YAML reflète exactement ce que le parser
(`yaml_schema_parser.cpp`) accepte — vérifiée dans le code source,
pas inventée :

  - `services[i]` : name, port, database, security, entities
  - `database`    : type, host, port, database_name, username, password,
                    migrations
  - `security.authentication` : type=jwt, secret, algorithm,
                    access_token_ttl, refresh_token_ttl, token_delivery
  - entité `User` avec `options.is_auth_source: true` — REQUIS pour que
    les routes /auth/* soient montées (cf. main.cpp : has_auth_source)
  - le champ `password` de type `password`, `email` de type `email`
    (les handlers register/login cherchent ces noms littéraux)
  - le champ `role` — le RegisterHandler injecte d'office
    record["role"] = "user" avant le create ; sans ce champ dans le
    schéma, le create échoue (champ inconnu) et register renvoie 400

Entités de test supplémentaires (juin 2026) pour exercer le CRUD
classique et les relations :
  - Team               : entité indépendante, pas de relation
  - Project (→ Team)   : BelongsTo Team avec on_delete=restrict, sert
                         à tester GET/PUT/DELETE sur entité avec FK et
                         le fix bug 9 (GenericCrudEngine::update qui
                         appelait l'UPDATE seulement si BelongsTo).
  - Tag (M2M Project)  : many_to_many via table pivot `project_tags`,
                         pour tester attach/detach et list_m2m.
"""

from __future__ import annotations

import textwrap


# Nom du service tel que passé au backend via --service_name.
SERVICE_NAME = "ItestService"

# Secret JWT de test. Le boot exige >= 32 caractères quand le secret
# est fourni en clair dans le YAML. Valeur fixe : les tests doivent
# être déterministes (un secret aléatoire invaliderait les tokens
# entre deux lancements, mais ici un seul lancement par session).
JWT_TEST_SECRET = "itest-jwt-secret-0123456789-abcdefgh"  # 36 caractères


def render_test_yaml(
    *,
    http_port: int,
    db_host: str,
    db_port: int,
    db_name: str,
    db_user: str,
    db_password: str,
    storage_root: str,
    access_ttl: str = "15m",
    refresh_ttl: str = "24h",
) -> str:
    """
    Produit le contenu YAML complet du service de test.

    Les TTL sont des chaînes de durée (ex. "15m", "24h") parsées par
    parse_duration() côté backend.

    L'entité User porte is_auth_source=true (sinon pas de routes
    /auth/*). L'entité Document a un champ File 'attachment' : sa
    présence + le bloc storage activent toute la pile fichiers
    (FileService, FileUploadExtractor, routes multipart + download).

    storage_root : dossier racine du stockage filesystem. Le harnais
    fournit un dossier temporaire jetable, nettoyé en fin de session.
    """
    return textwrap.dedent(f"""\
        project:
          name: ItestProject

        services:
          - name: {SERVICE_NAME}
            port: {http_port}

            database:
              type: mysql
              host: {db_host}
              port: {db_port}
              database_name: {db_name}
              username: {db_user}
              password: {db_password}
              migrations:
                enabled: true
                create_database_if_missing: false

            # Storage filesystem : active la pile fichiers dès qu'une
            # entité a un champ File (ici Document.attachment). Le
            # root_path est un dossier jetable fourni par le harnais.
            storage:
              backend: filesystem
              root_path: {storage_root}
              file_mode: "0640"
              directory_mode: "0750"

            security:
              authentication:
                type: jwt
                secret: {JWT_TEST_SECRET}
                algorithm: HS256
                access_token_ttl: {access_ttl}
                refresh_token_ttl: {refresh_ttl}
                token_delivery: body
                # Token tracking ACTIVÉ : sans ce bloc, le
                # TokenTrackingService tourne en mode no-op (défaut
                # enabled_ = false côté domaine) — la révocation au
                # logout ne ferait rien et /auth/refresh marcherait
                # encore après un logout. Sa simple présence l'active
                # (le parser met enabled=true par défaut quand le
                # bloc existe). Le bootstrapper crée alors les tables
                # refresh / revoked nécessaires.
                token_tracking:
                  enabled: true
                # ─── CORS : config explicite pour les tests F2 ──────
                # Origines autorisées : un cas dev (localhost) et un
                # cas prod (https). Toute autre Origin est rejetée
                # (pas de header Access-Control-Allow-Origin dans la
                # réponse) — c'est ce que demande la spec CORS.
              cors:
                allowed_origins:
                  - http://localhost:3000
                  - https://app.example.com
                allowed_methods:
                  - GET
                  - POST
                  - PUT
                  - DELETE
                  - OPTIONS
                allowed_headers:
                  - Content-Type
                  - Authorization
                exposed_headers:
                  - X-Total-Count
                allow_credentials: true
                max_age: 1h
                origin_policy: strict
              http_limits:
                max_body_size: 100KB         # tests avec ~4096 bytes max, vite calculable
                max_url_length: 512        # URL standard ~200, on teste à 600
                max_query_params: 5        # tests avec 6 params
              # ─── Rate limits : config pour F4 ────────────────────
              # 5 req par fenêtre de 10s, burst = 5 (pas de marge).
              # Scope = per_user pour ne pas que la suite e2e
              # s'auto-DoS : chaque test cree un user unique
              # (unique_email), donc chaque test a son propre bucket.
              # Les routes non-auth (ex: /health, /auth/login) ne sont
              # pas rate-limitees (identify_client retourne '' pour
              # per_user sans X-User-Id, et le middleware skip).
              rate_limits:
                - scope: per_user
                  requests: 5
                  window: 10s
                  burst: 20
              authorization:
                enabled: true
                default_policy: allow
                roles_claim_name: role
                admin_role: admin
                default_allow_admin: true
                roles: [admin, manager, user]
            entities:
              - name: User
                options:
                  is_auth_source: true
                fields:
                  - name: id
                    type: uuid
                    required: true
                    unique: true
                  - name: email
                    type: email
                    required: true
                    unique: true
                  - name: password
                    type: password
                    required: true
                  - name: role
                    type: string
                    required: false
                  - name: full_name
                    type: string
                    required: false

              - name: Document
                fields:
                  - name: id
                    type: uuid
                    required: true
                    unique: true
                  - name: title
                    type: string
                    required: true
                  - name: attachment
                    type: file
                    required: false
                    file:
                      storage_path: documents/attachments
                      max_size: "10MB"
                      on_delete: cascade

              # ─── Team : entité simple, pas de relation ────────
              # Sert à tester :
              # - CRUD nominal sur entité sans FK ni champ File
              # - Le restrict-on-delete : DELETE d'un Team référencé
              #   par un Project doit échouer (409) avec un message
              #   explicite.
              - name: Team
                fields:
                  - name: id
                    type: uuid
                    required: true
                    unique: true
                  - name: name
                    type: string
                    required: true
                    unique: true
                relations:
                  - name: projects
                    kind: has_many
                    target_entity: Project
                    fk_column: team_id
              # ─── Project : BelongsTo Team (on_delete=restrict) ─
              # Sert à tester :
              # - le fix bug 9 (GenericCrudEngine::update) sur une
              #   entité AVEC relation BelongsTo (l'autre branche du
              #   nouveau code, qui itère sur les relations via
              #   do_for_each).
              # - POST avec FK invalide → 400 "Target entity not found"
              # - PUT changeant la FK vers une cible inexistante
              # - DELETE d'un Project (cascade-trivial : pas d'enfant)
              - name: Project
                fields:
                  - name: id
                    type: uuid
                    required: true
                    unique: true
                  - name: title
                    type: string
                    required: true
                  - name: team_id
                    type: uuid
                    required: true
                relations:
                  - name: team
                    kind: belongs_to
                    target_entity: Team
                    fk_column: team_id
                    on_delete: restrict

              # ─── Tag : many_to_many avec Project ──────────────
              # Sert à tester :
              # - attach (POST /tags/id/projects/project_id) ou
              #   équivalent (la route exacte dépend de comment ton
              #   RouteGenerator nomme les routes M2M ; à vérifier
              #   au boot via les logs [ROUTE]).
              # - detach (DELETE ...)
              # - list M2M (GET /tags/id/projects ou similaire)
              # - pivot exists / not exists
              #
              # IMPORTANT : un Tag peut être attaché à plusieurs
              # Projects et vice-versa. La table pivot 'project_tags'
              # doit être créée automatiquement par le bootstrapper.
              - name: Tag
                fields:
                  - name: id
                    type: uuid
                    required: true
                    unique: true
                  - name: name
                    type: string
                    required: true
                    unique: true
                relations:
                  - name: projects
                    kind: many_to_many
                    target_entity: Project
                    pivot_table: project_tags
                    source_fk_column: tag_id
                    target_fk_column: project_id
                    
              # ─── SecretResource : entité de test ABAC isolée ────
              # Cette entité a des policies par opération différentes
              # selon le rôle. Permet de tester ABAC sans toucher
              # aux entités CRUD existantes (Team, Project, Tag, etc.).
              #
              # Policies :
              #   list / get_by_id : tous les rôles
              #   create / update  : admin + manager
              #   delete           : admin uniquement
              - name: SecretResource
                fields:
                  - name: id
                    type: uuid
                    required: true
                    unique: true
                  - name: name
                    type: string
                    required: true
                access_control:
                  list:
                    allow_roles: [admin, manager, user]
                  get_by_id:
                    allow_roles: [admin, manager, user]
                  create:
                    allow_roles: [admin, manager]
                  update:
                    allow_roles: [admin, manager]
                  delete:
                    allow_roles: [admin]
        """)
