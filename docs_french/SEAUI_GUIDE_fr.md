# SeaUI — Guide utilisateur

SeaUI est l'application de bureau qui sert à gérer vos projets SeaDesktop.
Elle vous permet de créer et d'organiser des projets, des services et des
entités, de les exécuter et de les inspecter — le tout par une interface
graphique, sans éditer de fichiers de configuration à la main ni passer par un
terminal.

Ce guide explique comment utiliser SeaUI.

---

## 1. Premiers pas

### 1.1 Deux façons d'utiliser SeaUI

SeaUI peut dialoguer avec vos projets de deux façons différentes :

- **Mode Local** — SeaUI lit et écrit les fichiers YAML de projet
  directement sur le disque de votre ordinateur, dans le dossier
  `configs/`. Il démarre et arrête également les services sur votre
  machine sous forme de processus en arrière-plan. C'est le mode
  classique, idéal pour le développement sur une seule machine.

- **Mode Remote** — SeaUI dialogue avec un backend SeaDesktop qui
  s'exécute sur une autre machine (typiquement dans un conteneur Docker)
  par HTTP. Il lit, modifie et crée des fichiers YAML via les endpoints
  d'administration, et déclenche les redémarrages du service distant.
  Ce mode est idéal pour administrer une instance SeaDesktop déployée
  depuis votre poste.

Vous choisissez le mode au démarrage à l'aide d'un profil. SeaUI est
livré avec un profil intégré nommé **Local**, toujours disponible et
qui ne nécessite aucune configuration. Vous pouvez créer des profils
**Remote** supplémentaires, un par service distant que vous souhaitez
administrer.

Les deux modes partagent la même interface ; seules quelques actions
se comportent différemment ou sont désactivées en mode Remote. Les
différences sont signalées tout au long de ce guide.

### 1.2 Ce que SeaUI permet de faire

Avec SeaUI, vous pouvez :

- parcourir tous vos projets et explorer leurs services, entités, champs et
  routes ;
- créer de nouveaux projets, services et entités à l'aide de dialogues
  guidés ;
- renommer des projets, changer le port d'un service et ajuster les options
  d'une entité ;
- importer et exporter des fichiers de projet ;
- démarrer, arrêter, redémarrer et recharger vos services (démarrage et
  arrêt en mode Local uniquement) ;
- vous connecter à un service en cours d'exécution et consulter ses données ;
- ouvrir la documentation Swagger d'un service en cours d'exécution ;
- consulter les journaux produits par vos services (en local ou à distance) ;
- basculer entre les profils Local et Remote à tout moment ;
- changer la langue de l'interface entre l'anglais et le français.

### 1.3 La fenêtre principale

À l'ouverture de SeaUI, vous voyez une série de panneaux disposés du plus
général au plus détaillé :

- **Projects** — tous vos projets. Sélectionnez-en un pour voir ses services.
- **Services** — les services du projet sélectionné. Sélectionnez-en un pour
  voir ses entités et ses détails.
- **Entities** — les entités du service sélectionné. Sélectionnez-en une pour
  voir ses champs.
- **Fields** — les champs de l'entité sélectionnée.
- **Routes** — les routes REST disponibles. Les routes paginées sont signalées
  par un badge coloré (PAGE, OFFSET, CURSOR).

À côté de ces panneaux, des panneaux de détails affichent le port du service
sélectionné, son type de base de données, son statut d'exécution et son statut
de connexion. Une rangée de boutons permet d'agir sur le service sélectionné :
Démarrer, Arrêter, Redémarrer, Swagger, Logs, Login, Logout et Open Data.

Le statut d'exécution d'un service se rafraîchit tout seul : il reflète donc
toujours la réalité, même si vous avez démarré ou arrêté le service ailleurs.

---

## 2. Se connecter à SeaDesktop

À chaque démarrage de SeaUI, un dialogue **Connect to SeaDesktop**
s'affiche pour vous demander quel profil utiliser.

### 2.1 Se connecter en mode Local

Sélectionnez le profil **Local (built-in)** dans la liste déroulante et
cliquez sur **Connect**. La fenêtre principale s'ouvre avec vos projets
locaux chargés. Aucune authentification n'est nécessaire.

### 2.2 Se connecter en mode Remote

Sélectionnez un profil Remote dans la liste déroulante. La zone
Authentication devient active et affiche :

- **Email** — l'adresse e-mail de votre utilisateur administrateur sur
  le backend distant.
- **Password** — le mot de passe de cet utilisateur.

L'e-mail est pré-rempli avec celui que vous aviez utilisé la dernière
fois pour ce profil.

Cliquez sur **Connect**. SeaUI envoie une requête de connexion au
backend distant et, en cas de succès, ouvre la fenêtre principale avec
les projets distants chargés. En cas d'échec, une boîte d'erreur
explique ce qui s'est passé (identifiants invalides, réseau injoignable,
etc.) et vous pouvez réessayer ou annuler.

> **Rôle administrateur requis.** Pour utiliser un profil Remote, votre
> compte sur le backend distant doit avoir le rôle administrateur. Un
> compte utilisateur ordinaire peut s'authentifier mais ne peut pas
> lire les endpoints d'administration : SeaUI affichera alors "Admin
> role required" en tentant de lister les projets.

### 2.3 Annuler

Cliquer sur **Cancel** quitte SeaUI sans ouvrir la fenêtre principale.

### 2.4 Changer de connexion en cours de session

Vous pouvez changer de profil à tout moment sans redémarrer SeaUI :
ouvrez **File ▸ Switch Connection...**. Le même dialogue de connexion
apparaît. Si vous choisissez un autre profil et vous authentifiez avec
succès, SeaUI :

- remplace la connexion actuelle par la nouvelle ;
- efface les sélections en cours (projet, service, entité) ;
- recharge la liste des projets depuis la nouvelle connexion.

Si vous annulez le dialogue, la connexion actuelle est conservée.

---

## 3. Gérer les profils de connexion

Un profil enregistre les informations de connexion d'un backend Remote
pour vous éviter de retaper l'URL à chaque fois. Cliquez sur **Manage
Profiles...** dans le dialogue Connect pour ouvrir le gestionnaire de
profils.

### 3.1 La liste des profils

Le gestionnaire affiche tous les profils, avec **Local (built-in)**
toujours en haut. Sélectionner un profil affiche ses détails (type,
URL de base, dernier utilisateur) dans le panneau du bas.

### 3.2 Ajouter un profil Remote

Cliquez sur **+ Add Remote** et remplissez :

- **Name** — un libellé de votre choix, par exemple "Serveur de
  production" ou "Staging". Doit être unique parmi vos profils.
- **Base URL** — l'URL où le backend distant est joignable, par
  exemple `https://api.example.com` ou `http://192.168.1.50:8080`.
  Sans slash final.

Cliquez sur OK. Le nouveau profil est ajouté à la liste.

### 3.3 Modifier un profil Remote

Sélectionnez un profil et cliquez sur **Edit**. Le même dialogue
apparaît, pré-rempli avec les valeurs actuelles. Modifiez ce dont
vous avez besoin et cliquez sur OK. Le profil Local ne peut pas être
modifié (Edit est désactivé quand il est sélectionné).

### 3.4 Supprimer un profil Remote

Sélectionnez un profil et cliquez sur **Remove**. SeaUI demande
confirmation avant de supprimer le profil. Le profil Local ne peut
pas être supprimé (Remove est désactivé quand il est sélectionné).

### 3.5 Enregistrer

Les modifications sont sauvegardées quand vous cliquez sur **Close**.
Elles prendront effet à la prochaine ouverture du dialogue de
connexion.

> **À propos du token.** Le token d'authentification obtenu lors de la
> connexion à un profil Remote n'est **pas sauvegardé** entre les
> sessions. Vous devrez ressaisir votre mot de passe à chaque
> démarrage de SeaUI ou à chaque changement de profil. C'est une
> précaution pour protéger vos identifiants au cas où votre fichier
> de configuration serait lu par un tiers.

---

## 4. Règles de nommage

Quand vous nommez un projet, un service ou une entité, SeaUI nettoie ce que
vous saisissez afin que le nom soit toujours valide :

- seules les lettres, les chiffres et les underscores sont conservés ;
- les espaces sont transformés en underscores ;
- tout autre caractère (accents, ponctuation, symboles) est supprimé.

Par exemple, saisir `Mon Blog !` donne `Mon_Blog`. Gardez-le à l'esprit pour
ne pas être surpris par le nom final. Pour éviter les surprises, privilégiez
dès le départ des noms simples composés de lettres, de chiffres et
d'underscores.

---

## 5. Gérer les projets

Les actions de cette section fonctionnent en mode Local comme en mode
Remote. En mode Remote, les modifications sont écrites sur le système
de fichiers du backend distant (via les endpoints d'administration), pas
sur votre disque local.

### 5.1 Créer un projet

Ouvrez **File ▸ Add New Project**. Saisissez un nom de projet et un nom de
service — les deux sont obligatoires. SeaUI crée le projet avec un service
prêt à l'emploi configuré pour la production : une base de données, la
sécurité et la journalisation sont déjà mises en place pour vous.

Un projet dont le nom existe déjà ne sera pas écrasé.

> **Avant de démarrer le service :** le service est configuré pour lire sa
> clé de sécurité depuis une variable d'environnement nommée
> `SEA_DESKTOP_JWT_SECRET`. Assurez-vous que cette variable est définie dans
> votre environnement, sans quoi le service ne démarrera pas.

### 5.2 Renommer un projet

Ouvrez **Edit ▸ Edit Project**, choisissez le projet et saisissez le nouveau
nom. SeaUI demande confirmation, puis renomme le projet. Un projet ne peut pas
être renommé avec un nom déjà utilisé.

### 5.3 Importer un projet

Ouvrez **File ▸ Import Yaml** et choisissez un fichier de projet n'importe où
sur votre ordinateur. Il est copié dans vos projets et apparaît dans le
panneau Projects. Si un projet du même nom existe déjà, SeaUI demande s'il
faut le remplacer.

En mode Remote, le fichier importé est téléversé vers le backend distant.

### 5.4 Exporter un projet

Ouvrez **File ▸ Export Yaml**, choisissez le projet à exporter et indiquez où
l'enregistrer. Une copie du fichier de projet est sauvegardée à cet endroit.

En mode Remote, SeaUI télécharge le fichier depuis le backend distant
avant de l'enregistrer.

---

## 6. Gérer les services

### 6.1 Ajouter un service à un projet

Ouvrez **File ▸ Add New Service**, choisissez le projet et saisissez le nom du
service. Le nouveau service est créé avec la même configuration de production
prête à l'emploi que le service d'un nouveau projet. Un nom de service déjà
utilisé dans ce projet est refusé.

### 6.2 Changer le port d'un service

Ouvrez **Edit ▸ Edit Service**, choisissez le projet et le service, et
saisissez le nouveau port. Le changement est enregistré dans le projet.

### 6.3 Démarrer, arrêter et redémarrer les services

Les actions disponibles dépendent du mode.

**Mode Local — un service :** sélectionnez-le dans le panneau Services et
utilisez les boutons Démarrer, Arrêter ou Redémarrer. Ces boutons s'activent
et se désactivent selon que le service est en cours d'exécution.

**Mode Local — tous les services :** le menu **Services Actions** agit sur
tous les services de tous les projets à la fois :

- **Start All Services** — démarre tout service qui n'est pas déjà en
  exécution.
- **Stop All Services** — arrête tout service en exécution.
- **Restart All Services** — arrête et redémarre tous les services.
- **Reload All Services** — arrête et redémarre tous les services afin que
  toute modification de la configuration soit prise en compte.

**Mode Remote :** les boutons Démarrer et Arrêter sont désactivés, et le menu
Services Actions est désactivé. Le bouton Redémarrer reste actif et
fonctionne différemment : au lieu d'arrêter et de relancer un processus
local, il envoie une requête de redémarrage au backend distant. Le service
distant se termine proprement et est automatiquement redémarré par son
orchestrateur de conteneurs (typiquement Docker). Un dialogue "Service is
restarting, please wait..." s'affiche pendant que SeaUI attend le retour
en ligne du service.

> **Pourquoi Démarrer et Arrêter sont-ils désactivés en mode Remote ?**
> Le backend distant tourne dans un conteneur Docker géré par le système
> hôte, pas par SeaUI. Le démarrer ou l'arrêter nécessite un accès direct
> à Docker sur la machine distante, ce que SeaUI n'a pas. Utilisez SSH
> ou votre orchestrateur pour ces actions.

---

## 7. Gérer les entités

### 7.1 Ajouter une entité

Ouvrez **File ▸ Add New Entity**, puis :

1. choisissez le projet ;
2. choisissez le service auquel l'entité appartient ;
3. saisissez le nom de l'entité ;
4. choisissez les options de l'entité — Enable CRUD, Timestamps et Soft
   delete (CRUD et Timestamps sont activés par défaut) ;
5. ajoutez les champs un par un. Pour chaque champ, saisissez son nom,
   choisissez son type, et indiquez s'il est Required, Unique ou Indexed.
   Après chaque champ, SeaUI demande si vous voulez en ajouter un autre. Au
   moins un champ est exigé.

Les types de champ disponibles sont : string, int, float, bool, timestamp,
uuid, bigint, smallint, decimal, json, binary, password, email, text et file.

Une fois l'entité ajoutée, SeaUI demande s'il faut appliquer le changement à
la base de données maintenant. Répondre oui redémarre le service afin que la
nouvelle entité devienne disponible. En mode Remote, le redémarrage passe
par le backend distant.

### 7.2 Modifier les options d'une entité

Ouvrez **Edit ▸ Edit Entity**, choisissez le projet, le service et l'entité.
Un dialogue affiche les options actuelles de l'entité — Enable CRUD,
Timestamps et Soft delete — et vous permet de les modifier. SeaUI propose
ensuite d'appliquer le changement à la base de données en redémarrant le
service.

---

## 8. Modifier directement le fichier de projet

Pour tout changement que les dialogues ci-dessus ne couvrent pas, ouvrez
**Edit ▸ Edit Yaml**. Choisissez un projet et sa configuration s'ouvre dans un
éditeur de texte intégré. Faites vos modifications et cliquez sur Enregistrer,
ou sur Annuler pour les abandonner.

En mode Remote, le YAML est récupéré depuis le backend distant à
l'ouverture de l'éditeur et téléversé en retour quand vous cliquez sur
Enregistrer.

---

## 9. Se connecter et consulter les données

### 9.1 Se connecter à un service

Lorsqu'un service est en cours d'exécution, sélectionnez-le et cliquez sur
**Login**. Saisissez votre adresse e-mail et votre mot de passe. S'ils sont
corrects, le statut de connexion passe à **Connected**. Cliquez sur **Logout**
pour vous déconnecter.

> **Note pour le mode Remote.** Ce Login est distinct de l'authentification
> que vous avez faite pour connecter SeaUI au backend. Le bouton Login vous
> connecte en tant qu'utilisateur ordinaire de l'API pour consulter des
> données ; le dialogue Connect vous connecte en tant qu'administrateur
> pour gérer les projets. Les deux sessions sont indépendantes.

### 9.2 Consulter les données d'une entité

Avec un service en cours d'exécution et une entité sélectionnée, cliquez sur
**Open Data**. SeaUI récupère les enregistrements de l'entité et les affiche
dans un tableau.

### 9.3 Ouvrir la documentation Swagger

Avec un service en cours d'exécution, cliquez sur **Swagger**. La
documentation interactive de l'API du service s'ouvre dans une fenêtre à
l'intérieur de SeaUI.

---

## 10. Consulter les journaux

La façon de consulter les journaux dépend du mode.

### 10.1 Mode Local

**Un seul service :** sélectionnez-le et cliquez sur le bouton **Logs**. Son
fichier de journal s'ouvre dans l'application texte par défaut de votre
ordinateur.

**Dans SeaUI :** utilisez le menu **Audits**.

- **Show All Services Logs** ouvre une fenêtre avec un onglet par service,
  chaque onglet affichant le journal du service. Un service qui n'a jamais été
  démarré affiche un court message à la place.
- **Choose a service to show Logs** vous laisse choisir un service et affiche
  uniquement son journal.

Les journaux sont affichés tels qu'ils étaient à l'ouverture de la fenêtre ;
ils ne se rafraîchissent pas tout seuls.

### 10.2 Mode Remote

**Un seul service :** cliquez sur le bouton **Logs**. Le visualiseur de
journaux distants s'ouvre et affiche les journaux du backend connecté,
récupérés depuis son buffer mémoire. Chaque entrée est formatée comme
`[timestamp] [level] [logger] message`. Cliquez sur **Refresh** pour
récupérer les dernières entrées, ou sur **Close** pour fermer.

**Dans SeaUI :** utilisez le menu **Audits**.

- **Show All Services Logs** est désactivé en mode Remote (un profil
  Remote est connecté à un seul backend, il n'y a donc rien à
  combiner).
- **Choose a service to show Logs** ouvre directement le visualiseur de
  journaux distants pour le backend connecté.

Le visualiseur de journaux distants lit depuis le buffer circulaire en
mémoire du backend, qui conserve un nombre configurable d'entrées
récentes. Les entrées plus anciennes qui sont sorties du buffer ne sont
pas récupérables via ce visualiseur.

---

## 11. Changer la langue

SeaUI est disponible en anglais et en français. Ouvrez **Edit ▸ Preferences ▸
Languages** et choisissez **English** ou **Francais**. L'interface change
immédiatement — aucun redémarrage nécessaire. Votre choix est mémorisé pour la
prochaine ouverture de SeaUI.

---

## 12. Limitations connues en mode Remote

Le mode Remote est nouveau en v1.0 et quelques limitations sont
volontairement acceptées pour cette release :

- **Un profil = un service.** Chaque profil Remote pointe vers un seul
  backend. Si vous administrez plusieurs services tournant côte à côte
  (par exemple trois conteneurs sur le même hôte), créez un profil
  distinct pour chacun.

- **Démarrer et Arrêter sont désactivés.** Seul le Redémarrage est
  disponible à distance (via l'endpoint admin de redémarrage du
  backend). Démarrer un service arrêté ou arrêter un service en cours
  nécessite un accès SSH à la machine hôte.

- **Le visualiseur de journaux distants est en lecture seule et n'est
  pas en direct.** Pas de rafraîchissement automatique, pas de filtrage
  par niveau ou par logger, pas de recherche. Utilisez Refresh pour
  récupérer les dernières entrées. Des fonctionnalités plus avancées
  sont prévues pour la v1.1.

- **Le token d'authentification n'est pas stocké.** Vous devez vous
  authentifier à chaque ouverture de SeaUI ou à chaque changement de
  profil Remote. C'est un choix de sécurité, pas un oubli.

- **Créer un nouveau projet ne démarre pas automatiquement un nouveau
  conteneur.** Dans les déploiements Docker multi-services, déployer un
  nouveau projet nécessite de mettre à jour la configuration
  docker-compose à la main. Modifier des projets existants est en
  revanche entièrement supporté.
