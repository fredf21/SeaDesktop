# SeaUI — Guide utilisateur et technique

SeaUI est l'interface graphique de la plateforme SeaDesktop. C'est une
application Qt 6 qui permet à un développeur de gérer les projets, services et
entités SeaDesktop, de contrôler le cycle de vie des backends générés et
d'inspecter leur comportement — sans éditer les fichiers YAML à la main ni
passer par un terminal.

Ce guide documente uniquement l'application de bureau SeaUI. Le backend
Seastar n'entre pas dans son périmètre.

---

## 1. Présentation

### 1.1 À quoi sert SeaUI

Un projet SeaDesktop est décrit par un fichier YAML déclaratif. Chaque projet
contient un ou plusieurs services, et chaque service expose des entités qui
deviennent des API REST générées automatiquement. SeaUI est la couche
graphique au-dessus de ce modèle. Elle permet de :

- parcourir tous les projets présents dans le dossier `configs/`, et descendre
  dans leurs services, entités, champs et routes générées ;
- créer de nouveaux projets, services et entités via des dialogues guidés qui
  écrivent le YAML à votre place ;
- modifier les projets, services et entités existants ;
- importer et exporter des fichiers YAML ;
- démarrer, arrêter, redémarrer et recharger les processus backend des
  services ;
- s'authentifier auprès d'un service en cours d'exécution et explorer ses
  données ;
- ouvrir la documentation Swagger d'un service en cours d'exécution ;
- consulter les fichiers de journaux produits par les services ;
- changer la langue de l'interface à chaud.

### 1.2 Lien entre SeaUI et le backend

SeaUI ne communique jamais directement avec la base de données. Elle fait deux
choses :

1. Elle lit et écrit les fichiers de configuration YAML du dossier `configs/`.
2. Elle lance le backend Seastar comme processus séparé (via `QProcess`), un
   processus par service en cours d'exécution.

Lorsqu'un service est démarré, le backend lit son fichier YAML, applique les
migrations en attente selon le mode `migrations.mode` configuré, et expose
l'API REST. SeaUI communique alors avec ce service en cours d'exécution via
HTTP (pour le suivi de statut, l'authentification et l'exploration des
données). Toute modification d'un fichier YAML faite par SeaUI ne prend effet
dans la base de données que lorsque le service correspondant est (re)démarré.

### 1.3 Organisation des dossiers

SeaUI travaille avec deux dossiers dont l'emplacement dépend du type de build :

- **Dossier de configuration** — contient les fichiers YAML des projets. En
  build debug, il pointe vers le dossier `configs/` du dépôt, pour travailler
  directement sur les fichiers versionnés. En build release, c'est un dossier
  de données applicatives inscriptible standard.
- **Dossier des journaux** — contient un fichier `.log` par processus de
  service. Il suit la même règle debug/release.

Chaque projet est stocké dans un fichier unique nommé `<NomProjet>.yaml`. Le
nom du projet et le nom de son fichier sont toujours maintenus synchronisés.

---

## 2. La fenêtre principale

Au démarrage, SeaUI s'ouvre en plein écran. La fenêtre est organisée en une
série de panneaux de listes allant du plus général (projets) au plus
spécifique (champs et routes), complétés par des panneaux de détails et des
boutons d'action.

### 2.1 Les panneaux

**Projets** — liste tous les fichiers `.yaml` du dossier de configuration.
Sélectionner un projet charge ses services dans le panneau Services et vide
les panneaux situés en dessous.

**Services** — liste les services du projet sélectionné. Sélectionner un
service charge ses entités, affiche ses détails (port, type de base de
données), démarre le suivi de statut et calcule l'ensemble des routes
générées.

**Entités** — liste les entités du service sélectionné. Sélectionner une
entité charge ses champs et filtre la liste des routes pour ne montrer que
celles liées à cette entité.

**Champs** — liste les champs de l'entité sélectionnée.

**Routes** — liste les routes REST. Quand un service est sélectionné, toutes
ses routes sont affichées ; quand une entité est sélectionnée, seules les
routes de cette entité le sont. Chaque route est dessinée par un délégué
personnalisé qui colore la méthode HTTP et ajoute un badge coloré pour les
routes paginées (PAGE, OFFSET, CURSOR).

### 2.2 Panneaux de détails

Quand un service est sélectionné, les panneaux de détails affichent :

- **Détails du service** — le port et le type de base de données.
- **Statut du service** — `RUNNING` ou `STOPPED`, rafraîchi automatiquement
  par un sondage en arrière-plan. Le libellé est vert en exécution, rouge à
  l'arrêt.
- **Statut d'authentification du service** — `Connected` ou `Disconnected`,
  selon que vous êtes actuellement authentifié auprès du service.

### 2.3 Boutons d'action du service

Une rangée de boutons agit sur le service actuellement sélectionné :

- **Démarrer / Arrêter / Redémarrer** — contrôlent le processus backend du
  service. Ces boutons s'activent ou se désactivent automatiquement selon que
  le service est en cours d'exécution ou non.
- **Swagger** — ouvre la documentation Swagger du service dans une fenêtre de
  navigateur intégrée. Disponible uniquement pendant que le service tourne.
- **Logs** — ouvre le fichier de journal du service dans l'application par
  défaut du système.
- **Login / Logout** — authentifie ou déconnecte auprès du service en
  exécution.
- **Open Data** — récupère et affiche les lignes de l'entité sélectionnée dans
  un tableau.

### 2.4 Suivi de statut

Dès qu'un service est sélectionné, SeaUI sonde en arrière-plan son point
d'entrée de santé à intervalle fixe. Le résultat pilote le libellé de statut
du service et l'état d'activation des boutons d'action : un service en
exécution active Arrêter/Redémarrer/Swagger et désactive Démarrer ; un service
injoignable fait l'inverse. Les boutons reflètent ainsi toujours l'état réel
du backend, même si le service a été démarré ou arrêté en dehors de SeaUI.

---

## 3. Le menu

La barre de menu comporte quatre menus : **File**, **Edit**, **Audits** et
**Services Actions**.

### 3.1 Menu File

#### Add New Project

Crée un nouveau projet avec une configuration de production minimale.

Un dialogue demande deux valeurs : le **nom du projet** et le **nom du
service**. Les deux sont obligatoires et normalisés automatiquement — les
espaces de début et de fin sont retirés, les caractères invalides supprimés,
et les espaces internes remplacés par des underscores.

Un fichier `<NomProjet>.yaml` est ensuite créé dans le dossier de
configuration. Si un projet de ce nom existe déjà, l'opération est refusée. Le
YAML généré contient un service avec une configuration de production complète :

- un bloc base de données MySQL avec migrations activées en mode
  `conservative` ;
- un bloc sécurité : authentification JWT (le secret est lu depuis la variable
  d'environnement `${SEA_DESKTOP_JWT_SECRET}`), CORS restreint, en-têtes de
  sécurité stricts, et limites HTTP ;
- un bloc logging de production : un sink console et un sink fichier JSON avec
  rotation, le logging asynchrone étant activé.

Comme le secret JWT provient d'une variable d'environnement, le service ne
démarrera pas tant que `SEA_DESKTOP_JWT_SECRET` n'est pas définie dans
l'environnement.

#### Add New Service

Ajoute un nouveau service à un projet existant. Un dialogue demande à quel
projet l'ajouter, puis demande le nom du service. Le nouveau service est
refusé si un service de ce nom existe déjà dans le projet. Il est généré avec
la même configuration de production complète que le service créé par Add New
Project, et ajouté à la liste `services:` du YAML du projet. Le contenu
existant et les commentaires du fichier sont préservés.

#### Add New Entity

Ajoute une entité à un service. Le déroulé est :

1. choisir le projet ;
2. choisir le service dans ce projet ;
3. saisir le nom de l'entité ;
4. choisir les options de l'entité — `enable_crud`, `timestamps`,
   `soft_delete` (CRUD et timestamps activés par défaut) ;
5. saisir les champs un par un. Pour chaque champ, un dialogue demande son
   nom, son type (parmi la liste des types pris en charge) et ses attributs
   `required`, `unique` et `indexed`. Après chaque champ, SeaUI demande s'il
   faut en ajouter un autre. Au moins un champ est exigé.

Le bloc de l'entité est ensuite inséré dans la section `entities:` du service
choisi (la section est créée si elle n'existe pas), en préservant le reste du
fichier et ses commentaires.

Enfin, SeaUI demande s'il faut appliquer le changement à la base de données
maintenant. Répondre oui redémarre le service, ce qui amène le backend à
relire le YAML et à exécuter la migration selon le `migrations.mode`
configuré.

Les types de champ pris en charge sont : `string`, `int`, `float`, `bool`,
`timestamp`, `uuid`, `bigint`, `smallint`, `decimal`, `json`, `binary`,
`password`, `email`, `text`, `file`.

#### Import Yaml

Permet de choisir un fichier `.yaml` (ou `.yml`) n'importe où sur le disque et
le copie dans le dossier de configuration. Si un projet du même nom existe
déjà, SeaUI demande confirmation avant de le remplacer. Le fichier est copié
tel quel ; il est validé lors du rechargement de la liste des projets.

#### Export Yaml

Demande quel projet exporter, puis demande un chemin de destination, et y
copie le fichier YAML du projet.

### 3.2 Menu Edit

#### Edit Project

Renomme un projet. Après avoir choisi le projet et saisi le nouveau nom
(normalisé comme à la création), SeaUI demande confirmation, puis met à jour
la clé `name:` dans le YAML et renomme le fichier `<NomProjet>.yaml` en
conséquence. Le renommage est refusé si un projet du nouveau nom existe déjà.
Les valeurs dérivées du nom du projet à la création (`database_name`,
`issuer`, chemins de logs) sont volontairement laissées inchangées, pour ne
pas rompre le lien avec la base de données existante.

#### Edit Service

Modifie le port d'un service. Après avoir choisi le projet et le service, un
dialogue pré-rempli avec le port actuel demande la nouvelle valeur
(1–65535). La clé `port:` de ce service est mise à jour dans le YAML, en
laissant intacts le reste du fichier et ses commentaires.

#### Edit Entity

Modifie les options d'une entité. Après avoir choisi le projet, le service et
l'entité, un dialogue pré-rempli avec les options actuelles de l'entité permet
de basculer `enable_crud`, `timestamps` et `soft_delete`. La section
`options:` de cette entité est mise à jour (créée si absente). SeaUI propose
ensuite d'appliquer le changement à la base de données en redémarrant le
service.

#### Edit Yaml

Ouvre le fichier YAML d'un projet choisi dans un éditeur intégré — un éditeur
de texte brut en police à chasse fixe, avec Enregistrer et Annuler.
L'enregistrement réécrit le fichier et recharge le projet. C'est l'éditeur
polyvalent pour tout changement que les dialogues d'édition spécialisés ne
couvrent pas.

#### Preferences

Un sous-menu. Il contient actuellement le sous-menu **Languages** servant à
changer la langue de l'interface (voir section 8).

### 3.3 Menu Audits

#### Show All Services Logs

Ouvre une fenêtre à onglets, un onglet par service de chaque projet. Chaque
onglet affiche le contenu du fichier de journal de ce service, défilé jusqu'aux
événements les plus récents. Un service dont le fichier de journal n'existe pas
encore (jamais démarré) affiche à la place un message explicatif, et son onglet
est signalé en conséquence.

#### Choose a service to show Logs

Affiche la liste de tous les services (projet / service / port) et ouvre la
fenêtre de journaux pour le seul service que vous sélectionnez.

### 3.4 Menu Services Actions

Ces quatre entrées agissent sur tous les services de tous les projets à la
fois :

- **Start All Services** — démarre tout service qui n'est pas déjà en
  exécution.
- **Stop All Services** — arrête tout service en exécution.
- **Restart All Services** — arrête puis démarre tous les services.
- **Reload All Services** — arrête puis démarre tous les services. Comme le
  backend relit le YAML à chaque démarrage, un rechargement prend en compte
  toute modification faite aux fichiers de configuration.

Chaque action se termine par un message de confirmation.

---

## 4. Cycle de vie des services

### 4.1 Démarrer et arrêter un service

Chaque service s'exécute comme un processus backend séparé lancé par SeaUI via
`QProcess`. Un service est identifié en interne par une clé de processus
construite à partir du nom du projet, du nom du service et du port, de sorte
que deux services n'entrent jamais en collision.

Démarrer un service lance l'exécutable backend avec, en arguments, le fichier
YAML du service et le nom du service, et redirige sa sortie standard et sa
sortie d'erreur vers le fichier de journal du service (en mode ajout).
Démarrer un service déjà en exécution n'a aucun effet.

Arrêter un service termine le processus proprement, et le tue de force s'il ne
se termine pas dans un court délai.

### 4.2 Contrôles par service vs. actions groupées

Les boutons Démarrer/Arrêter/Redémarrer du panneau service agissent sur le
service actuellement sélectionné. Le menu Services Actions effectue les mêmes
opérations mais sur tous les services de tous les projets à la fois. Les deux
partagent la même logique sous-jacente de gestion des processus.

### 4.3 Reload

Reload équivaut actuellement à un redémarrage : le processus est arrêté puis
redémarré, et comme le backend relit le fichier YAML au démarrage, la nouvelle
configuration est prise en compte. Un véritable rechargement à chaud —
signaler au backend en exécution de relire sa configuration sans redémarrer —
nécessiterait un support backend qui n'existe pas encore.

---

## 5. Authentification

Lorsqu'un service est en exécution, vous pouvez vous authentifier auprès de
lui. Le bouton Login ouvre un dialogue demandant une adresse e-mail et un mot
de passe. SeaUI les envoie au point d'entrée `auth/login` du service. En cas
de succès, le jeton d'accès JWT renvoyé (et le jeton de rafraîchissement, s'il
y en a un) est conservé en mémoire pour la session, et le libellé de statut
d'authentification passe à `Connected`.

Le bouton Logout efface le jeton stocké et le statut revient à `Disconnected`.
Les boutons Login et Logout s'activent et se désactivent selon l'état
d'authentification courant.

Le jeton n'est conservé qu'en mémoire pour la session SeaUI en cours ; il
n'est pas persisté sur le disque.

---

## 6. Exploration des données et Swagger

### 6.1 Open Data

Avec un service et une entité sélectionnés, le bouton Open Data récupère les
lignes de cette entité depuis le service en exécution et les affiche dans une
fenêtre tableau. La réponse est attendue sous la forme d'un tableau JSON ;
chaque élément du tableau devient une ligne. Si le service est injoignable ou
si la réponse n'est pas un tableau JSON, un message explicatif est affiché.

### 6.2 Swagger

Le bouton Swagger ouvre la documentation Swagger du service (`/docs`) dans une
fenêtre de navigateur intégrée. Disponible uniquement pendant que le service
tourne.

---

## 7. Journaux

Il y a deux façons de consulter les journaux :

- Le **bouton Logs** du panneau service ouvre le fichier de journal du service
  sélectionné dans l'application texte par défaut du système.
- Le **menu Audits** ouvre les journaux à l'intérieur de SeaUI, dans un
  visualiseur à onglets en lecture seule — soit pour tous les services à la
  fois, soit pour un seul service choisi.

Les journaux sont affichés comme un instantané pris à l'ouverture de la
fenêtre ; ils ne sont pas rafraîchis en direct.

---

## 8. Internationalisation

SeaUI peut afficher son interface en anglais ou en français, et la langue peut
être changée à chaud sans redémarrer.

### 8.1 Changer la langue

Ouvrez **Edit ▸ Preferences ▸ Languages** et choisissez **English** ou
**Francais**. L'interface est retraduite immédiatement. Les deux entrées se
comportent comme un ensemble d'éléments cochables mutuellement exclusifs, de
sorte que la langue active est toujours indiquée par une coche. La langue
choisie est persistée et restaurée au démarrage suivant de SeaUI.

### 8.2 Fonctionnement

L'internationalisation est gérée par la classe `TranslationManager`. Elle
détient les traducteurs Qt, expose la liste des langues disponibles, applique
une langue, et persiste le choix via `QSettings`.

L'anglais est la langue source : les chaînes écrites dans le code sont déjà en
anglais, donc aucun fichier de traduction n'est chargé pour l'anglais — le
traducteur français est simplement retiré. Le français est fourni par un
fichier `.qm` compilé, embarqué dans les ressources de l'application sous
`:/i18n/`.

Quand la langue change, Qt envoie un événement `LanguageChange` à la fenêtre
principale. La fenêtre appelle alors `retranslateUi()` pour rafraîchir tous
les widgets définis dans le fichier `.ui`, et ré-applique les quelques textes
positionnés dynamiquement dans le code (le titre de la fenêtre, le libellé de
statut d'authentification).

### 8.3 Ajouter une langue

1. Ajoutez la langue à la liste déclarée dans le constructeur du
   `TranslationManager` (son code locale et son nom d'affichage).
2. Créez le fichier `SeaUI_<locale>.ts` correspondant et ajoutez-le à l'appel
   `qt_add_translations` du `CMakeLists.txt`.
3. Traduisez les chaînes (avec Qt Linguist ou en éditant le `.ts`
   directement).
4. Recompilez — `qt_add_translations` compile le `.ts` en `.qm` et l'embarque
   sous `:/i18n/`.

### 8.4 Ajouter une chaîne traduisible

Toute chaîne visible par l'utilisateur dans le code doit être encadrée par
`tr("...")`, écrite en anglais. Les chaînes placées dans le fichier `.ui` sont
traduisibles automatiquement. Après l'ajout de chaînes, les fichiers `.ts`
doivent être mis à jour (`lupdate`, lancé automatiquement par
`qt_add_translations`) et les nouvelles entrées traduites.

---

## 9. Référence technique

### 9.1 Structure des fichiers

L'application SeaUI se compose de :

- `main.cpp` — le point d'entrée. Il définit l'identité de l'application
  (nécessaire à `QSettings`), crée le `TranslationManager`, charge la langue
  persistée avant de construire la fenêtre, et affiche la fenêtre principale.
- `mainwindow.{h,cpp,ui}` — la fenêtre principale : panneaux, libellés de
  détails, boutons d'action, et tous les slots de menu.
- `translation_manager.{h,cpp}` — le gestionnaire d'internationalisation.
- `projectlistmodel`, `servicelistmodel`, `entitylistmodel`,
  `fieldlistmodel` — les modèles de liste des quatre panneaux principaux.
- `routelistmodel` + `routelistitemdelegate` — le modèle de la liste des
  routes et son délégué de rendu personnalisé (méthode HTTP colorée, badges de
  pagination).
- `servicestatuscheck` — sondeur HTTP en arrière-plan qui indique si un
  service est en exécution.

### 9.2 Les modèles de liste

Chaque panneau est alimenté par une sous-classe de `QAbstractListModel`.
`ProjectListModel` détient les projets chargés ; sélectionner un projet
alimente `ServiceListModel` ; sélectionner un service alimente
`EntityListModel` et calcule les routes ; sélectionner une entité alimente
`FieldListModel` et filtre les routes. Le modèle de routes expose des rôles
personnalisés (mode de pagination, méthode HTTP, chemin, entité/opération)
consommés par `RouteListItemDelegate` pour le rendu.

### 9.3 Édition du YAML par texte

Les actions de menu de création et d'édition modifient les fichiers YAML par
**manipulation textuelle** plutôt qu'en ré-émettant le fichier via une
bibliothèque YAML. C'est un choix délibéré : la ré-émission supprimerait les
commentaires et le formatage des fichiers générés. La contrepartie est que
l'approche textuelle doit localiser les clés avec soin.

Les helpers suivent un schéma cohérent : localiser le début du bloc concerné
(un service par sa ligne `  - name:`, la section racine `project:`, une entité
par sa ligne `      - name:`), déterminer où ce bloc se termine, puis chercher
la clé cible **à la profondeur d'indentation exacte** afin que, par exemple,
le `port:` d'un service (quatre espaces) ne soit jamais confondu avec le
`port:` de la base de données (six espaces). Les insertions ajoutent du
contenu à la fin du bloc concerné ; les éditions remplacent une valeur sur
place ou, lorsque la clé est absente, l'insèrent.

Comme l'approche textuelle s'appuie sur la structure produite par les
générateurs de SeaUI, elle est fiable pour les fichiers générés par SeaUI. Un
YAML fortement réorganisé à la main pourrait en théorie tromper la détection
des blocs ; dans ce cas, l'éditeur intégré Edit Yaml reste le recours sûr.

### 9.4 Configuration de production générée

`buildProductionServiceBlock()` génère le bloc YAML d'un service en
configuration de production ; il est partagé par Add New Project et Add New
Service. `buildProductionYaml()` l'enveloppe avec l'en-tête `project:`.
`buildEntityBlock()` génère le bloc YAML d'une entité. Toutes les clés
produites par ces générateurs sont validées par rapport au parser YAML du
backend.

### 9.5 Rafraîchissement de la liste des projets

Un `QFileSystemWatcher` surveille le dossier de configuration et recharge la
liste des projets quand des fichiers y sont ajoutés ou supprimés. Comme le
signal `directoryChanged` du watcher ne se déclenche pas quand un fichier
existant est *modifié*, les actions de menu qui modifient un fichier existant
(Add New Service, Add New Entity, Edit Service, Edit Project, Edit Entity,
Edit Yaml) appellent le rechargement explicitement après l'écriture. Les
actions qui créent un nouveau fichier (Add New Project, Import Yaml) rechargent
aussi explicitement, de sorte que l'interface ne dépend jamais du timing du
watcher.
