# SeaUI — Guide utilisateur

SeaUI est l'application de bureau qui sert à gérer vos projets SeaDesktop.
Elle vous permet de créer et d'organiser des projets, des services et des
entités, de les exécuter et de les inspecter — le tout par une interface
graphique, sans éditer de fichiers de configuration à la main ni passer par un
terminal.

Ce guide explique comment utiliser SeaUI.

---

## 1. Premiers pas

### 1.1 Ce que SeaUI permet de faire

Avec SeaUI, vous pouvez :

- parcourir tous vos projets et explorer leurs services, entités, champs et
  routes ;
- créer de nouveaux projets, services et entités à l'aide de dialogues
  guidés ;
- renommer des projets, changer le port d'un service et ajuster les options
  d'une entité ;
- importer et exporter des fichiers de projet ;
- démarrer, arrêter, redémarrer et recharger vos services ;
- vous connecter à un service en cours d'exécution et consulter ses données ;
- ouvrir la documentation Swagger d'un service en cours d'exécution ;
- consulter les journaux produits par vos services ;
- changer la langue de l'interface entre l'anglais et le français.

### 1.2 La fenêtre principale

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

## 2. Règles de nommage

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

## 3. Gérer les projets

### 3.1 Créer un projet

Ouvrez **File ▸ Add New Project**. Saisissez un nom de projet et un nom de
service — les deux sont obligatoires. SeaUI crée le projet avec un service
prêt à l'emploi configuré pour la production : une base de données, la
sécurité et la journalisation sont déjà mises en place pour vous.

Un projet dont le nom existe déjà ne sera pas écrasé.

> **Avant de démarrer le service :** le service est configuré pour lire sa
> clé de sécurité depuis une variable d'environnement nommée
> `SEA_DESKTOP_JWT_SECRET`. Assurez-vous que cette variable est définie dans
> votre environnement, sans quoi le service ne démarrera pas.

### 3.2 Renommer un projet

Ouvrez **Edit ▸ Edit Project**, choisissez le projet et saisissez le nouveau
nom. SeaUI demande confirmation, puis renomme le projet. Un projet ne peut pas
être renommé avec un nom déjà utilisé.

### 3.3 Importer un projet

Ouvrez **File ▸ Import Yaml** et choisissez un fichier de projet n'importe où
sur votre ordinateur. Il est copié dans vos projets et apparaît dans le
panneau Projects. Si un projet du même nom existe déjà, SeaUI demande s'il
faut le remplacer.

### 3.4 Exporter un projet

Ouvrez **File ▸ Export Yaml**, choisissez le projet à exporter et indiquez où
l'enregistrer. Une copie du fichier de projet est sauvegardée à cet endroit.

---

## 4. Gérer les services

### 4.1 Ajouter un service à un projet

Ouvrez **File ▸ Add New Service**, choisissez le projet et saisissez le nom du
service. Le nouveau service est créé avec la même configuration de production
prête à l'emploi que le service d'un nouveau projet. Un nom de service déjà
utilisé dans ce projet est refusé.

### 4.2 Changer le port d'un service

Ouvrez **Edit ▸ Edit Service**, choisissez le projet et le service, et
saisissez le nouveau port. Le changement est enregistré dans le projet.

### 4.3 Démarrer, arrêter et redémarrer les services

Vous pouvez agir sur un seul service ou sur tous à la fois.

**Un service :** sélectionnez-le dans le panneau Services et utilisez les
boutons Démarrer, Arrêter ou Redémarrer. Ces boutons s'activent et se
désactivent selon que le service est en cours d'exécution.

**Tous les services :** le menu **Services Actions** agit sur tous les
services de tous les projets à la fois :

- **Start All Services** — démarre tout service qui n'est pas déjà en
  exécution.
- **Stop All Services** — arrête tout service en exécution.
- **Restart All Services** — arrête et redémarre tous les services.
- **Reload All Services** — arrête et redémarre tous les services afin que
  toute modification de la configuration soit prise en compte.

---

## 5. Gérer les entités

### 5.1 Ajouter une entité

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
nouvelle entité devienne disponible.

### 5.2 Modifier les options d'une entité

Ouvrez **Edit ▸ Edit Entity**, choisissez le projet, le service et l'entité.
Un dialogue affiche les options actuelles de l'entité — Enable CRUD,
Timestamps et Soft delete — et vous permet de les modifier. SeaUI propose
ensuite d'appliquer le changement à la base de données en redémarrant le
service.

---

## 6. Modifier directement le fichier de projet

Pour tout changement que les dialogues ci-dessus ne couvrent pas, ouvrez
**Edit ▸ Edit Yaml**. Choisissez un projet et sa configuration s'ouvre dans un
éditeur de texte intégré. Faites vos modifications et cliquez sur Enregistrer,
ou sur Annuler pour les abandonner.

---

## 7. Se connecter et consulter les données

### 7.1 Se connecter à un service

Lorsqu'un service est en cours d'exécution, sélectionnez-le et cliquez sur
**Login**. Saisissez votre adresse e-mail et votre mot de passe. S'ils sont
corrects, le statut de connexion passe à **Connected**. Cliquez sur **Logout**
pour vous déconnecter.

### 7.2 Consulter les données d'une entité

Avec un service en cours d'exécution et une entité sélectionnée, cliquez sur
**Open Data**. SeaUI récupère les enregistrements de l'entité et les affiche
dans un tableau.

### 7.3 Ouvrir la documentation Swagger

Avec un service en cours d'exécution, cliquez sur **Swagger**. La
documentation interactive de l'API du service s'ouvre dans une fenêtre à
l'intérieur de SeaUI.

---

## 8. Consulter les journaux

Il y a deux façons de consulter les journaux de vos services.

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

---

## 9. Changer la langue

SeaUI est disponible en anglais et en français. Ouvrez **Edit ▸ Preferences ▸
Languages** et choisissez **English** ou **Francais**. L'interface change
immédiatement — aucun redémarrage nécessaire. Votre choix est mémorisé pour la
prochaine ouverture de SeaUI.
