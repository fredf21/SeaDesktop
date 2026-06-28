#pragma once

#include "entity.h"

#include <QDialog>
#include <QJsonArray>
#include <QString>

#include <optional>

class EntityDataTableModel;
class QLabel;
class QNetworkAccessManager;
class QNetworkReply;
class QPushButton;
class QTableView;
class QVBoxLayout;

/**
 * @brief Dialog d'affichage des donnees d'une entite avec lazy
 *        rendering et pagination conditionnelle.
 *
 * Quatre modes de fetch selon la config pagination de l'entite :
 *
 *   - Aucun (entity.pagination = nullopt)
 *     Fetch tout en une fois via /entities (route non paginee).
 *     Affiche un bandeau d'avertissement si plus de kWarningThreshold
 *     lignes recues.
 *
 *   - OFFSET (entity.pagination.has_offset())
 *     Fetch /entities/offset?offset=N&limit=M en infinite scroll.
 *     Total exact connu via le champ "total" de la reponse.
 *
 *   - CURSOR (entity.pagination.has_cursor())
 *     Fetch /entities/cursor?after=<token>&limit=M en infinite scroll.
 *     Total inconnu (par design du mode cursor).
 *
 *   - PAGE (entity.pagination.has_page())
 *     Fetch /entities/page?page=N&page_size=M en infinite scroll
 *     (concat des pages).
 *
 * Priorite quand plusieurs modes actifs : OFFSET > CURSOR > PAGE.
 */
class EntityDataDialog : public QDialog
{
    Q_OBJECT
public:
    /**
     * @param entity        L'entite dont on affiche les donnees.
     * @param baseUrl       URL de base du backend, ex "http://127.0.0.1:8080".
     * @param collectionPath Path HTTP de la collection, ex "/articles".
     * @param authToken     Token JWT (vide si pas d'auth).
     * @param parent        Parent Qt.
     */
    EntityDataDialog(const sea::domain::Entity& entity,
                     const QString& baseUrl,
                     const QString& collectionPath,
                     const QString& authToken,
                     QWidget* parent = nullptr);

private slots:
    /**
     * Appele a chaque changement de position du scrollbar vertical.
     * Si on arrive pres du bas, declenche fetchMore() si applicable.
     */
    void onVerticalScroll(int value);

private:
    /**
     * Mode de pagination actif pour cette session de visualisation.
     * Determine par la priorite OFFSET > CURSOR > PAGE.
     */
    enum class FetchMode {
        None,    // Pas de pagination YAML -> fetch tout en une fois
        Offset,
        Cursor,
        Page
    };

    /**
     * Determine le mode en fonction de entity.pagination.
     */
    [[nodiscard]] static FetchMode determineFetchMode(
        const sea::domain::Entity& entity);

    /**
     * Construit l'URL pour le batch suivant selon _fetchMode et l'etat
     * courant (_offset / _cursor / _page).
     */
    [[nodiscard]] QString buildNextFetchUrl() const;

    /**
     * Lance la prochaine requete (fetch initial ou batch suivant).
     * Si _isFetching est deja true, ne fait rien (evite les requetes
     * paralleles au scroll).
     */
    void fetchMore();

    /**
     * Callback de la requete HTTP : parse la reponse selon _fetchMode
     * et appelle le model->appendRows().
     */
    void onReplyFinished(QNetworkReply* reply);

    /**
     * Met a jour le bandeau d'info en haut (compte / mode / etc.).
     */
    void updateInfoBanner();

    // ── Donnees d'entree ────────────────────────────────────────
    const sea::domain::Entity& _entity;
    const QString              _baseUrl;
    const QString              _collectionPath;
    const QString              _authToken;

    // ── Etat de pagination ──────────────────────────────────────
    FetchMode                  _fetchMode    = FetchMode::None;
    int                        _batchSize    = 100;
    std::optional<QString>     _defaultSort;

    // Etat specifique aux modes paginated :
    int                        _offset       = 0;  // OFFSET
    std::optional<QString>     _nextCursor;        // CURSOR (vide initialement)
    int                        _page         = 1;  // PAGE (1-based)

    // Indique qu'aucun batch suivant n'existe (fin de la collection).
    bool                       _exhausted    = false;
    bool                       _isFetching   = false;

    // ── UI ──────────────────────────────────────────────────────
    QLabel*                    _infoLabel    = nullptr;
    QTableView*                _tableView    = nullptr;
    EntityDataTableModel*      _model        = nullptr;
    QLabel*                    _loadingLabel = nullptr;
    QNetworkAccessManager*     _netManager   = nullptr;

    // ── Seuils ──────────────────────────────────────────────────

    // Si plus de kWarningThreshold lignes dans le mode None,
    // affiche un avertissement dans le bandeau.
    static constexpr int kWarningThreshold = 1000;

    // Quand le scroll atteint ce % de la zone visible vers le bas,
    // on declenche fetchMore() (pour avoir les donnees avant que
    // l'utilisateur n'arrive vraiment au bout).
    static constexpr int kScrollTriggerPercent = 85;
};
