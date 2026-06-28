#include "entitydatatablemodel.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>

EntityDataTableModel::EntityDataTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int EntityDataTableModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return _rows.size();
}

int EntityDataTableModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return _columns.size();
}

QVariant EntityDataTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return {};
    }
    if (role != Qt::DisplayRole && role != Qt::ToolTipRole) {
        return {};
    }

    const int row = index.row();
    const int col = index.column();

    if (row < 0 || row >= _rows.size() || col < 0 || col >= _columns.size()) {
        return {};
    }

    const QJsonValue rowVal = _rows.at(row);
    if (!rowVal.isObject()) {
        return {};
    }

    const QJsonObject rowObj = rowVal.toObject();
    const QString colName = _columns.at(col);

    if (!rowObj.contains(colName)) {
        return QString();  // cellule vide pour les cles absentes
    }

    return jsonValueToString(rowObj.value(colName));
}

QVariant EntityDataTableModel::headerData(int section,
                                          Qt::Orientation orientation,
                                          int role) const
{
    if (role != Qt::DisplayRole) {
        return {};
    }

    if (orientation == Qt::Horizontal) {
        if (section < 0 || section >= _columns.size()) {
            return {};
        }
        return _columns.at(section);
    }

    // En-tete vertical : numero de ligne (1-based).
    return section + 1;
}

void EntityDataTableModel::setRows(const QJsonArray& rows)
{
    beginResetModel();
    _rows = rows;
    rebuildColumns();
    endResetModel();
}

void EntityDataTableModel::appendRows(const QJsonArray& rows)
{
    if (rows.isEmpty()) {
        return;
    }

    // Si de nouvelles colonnes apparaissent dans les nouvelles lignes,
    // il faut reset le modele. Pour rester simple, on detecte cette
    // situation et on fait un beginResetModel/endResetModel global.
    QSet<QString> existingCols(_columns.begin(), _columns.end());
    bool hasNewColumns = false;

    for (const auto& v : rows) {
        if (!v.isObject()) continue;
        const QJsonObject obj = v.toObject();
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            if (!existingCols.contains(it.key())) {
                hasNewColumns = true;
                break;
            }
        }
        if (hasNewColumns) break;
    }

    if (hasNewColumns) {
        // Reset complet pour propager les nouvelles colonnes.
        beginResetModel();
        for (const auto& v : rows) {
            _rows.append(v);
        }
        rebuildColumns();
        endResetModel();
    } else {
        // Append simple : Qt ne re-rend que les nouvelles lignes.
        const int firstNew = _rows.size();
        const int lastNew  = firstNew + rows.size() - 1;
        beginInsertRows({}, firstNew, lastNew);
        for (const auto& v : rows) {
            _rows.append(v);
        }
        endInsertRows();
    }
}

void EntityDataTableModel::clear()
{
    beginResetModel();
    _rows = QJsonArray{};
    _columns.clear();
    _knownTotal = -1;
    endResetModel();
}

void EntityDataTableModel::rebuildColumns()
{
    _columns.clear();
    QSet<QString> seen;

    // Premier passage : collecter toutes les cles uniques, dans
    // l'ordre de premiere apparition (preserve l'ordre defini par
    // le YAML / les champs de l'entite).
    for (const auto& v : _rows) {
        if (!v.isObject()) continue;
        const QJsonObject obj = v.toObject();
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            if (!seen.contains(it.key())) {
                _columns.append(it.key());
                seen.insert(it.key());
            }
        }
    }
}

QString EntityDataTableModel::jsonValueToString(const QJsonValue& value)
{
    switch (value.type()) {
    case QJsonValue::Null:
        return QStringLiteral("");
    case QJsonValue::Bool:
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::Double:
        // Affiche les entiers sans point decimal, les flottants avec.
        if (value.toDouble() == static_cast<qint64>(value.toDouble())) {
            return QString::number(static_cast<qint64>(value.toDouble()));
        }
        return QString::number(value.toDouble());
    case QJsonValue::String:
        return value.toString();
    case QJsonValue::Array:
    case QJsonValue::Object:
        // Objets imbriques / arrays : compact JSON pour rester lisible.
        return QString::fromUtf8(
            QJsonDocument(value.isArray()
                              ? QJsonDocument(value.toArray())
                              : QJsonDocument(value.toObject())
                          ).toJson(QJsonDocument::Compact));
    case QJsonValue::Undefined:
        return QStringLiteral("");
    }
    return {};
}