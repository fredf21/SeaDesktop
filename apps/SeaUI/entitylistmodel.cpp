#include "entitylistmodel.h"

EntityListModel::EntityListModel(QObject *parent)
    : QAbstractListModel{parent}
{}

void EntityListModel::setEntities(std::vector<sea::domain::Entity> entities)
{
    beginResetModel();
        _entities = std::move(entities);
    endResetModel();
}

const sea::domain::Entity *EntityListModel::entityAt(int row) const
{
    if (row < 0 || row >= static_cast<int>(_entities.size())) {
        return nullptr;
    }
    return &_entities[row];
}

void EntityListModel::clear()
{
    beginResetModel();
        _entities.clear();
    endResetModel();
}

int EntityListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return static_cast<int>(_entities.size());
}

QVariant EntityListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(_entities.size())) {
        return {};
    }

    const auto& entity = _entities[index.row()];

    if (role == Qt::DisplayRole) {
        return QString::fromStdString(entity.name);
    }

    return {};
}

