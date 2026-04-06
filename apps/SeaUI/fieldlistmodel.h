#ifndef FIELDLISTMODEL_H
#define FIELDLISTMODEL_H

#include <QAbstractListModel>
#include <QObject>
#include"field.h"

class FieldListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    explicit FieldListModel(QObject *parent = nullptr);
    void setFields(std::vector<sea::domain::Field> fields);
    const sea::domain::Field* fieldAt(int row) const;
    void clear();

    // QAbstractItemModel interface
    int rowCount(const QModelIndex &parent) const;
    QVariant data(const QModelIndex &index, int role) const;
private:
    std::vector<sea::domain::Field> _fields;

};

#endif // FIELDLISTMODEL_H
