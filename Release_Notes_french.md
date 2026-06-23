# SeaDesktop Release Notes

## v1.0.0 - Version production-ready (2026-06-23)

🎯 **Première version production-ready.** Amène la plateforme à la
complétude fonctionnelle : déploiement backend conteneurisé via
Docker, orchestration multi-services, mode Remote de SeaUI pour gérer
des backends déployés depuis n'importe quel OS, validation HTTPS,
packaging Linux natif via `.deb`, et documentation extensive en
anglais et en français.

Cette version clôt le cycle de développement v0.x en consolidant tout
en un produit stable et distribuable. Le backend peut désormais être
livré soit comme un `.deb` Linux (avec Seastar lié statiquement et
libmariadbcpp embarqué), soit comme une image Docker. SeaUI est livré
comme `.deb` Linux avec icône embarquée et intégration `.desktop` ;
les installeurs `.dmg` macOS et NSIS Windows sont documentés et prêts
à être produits.

---

### ✨ Ajouté

#### Conteneurisation Docker et orchestration multi-services

**Dockerfile pour le backend** basé sur Ubuntu 24.04 avec Seastar
compilé depuis les sources (épinglé au commit `a2dd373e`) et le
connecteur MariaDB C++ embarqué. L'image tourne en tant qu'utilisateur
système non-root `seadesktop` avec les capabilities `CAP_IPC_LOCK` et
`CAP_SYS_NICE` pour le scheduling Seastar. La configuration est montée
en lecture seule depuis l'hôte et sélectionnée via un argument
`--config` par container.

**docker-compose.yml** : orchestration production avec un container
MySQL et N containers backend, un par définition de service. Les
health checks, depends_on et bind de volumes sont préconfigurés.
Ajouter un nouveau service revient à dupliquer un bloc de service
dans le YAML.

**docker-compose.override.yml** : surcharge de développement avec
logging niveau debug, mounts des sources, et rechargement à chaud des
YAML de configuration sans rebuilder l'image.

#### Mode Remote de SeaUI pour gérer les backends déployés

SeaUI gagne une **boîte de dialogue Connexion** au démarrage qui
laisse l'utilisateur choisir entre le mode **Local (intégré)**
(lecture/écriture des fichiers YAML directement sur le système de
fichiers, Linux uniquement) et un mode **Distant** qui dialogue avec
un backend déployé via HTTP/HTTPS. Plusieurs profils Distant peuvent
être enregistrés via le **Gestionnaire de profils**, chacun avec son
URL de base, sa liste de projets et ses identifiants mémorisés.

En mode Distant, SeaUI utilise la nouvelle abstraction
`IProjectRepository` pour déléguer toutes les opérations de gestion
de projet aux endpoints REST `/admin/projects/*` du backend. Le
repository est entièrement asynchrone (`QFuture<T>`) et change
d'implémentation au runtime en fonction du profil actif, sans
changement de comportement dans la couche UI.

Une fenêtre `RemoteLogsViewer` diffuse en temps réel les loggers
backend (`sea.persistence`, `sea.application`, `sea.http`,
`sea.boot`) via Server-Sent Events sur `/admin/logs/stream`. Les
logs sont filtrables par logger, niveau et recherche textuelle.

#### Support HTTPS validé de bout en bout

**`tests/https/`** fournit un harnais de test HTTPS autonome : un
script `generate_certs.sh` qui produit un certificat auto-signé avec
les SAN `localhost` et `127.0.0.1`, une configuration `nginx.conf`
de reverse proxy qui front le backend sur le port 443, et une
surcharge `docker-compose.https.yml` qui place nginx devant n'importe
quel service du compose principal.

**Workflow testé** :
1. Lancer `bash tests/https/generate_certs.sh` pour produire
   `tests/https/cert.pem` et `tests/https/key.pem` (ignorés par git).
2. Démarrer la stack avec `docker compose -f docker-compose.yml -f
   docker-compose.https.yml up -d`.
3. Curl avec `-k` fonctionne ; curl sans `-k` échoue correctement
   sur le certificat auto-signé (comportement attendu).
4. Ajouter le certificat au truststore système (`sudo cp
   tests/https/cert.pem
   /usr/local/share/ca-certificates/seadesktop-test.crt && sudo
   update-ca-certificates`) et SeaUI Remote se connecte sur
   `https://localhost` nativement, en utilisant le magasin CA système.

En production avec un vrai certificat (Let's Encrypt, CA
commerciale), aucune configuration SeaUI n'est nécessaire - Qt
utilise le truststore système automatiquement. Idem pour macOS et
Windows.

#### Packaging Linux natif via `.deb`

Deux paquets `.deb` sont produits via CPack en mode Release :

**`seadesktop-backend-1.0.0-Linux.deb`** (5,7 Mo) :
- `/opt/seadesktop/bin/backend_seastar` (binaire Release 14,7 Mo,
  strippé, Seastar lié statiquement)
- `/opt/seadesktop/lib/libmariadbcpp.so` (embarqué)
- `/opt/seadesktop/lib/mariadb/plugin/*.so` (5 plugins MariaDB)
- `/usr/bin/seadesktop-backend` (wrapper qui définit
  `LD_LIBRARY_PATH=/opt/seadesktop/lib`)
- `/lib/systemd/system/seadesktop-backend.service` (unit durcie avec
  `NoNewPrivileges`, `ProtectSystem=strict`, capabilities)
- `/usr/share/seadesktop/default.yaml.example` (template de
  configuration)

Le script `postinst` crée l'utilisateur système `seadesktop`, les
dossiers `/var/lib/seadesktop`, `/var/log/seadesktop` et
`/etc/seadesktop` avec les droits appropriés, recharge systemd, et
affiche les trois étapes restantes pour l'admin (créer
`/etc/seadesktop/default.yaml`, générer `SEA_DESKTOP_JWT_SECRET`,
`systemctl enable --now`).

Les dépendances sont résolues via les paquets standards Ubuntu 24.04
(`libssl3t64`, `libboost-program-options1.83.0`, `libhwloc15`,
`liburing2`, `libyaml-cpp0.8`, `libfmt9`, `libmariadb3`,
`libsystemd0`). `mysql-server` et `mariadb-server` sont recommandés ;
`nginx`, `caddy` et `traefik` sont suggérés pour la terminaison
HTTPS.

**`seaui-1.0.0-Linux.deb`** (2,3 Mo) :
- `/usr/bin/SeaUI` (binaire Release 6,3 Mo, lié contre Qt 6.4
  d'Ubuntu)
- `/usr/share/applications/SeaUI.desktop` (entrée de menu avec
  `StartupWMClass=SeaUI` pour Wayland)
- `/usr/share/icons/hicolor/256x256/apps/seaui.png` (icône 256x256)

Les dépendances sont les paquets Qt6 standards d'Ubuntu
(`libqt6core6t64`, `libqt6widgets6t64`, `libqt6network6t64`,
`libqt6webenginewidgets6`, `libqt6webenginecore6-bin`).
`seadesktop-backend` est recommandé pour les utilisateurs qui
veulent les deux composants sur la même machine.

#### Icône SeaUI multi-plateforme

SeaUI est désormais livré avec une icône d'application dans trois
formats :

- `apps/SeaUI/icons/seaui.png` (256x256, 57 Ko)
- `apps/SeaUI/icons/seaui.ico` (multi-résolution
  16/32/48/64/128/256 pour Windows)
- `apps/SeaUI/icons/seaui.icns` (multi-résolution pour bundle macOS)

L'icône est embarquée dans le système de ressources Qt via
`seaui_icons.qrc` (préfixe `/`), référencée par
`QApplication::setWindowIcon`. Sur Windows, l'EXE inclut le `.ico`
via `packaging/SeaUI.rc`. Sur macOS, le bundle déclare le `.icns`
via `MACOSX_BUNDLE_ICON_FILE` dans CMake.

Pour contourner le choix de conception de GNOME/Wayland qui masque
les icônes de barre de titre, `MainWindow` ajoute désormais une
`QToolBar` nommée "LogoToolBar" en haut avec le logo d'application
(32x32) et le titre "SeaDesktop" en gras. Cela rend l'application
visuellement identifiable sur toutes les plateformes quel que soit
le gestionnaire de fenêtres.

`main.cpp` appelle `QGuiApplication::setDesktopFileName("SeaUI")`
entre `setApplicationName` et la `ConnectionDialog` pour maintenir
l'app_id Wayland stable durant la transition dialogue-vers-fenêtre
principale. Sans cela, l'icône disparaîtrait brièvement du dock
GNOME.

#### Documentation multi-plateforme

**`docs/installation.md`** et **`docs_french/installation_fr.md`**
fournissent un guide d'installation complet réparti en :
- Installation utilisateur final (commandes `apt install`, walkthrough
  premier lancement)
- Vue d'ensemble des composants (quelles plateformes supportent quoi
  nativement)
- Linux (deps de build, workflows Qt Creator et CLI, génération des
  `.deb`, alternative AppImage)
- macOS (build, `macdeployqt`, codesign, notarytool, `create-dmg`)
- Windows (MSVC/MinGW, `windeployqt`, NSIS, signtool)
- Vérification post-installation multi-plateforme

**Sections Support des plateformes** ajoutées dans `README.md`,
`README_french.md`, `docs/docker_deployment.md` et
`docs_french/docker_deployment_fr.md`. Chacune précise quel workflow
s'applique par OS : Linux peut exécuter le backend nativement ou dans
Docker ; macOS et Windows doivent exécuter le backend dans Docker
Desktop et connecter SeaUI via le mode Remote.

#### Couverture de tests end-to-end

83 tests d'intégration C++ plus 115 tests end-to-end Python couvrent
désormais :
- Le cycle de vie CRUD complet sur tous les types de champs y compris
  binaires (`UUID`, `Binary`, `File`)
- La pagination (offset et cursor-based)
- Les relations many-to-many (`/attach` et `/detach`)
- L'upload de fichier avec parsing multipart
- L'authentification : login, register, logout, refresh, /me
- L'autorisation : politiques ABAC, vérifications au niveau route,
  ownership au niveau ressource
- Admin : listing de projets, fetch, save, create, delete, restart
- Streaming de logs via SSE

Le nouveau `tests/e2e/run_e2e.sh` orchestre deux passes pytest car
le test `test_admin_restart.py` spawne des backends séquentiellement
et toucherait sinon un assert
Seastar `sharded<MysqlConnexionPool>` au second démarrage. La
double passe maintient tous les 115 tests verts sans hacks
d'isolation au niveau processus dans les corps de test.

---

### 🔧 Modifié

#### Centralisation des endpoints admin

Les endpoints `/admin/projects/*` (list, get, save, create, delete)
et `/admin/restart` sont désormais des endpoints REST de première
classe exposés par le backend, remplaçant l'accès filesystem
précédent. SeaUI en mode Remote les utilise exclusivement. Les
endpoints appliquent le rôle admin via le middleware JWT existant.

#### Manipulation YAML dans SeaUI

Toutes les modifications YAML effectuées par SeaUI passent
désormais par de la manipulation textuelle (patching ligne par
ligne) au lieu d'un round-trip `yaml-cpp`, préservant exactement
les commentaires, le formatage et les espaces de fin. C'est
important parce que les fichiers `default.yaml` sont versionnés par
les admins et les changements depuis l'UI doivent produire des diffs
minimaux.

#### Traductions (FR)

Toutes les chaînes utilisateur ajoutées en Phase 5 (Pagination,
Many-to-many, Gestion de fichiers) et Phase 7-bis (mode Remote,
endpoints Admin) sont désormais traduites en français via
`apps/SeaUI/SeaUI_fr_FR.ts`. Le `TranslationManager` permet le
changement de langue à chaud sans redémarrage.

---

### 🐛 Corrigé

#### Assertion de démarrage Seastar `sharded<MysqlConnexionPool>`

Le backend assertait au second démarrage dans le même arbre de
processus (par exemple les tests pytest restart parallèles) car le
pointeur intelligent `sharded` n'était pas arrêté avant
destruction. Chaque chemin de code entre `mysql_pool->start()` et la
sortie du processus est désormais enveloppé dans un try/catch qui
garantit l'appel à `stop()`, y compris les handlers de signaux
d'arrêt.

#### Disparition de l'icône SeaUI sur Wayland

Sur GNOME avec Wayland, l'icône d'application disparaissait du dock
quand la `ConnectionDialog` se fermait et que `MainWindow`
s'ouvrait (l'app_id Wayland changeait). Corrigé en appelant
`setDesktopFileName("SeaUI")` après `setApplicationName` mais avant
la construction de toute fenêtre, verrouillant l'app_id sur le
fichier `.desktop` installé.

#### Bug de double-chemin PREFIX de `qt_add_resources`

La ressource d'icône initiale était à `:/icons/icons/seaui.png`
(double `icons/`) au lieu de `:/icons/seaui.png` parce que le CMake
`qt_add_resources(... PREFIX "/icons" FILES icons/seaui.png ...)`
concatène le préfixe et le chemin de fichier. Corrigé en définissant
`PREFIX "/"` pour que le chemin final soit correctement
`:/icons/seaui.png`.

#### Référence Swagger `ErrorResponse`

`ErrorResponse` était défini uniquement dans `add_auth_schemas()`
(qui ne tourne que si l'authentification est activée dans le YAML),
mais il était référencé par toutes les opérations CRUD quel que soit
le cas. Cela causait un crash de l'UI Swagger avec une erreur
"reference not found" sur les projets sans auth. Définition déplacée
dans la section de schémas inconditionnelle.

#### Divers petits correctifs

- `mysql_uses_binary_storage()` gère désormais correctement les
  types `UUID` et `File` (tous deux stockés en `BINARY(16)`).
- `check_single` retourne une struct `ResourceCheckResult` (avec
  les champs `.allowed` et `.reason`) au lieu d'un bool nu, ce qui
  induisait en erreur la couche de contrôle d'accès.
- La pivot table `insert_pivot()` enveloppe maintenant correctement
  les chaînes UUID de 36 caractères avec `UUID_TO_BIN(?, 1)` quand
  les colonnes pivot sont `BINARY(16)` (détecté via une heuristique
  sur les positions des tirets).

---

### 📊 Tests end-to-end validés

- **565 tests unitaires Qt** (côté SeaUI)
- **83 tests d'intégration C++** (`sea_integration_tests` contre un
  backend réel + MySQL)
- **115 tests end-to-end Python** (93 de base + 19 admin_projects + 3
  admin_restart)
- **Total : 763 tests**, tous verts via `tests/e2e/run_e2e.sh`

Validation HTTPS : SeaUI Remote se connecte avec succès sur
`https://localhost` contre le cert auto-signé une fois ajouté au
truststore système. Les certificats Let's Encrypt en production
fonctionnent sans configuration.

Validation install `.deb` : `seadesktop-backend-1.0.0-Linux.deb` et
`seaui-1.0.0-Linux.deb` s'installent via `sudo apt install ./*.deb`
sur une Ubuntu 24.04 propre, résolvent toutes les dépendances, et
tournent avec succès.

---

### 📁 Nouveaux fichiers (sélection)

```
apps/Backend_Seastar/packaging/        # Wrapper, unit systemd, postinst, prerm
apps/SeaUI/icons/                      # PNG, ICO, ICNS
apps/SeaUI/packaging/                  # .desktop.in, .rc
apps/SeaUI/seaui_icons.qrc             # Fichier de ressource Qt
apps/SeaUI/connectiondialog.*          # Selection de profil
apps/SeaUI/profilemanagerdialog.*      # Gestion des profils Remote
apps/SeaUI/httpprojectrepository.*     # Implementation HTTP du backend
apps/SeaUI/remotelogsviewer.*          # UI de streaming des logs via SSE
cmake/cpack_clean_dependencies.cmake   # Suppression des artefacts tiers
tests/https/                           # Harnais de test HTTPS
tests/e2e/test_admin_projects.py
tests/e2e/test_admin_restart.py
tests/e2e/run_e2e.sh
docker-compose.yml, .override.yml, .https.yml
docs/installation.md, docs_french/installation_fr.md
docs/docker_deployment.md, docs_french/docker_deployment_fr.md
docs/SEAUI_GUIDE.md, docs_french/SEAUI_GUIDE_fr.md
docs/FILE_FEATURE.md, docs_french/FILE_FEATURE_fr.md
docs/auth.md, docs_french/auth_fr.md
docs/admin.md, docs_french/admin_fr.md
docs/user_guide.md, docs_french/user_guide_fr.md
```

---

### 📁 Fichiers modifiés (sélection)

- `CMakeLists.txt` (racine) : bloc CPack conditionnel, option
  `PACKAGE_SEAUI_ONLY`, `EXCLUDE_FROM_ALL` pour les deps tierces
- `apps/Backend_Seastar/CMakeLists.txt` : règles d'install
  enveloppées dans `if(NOT PACKAGE_SEAUI_ONLY)`, chemins
  d'installation absolus
- `apps/SeaUI/CMakeLists.txt` : règles d'install enveloppées dans
  `if(NOT PACKAGE_BACKEND_ONLY)`, configuration des ressources Qt,
  blocs conditionnels Windows .rc / bundle macOS / .desktop Linux,
  `qt_add_translations` compatible Qt 6.4 et Qt 6.7+
- `apps/SeaUI/main.cpp` : `setDesktopFileName`, chargement de
  profils, sélection de factory `IProjectRepository`
- `apps/SeaUI/mainwindow.cpp` : QToolBar avec logo, opérations de
  projet basées sur repository
- `apps/SeaUI/localprojectrepository.cpp` : shim de compatibilité
  Qt 6.4 pour `QtFuture::makeReadyValueFuture`
- `apps/Backend_Seastar/src/main.cpp` : intégration
  SeedOrchestrator, nettoyage des handlers de signaux, garanties
  d'arrêt du pool mysql
- `libs/infrastructure/CMakeLists.txt` : `EXCLUDE_FROM_ALL` pour
  jwt-cpp
- `tests/CMakeLists.txt` : `EXCLUDE_FROM_ALL` pour doctest
- `README.md`, `README_french.md` : section Support des plateformes

---

### 📈 Statistiques

- **9 nouveaux commits** depuis v0.2.0
- **~12 000 nouvelles lignes** entre code source, tests et
  documentation
- **~3 000 lignes modifiées**
- **5 nouveaux artefacts de packaging** (.deb backend, .deb SeaUI,
  image Docker, .dmg prévu, .exe NSIS prévu)
- **2 modèles de livraison supportés** pour le backend (.deb natif
  + Docker)
- **3 plateformes documentées** (Linux, macOS, Windows) pour le
  packaging SeaUI
- **Total LoC** : ~85 000 (hors tiers et fichiers générés)

---

### 🔄 Migration depuis v0.2.0

Pour les utilisateurs qui exécutent le backend nativement depuis les
sources :
- Optionnellement passer au `.deb` pour des mises à jour plus
  simples : `sudo apt install
  ./seadesktop-backend-1.0.0-Linux.deb` au lieu de rebuilder depuis
  les sources. Le `.deb` s'installe dans `/opt/seadesktop/` donc il
  n'entre pas en conflit avec un
  `/usr/local/bin/backend_seastar` d'un `make install` précédent.
- Déplacer la configuration existante vers
  `/etc/seadesktop/default.yaml` (le chemin attendu par la nouvelle
  unit systemd) et créer `/etc/seadesktop/seadesktop.env` avec
  `SEA_DESKTOP_JWT_SECRET`.
- Remplacer toute unit systemd custom par celle installée par le
  `.deb` (`/lib/systemd/system/seadesktop-backend.service`), puis
  `sudo systemctl daemon-reload && sudo systemctl restart
  seadesktop-backend`.

Pour les utilisateurs qui exécutent le backend dans Docker :
- Pull la nouvelle image Docker (buildée depuis le même commit que
  les sources v1.0.0).
- Ajouter la surcharge `docker-compose.https.yml` pour la
  terminaison HTTPS via nginx (optionnel, on peut aussi garder son
  reverse proxy existant).
- La structure de `docker-compose.yml` est inchangée - les
  déploiements existants continuent à fonctionner.

Pour les utilisateurs de SeaUI :
- Installer le nouveau `.deb` sur Linux : `sudo apt install
  ./seaui-1.0.0-Linux.deb`. Les builds SeaUI précédents depuis les
  sources dans `/usr/local/bin/SeaUI` doivent être supprimés
  manuellement d'abord pour éviter les conflits de PATH.
- Les profils créés en v0.2.0 (mode Local) sont préservés dans
  `~/.config/SeaDesktop/SeaUI.conf`. Les nouveaux profils mode
  Remote peuvent être ajoutés via le Gestionnaire de profils.
- La boîte de dialogue `Connexion` au démarrage est nouvelle. Les
  utilisateurs qui n'utilisent que le mode Local peuvent cocher
  "Passer et toujours utiliser Local" pour la contourner.

---

## v0.2.0 - JWT Cookies, Token Tracking & Structured Logging (2026-05-15)

🎯 **Authentification JWT renforcée avec livraison par cookies et suivi
centralisé des tokens, plus système de logging structuré complet basé sur
spdlog.**

Cette release apporte un système d'authentification production-ready
(cookies HttpOnly, révocation immédiate, rotation des refresh tokens) et un
système de logs structuré utilisable depuis SeaUI via une API REST dédiée.
Elle s'accompagne d'une documentation utilisateur complète couvrant
l'ensemble des fonctionnalités déclaratives YAML de SeaDesktop.

---

### ✨ Added

#### Authentification JWT renforcée

**Livraison des tokens par cookies (`token_delivery`)**

Nouveau champ dans le YAML pour choisir comment les tokens sont transmis :

```yaml
security:
  authentication:
    token_delivery: cookie   # body | cookie | both
    cookie:
      domain: ".example.com"
      path: "/"
      secure: true
      same_site: lax
      access_token_name: sea_access
      refresh_token_name: sea_refresh
```

- Mode `body` : tokens dans le JSON de réponse (API mobile, CLI)
- Mode `cookie` : tokens dans des cookies HttpOnly (protection XSS native)
- Mode `both` : combinaison des deux pour migrations progressives

L'attribut `HttpOnly` est toujours `true` (non configurable, sécurité).

**Token tracking : révocation et rotation**

Système complet de suivi centralisé des tokens JWT :

```yaml
authentication:
  token_tracking:
    enabled: true
    refresh_table: RefreshToken
    revoked_table: RevokedToken
    cache:
      enabled: true
      ttl: "5m"
      max_size: 10000
    rotation:
      enabled: true
    auto_cleanup:
      enabled: true
      interval: "1h"
      keep_revoked_for: "30d"
```

- **Liste blanche refresh tokens** : chaque refresh émis est enregistré en
  base. Un refresh inconnu est rejeté.
- **Liste noire access tokens** : `/auth/logout` ajoute l'access token à
  la liste noire pour une révocation immédiate.
- **Rotation automatique** : chaque appel à `/auth/refresh` invalide
  l'ancien refresh et en émet un nouveau (détection de réutilisation).
- **Cache local** : vérification de la liste noire en ~200 ns au lieu
  d'une requête DB par appel.
- **Nettoyage automatique** : suppression périodique des tokens expirés.

#### Nouveaux handlers HTTP

| Handler | Route | Description |
|---|---|---|
| `RefreshHandler` | `POST /auth/refresh` | Renouvelle l'access token. Lit le refresh depuis le body ou le cookie. Émet de nouveaux tokens avec rotation. |
| `LogoutHandler` | `POST /auth/logout` | Révoque les tokens en cours. Insère dans la denylist, supprime de l'allowlist, efface les cookies. |

#### Login enrichi

Le `LoginHandler` existant a été étendu pour :

- Enregistrer le refresh token dans la liste blanche au login
- Émettre les tokens selon le mode `token_delivery` configuré
- Inclure les claims custom dans l'access token

#### ProtectedHandler avec fallback cookie

`ProtectedHandler` lit maintenant le token dans l'ordre :
1. Header HTTP `Authorization: Bearer <token>`
2. Cookie portant le nom configuré dans `cookie.access_token_name`

Un même service accepte ainsi simultanément les clients utilisant l'un ou
l'autre mode.

#### Tables système auto-gérées

Lorsque `token_tracking.enabled: true`, deux tables système sont créées
automatiquement au démarrage :

- `RefreshToken` : liste blanche des refresh tokens valides
- `RevokedToken` : liste noire des access tokens explicitement révoqués

Ces tables sont entièrement gérées par le système et n'ont pas à être
déclarées manuellement dans le YAML.

---

#### Logging structuré avec spdlog

**Configuration YAML complète du logging**

```yaml
logging:
  level: info
  modules:
    sea.http: debug
    sea.persistence: info
    seastar: warn
  sinks:
    - type: console
      format: text
      enabled: true
    - type: file
      format: json
      enabled: true
      path: "./logs/service.log"
      rotation:
        max_size: "100MB"
        time_pattern: daily
        max_files: 30
  flush_level: error
  async:
    enabled: true
    queue_size: 8192
    overflow_policy: overrun_oldest
```

#### Sept loggers nommés

Le système expose sept modules de logging configurables indépendamment :

| Module | Contenu |
|---|---|
| `sea.boot` | Démarrage, migrations, initialisations |
| `sea.http` | Handlers HTTP, autorisation, routes |
| `sea.application` | Services applicatifs |
| `sea.persistence` | Requêtes base, seeds, schéma |
| `sea.runtime` | Validation, sérialisation |
| `sea.security` | Authentification, tokens, cleanup |
| `seastar` | Logs internes du framework réseau |

Chaque module peut avoir son propre niveau via `modules:` dans le YAML.

#### Deux types de sinks

- **`console`** : écriture sur stderr avec couleurs ANSI
- **`file`** : écriture dans un fichier avec rotation (taille, temps, ou les deux)

Plusieurs sinks peuvent coexister : chaque message est envoyé à tous les
sinks actifs simultanément.

#### Deux formats de sortie

- **`text`** : lignes lisibles, idéal pour développement
  ```
  [2026-05-14 10:23:45.123] [sea.http] [info] login successful: alice@example.com
  ```

- **`json`** : JSON line-delimited, idéal pour ingestion (Loki, ELK, Datadog)
  ```json
  {"timestamp":"2026-05-14T10:23:45.123Z","logger":"sea.http","level":"info","message":"login successful"}
  ```

Échappement RFC 8259 conforme via `nlohmann/json` pour garantir la validité
des sorties JSON même avec des caractères spéciaux dans les messages.

#### Rotation des fichiers

- Par taille : `max_size: "100MB"` (formats `KB`, `MB`, `GB` acceptés)
- Par temps : `time_pattern: hourly` ou `daily`
- Combinaison : la rotation se déclenche au premier critère atteint
- Conservation configurable : `max_files: 10`

#### Logging asynchrone

```yaml
async:
  enabled: true
  queue_size: 8192
  overflow_policy: overrun_oldest   # block | overrun_oldest
```

Écriture des logs déchargée sur un thread dédié pour ne pas bloquer le
reactor Seastar. Deux politiques de débordement :

- **`overrun_oldest`** (défaut) : écrase les messages anciens, jamais de
  blocage du service
- **`block`** : attend la libération d'une place, aucune perte de messages

#### Hook Seastar → spdlog

Les logs internes du framework Seastar sont automatiquement capturés et
routés vers le logger `seastar` via un `std::streambuf` custom avec
bufferisation thread-local. Aucune perte de logs lors de la transition.

#### Endpoint `/admin/logs` avec ring buffer mémoire

Un buffer mémoire FIFO conserve en permanence les **10 000 derniers
messages**, indépendamment des sinks configurés. Exposé via deux endpoints
REST protégés (authentification JWT + rôle admin) :

```
GET /admin/logs                    # consultation avec filtrage
GET /admin/logs/loggers            # liste des modules disponibles
```

Filtres supportés :

| Paramètre | Description |
|---|---|
| `limit` | Nombre max d'entrées (défaut 100, max 1000) |
| `level` | Filtre par niveau minimum (trace/debug/info/warn/error/critical) |
| `logger` | Filtre exact par nom de module |
| `since` | Polling incrémental via `sequence_id` |
| `search` | Recherche insensible à la casse dans le message |

Pattern de polling incrémental pour suivi temps réel :

```bash
# Première requête
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8081/admin/logs?limit=100"
# next_sequence_id: 12345

# Suivantes : ne récupèrent que les nouveaux
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8081/admin/logs?since=12345"
```

#### Configuration de l'admin role

Le rôle administrateur permettant d'accéder à `/admin/logs` est entièrement
configurable via le YAML :

```yaml
authorization:
  admin_role: "administrateur"   # ou "superuser", "root", etc.
```

#### Documentation utilisateur complète

Quatre documents de référence rédigés et basés exclusivement sur le code
source vérifié :

| Document | Contenu |
|---|---|
| `seadesktop_user_guide.md` | Guide utilisateur global : tous les YAML keys, valeurs acceptées, défauts, comportements |
| `auth.md` | Authentification JWT : tokens, cookies, tracking, rotation |
| `pagination.md` | Pagination : 3 modes (page/offset/cursor), sortable_fields, exemples |
| `logging.md` | Logging : niveaux, modules, sinks, rotation, async, /admin/logs |

Chaque document suit le standard d'exhaustivité de `FILE_FEATURE_USER_GUIDE.md` :
tableaux exhaustifs des clés YAML, sections "Comportement attendu", exemples
concrets de configurations par cas d'usage.

---

### 🔧 Changed

#### Refactorisation massive des logs `std::cerr` → spdlog

Environ **124 appels à `std::cerr`** ont été remplacés par des appels
spdlog avec le module et le niveau appropriés dans :

- `apps/Backend_Seastar/src/main.cpp` (bootstrap)
- Tous les handlers HTTP (`src/http/handlers/`)
- Tous les middlewares (`src/http/middleware/`)
- Couche persistance MySQL (bootstrapper, repository, introspector)
- `schema_differ`
- `seed_orchestrator`

Les préfixes `[BOOT]` historiques ont été supprimés (redondants avec le nom
du module logger).

#### YAML demo complet

Le fichier `SeaDesktopDemo1.yaml` a été enrichi pour illustrer toutes les
nouvelles fonctionnalités : section `logging:` complète avec tous les sinks
et la rotation, blocs `cookie:` et `token_tracking:` activés.

---

### 🐛 Fixed

#### Génération UUID dans les seeds many-to-many

Bug dans `MySQLGenericRepository::insert_pivot()` : les UUIDs étaient
insérés en tant que chaînes brutes dans des colonnes `BINARY(16)`, causant
des erreurs `Incorrect string value`.

**Fix** : détection heuristique des valeurs UUID et wrapping automatique
avec `UUID_TO_BIN(?, 1)` lors de l'insertion dans la table pivot.

#### Ordre d'include critique pour `seastar_log_bridge.cpp`

Bug subtil : l'include `<seastar/util/log.hh>` DOIT précéder
`<spdlog/spdlog.h>`. L'ordre inverse provoque une erreur de compilation
cryptique "templates can only be declared in namespace or class scope" sur
les builds release.

**Fix** : documenté et appliqué dans tous les fichiers concernés.

---

### 📊 Tests end-to-end validés

```
✅ POST /auth/login (mode body)              → tokens dans JSON
✅ POST /auth/login (mode cookie)            → cookies HttpOnly émis
✅ POST /auth/refresh                        → rotation effective (ancien révoqué)
✅ POST /auth/logout                         → access token blacklisté immédiatement
✅ GET /protected avec token révoqué         → 401 (vérification denylist + cache)
✅ GET /admin/logs sans auth                 → 401
✅ GET /admin/logs avec user normal          → 403
✅ GET /admin/logs avec admin                → 200, logs filtrables
✅ GET /admin/logs?since=N polling           → uniquement les nouveaux
✅ Logs Seastar reactor visibles dans logger seastar
✅ Rotation fichier déclenchée à 100MB       → archive créée, max_files respecté
✅ Mode async overrun_oldest                 → service jamais bloqué en charge
```

---

### 📁 Nouveaux fichiers

```
NEW   libs/sea_domain/security_scheme/
      ├── cookie_config.{h,cpp}
      ├── token_tracking_config.{h,cpp}
      └── (extension de authentification_config)

NEW   libs/sea_domain/logging/
      └── logging_config.{h,cpp}

NEW   libs/sea_application/security/
      ├── denylist_cache.{h,cpp}
      └── token_tracking_service.{h,cpp}

NEW   libs/sea_application/logging/
      ├── logging_initializer.{h,cpp}
      ├── seastar_log_bridge.{h,cpp}
      └── ring_buffer_sink.{h,cpp}

NEW   apps/Backend_Seastar/src/http/handlers/auth/
      ├── refresh_handler.{h,cpp}
      └── logout_handler.{h,cpp}

NEW   apps/Backend_Seastar/src/http/handlers/admin/
      └── logs_handler.{h,cpp}

NEW   docs/
      ├── seadesktop_user_guide.md
      ├── auth.md
      ├── pagination.md
      └── logging.md
```

### 📁 Fichiers modifiés

```
MOD   libs/infrastructure/yaml/yaml_schema_parser.{h,cpp}
      (+ parse_cookie_config, parse_token_tracking_config,
         parse_logging_node, parse_sink_node, parse_rotation_node,
         parse_async_node, helpers parse_duration et parse_size)

MOD   apps/Backend_Seastar/src/http/handlers/auth/
      ├── login_handler.{h,cpp}      (+ token tracking, cookies)
      └── protected_handler.{h,cpp}  (+ fallback cookie + denylist check)

MOD   apps/Backend_Seastar/src/http/routing/route_registration.{h,cpp}
      (+ refresh, logout, /admin/logs routes)

MOD   apps/Backend_Seastar/src/main.cpp
      (+ LoggingInitializer setup, hook Seastar, token tracking wiring,
        cleanup timer pour auto_cleanup)

MOD   libs/infrastructure/persistence/mysql/
      ├── mysql_bootstrapper.{h,cpp}     (+ tables système RefreshToken/RevokedToken)
      ├── mysql_generic_repository.cpp   (fix UUID_TO_BIN dans insert_pivot)
      └── seed_orchestrator.cpp          (+ logs spdlog)

MOD   ~124 fichiers du backend : std::cerr → spdlog::get(...)->info/warn/error(...)

MOD   SeaDesktopDemo1.yaml
      (+ section logging complète, blocs cookie et token_tracking)
```

---

### 📈 Statistiques

```
Lignes de code ajoutées       : ~3 200
Lignes de code refactorisées  : ~1 800 (passages std::cerr → spdlog)
Nouveaux fichiers             : 14 (code + tests + docs)
Nouvelles routes              : 4 (/auth/refresh, /auth/logout,
                                   /admin/logs, /admin/logs/loggers)
Nouvelles tables système      : 2 (RefreshToken, RevokedToken)
Modules de logging            : 7 (configurables indépendamment)
Documentation utilisateur     : ~5 100 lignes (4 documents)
Bugs critiques fixés          : 2
Tests end-to-end validés      : 12+
```

---

### 🔄 Migration depuis v0.1.0

#### Configurations existantes : 100 % compatibles

Toutes les nouvelles sections (`cookie:`, `token_tracking:`, `logging:`)
sont **optionnelles**. Une configuration v0.1.0 fonctionne sans modification
en v0.2.0 :

- `token_delivery` par défaut : `body` (comportement v0.1.0 préservé)
- `token_tracking.enabled` par défaut : `false` (comportement stateless préservé)
- Section `logging:` absente : défauts appliqués (console texte info async)

#### Activation progressive recommandée

```yaml
# Étape 1 : activer le logging structuré
logging:
  level: info
  sinks:
    - type: console
      format: text
      enabled: true
    - type: file
      format: json
      enabled: true
      path: "./logs/service.log"

# Étape 2 : activer les cookies pour les clients web
authentication:
  token_delivery: both    # tokens dans le body ET dans les cookies
  cookie:
    secure: true
    same_site: lax

# Étape 3 : activer le token tracking pour la révocation
authentication:
  token_tracking:
    enabled: true
    rotation:
      enabled: true
```

---

## Versions précédentes

### v0.1.0 - Fondations : Domain, ABAC, Authentification JWT

Première release stable de SeaDesktop, regroupant l'ensemble des fondations
de la plateforme : modélisation déclarative, persistance MySQL, génération
automatique de routes CRUD et système d'autorisation ABAC complet.

#### ABAC resource-aware

- `ResourceAuthorizationHelper` centralisé évaluant les règles ABAC qui
  nécessitent la ressource chargée depuis la DB
- Intégration dans 9 handlers CRUD et relationnels :
  `ListHandler`, `GetByIdHandler`, `CreateHandler`, `UpdateHandler`,
  `DeleteHandler`, `GetOneByFkHandler`, `GetWithChildrenHandler`,
  `ListByFkHandler`, `ListByFkFieldHandler`, `ListManyToManyHandler`
- 2 nouvelles routes auto-générées par relation HasMany :
  - `GET /<parent>s_with_<children>/{id}`
  - `GET /<children>/filter/with_<parent>_name/{value}`
- Configuration `abac_mode` (permissive/strict) au niveau service et entité
- 3 bugs critiques fixés (UUID MySQL, ordre routes, parser jamais appelé)

#### AuthorizationMiddleware

- Pipeline middleware étendu avec `AuthorizationMiddleware`
- `RouteAuthorizationResolver` : 8+ patterns de routes reconnus
- Stratégie C : double check parent + child sur routes relationnelles
- Logs `[AUTHZ]` exhaustifs

#### JWT avec claims custom

- `entity_id` injecté dans le JWT au login
- Headers `X-User-*` propagés depuis ProtectedHandler
- Refresh tokens (première implémentation, sans tracking ni rotation)

#### YAML Parser ABAC

- Parsing des `access_control` blocks
- Support `own_resource`, `same_scope`, `allow_roles`
- `abac_mode` configurable

#### Domain & PolicyEngine

- Domain types : `PolicySubject`, `PolicyResource`, `PolicyContext`
- `PolicyEngine` avec stratégies (subject-only, full evaluation)
- Operators evaluator
