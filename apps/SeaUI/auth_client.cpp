#include "auth_client.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPromise>
#include <QUrl>

#include <exception>
#include <memory>
#include <stdexcept>

namespace {

/**
 * Cree un std::exception_ptr a partir d'une reponse HTTP en erreur.
 * Reproduit la logique de makeHttpException de httpprojectrepository.cpp
 * mais en autonome ici pour eviter une dependance.
 */
std::exception_ptr makeAuthException(int httpStatus,
                                     const QByteArray& body,
                                     const QString& networkError)
{
    QString message;

    if (httpStatus == 0) {
        message = QStringLiteral("Network error: %1").arg(networkError);
        return std::make_exception_ptr(
            std::runtime_error(message.toStdString()));
    }

    // Tenter de parser le JSON d'erreur du backend.
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (doc.isObject()) {
        const QJsonObject root = doc.object();
        const QJsonObject errorObj = root.value(QStringLiteral("error")).toObject();
        const QString serverMessage =
            errorObj.value(QStringLiteral("message")).toString();
        if (!serverMessage.isEmpty()) {
            message = serverMessage;
        }
    }

    if (message.isEmpty()) {
        switch (httpStatus) {
        case 400: message = QStringLiteral("Bad request"); break;
        case 401: message = QStringLiteral("Invalid credentials"); break;
        case 403: message = QStringLiteral("Access denied"); break;
        case 500: message = QStringLiteral("Server error"); break;
        default:
            message = QStringLiteral("HTTP error %1").arg(httpStatus);
            break;
        }
    }

    return std::make_exception_ptr(
        std::runtime_error(message.toStdString()));
}

/**
 * Retourne le code HTTP d'un QNetworkReply, ou 0 si la requete n'a
 * jamais atteint le serveur (erreur de transport).
 */
int httpStatusOf(QNetworkReply* reply)
{
    const QVariant statusVariant =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    return statusVariant.isValid() ? statusVariant.toInt() : 0;
}

/**
 * Construit l'URL de login a partir de la baseUrl, en gerant les
 * eventuels slashes finals.
 */
QUrl loginUrlFor(QString baseUrl)
{
    while (baseUrl.endsWith('/')) {
        baseUrl.chop(1);
    }
    return QUrl(baseUrl + QStringLiteral("/auth/login"));
}

} // namespace anonyme


QFuture<QString> AuthClient::login(const QString& baseUrl,
                                   const QString& email,
                                   const QString& password)
{
    auto promise = std::make_shared<QPromise<QString>>();
    promise->start();
    QFuture<QString> future = promise->future();

    // QNetworkAccessManager dedie a cette requete (pas de membre, le
    // login est ponctuel). On le rend self-deleting via deleteLater
    // dans le slot finished pour eviter une fuite.
    auto* nam = new QNetworkAccessManager;

    // Construction de la requete.
    QNetworkRequest request(loginUrlFor(baseUrl));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));

    // Construction du body JSON.
    QJsonObject bodyObj;
    bodyObj[QStringLiteral("email")]    = email;
    bodyObj[QStringLiteral("password")] = password;
    const QByteArray bodyBytes =
        QJsonDocument(bodyObj).toJson(QJsonDocument::Compact);

    // Envoi.
    QNetworkReply* reply = nam->post(request, bodyBytes);
    QObject::connect(reply, &QNetworkReply::finished, nam,
                     [promise, reply, nam]() {
                         const int status = httpStatusOf(reply);
                         const QByteArray body = reply->readAll();
                         const QString networkError = reply->errorString();
                         reply->deleteLater();
                         nam->deleteLater();

                         if (status != 200) {
                             promise->setException(
                                 makeAuthException(status, body, networkError));
                             promise->finish();
                             return;
                         }

                         // Parsing de la reponse pour extraire access_token.
                         const QJsonDocument doc = QJsonDocument::fromJson(body);
                         if (!doc.isObject()) {
                             promise->setException(std::make_exception_ptr(
                                 std::runtime_error("Invalid login response format")));
                             promise->finish();
                             return;
                         }
                         const QString token =
                             doc.object().value(QStringLiteral("access_token")).toString();

                         if (token.isEmpty()) {
                             promise->setException(std::make_exception_ptr(
                                 std::runtime_error(
                                     "Login response missing access_token. The server may "
                                     "be configured with token_delivery: cookie which is "
                                     "not supported by SeaUI.")));
                             promise->finish();
                             return;
                         }

                         promise->addResult(token);
                         promise->finish();
                     });

    return future;
}