#include "remotelogsviewer.h"

#include <QFont>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

/**
 * Formate une entree de log JSON en une ligne texte selon le pattern :
 *   [timestamp] [level] [logger] message
 *
 * Le timestamp et les autres champs sont alignes pour faciliter la
 * lecture. Si un champ est absent, il est remplace par "-".
 */
QString formatLogEntry(const QJsonObject& entry)
{
    const QString timestamp =
        entry.value(QStringLiteral("timestamp")).toString(QStringLiteral("-"));
    const QString level =
        entry.value(QStringLiteral("level")).toString(QStringLiteral("-"));
    const QString logger =
        entry.value(QStringLiteral("logger")).toString(QStringLiteral("-"));
    const QString message =
        entry.value(QStringLiteral("message")).toString();

    return QStringLiteral("[%1] [%2] [%3] %4")
        .arg(timestamp,
             level.leftJustified(5),         // info=4, warn=4, error=5...
             logger.leftJustified(15),       // alignement visuel
             message);
}

} // namespace anonyme


RemoteLogsViewer::RemoteLogsViewer(const QString& baseUrl,
                                   const QString& token,
                                   QWidget* parent)
    : QDialog(parent)
    , _baseUrl(baseUrl)
    , _token(token)
    , _nam(new QNetworkAccessManager(this))
{
    setWindowTitle(tr("Remote Service Logs"));
    setMinimumSize(900, 600);

    // Nettoyer le baseUrl des slashes finaux.
    while (_baseUrl.endsWith('/')) {
        _baseUrl.chop(1);
    }

    // ── Zone de logs en monospace ──────────────────────────────
    _logsEdit = new QPlainTextEdit(this);
    _logsEdit->setReadOnly(true);
    _logsEdit->setLineWrapMode(QPlainTextEdit::NoWrap);

    // Font monospace pour aligner les colonnes.
    const QFont monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    _logsEdit->setFont(monoFont);

    // ── Status label (compteur entrees, derniere maj, erreurs) ─
    _statusLabel = new QLabel(tr("Loading..."), this);
    _statusLabel->setStyleSheet(QStringLiteral("color: gray;"));

    // ── Boutons ────────────────────────────────────────────────
    _refreshButton = new QPushButton(tr("Refresh"), this);
    _closeButton   = new QPushButton(tr("Close"), this);

    auto* buttonsLayout = new QHBoxLayout;
    buttonsLayout->addWidget(_statusLabel);
    buttonsLayout->addStretch();
    buttonsLayout->addWidget(_refreshButton);
    buttonsLayout->addWidget(_closeButton);

    // ── Layout global ──────────────────────────────────────────
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(_logsEdit, 1);
    mainLayout->addLayout(buttonsLayout);

    // ── Connexions ─────────────────────────────────────────────
    connect(_refreshButton, &QPushButton::clicked,
            this, &RemoteLogsViewer::onRefreshClicked);
    connect(_closeButton, &QPushButton::clicked,
            this, &QDialog::accept);

    // ── Chargement initial ─────────────────────────────────────
    fetchLogs();
}

void RemoteLogsViewer::onRefreshClicked()
{
    fetchLogs();
}

void RemoteLogsViewer::fetchLogs()
{
    _refreshButton->setEnabled(false);
    _statusLabel->setText(tr("Loading..."));

    const QUrl url(_baseUrl + QStringLiteral("/admin/logs"));
    QNetworkRequest request(url);
    request.setRawHeader("Authorization",
                         QByteArray("Bearer ") + _token.toUtf8());

    QNetworkReply* reply = _nam->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const int status = reply->attribute(
                                    QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        const QString networkError = reply->errorString();
        reply->deleteLater();

        _refreshButton->setEnabled(true);

        if (status == 0) {
            _logsEdit->setPlainText(tr("Network error: %1").arg(networkError));
            _statusLabel->setText(tr("Error"));
            return;
        }

        if (status == 401) {
            _logsEdit->setPlainText(
                tr("Authentication required. The token may have expired. "
                   "Please reconnect via Switch Connection."));
            _statusLabel->setText(tr("401 Unauthorized"));
            return;
        }

        if (status == 403) {
            _logsEdit->setPlainText(tr("Admin role required."));
            _statusLabel->setText(tr("403 Forbidden"));
            return;
        }

        if (status != 200) {
            _logsEdit->setPlainText(
                tr("Unexpected HTTP status: %1\n\n%2")
                    .arg(status)
                    .arg(QString::fromUtf8(body)));
            _statusLabel->setText(tr("HTTP %1").arg(status));
            return;
        }

        // Parsing du JSON.
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (!doc.isObject()) {
            _logsEdit->setPlainText(tr("Invalid response format."));
            _statusLabel->setText(tr("Parse error"));
            return;
        }

        const QJsonArray logsArray =
            doc.object().value(QStringLiteral("logs")).toArray();

        // Formatage de chaque entree.
        QString formatted;
        formatted.reserve(logsArray.size() * 120);
        for (const QJsonValue& v : logsArray) {
            formatted += formatLogEntry(v.toObject());
            formatted += QChar('\n');
        }

        _logsEdit->setPlainText(formatted);

        // Auto-scroll en bas pour voir les dernieres entrees.
        QTextCursor cursor = _logsEdit->textCursor();
        cursor.movePosition(QTextCursor::End);
        _logsEdit->setTextCursor(cursor);

        _statusLabel->setText(tr("%1 entries").arg(logsArray.size()));
    });
}