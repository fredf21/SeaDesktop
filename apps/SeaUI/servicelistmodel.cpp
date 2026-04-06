#include "servicelistmodel.h"

ServiceListModel::ServiceListModel(QObject *parent)
    : QAbstractListModel{parent}
{}

void ServiceListModel::setServices(std::vector<sea::domain::Service> services)
{
    beginResetModel();
        _services = std::move(services);
    endResetModel();
}

int ServiceListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return static_cast<int>(_services.size());
}

QVariant ServiceListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(_services.size())) {
        return {};
    }

    const auto& service = _services[index.row()];

    if (role == Qt::DisplayRole) {
        return QString::fromStdString(service.name);
    }

    return {};
}

const sea::domain::Service *ServiceListModel::serviceAt(int row) const
{
    if (row < 0 || row >= static_cast<int>(_services.size())) {
        return nullptr;
    }
    return &_services[row];
}

