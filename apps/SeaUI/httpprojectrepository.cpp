#include "httpprojectrepository.h"

#include "yaml/yaml_schema_parser.h"

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPromise>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>
#include <QFuture>

#include <exception>
#include <filesystem>
#include <stdexcept>
#include <utility>

namespace {

/**
 * Construit une exception std::runtime_error a partir du code HTTP
 * et de l'eventuel message d'erreur du serveur.
 *
 * Le backend SeaDesktop retourne typiquement un JSON d'erreur :
 *   {"success":false,"error":{"code":"VALIDATION_ERROR","message":"..."}}
 * On extrait error.message si present.
 */
std::exception_ptr makeHttpException(int httpStatus,
                                     const QByteArray& body,
                                     const QString& networkError)
{
    QString message;

    // 1. Erreur de transport (timeout, host unreachable, etc.)
    if (httpStatus == 0) {
        message = QStringLiteral("Network error: %1").arg(networkError);
        return std::make_exception_ptr(
            std::runtime_error(message.toStdString()));
    }

    // 2. Tenter de parser le JSON d'erreur du serveur.
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (doc.isObject()) {
        const QJsonObject root = doc.object();
        const QJsonObject errorObj = root.value("error").toObject();
        const QString serverMessage = errorObj.value("message").toString();
        if (!serverMessage.isEmpty()) {
            message = serverMessage;
        }
    }

    // 3. Sinon message generique selon code HTTP.
    if (message.isEmpty()) {
        switch (httpStatus) {
        case 400: message = QStringLiteral("Bad request"); break;
        case 401: message = QStringLiteral("Authentication required"); break;
        case 403: message = QStringLiteral("Admin role required"); break;
        case 404: message = QStringLiteral("Project not found"); break;
        case 409: message = QStringLiteral("Project already exists"); break;
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
 * Cree un QFuture<T> deja resolu portant l'exception passee. Utilise
 * pour propager les erreurs synchrones via le mecanisme QFuture.
 */
template <typename T>
QFuture<T> makeFailedFuture(std::exception_ptr ex)
{
    QPromise<T> promise;
    promise.start();
    promise.setException(ex);
    promise.finish();
    return promise.future();
}

} // namespace anonyme


HttpProjectRepository::HttpProjectRepository(QString baseUrl,
                                             QString token,
                                             QObject* parent)
    : _baseUrl(std::move(baseUrl))
    , _token(std::move(token))
    , _nam(new QNetworkAccessManager(parent))
{
    // Retirer un eventuel slash final pour eviter les doubles slashes
    // dans les URLs construites.
    while (_baseUrl.endsWith('/')) {
        _baseUrl.chop(1);
    }
}

QUrl HttpProjectRepository::projectUrl(const QString& projectName) const
{
    return QUrl(QStringLiteral("%1/admin/projects/%2.yaml")
                    .arg(_baseUrl, projectName));
}

QUrl HttpProjectRepository::listUrl() const
{
    return QUrl(QStringLiteral("%1/admin/projects").arg(_baseUrl));
}

// ─────────────────────────────────────────────────────────────────
// listProjects
// ─────────────────────────────────────────────────────────────────
QFuture<IProjectRepository::ListResult> HttpProjectRepository::listProjects()
{
    auto promise = std::make_shared<QPromise<ListResult>>();
    promise->start();
    QFuture<ListResult> future = promise->future();

    // 1. GET /admin/projects pour obtenir la liste.
    QNetworkRequest request(listUrl());
    request.setRawHeader("Authorization",
                         QByteArray("Bearer ") + _token.toUtf8());

    QNetworkReply* reply = _nam->get(request);
    QObject::connect(reply, &QNetworkReply::finished, _nam,
                     [this, promise, reply]() {
                         const int status = httpStatusOf(reply);
                         const QByteArray body = reply->readAll();
                         const QString networkError = reply->errorString();
                         reply->deleteLater();

                         if (status != 200) {
                             promise->setException(makeHttpException(status, body, networkError));
                             promise->finish();
                             return;
                         }

                         // 2. Parser la liste JSON pour recuperer les noms de fichiers.
                         const QJsonDocument doc = QJsonDocument::fromJson(body);
                         if (!doc.isObject()) {
                             promise->setException(std::make_exception_ptr(
                                 std::runtime_error("Invalid response format from server")));
                             promise->finish();
                             return;
                         }
                         const QJsonArray projectsArray = doc.object().value("projects").toArray();

                         // 3. Pour chaque projet, faire un GET pour recuperer le YAML brut
                         //    et le parser. On utilise un compteur partage : la promise est
                         //    resolue quand toutes les requetes individuelles sont finies.
                         auto result = std::make_shared<ListResult>();
                         auto remaining = std::make_shared<int>(projectsArray.size());

                         if (*remaining == 0) {
                             // Aucun projet a charger, on resoud immediatement.
                             promise->addResult(*result);
                             promise->finish();
                             return;
                         }

                         for (const QJsonValue& projectValue : projectsArray) {
                             const QJsonObject projectObj = projectValue.toObject();
                             const QString file = projectObj.value("file").toString();

                             QNetworkRequest projectRequest(
                                 QUrl(QStringLiteral("%1/admin/projects/%2")
                                          .arg(_baseUrl, file)));
                             projectRequest.setRawHeader(
                                 "Authorization", QByteArray("Bearer ") + _token.toUtf8());

                             QNetworkReply* projectReply = _nam->get(projectRequest);
                             QObject::connect(projectReply, &QNetworkReply::finished, _nam,
                                              [promise, result, remaining, projectReply, file]() {
                                                  const int s = httpStatusOf(projectReply);
                                                  const QByteArray yamlBody = projectReply->readAll();
                                                  projectReply->deleteLater();

                                                  if (s == 200) {
                                                      // Ecriture du contenu dans un fichier temporaire pour le
                                                      // parser (YamlSchemaParser parse depuis un fichier).
                                                      QTemporaryDir tmpDir;
                                                      if (tmpDir.isValid()) {
                                                          const std::string tmpPath =
                                                              (std::filesystem::path(tmpDir.path().toStdString())
                                                               / file.toStdString()).string();
                                                          {
                                                              QFile tmpFile(QString::fromStdString(tmpPath));
                                                              if (tmpFile.open(QIODevice::WriteOnly)) {
                                                                  tmpFile.write(yamlBody);
                                                                  tmpFile.close();
                                                              }
                                                          }
                                                          try {
                                                              sea::infrastructure::yaml::YamlSchemaParser parser;
                                                              result->projects.push_back(
                                                                  parser.parse_project_file(tmpPath));
                                                          } catch (const std::exception& e) {
                                                              result->errors.append(
                                                                  QStringLiteral("%1: %2").arg(
                                                                      file, QString::fromUtf8(e.what())));
                                                          }
                                                      }
                                                  } else {
                                                      result->errors.append(
                                                          QStringLiteral("%1: HTTP %2").arg(file).arg(s));
                                                  }

                                                  --(*remaining);
                                                  if (*remaining == 0) {
                                                      promise->addResult(*result);
                                                      promise->finish();
                                                  }
                                              });
                         }
                     });

    return future;
}

// ─────────────────────────────────────────────────────────────────
// readRawYaml
// ─────────────────────────────────────────────────────────────────
QFuture<QString> HttpProjectRepository::readRawYaml(const QString& projectName)
{
    auto promise = std::make_shared<QPromise<QString>>();
    promise->start();
    QFuture<QString> future = promise->future();

    QNetworkRequest request(projectUrl(projectName));
    request.setRawHeader("Authorization",
                         QByteArray("Bearer ") + _token.toUtf8());

    QNetworkReply* reply = _nam->get(request);
    QObject::connect(reply, &QNetworkReply::finished, _nam,
                     [promise, reply]() {
                         const int status = httpStatusOf(reply);
                         const QByteArray body = reply->readAll();
                         const QString networkError = reply->errorString();
                         reply->deleteLater();

                         if (status != 200) {
                             promise->setException(makeHttpException(status, body, networkError));
                         } else {
                             promise->addResult(QString::fromUtf8(body));
                         }
                         promise->finish();
                     });

    return future;
}

// ─────────────────────────────────────────────────────────────────
// writeRawYaml
// ─────────────────────────────────────────────────────────────────
QFuture<bool> HttpProjectRepository::writeRawYaml(const QString& projectName,
                                                  const QString& content)
{
    auto promise = std::make_shared<QPromise<bool>>();
    promise->start();
    QFuture<bool> future = promise->future();

    // 1. Determiner si le projet existe pour choisir PUT (existe) ou
    //    POST (nouveau).
    QNetworkRequest listRequest(listUrl());
    listRequest.setRawHeader("Authorization",
                             QByteArray("Bearer ") + _token.toUtf8());

    QNetworkReply* listReply = _nam->get(listRequest);
    QObject::connect(listReply, &QNetworkReply::finished, _nam,
                     [this, promise, listReply, projectName, content]() {
                         const int status = httpStatusOf(listReply);
                         const QByteArray body = listReply->readAll();
                         const QString networkError = listReply->errorString();
                         listReply->deleteLater();

                         if (status != 200) {
                             promise->setException(makeHttpException(status, body, networkError));
                             promise->finish();
                             return;
                         }

                         const QJsonDocument doc = QJsonDocument::fromJson(body);
                         const QJsonArray projectsArray =
                             doc.object().value("projects").toArray();

                         bool exists = false;
                         for (const QJsonValue& v : projectsArray) {
                             if (v.toObject().value("name").toString() == projectName) {
                                 exists = true;
                                 break;
                             }
                         }

                         // 2. Selon l'existence, on fait PUT ou POST.
                         QNetworkRequest writeRequest(projectUrl(projectName));
                         writeRequest.setRawHeader("Authorization",
                                                   QByteArray("Bearer ") + _token.toUtf8());
                         writeRequest.setHeader(QNetworkRequest::ContentTypeHeader,
                                                QStringLiteral("application/x-yaml"));

                         const QByteArray contentBytes = content.toUtf8();
                         QNetworkReply* writeReply = exists
                                                         ? _nam->put(writeRequest, contentBytes)
                                                         : _nam->post(writeRequest, contentBytes);

                         QObject::connect(writeReply, &QNetworkReply::finished, _nam,
                                          [promise, writeReply]() {
                                              const int s = httpStatusOf(writeReply);
                                              const QByteArray b = writeReply->readAll();
                                              const QString netErr = writeReply->errorString();
                                              writeReply->deleteLater();

                                              // 200 (PUT succes) ou 201 (POST cree).
                                              if (s != 200 && s != 201) {
                                                  promise->setException(makeHttpException(s, b, netErr));
                                              } else {
                                                  promise->addResult(true);
                                              }
                                              promise->finish();
                                          });
                     });

    return future;
}

// ─────────────────────────────────────────────────────────────────
// projectExists
// ─────────────────────────────────────────────────────────────────
QFuture<bool> HttpProjectRepository::projectExists(const QString& projectName)
{
    auto promise = std::make_shared<QPromise<bool>>();
    promise->start();
    QFuture<bool> future = promise->future();

    QNetworkRequest request(listUrl());
    request.setRawHeader("Authorization",
                         QByteArray("Bearer ") + _token.toUtf8());

    QNetworkReply* reply = _nam->get(request);
    QObject::connect(reply, &QNetworkReply::finished, _nam,
                     [promise, reply, projectName]() {
                         const int status = httpStatusOf(reply);
                         const QByteArray body = reply->readAll();
                         const QString networkError = reply->errorString();
                         reply->deleteLater();

                         if (status != 200) {
                             promise->setException(makeHttpException(status, body, networkError));
                             promise->finish();
                             return;
                         }

                         const QJsonDocument doc = QJsonDocument::fromJson(body);
                         const QJsonArray projectsArray =
                             doc.object().value("projects").toArray();

                         bool found = false;
                         for (const QJsonValue& v : projectsArray) {
                             if (v.toObject().value("name").toString() == projectName) {
                                 found = true;
                                 break;
                             }
                         }
                         promise->addResult(found);
                         promise->finish();
                     });

    return future;
}

// ─────────────────────────────────────────────────────────────────
// removeProject
// ─────────────────────────────────────────────────────────────────
QFuture<bool> HttpProjectRepository::removeProject(const QString& projectName)
{
    auto promise = std::make_shared<QPromise<bool>>();
    promise->start();
    QFuture<bool> future = promise->future();

    QNetworkRequest request(projectUrl(projectName));
    request.setRawHeader("Authorization",
                         QByteArray("Bearer ") + _token.toUtf8());

    QNetworkReply* reply = _nam->deleteResource(request);
    QObject::connect(reply, &QNetworkReply::finished, _nam,
                     [promise, reply]() {
                         const int status = httpStatusOf(reply);
                         const QByteArray body = reply->readAll();
                         const QString networkError = reply->errorString();
                         reply->deleteLater();

                         if (status != 200) {
                             promise->setException(makeHttpException(status, body, networkError));
                         } else {
                             promise->addResult(true);
                         }
                         promise->finish();
                     });

    return future;
}