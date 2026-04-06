#include "servicestatuscheck.h"
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>

ServiceStatusCheck::ServiceStatusCheck(QObject *parent)
    : QObject{parent}
     ,_nam(new QNetworkAccessManager(this))
     ,_timer(new QTimer(this))
{
        connect(_timer, &QTimer::timeout, this, &ServiceStatusCheck::fetchStatus);
}
void ServiceStatusCheck::fetchStatus() {
    if (_pendingReply) return; // sécurité : déjà une requête en cours

    QString url = QString("http://%1:%2/health")
                      .arg(_currentHost)
                      .arg(_currentPort);

    _pendingReply = _nam->get(QNetworkRequest(QUrl(url)));

    // capture le nom du service au moment de la requête
    QString serviceAtRequest = _currentService;

    connect(_pendingReply, &QNetworkReply::finished,
            this, [this, serviceAtRequest]() {
                auto* reply = _pendingReply;
                _pendingReply = nullptr;
                reply->deleteLater();

                // réponse d'un ancien service → on ignore
                if (serviceAtRequest != _currentService) return;

                if (reply->error() != QNetworkReply::NoError) {
                    if (reply->error() != QNetworkReply::OperationCanceledError) {
                        emit serviceUnreachable(_currentService);
                    }
                    return;
                }

                auto doc = QJsonDocument::fromJson(reply->readAll());
                auto obj = doc.object();
                emit statusUpdated(
                    _currentService,
                    obj["status"].toString(),
                    obj["port"].toInt()
                    );
            });
}
void ServiceStatusCheck::startPolling(QString url, int intervalMs) {
    fetchStatus(); // immediate first call
    _timer->start(intervalMs);
}

void ServiceStatusCheck::selectService(const QString &serviceName, const QString &host, int port)
{
    cancelPendingRequest(); // ← annule l'ancienne requête immédiatement
    _timer->stop();

    _currentService = serviceName;
    _currentHost    = host;
    _currentPort    = port;

    fetchStatus();              // fetch immédiat pour le nouveau service
    _timer->start(3000);
}

void ServiceStatusCheck::stopPolling()
{
    cancelPendingRequest();
    _timer->stop();
}

void ServiceStatusCheck::cancelPendingRequest()
{
    if (_pendingReply && _pendingReply->isRunning()) {
        _pendingReply->abort(); // ← coupe la requête réseau
        // deleteLater() sera appelé dans le slot finished
    }
    _pendingReply = nullptr;
}