#include "fieldlistmodel.h"

FieldListModel::FieldListModel(QObject *parent)
    : QAbstractListModel{parent}
{}

void FieldListModel::setFields(std::vector<sea::domain::Field> fields)
{
    beginResetModel();
        _fields = std::move(fields);
    endResetModel();
}

const sea::domain::Field *FieldListModel::fieldAt(int row) const
{
    if (row < 0 || row >= static_cast<int>(_fields.size())) {
        return nullptr;
    }
    return &_fields[row];
}

void FieldListModel::clear()
{
    beginResetModel();
        _fields.clear();
    endResetModel();
}

int FieldListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return static_cast<int>(_fields.size());
}

QVariant FieldListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(_fields.size())) {
        return {};
    }

    const auto& field = _fields[index.row()];

    if (role == Qt::DisplayRole) {
        return QString::fromStdString(field.name);
    }

    return {};
}

