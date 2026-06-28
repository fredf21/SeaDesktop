#include "entitydatadialog.h"

#include "entitydatatablemodel.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QScrollBar>
#include <QTableView>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

EntityDataDialog::EntityDataDialog(const sea::domain::Entity& entity,
                                   const QString& baseUrl,
                                   const QString& collectionPath,
                                   const QString& authToken,
                                   QWidget* parent)
    : QDialog(parent),
    _entity(entity),
    _baseUrl(baseUrl),
    _collectionPath(collectionPath),
    _authToken(authToken),
    _fetchMode(determineFetchMode(entity))
{
    setWindowTitle(tr("Data - %1").arg(QString::fromStdString(entity.name)));
    resize(1100, 650);
    setAttribute(Qt::WA_DeleteOnClose);

    // ── Determination de la taille de batch et du tri par defaut ──
    // selon le mode actif. On respecte le default_limit / default_page_size
    // du YAML quand disponible, sinon fallback a 100.
    if (entity.pagination.has_value()) {
        const auto& p = *entity.pagination;
        if (_fetchMode == FetchMode::Offset && p.has_offset()) {
            _batchSize = static_cast<int>(p.offset->default_limit);
            _defaultSort = p.offset->default_sort
                               ? std::optional<QString>(QString::fromStdString(
                                     *p.offset->default_sort))
                               : std::nullopt;
        } else if (_fetchMode == FetchMode::Cursor && p.has_cursor()) {
            _batchSize = static_cast<int>(p.cursor->default_limit);
        } else if (_fetchMode == FetchMode::Page && p.has_page()) {
            _batchSize = static_cast<int>(p.page->default_page_size);
            _defaultSort = p.page->default_sort
                               ? std::optional<QString>(QString::fromStdString(
                                     *p.page->default_sort))
                               : std::nullopt;
        }
    }
    if (_batchSize <= 0) _batchSize = 100;

    // ── UI ──────────────────────────────────────────────────────
    auto* mainLayout = new QVBoxLayout(this);

    _infoLabel = new QLabel(this);
    _infoLabel->setWordWrap(true);
    _infoLabel->setStyleSheet(
        QStringLiteral("padding: 6px; background-color: #f0f4f8; "
                       "border: 1px solid #d0d7de; border-radius: 4px;"));
    mainLayout->addWidget(_infoLabel);

    _tableView = new QTableView(this);
    _tableView->setAlternatingRowColors(true);
    _tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    _tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // Toutes les colonnes s'etirent pour remplir la largeur du dialog,
    // mais l'utilisateur peut redimensionner individuellement (Interactive).
    _tableView->horizontalHeader()->setSectionResizeMode(
        QHeaderView::Interactive);
    _tableView->horizontalHeader()->setStretchLastSection(true);
    _tableView->verticalHeader()->setVisible(true);
    mainLayout->addWidget(_tableView, 1);

    _loadingLabel = new QLabel(this);
    _loadingLabel->setText(tr("Loading..."));
    _loadingLabel->setAlignment(Qt::AlignCenter);
    _loadingLabel->setStyleSheet(QStringLiteral("color: #57606a; padding: 4px;"));
    _loadingLabel->setVisible(false);
    mainLayout->addWidget(_loadingLabel);

    auto* closeButton = new QPushButton(tr("Close"), this);
    auto* buttonsLayout = new QHBoxLayout;
    buttonsLayout->addStretch();
    buttonsLayout->addWidget(closeButton);
    mainLayout->addLayout(buttonsLayout);

    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

    _model = new EntityDataTableModel(this);
    _tableView->setModel(_model);

    _netManager = new QNetworkAccessManager(this);

    connect(_tableView->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &EntityDataDialog::onVerticalScroll);

    updateInfoBanner();

    // ── Premier fetch ───────────────────────────────────────────
    fetchMore();
}

EntityDataDialog::FetchMode
EntityDataDialog::determineFetchMode(const sea::domain::Entity& entity)
{
    if (!entity.pagination.has_value()) {
        return FetchMode::None;
    }
    const auto& p = *entity.pagination;
    // Priorite : OFFSET > CURSOR > PAGE.
    if (p.has_offset()) return FetchMode::Offset;
    if (p.has_cursor()) return FetchMode::Cursor;
    if (p.has_page())   return FetchMode::Page;
    // Pagination presente mais aucun sous-mode actif : traite comme None.
    return FetchMode::None;
}

QString EntityDataDialog::buildNextFetchUrl() const
{
    QString path;
    QUrlQuery query;

    switch (_fetchMode) {
    case FetchMode::None:
        // Route non paginee.
        path = _collectionPath;
        break;

    case FetchMode::Offset:
        path = _collectionPath + "/offset";
        query.addQueryItem(QStringLiteral("offset"), QString::number(_offset));
        query.addQueryItem(QStringLiteral("limit"),  QString::number(_batchSize));
        if (_defaultSort.has_value()) {
            query.addQueryItem(QStringLiteral("sort"), *_defaultSort);
        }
        break;

    case FetchMode::Cursor:
        path = _collectionPath + "/cursor";
        query.addQueryItem(QStringLiteral("limit"), QString::number(_batchSize));
        if (_nextCursor.has_value() && !_nextCursor->isEmpty()) {
            query.addQueryItem(QStringLiteral("after"), *_nextCursor);
        }
        break;

    case FetchMode::Page:
        path = _collectionPath + "/page";
        query.addQueryItem(QStringLiteral("page"),
                           QString::number(_page));
        query.addQueryItem(QStringLiteral("page_size"),
                           QString::number(_batchSize));
        if (_defaultSort.has_value()) {
            query.addQueryItem(QStringLiteral("sort"), *_defaultSort);
        }
        break;
    }

    QUrl url(_baseUrl + path);
    if (!query.isEmpty()) {
        url.setQuery(query);
    }
    return url.toString();
}

void EntityDataDialog::fetchMore()
{
    if (_isFetching || _exhausted) {
        return;
    }

    _isFetching = true;
    _loadingLabel->setVisible(true);

    const QString url = buildNextFetchUrl();
    QNetworkRequest req{QUrl(url)};
    if (!_authToken.isEmpty()) {
        req.setRawHeader("Authorization", "Bearer " + _authToken.toUtf8());
    }

    auto* reply = _netManager->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onReplyFinished(reply);
    });
}

void EntityDataDialog::onReplyFinished(QNetworkReply* reply)
{
    reply->deleteLater();
    _isFetching = false;
    _loadingLabel->setVisible(false);

    if (reply->error() != QNetworkReply::NoError) {
        QMessageBox::critical(this, tr("Open Data"),
                              tr("Unable to retrieve data.\nURL: %1\nError: %2")
                                  .arg(reply->url().toString(), reply->errorString()));
        return;
    }

    const QByteArray payload = reply->readAll();
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        QMessageBox::critical(this, tr("Open Data"),
                              tr("Invalid JSON response.\nError: %1").arg(parseError.errorString()));
        return;
    }

    QJsonArray items;
    int batchTotal = -1;
    std::optional<QString> nextCursor;

    if (_fetchMode == FetchMode::None) {
        // Route non paginee : reponse = QJsonArray brut.
        if (!doc.isArray()) {
            QMessageBox::warning(this, tr("Open Data"),
                                 tr("The response is not a JSON array."));
            return;
        }
        items = doc.array();
        // Tout est recu en une fois : on epuise.
        _exhausted = true;
    } else {
        // Routes paginees : reponse = objet enveloppe { items, ... }.
        if (!doc.isObject()) {
            QMessageBox::warning(this, tr("Open Data"),
                                 tr("Paginated response is not a JSON object."));
            return;
        }
        const QJsonObject env = doc.object();
        if (!env.contains(QStringLiteral("items"))
            || !env.value(QStringLiteral("items")).isArray()) {
            QMessageBox::warning(this, tr("Open Data"),
                                 tr("Paginated response missing 'items' array."));
            return;
        }
        items = env.value(QStringLiteral("items")).toArray();

        if (_fetchMode == FetchMode::Offset || _fetchMode == FetchMode::Page) {
            if (env.contains(QStringLiteral("total"))) {
                batchTotal = env.value(QStringLiteral("total")).toInt();
            }
        }
        if (_fetchMode == FetchMode::Cursor) {
            if (env.contains(QStringLiteral("next_cursor"))) {
                nextCursor = env.value(QStringLiteral("next_cursor")).toString();
            }
        }
    }

    // Append au model.
    _model->appendRows(items);

    // Mise a jour de l'etat selon le mode.
    switch (_fetchMode) {
    case FetchMode::None:
        // Rien a faire : _exhausted deja a true.
        break;

    case FetchMode::Offset:
        _offset += items.size();
        if (batchTotal >= 0) {
            _model->setKnownTotal(batchTotal);
            if (_model->rowCount() >= batchTotal) {
                _exhausted = true;
            }
        }
        if (items.size() < _batchSize) {
            _exhausted = true;
        }
        break;

    case FetchMode::Cursor:
        if (nextCursor.has_value() && !nextCursor->isEmpty()) {
            _nextCursor = nextCursor;
        } else {
            _exhausted = true;
        }
        if (items.size() < _batchSize) {
            _exhausted = true;
        }
        break;

    case FetchMode::Page:
        _page += 1;
        if (batchTotal >= 0) {
            _model->setKnownTotal(batchTotal);
            if (_model->rowCount() >= batchTotal) {
                _exhausted = true;
            }
        }
        if (items.size() < _batchSize) {
            _exhausted = true;
        }
        break;
    }

    updateInfoBanner();

    // Ajuste les largeurs de colonne uniquement apres le premier
    // batch (sinon Qt re-calculerait a chaque fetch).
    if (_model->rowCount() == items.size()) {
        _tableView->resizeColumnsToContents();
        // Cap chaque colonne a 300px pour eviter qu'une colonne JSON
        // imbrique prenne tout l'ecran.
        for (int c = 0; c < _model->columnCount(); ++c) {
            if (_tableView->columnWidth(c) > 300) {
                _tableView->setColumnWidth(c, 300);
            }
        }

        // Calcule l'espace disponible : si toutes les colonnes ensemble
        // sont moins larges que le viewport, on agrandit chaque colonne
        // proportionnellement pour remplir l'espace.
        const int viewportWidth = _tableView->viewport()->width();
        int totalCols = 0;
        for (int c = 0; c < _model->columnCount(); ++c) {
            totalCols += _tableView->columnWidth(c);
        }
        if (totalCols > 0 && totalCols < viewportWidth
            && _model->columnCount() > 0) {
            const int extraPerColumn =
                (viewportWidth - totalCols) / _model->columnCount();
            for (int c = 0; c < _model->columnCount(); ++c) {
                _tableView->setColumnWidth(c,
                                           _tableView->columnWidth(c) + extraPerColumn);
            }
        }
    }
}

void EntityDataDialog::onVerticalScroll(int value)
{
    if (_exhausted || _isFetching || _fetchMode == FetchMode::None) {
        return;
    }

    auto* bar = _tableView->verticalScrollBar();
    if (bar == nullptr) {
        return;
    }

    const int maximum = bar->maximum();
    if (maximum <= 0) {
        return;
    }

    // Si on a passe kScrollTriggerPercent% de la zone scrollable,
    // declencher le fetch suivant. Comme ca les donnees arrivent avant
    // que l'utilisateur n'atteigne le vrai bas.
    const int triggerValue = (maximum * kScrollTriggerPercent) / 100;
    if (value >= triggerValue) {
        fetchMore();
    }
}

void EntityDataDialog::updateInfoBanner()
{
    QString text;
    const int currentRows = _model->rowCount();
    const int knownTotal  = _model->knownTotal();

    switch (_fetchMode) {
    case FetchMode::None:
        if (currentRows >= kWarningThreshold) {
            text = tr("Displaying %1 rows (all loaded). "
                      "For better performance with large tables, "
                      "enable pagination in your YAML "
                      "(offset, cursor, or page mode).")
                       .arg(currentRows);
            _infoLabel->setStyleSheet(
                QStringLiteral("padding: 6px; background-color: #fff8c5; "
                               "border: 1px solid #d4a72c; border-radius: 4px;"));
        } else {
            text = tr("Displaying %1 rows (all loaded).").arg(currentRows);
        }
        break;

    case FetchMode::Offset:
        if (knownTotal >= 0) {
            text = tr("OFFSET pagination - showing %1 of %2 rows "
                      "(batch size: %3).")
                       .arg(currentRows).arg(knownTotal).arg(_batchSize);
        } else {
            text = tr("OFFSET pagination - showing %1 rows "
                      "(batch size: %2).").arg(currentRows).arg(_batchSize);
        }
        break;

    case FetchMode::Cursor:
        text = tr("CURSOR pagination - showing %1 rows "
                  "(batch size: %2, total unknown by design).")
                   .arg(currentRows).arg(_batchSize);
        break;

    case FetchMode::Page:
        if (knownTotal >= 0) {
            text = tr("PAGE pagination - showing %1 of %2 rows "
                      "(page size: %3).")
                       .arg(currentRows).arg(knownTotal).arg(_batchSize);
        } else {
            text = tr("PAGE pagination - showing %1 rows "
                      "(page size: %2).").arg(currentRows).arg(_batchSize);
        }
        break;
    }

    if (_exhausted && _fetchMode != FetchMode::None) {
        text += QStringLiteral(" ");
        text += tr("End of collection.");
    }

    _infoLabel->setText(text);
}