#ifndef ROUTELISTITEMDELEGATE_H
#define ROUTELISTITEMDELEGATE_H

#include <QStyledItemDelegate>

/**
 * @brief Delegate de rendu pour la QListView des routes.
 *
 * Affiche chaque route sous la forme :
 *
 *   [GET]  /users/page                 [User / list_page]   [\ud83d\udfe6 PAGE]
 *   |__|                                                     |__________|
 *  methode                                                     badge mode
 *
 * Le badge est rendu uniquement pour les routes paginees. Les couleurs :
 *   - page    : bleu
 *   - offset  : vert
 *   - cursor  : jaune
 *
 * Le delegate lit les roles personnalises de RouteListModel :
 *   - PaginationModeRole       (badge)
 *   - HttpMethodRole           (texte methode)
 *   - RoutePathRole            (texte path)
 *   - RouteEntityOperationRole (texte entity / operation)
 *
 * Si PaginationModeRole est vide, aucun badge n'est dessine.
 */
class RouteListItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit RouteListItemDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter,
               const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;

    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;
};

#endif // ROUTELISTITEMDELEGATE_H