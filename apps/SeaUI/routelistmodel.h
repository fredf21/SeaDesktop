#ifndef ROUTELISTMODEL_H
#define ROUTELISTMODEL_H

#include <QAbstractListModel>
#include <QObject>
#include "route_generator.h"

class RouteListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    /**
     * Roles personnalises exposes en plus de Qt::DisplayRole.
     *
     * Utilises par RouteListItemDelegate pour dessiner les badges
     * de pagination (PAGE / OFFSET / CURSOR).
     */
    enum CustomRoles {
        // Mode de pagination de la route (QString) :
        //   ""        -> route non paginee
        //   "page"    -> badge \ud83d\udfe6 PAGE
        //   "offset"  -> badge \ud83d\udfe9 OFFSET
        //   "cursor"  -> badge \ud83d\udfe8 CURSOR
        PaginationModeRole = Qt::UserRole + 1,

        // Texte HTTP de la methode (GET / POST / PUT / DELETE)
        HttpMethodRole = Qt::UserRole + 2,

        // Texte du path
        RoutePathRole = Qt::UserRole + 3,

        // Texte "<entity> / <operation>" (sans crochets, pour le delegate)
        RouteEntityOperationRole = Qt::UserRole + 4,
    };

    explicit RouteListModel(QObject *parent = nullptr);

    /**
     * Remplit le model avec un set de routes.
     *
     * Le tri est applique automatiquement :
     * - regroupement par entite (path commun)
     * - dans chaque groupe, ordre : list -> list_page -> list_offset -> list_cursor
     *   (pareil pour list_by_fk, list_by_fk_field, list_many_to_many,
     *   get_with_children)
     */
    void setRoutes(std::vector<sea::application::RouteDefinition> routes);

    void clear();

    // QAbstractItemModel interface
    int rowCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;

    /**
     * Override pour exposer les noms des roles personnalises a QML/Qt Test.
     * Optionnel pour QWidgets mais bonne pratique.
     */
    QHash<int, QByteArray> roleNames() const override;

private:
    std::vector<sea::application::RouteDefinition> _routes;

    /**
     * Determine le mode de pagination d'une route a partir de
     * son operation_name. Retourne "" si pas paginee.
     */
    static QString paginationModeOf(const std::string& operation_name);

    /**
     * Tri stable des routes :
     * - cle primaire   : entity_name
     * - cle secondaire : "rang" de l'operation (list=0, list_page=1, ...)
     * - cle tertiaire  : path (pour stabilite)
     */
    static void sortRoutesGrouped(std::vector<sea::application::RouteDefinition>& routes);
};

#endif // ROUTELISTMODEL_H
