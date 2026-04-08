#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "load_project_use_case.h"
#include "../../libs/infrastructure/yaml/yaml_project_repository.h"

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
namespace {

/**
 * @brief Retourne le nom d'une entité avec première lettre en minuscule.
 *
 * Exemple : `Department` -> `department`.
 *
 * @param value Nom d'entité.
 * @return QString Nom transformé.
 */
QString lowerFirst(QString value)
{
    if (!value.isEmpty()) {
        value[0] = value[0].toLower();
    }
    return value;
}

/**
 * @brief Retourne le chemin pluriel attendu pour une entité.
 *
 * Exemple : `Department` -> `/departments`.
 *
 * @param entityName Nom d'entité.
 * @return QString Chemin pluriel.
 */
QString pluralEntityPath(const QString& entityName)
{
    return "/" + lowerFirst(entityName) + "s";
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

    const QString entityLower = lowerFirst(entityName);
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

} // namespace

/**
 * @brief Construit la fenêtre principale et initialise tous les modèles et signaux.
 *
 * @param parent Parent Qt.
 */
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , _projectModel(new ProjectListModel(this))
    , _serviceModel(new ServiceListModel(this))
    , _entityModel(new EntityListModel(this))
    , _fieldModel(new FieldListModel(this))
    , _routeModel(new RouteListModel(this))
    ,_networkManager(new QNetworkAccessManager(this))
{
    ui->setupUi(this);
    showMaximized();
    setWindowTitle("SeaDesktop");

    ui->projectListView->setModel(_projectModel);
    ui->serviceListView->setModel(_serviceModel);
    ui->entityListView->setModel(_entityModel);
    ui->fieldListView->setModel(_fieldModel);
    ui->routeListView->setModel(_routeModel);

    ui->startServiceButton->setEnabled(false);
    ui->stopServiceButton->setEnabled(false);
    ui->restartServiceButton->setEnabled(false);
    ui->swaggerServiceButton->setEnabled(false);

    ui->serviceLogoutButton->setEnabled(false);
    ui->serviceAuthStatusLabel->setText("Disconnected");
    ui->serviceAuthStatusLabel->setStyleSheet("color: #e74c3c; font-weight: bold;");

    updateAuthUi();
    connect(ui->projectListView, &QListView::clicked,
            this, &MainWindow::on_projectListView_clicked);
    connect(ui->serviceListView, &QListView::clicked,
            this, &MainWindow::on_serviceListView_clicked);
    connect(ui->entityListView, &QListView::clicked,
            this, &MainWindow::on_entityListView_clicked);
    connect(ui->fieldListView, &QListView::clicked,
            this, &MainWindow::on_fieldListView_clicked);

    loadProjects();

    _watcher = new QFileSystemWatcher(this);
    _watcher->addPath("/home/frederic/QtProjects/SeaDesktop/configs/");

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
                ui->startServiceButton->setEnabled(!running);
                ui->stopServiceButton->setEnabled(running);
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
                ui->startServiceButton->setEnabled(true);
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
        if (_currentServiceRow < 0) {
            return;
        }

        const auto& service = _serviceModel->serviceAt(_currentServiceRow);
        const QString logPath = "/home/frederic/QtProjects/SeaDesktop/logs/"
                                + QString::fromStdString(service->name) + ".log";
        QDesktopServices::openUrl(QUrl::fromLocalFile(logPath));
    });

}

/**
 * @brief Détruit la fenêtre principale.
 */
MainWindow::~MainWindow()
{
    delete ui;
}

/**
 * @brief Recharge tous les projets YAML depuis le dossier de configuration.
 *
 * La sélection courante est restaurée si possible.
 */
void MainWindow::loadProjects()
{
    try {
        sea::infrastructure::yaml::yaml_project_repository repo;
        sea::application::LoadProjectUseCase useCase(repo);

        auto projects = useCase.execute("/home/frederic/QtProjects/SeaDesktop/configs/");
        _projectModel->setProjects(projects);

        if (_currentProjectRow >= 0 && _currentProjectRow < _projectModel->rowCount({})) {
            const auto projectIndex = _projectModel->index(_currentProjectRow);
            ui->projectListView->setCurrentIndex(projectIndex);
            on_projectListView_clicked(projectIndex);

            if (_currentServiceRow >= 0 && _currentServiceRow < _serviceModel->rowCount({})) {
                const auto serviceIndex = _serviceModel->index(_currentServiceRow);
                ui->serviceListView->setCurrentIndex(serviceIndex);
                on_serviceListView_clicked(serviceIndex);

                if (_currentEntityRow >= 0 && _currentEntityRow < _entityModel->rowCount({})) {
                    const auto entityIndex = _entityModel->index(_currentEntityRow);
                    ui->entityListView->setCurrentIndex(entityIndex);
                    on_entityListView_clicked(entityIndex);
                }
            }
        }
    } catch (const std::exception& e) {
        qWarning() << "Erreur chargement projets:" << e.what();
    }
}

/**
 * @brief Démarre le processus backend correspondant au service sélectionné.
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

    const QString projectName = QString::fromStdString(project->name);
    const QString processKey = serviceProcessKey(projectName, serviceName, static_cast<int>(service->port));

    if (_processes.contains(processKey) &&
        _processes[processKey] != nullptr &&
        _processes[processKey]->state() != QProcess::NotRunning) {
        return;
    }

    const QString logsDir = "/home/frederic/QtProjects/SeaDesktop/logs/";
    QDir().mkpath(logsDir);

    const QString logPath = logsDir + processKey + ".log";
    const QString backendPath = "../Backend_Seastar/backend_seastar";

    auto* process = new QProcess(this);

    connect(process, &QProcess::readyReadStandardOutput, this, [process, processKey]() {
        qDebug().noquote() << "[" + processKey + "][OUT]"
                           << QString::fromLocal8Bit(process->readAllStandardOutput());
    });

    connect(process, &QProcess::readyReadStandardError, this, [process, processKey]() {
        qDebug().noquote() << "[" + processKey + "][ERR]"
                           << QString::fromLocal8Bit(process->readAllStandardError());
    });

    process->setStandardOutputFile(logPath, QIODevice::Append);
    process->setStandardErrorFile(logPath, QIODevice::Append);

    QStringList args;
    args << "--smp" << "1"
         << "--config" << yamlPath
         << "--service_name" << serviceName;

    process->start(backendPath, args);
    _processes[processKey] = process;
}

/**
 * @brief Arrête le processus backend correspondant au service sélectionné.
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

    const QString projectName = QString::fromStdString(project->name);
    const QString processKey = serviceProcessKey(projectName, serviceName, static_cast<int>(service->port));

    if (!_processes.contains(processKey)) {
        return;
    }

    auto* process = _processes[processKey];
    if (!process) {
        _processes.remove(processKey);
        return;
    }

    if (process->state() != QProcess::NotRunning) {
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
 * @brief Redémarre le service demandé.
 *
 * @param serviceName Nom du service.
 * @param yamlPath Chemin du YAML du projet.
 */
void MainWindow::restartService(const QString& serviceName, const QString& yamlPath)
{
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
    _currentServiceRoutes = routeGenerator.generate(service->schema);

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
    dialog->setWindowTitle("Swagger");
    dialog->resize(1200, 800);

    auto* layout = new QVBoxLayout(dialog);
    auto* view = new QWebEngineView(dialog);
    auto* closeButton = new QPushButton("Retour", dialog);

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
    dialog->setWindowTitle("Connexion");
    dialog->resize(360, 140);

    auto* layout = new QFormLayout(dialog);
    auto* emailEdit = new QLineEdit(dialog);
    auto* passwordEdit = new QLineEdit(dialog);
    passwordEdit->setEchoMode(QLineEdit::Password);

    layout->addRow("Email :", emailEdit);
    layout->addRow("Mot de passe :", passwordEdit);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
        dialog
        );
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, [this, dialog, emailEdit, passwordEdit]() {
        const QString email = emailEdit->text().trimmed();
        const QString password = passwordEdit->text();

        if (email.isEmpty() || password.isEmpty()) {
            QMessageBox::warning(this, "Connexion", "Email et mot de passe requis.");
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
        QMessageBox::warning(this, "Connexion", "Aucun service sélectionné.");
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
                "Connexion",
                "Échec du login.\nURL: " + url + "\nErreur: " + reply->errorString()
                );
            return;
        }

        const QByteArray payload = reply->readAll();
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);

        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            QMessageBox::critical(this, "Connexion", "Réponse JSON invalide.");
            return;
        }

        const QJsonObject obj = doc.object();

        if (!obj.contains("token") || !obj.value("token").isString()) {
            QMessageBox::critical(this, "Connexion", "Aucun token JWT reçu.");
            return;
        }

        _authToken = obj.value("token").toString();
        qDebug() << "JWT =" << _authToken;

        updateAuthUi();
        QMessageBox::information(this, "Connexion", "Connexion réussie.");
    });
}

void MainWindow::logoutUser()
{
    _authToken.clear();
    updateAuthUi();
    QMessageBox::information(this, "Logout", "Déconnecté.");
}

void MainWindow::updateAuthUi()
{
    const bool connected = !_authToken.isEmpty();

    ui->serviceLoginButton->setEnabled(!connected);
    ui->serviceLogoutButton->setEnabled(connected);

    ui->serviceAuthStatusLabel->setText(connected ? "Connected" : "Disconnected");
    ui->serviceAuthStatusLabel->setStyleSheet(
        connected
            ? "color: #2ecc71; font-weight: bold;"
            : "color: #e74c3c; font-weight: bold;"
        );
}


/**
 * @brief Affiche un tableau JSON dans une boîte de dialogue Qt.
 *
 * Chaque objet du tableau devient une ligne, et l’union de toutes les clés
 * devient les colonnes.
 *
 * @param array Tableau JSON retourné par l'API.
 * @param title Titre de la fenêtre.
 */
void MainWindow::showJsonArrayInTable(const QJsonArray& array, const QString& title)
{
    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(title);
    dialog->resize(1000, 600);

    auto* layout = new QVBoxLayout(dialog);
    auto* table = new QTableWidget(dialog);

    layout->addWidget(table);
    dialog->setLayout(layout);

    if (array.isEmpty()) {
        table->setRowCount(0);
        table->setColumnCount(1);
        table->setHorizontalHeaderLabels(QStringList() << "Info");
        table->setRowCount(1);
        table->setItem(0, 0, new QTableWidgetItem("Aucune donnée."));
        table->horizontalHeader()->setStretchLastSection(true);
        dialog->show();
        return;
    }

    QStringList headers;
    for (const auto& value : array) {
        if (!value.isObject()) {
            continue;
        }

        const auto obj = value.toObject();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            if (!headers.contains(it.key())) {
                headers.append(it.key());
            }
        }
    }

    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->setRowCount(array.size());

    for (int row = 0; row < array.size(); ++row) {
        const auto value = array[row];
        if (!value.isObject()) {
            table->setItem(row, 0, new QTableWidgetItem(QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact))));
            continue;
        }

        const auto obj = value.toObject();

        for (int col = 0; col < headers.size(); ++col) {
            const QString key = headers[col];
            QString cellText;

            if (obj.contains(key)) {
                const auto jsonValue = obj.value(key);

                if (jsonValue.isString()) {
                    cellText = jsonValue.toString();
                } else if (jsonValue.isDouble()) {
                    cellText = QString::number(jsonValue.toDouble());
                } else if (jsonValue.isBool()) {
                    cellText = jsonValue.toBool() ? "true" : "false";
                } else if (jsonValue.isNull()) {
                    cellText = "null";
                } else if (jsonValue.isArray()) {
                    cellText = QString::fromUtf8(QJsonDocument(jsonValue.toArray()).toJson(QJsonDocument::Compact));
                } else if (jsonValue.isObject()) {
                    cellText = QString::fromUtf8(QJsonDocument(jsonValue.toObject()).toJson(QJsonDocument::Compact));
                }
            }

            table->setItem(row, col, new QTableWidgetItem(cellText));
        }
    }

    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);

    dialog->show();
}
void MainWindow::on_openEntityDataButton_clicked()
{
    if (_currentServiceRow < 0) {
        QMessageBox::warning(this, "Open Data", "Aucun service sélectionné.");
        return;
    }

    if (_currentEntityRow < 0) {
        QMessageBox::warning(this, "Open Data", "Aucune entité sélectionnée.");
        return;
    }

    const auto& service = _serviceModel->serviceAt(_currentServiceRow);
    const auto& entity = _entityModel->entityAt(_currentEntityRow);

    const QString entityName = QString::fromStdString(entity->name);
    const QString path = entityCollectionPath(entityName);
    const QString url = QString("http://127.0.0.1:%1%2")
                            .arg(static_cast<int>(service->port))
                            .arg(path);

    QNetworkRequest request{QUrl(url)};

    if (!_authToken.isEmpty()) {
        request.setRawHeader("Authorization", "Bearer " + _authToken.toUtf8());
    }

    auto* reply = _networkManager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, entityName, url]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::critical(
                this,
                "Open Data",
                "Impossible de récupérer les données.\nURL: " + url + "\nErreur: " + reply->errorString()
                );
            return;
        }

        const QByteArray payload = reply->readAll();
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);

        if (parseError.error != QJsonParseError::NoError) {
            QMessageBox::critical(
                this,
                "Open Data",
                "Réponse JSON invalide.\nErreur: " + parseError.errorString()
                );
            return;
        }

        if (!doc.isArray()) {
            QMessageBox::warning(
                this,
                "Open Data",
                "La réponse n'est pas un tableau JSON."
                );
            return;
        }

        showJsonArrayInTable(doc.array(), "Données - " + entityName);
    });
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

