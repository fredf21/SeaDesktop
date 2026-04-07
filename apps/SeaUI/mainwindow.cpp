#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "load_project_use_case.h"
#include "../../libs/infrastructure/yaml/yaml_project_repository.h"
#include <QDir>
#include <QDesktopServices>
#include <QUrl>
#include <QWebEngineView>
#include <QDialog>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , _projectModel(new ProjectListModel(this))
    , _serviceModel(new ServiceListModel(this))
    , _entityModel(new EntityListModel(this))
    , _fieldModel(new FieldListModel(this))
    , _routeModel(new RouteListModel(this))
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
    // Surveiller les changements de fichiers dans le dossier
    connect(_watcher, &QFileSystemWatcher::fileChanged,
            this, [this](const QString& path) {
                qDebug() << "Fichier modifié:" << path;
                loadProjects(); // recharge automatiquement
            });

    // Surveiller si un nouveau fichier yaml est ajouté/supprimé
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
            [this](const QString& service, const QString& status, int port) {
                bool running = (status == "RUNNING");
                ui->startServiceButton->setEnabled(!running);
                ui->stopServiceButton->setEnabled(running);
                ui->restartServiceButton->setEnabled(running);
                ui->swaggerServiceButton->setEnabled(running);
                ui->serviceStatusLabel->setStyleSheet(
                    status == "RUNNING"
                        ? "color: #2ecc71; font-weight: bold;"  // vert
                        : "color: #e74c3c; font-weight: bold;"  // rouge
                    );
            });
    connect(_statusCheck, &ServiceStatusCheck::serviceUnreachable, this,
            [this](const QString& service) {
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

        QString yamlPath = yamlPathForProject(QString::fromStdString(project->name));
        startService(QString::fromStdString(service->name), yamlPath);
    });


    connect(ui->stopServiceButton, &QPushButton::clicked, this, [this]() {
        if (_currentServiceRow < 0) return;
        const auto& service = _serviceModel->serviceAt(_currentServiceRow);
        stopService(QString::fromStdString(service->name));
    });

    connect(ui->restartServiceButton, &QPushButton::clicked, this, [this]() {
        if (_currentServiceRow < 0 || _currentProjectRow < 0) return;
        const auto& project = _projectModel->projectAt(_currentProjectRow);
        const auto& service = _serviceModel->serviceAt(_currentServiceRow);
        QString yamlPath = yamlPathForProject(QString::fromStdString(project->name));
        restartService(QString::fromStdString(service->name), yamlPath);
    });
    connect(ui->logsServiceButton, &QPushButton::clicked, this, [this]() {
        if (_currentServiceRow < 0) return;
        const auto& service = _serviceModel->serviceAt(_currentServiceRow);
        QString logPath = "/home/frederic/QtProjects/SeaDesktop/logs/"
                          + QString::fromStdString(service->name) + ".log";
        QDesktopServices::openUrl(QUrl::fromLocalFile(logPath));
    });
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::loadProjects()
{
    try {
        sea::infrastructure::yaml::yaml_project_repository repo;
        sea::application::LoadProjectUseCase useCase(repo);

        auto projects = useCase.execute("/home/frederic/QtProjects/SeaDesktop/configs/");
        _projectModel->setProjects(projects);
        // Restaurer la sélection
        if (_currentProjectRow >= 0 && _currentProjectRow < _projectModel->rowCount({})) {
            auto index = _projectModel->index(_currentProjectRow);
            ui->projectListView->setCurrentIndex(index);
            on_projectListView_clicked(index);

            if (_currentServiceRow >= 0 && _currentServiceRow < _serviceModel->rowCount({})) {
                auto sIndex = _serviceModel->index(_currentServiceRow);
                ui->serviceListView->setCurrentIndex(sIndex);
                on_serviceListView_clicked(sIndex);

                if (_currentEntityRow >= 0 && _currentEntityRow < _entityModel->rowCount({})) {
                    auto eIndex = _entityModel->index(_currentEntityRow);
                    ui->entityListView->setCurrentIndex(eIndex);
                    on_entityListView_clicked(eIndex);
                }
            }
        }
        //return true;

    } catch (const std::exception& e) {
        qWarning() << "Erreur chargement projets:" << e.what();
        //return false;
    }
}

void MainWindow::startService(const QString &serviceName, const QString &yamlPath)
{
    if (_currentProjectRow < 0 || _currentServiceRow < 0) {
        return;
    }

    const auto& project = _projectModel->projectAt(_currentProjectRow);
    const auto& service = _serviceModel->serviceAt(_currentServiceRow);

    const QString projectName = QString::fromStdString(project->name);
    const QString processKey =
        QString("%1:%2:%3").arg(projectName, serviceName).arg(service->port);

    if (_processes.contains(processKey) &&
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

void MainWindow::stopService(const QString &serviceName)
{
    if (_currentProjectRow < 0 || _currentServiceRow < 0) {
        return;
    }

    const auto& project = _projectModel->projectAt(_currentProjectRow);
    const auto& service = _serviceModel->serviceAt(_currentServiceRow);

    const QString projectName = QString::fromStdString(project->name);
    const QString processKey =
        serviceProcessKey(projectName, serviceName, static_cast<int>(service->port));

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

void MainWindow::restartService(const QString& serviceName, const QString& yamlPath)
{
    stopService(serviceName);
    startService(serviceName, yamlPath);
}

QString MainWindow::serviceProcessKey(const QString &projectName, const QString &serviceName, int port) const
{
    return QString("%1:%2:%3").arg(projectName, serviceName).arg(port);
}

void MainWindow::on_projectListView_clicked(const QModelIndex &index)
{
    _currentProjectRow = index.row();
    const auto& project = _projectModel->projectAt(_currentProjectRow);

    _serviceModel->setServices(project->services);
    _entityModel->setEntities({});
    _fieldModel->setFields({});
}


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
    ui->serviceDBTypeLabel->setText(QString::fromStdString(std::string(sea::domain::to_string(service->database_config.type))));
    QString serviceName = QString::fromStdString(service->name);
    QString host = "127.0.0.1";
    // Désactiver tous les boutons pendant le fetch
    ui->startServiceButton->setEnabled(false);
    ui->stopServiceButton->setEnabled(false);
    ui->restartServiceButton->setEnabled(false);
    _statusCheck->selectService(serviceName, "127.0.0.1", service->port);
    sea::application::RouteGenerator route_generator;
    _currentServiceRoutes = route_generator.generate(service->schema);
    _routeModel->clear();
    qDebug() << "selectService:" << serviceName << "port:" << QString::number(static_cast<int>(service->port));
}


void MainWindow::on_entityListView_clicked(const QModelIndex &index)
{
    _currentEntityRow = index.row();
    const auto& entity = _entityModel->entityAt(_currentEntityRow);
    _fieldModel->setFields(entity->fields);
    std::vector<sea::application::RouteDefinition> filteredRoutes;
    for(const auto& route : _currentServiceRoutes){
        if(route.entity_name == entity->name){
            filteredRoutes.push_back(route);
        }
    }
    // Juste avant le setRoutes, ligne ~116
    qDebug() << "entity name:" << QString::fromStdString(entity->name);
    qDebug() << "routes total:" << _currentServiceRoutes.size();
    qDebug() << "filtered count:" << filteredRoutes.size();
    for (const auto& r : _currentServiceRoutes)
        qDebug() << "  route entity_name:" << QString::fromStdString(r.entity_name);
    _routeModel->setRoutes(std::move(filteredRoutes));
}


void MainWindow::on_fieldListView_clicked(const QModelIndex &index)
{

}

void MainWindow::onStatusUpdated(const QString &service, const QString &status, int port)
{
    // sécurité : on vérifie que c'est bien le service sélectionné
    QModelIndex current = ui->serviceListView->currentIndex();
    if (!current.isValid()) return;
    if (current.data().toString() != service) return;

    // status label
    ui->serviceStatusLabel->setText(status);
    ui->serviceStatusLabel->setStyleSheet(
        status == "RUNNING"
            ? "color: #2ecc71; font-weight: bold;"  // vert
            : "color: #e74c3c; font-weight: bold;"  // rouge
        );
}

void MainWindow::onServiceUnreachable(const QString &service)
{
    QModelIndex current = ui->serviceListView->currentIndex();
    if (!current.isValid()) return;
    if (current.data().toString() != service) return;

    ui->serviceStatusLabel->setText("STOPPED");
    ui->serviceStatusLabel->setStyleSheet(
        "color: #e74c3c; font-weight: bold;"
        );
    //ui->servicePortLabel->setText("- - - -");
}


void MainWindow::on_swaggerServiceButton_clicked()
{
    if (_currentServiceRow < 0) return;
    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("Swagger");
    dialog->resize(1200, 800);

    auto* layout = new QVBoxLayout(dialog);
    auto* view = new QWebEngineView(dialog);
    auto* closeButton = new QPushButton("Retour", dialog);

    const auto& project = _projectModel->projectAt(_currentProjectRow);
    const auto& service = _serviceModel->serviceAt(_currentServiceRow);
    QString url = QString("http://localhost:%1/docs").arg(service->port);
    view->setUrl(QUrl(url));
    layout->addWidget(view);
    layout->addWidget(closeButton);

    QObject::connect(closeButton, &QPushButton::clicked, dialog, &QDialog::close);

    dialog->setLayout(layout);
    dialog->show();
}

