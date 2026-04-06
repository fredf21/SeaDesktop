#include "projectlistmodel.h"

ProjectListModel::ProjectListModel(QObject *parent)
    : QAbstractListModel{parent}
{}

void ProjectListModel::setProjects(std::vector<sea::domain::Project> projects)
{
    beginResetModel();
        _projects = std::move(projects);
    endResetModel();
}

const sea::domain::Project *ProjectListModel::projectAt(int row) const
{
    if (row < 0 || row >= static_cast<int>(_projects.size())) {
        return nullptr;
    }
    return &_projects[row];
}

int ProjectListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return static_cast<int>(_projects.size());
}

QVariant ProjectListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(_projects.size())) {
        return {};
    }

    const auto& project = _projects[index.row()];

    if (role == Qt::DisplayRole) {
        return QString::fromStdString(project.name);
    }

    return {};
}

