#ifndef SERVICELISTMODEL_H
#define SERVICELISTMODEL_H

#include <QAbstractListModel>
#include <QObject>
#include "service.h"
class ServiceListModel : public QAbstractListModel
{
public:
    explicit ServiceListModel(QObject *parent = nullptr);

    void setServices(std::vector<sea::domain::Service> services);
    const sea::domain::Service* serviceAt(int row) const;

    // QAbstractItemModel interface
    int rowCount(const QModelIndex &parent) const;
    QVariant data(const QModelIndex &index, int role) const;
private:
    std::vector<sea::domain::Service> _services;
};

#endif // SERVICELISTMODEL_H
