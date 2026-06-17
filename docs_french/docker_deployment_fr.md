# Déploiement Docker

SeaDesktop est livré avec un déploiement Docker qui vous permet de
faire tourner un ou plusieurs services backend (avec une base MySQL
partagée) sur n'importe quelle machine où Docker est installé. C'est
la méthode recommandée pour exposer une instance SeaDesktop à des
clients SeaUI distants.

Ce document couvre la mise en route rapide, les paramètres de
configuration, l'architecture multi-services, le passage du
développement à la production, la maintenance courante et les
problèmes les plus fréquents.

---

## 1. Vue d'ensemble

Un déploiement Docker SeaDesktop typique ressemble à ceci :

```
Machine hôte (Linux, Mac ou Windows avec Docker)
│
├─ /var/lib/seadesktop/configs/   ← volume partagé sur l'hôte
│   ├─ TestDemo.yaml
│   ├─ BlogDemo.yaml
│   └─ FileTest.yaml
│
└─ Docker
    ├─ Container mysql            ← base de données partagée
    ├─ Container service_a        ← sert TestDemo.yaml sur le port 8080
    ├─ Container service_b        ← sert BlogDemo.yaml sur le port 8081
    └─ Container service_c        ← sert FileTest.yaml sur le port 8082
```

Plusieurs containers Backend_Seastar partagent un volume `configs/`
commun, si bien que tous voient l'ensemble des fichiers YAML projet.
Chaque container exécute un projet (sélectionné par `--config` et
`--service_name`) et expose son API REST sur son propre port. SeaUI
peut administrer tous les projets en se connectant à n'importe quel
service, puisque `/admin/projects/*` répond équivalemment sur tous
les services. L'endpoint `/admin/restart`, en revanche, ne redémarre
que le service qui a reçu l'appel.

---

## 2. Prérequis

Vous avez besoin de :

- **Docker Engine 24+** avec le plugin Compose (`docker compose`,
  pas `docker-compose`). Vérifiez avec `docker compose version`.
- **Une machine hôte Linux/Mac/Windows** avec au moins **4 Go de
  RAM** pour le build, et **2 Go de RAM libre** au runtime par
  service. Moins est possible mais le build sera plus long ou
  pourra échouer.
- **Environ 20 Go d'espace disque libre** pour le premier build
  (Seastar est compilé depuis les sources dans le cadre de
  l'image). Les rebuilds suivants sont beaucoup plus petits grâce
  au cache de couches Docker.
- **`openssl`** installé localement (ou tout outil capable de
  générer un secret aléatoire) pour produire la clé de signature
  JWT.

---

## 3. Mise en route rapide

La séquence suivante lance la stack complète depuis un clone neuf.

### 3.1 Cloner et préparer l'environnement

```bash
git clone https://github.com/yourorg/SeaDesktop.git
cd SeaDesktop

# Créer le fichier .env depuis le template
cp .env.example .env
```

### 3.2 Remplir le fichier .env

Ouvrez `.env` dans votre éditeur et renseignez :

```env
# Générer avec : openssl rand -base64 48
SEA_DESKTOP_JWT_SECRET=votre-secret-aleatoire-d-au-moins-32-caracteres

# Choisissez un mot de passe robuste
MYSQL_ROOT_PASSWORD=votre-mot-de-passe-mysql-robuste

# Chemin du dossier configs/ sur l'hôte. Défaut : ./configs
SEA_DESKTOP_CONFIGS_HOST_DIR=./configs
```

### 3.3 Construire l'image

```bash
docker compose build service_a
```

Le premier build compile Seastar depuis les sources et prend 20 à 30
minutes sur une machine typique. Soyez patient et surveillez le
compteur `[N/total]`. Les builds suivants, après une modification du
code source, prennent 2 à 5 minutes puisque Seastar reste dans le
cache de couches Docker.

### 3.4 Démarrer la stack

```bash
docker compose up -d
```

Cela lance MySQL en premier, puis les trois services d'exemple. Vous
pouvez vérifier que tout est en route avec :

```bash
docker compose ps
```

Vous devriez voir quatre containers à l'état `running`. Chaque
service backend répond sur son propre port :

```bash
curl http://localhost:8080/health
curl http://localhost:8081/health
curl http://localhost:8082/health
```

Chacun doit renvoyer `{"status":"RUNNING"}`.

### 3.5 Configuration initiale : créer un administrateur

Les services backend démarrent sans aucun utilisateur. Avant de
pouvoir les administrer via SeaUI (mode Remote) ou les endpoints
`/admin/*`, créez un compte administrateur.

Pour que cela fonctionne, votre YAML doit déclarer au moins une
entité avec `is_auth_source: true` et un champ `password`. Le
fichier `TestDemo.yaml` fourni le fait déjà. Si vous écrivez votre
propre YAML, assurez-vous d'inclure quelque chose comme :

```yaml
entities:
  - name: User
    options:
      enable_crud: true
      is_auth_source: true
      timestamps: true
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
        required: true
```

Créez ensuite l'admin via l'endpoint `/auth/register` :

```bash
curl -X POST http://localhost:8080/auth/register \
  -H "Content-Type: application/json" \
  -d '{
    "email":    "admin@example.com",
    "password": "ChangeMe123!",
    "role":     "admin"
  }'
```

Vous avez maintenant un compte administrateur. Connectez-vous via le
dialogue Connect de SeaUI (profil Remote) ou en appelant
directement `/auth/login`.

> **Note de sécurité.** En v1.0, le champ `role` est accepté tel
> quel lors de l'inscription, ce qui signifie que quiconque a un
> accès réseau à `/auth/register` peut créer un compte
> administrateur. Pour un déploiement public, désactivez
> l'inscription ouverte une fois le premier admin créé (un
> durcissement est prévu pour la v1.1).

---

## 4. Configuration

### 4.1 Variables d'environnement

Le fichier `.env` fournit les variables suivantes à
`docker-compose.yml` :

| Variable | Défaut | Rôle |
|---|---|---|
| `SEA_DESKTOP_JWT_SECRET` | (aucune) | Clé de signature des tokens JWT. Doit faire au moins 32 caractères, générée aléatoirement. **Ne doit pas changer** une fois des utilisateurs créés, sinon leurs tokens deviennent invalides. |
| `MYSQL_ROOT_PASSWORD` | (aucun) | Mot de passe root pour le container MySQL. |
| `SEA_DESKTOP_CONFIGS_HOST_DIR` | `./configs` | Chemin sur l'hôte vers le dossier contenant les projets YAML. Monté en lecture-écriture dans chaque container backend à `/app/configs`. |

### 4.2 Le volume configs

Chaque container backend monte le même dossier configs à
`/app/configs`. C'est ce qui rend les endpoints `/admin/projects/*`
équivalents entre services : un fichier écrit par un service est
immédiatement lisible par tous les autres.

Pour le développement, le défaut `./configs` (relatif au dossier
docker-compose) est pratique. Pour la production, pointez
`SEA_DESKTOP_CONFIGS_HOST_DIR` vers un emplacement persistant :

```env
SEA_DESKTOP_CONFIGS_HOST_DIR=/var/lib/seadesktop/configs
```

Ainsi, supprimer et recréer les containers n'affecte pas les
fichiers projet.

### 4.3 Ports

Dans le `docker-compose.yml` d'exemple, les trois services se lient
aux ports hôtes 8080, 8081 et 8082. Pour changer cela, éditez la
section `ports:` de chaque service. Le container écoute toujours sur
le port 8080 en interne ; seul le mapping côté hôte change.

### 4.4 Le chemin des plugins MariaDB

La bibliothèque `mariadb-connector-cpp` livrée avec SeaDesktop a un
chemin de plugins compilé en dur au moment du build. Le
`docker-compose.yml` surcharge ce chemin via la variable
d'environnement `MARIADB_PLUGIN_DIR: /usr/local/lib/mariadb/plugin`,
qui pointe vers l'emplacement où l'image runtime installe les
plugins. Ne retirez pas cette variable.

---

## 5. Architecture multi-services

### 5.1 Ajouter un service

Le `docker-compose.yml` d'exemple contient trois services
(`service_a`, `service_b`, `service_c`). Pour en ajouter un
quatrième, dupliquez un des blocs et adaptez :

```yaml
  service_d:
    image: seadesktop/backend:latest
    container_name: seadesktop_service_d
    restart: unless-stopped
    depends_on:
      mysql:
        condition: service_healthy
      service_a:
        condition: service_started
    environment:
      SEA_DESKTOP_JWT_SECRET: ${SEA_DESKTOP_JWT_SECRET}
      MYSQL_HOST: mysql
      MYSQL_USER: root
      MYSQL_PASSWORD: ${MYSQL_ROOT_PASSWORD}
      SEA_DESKTOP_CONFIGS_DIR: /app/configs
      MARIADB_PLUGIN_DIR: /usr/local/lib/mariadb/plugin
    command: >
      --config /app/configs/MonNouveauProjet.yaml
      --service_name MonServiceName
    volumes:
      - ${SEA_DESKTOP_CONFIGS_HOST_DIR:-./configs}:/app/configs
      - ./logs/service_d:/app/logs
    networks:
      - seadesktop_net
    ports:
      - "8083:8080"
```

Puis `docker compose up -d service_d`.

### 5.2 Pourquoi tous les services sur le port interne 8080 ?

À l'intérieur de chaque container, le backend écoute toujours sur
8080. Le mapping `ports:` réécrit le port côté hôte (8080, 8081,
8082, …). Cela maintient la configuration interne uniforme : la
même image et le même YAML peuvent tourner en tant que n'importe
quel service simplement en passant des arguments `--config` et
`--service_name` différents.

### 5.3 Sémantique du redémarrage

L'endpoint `/admin/restart` redémarre **uniquement le container qui
a reçu la requête**. Pour redémarrer un autre service, envoyez la
requête à l'URL de ce service.

### 5.4 Limitation : les nouveaux projets ne se déploient pas automatiquement

Créer un nouveau projet YAML via SeaUI (ou
`POST /admin/projects/...`) écrit le fichier sur le volume partagé
mais ne **démarre pas** automatiquement un nouveau container pour
celui-ci. Vous devez ajouter manuellement un nouveau bloc service
dans `docker-compose.yml` et exécuter `docker compose up -d`.

Cette limitation est intentionnelle en v1.0 : un workflow
entièrement automatisé nécessiterait soit l'accès au socket Docker
depuis un container (risque de sécurité), soit un daemon
orchestrateur dédié sur l'hôte. L'étape manuelle est le compromis
sûr. Un daemon orchestrateur est prévu pour la v1.1.

---

## 6. Déploiement production

Pour la production, utilisez `docker-compose.prod.yml` comme
override du `docker-compose.yml` de base. Il remplace les secrets
basés sur `.env` par des Docker secrets, qui sont stockés chiffrés
(en Swarm) ou comme fichiers en clair avec des permissions strictes
(en Compose mono-noeud).

### 6.1 Créer les fichiers de secrets

```bash
mkdir -p secrets
chmod 700 secrets

# Secret JWT
openssl rand -base64 48 > secrets/jwt_secret.txt

# Mot de passe MySQL
echo 'votre-mot-de-passe-robuste' > secrets/mysql_password.txt

chmod 600 secrets/*.txt
```

### 6.2 Démarrer la stack avec l'override de production

```bash
docker compose \
  -f docker-compose.yml \
  -f docker-compose.prod.yml \
  up -d
```

L'override :

- retire le mapping de port hôte pour MySQL (donc MySQL n'est
  joignable que depuis le réseau Docker interne) ;
- lit `SEA_DESKTOP_JWT_SECRET` et `MYSQL_PASSWORD` depuis des
  fichiers montés sous `/run/secrets/` au lieu de variables
  d'environnement.

### 6.3 Reverse proxy et HTTPS

L'image Docker expose du HTTP en clair. Pour HTTPS, placez un
reverse proxy (nginx, Caddy, Traefik) devant les services. Le proxy
termine le TLS, puis transmet aux containers backend par le réseau
Docker interne.

Exemple minimal nginx :

```nginx
server {
    listen 443 ssl http2;
    server_name api.example.com;

    ssl_certificate     /etc/letsencrypt/live/api.example.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/api.example.com/privkey.pem;

    location / {
        proxy_pass http://localhost:8080;
        proxy_set_header Host              $host;
        proxy_set_header X-Real-IP         $remote_addr;
        proxy_set_header X-Forwarded-For   $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }
}
```

Lorsque vous utilisez un reverse proxy, configurez votre profil
Remote dans SeaUI avec l'URL HTTPS (`https://api.example.com`)
plutôt que le port HTTP interne.

---

## 7. Maintenance

### 7.1 Consulter les logs

Pour tous les containers :
```bash
docker compose logs -f
```

Pour un seul service :
```bash
docker compose logs -f service_a
```

Le backend écrit également des logs structurés dans `/app/logs` à
l'intérieur du container, qui est monté depuis `./logs/service_X`
sur l'hôte.

### 7.2 Redémarrer un service

Pour redémarrer un seul service :

```bash
docker compose restart service_a
```

Pour déclencher un redémarrage depuis un client SeaUI distant sans
accès SSH, utilisez l'endpoint `/admin/restart` (voir `admin.md`).
Le bouton Restart de SeaUI en mode Remote appelle cet endpoint
automatiquement.

### 7.3 Mise à jour

Pour récupérer une nouvelle version du backend :

```bash
git pull
docker compose build service_a
docker compose up -d
```

Compose détecte la nouvelle image et recrée les containers sans
perdre les données MySQL ni le volume configs.

### 7.4 Sauvegarde

Deux choses à sauvegarder :

- **Les données MySQL** — le volume nommé `mysql_data`. Utilisez
  `docker compose exec mysql mysqldump ...` ou un outil de
  sauvegarde de volume classique.
- **Le dossier configs** — l'emplacement vers lequel pointe
  `SEA_DESKTOP_CONFIGS_HOST_DIR`. Sauvegarde de fichiers
  classique, puisque les YAML sont du texte.

### 7.5 Nettoyage

Pour tout arrêter sans perdre de données :
```bash
docker compose down
```

Pour arrêter **et effacer toutes les données MySQL** (rarement ce
que vous voulez) :
```bash
docker compose down -v
```

---

## 8. Résolution de problèmes

### 8.1 Le build manque de mémoire

Si la phase de build de Seastar se termine par
`Killed signal terminated program cc1plus` et `cannot allocate
memory`, baissez le parallélisme dans le Dockerfile. Trouvez cette
ligne dans le stage seastar :

```dockerfile
RUN cd /opt/seastar \
    && ./configure.py --mode=release --without-tests --without-demos --without-apps \
    && ninja -C build/release -j 2 \
    && cmake --install build/release
```

Baissez le `-j 2` à `-j 1` (plus lent mais utilise moins de RAM).

### 8.2 Le container se ferme immédiatement

Exécutez `docker compose logs service_a` pour voir la cause. Les
raisons les plus fréquentes :

- **`error while loading shared libraries`** — il manque un `.so`
  dans l'image runtime. Vérifiez que la bibliothèque est listée
  dans le `apt-get install` du stage runtime et que le dossier
  `mariadb/` des plugins est copié. Voir le Dockerfile.

- **`Plugin caching_sha2_password could not be loaded`** — la
  variable `MARIADB_PLUGIN_DIR` n'est pas définie sur le service
  dans `docker-compose.yml`. Ajoutez-la.

- **`Service introuvable: XXX`** — l'argument `--service_name` ne
  correspond à aucun service déclaré dans la liste `services:` du
  YAML. Corrigez la `command:` dans `docker-compose.yml` ou
  renommez le service dans le YAML.

### 8.3 `/auth/login` renvoie 404

Les routes `/auth/*` ne sont enregistrées que si le YAML déclare au
moins une entité avec `is_auth_source: true`. Sans cela, le backend
peut toujours vérifier les JWT (pour les endpoints protégés) mais
n'expose aucun moyen d'en obtenir un. Ajoutez le marqueur sur
l'entité User, redémarrez le service.

### 8.4 La connexion Remote SeaUI échoue avec "Login response missing access_token"

Le backend est configuré avec `token_delivery: cookie`. SeaUI ne
supporte pas l'authentification par cookie en v1.0. Changez le YAML
en `token_delivery: body` ou `token_delivery: both`, redémarrez le
service.

### 8.5 Impossible de lister les projets : "Admin role required"

L'utilisateur avec lequel vous vous êtes connecté n'a pas le rôle
administrateur. Soit réenregistrez un utilisateur avec
`"role": "admin"`, soit mettez à jour un utilisateur existant
directement en base :

```bash
docker compose exec mysql mysql -uroot -p<mot-de-passe> <base> -e \
  "UPDATE User SET role='admin' WHERE email='vous@example.com';"
```

Le nom de la table suit le nom de l'entité dans votre YAML avec la
casse ajustée par votre configuration MySQL (généralement en
minuscules : `user`).

### 8.6 L'utilisateur hôte est propriétaire des fichiers bind-mountés

À l'intérieur du container, le backend tourne sous l'UID 1000
(`seadesktop`). Sur l'hôte, les fichiers écrits par le backend
(logs, fichiers YAML générés) appartiendront à l'utilisateur local
d'UID 1000. Si votre utilisateur hôte a un UID différent, vous
pourriez avoir des problèmes de permissions.

Pour aligner les UID, soit :

- exécutez `chown -R 1000:1000 ./configs ./logs` sur l'hôte ;
- soit modifiez le Dockerfile pour utiliser le même UID que votre
  utilisateur hôte (et reconstruisez).

---

## 9. Notes d'implémentation

Cette section est destinée aux contributeurs qui veulent comprendre
les choix de build. Les utilisateurs habituels n'ont pas besoin de
la lire.

### 9.1 Dockerfile multi-stage

Le Dockerfile a trois stages logiques :

| Stage | Rôle | Invalidation du cache |
|---|---|---|
| `seastar` | Cloner et compiler Seastar depuis les sources au commit épinglé. Lourd (~25 min au premier build) mais mis en cache tant que `SEASTAR_COMMIT` ne change pas. | Uniquement si `SEASTAR_COMMIT` est modifié dans le Dockerfile. |
| `backend` | Compiler `backend_seastar` contre les bibliothèques Seastar du stage précédent. Léger (~2-5 min). | À chaque modification du code source. |
| `runtime` | Image finale. Ubuntu 24.04 minimal, bibliothèques runtime uniquement, pas de compilateur, pas de headers. ~150 Mo. | Quand le stage backend reconstruit. |

Le commit Seastar est épinglé (`a2dd373e` au moment d'écrire ces
lignes, basé sur le tag `seastar-25.05.0`). Pour mettre à jour
Seastar, changez l'ARG `SEASTAR_COMMIT` en haut du Dockerfile et
reconstruisez avec `--no-cache` sur le stage seastar.

### 9.2 Pourquoi `--without-tests --without-demos --without-apps` ?

Sans ces drapeaux, le build de Seastar compile ~400 exécutables de
test. Cela ajoute 20+ minutes de temps CPU et est la cause
principale des échecs OOM sur les machines plus petites. SeaDesktop
n'utilise pas les tests Seastar, donc on les saute.

### 9.3 Pourquoi `-j 2` ?

Le défaut pour `ninja` (qui est ce que `make -j` devient) est
d'utiliser tous les cores disponibles. Chaque compilation C++
parallèle peut consommer 1,5 à 2 Go de RAM. Sur une machine avec 8
Go et 8 cores, 8 instances simultanées de `cc1plus` épuisent
facilement la mémoire. Le `-j 2` codé en dur troque la vitesse de
build contre la fiabilité sur un large spectre de matériels.

### 9.4 Le flag CMake `BUILD_SEAUI`

Le `CMakeLists.txt` racine déclare
`option(BUILD_SEAUI "Build the SeaUI desktop application" ON)`.
Le Dockerfile passe `-DBUILD_SEAUI=OFF` parce que l'image serveur
n'a aucune utilité pour Qt6 et économiser ~500 Mo de dépendances
en vaut la peine. Le flag est par défaut à `ON` pour les builds
locaux de développement, qui ont besoin de SeaUI.

### 9.5 Le chemin des plugins mariadb-connector-cpp

Le `libmariadbcpp.so` livré sous `third_party/` est construit
depuis les sources par le développeur local et incorpore un chemin
de dossier de plugins codé en dur qui pointe vers le dossier home
du développeur
(`/home/.../third_party/mariadb-connector-cpp/install/lib/mariadb/plugin/`).
Ce chemin n'existe pas dans le container.

Le Dockerfile copie le dossier des plugins vers
`/usr/local/lib/mariadb/plugin/` et `docker-compose.yml` définit
`MARIADB_PLUGIN_DIR` à ce chemin. La variable d'environnement
surcharge le chemin codé en dur au runtime.

Un chantier v1.1 pourrait soit linker statiquement le connecteur,
soit le construire dans le container avec un chemin fixe. Pour
l'instant, la surcharge est le correctif le plus simple.
