#include "mainwindow.h"
#include "connectiondialog.h"
#include "entitydatadialog.h"
#include "httpprojectrepository.h"
#include "remotelogsviewer.h"
#include "routelistitemdelegate.h"
#include "ui_mainwindow.h"


#include <QDebug>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFileSystemWatcher>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWebEngineView>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMessageBox>
#include <QTableWidget>
#include <QLineEdit>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QRegularExpression>
#include <QTabWidget>
#include <QPlainTextEdit>
#include <QListWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QFontDatabase>
#include <QInputDialog>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QSet>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QEvent>
#include <QTextStream>
#include <QStringConverter>
#include "localprojectrepository.h"
#include <QToolBar>
#include <QAction>
#include <QLabel>
#include <QSettings>
#include "envfileloader.h"
namespace {

/**
 * @brief Retourne le dossier de configuration de l'application.
 *
 * En debug, on utilise le dossier `configs` du projet pour travailler
 * directement sur les YAML du dépôt.
 *
 * En release, on utilise un dossier inscriptible standard propre à l'application.
 *
 * @return QString Chemin absolu du dossier de configuration.
 */
[[nodiscard]] QString appConfigsDir()
{
    // Priorite identique a resolveConfigsDir() de main.cpp :
    //   1. SEA_DESKTOP_CONFIGS_DIR
    //   2. QSettings [local]/configsDir (choisi par LocalSetupDialog
    //      au premier lancement)
    //   3. AppDataLocation/configs (fallback)
    const QString envDir = qEnvironmentVariable("SEA_DESKTOP_CONFIGS_DIR");
    if (!envDir.isEmpty()) {
        return envDir;
    }

    QSettings settings;
    const QString persistedDir = settings.value("local/configsDir").toString();
    if (!persistedDir.isEmpty()) {
        return persistedDir;
    }

    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + "/configs";
}

/**
 * @brief Retourne le dossier de logs de l'application.
 *
 * @return QString Chemin absolu du dossier de logs.
 */
[[nodiscard]] QString appLogsDir()
{
    // Possibilite de redirection via variable d'environnement
    // (utile pour les tests / packaging custom).
    const QString envDir = qEnvironmentVariable("SEA_DESKTOP_LOGS_DIR");
    if (!envDir.isEmpty()) {
        return envDir;
    }

    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + "/logs";
}

} // namespace
namespace {

/**
 * @brief Retourne le chemin pluriel attendu pour une entité.
 *
 * Exemple : `Department` -> `/departments`.
 *
 * @param entityName Nom d'entité.
 * @return QString Chemin pluriel.
 */
QString pluralEntityPath(const QString& entityName, bool must_be_plural = true)
{
    if (entityName.isEmpty()) {
        return "";
    }

    QString name = entityName.toLower();

    if (!must_be_plural) {
        return "/" + name;
    }

    // Déjà pluriel évident : categories, companies, policies
    if (name.size() >= 3 && name.endsWith("ies", Qt::CaseInsensitive)) {
        return "/" + name;
    }

    // Déjà pluriel simple : users, articles, tags
    // On évite address, class, status, etc.
    if (name.endsWith('s', Qt::CaseInsensitive) &&
        !name.endsWith("ss", Qt::CaseInsensitive) &&
        !name.endsWith("us", Qt::CaseInsensitive)) {
        return "/" + name;
    }

    // category -> categories
    // company  -> companies
    if (name.size() >= 2 && name.endsWith('y', Qt::CaseInsensitive)) {
        QChar beforeY = name.at(name.size() - 2).toLower();

        const bool beforeIsVowel =
            beforeY == 'a' ||
            beforeY == 'e' ||
            beforeY == 'i' ||
            beforeY == 'o' ||
            beforeY == 'u';

        if (!beforeIsVowel) {
            name.chop(1);
            name += "ies";
            return "/" + name;
        }
    }

    // address -> addresses
    // class   -> classes
    // box     -> boxes
    // church  -> churches
    // dish    -> dishes
    if (name.endsWith('s', Qt::CaseInsensitive) ||
        name.endsWith("ss", Qt::CaseInsensitive) ||
        name.endsWith('x', Qt::CaseInsensitive) ||
        name.endsWith('z', Qt::CaseInsensitive) ||
        name.endsWith("ch", Qt::CaseInsensitive) ||
        name.endsWith("sh", Qt::CaseInsensitive)) {
        return "/" + name + "es";
    }

    return "/" + name + "s";
}
/**
 * @brief Détermine si une route est liée à une entité donnée.
 *
 * Le filtrage ne se contente pas de comparer `entity_name`, car certaines
 * routes de relation impliquent une autre entité dans leur chemin.
 *
 * @param route Route logique à tester.
 * @param entityName Nom de l'entité sélectionnée.
 * @return true si la route semble liée à l'entité, false sinon.
 */
bool routeMatchesEntity(const sea::application::RouteDefinition& route, const QString& entityName)
{
    const QString routeEntity = QString::fromStdString(route.entity_name);
    const QString routePath = QString::fromStdString(route.path);

    const QString entityLower = pluralEntityPath(entityName, false);
    const QString entityPluralPath = pluralEntityPath(entityName);

    if (routeEntity.compare(entityName, Qt::CaseInsensitive) == 0) {
        return true;
    }

    if (routePath.contains(entityPluralPath, Qt::CaseInsensitive)) {
        return true;
    }

    if (routePath.contains("/with_" + entityLower, Qt::CaseInsensitive)) {
        return true;
    }

    if (routePath.contains("_with_" + entityLower, Qt::CaseInsensitive)) {
        return true;
    }

    return false;
}
/**
 * Charger le .env avant les projets
*/
void loadEnvIntoSeaUIProcess()
{
    const QString envFilePath = EnvFileLoader::envFilePathFor(appConfigsDir());

    const QMap<QString, QString> envVars = EnvFileLoader::load(envFilePath);

    for (auto it = envVars.constBegin(); it != envVars.constEnd(); ++it) {
        qputenv(it.key().toUtf8(), it.value().toUtf8());
    }
}
} // namespace

/**
 * @brief Construit la fenêtre principale et initialise tous les modèles et signaux.
 *
 * @param parent Parent Qt.
 */
MainWindow::MainWindow(TranslationManager* translationManager, std::unique_ptr<IProjectRepository> repository,
                       Profile activeProfile, QString token, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , _projectModel(new ProjectListModel(this))
    , _serviceModel(new ServiceListModel(this))
    , _entityModel(new EntityListModel(this))
    , _fieldModel(new FieldListModel(this))
    , _routeModel(new RouteListModel(this))
    ,_networkManager(new QNetworkAccessManager(this))
    , _translationManager(translationManager)
{
    ui->setupUi(this);
    // ── Icône de l'application ────────────────────────────────────
    // setWindowIcon est utilisé par Windows, KDE, XFCE et la plupart
    // des environnements desktop pour afficher l'icône dans la barre
    // de titre et dans Alt+Tab. Sur GNOME/Wayland, la barre de titre
    // masque ce icon par design : la QToolBar ci-dessous garantit que
    // le logo est tout de meme visible dans la fenetre.
    const QIcon appIcon = QIcon::fromTheme(
        QStringLiteral("seaui"),
        QIcon(QStringLiteral(":/icons/seaui.png"))
        );

    setWindowIcon(appIcon);

    // ── Barre logo en haut a gauche (visible sur toutes les plateformes) ─
    auto* logoToolBar = new QToolBar(tr("Logo"), this);
    logoToolBar->setObjectName(QStringLiteral("LogoToolBar"));
    logoToolBar->setMovable(false);
    logoToolBar->setFloatable(false);
    logoToolBar->setIconSize(QSize(32, 32));

    auto* logoLabel = new QLabel(this);
    logoLabel->setPixmap(appIcon.pixmap(32, 32));
    logoLabel->setContentsMargins(8, 4, 12, 4);  // padding visuel
    logoToolBar->addWidget(logoLabel);

    auto* titleLabel = new QLabel(QStringLiteral("SeaDesktop"), this);
    titleLabel->setStyleSheet(QStringLiteral(
        "font-size: 14px; font-weight: bold; padding-right: 12px;"));
    logoToolBar->addWidget(titleLabel);

    addToolBar(Qt::TopToolBarArea, logoToolBar);

    setWindowTitle(tr("SeaDesktop"));
    _projectRepository = std::move(repository);
    _activeProfile = std::move(activeProfile);
    _token = std::move(token);
    _isRemoteMode = (_activeProfile.type == Profile::Type::Remote);
    updateServicesActionsMenuState();
    updateAuditsMenuState();
    _projectRepository = std::make_unique<LocalProjectRepository>(appConfigsDir());
    ui->projectListView->setModel(_projectModel);
    ui->serviceListView->setModel(_serviceModel);
    ui->entityListView->setModel(_entityModel);
    ui->fieldListView->setModel(_fieldModel);
    ui->routeListView->setModel(_routeModel);
    // Delegate de rendu personnalise : affiche les routes avec
    // methode HTTP coloree et badge colore pour les routes paginees
    // (PAGE / OFFSET / CURSOR).
    //
    // Le delegate est ownership-transferred a la view (parent Qt).
    ui->routeListView->setItemDelegate(new RouteListItemDelegate(ui->routeListView));
    // Lignes alternees pour meilleure lisibilite
    ui->routeListView->setAlternatingRowColors(true);

    // Largeur uniforme des items (si la view supporte le wrap, eviter)
    ui->routeListView->setUniformItemSizes(true);

    // Hauteur calculee par le delegate (pas necessaire si sizeHint OK)
    // ui->routeListView->setSpacing(2);

    // Active le mouse tracking pour le rendu hover du delegate
    ui->routeListView->setMouseTracking(true);

    ui->startServiceButton->setEnabled(false);
    ui->stopServiceButton->setEnabled(false);
    ui->restartServiceButton->setEnabled(false);
    ui->swaggerServiceButton->setEnabled(false);

    ui->serviceLogoutButton->setEnabled(false);
    ui->serviceAuthStatusLabel->setText(tr("Disconnected"));
    ui->serviceAuthStatusLabel->setStyleSheet("color: #e74c3c; font-weight: bold;");

    updateAuthUi();

    const QString configsDir = appConfigsDir();
    QDir().mkpath(configsDir);

    // Important : charger le .env AVANT le parsing des YAML.
    loadEnvIntoSeaUIProcess();

    loadProjects();


    _watcher = new QFileSystemWatcher(this);
    _watcher->addPath(configsDir);

    connect(_watcher, &QFileSystemWatcher::fileChanged,
            this, [this](const QString& path) {
                qDebug() << "Fichier modifié:" << path;
                loadProjects();
            });

    connect(_watcher, &QFileSystemWatcher::directoryChanged,
            this, [this](const QString& path) {
                qDebug() << "Dossier modifié:" << path;
                loadProjects();
            });

    _statusCheck = new ServiceStatusCheck(this);

    connect(_statusCheck, &ServiceStatusCheck::statusUpdated,
            this, &MainWindow::onStatusUpdated);

    connect(_statusCheck, &ServiceStatusCheck::serviceUnreachable,
            this, &MainWindow::onServiceUnreachable);

    connect(_statusCheck, &ServiceStatusCheck::statusUpdated, this,
            [this](const QString&, const QString& status, int) {
                const bool running = (status == "RUNNING");
                // En mode Remote, Start et Stop sont desactives : le
                // backend tourne sur un Docker distant, SeaUI ne peut
                // pas lancer/arreter le process. Seul Restart est
                // adapte pour le remote (via POST /admin/restart).
                ui->startServiceButton->setEnabled(!running && !_isRemoteMode);
                ui->stopServiceButton->setEnabled(running && !_isRemoteMode);
                ui->restartServiceButton->setEnabled(running);
                ui->swaggerServiceButton->setEnabled(running);
                ui->serviceStatusLabel->setStyleSheet(
                    running
                        ? "color: #2ecc71; font-weight: bold;"
                        : "color: #e74c3c; font-weight: bold;"
                    );
            });

    connect(_statusCheck, &ServiceStatusCheck::serviceUnreachable, this,
            [this](const QString&) {
                // Start desactive en mode Remote (idem patch precedent).
                ui->startServiceButton->setEnabled(!_isRemoteMode);
                ui->stopServiceButton->setEnabled(false);
                ui->restartServiceButton->setEnabled(false);
                ui->swaggerServiceButton->setEnabled(false);
            });

    connect(ui->startServiceButton, &QPushButton::clicked, this, [this]() {
        ui->startServiceButton->setEnabled(false);

        if (_currentServiceRow < 0 || _currentProjectRow < 0) {
            ui->startServiceButton->setEnabled(true);
            return;
        }

        const auto& project = _projectModel->projectAt(_currentProjectRow);
        const auto& service = _serviceModel->serviceAt(_currentServiceRow);

        const QString yamlPath = yamlPathForProject(QString::fromStdString(project->name));
        startService(QString::fromStdString(service->name), yamlPath);
    });

    connect(ui->stopServiceButton, &QPushButton::clicked, this, [this]() {
        if (_currentServiceRow < 0) {
            return;
        }

        const auto& service = _serviceModel->serviceAt(_currentServiceRow);
        stopService(QString::fromStdString(service->name));
    });

    connect(ui->restartServiceButton, &QPushButton::clicked, this, [this]() {
        if (_currentServiceRow < 0 || _currentProjectRow < 0) {
            return;
        }

        const auto& project = _projectModel->projectAt(_currentProjectRow);
        const auto& service = _serviceModel->serviceAt(_currentServiceRow);
        const QString yamlPath = yamlPathForProject(QString::fromStdString(project->name));

        restartService(QString::fromStdString(service->name), yamlPath);
    });

    connect(ui->logsServiceButton, &QPushButton::clicked, this, [this]() {
        if (_currentProjectRow < 0 || _currentServiceRow < 0) {
            QMessageBox::warning(this, tr("Logs"), tr("No project or service selected."));
            return;
        }

        // Mode Remote : ouvre le RemoteLogsViewer qui appelle
        // GET /admin/logs sur le backend distant.
        if (_isRemoteMode) {
            auto* viewer = new RemoteLogsViewer(_activeProfile.baseUrl,
                                                _token,
                                                this);
            viewer->setAttribute(Qt::WA_DeleteOnClose);
            viewer->show();
            return;
        }

        // Mode Local : ouvre le fichier log local dans l'editeur
        // par defaut (comportement historique).
        const auto& project = _projectModel->projectAt(_currentProjectRow);
        const auto& service = _serviceModel->serviceAt(_currentServiceRow);

        const QString projectName = QString::fromStdString(project->name);
        const QString serviceName = QString::fromStdString(service->name);

        const QString processKey =
            serviceProcessKey(projectName, serviceName, static_cast<int>(service->port));

        const QString logPath = appLogsDir() + "/" + processKey + ".log";

        if (!QFileInfo::exists(logPath)) {
            QMessageBox::warning(
                this,
                tr("Logs"),
                tr("The log file does not exist yet:\n%1").arg(logPath)
                );
            return;
        }

        QDesktopServices::openUrl(QUrl::fromLocalFile(logPath));
    });

    setupLanguageMenu();
}

/**
 * @brief Détruit la fenêtre principale.
 */
MainWindow::~MainWindow()
{
    delete ui;
}

/**
 * @brief Initialise le sous-menu de selection de langue.
 *
 * Les actions actionEnglish et actionFrancais (definies dans le .ui sous
 * Edit -> Preferences -> Languages) sont rendues cochables et placees dans
 * un QActionGroup pour etre mutuellement exclusives. La langue actuellement
 * active est cochee.
 */
void MainWindow::setupLanguageMenu()
{
    _languageGroup = new QActionGroup(this);
    _languageGroup->setExclusive(true);

    ui->actionEnglish->setCheckable(true);
    ui->actionFrancais->setCheckable(true);

    _languageGroup->addAction(ui->actionEnglish);
    _languageGroup->addAction(ui->actionFrancais);

    if (_translationManager) {
        syncLanguageMenu(_translationManager->currentLanguage());

        // Garder la coche synchronisee si la langue change par une autre
        // voie que le menu.
        connect(_translationManager, &TranslationManager::languageChanged,
                this, &MainWindow::syncLanguageMenu);
    }
}

/**
 * @brief Met a jour la coche du sous-menu langue selon la langue active.
 *
 * @param code Code de la langue desormais active ("en_US", "fr_FR").
 */
void MainWindow::syncLanguageMenu(const QString& code)
{
    if (code == QStringLiteral("fr_FR")) {
        ui->actionFrancais->setChecked(true);
    } else {
        ui->actionEnglish->setChecked(true);
    }
}

/**
 * @brief Intercepte les changements d'etat de la fenetre.
 *
 * Sur QEvent::LanguageChange, l'interface est retraduite a chaud :
 *  - retranslateUi() recharge tous les textes definis dans le .ui ;
 *  - les textes positionnes dynamiquement dans le code (titre de la
 *    fenetre, label de statut d'authentification) sont re-appliques
 *    manuellement car retranslateUi() ne les connait pas.
 *
 * @param event Evenement Qt.
 */
void MainWindow::changeEvent(QEvent* event)
{
    if (event != nullptr && event->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);

        // Textes dynamiques non couverts par retranslateUi().
        setWindowTitle(tr("SeaDesktop"));
        updateAuthUi();
    }

    QMainWindow::changeEvent(event);
}

/**
 * @brief Bascule l'application en anglais.
 */
void MainWindow::on_actionEnglish_triggered()
{
    if (_translationManager) {
        _translationManager->applyLanguage(QStringLiteral("en_US"));
    }
}

/**
 * @brief Bascule l'application en francais.
 */
void MainWindow::on_actionFrancais_triggered()
{
    if (_translationManager) {
        _translationManager->applyLanguage(QStringLiteral("fr_FR"));
    }
}

/**
 * @brief Recharge tous les projets YAML depuis le dossier de configuration.
 *
 * La sélection courante est restaurée si possible.
 */
void MainWindow::loadProjects()
{
    _projectRepository->listProjects()
    .then(this, [this](const IProjectRepository::ListResult& result) {

        // Erreurs de parsing YAML : un message par fichier invalide.
        // Ces fichiers n'apparaissent pas dans la liste des projets ; sans
        // cette alerte, l'utilisateur ne saurait pas pourquoi son fichier
        // n'est pas affiche.
        if (!result.errors.isEmpty()) {
            for (const QString& err : std::as_const(result.errors)) {
                qWarning().noquote() << "YAML parse error:" << err;
            }

            QMessageBox box(this);
            box.setIcon(QMessageBox::Warning);
            box.setWindowTitle(tr("Invalid YAML file"));
            box.setText(tr("%n configuration file(s) could not be loaded and "
                           "will not appear in the project list.",
                           "", static_cast<int>(result.errors.size())));
            box.setInformativeText(
                tr("Fix the reported errors; the file will then load "
                   "automatically."));
            box.setDetailedText(result.errors.join('\n'));
            box.setStandardButtons(QMessageBox::Ok);
            box.exec();
        }

        _projectModel->setProjects(result.projects);

        // Restauration sélection (inchangé)
        if (_currentProjectRow >= 0 &&
            _currentProjectRow < _projectModel->rowCount({})) {

            const auto projectIndex = _projectModel->index(_currentProjectRow);
            ui->projectListView->setCurrentIndex(projectIndex);
            on_projectListView_clicked(projectIndex);

            if (_currentServiceRow >= 0 &&
                _currentServiceRow < _serviceModel->rowCount({})) {

                const auto serviceIndex = _serviceModel->index(_currentServiceRow);
                ui->serviceListView->setCurrentIndex(serviceIndex);
                on_serviceListView_clicked(serviceIndex);

                if (_currentEntityRow >= 0 &&
                    _currentEntityRow < _entityModel->rowCount({})) {

                    const auto entityIndex = _entityModel->index(_currentEntityRow);
                    ui->entityListView->setCurrentIndex(entityIndex);
                    on_entityListView_clicked(entityIndex);
                }
            }
        }
    })
        .onFailed(this, [](const std::exception& e) {
            qWarning() << "Erreur chargement projets:" << e.what();
        });
}
/**
 * @brief Demarre le processus backend d'un service donne.
 *
 * Helper independant de la selection courante de l'interface.
 */
void MainWindow::startServiceProcess(const QString& projectName,
                                     const QString& serviceName,
                                     int port,
                                     const QString& yamlPath)
{
    const QString processKey = serviceProcessKey(projectName, serviceName, port);

    // Idempotent : ne rien faire si le processus tourne deja.
    if (_processes.contains(processKey) &&
        _processes[processKey] != nullptr &&
        _processes[processKey]->state() != QProcess::NotRunning) {
        return;
    }

    const QString logsDir = appLogsDir();
    QDir().mkpath(logsDir);
    const QString logPath = logsDir + "/" + processKey + ".log";

    // ── Resolution du binaire backend ───────────────────────────
    // Trois sources possibles, dans cet ordre de priorite :
    //   1. Variable d'env SEA_DESKTOP_BACKEND_BIN (override pour tests)
    //   2. /usr/bin/seadesktop-backend (wrapper du paquet .deb qui
    //      definit LD_LIBRARY_PATH et MARIADB_PLUGIN_DIR pour pointer
    //      vers les libs bundlees dans /opt/seadesktop/lib/)
    //   3. ../Backend_Seastar/backend_seastar (mode dev, relatif au
    //      binaire SeaUI dans son build directory)
    QString backendPath;
    const QString envBackend = qEnvironmentVariable("SEA_DESKTOP_BACKEND_BIN");
    if (!envBackend.isEmpty() && QFile::exists(envBackend)) {
        backendPath = envBackend;
    } else if (QFile::exists(QStringLiteral("/usr/bin/seadesktop-backend"))) {
        backendPath = QStringLiteral("/usr/bin/seadesktop-backend");
    } else {
        // Mode dev : resolution relative au binaire SeaUI courant pour
        // que le chemin marche peu importe d'ou SeaUI est lance.
        const QString devCandidate = QDir(QCoreApplication::applicationDirPath())
                                         .absoluteFilePath("../Backend_Seastar/backend_seastar");
        backendPath = devCandidate;
    }

    qDebug().noquote() << "[" + processKey + "] Launching backend:" << backendPath;

    auto* process = new QProcess(this);

    // ── Environnement explicite pour le QProcess ────────────────
    // Strategie en deux temps :
    //   1. On part de l'environnement de SeaUI (utile si l'utilisateur
    //      a deja exporte MYSQL_PASSWORD, etc. dans son shell)
    //   2. On surcharge avec le contenu de <parent>/environment/seadesktop.env
    //      cree par LocalSetupDialog. Cela permet que SeaUI lance depuis
    //      le menu Applications GNOME (sans heritage de ~/.profile)
    //      fournisse quand meme les bons MYSQL_USER, MYSQL_PASSWORD,
    //      SEA_DESKTOP_JWT_SECRET, etc. au backend.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    const QString envFilePath =
        EnvFileLoader::envFilePathFor(appConfigsDir());
    const QMap<QString, QString> envVars =
        EnvFileLoader::load(envFilePath);

    if (envVars.isEmpty()) {
        qDebug().noquote() << "[" + processKey + "] No .env loaded from"
                           << envFilePath
                           << "(file missing or empty)";
    } else {
        qDebug().noquote() << "[" + processKey + "] Loaded" << envVars.size()
        << "env vars from" << envFilePath;
        for (auto it = envVars.constBegin(); it != envVars.constEnd(); ++it) {
            env.insert(it.key(), it.value());
        }
    }

    process->setProcessEnvironment(env);

    // NOTE : la sortie standard et la sortie d'erreur sont redirigees vers
    // un fichier (setStandardOutputFile/setStandardErrorFile ci-dessous).
    // Une fois ces redirections en place, les signaux readyReadStandard*
    // ne se declenchent plus : Qt ecrit directement dans le fichier sans
    // passer par les pipes internes. On lit donc le motif d'erreur depuis
    // le fichier log dans reportBackendStartupFailure().
    process->setStandardOutputFile(logPath, QIODevice::Append);
    process->setStandardErrorFile(logPath, QIODevice::Append);

    // Horodatage du demarrage : sert a distinguer un echec de boot
    // (mort < 5 s, typiquement MySQL qui refuse la connexion) d'un arret
    // ou crash tardif (qui releve du polling /health).
    auto* startClock = new QElapsedTimer;
    startClock->start();

    // ── Echec de lancement du binaire (introuvable, permissions, etc.) ──
    connect(process, &QProcess::errorOccurred, this,
            [this, serviceName, logPath](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart) {
                    reportBackendStartupFailure(serviceName, logPath);
                }
            });

    // ── Terminaison du process ──────────────────────────────────────
    connect(process, &QProcess::finished, this,
            [this, processKey, serviceName, logPath, startClock]
            (int exitCode, QProcess::ExitStatus exitStatus) {
                const qint64 uptimeMs = startClock->elapsed();
                delete startClock;

                // Arret volontaire (stop/restart) → jamais d'alerte.
                if (_intentionalStops.remove(processKey)) {
                    return;
                }

                const bool crashed = (exitStatus == QProcess::CrashExit);
                const bool badExit = (exitCode != 0);

                // Echec de boot : mort rapide (< 5 s) avec code/etat d'erreur.
                // C'est le cas d'une connexion MySQL refusee au demarrage
                // (identifiants invalides, base inexistante, variable
                // d'environnement non resolue, serveur injoignable).
                if ((crashed || badExit) && uptimeMs < 5000) {
                    reportBackendStartupFailure(serviceName, logPath);
                }
                // Au-dela de 5 s, un echec releve d'un probleme runtime :
                // le polling /health (ServiceStatusCheck) prend le relais
                // et bascule l'etat a STOPPED via onServiceUnreachable().
            });

    QStringList args;
    args << "--config" << yamlPath
         << "--service_name" << serviceName;

    process->start(backendPath, args);
    _processes[processKey] = process;
}
/**
 * @brief Arrete le processus backend d'un service donne.
 *
 * Helper independant de la selection courante de l'interface.
 */
void MainWindow::stopServiceProcess(const QString& projectName,
                                    const QString& serviceName,
                                    int port)
{
    const QString processKey = serviceProcessKey(projectName, serviceName, port);

    if (!_processes.contains(processKey)) {
        return;
    }

    auto* process = _processes[processKey];
    if (process == nullptr) {
        _processes.remove(processKey);
        return;
    }

    if (process->state() != QProcess::NotRunning) {
        // Marque cet arret comme volontaire : le handler finished() ne
        // doit pas l'interpreter comme un echec de demarrage.
        _intentionalStops.insert(processKey);
        process->terminate();
        if (!process->waitForFinished(3000)) {
            process->kill();
            process->waitForFinished();
        }
    }

    _processes.remove(processKey);
    process->deleteLater();
}

/**
 * @brief Lit les dernieres lignes d'un fichier log backend.
 *
 * Fenetre glissante : ne conserve que les `maxLines` dernieres lignes,
 * suffisantes pour exposer le motif d'erreur d'un echec de demarrage
 * sans charger un fichier potentiellement volumineux.
 */
QString MainWindow::readLogTail(const QString& logPath, int maxLines) const
{
    QFile file(logPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }

    QStringList lines;
    QTextStream in(&file);
    while (!in.atEnd()) {
        lines.append(in.readLine());
        if (lines.size() > maxLines) {
            lines.removeFirst();
        }
    }
    return lines.join('\n');
}

/**
 * @brief Diagnostique un echec de demarrage backend et alerte l'utilisateur.
 *
 * Lit la fin du log, detecte les motifs d'erreur connus (connexion MySQL
 * refusee, base inexistante, variable d'environnement non resolue, serveur
 * injoignable) et affiche une QMessageBox::critical avec un message clair,
 * plus le contenu brut du log en detail repliable. Si aucun motif connu
 * n'est reconnu, le message reste generique mais le log brut demeure
 * accessible, garantissant que l'erreur reelle est toujours consultable.
 */
void MainWindow::reportBackendStartupFailure(const QString& serviceName,
                                             const QString& logPath)
{
    const QString tail = readLogTail(logPath);

    QString reason = "Nothing captured";
    if (tail.contains("Access denied for user", Qt::CaseInsensitive)) {
        reason = tr("MySQL connection refused: invalid username or password. "
                    "Check MYSQL_USER and MYSQL_PASSWORD in your environment "
                    "configuration.");
    } else if (tail.contains("Unknown database", Qt::CaseInsensitive)) {
        reason = tr("The configured MySQL database does not exist. "
                    "Check the 'database_name' value in your YAML file.");
    } else if (tail.contains("Can't connect", Qt::CaseInsensitive) ||
               tail.contains("Connection refused", Qt::CaseInsensitive) ||
               tail.contains("connect to server", Qt::CaseInsensitive)) {
        reason = tr("Cannot reach the MySQL server. Check that the database "
                    "is running and that 'host' and 'port' are correct.");
    } else if (tail.contains(QStringLiteral("${"))) {
        reason = tr("An environment variable was not resolved "
                    "(for example ${MYSQL_PASSWORD}). Check your environment "
                    "configuration file.");
    } else {
        reason = tr("The service '%1' failed to start. See the details below "
                    "for the backend error.").arg(serviceName);
    }

    qWarning().noquote()
        << "[backend startup failure]" << serviceName << ":" << reason;

    QMessageBox box(this);
    box.setIcon(QMessageBox::Critical);
    box.setWindowTitle(tr("Service startup failed"));
    box.setText(tr("Service '%1' could not start.").arg(serviceName));
    box.setInformativeText(reason);
    if (!tail.isEmpty()) {
        box.setDetailedText(tail);
    }
    box.setStandardButtons(QMessageBox::Ok);
    box.exec();
}

/**
 * @brief Démarre le processus backend correspondant au service sélectionné.
 *
 * Delegue au helper startServiceProcess en resolvant le projet et le
 * service depuis la selection courante de l'interface.
 *
 * @param serviceName Nom du service.
 * @param yamlPath Chemin du YAML du projet.
 */
void MainWindow::startService(const QString &serviceName, const QString &yamlPath)
{
    if (_currentProjectRow < 0 || _currentServiceRow < 0) {
        return;
    }

    const auto& project = _projectModel->projectAt(_currentProjectRow);
    const auto& service = _serviceModel->serviceAt(_currentServiceRow);

    startServiceProcess(
        QString::fromStdString(project->name),
        serviceName,
        static_cast<int>(service->port),
        yamlPath
        );
}

/**
 * @brief Arrête le processus backend correspondant au service sélectionné.
 *
 * Delegue au helper stopServiceProcess en resolvant le projet et le
 * service depuis la selection courante de l'interface.
 *
 * @param serviceName Nom du service.
 */
void MainWindow::stopService(const QString &serviceName)
{
    if (_currentProjectRow < 0 || _currentServiceRow < 0) {
        return;
    }

    const auto& project = _projectModel->projectAt(_currentProjectRow);
    const auto& service = _serviceModel->serviceAt(_currentServiceRow);

    stopServiceProcess(
        QString::fromStdString(project->name),
        serviceName,
        static_cast<int>(service->port)
        );
}

/**
 * @brief Redémarre le service demandé.
 *
 * @param serviceName Nom du service.
 * @param yamlPath Chemin du YAML du projet.
 */
void MainWindow::restartService(const QString& serviceName, const QString& yamlPath)
{
    // Mode Remote : on ne peut pas arreter le QProcess local (il n'existe
    // pas), on appelle l'endpoint POST /admin/restart du backend distant.
    // Le backend va se terminer apres 500ms, Docker va le relancer, et
    // le ServiceStatusCheck mettra a jour le statut automatiquement
    // (RUNNING -> [erreur transient] -> RUNNING).
    if (_isRemoteMode) {
        // Dialog modal non-bloquant pour l'utilisateur. On le ferme
        // quand le statut redevient RUNNING via ServiceStatusCheck.
        auto* progressDialog = new QMessageBox(this);
        progressDialog->setWindowTitle(tr("Restarting"));
        progressDialog->setText(
            tr("Service is restarting, please wait..."));
        progressDialog->setStandardButtons(QMessageBox::NoButton);
        progressDialog->setIcon(QMessageBox::Information);
        progressDialog->setWindowModality(Qt::WindowModal);

        auto* nam = new QNetworkAccessManager(this);

        // URL de restart sur le backend du profil actif.
        QString baseUrl = _activeProfile.baseUrl;
        while (baseUrl.endsWith('/')) {
            baseUrl.chop(1);
        }
        QNetworkRequest request(QUrl(baseUrl + "/admin/restart"));
        request.setRawHeader("Authorization",
                             QByteArray("Bearer ") + _token.toUtf8());
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          QStringLiteral("application/json"));

        QNetworkReply* reply = nam->post(request, QByteArray());

        connect(reply, &QNetworkReply::finished, this,
                [this, reply, nam, progressDialog, serviceName]() {
                    const int status = reply->attribute(
                                                QNetworkRequest::HttpStatusCodeAttribute).toInt();

                    if (status == 202) {
                        qDebug() << "Remote restart requested for" << serviceName;

                        // Le backend va se terminer dans 500ms. On attend que
                        // ServiceStatusCheck constate le retour a RUNNING pour
                        // fermer le dialog. Polling toutes les 500ms via QTimer.
                        auto* timer = new QTimer(this);
                        timer->setInterval(500);
                        connect(timer, &QTimer::timeout, this,
                                [this, timer, progressDialog]() {
                                    if (ui->serviceStatusLabel->text() == "RUNNING") {
                                        progressDialog->done(QMessageBox::Ok);
                                        progressDialog->deleteLater();
                                        timer->stop();
                                        timer->deleteLater();
                                    }
                                });

                        // Securite : timeout apres 30s pour ne pas bloquer
                        // si le service ne revient jamais.
                        QTimer::singleShot(30000, this,
                                           [progressDialog, timer]() {
                                               if (progressDialog->isVisible()) {
                                                   progressDialog->done(QMessageBox::Cancel);
                                                   progressDialog->deleteLater();
                                                   timer->stop();
                                                   timer->deleteLater();
                                               }
                                           });

                        // Attendre un peu avant de commencer le polling, le
                        // temps que le backend ait vraiment redemarre.
                        QTimer::singleShot(1000, this, [timer]() { timer->start(); });

                    } else {
                        progressDialog->done(QMessageBox::Cancel);
                        progressDialog->deleteLater();

                        if (status == 401) {
                            QMessageBox::warning(this, tr("Restart"),
                                                 tr("Authentication failed. Please log in again."));
                        } else if (status == 403) {
                            QMessageBox::warning(this, tr("Restart"),
                                                 tr("Admin role required."));
                        } else if (status == 0) {
                            QMessageBox::warning(this, tr("Restart"),
                                                 tr("Network error: %1").arg(reply->errorString()));
                        } else {
                            QMessageBox::warning(this, tr("Restart"),
                                                 tr("Restart failed: HTTP %1").arg(status));
                        }
                    }

                    reply->deleteLater();
                    nam->deleteLater();
                });

        // Affichage du dialog (modal, attend la fermeture).
        progressDialog->exec();
        return;
    }

    // Mode Local : comportement historique (stop + start du QProcess).
    stopService(serviceName);
    startService(serviceName, yamlPath);
}

/**
 * @brief Construit la clé unique identifiant un processus de service.
 *
 * @param projectName Nom du projet.
 * @param serviceName Nom du service.
 * @param port Port du service.
 * @return QString Clé unique.
 */
QString MainWindow::serviceProcessKey(const QString &projectName, const QString &serviceName, int port) const
{
    return QString("%1:%2:%3").arg(projectName, serviceName).arg(port);
}

/**
 * @brief Réagit à la sélection d'un projet.
 *
 * @param index Index sélectionné.
 */
void MainWindow::on_projectListView_clicked(const QModelIndex &index)
{
    _currentProjectRow = index.row();
    const auto& project = _projectModel->projectAt(_currentProjectRow);

    _serviceModel->setServices(project->services);
    _entityModel->setEntities({});
    _fieldModel->setFields({});
    _routeModel->clear();
    _currentServiceRoutes.clear();
}

/**
 * @brief Réagit à la sélection d'un service.
 *
 * Cette fonction charge toutes les entités, met à jour les labels, lance le
 * contrôle de statut et affiche toutes les routes du service.
 *
 * @param index Index sélectionné.
 */
void MainWindow::on_serviceListView_clicked(const QModelIndex &index)
{
    ui->serviceDBTypeLabel->setText("...");
    ui->servicePortLabel->setText("...");
    ui->serviceStatusLabel->setStyleSheet("color: grey;");

    _currentServiceRow = index.row();
    const auto& service = _serviceModel->serviceAt(_currentServiceRow);

    _entityModel->setEntities(service->schema.entities);
    _fieldModel->setFields({});

    ui->servicePortLabel->setText(QString::number(static_cast<int>(service->port)));
    ui->serviceDBTypeLabel->setText(
        QString::fromStdString(std::string(sea::domain::to_string(service->database_config.type)))
        );

    const QString serviceName = QString::fromStdString(service->name);

    ui->startServiceButton->setEnabled(false);
    ui->stopServiceButton->setEnabled(false);
    ui->restartServiceButton->setEnabled(false);

    _statusCheck->selectService(serviceName, "127.0.0.1", service->port);

    sea::application::RouteGenerator routeGenerator;
    _currentServiceRoutes = routeGenerator.generate(*service);

    // Important : afficher toutes les routes du service dès la sélection.
    _routeModel->setRoutes(_currentServiceRoutes);

    qDebug() << "selectService:" << serviceName
             << "port:" << QString::number(static_cast<int>(service->port))
             << "routes:" << _currentServiceRoutes.size();
}

/**
 * @brief Réagit à la sélection d'une entité.
 *
 * Cette version applique un filtrage plus intelligent des routes liées à
 * l'entité au lieu de comparer uniquement `route.entity_name`.
 *
 * @param index Index sélectionné.
 */
void MainWindow::on_entityListView_clicked(const QModelIndex &index)
{
    _currentEntityRow = index.row();
    const auto& entity = _entityModel->entityAt(_currentEntityRow);
    _fieldModel->setFields(entity->fields);

    std::vector<sea::application::RouteDefinition> filteredRoutes;
    const QString entityName = QString::fromStdString(entity->name);

    for (const auto& route : _currentServiceRoutes) {
        if (routeMatchesEntity(route, entityName)) {
            filteredRoutes.push_back(route);
        }
    }

    qDebug() << "entity name:" << entityName;
    qDebug() << "routes total:" << _currentServiceRoutes.size();
    qDebug() << "filtered count:" << filteredRoutes.size();

    for (const auto& route : filteredRoutes) {
        qDebug() << "  kept route:"
                 << QString::fromStdString(route.path)
                 << "entity_name:" << QString::fromStdString(route.entity_name)
                 << "operation:" << QString::fromStdString(route.operation_name);
    }
    _routeModel->setRoutes(std::move(filteredRoutes));
}

/**
 * @brief Réagit à la sélection d'un champ.
 *
 * @param index Index sélectionné.
 */
void MainWindow::on_fieldListView_clicked(const QModelIndex &index)
{
    Q_UNUSED(index);
}

/**
 * @brief Met à jour l'interface lors d'un statut RUNNING/STOPPED reçu.
 *
 * @param service Nom du service.
 * @param status Nouveau statut.
 * @param port Port du service.
 */
void MainWindow::onStatusUpdated(const QString &service, const QString &status, int port)
{
    Q_UNUSED(port);

    const QModelIndex current = ui->serviceListView->currentIndex();
    if (!current.isValid()) {
        return;
    }

    if (current.data().toString() != service) {
        return;
    }

    ui->serviceStatusLabel->setText(status);
    ui->serviceStatusLabel->setStyleSheet(
        status == "RUNNING"
            ? "color: #2ecc71; font-weight: bold;"
            : "color: #e74c3c; font-weight: bold;"
        );
}

/**
 * @brief Met à jour l'interface lorsqu'un service ne répond plus.
 *
 * @param service Nom du service.
 */
void MainWindow::onServiceUnreachable(const QString &service)
{
    const QModelIndex current = ui->serviceListView->currentIndex();
    if (!current.isValid()) {
        return;
    }

    if (current.data().toString() != service) {
        return;
    }

    ui->serviceStatusLabel->setText("STOPPED");
    ui->serviceStatusLabel->setStyleSheet("color: #e74c3c; font-weight: bold;");
}

/**
 * @brief Ouvre la documentation Swagger du service sélectionné.
 */
void MainWindow::on_swaggerServiceButton_clicked()
{
    if (_currentServiceRow < 0) {
        return;
    }

    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("Swagger"));
    dialog->resize(1200, 800);

    auto* layout = new QVBoxLayout(dialog);
    auto* view = new QWebEngineView(dialog);
    auto* closeButton = new QPushButton(tr("Back"), dialog);

    const auto& service = _serviceModel->serviceAt(_currentServiceRow);
    const QString url = QString("http://localhost:%1/docs").arg(service->port);

    view->setUrl(QUrl(url));
    layout->addWidget(view);
    layout->addWidget(closeButton);

    QObject::connect(closeButton, &QPushButton::clicked, dialog, &QDialog::close);

    dialog->setLayout(layout);
    dialog->show();
}

/**
 * @brief Construit le chemin de collection d'une entité.
 *
 * Exemple :
 * - Department -> /departments
 * - Employee   -> /employees
 *
 * @param entityName Nom de l'entité.
 * @return QString Chemin HTTP de collection.
 */
QString MainWindow::entityCollectionPath(const QString& entityName) const
{
    if (entityName.isEmpty()) {
        return {};
    }

    QString path = entityName;
    path[0] = path[0].toLower();
    return "/" + path + "s";
}

void MainWindow::promptLogin()
{
    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("Login"));
    dialog->resize(360, 140);

    auto* layout = new QFormLayout(dialog);
    auto* emailEdit = new QLineEdit(dialog);
    auto* passwordEdit = new QLineEdit(dialog);
    passwordEdit->setEchoMode(QLineEdit::Password);

    layout->addRow(tr("Email:"), emailEdit);
    layout->addRow(tr("Password:"), passwordEdit);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
        dialog
        );
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, [this, dialog, emailEdit, passwordEdit]() {
        const QString email = emailEdit->text().trimmed();
        const QString password = passwordEdit->text();

        if (email.isEmpty() || password.isEmpty()) {
            QMessageBox::warning(this, tr("Login"), tr("Email and password are required."));
            return;
        }

        dialog->close();
        loginUser(email, password);
    });

    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);

    dialog->setLayout(layout);
    dialog->show();
}

void MainWindow::loginUser(const QString &email, const QString &password)
{
    if (_currentServiceRow < 0) {
        QMessageBox::warning(this, tr("Login"), tr("No service selected."));
        return;
    }

    const auto& service = _serviceModel->serviceAt(_currentServiceRow);
    const QString url = QString("http://127.0.0.1:%1/auth/login")
                            .arg(static_cast<int>(service->port));

    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject body;
    body["email"] = email;
    body["password"] = password;

    auto* reply = _networkManager->post(
        request,
        QJsonDocument(body).toJson(QJsonDocument::Compact)
        );

    connect(reply, &QNetworkReply::finished, this, [this, reply, url]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::critical(
                this,
                tr("Login"),
                tr("Login failed.\nURL: %1\nError: %2")
                    .arg(url, reply->errorString())
                );
            return;
        }

        const QByteArray payload = reply->readAll();
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);

        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            QMessageBox::critical(this, tr("Login"), tr("Invalid JSON response."));
            return;
        }

        const QJsonObject obj = doc.object();

        if (!obj.contains("access_token") || !obj.value("access_token").isString()) {
            QMessageBox::critical(this, tr("Login"), tr("No JWT access_token received."));
            return;
        }

        _authToken = obj.value("access_token").toString();

        if (obj.contains("refresh_token") && obj.value("refresh_token").isString()) {
            _refreshToken = obj.value("refresh_token").toString();
        }
        qDebug() << "JWT =" << _authToken;

        updateAuthUi();
        QMessageBox::information(this, tr("Login"), tr("Login successful."));
    });
}

void MainWindow::logoutUser()
{
    _authToken.clear();
    updateAuthUi();
    QMessageBox::information(this, tr("Logout"), tr("Logged out."));
}

void MainWindow::updateAuthUi()
{
    const bool connected = !_authToken.isEmpty();

    ui->serviceLoginButton->setEnabled(!connected);
    ui->serviceLogoutButton->setEnabled(connected);

    ui->serviceAuthStatusLabel->setText(connected ? tr("Connected") : tr("Disconnected"));
    ui->serviceAuthStatusLabel->setStyleSheet(
        connected
            ? "color: #2ecc71; font-weight: bold;"
            : "color: #e74c3c; font-weight: bold;"
        );
}

void MainWindow::on_openEntityDataButton_clicked()
{
    if (_currentServiceRow < 0) {
        QMessageBox::warning(this, tr("Open Data"), tr("No service selected."));
        return;
    }
    if (_currentEntityRow < 0) {
        QMessageBox::warning(this, tr("Open Data"), tr("No entity selected."));
        return;
    }

    const auto& service = _serviceModel->serviceAt(_currentServiceRow);
    const auto& entity  = _entityModel->entityAt(_currentEntityRow);

    if (entity == nullptr) {
        QMessageBox::warning(this, tr("Open Data"), tr("Invalid entity."));
        return;
    }

    const QString entityName     = QString::fromStdString(entity->name);
    const QString collectionPath = entityCollectionPath(entityName);
    const QString baseUrl = QString("http://127.0.0.1:%1")
                                .arg(static_cast<int>(service->port));

    // Nouveau dialog : lazy rendering + pagination conditionnelle.
    // - Si entity.pagination est defini : utilise OFFSET / CURSOR / PAGE
    //   (priorite OFFSET > CURSOR > PAGE) avec infinite scroll
    // - Sinon : fetch tout en une fois + lazy rendering du QTableView
    //   (pas de freeze UI), bandeau d'avertissement si > 1000 lignes
    auto* dialog = new EntityDataDialog(*entity,
                                        baseUrl,
                                        collectionPath,
                                        _authToken,
                                        this);
    dialog->show();
}

/**
 * @brief Ouvre la boîte de dialogue de connexion.
 */
void MainWindow::on_serviceLoginButton_clicked()
{
    promptLogin();
}


/**
 * @brief Déconnecte l'utilisateur courant.
 */
void MainWindow::on_serviceLogoutButton_clicked()
{
    logoutUser();
}


/**
 * @brief Affiche un modal demandant le nom du projet et du service.
 */
bool MainWindow::promptNewProject(QString& projectName, QString& serviceName)
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("New Project"));
    dialog.resize(380, 150);

    auto* layout = new QFormLayout(&dialog);

    auto* projectEdit = new QLineEdit(&dialog);
    auto* serviceEdit = new QLineEdit(&dialog);

    layout->addRow(tr("Project name:"), projectEdit);
    layout->addRow(tr("Service name:"), serviceEdit);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
        &dialog
        );
    layout->addRow(buttons);

    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    // Normalisation : retrait des espaces de bordure, des caracteres
    // invalides, puis remplacement des espaces internes par '_'.
    const auto normalize = [](QString value) -> QString {
        value = value.trimmed();
        value.remove(QRegularExpression("[^A-Za-z0-9_ ]"));
        value.replace(QRegularExpression("\\s+"), "_");
        return value;
    };

    projectName = normalize(projectEdit->text());
    serviceName = normalize(serviceEdit->text());

    if (projectName.isEmpty() || serviceName.isEmpty()) {
        QMessageBox::warning(
            this,
            tr("New Project"),
            tr("Project name and service name are required.")
            );
        return false;
    }

    return true;
}

/**
 * @brief Construit le bloc YAML d'un service en configuration de
 *        production.
 */
QString MainWindow::buildProductionServiceBlock(const QString& projectName,
                                                const QString& serviceName) const
{
    const QString databaseName = projectName.toLower() + "_db";
    const QString logFileName  = projectName.toLower() + ".log";

    QString block;
    QTextStream out(&block);

    out << "  - name: " << serviceName << "\n";
    out << "    port: 8080\n";
    out << "\n";

    // ── Base de donnees ──────────────────────────────────────────
    out << "    database:\n";
    out << "      type: mysql\n";
    out << "      host: 127.0.0.1\n";
    out << "      port: 3306\n";
    out << "      database_name: " << databaseName << "\n";
    out << "      username: \"\"\n";
    out << "      password: \"\"\n";
    out << "      migrations:\n";
    out << "        enabled: true\n";
    out << "        create_database_if_missing: true\n";
    out << "        mode: conservative\n";
    out << "\n";

    // ── Securite (configuration minimale de production) ──────────
    out << "    security:\n";
    out << "      authentication:\n";
    out << "        type: jwt\n";
    out << "        algorithm: HS256\n";
    out << "        secret: \"${SEA_DESKTOP_JWT_SECRET}\"\n";
    out << "        issuer: " << projectName << "\n";
    out << "        access_token_ttl: 15m\n";
    out << "        refresh_token_ttl: 7d\n";
    out << "        token_delivery: cookie\n";
    out << "      cors:\n";
    out << "        allowed_origins:\n";
    out << "          - \"https://localhost\"\n";
    out << "        allowed_methods:\n";
    out << "          - GET\n";
    out << "          - POST\n";
    out << "          - PUT\n";
    out << "          - DELETE\n";
    out << "        allow_credentials: true\n";
    out << "      headers:\n";
    out << "        preset: strict\n";
    out << "      http_limits:\n";
    out << "        max_body_size: \"10MB\"\n";
    out << "        request_timeout: 30s\n";
    out << "        max_connections_per_ip: 100\n";
    out << "\n";

    // ── Logging (production) ─────────────────────────────────────
    out << "    logging:\n";
    out << "      enabled: true\n";
    out << "      level: info\n";
    out << "      sinks:\n";
    out << "        - type: console\n";
    out << "          format: text\n";
    out << "          enabled: true\n";
    out << "        - type: file\n";
    out << "          format: json\n";
    out << "          enabled: true\n";
    out << "          path: \"./logs/" << logFileName << "\"\n";
    out << "          rotation:\n";
    out << "            max_size: \"100MB\"\n";
    out << "            time_pattern: daily\n";
    out << "            max_files: 10\n";
    out << "            compress: true\n";
    out << "      flush_level: error\n";
    out << "      async:\n";
    out << "        enabled: true\n";
    out << "        queue_size: 8192\n";
    out << "        overflow_policy: overrun_oldest\n";

    return block;
}

/**
 * @brief Construit le contenu YAML d'un projet en configuration
 *        minimale de production.
 */
QString MainWindow::buildProductionYaml(const QString& projectName,
                                        const QString& serviceName) const
{
    QString yaml;
    QTextStream out(&yaml);

    out << "project:\n";
    out << "  name: " << projectName << "\n";
    out << "\n";
    out << "services:\n";
    out << buildProductionServiceBlock(projectName, serviceName);

    return yaml;
}

/**
 * @brief Construit le bloc YAML d'une entite, indente pour 'entities:'.
 *
 * Indentation : l'entite est un element de la sequence 'entities:' d'un
 * service. 'entities:' etant a 4 espaces, le tiret de l'entite est a 6
 * espaces et son contenu a 8.
 */
QString MainWindow::buildEntityBlock(const QString& entityName,
                                     bool enableCrud,
                                     bool timestamps,
                                     bool softDelete,
                                     const QVector<EntityFieldDraft>& fields) const
{
    QString block;
    QTextStream out(&block);

    out << "      - name: " << entityName << "\n";

    // options:
    out << "        options:\n";
    out << "          enable_crud: " << (enableCrud ? "true" : "false") << "\n";
    out << "          timestamps: "  << (timestamps ? "true" : "false") << "\n";
    out << "          soft_delete: " << (softDelete ? "true" : "false") << "\n";

    // fields:
    out << "        fields:\n";
    for (const EntityFieldDraft& field : fields) {
        out << "          - name: " << field.name << "\n";
        out << "            type: " << field.type << "\n";
        // N'ecrire les attributs booleens que lorsqu'ils sont vrais :
        // les valeurs par defaut du parser sont 'false'.
        if (field.required) {
            out << "            required: true\n";
        }
        if (field.unique) {
            out << "            unique: true\n";
        }
        if (field.indexed) {
            out << "            indexed: true\n";
        }
    }

    return block;
}

/**
 * @brief Insere un bloc entite dans le service cible d'un fichier YAML.
 */
bool MainWindow::insertEntityIntoYaml(QString& yamlContent,
                                      const QString& serviceName,
                                      const QString& entityBlock) const
{
    QStringList lines = yamlContent.split('\n');

    // 1. Localiser la ligne de debut du service : "  - name: <serviceName>".
    const QString serviceHeader = "  - name: " + serviceName;
    int serviceStart = -1;
    for (int i = 0; i < lines.size(); ++i) {
        if (lines.at(i).trimmed() == serviceHeader.trimmed() &&
            lines.at(i).startsWith("  - name:")) {
            serviceStart = i;
            break;
        }
    }
    if (serviceStart < 0) {
        return false; // Service introuvable.
    }

    // 2. Determiner la fin du bloc du service : prochaine ligne "  - name:"
    //    (service suivant) ou prochaine section racine (colonne 0), sinon
    //    la fin du fichier.
    int serviceEnd = lines.size(); // exclusif
    for (int i = serviceStart + 1; i < lines.size(); ++i) {
        const QString& line = lines.at(i);
        if (line.startsWith("  - name:")) {
            serviceEnd = i;
            break;
        }
        // Ligne non vide et non indentee = nouvelle section racine.
        if (!line.isEmpty() && !line.at(0).isSpace()) {
            serviceEnd = i;
            break;
        }
    }

    // 3. Chercher une section "    entities:" dans le bloc du service.
    int entitiesLine = -1;
    for (int i = serviceStart + 1; i < serviceEnd; ++i) {
        if (lines.at(i) == "    entities:") {
            entitiesLine = i;
            break;
        }
    }

    const QStringList entityLines = entityBlock.split('\n');

    if (entitiesLine >= 0) {
        // 3a. Section 'entities:' existante : inserer l'entite a la fin de
        //     cette section (avant la prochaine cle de meme niveau ou la
        //     fin du bloc service).
        int insertAt = serviceEnd;
        for (int i = entitiesLine + 1; i < serviceEnd; ++i) {
            const QString& line = lines.at(i);
            if (line.isEmpty()) {
                continue;
            }
            // Cle indentee a 4 espaces = section soeur de 'entities:'.
            if (line.length() >= 5 &&
                line.startsWith("    ") &&
                !line.at(4).isSpace()) {
                insertAt = i;
                break;
            }
        }
        for (int k = entityLines.size() - 1; k >= 0; --k) {
            lines.insert(insertAt, entityLines.at(k));
        }
    } else {
        // 3b. Pas de section 'entities:' : la creer a la fin du bloc
        //     service, suivie de l'entite.
        QStringList toInsert;
        toInsert << "    entities:";
        toInsert << entityLines;

        for (int k = toInsert.size() - 1; k >= 0; --k) {
            lines.insert(serviceEnd, toInsert.at(k));
        }
    }

    yamlContent = lines.join('\n');
    return true;
}

/**
 * @brief Remplace la valeur du port d'un service dans un YAML.
 */
bool MainWindow::replacePortInService(QString& yamlContent,
                                      const QString& serviceName,
                                      int newPort) const
{
    QStringList lines = yamlContent.split('\n');

    // 1. Localiser la ligne de debut du service.
    const QString serviceHeader = "  - name: " + serviceName;
    int serviceStart = -1;
    for (int i = 0; i < lines.size(); ++i) {
        if (lines.at(i).trimmed() == serviceHeader.trimmed() &&
            lines.at(i).startsWith("  - name:")) {
            serviceStart = i;
            break;
        }
    }
    if (serviceStart < 0) {
        return false; // Service introuvable.
    }

    // 2. Determiner la fin du bloc du service.
    int serviceEnd = lines.size();
    for (int i = serviceStart + 1; i < lines.size(); ++i) {
        const QString& line = lines.at(i);
        if (line.startsWith("  - name:")) {
            serviceEnd = i;
            break;
        }
        if (!line.isEmpty() && !line.at(0).isSpace()) {
            serviceEnd = i;
            break;
        }
    }

    // 3. Chercher la cle 'port:' indentee a EXACTEMENT 4 espaces.
    //    Le port de la base de donnees est indente a 6 espaces : on
    //    l'ignore en exigeant que le 5e caractere ne soit pas un espace.
    for (int i = serviceStart + 1; i < serviceEnd; ++i) {
        const QString& line = lines.at(i);
        if (line.startsWith("    port:") &&
            (line.length() == 4 || !line.at(4).isSpace())) {
            lines[i] = "    port: " + QString::number(newPort);
            yamlContent = lines.join('\n');
            return true;
        }
    }

    return false; // Cle 'port:' introuvable dans le service.
}

/**
 * @brief Remplace le nom du projet dans un YAML.
 */
bool MainWindow::replaceProjectName(QString& yamlContent,
                                    const QString& newName) const
{
    QStringList lines = yamlContent.split('\n');

    // 1. Localiser la section racine 'project:'.
    int projectStart = -1;
    for (int i = 0; i < lines.size(); ++i) {
        if (lines.at(i) == "project:") {
            projectStart = i;
            break;
        }
    }
    if (projectStart < 0) {
        return false; // Section 'project:' introuvable.
    }

    // 2. Determiner la fin du bloc 'project:' : prochaine ligne non vide
    //    et non indentee (nouvelle section racine).
    int projectEnd = lines.size();
    for (int i = projectStart + 1; i < lines.size(); ++i) {
        const QString& line = lines.at(i);
        if (!line.isEmpty() && !line.at(0).isSpace()) {
            projectEnd = i;
            break;
        }
    }

    // 3. Chercher la cle 'name:' indentee a 2 espaces dans ce bloc.
    for (int i = projectStart + 1; i < projectEnd; ++i) {
        if (lines.at(i).startsWith("  name:")) {
            lines[i] = "  name: " + newName;
            yamlContent = lines.join('\n');
            return true;
        }
    }

    return false; // Cle 'name:' introuvable sous 'project:'.
}

/**
 * @brief Remplace les options d'une entite dans un YAML.
 */
bool MainWindow::replaceEntityOptions(QString& yamlContent,
                                      const QString& serviceName,
                                      const QString& entityName,
                                      bool enableCrud,
                                      bool timestamps,
                                      bool softDelete) const
{
    QStringList lines = yamlContent.split('\n');

    // 1. Localiser le bloc du service.
    int serviceStart = -1;
    for (int i = 0; i < lines.size(); ++i) {
        if (lines.at(i).trimmed() == ("- name: " + serviceName).trimmed() &&
            lines.at(i).startsWith("  - name:")) {
            serviceStart = i;
            break;
        }
    }
    if (serviceStart < 0) {
        return false; // Service introuvable.
    }

    int serviceEnd = lines.size();
    for (int i = serviceStart + 1; i < lines.size(); ++i) {
        const QString& line = lines.at(i);
        if (line.startsWith("  - name:")) {
            serviceEnd = i;
            break;
        }
        if (!line.isEmpty() && !line.at(0).isSpace()) {
            serviceEnd = i;
            break;
        }
    }

    // 2. Localiser le bloc de l'entite dans le service.
    //    Une entite commence par "      - name: <entityName>" (6 espaces).
    int entityStart = -1;
    for (int i = serviceStart + 1; i < serviceEnd; ++i) {
        if (lines.at(i).startsWith("      - name:") &&
            lines.at(i).mid(QString("      - name:").length()).trimmed()
                == entityName) {
            entityStart = i;
            break;
        }
    }
    if (entityStart < 0) {
        return false; // Entite introuvable.
    }

    // 3. Determiner la fin du bloc de l'entite : prochaine entite
    //    ("      - name:") ou fin du bloc service.
    int entityEnd = serviceEnd;
    for (int i = entityStart + 1; i < serviceEnd; ++i) {
        if (lines.at(i).startsWith("      - name:")) {
            entityEnd = i;
            break;
        }
    }

    // 4. Chercher la section "        options:" (8 espaces) dans l'entite.
    int optionsLine = -1;
    for (int i = entityStart + 1; i < entityEnd; ++i) {
        if (lines.at(i) == "        options:") {
            optionsLine = i;
            break;
        }
    }

    // Lignes des trois options, indentees a 10 espaces.
    const QString crudLine =
        QString("          enable_crud: ") + (enableCrud ? "true" : "false");
    const QString tsLine =
        QString("          timestamps: ") + (timestamps ? "true" : "false");
    const QString sdLine =
        QString("          soft_delete: ") + (softDelete ? "true" : "false");

    if (optionsLine < 0) {
        // 4a. Pas de section 'options:' : la creer juste apres la ligne
        //     "      - name:" de l'entite.
        QStringList block;
        block << "        options:";
        block << crudLine << tsLine << sdLine;
        for (int k = block.size() - 1; k >= 0; --k) {
            lines.insert(entityStart + 1, block.at(k));
        }
        yamlContent = lines.join('\n');
        return true;
    }

    // 4b. Section 'options:' existante : determiner sa fin (prochaine cle
    //     a 8 espaces, ou fin du bloc entite).
    int optionsEnd = entityEnd;
    for (int i = optionsLine + 1; i < entityEnd; ++i) {
        const QString& line = lines.at(i);
        if (line.isEmpty()) {
            continue;
        }
        if (line.length() >= 9 &&
            line.startsWith("        ") &&
            !line.at(8).isSpace()) {
            optionsEnd = i;
            break;
        }
    }

    // Remplacer chaque option existante ; noter celles a ajouter.
    bool foundCrud = false, foundTs = false, foundSd = false;
    for (int i = optionsLine + 1; i < optionsEnd; ++i) {
        const QString trimmed = lines.at(i).trimmed();
        if (trimmed.startsWith("enable_crud:")) {
            lines[i] = crudLine;
            foundCrud = true;
        } else if (trimmed.startsWith("timestamps:")) {
            lines[i] = tsLine;
            foundTs = true;
        } else if (trimmed.startsWith("soft_delete:")) {
            lines[i] = sdLine;
            foundSd = true;
        }
    }

    // Ajouter les options absentes juste apres la ligne 'options:'.
    QStringList missing;
    if (!foundCrud) { missing << crudLine; }
    if (!foundTs)   { missing << tsLine; }
    if (!foundSd)   { missing << sdLine; }
    for (int k = missing.size() - 1; k >= 0; --k) {
        lines.insert(optionsLine + 1, missing.at(k));
    }

    yamlContent = lines.join('\n');
    return true;
}

void MainWindow::on_actionAdd_New_Project_triggered()
{
    // 1. Demander le nom du projet et du service.
    QString projectName;
    QString serviceName;
    if (!promptNewProject(projectName, serviceName)) {
        return;
    }

    // 2. Verifier l'existence puis creer si absent.
    //    Les deux operations sont asynchrones et chainees via .then().
    _projectRepository->projectExists(projectName)
        .then(this, [this, projectName, serviceName](bool exists) {
            if (exists) {
                QMessageBox::warning(this, tr("Error"),
                                     tr("This project already exists."));
                return QFuture<bool>();  // chaine cassee, suite ignoree
            }
            // Lance la creation et retourne ce future a la chaine.
            return _projectRepository->writeRawYaml(
                projectName,
                buildProductionYaml(projectName, serviceName)
                );
        })
        .unwrap()
        .then(this, [this](bool success) {
            if (!success) {
                return; // .onFailed n'a pas ete declenche mais succes=false
            }
            // Recharger explicitement la liste des projets.
            loadProjects();
            QMessageBox::information(this, tr("Success"),
                                     tr("Project created in configs/"));
        })
        .onFailed(this, [this](const std::exception& e) {
            QMessageBox::critical(this, tr("Error"),
                                  tr("Failed to create project: %1")
                                      .arg(QString::fromUtf8(e.what())));
        });
}

void MainWindow::on_actionAdd_New_Service_triggered()
{
    // 1. Demander a quel projet ajouter le service.
    QString projectName;
    if (!promptSelectProject(tr("Add New Service"), projectName)) {
        return;
    }

    // 2. Retrouver le projet dans le modele pour verifier les doublons.
    const sea::domain::Project* project = nullptr;
    const QModelIndex noParent;
    const int projectCount = _projectModel->rowCount(noParent);
    for (int row = 0; row < projectCount; ++row) {
        const sea::domain::Project* candidate = _projectModel->projectAt(row);
        if (candidate != nullptr &&
            QString::fromStdString(candidate->name) == projectName) {
            project = candidate;
            break;
        }
    }

    if (project == nullptr) {
        QMessageBox::critical(
            this,
            tr("Add New Service"),
            tr("The selected project could not be found.")
            );
        return;
    }

    // 3. Demander le nom du nouveau service.
    bool ok = false;
    QString serviceName = QInputDialog::getText(
        this,
        tr("Add New Service"),
        tr("Service name:"),
        QLineEdit::Normal,
        "",
        &ok
        );

    if (!ok) {
        return;
    }

    // Normalisation (memes regles que pour le nom de projet).
    serviceName = serviceName.trimmed();
    serviceName.remove(QRegularExpression("[^A-Za-z0-9_ ]"));
    serviceName.replace(QRegularExpression("\\s+"), "_");

    if (serviceName.isEmpty()) {
        QMessageBox::warning(
            this,
            tr("Add New Service"),
            tr("The service name is required.")
            );
        return;
    }

    // 4. Refuser un service portant un nom deja present dans le projet.
    if (project->has_service(serviceName.toStdString())) {
        QMessageBox::warning(
            this,
            tr("Add New Service"),
            tr("A service with this name already exists in the project.")
            );
        return;
    }

    // 5. Lire le YAML, modifier en memoire, reecrire.
    //    Chaine asynchrone : read -> modify -> write -> loadProjects.
    _projectRepository->readRawYaml(projectName)
        .then(this, [this, projectName, serviceName](QString content) {
            // Inserer le nouveau bloc service.
            // Les YAML generes placent 'services:' en derniere section : le
            // nouveau service est donc ajoute a la fin du fichier, dans la
            // sequence 'services:'. Le reste du fichier reste inchange.
            if (!content.endsWith('\n')) {
                content += '\n';
            }
            content += '\n';
            content += buildProductionServiceBlock(projectName, serviceName);

            // Lance l'ecriture et retourne ce future a la chaine.
            return _projectRepository->writeRawYaml(projectName, content);
        })
        .unwrap()
        .then(this, [this](bool success) {
            if (!success) {
                return;
            }
            // Recharger la liste des projets.
            loadProjects();
            QMessageBox::information(
                this,
                tr("Add New Service"),
                tr("Service added to the project.")
                );
        })
        .onFailed(this, [this](const std::exception& e) {
            QMessageBox::critical(
                this,
                tr("Add New Service"),
                tr("Failed to add service: %1")
                    .arg(QString::fromUtf8(e.what()))
                );
        });
}

void MainWindow::on_actionAdd_New_Entity_triggered()
{
    // 1. Choisir le projet.
    QString projectName;
    if (!promptSelectProject(tr("Add New Entity"), projectName)) {
        return;
    }

    // 2. Retrouver le projet dans le modele.
    const sea::domain::Project* project = nullptr;
    const QModelIndex noParent;
    const int projectCount = _projectModel->rowCount(noParent);
    for (int row = 0; row < projectCount; ++row) {
        const sea::domain::Project* candidate = _projectModel->projectAt(row);
        if (candidate != nullptr &&
            QString::fromStdString(candidate->name) == projectName) {
            project = candidate;
            break;
        }
    }
    if (project == nullptr) {
        QMessageBox::critical(
            this,
            tr("Add New Entity"),
            tr("The selected project could not be found.")
            );
        return;
    }

    // 3. Choisir le service du projet.
    QString serviceName;
    if (!promptSelectService(*project, tr("Add New Entity"), serviceName)) {
        return;
    }

    // 4. Saisir le nom de l'entite.
    bool ok = false;
    QString entityName = QInputDialog::getText(
        this,
        tr("Add New Entity"),
        tr("Entity name:"),
        QLineEdit::Normal,
        "",
        &ok
        );
    if (!ok) {
        return;
    }
    entityName = entityName.trimmed();
    entityName.remove(QRegularExpression("[^A-Za-z0-9_ ]"));
    entityName.replace(QRegularExpression("\\s+"), "_");
    if (entityName.isEmpty()) {
        QMessageBox::warning(
            this,
            tr("Add New Entity"),
            tr("The entity name is required.")
            );
        return;
    }

    // 5. Choisir les options de l'entite.
    bool enableCrud = true;
    bool timestamps = true;
    bool softDelete = false;
    {
        QDialog optionsDialog(this);
        optionsDialog.setWindowTitle(tr("Entity options"));

        auto* layout = new QFormLayout(&optionsDialog);

        auto* crudCheck = new QCheckBox(&optionsDialog);
        crudCheck->setChecked(true);
        auto* timestampsCheck = new QCheckBox(&optionsDialog);
        timestampsCheck->setChecked(true);
        auto* softDeleteCheck = new QCheckBox(&optionsDialog);

        layout->addRow(tr("Enable CRUD:"), crudCheck);
        layout->addRow(tr("Timestamps:"), timestampsCheck);
        layout->addRow(tr("Soft delete:"), softDeleteCheck);

        auto* buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
            &optionsDialog
            );
        layout->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &optionsDialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &optionsDialog, &QDialog::reject);

        if (optionsDialog.exec() != QDialog::Accepted) {
            return;
        }

        enableCrud = crudCheck->isChecked();
        timestamps = timestampsCheck->isChecked();
        softDelete = softDeleteCheck->isChecked();
    }

    // 6. Saisir les champs un par un (au moins un champ exige).
    QVector<EntityFieldDraft> fields;
    while (true) {
        EntityFieldDraft draft;
        if (!promptEntityField(draft)) {
            if (fields.isEmpty()) {
                return;
            }
            break;
        }

        bool duplicate = false;
        for (const EntityFieldDraft& existing : fields) {
            if (existing.name == draft.name) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            QMessageBox::warning(
                this,
                tr("Add New Entity"),
                tr("A field with this name already exists in the entity.")
                );
            continue;
        }

        fields.push_back(draft);

        const auto answer = QMessageBox::question(
            this,
            tr("Add New Entity"),
            tr("Field added. Do you want to add another field?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes
            );
        if (answer != QMessageBox::Yes) {
            break;
        }
    }

    // 7. Capturer le port du service (utilise plus tard pour le restart).
    //    On le capture maintenant car `project` peut etre invalide apres
    //    le rechargement asynchrone.
    int servicePort = 0;
    {
        const sea::domain::Service* service =
            project->find_service(serviceName.toStdString());
        if (service != nullptr) {
            servicePort = static_cast<int>(service->port);
        }
    }

    // 8. Construire le bloc entite avant la chaine asynchrone.
    const QString entityBlock = buildEntityBlock(
        entityName, enableCrud, timestamps, softDelete, fields
        );

    // 9. Chaine asynchrone : read -> modify -> write -> reload -> ask restart.
    _projectRepository->readRawYaml(projectName)
        .then(this, [this, projectName, serviceName, entityBlock](QString content) {
            if (!insertEntityIntoYaml(content, serviceName, entityBlock)) {
                throw std::runtime_error(
                    "Unable to locate the service in the YAML file.");
            }
            return _projectRepository->writeRawYaml(projectName, content);
        })
        .unwrap()
        .then(this, [this, projectName, serviceName, servicePort](bool success) {
            if (!success) {
                return;
            }

            loadProjects();

            const auto applyAnswer = QMessageBox::question(
                this,
                tr("Add New Entity"),
                tr("Entity added to the YAML file.\n\n"
                   "Do you want to apply the changes to the database now?\n"
                   "This will restart the service."),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No
                );

            if (applyAnswer == QMessageBox::Yes && servicePort > 0) {
                const QString yamlPath = yamlPathForProject(projectName);
                stopServiceProcess(projectName, serviceName, servicePort);
                startServiceProcess(projectName, serviceName, servicePort, yamlPath);
            }

            QMessageBox::information(
                this,
                tr("Add New Entity"),
                tr("Entity added successfully.")
                );
        })
        .onFailed(this, [this](const std::exception& e) {
            QMessageBox::critical(
                this,
                tr("Add New Entity"),
                tr("Failed to add entity: %1")
                    .arg(QString::fromUtf8(e.what()))
                );
        });
}

/**
 * @brief Affiche un dialogue de selection d'un projet charge.
 */
bool MainWindow::promptSelectProject(const QString& title, QString& chosen)
{
    const QModelIndex noParent;
    const int projectCount = _projectModel->rowCount(noParent);

    if (projectCount == 0) {
        QMessageBox::information(
            this,
            title,
            tr("No project is currently available.")
            );
        return false;
    }

    QStringList projectNames;
    for (int row = 0; row < projectCount; ++row) {
        const sea::domain::Project* project = _projectModel->projectAt(row);
        if (project != nullptr) {
            projectNames << QString::fromStdString(project->name);
        }
    }

    QDialog dialog(this);
    dialog.setWindowTitle(title);
    dialog.resize(380, 300);

    auto* layout = new QVBoxLayout(&dialog);
    auto* list   = new QListWidget(&dialog);
    list->addItems(projectNames);
    list->setCurrentRow(0);
    layout->addWidget(list);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
        &dialog
        );
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(list, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    const int selectedRow = list->currentRow();
    if (selectedRow < 0 || selectedRow >= projectNames.size()) {
        return false;
    }

    chosen = projectNames.at(selectedRow);
    return true;
}

/**
 * @brief Affiche un dialogue de selection d'un service d'un projet.
 */
bool MainWindow::promptSelectService(const sea::domain::Project& project,
                                     const QString& title,
                                     QString& chosen)
{
    if (project.services.empty()) {
        QMessageBox::information(
            this,
            title,
            tr("This project has no service.")
            );
        return false;
    }

    QStringList serviceNames;
    for (const sea::domain::Service& service : project.services) {
        serviceNames << QString::fromStdString(service.name);
    }

    QDialog dialog(this);
    dialog.setWindowTitle(title);
    dialog.resize(380, 300);

    auto* layout = new QVBoxLayout(&dialog);
    auto* list   = new QListWidget(&dialog);
    list->addItems(serviceNames);
    list->setCurrentRow(0);
    layout->addWidget(list);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
        &dialog
        );
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(list, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    const int selectedRow = list->currentRow();
    if (selectedRow < 0 || selectedRow >= serviceNames.size()) {
        return false;
    }

    chosen = serviceNames.at(selectedRow);
    return true;
}

/**
 * @brief Affiche un dialogue de selection d'une entite d'un service.
 */
bool MainWindow::promptSelectEntity(const sea::domain::Service& service,
                                    const QString& title,
                                    QString& chosen)
{
    if (service.schema.entities.empty()) {
        QMessageBox::information(
            this,
            title,
            tr("This service has no entity.")
            );
        return false;
    }

    QStringList entityNames;
    for (const sea::domain::Entity& entity : service.schema.entities) {
        entityNames << QString::fromStdString(entity.name);
    }

    QDialog dialog(this);
    dialog.setWindowTitle(title);
    dialog.resize(380, 300);

    auto* layout = new QVBoxLayout(&dialog);
    auto* list   = new QListWidget(&dialog);
    list->addItems(entityNames);
    list->setCurrentRow(0);
    layout->addWidget(list);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
        &dialog
        );
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(list, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    const int selectedRow = list->currentRow();
    if (selectedRow < 0 || selectedRow >= entityNames.size()) {
        return false;
    }

    chosen = entityNames.at(selectedRow);
    return true;
}

/**
 * @brief Affiche un dialogue de saisie d'un champ d'entite.
 */
bool MainWindow::promptEntityField(EntityFieldDraft& draft)
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Add field"));
    dialog.resize(360, 220);

    auto* layout = new QFormLayout(&dialog);

    auto* nameEdit = new QLineEdit(&dialog);

    auto* typeCombo = new QComboBox(&dialog);
    // Types confirmes dans field_type_from_string().
    typeCombo->addItems({
        "string", "int", "float", "bool", "timestamp", "uuid",
        "bigint", "smallint", "decimal", "json", "binary",
        "password", "email", "text", "file"
    });

    auto* requiredCheck = new QCheckBox(&dialog);
    auto* uniqueCheck   = new QCheckBox(&dialog);
    auto* indexedCheck  = new QCheckBox(&dialog);

    layout->addRow(tr("Field name:"), nameEdit);
    layout->addRow(tr("Type:"), typeCombo);
    layout->addRow(tr("Required:"), requiredCheck);
    layout->addRow(tr("Unique:"), uniqueCheck);
    layout->addRow(tr("Indexed:"), indexedCheck);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
        &dialog
        );
    layout->addRow(buttons);

    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    // Normalisation du nom de champ (memes regles que projet / service).
    QString fieldName = nameEdit->text().trimmed();
    fieldName.remove(QRegularExpression("[^A-Za-z0-9_ ]"));
    fieldName.replace(QRegularExpression("\\s+"), "_");

    if (fieldName.isEmpty()) {
        QMessageBox::warning(
            this,
            tr("Add field"),
            tr("The field name is required.")
            );
        return false;
    }

    draft.name     = fieldName;
    draft.type     = typeCombo->currentText();
    draft.required = requiredCheck->isChecked();
    draft.unique   = uniqueCheck->isChecked();
    draft.indexed  = indexedCheck->isChecked();
    return true;
}

void MainWindow::on_actionImport_Yaml_triggered()
{
    // 1. Choisir le fichier YAML a importer.
    const QString sourcePath = QFileDialog::getOpenFileName(
        this,
        tr("Import Yaml"),
        QString(),
        tr("YAML files (*.yaml *.yml)")
        );

    if (sourcePath.isEmpty()) {
        return; // Annule par l'utilisateur.
    }

    // 2. Lire le contenu du fichier source (filesystem local).
    QString content;
    {
        QFile sourceFile(sourcePath);
        if (!sourceFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QMessageBox::critical(
                this,
                tr("Import Yaml"),
                tr("Unable to open the source file.")
                );
            return;
        }
        QTextStream in(&sourceFile);
        in.setEncoding(QStringConverter::Utf8);
        content = in.readAll();
        sourceFile.close();
    }

    // 3. Determiner le nom logique du projet a partir du fichier source.
    const QFileInfo sourceInfo(sourcePath);
    const QString projectName = sourceInfo.completeBaseName();

    // 4. Verifier si un projet du meme nom existe, demander confirmation,
    //    puis ecrire. Chaine asynchrone.
    _projectRepository->projectExists(projectName)
        .then(this, [this, projectName, content](bool exists) {
            if (exists) {
                const auto answer = QMessageBox::question(
                    this,
                    tr("Import Yaml"),
                    tr("A project with this name already exists.\n"
                       "Do you want to replace it?"),
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::No
                    );

                if (answer != QMessageBox::Yes) {
                    return QFuture<bool>();  // chaine cassee
                }
            }
            // writeRawYaml cree le fichier s'il n'existe pas, ou le remplace
            // s'il existe.
            return _projectRepository->writeRawYaml(projectName, content);
        })
        .unwrap()
        .then(this, [this](bool success) {
            if (!success) {
                return;
            }
            loadProjects();
            QMessageBox::information(
                this,
                tr("Import Yaml"),
                tr("YAML file imported into configs/")
                );
        })
        .onFailed(this, [this](const std::exception& e) {
            QMessageBox::critical(
                this,
                tr("Import Yaml"),
                tr("Failed to import: %1")
                    .arg(QString::fromUtf8(e.what()))
                );
        });
}

void MainWindow::on_actionExport_Yaml_triggered()
{
    // 1. Demander quel projet exporter.
    QString projectName;
    if (!promptSelectProject(tr("Export Yaml"), projectName)) {
        return;
    }

    // 2. Lire le contenu du YAML via le repository, puis demander la
    //    destination et ecrire localement.
    _projectRepository->readRawYaml(projectName)
        .then(this, [this, projectName](const QString& content) {
            // 3. Choisir l'emplacement de destination.
            const QString destPath = QFileDialog::getSaveFileName(
                this,
                tr("Export Yaml"),
                projectName + ".yaml",
                tr("YAML files (*.yaml *.yml)")
                );

            if (destPath.isEmpty()) {
                return; // Annule par l'utilisateur.
            }

            // 4. Ecrire le contenu vers la destination locale (filesystem
            //    direct, car la destination est choisie par l'utilisateur,
            //    hors du depot).
            QFile destFile(destPath);
            if (!destFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
                QMessageBox::critical(
                    this,
                    tr("Export Yaml"),
                    tr("Unable to write to the destination file.")
                    );
                return;
            }

            QTextStream out(&destFile);
            out.setEncoding(QStringConverter::Utf8);
            out << content;
            destFile.close();

            QMessageBox::information(
                this,
                tr("Export Yaml"),
                tr("Project exported successfully.")
                );
        })
        .onFailed(this, [this](const std::exception& e) {
            QMessageBox::critical(
                this,
                tr("Export Yaml"),
                tr("Failed to export: %1")
                    .arg(QString::fromUtf8(e.what()))
                );
        });
}

void MainWindow::on_actionEdit_Yaml_triggered()
{
    // 1. Demander quel projet editer.
    QString projectName;
    if (!promptSelectProject(tr("Edit Yaml"), projectName)) {
        return;
    }

    // 2. Charger le contenu du fichier via le repository, puis ouvrir
    //    la fenetre d'edition.
    _projectRepository->readRawYaml(projectName)
        .then(this, [this, projectName](const QString& content) {

            // 3. Construire la fenetre d'edition.
            auto* dialog = new QDialog(this);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->setWindowTitle(tr("Edit Yaml - %1").arg(projectName));
            dialog->resize(800, 600);

            auto* layout = new QVBoxLayout(dialog);

            auto* editor = new QPlainTextEdit(dialog);
            editor->setLineWrapMode(QPlainTextEdit::NoWrap);
            editor->setPlainText(content);
            // Police a chasse fixe : l'indentation YAML reste lisible.
            editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
            layout->addWidget(editor);

            auto* buttons = new QDialogButtonBox(
                QDialogButtonBox::Save | QDialogButtonBox::Cancel,
                dialog
                );
            layout->addWidget(buttons);

            connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);

            // 4. Enregistrement : reecrire via le repository (asynchrone)
            //    puis fermer la fenetre.
            connect(buttons, &QDialogButtonBox::accepted, dialog,
                    [this, dialog, editor, projectName]() {
                        _projectRepository->writeRawYaml(projectName, editor->toPlainText())
                        .then(this, [this, dialog](bool success) {
                            if (!success) {
                                return;
                            }
                            // Recharger explicitement : le QFileSystemWatcher ne
                            // detecte pas la modification d'un fichier existant.
                            loadProjects();
                            dialog->accept();
                        })
                            .onFailed(this, [dialog](const std::exception& e) {
                                QMessageBox::critical(
                                    dialog,
                                    tr("Edit Yaml"),
                                    tr("Unable to save the YAML file: %1")
                                        .arg(QString::fromUtf8(e.what()))
                                    );
                            });
                    });

            dialog->show();
        })
        .onFailed(this, [this](const std::exception& e) {
            QMessageBox::critical(
                this,
                tr("Edit Yaml"),
                tr("Unable to read the YAML file: %1")
                    .arg(QString::fromUtf8(e.what()))
                );
        });
}

/**
 * @brief Collecte tous les services de tous les projets charges.
 */
QVector<MainWindow::ServiceLogInfo> MainWindow::collectAllServiceLogs() const
{
    QVector<ServiceLogInfo> entries;

    const QModelIndex noParent;
    const int projectCount = _projectModel->rowCount(noParent);

    for (int row = 0; row < projectCount; ++row) {
        const sea::domain::Project* project = _projectModel->projectAt(row);
        if (project == nullptr) {
            continue;
        }

        const QString projectName = QString::fromStdString(project->name);

        for (const sea::domain::Service& service : project->services) {
            ServiceLogInfo info;
            info.projectName = projectName;
            info.serviceName = QString::fromStdString(service.name);
            info.port        = static_cast<int>(service.port);

            const QString processKey =
                serviceProcessKey(info.projectName, info.serviceName, info.port);

            info.logPath   = appLogsDir() + "/" + processKey + ".log";
            info.logExists = QFileInfo::exists(info.logPath);

            entries.push_back(info);
        }
    }

    return entries;
}

/**
 * @brief Ouvre une fenetre a onglets affichant des journaux.
 */
void MainWindow::openLogsWindow(const QVector<ServiceLogInfo>& entries,
                                const QString& title)
{
    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(title);
    dialog->resize(900, 600);

    auto* layout = new QVBoxLayout(dialog);
    auto* tabs   = new QTabWidget(dialog);
    layout->addWidget(tabs);

    for (const ServiceLogInfo& entry : entries) {
        auto* viewer = new QPlainTextEdit(tabs);
        viewer->setReadOnly(true);
        viewer->setLineWrapMode(QPlainTextEdit::NoWrap);

        if (entry.logExists) {
            QFile logFile(entry.logPath);
            if (logFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream in(&logFile);
                in.setEncoding(QStringConverter::Utf8);
                viewer->setPlainText(in.readAll());
                logFile.close();

                // Faire defiler jusqu'aux derniers evenements.
                viewer->moveCursor(QTextCursor::End);
            } else {
                viewer->setPlainText(
                    tr("Unable to open the log file:\n%1").arg(entry.logPath)
                    );
            }
        } else {
            // Service jamais demarre : aucun fichier de journal.
            viewer->setPlainText(
                tr("No log available for this service yet.\n"
                   "The service has probably never been started.")
                );
        }

        // Onglet : "service (port)", suffixe " - no log" si indisponible.
        QString tabLabel = QString("%1 (%2)")
                               .arg(entry.serviceName)
                               .arg(entry.port);
        if (!entry.logExists) {
            tabLabel += tr(" - no log");
        }

        tabs->addTab(viewer, tabLabel);
    }

    dialog->show();
}

void MainWindow::on_actionEdit_Service_triggered()
{
    // 1. Choisir le projet.
    QString projectName;
    if (!promptSelectProject(tr("Edit Service"), projectName)) {
        return;
    }

    // 2. Retrouver le projet dans le modele.
    const sea::domain::Project* project = nullptr;
    const QModelIndex noParent;
    const int projectCount = _projectModel->rowCount(noParent);
    for (int row = 0; row < projectCount; ++row) {
        const sea::domain::Project* candidate = _projectModel->projectAt(row);
        if (candidate != nullptr &&
            QString::fromStdString(candidate->name) == projectName) {
            project = candidate;
            break;
        }
    }
    if (project == nullptr) {
        QMessageBox::critical(
            this,
            tr("Edit Service"),
            tr("The selected project could not be found.")
            );
        return;
    }

    // 3. Choisir le service.
    QString serviceName;
    if (!promptSelectService(*project, tr("Edit Service"), serviceName)) {
        return;
    }

    // 4. Recuperer le port actuel du service.
    const sea::domain::Service* service = project->find_service(
        serviceName.toStdString()
        );
    if (service == nullptr) {
        QMessageBox::critical(
            this,
            tr("Edit Service"),
            tr("The selected service could not be found.")
            );
        return;
    }
    const int currentPort = static_cast<int>(service->port);

    // 5. Demander le nouveau port (pre-rempli avec la valeur actuelle).
    bool ok = false;
    const int newPort = QInputDialog::getInt(
        this,
        tr("Edit Service"),
        tr("Port:"),
        currentPort,
        1,        // port minimum
        65535,    // port maximum
        1,
        &ok
        );
    if (!ok || newPort == currentPort) {
        return; // Annule ou inchange.
    }

    // 6. Chaine asynchrone : read -> modify -> write -> reload.
    _projectRepository->readRawYaml(projectName)
        .then(this, [this, projectName, serviceName, newPort](QString content) {
            if (!replacePortInService(content, serviceName, newPort)) {
                throw std::runtime_error(
                    "Unable to update the port in the YAML file.");
            }
            return _projectRepository->writeRawYaml(projectName, content);
        })
        .unwrap()
        .then(this, [this](bool success) {
            if (!success) {
                return;
            }
            loadProjects();
            QMessageBox::information(
                this,
                tr("Edit Service"),
                tr("Service updated successfully.")
                );
        })
        .onFailed(this, [this](const std::exception& e) {
            QMessageBox::critical(
                this,
                tr("Edit Service"),
                tr("Failed to edit service: %1")
                    .arg(QString::fromUtf8(e.what()))
                );
        });
}

void MainWindow::on_actionEdit_Project_triggered()
{
    // 1. Choisir le projet a renommer.
    QString oldName;
    if (!promptSelectProject(tr("Edit Project"), oldName)) {
        return;
    }

    // 2. Demander le nouveau nom (pre-rempli avec l'actuel).
    bool ok = false;
    QString newName = QInputDialog::getText(
        this,
        tr("Edit Project"),
        tr("New project name:"),
        QLineEdit::Normal,
        oldName,
        &ok
        );
    if (!ok) {
        return;
    }

    // Normalisation (memes regles que la creation de projet).
    newName = newName.trimmed();
    newName.remove(QRegularExpression("[^A-Za-z0-9_ ]"));
    newName.replace(QRegularExpression("\\s+"), "_");

    if (newName.isEmpty()) {
        QMessageBox::warning(
            this,
            tr("Edit Project"),
            tr("The project name is required.")
            );
        return;
    }

    if (newName == oldName) {
        return; // Nom inchange.
    }

    // 3. Chaine asynchrone :
    //    a. projectExists(newName) -> refuser si existe
    //    b. confirmation utilisateur
    //    c. readRawYaml(oldName)
    //    d. replaceProjectName(content, newName)
    //    e. writeRawYaml(newName, content)
    //    f. removeProject(oldName)
    //    g. loadProjects + message succes
    _projectRepository->projectExists(newName)
        .then(this, [this, oldName, newName](bool exists) -> QFuture<QString> {
            if (exists) {
                QMessageBox::warning(
                    this,
                    tr("Edit Project"),
                    tr("A project with this name already exists.")
                    );
                return QFuture<QString>();  // chaine cassee
            }

            // Confirmation avant action destructive.
            const auto answer = QMessageBox::question(
                this,
                tr("Edit Project"),
                tr("The project and its YAML file will be renamed "
                   "from \"%1\" to \"%2\".\n\nDo you want to continue?")
                    .arg(oldName, newName),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No
                );
            if (answer != QMessageBox::Yes) {
                return QFuture<QString>();  // chaine cassee
            }

            // Lire le YAML de l'ancien nom.
            return _projectRepository->readRawYaml(oldName);
        })
        .unwrap()
        .then(this, [this, newName](QString content) -> QFuture<bool> {
            if (!replaceProjectName(content, newName)) {
                throw std::runtime_error(
                    "Unable to update the project name in the YAML file.");
            }
            // Ecrire sous le nouveau nom.
            return _projectRepository->writeRawYaml(newName, content);
        })
        .unwrap()
        .then(this, [this, oldName](bool success) -> QFuture<bool> {
            if (!success) {
                return QFuture<bool>();  // chaine cassee
            }
            // Supprimer l'ancien fichier.
            return _projectRepository->removeProject(oldName);
        })
        .unwrap()
        .then(this, [this](bool removed) {
            // Si la suppression de l'ancien echoue, on signale mais on
            // continue : le nouveau projet existe (duplication, jamais
            // perte de donnees). Cette branche n'est atteinte qu'en cas
            // de succes complet (failure -> onFailed).
            if (!removed) {
                return;
            }
            loadProjects();
            QMessageBox::information(
                this,
                tr("Edit Project"),
                tr("Project renamed successfully.")
                );
        })
        .onFailed(this, [this](const std::exception& e) {
            QMessageBox::critical(
                this,
                tr("Edit Project"),
                tr("Failed to rename project: %1")
                    .arg(QString::fromUtf8(e.what()))
                );
        });
}

void MainWindow::on_actionEdit_Entity_triggered()
{
    // 1. Choisir le projet.
    QString projectName;
    if (!promptSelectProject(tr("Edit Entity"), projectName)) {
        return;
    }

    // 2. Retrouver le projet dans le modele.
    const sea::domain::Project* project = nullptr;
    const QModelIndex noParent;
    const int projectCount = _projectModel->rowCount(noParent);
    for (int row = 0; row < projectCount; ++row) {
        const sea::domain::Project* candidate = _projectModel->projectAt(row);
        if (candidate != nullptr &&
            QString::fromStdString(candidate->name) == projectName) {
            project = candidate;
            break;
        }
    }
    if (project == nullptr) {
        QMessageBox::critical(
            this,
            tr("Edit Entity"),
            tr("The selected project could not be found.")
            );
        return;
    }

    // 3. Choisir le service.
    QString serviceName;
    if (!promptSelectService(*project, tr("Edit Entity"), serviceName)) {
        return;
    }
    const sea::domain::Service* service = project->find_service(
        serviceName.toStdString()
        );
    if (service == nullptr) {
        QMessageBox::critical(
            this,
            tr("Edit Entity"),
            tr("The selected service could not be found.")
            );
        return;
    }

    // 4. Choisir l'entite.
    QString entityName;
    if (!promptSelectEntity(*service, tr("Edit Entity"), entityName)) {
        return;
    }
    const sea::domain::Entity* entity = service->find_entity(
        entityName.toStdString()
        );
    if (entity == nullptr) {
        QMessageBox::critical(
            this,
            tr("Edit Entity"),
            tr("The selected entity could not be found.")
            );
        return;
    }

    // 5. Dialogue d'edition des options, pre-rempli depuis l'entite.
    bool enableCrud = entity->options.enable_crud;
    bool timestamps = entity->options.timestamps;
    bool softDelete = entity->options.soft_delete;
    {
        QDialog optionsDialog(this);
        optionsDialog.setWindowTitle(tr("Edit Entity - %1").arg(entityName));

        auto* layout = new QFormLayout(&optionsDialog);

        auto* crudCheck = new QCheckBox(&optionsDialog);
        crudCheck->setChecked(enableCrud);
        auto* timestampsCheck = new QCheckBox(&optionsDialog);
        timestampsCheck->setChecked(timestamps);
        auto* softDeleteCheck = new QCheckBox(&optionsDialog);
        softDeleteCheck->setChecked(softDelete);

        layout->addRow(tr("Enable CRUD:"), crudCheck);
        layout->addRow(tr("Timestamps:"), timestampsCheck);
        layout->addRow(tr("Soft delete:"), softDeleteCheck);

        auto* buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
            &optionsDialog
            );
        layout->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &optionsDialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &optionsDialog, &QDialog::reject);

        if (optionsDialog.exec() != QDialog::Accepted) {
            return;
        }

        enableCrud = crudCheck->isChecked();
        timestamps = timestampsCheck->isChecked();
        softDelete = softDeleteCheck->isChecked();
    }

    // 6. Capturer le port maintenant (avant la chaine asynchrone qui peut
    //    invalider le pointeur service apres rechargement).
    const int servicePort = static_cast<int>(service->port);

    // 7. Chaine asynchrone : read -> modify -> write -> reload -> ask restart.
    _projectRepository->readRawYaml(projectName)
        .then(this, [this, projectName, serviceName, entityName,
                     enableCrud, timestamps, softDelete](QString content) {
            if (!replaceEntityOptions(content, serviceName, entityName,
                                      enableCrud, timestamps, softDelete)) {
                throw std::runtime_error(
                    "Unable to update the entity in the YAML file.");
            }
            return _projectRepository->writeRawYaml(projectName, content);
        })
        .unwrap()
        .then(this, [this, projectName, serviceName, servicePort](bool success) {
            if (!success) {
                return;
            }

            loadProjects();

            const auto applyAnswer = QMessageBox::question(
                this,
                tr("Edit Entity"),
                tr("Entity options updated in the YAML file.\n\n"
                   "Do you want to apply the changes to the database now?\n"
                   "This will restart the service."),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No
                );

            if (applyAnswer == QMessageBox::Yes && servicePort > 0) {
                const QString yamlPath = yamlPathForProject(projectName);
                stopServiceProcess(projectName, serviceName, servicePort);
                startServiceProcess(projectName, serviceName, servicePort, yamlPath);
            }

            QMessageBox::information(
                this,
                tr("Edit Entity"),
                tr("Entity updated successfully.")
                );
        })
        .onFailed(this, [this](const std::exception& e) {
            QMessageBox::critical(
                this,
                tr("Edit Entity"),
                tr("Failed to edit entity: %1")
                    .arg(QString::fromUtf8(e.what()))
                );
        });
}

void MainWindow::on_actionShow_All_Services_Logs_triggered()
{
    // En mode Remote, cet item est desactive via le mecanisme du
    // menu (cf. updateAuditsMenuState). Ce code n'est donc execute
    // qu'en mode Local. Garde-fou en cas d'oubli.
    if (_isRemoteMode) {
        return;
    }

    const QVector<ServiceLogInfo> entries = collectAllServiceLogs();

    if (entries.isEmpty()) {
        QMessageBox::information(
            this,
            tr("Logs"),
            tr("No service is currently available.")
            );
        return;
    }

    openLogsWindow(entries, tr("All Services Logs"));
}

void MainWindow::on_actionChoose_a_service_to_show_Logs_triggered()
{
    // Mode Remote : ouvre directement le RemoteLogsViewer pour le
    // seul service auquel SeaUI est connecte (le profil actif).
    if (_isRemoteMode) {
        auto* viewer = new RemoteLogsViewer(_activeProfile.baseUrl,
                                            _token,
                                            this);
        viewer->setAttribute(Qt::WA_DeleteOnClose);
        viewer->show();
        return;
    }
    const QVector<ServiceLogInfo> entries = collectAllServiceLogs();

    if (entries.isEmpty()) {
        QMessageBox::information(
            this,
            tr("Logs"),
            tr("No service is currently available.")
            );
        return;
    }

    // Dialogue de selection : liste "projet / service / port".
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Choose a service"));
    dialog.resize(420, 320);

    auto* layout = new QVBoxLayout(&dialog);
    auto* list   = new QListWidget(&dialog);
    layout->addWidget(list);

    for (const ServiceLogInfo& entry : entries) {
        QString label = QString("%1 / %2 / %3")
        .arg(entry.projectName)
            .arg(entry.serviceName)
            .arg(entry.port);
        if (!entry.logExists) {
            label += tr("  (no log yet)");
        }
        list->addItem(label);
    }
    list->setCurrentRow(0);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
        &dialog
        );
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    // Double-clic sur un service = validation directe.
    connect(list, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const int selectedRow = list->currentRow();
    if (selectedRow < 0 || selectedRow >= entries.size()) {
        return;
    }

    // Ouvrir la fenetre de logs sur le seul service choisi.
    openLogsWindow({entries.at(selectedRow)}, tr("Service Log"));
}

/**
 * @brief Applique une action a tous les services de tous les projets.
 */
void MainWindow::forEachService(
    const std::function<void(const QString& projectName,
                             const QString& serviceName,
                             int port,
                             const QString& yamlPath)>& action)
{
    const QModelIndex noParent;
    const int projectCount = _projectModel->rowCount(noParent);

    for (int row = 0; row < projectCount; ++row) {
        const sea::domain::Project* project = _projectModel->projectAt(row);
        if (project == nullptr) {
            continue;
        }

        const QString projectName = QString::fromStdString(project->name);
        const QString yamlPath    = yamlPathForProject(projectName);

        for (const sea::domain::Service& service : project->services) {
            action(
                projectName,
                QString::fromStdString(service.name),
                static_cast<int>(service.port),
                yamlPath
                );
        }
    }
}

void MainWindow::on_actionStart_All_Services_triggered()
{
    forEachService([this](const QString& projectName,
                          const QString& serviceName,
                          int port,
                          const QString& yamlPath) {
        startServiceProcess(projectName, serviceName, port, yamlPath);
    });

    QMessageBox::information(
        this,
        tr("Services Actions"),
        tr("All services have been started.")
        );
}

void MainWindow::on_actionStop_All_Services_triggered()
{
    forEachService([this](const QString& projectName,
                          const QString& serviceName,
                          int port,
                          const QString& /*yamlPath*/) {
        stopServiceProcess(projectName, serviceName, port);
    });

    QMessageBox::information(
        this,
        tr("Services Actions"),
        tr("All services have been stopped.")
        );
}

void MainWindow::on_actionRestart_All_Services_triggered()
{
    forEachService([this](const QString& projectName,
                          const QString& serviceName,
                          int port,
                          const QString& yamlPath) {
        stopServiceProcess(projectName, serviceName, port);
        startServiceProcess(projectName, serviceName, port, yamlPath);
    });

    QMessageBox::information(
        this,
        tr("Services Actions"),
        tr("All services have been restarted.")
        );
}

void MainWindow::on_actionReload_All_Services_triggered()
{
    // Reload : arret puis redemarrage. Le YAML etant relu a chaque
    // demarrage du backend (argument --config), un redemarrage prend
    // automatiquement en compte les modifications du fichier.
    forEachService([this](const QString& projectName,
                          const QString& serviceName,
                          int port,
                          const QString& yamlPath) {
        stopServiceProcess(projectName, serviceName, port);
        startServiceProcess(projectName, serviceName, port, yamlPath);
    });

    QMessageBox::information(
        this,
        tr("Services Actions"),
        tr("All services have been reloaded from their YAML configuration.")
        );
}
void MainWindow::on_actionSwitch_Connection_triggered()
{
    // 1. Ouvrir le dialog de connexion.
    ConnectionDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) {
        return;  // L'utilisateur a annule, on garde la connexion actuelle.
    }

    // 2. Construire le nouveau repository selon le profil choisi.
    std::unique_ptr<IProjectRepository> newRepo;
    const Profile& profile = dlg.activeProfile();
    if (profile.type == Profile::Type::Local) {
        newRepo = std::make_unique<LocalProjectRepository>(appConfigsDir());
    } else {
        newRepo = std::make_unique<HttpProjectRepository>(
            profile.baseUrl, dlg.token());
    }

    // 3. Remplacer le repository actif et mettre a jour les membres
    //    de profil pour que tous les helpers (Start/Stop, Restart,
    //    Logs) connaissent le nouveau mode.
    _projectRepository = std::move(newRepo);
    _activeProfile = profile;
    _token = dlg.token();
    _isRemoteMode = (profile.type == Profile::Type::Remote);
    updateServicesActionsMenuState();
    updateAuditsMenuState();

    // 4. Reinitialiser les selections : les projets ne sont plus les
    //    memes, les indices courants ne sont plus valides.
    _currentProjectRow = -1;
    _currentServiceRow = -1;
    _currentEntityRow  = -1;

    // 5. Vider les modeles pour eviter d'afficher des donnees du
    //    profil precedent pendant que la nouvelle liste se charge.
    _projectModel->setProjects({});
    _serviceModel->setServices({});
    _entityModel->setEntities({});
    _fieldModel->setFields({});
    _routeModel->setRoutes({});

    // 6. Recharger la liste des projets via le nouveau repository.
    loadProjects();
}

QString MainWindow::yamlPathForProject(const QString &projectName) const
{
    return appConfigsDir() + "/" + projectName + ".yaml";
}
/**
 * @brief Met a jour l'etat (enabled/disabled) du menu Services Actions
 *        en fonction du mode courant (Local ou Remote).
 *
 * En mode Remote, le menu entier est desactive : les actions
 * "Start/Stop/Restart/Reload All Services" agissent sur tous les
 * services LOCAUX via QProcess, ce qui n'a aucun sens quand SeaUI
 * est connecte a un backend distant (qui ne gere qu'un seul service
 * par profil v1.0).
 *
 * Appelee au constructeur et apres chaque Switch Connection.
 */
void MainWindow::updateServicesActionsMenuState()
{
    ui->menuServcies_Actions->setEnabled(!_isRemoteMode);
}
/**
 * @brief Met a jour l'etat (enabled/disabled) du menu Audits selon
 *        le mode courant (Local ou Remote).
 *
 * En mode Remote, "Show All Services Logs" est desactive (SeaUI
 * n'est connecte qu'a un seul service via le profil actif).
 * "Choose a service to show Logs" reste actif et ouvre directement
 * le RemoteLogsViewer.
 */
void MainWindow::updateAuditsMenuState()
{
    ui->actionShow_All_Services_Logs->setEnabled(!_isRemoteMode);
    // Choose a service reste actif en remote.
}