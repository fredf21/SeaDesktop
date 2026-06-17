#pragma once

#include <QDialog>
#include <QString>

class QPlainTextEdit;
class QPushButton;
class QLabel;
class QNetworkAccessManager;

/**
 * @brief Dialog de visualisation des logs d'un backend distant.
 *
 * Appelle GET <baseUrl>/admin/logs sur le backend distant et affiche
 * les entrees retournees dans un QPlainTextEdit read-only. Chaque
 * entree est formatee selon le pattern :
 *
 *   [timestamp] [level] [logger] message
 *
 * Le bouton Refresh recharge les logs (utile pour voir les nouvelles
 * entrees apres une action sur le backend). Le bouton Close ferme
 * le dialog.
 *
 * Utilise par MainWindow en mode Remote a la place de l'ouverture du
 * fichier log local (qui n'existe pas en mode Remote, le service
 * tourne sur Docker distant).
 *
 * Note v1.0 : pas de polling automatique, pas de filtres level/logger,
 * pas de recherche. Version simple pour le sprint v1.0. Ameliorations
 * prevues en v1.1.
 */
class RemoteLogsViewer : public QDialog
{
    Q_OBJECT
public:
    /**
     * @param baseUrl URL de base du backend, ex: "http://localhost:8080".
     *                Pas de slash final (sera nettoye si present).
     * @param token   JWT admin valide.
     * @param parent  QObject parent.
     */
    RemoteLogsViewer(const QString& baseUrl,
                     const QString& token,
                     QWidget* parent = nullptr);

private slots:
    void onRefreshClicked();

private:
    /**
     * Lance la requete HTTP GET /admin/logs et remplit le viewer
     * avec le contenu recu. En cas d'erreur, affiche un message
     * a la place des logs.
     */
    void fetchLogs();

    QString                _baseUrl;
    QString                _token;
    QNetworkAccessManager* _nam;

    QPlainTextEdit*        _logsEdit;
    QPushButton*           _refreshButton;
    QPushButton*           _closeButton;
    QLabel*                _statusLabel;
};