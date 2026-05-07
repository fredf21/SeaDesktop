#ifndef SEA_DOMAIN_ACCESS_CONTROL_CRUD_OPERATION_H
#define SEA_DOMAIN_ACCESS_CONTROL_CRUD_OPERATION_H

#include <optional>
#include <string>
#include <string_view>

namespace sea::domain::access_control {

/**
 * Les opérations CRUD pour lesquelles on peut définir des règles d'accès.
 *
 * Mapping avec les routes générées :
 *   List      : GET    /entities
 *   GetById   : GET    /entities/{id}
 *   Create    : POST   /entities
 *   Update    : PUT    /entities/{id}
 *   Delete    : DELETE /entities/{id}
 */
enum class CrudOperation {
    List,
    GetById,
    Create,
    Update,
    Delete
};

std::string_view to_string(CrudOperation op) noexcept;

std::optional<CrudOperation> crud_operation_from_string(
    const std::string& s) noexcept;

} // namespace sea::domain::access_control

#endif // SEA_DOMAIN_ACCESS_CONTROL_CRUD_OPERATION_H