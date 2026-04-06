#ifndef ROUTELISTMODEL_H
#define ROUTELISTMODEL_H

#include <QAbstractListModel>
#include <QObject>
#include "route_generator.h"

class RouteListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    explicit RouteListModel(QObject *parent = nullptr);
    void setRoutes(std::vector<sea::application::RouteDefinition> routes);
    void clear();

    // QAbstractItemModel interface
    int rowCount(const QModelIndex &parent) const;
    QVariant data(const QModelIndex &index, int role) const;
private:
    std::vector<sea::application::RouteDefinition> _routes;

};

#endif // ROUTELISTMODEL_H
