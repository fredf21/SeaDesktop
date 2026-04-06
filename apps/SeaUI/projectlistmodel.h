#ifndef PROJECTLISTMODEL_H
#define PROJECTLISTMODEL_H

#include <QAbstractListModel>
#include <QObject>
#include "project.h"
class ProjectListModel : public QAbstractListModel
{
public:
    explicit ProjectListModel(QObject *parent = nullptr);
    void setProjects(std::vector<sea::domain::Project> projects);
    const sea::domain::Project *projectAt(int row) const;

    // QAbstractItemModel interface
    int rowCount(const QModelIndex &parent) const;
    QVariant data(const QModelIndex &index, int role) const;

private:
    std::vector<sea::domain::Project> _projects;
};

#endif // PROJECTLISTMODEL_H
