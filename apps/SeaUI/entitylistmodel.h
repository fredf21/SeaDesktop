#ifndef ENTITYLISTMODEL_H
#define ENTITYLISTMODEL_H

#include <QAbstractListModel>
#include <QObject>
#include "entity.h"
class EntityListModel : public QAbstractListModel
{
public:
    explicit EntityListModel(QObject *parent = nullptr);
    void setEntities(std::vector<sea::domain::Entity> entities);
    const sea::domain::Entity* entityAt(int row) const;
    void clear();
    // QAbstractItemModel interface
    int rowCount(const QModelIndex &parent) const;
    QVariant data(const QModelIndex &index, int role) const;

private:
    std::vector<sea::domain::Entity> _entities;
};

#endif // ENTITYLISTMODEL_H
