#ifndef SERVICESTATUSCHECK_H
#define SERVICESTATUSCHECK_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QTimer>
class ServiceStatusCheck : public QObject
{
    Q_OBJECT
public:
    explicit ServiceStatusCheck(QObject *parent = nullptr);
    void startPolling( QString url, int intervalMs = 3000);
    void selectService(const QString& serviceName, const QString& host, int port);
    void stopPolling();
    void cancelPendingRequest();
signals:
    void statusUpdated(const QString& service, const QString& status, int port);
    void serviceUnreachable(const QString& service);

private:
    QNetworkAccessManager* _nam;
    QTimer* _timer;
    QNetworkReply* _pendingReply = nullptr; // ← requête en cours
    QString _currentService;
    QString _currentHost;
    int _currentPort = 0;
    void fetchStatus();
};

#endif // SERVICESTATUSCHECK_H
