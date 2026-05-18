#include "routelistmodel.h"

#include <algorithm>

namespace {

/**
 * Helper : convertit HttpMethod en QString.
 */
QString httpMethodToString(sea::application::HttpMethod method)
{
    switch (method) {
    case sea::application::HttpMethod::Get:    return "GET";
    case sea::application::HttpMethod::Post:   return "POST";
    case sea::application::HttpMethod::Put:    return "PUT";
    case sea::application::HttpMethod::Delete: return "DELETE";
    }
    return "?";
}

/**
 * Calcule le rang de tri pour une operation, dans l'ordre :
 *   0 : list
 *   1 : list_page
 *   2 : list_offset
 *   3 : list_cursor
 *   4 : get_by_id
 *   5 : create / update / delete (rang generique)
 *
 *   10 : list_by_fk
 *   11 : list_by_fk_page
 *   12 : list_by_fk_offset
 *   13 : list_by_fk_cursor
 *
 *   20 : list_by_fk_field
 *   21 : list_by_fk_field_page
 *   ...
 *
 *   30 : list_many_to_many (+_page/_offset/_cursor)
 *
 *   40 : get_with_children (+_page/_offset/_cursor)
 *
 *   50 : get_one_by_fk
 *
 *   99 : autre
 *
 * Le but : regrouper visuellement la route standard et ses variantes paginees.
 */
int operationRank(const std::string& op_name)
{
    // CRUD standard d'abord
    if (op_name == "list")          return 0;
    if (op_name == "list_page")     return 1;
    if (op_name == "list_offset")   return 2;
    if (op_name == "list_cursor")   return 3;

    if (op_name == "get_by_id")     return 4;
    if (op_name == "create")        return 5;
    if (op_name == "update")        return 6;
    if (op_name == "delete")        return 7;

    // list_by_fk
    if (op_name == "list_by_fk")          return 10;
    if (op_name == "list_by_fk_page")     return 11;
    if (op_name == "list_by_fk_offset")   return 12;
    if (op_name == "list_by_fk_cursor")   return 13;

    // list_by_fk_field
    if (op_name == "list_by_fk_field")          return 20;
    if (op_name == "list_by_fk_field_page")     return 21;
    if (op_name == "list_by_fk_field_offset")   return 22;
    if (op_name == "list_by_fk_field_cursor")   return 23;

    // list_many_to_many
    if (op_name == "list_many_to_many")          return 30;
    if (op_name == "list_many_to_many_page")     return 31;
    if (op_name == "list_many_to_many_offset")   return 32;
    if (op_name == "list_many_to_many_cursor")   return 33;

    // get_with_children
    if (op_name == "get_with_children")          return 40;
    if (op_name == "get_with_children_page")     return 41;
    if (op_name == "get_with_children_offset")   return 42;
    if (op_name == "get_with_children_cursor")   return 43;

    // get_one_by_fk (HasOne, pas de variante paginee)
    if (op_name == "get_one_by_fk")              return 50;

    return 99;
}

} // namespace

RouteListModel::RouteListModel(QObject *parent)
    : QAbstractListModel{parent}
{}

void RouteListModel::setRoutes(std::vector<sea::application::RouteDefinition> routes)
{
    sortRoutesGrouped(routes);

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
    if (!index.isValid() ||
        index.row() < 0 ||
        index.row() >= static_cast<int>(_routes.size())) {
        return {};
    }

    const auto& route = _routes[index.row()];
    const QString method = httpMethodToString(route.method);
    const QString path = QString::fromStdString(route.path);
    const QString entity = QString::fromStdString(route.entity_name);
    const QString operation = QString::fromStdString(route.operation_name);

    // Fallback DisplayRole : meme format que l'ancien (compatibilite si un
    // appelant ne passe pas par le delegate, ex: un debug print)
    if (role == Qt::DisplayRole) {
        return QString("%1 %2 [%3 / %4]").arg(method, path, entity, operation);
    }

    // Roles personnalises pour le delegate
    if (role == HttpMethodRole) {
        return method;
    }
    if (role == RoutePathRole) {
        return path;
    }
    if (role == RouteEntityOperationRole) {
        return QString("%1 / %2").arg(entity, operation);
    }
    if (role == PaginationModeRole) {
        return paginationModeOf(route.operation_name);
    }

    return {};
}

QHash<int, QByteArray> RouteListModel::roleNames() const
{
    auto roles = QAbstractListModel::roleNames();
    roles[PaginationModeRole]       = "paginationMode";
    roles[HttpMethodRole]           = "httpMethod";
    roles[RoutePathRole]            = "routePath";
    roles[RouteEntityOperationRole] = "routeEntityOperation";
    return roles;
}

QString RouteListModel::paginationModeOf(const std::string& operation_name)
{
    static const char* const suffixes[] = { "_page", "_offset", "_cursor" };
    for (const char* s : suffixes) {
        const std::size_t slen = std::strlen(s);
        if (operation_name.size() > slen &&
            operation_name.compare(operation_name.size() - slen, slen, s) == 0) {
            return QString::fromLatin1(s + 1);   // saute le "_"
        }
    }
    return QString();   // pas paginee
}

void RouteListModel::sortRoutesGrouped(std::vector<sea::application::RouteDefinition>& routes)
{
    std::stable_sort(routes.begin(), routes.end(),
                     [](const sea::application::RouteDefinition& a,
                        const sea::application::RouteDefinition& b) {
                         // 1. Tri par entity_name (regroupement)
                         if (a.entity_name != b.entity_name) {
                             return a.entity_name < b.entity_name;
                         }

                         // 2. Tri par rang d'operation
                         const int ra = operationRank(a.operation_name);
                         const int rb = operationRank(b.operation_name);
                         if (ra != rb) {
                             return ra < rb;
                         }

                         // 3. Stabilite : path
                         return a.path < b.path;
                     });
}