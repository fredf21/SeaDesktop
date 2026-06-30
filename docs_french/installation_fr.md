# Installation et packaging

Ce guide couvre deux publics distincts :

- **Les utilisateurs finaux** qui veulent juste installer SeaDesktop
  et commencer à l'utiliser → voir
  [§1 Installation utilisateur final](#1-installation-utilisateur-final).
- **Les développeurs et contributeurs** qui veulent compiler
  SeaDesktop depuis les sources et produire eux-mêmes les paquets de
  distribution → voir [§2 Vue d'ensemble des composants](#2-composants-et-plateformes)
  et les sections suivantes.

Le fichier compagnon [`docker_deployment_fr.md`](./docker_deployment_fr.md)
couvre en détail le déploiement du backend via Docker.

---

## 1. Installation utilisateur final

Pas besoin de compiler quoi que ce soit pour installer SeaDesktop.
Des paquets pré-compilés sont disponibles pour les trois plateformes
principales.

### 1.1 Linux (Ubuntu 24.04 et dérivés)

SeaDesktop est distribué sous forme de deux paquets `.deb` : un pour
le backend, un pour SeaUI. Les deux peuvent être installés côte à
côte sur la même machine, ou indépendamment sur des machines
différentes.

**Backend (serveur d'API REST)**

```bash
# Téléchargement depuis GitHub Releases
wget https://github.com/fredf21/SeaDesktop/releases/download/v1.0.1/seadesktop-backend-1.0.1-Linux.deb

# Installation (résout les dépendances automatiquement)
sudo apt install ./seadesktop-backend-1.0.1-Linux.deb
```

Le paquet installe :
- Le binaire backend dans `/opt/seadesktop/bin/backend_seastar`
  (Seastar est lié statiquement - pas besoin de l'installer
  séparément)
- Un script wrapper dans `/usr/bin/seadesktop-backend`
- Une unité systemd dans
  `/lib/systemd/system/seadesktop-backend.service`
- Une configuration d'exemple dans
  `/usr/share/seadesktop/default.yaml.example`

Après l'installation, le script post-installation affiche les trois
prochaines étapes :

```bash
# 1. Créer une configuration à partir du template
sudo cp /usr/share/seadesktop/default.yaml.example /etc/seadesktop/default.yaml
sudo vim /etc/seadesktop/default.yaml
# Modifier les identifiants MySQL, le nom du projet, les services, etc.

# 2. Créer le fichier d'environnement avec le secret JWT
echo "SEA_DESKTOP_JWT_SECRET=$(openssl rand -hex 32)" | sudo tee /etc/seadesktop/seadesktop.env
sudo chmod 600 /etc/seadesktop/seadesktop.env
sudo chown root:seadesktop /etc/seadesktop/seadesktop.env

# 3. Activer et démarrer le service
sudo systemctl enable --now seadesktop-backend
```

**Note** : les étapes ci-dessus (étapes 1 à 3) servent à exécuter
le backend comme un service système via systemd. Si vous voulez
seulement utiliser SeaUI en mode Local pour gérer les backends de
façon interactive, vous pouvez sauter ces étapes — la
**configuration au premier lancement** de SeaUI (voir §1.4) crée
automatiquement la configuration nécessaire dans votre dossier
utilisateur et lance le backend à la demande via `QProcess`.
Utilisez systemd uniquement pour les déploiements non surveillés
qui doivent rester en marche en permanence.
Vérifier que le service tourne :

```bash
sudo systemctl status seadesktop-backend
sudo journalctl -u seadesktop-backend -f
curl http://localhost:8080/health
# Réponse : {"status":"RUNNING"}
```

**SeaUI (client desktop)**

```bash
# Téléchargement
wget https://github.com/fredf21/SeaDesktop/releases/download/v1.0.1/seaui-1.0.1-Linux.deb

# Installation (résout les dépendances Qt6 via apt)
sudo apt install ./seaui-1.0.1-Linux.deb
```

Le paquet installe :
- `/usr/bin/SeaUI` (le binaire, lié contre Qt 6.4 d'Ubuntu)
- `/usr/share/applications/SeaUI.desktop` (entrée de menu)
- `/usr/share/icons/hicolor/256x256/apps/seaui.png` (icône
  d'application)

Lancer SeaUI depuis le menu Applications (touche Super, taper
"SeaUI") ou depuis le terminal :

```bash
SeaUI
```

Au premier lancement, choisir entre le **mode Local** (lecture/écriture
directe des fichiers YAML sur le système de fichiers) ou le **mode
Remote** (connexion à un backend déployé via HTTP/HTTPS).

### 1.2 macOS

> Les binaires v1.0 pour macOS seront publiés une fois le build
> macOS finalisé. Suivre l'état de la release sur la page GitHub
> Releases.

Lorsque disponible, installer ainsi :

```
Télécharger SeaUI-1.0.1.dmg, l'ouvrir, glisser SeaUI.app dans
Applications.
```

Le backend sur macOS tourne dans **Docker Desktop** (Seastar requiert
Linux). Voir [`docker_deployment_fr.md`](./docker_deployment_fr.md)
pour la configuration Docker. SeaUI se connecte ensuite au backend via
le mode Remote sur `http://localhost:8080`.

### 1.3 Windows

> Les binaires v1.0 pour Windows seront publiés une fois le build
> Windows finalisé. Suivre l'état de la release sur la page GitHub
> Releases.

Lorsque disponible, installer ainsi :

```
Double-cliquer sur SeaUI-1.0.1-win64.exe et suivre l'assistant
d'installation.
```

Le backend sur Windows tourne dans **Docker Desktop avec WSL2**.
SeaUI s'y connecte via le mode Remote sur `http://localhost:8080`.

### 1.4 Premier lancement - choisir son mode

Quand SeaUI s'ouvre pour la première fois, un dialog **Se
connecter à SeaDesktop** apparaît avec deux options de profil :

- **Local (intégré)** - utilisable uniquement sous Linux avec le
  `.deb` installé. SeaUI lit et écrit les fichiers YAML sur votre
  système de fichiers et gère les services backend via des
  processus natifs.
- **Distant** - se connecte à un backend déployé via HTTP/HTTPS.
  Sous macOS et Windows c'est le seul mode disponible (le backend
  tourne dans Docker). Cliquez sur **Gérer les profils**, puis sur
  **+ Ajouter Distant** pour enregistrer une URL de backend (par
  exemple `http://localhost:8080` pour Docker local,
  `https://api.example.com` pour la production).

#### Configuration au premier lancement Local

Quand vous sélectionnez **Local** pour la première fois, SeaUI
ouvre le dialog **Bienvenue dans SeaUI** qui parcourt trois
sections de configuration :

**1. Dossier de configuration**

L'utilisateur choisit où SeaUI stockera ses fichiers YAML de
projet. La valeur par défaut est
`~/.local/share/SeaDesktop/SeaUI/configs/`, mais vous pouvez
choisir n'importe quel dossier — par exemple un répertoire
versionné par Git partagé avec votre équipe. Le bouton
**Parcourir** permet de naviguer visuellement.

Une case à cocher permet de copier un projet d'exemple
(`BlogDemo.yaml`) dans le dossier pour démarrer rapidement.
Recommandé à la première installation.

**2. Identifiants MySQL**

Le backend a besoin d'identifiants MySQL pour démarrer. Remplir :
- **Hôte** (par défaut : `127.0.0.1`)
- **Port** (par défaut : `3306`)
- **Utilisateur** (par défaut : `root`)
- **Mot de passe** (utilisez **Afficher/Masquer** pour basculer
  la visibilité)

Si votre utilisateur root MySQL n'a pas de mot de passe, laissez
le champ vide.

**3. Secret JWT**

Un secret JWT cryptographique de 256 bits est généré
automatiquement. Utilisez **Régénérer** pour le remplacer si
nécessaire. Ce secret signe les tokens d'authentification pour
les projets qui utilisent l'authentification.

#### Fichiers créés sur le système de fichiers

Quand vous cliquez sur **Continuer**, SeaUI crée cette structure :

<parent>/
├── configs/                   # Vos fichiers YAML de projet
│   └── BlogDemo.yaml          # (si la case d'exemple est cochee)
└── environment/               # Secrets (a ne jamais versionner)
└── seadesktop.env         # MySQL + JWT (permissions 0600)

La séparation entre `configs/` et `environment/` vous permet de
versionner `configs/` en toute sécurité dans Git tout en gardant
les identifiants en local.

Le fichier `seadesktop.env` a le format suivant :
MYSQL_HOST=127.0.0.1
MYSQL_PORT=3306
MYSQL_USER=root
MYSQL_PASSWORD=...
SEA_DESKTOP_JWT_SECRET=...

SeaUI charge ce fichier à chaque démarrage d'un service backend
et injecte les variables dans l'environnement du processus
backend. Cela signifie que le backend peut résoudre les
références `${MYSQL_PASSWORD:-root}` dans vos projets YAML
indépendamment de la manière dont SeaUI a été lancé (terminal,
menu GNOME, etc.).

#### Reconfiguration ultérieure

Pour changer les identifiants ou déplacer le dossier configs
après le premier lancement, modifiez
`~/.config/SeaDesktop/SeaUI.conf` et mettez à jour la clé
`[local]/configsDir`, ou modifiez directement
`<parent>/environment/seadesktop.env`. Un dialog de Préférences
dédié est prévu pour une version ultérieure.

Pour les détails complets d'utilisation de SeaUI, voir
[`SEAUI_GUIDE_fr.md`](./SEAUI_GUIDE_fr.md).

### 1.5 Sécurité des routes système

SeaDesktop expose cinq routes système qui se comportent
différemment selon que l'authentification est activée ou non dans
votre YAML :

| Route | Quand `auth=none` | Quand `auth=jwt` |
|---|---|---|
| `GET /health` | Publique | Rôle admin requis |
| `GET /health/ready` | Publique | Rôle admin requis |
| `GET /openapi.json` | Publique | Rôle admin requis |
| `GET /docs` (Swagger UI) | Publique | Rôle admin requis |
| `GET /assets/swagger-ui/*` | Publique | Rôle admin requis |

En **mode développement** (sans authentification, typique pour
l'exploration locale), ces routes sont ouvertes pour que vous
puissiez naviguer la spec OpenAPI et tester les endpoints dans
Swagger UI sans authentification.

En **mode production** (authentification activée), ces routes
renvoient 401 sans JWT valide et 403 si le JWT ne porte pas le
rôle admin configuré dans `authorization.admin_role`.

**Important pour les load-balancers** : si votre YAML a
`auth=jwt`, les health checks du LB doivent inclure un JWT admin
valide dans l'en-tête `Authorization`. Alternativement, exposez
`/health` sur un port interne séparé non protégé par
l'authentification.

Le middleware de rate limiting (configuré via
`service.security.rate_limits` dans votre YAML) s'applique à
**toutes** les routes y compris les routes système.
---

## 2. Composants et plateformes

Pour les développeurs qui veulent compiler depuis les sources :
SeaDesktop a deux composants distincts avec des stratégies de build et
de distribution différentes.

| Composant | Linux natif | macOS natif | Windows natif |
|---|---|---|---|
| `backend_seastar` | Oui | Non (Seastar requiert Linux) | Non (Seastar requiert Linux) |
| `SeaUI` (desktop Qt 6) | Oui | Oui | Oui |

En pratique :

- **Le backend peut être livré de deux manières** : sous forme de
  `.deb` Linux (recommandé pour les serveurs Linux - le paquet
  embarque libmariadbcpp et lie Seastar statiquement, donc aucune
  dépendance runtime au-delà des bibliothèques Ubuntu standards), ou
  sous forme d'image Docker (recommandé pour macOS, Windows, et les
  déploiements Linux multi-services).
- **SeaUI est livré comme une application desktop native** sur chaque
  plateforme : `.deb` pour Linux, `.dmg` pour macOS, installeur NSIS
  pour Windows.

Le reste du document couvre la compilation de SeaUI depuis les sources
sur les trois plateformes, et du `.deb` backend sur Linux. Pour le
build de l'image Docker du backend, voir
[`docker_deployment_fr.md`](./docker_deployment_fr.md).

---

## 3. Linux

### 3.1 Dépendances de build

Installer la chaîne de compilation et Qt 6.4 depuis les dépôts
officiels Ubuntu 24.04 :

```bash
sudo apt install build-essential cmake ninja-build pkg-config \
    libboost-program-options-dev libboost-thread-dev libboost-filesystem-dev \
    libssl-dev libyaml-cpp-dev libfmt-dev libgnutls28-dev \
    libhwloc-dev liburing-dev libsystemd-dev \
    qt6-base-dev qt6-tools-dev qt6-tools-dev-tools \
    qt6-webengine-dev linguist-qt6 \
    libmariadb-dev
```

Pour Seastar (requis pour compiler le backend nativement), suivre les
[instructions officielles de build Seastar](https://github.com/scylladb/seastar/blob/master/HACKING.md).
La version compatible avec SeaDesktop v1.0 est épinglée au commit
`a2dd373e`.

### 3.2 Compiler SeaUI en ligne de commande

SeaUI se compile contre Qt 6.4 d'Ubuntu (pas besoin de l'installeur Qt) :

```bash
cd /chemin/vers/SeaDesktop

# Configuration pour le .deb SeaUI (Release, sans backend)
cmake -B build/debian-seaui \
    -DCMAKE_BUILD_TYPE=Release \
    -DPACKAGE_SEAUI_ONLY=ON

# Build SeaUI
cmake --build build/debian-seaui -j --target SeaUI
```

Le binaire est produit dans `build/debian-seaui/apps/SeaUI/SeaUI`.
Pour le lancer directement :

```bash
build/debian-seaui/apps/SeaUI/SeaUI
```

### 3.3 Compiler le backend en ligne de commande

```bash
# Configuration pour le .deb backend (Release, sans SeaUI)
cmake -B build/debian-backend \
    -DCMAKE_BUILD_TYPE=Release \
    -DPACKAGE_BACKEND_ONLY=ON \
    -DBUILD_SEAUI=OFF

# Build (15-20 minutes en Release avec -O3)
cmake --build build/debian-backend -j --target backend_seastar
```

Le binaire est produit dans
`build/debian-backend/apps/Backend_Seastar/backend_seastar` (environ
15 Mo en Release, strippé).

### 3.4 Compiler avec Qt Creator

Pour le développement quotidien, ouvrir `CMakeLists.txt` dans Qt
Creator et configurer le projet avec le kit Qt 6.8 Desktop (ou Qt 6.4
d'Ubuntu - les deux fonctionnent). La configuration de build par
défaut est Debug ; basculer en Release dans **Projets, Compilation**
pour les builds de distribution.

Pour produire les paquets `.deb`, il est plus simple d'utiliser le
workflow en ligne de commande ci-dessus, car les builds Release
utilisent des dossiers de build distincts avec des options CMake
spécifiques (`PACKAGE_BACKEND_ONLY`, `PACKAGE_SEAUI_ONLY`).

### 3.5 Générer les paquets .deb

Le projet utilise CPack pour produire des paquets Debian directement
depuis les règles d'installation CMake. Deux dossiers de build
distincts produisent deux paquets distincts.

**`.deb` Backend** :

```bash
cd /chemin/vers/SeaDesktop

# Configuration et build (15-20 min en Release)
cmake -B build/debian-backend \
    -DCMAKE_BUILD_TYPE=Release \
    -DPACKAGE_BACKEND_ONLY=ON \
    -DBUILD_SEAUI=OFF
cmake --build build/debian-backend -j

# Génération du .deb
cd build/debian-backend
cpack -G DEB
# Produit seadesktop-backend-1.0.1-Linux.deb (~5,7 Mo)
```

Layout du paquet :

```
/opt/seadesktop/bin/backend_seastar       (binaire)
/opt/seadesktop/lib/libmariadbcpp.so      (embarqué)
/opt/seadesktop/lib/mariadb/plugin/*.so   (5 plugins MariaDB)
/usr/bin/seadesktop-backend               (wrapper)
/lib/systemd/system/seadesktop-backend.service
/usr/share/seadesktop/default.yaml.example
```

Le wrapper définit `LD_LIBRARY_PATH=/opt/seadesktop/lib` pour que le
`libmariadbcpp.so` embarqué soit utilisé au runtime. Le script
postinst crée l'utilisateur système `seadesktop` et les dossiers
nécessaires.

**`.deb` SeaUI** :

```bash
# Configuration et build (3-5 min en Release)
cmake -B build/debian-seaui \
    -DCMAKE_BUILD_TYPE=Release \
    -DPACKAGE_SEAUI_ONLY=ON
cmake --build build/debian-seaui -j

# Génération du .deb
cd build/debian-seaui
cpack -G DEB
# Produit seaui-1.0.1-Linux.deb (~2,3 Mo)
```

Layout du paquet :

```
/usr/bin/SeaUI
/usr/share/applications/SeaUI.desktop
/usr/share/icons/hicolor/256x256/apps/seaui.png
```

Le paquet dépend de `libqt6core6t64`, `libqt6widgets6t64`,
`libqt6network6t64`, `libqt6webenginewidgets6` et autres - tous
fournis par les paquets `qt6-*` officiels d'Ubuntu.

### 3.6 Générer un AppImage (alternative de distribution)

Pour une distribution mono-fichier qui fonctionne sur n'importe quel
Linux moderne sans sudo :

```bash
# Installer linuxdeploy et le plugin Qt (une fois)
wget https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
wget https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
chmod +x linuxdeploy-*.AppImage

# Construire l'AppImage depuis l'arbre d'installation
cmake --install build/debian-seaui --prefix AppDir/usr
./linuxdeploy-x86_64.AppImage --appdir AppDir --plugin qt --output appimage
```

Le résultat est `SeaUI-x86_64.AppImage`, exécutable sur n'importe
quelle distribution Linux avec glibc >= 2.31.

---

## 4. macOS

### 4.1 Dépendances de build

Installer Qt 6.8+ via l'installeur open source de [qt.io](https://qt.io)
(le Qt fourni par Homebrew est utilisable mais plus difficile à
synchroniser avec les machines de dev).

Installer les outils en ligne de commande de Xcode :

```bash
xcode-select --install
```

### 4.2 Compiler SeaUI en ligne de commande

Le backend ne peut pas être compilé nativement sur macOS (Seastar est
réservé à Linux). Compiler uniquement SeaUI sur macOS, et faire
tourner le backend dans Docker.

```bash
cd /chemin/vers/SeaDesktop

cmake -B build/release \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SEAUI=ON \
    -DCMAKE_PREFIX_PATH=~/Qt/6.8.3/macos \
    -G Ninja

cmake --build build/release --target SeaUI
```

Le résultat est un bundle macOS dans
`build/release/apps/SeaUI/SeaUI.app`.

### 4.3 Compiler avec Qt Creator

Ouvrir `CMakeLists.txt`, configurer avec le kit Qt 6.8 macOS, basculer
en Release, compiler. Qt Creator gère la génération du bundle
automatiquement.

### 4.4 Rendre le bundle autonome avec macdeployqt

Le bundle compilé dépend des frameworks Qt à leur emplacement
d'installation initial. Pour le rendre portable, lancer `macdeployqt` :

```bash
~/Qt/6.8.3/macos/bin/macdeployqt \
    build/release/apps/SeaUI/SeaUI.app
```

Cela copie tous les frameworks Qt requis dans
`Contents/Frameworks/` du bundle et réécrit le rpath. Le bundle est
maintenant autonome et peut être déplacé sur n'importe quel Mac.

### 4.5 Signer l'application

Pour une distribution hors Mac App Store, signer avec son Apple
Developer ID :

```bash
codesign --deep --force --verbose \
    --sign "Developer ID Application: Votre Nom (TEAMID)" \
    build/release/apps/SeaUI/SeaUI.app
```

Vérifier :

```bash
codesign --verify --deep --strict build/release/apps/SeaUI/SeaUI.app
```

### 4.6 Notariser et appliquer

Pour l'acceptation par Gatekeeper sur macOS 10.15+ :

```bash
# Créer un zip pour l'upload de notarisation
ditto -c -k --keepParent build/release/apps/SeaUI/SeaUI.app SeaUI.zip

# Soumettre
xcrun notarytool submit SeaUI.zip \
    --apple-id "votre@email.com" \
    --team-id "TEAMID" \
    --password "@keychain:AC_PASSWORD" \
    --wait

# Une fois acceptée, appliquer le résultat
xcrun stapler staple build/release/apps/SeaUI/SeaUI.app
```

L'entrée `AC_PASSWORD` du trousseau doit contenir un mot de passe
spécifique à l'application, généré depuis appleid.apple.com.

### 4.7 Générer un installeur DMG

Utiliser `create-dmg` (installation via `brew install create-dmg`) :

```bash
create-dmg \
    --volname "SeaUI 1.0.1" \
    --window-pos 200 120 \
    --window-size 600 400 \
    --icon-size 100 \
    --icon "SeaUI.app" 175 190 \
    --hide-extension "SeaUI.app" \
    --app-drop-link 425 190 \
    "SeaUI-1.0.1.dmg" \
    build/release/apps/SeaUI/SeaUI.app
```

Le résultat, `SeaUI-1.0.1.dmg`, est le fichier à distribuer aux
utilisateurs. À la première ouverture, ils voient une boîte de
dialogue glisser-vers-Applications.

---

## 5. Windows

### 5.1 Dépendances de build

Installer l'une de ces chaînes de compilation :

- **MSVC** - Visual Studio 2022 avec la charge de travail C++
  (recommandé pour les binaires Qt6 officiels).
- **MinGW** - MinGW-w64 11+ (fonctionne mais avec des temps de
  link légèrement plus lents).

Installer Qt 6.8+ via l'installeur open source de [qt.io](https://qt.io),
en sélectionnant la chaîne appropriée (par exemple MSVC 2022 64-bit).

Installer CMake 3.19+ et Ninja, tous deux fournis avec l'installeur Qt
ou téléchargeables séparément.

### 5.2 Compiler SeaUI en ligne de commande

Comme sur macOS, le backend ne peut pas être compilé nativement sur
Windows. Seul SeaUI se compile ; faire tourner le backend dans Docker
Desktop avec le backend WSL2.

Ouvrir une "Developer Command Prompt for VS 2022" (qui configure
l'environnement MSVC) et lancer :

```cmd
cd C:\chemin\vers\SeaDesktop

cmake -B build\release ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DBUILD_SEAUI=ON ^
    -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64 ^
    -G Ninja

cmake --build build\release --target SeaUI
```

Le binaire est dans `build\release\apps\SeaUI\SeaUI.exe`. L'icône est
embarquée dans l'EXE via `packaging\SeaUI.rc` et le `icons\seaui.ico`
multi-résolution.

### 5.3 Compiler avec Qt Creator

Ouvrir `CMakeLists.txt`, sélectionner le kit Qt 6.8 MSVC, basculer en
Release, compiler. Qt Creator gère l'environnement MSVC
automatiquement.

### 5.4 Rendre l'EXE autonome avec windeployqt

L'EXE dépend des DLLs Qt à leur emplacement d'installation. Utiliser
`windeployqt` pour les copier à côté de l'EXE :

```cmd
C:\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe ^
    --release ^
    --no-translations ^
    build\release\apps\SeaUI\SeaUI.exe
```

`windeployqt` analyse l'EXE, copie toutes les DLLs Qt requises
(`Qt6Core.dll`, `Qt6Widgets.dll`, etc.) et les plugins de plateforme
dans le même dossier. Le dossier est maintenant portable : on peut le
copier sur une autre machine Windows et l'EXE tourne.

Pour les traductions, retirer le flag `--no-translations`.

### 5.5 Générer un installeur NSIS

[NSIS](https://nsis.sourceforge.io/) produit des installeurs Windows
natifs. Depuis la racine du projet :

```cmd
cd build\release
cpack -G NSIS
```

Cela produit `SeaUI-1.0.1-win64.exe`, un installeur qui :

- Permet à l'utilisateur de choisir le dossier d'installation (par
  défaut `C:\Program Files\SeaUI`)
- Copie l'EXE, toutes les DLLs de `windeployqt`, et l'icône
- Crée un raccourci dans le menu Démarrer et un raccourci optionnel
  sur le Bureau
- Enregistre un désinstalleur dans Ajouter/Supprimer des programmes

CPack lit les métadonnées (chemin de l'icône, dossier d'installation,
nom du raccourci, fichier de licence) depuis les variables
`CPACK_NSIS_*` du `CMakeLists.txt` racine.

### 5.6 Signer l'installeur

Pour l'acceptation par Windows SmartScreen, signer avec un certificat
de signature de code d'une autorité de certification reconnue
(DigiCert, Sectigo, etc.) :

```cmd
signtool sign /fd SHA256 /a /tr http://timestamp.digicert.com /td SHA256 ^
    SeaUI-1.0.1-win64.exe
```

Le flag `/a` choisit le premier certificat de signature valide du
magasin de certificats Windows ; sinon utiliser `/f
chemin\vers\cert.pfx /p mot_de_passe` pour un fichier de certificat
explicite.

---

## 6. Vérification post-installation

Quelle que soit la plateforme, valider que l'installation fonctionne
de bout en bout :

1. Lancer SeaUI depuis le menu système (menu Démarrer sur Windows,
   Launchpad sur macOS, Activités sur GNOME).
2. La boîte de dialogue **Connexion à SeaDesktop** apparaît.
3. Sélectionner le profil **Local (intégré)** et cliquer sur Se
   connecter - uniquement sur Linux avec le `.deb` installé. Sur
   macOS/Windows, passer directement à l'étape 4.
4. Utiliser **Gérer les profils, puis + Ajouter Distant** pour
   enregistrer un profil distant pointant vers le backend Docker
   (typiquement `http://localhost:8080` si Docker Desktop tourne sur
   la même machine, ou `https://api.votreserveur.com` pour un
   déploiement distant).
5. Se connecter avec les identifiants administrateur.
6. Vérifier que la liste des projets se charge. L'application est
   fonctionnelle.

L'icône d'application doit apparaître dans :

- **GNOME (Ubuntu)** - le dock (barre des tâches). La barre de titre
  est vide par design ; l'icône apparaît à la place sous forme de
  petit logo dans la barre d'outils en haut de l'application.
- **KDE, XFCE** - la barre de titre, la barre des tâches, et Alt+Tab.
- **Windows** - barre de titre, barre des tâches, Alt+Tab, menu
  Démarrer.
- **macOS** - Dock, Launchpad, et Spotlight (pas d'icône dans la
  barre de titre par convention macOS).

Si l'une de ces apparitions manque, voir la section dépannage de
[`SEAUI_GUIDE_fr.md`](./SEAUI_GUIDE_fr.md).
