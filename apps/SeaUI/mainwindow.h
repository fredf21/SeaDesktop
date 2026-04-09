#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "entitylistmodel.h"
#include "servicelistmodel.h"
#include "projectlistmodel.h"
#include "fieldlistmodel.h"
#include "routelistmodel.h"
#include <QFileSystemWatcher>
#include "servicestatuscheck.h"
#include <QProcess>
#include <QNetworkReply>
#include <QNetworkRequest>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    void loadProjects();
    void startService(const QString& serviceName, const QString& yamlPath);
    void stopService(const QString& serviceName);
    void restartService(const QString& serviceName, const QString& yamlPath);
    QString serviceProcessKey(const QString& projectName,
                              const QString& serviceName,
                              int port) const;
private slots:
    void on_projectListView_clicked(const QModelIndex &index);

    void on_serviceListView_clicked(const QModelIndex &index);

    void on_entityListView_clicked(const QModelIndex &index);

    void on_fieldListView_clicked(const QModelIndex &index);
    // ← nouveaux slots pour le ServiceClient
    void onStatusUpdated(const QString& service, const QString& status, int port);
    void onServiceUnreachable(const QString& service);
    void on_swaggerServiceButton_clicked();

    void on_openEntityDataButton_clicked();

    void on_serviceLoginButton_clicked();

    void on_serviceLogoutButton_clicked();

    void on_actionAdd_New_Project_triggered();

private:
    Ui::MainWindow *ui;
    ProjectListModel* _projectModel;
    ServiceListModel* _serviceModel;
    EntityListModel* _entityModel;
    FieldListModel* _fieldModel;
    RouteListModel* _routeModel;
    QFileSystemWatcher* _watcher;
    ServiceStatusCheck*    _statusCheck;      // ← nouveau
    std::vector<sea::application::RouteDefinition> _currentServiceRoutes;
    int _currentProjectRow = -1;
    int _currentServiceRow = -1;
    int _currentEntityRow = -1;
    QMap<QString, QProcess*> _processes; // serviceName → process
    const QString _configsPath = "/home/frederic/QtProjects/SeaDesktop/configs/";

    QString yamlPathForProject(const QString& projectName) const {
        return _configsPath + projectName + ".yaml";
    }
    QNetworkAccessManager* _networkManager = nullptr;
    void showJsonArrayInTable(const QJsonArray& array, const QString& title);
    QString entityCollectionPath(const QString& entityName) const;
    QString _authToken;
    void promptLogin();
    void loginUser(const QString& email, const QString& password);
    void logoutUser();
    void updateAuthUi();
};
#endif // MAINWINDOW_H
