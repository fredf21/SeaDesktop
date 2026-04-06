#include "routelistmodel.h"

RouteListModel::RouteListModel(QObject *parent)
    : QAbstractListModel{parent}
{}

void RouteListModel::setRoutes(std::vector<sea::application::RouteDefinition> routes)
{
    beginResetModel();
        _routes = std::move(routes);
    endResetModel();
}

void RouteListModel::clear()
{
    beginResetModel();
        _routes.clear();
    endResetModel();
}

int RouteListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return static_cast<int>(_routes.size());
}

QVariant RouteListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(_routes.size())) {
        return {};
    }

    const auto& route = _routes[index.row()];
    QString method;
    switch (route.method) {
    case sea::application::HttpMethod::Get:
        method = "GET";
        break;
    case sea::application::HttpMethod::Post:
        method = "POST";
        break;
    case sea::application::HttpMethod::Put:
        method = "PUT";
        break;
    case sea::application::HttpMethod::Delete:
        method = "DELETE";
        break;
    default:
        break;
    }
    if (role == Qt::DisplayRole) {
        return QString("%1 %2 [%3 / %4]").arg(method, QString::fromStdString(route.path),
                                              QString::fromStdString(route.entity_name),QString::fromStdString(route.operation_name));
    }
    return {};

}
