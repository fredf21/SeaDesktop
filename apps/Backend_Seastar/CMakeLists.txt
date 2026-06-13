cmake_minimum_required(VERSION 3.20)
project(backend_seastar LANGUAGES CXX)

find_package(Seastar REQUIRED)

# NOTE : add_subdirectory(third_party/libbcrypt) a été RETIRÉ d'ici.
# La cible `bcrypt` est désormais déclarée dans le CMakeLists.txt
# RACINE (le code source de libbcrypt a été déplacé dans
# third_party/bcrypt à la racine du projet), pour être visible par
# tous les sous-projets — tests inclus. La cible `bcrypt` reste
# utilisable ici normalement puisqu'une cible déclarée au racine
# est globale.

add_executable(backend_seastar
    src/main.cpp

    src/http/utils/http_utils.h
    src/http/routing/route_registration.h

    src/http/handlers/misc_handlers/health_handler.h
    src/http/handlers/misc_handlers/openapi_handler.h
    src/http/handlers/misc_handlers/swagger_ui_handler.h

    src/http/handlers/auth_handlers/register_handler.h
    src/http/handlers/auth_handlers/login_handler.h
    src/http/handlers/auth_handlers/me_handler.h
    src/http/handlers/auth_handlers/protected_handler.h

    src/http/handlers/crud_handlers/list_handler.h
    src/http/handlers/crud_handlers/get_by_id_handler.h
    src/http/handlers/crud_handlers/create_handler.h
    src/http/handlers/crud_handlers/update_handler.h
    src/http/handlers/crud_handlers/delete_handler.h

    src/http/handlers/relation_handlers/list_by_fk_handler.h
    src/http/handlers/relation_handlers/list_by_fk_field_handler.h
    src/http/handlers/relation_handlers/get_one_by_fk_handler.h
    src/http/handlers/relation_handlers/get_with_children_handler.h
    src/http/handlers/relation_handlers/list_many_to_many_handler.h
    src/http/middlewares/rate_limit_middleware.h
    src/http/middlewares/http_limits_middleware.h
    src/http/middlewares/cors_middleware.h


    src/http/utils/http_utils.cpp
    src/http/routing/route_registration.cpp

    src/http/handlers/misc_handlers/health_handler.cpp
    src/http/handlers/misc_handlers/openapi_handler.cpp
    src/http/handlers/misc_handlers/swagger_ui_handler.cpp

    src/http/handlers/auth_handlers/register_handler.cpp
    src/http/handlers/auth_handlers/login_handler.cpp
    src/http/handlers/auth_handlers/me_handler.cpp
    src/http/handlers/auth_handlers/protected_handler.cpp

    src/http/handlers/crud_handlers/list_handler.cpp
    src/http/handlers/crud_handlers/get_by_id_handler.cpp
    src/http/handlers/crud_handlers/create_handler.cpp
    src/http/handlers/crud_handlers/update_handler.cpp
    src/http/handlers/crud_handlers/delete_handler.cpp

    src/http/handlers/relation_handlers/list_by_fk_handler.cpp
    src/http/handlers/relation_handlers/list_by_fk_field_handler.cpp
    src/http/handlers/relation_handlers/get_one_by_fk_handler.cpp
    src/http/handlers/relation_handlers/get_with_children_handler.cpp
    src/http/handlers/relation_handlers/list_many_to_many_handler.cpp
    src/http/middlewares/security_headers_middleware.cpp
    src/http/middlewares/rate_limit_middleware.cpp
    src/http/middlewares/http_limits_middleware.cpp
    src/http/middlewares/cors_middleware.cpp
    src/http/middlewares/rate_limit_store.h src/http/middlewares/rate_limit_store.cpp
    src/http/middlewares/token_bucket.h src/http/middlewares/token_bucket.cpp
    src/http/middlewares/policy_middleware/policy_types.h
    src/http/utils/nonce_generator.h src/http/utils/nonce_generator.cpp
    src/http/swagger/swagger_assets.h src/http/swagger/swagger_assets.cpp
    src/http/swagger/generated/swagger_favicon.h
    src/http/swagger/generated/swagger_ui_bundle_js.h
    src/http/swagger/generated/swagger_ui_css.h
    src/http/swagger/generated/swagger_ui_standalone_preset_js.h
    src/http/handlers/misc_handlers/swagger_assets_handler.h src/http/handlers/misc_handlers/swagger_assets_handler.cpp
    src/http/middlewares/route_authorization_resolver.h src/http/middlewares/route_authorization_resolver.cpp
    src/http/middlewares/authorization_middleware.h src/http/middlewares/authorization_middleware.cpp
    src/http/handlers/access_control/resource_authorization_helper.h src/http/handlers/access_control/resource_authorization_helper.cpp
    src/http/errors/http_exception.h
    src/http/errors/error_response_factory.h
    src/http/utils/pagination_query.h src/http/utils/pagination_query.cpp
    src/http/handlers/pagination_handler/page_handler.h src/http/handlers/pagination_handler/page_handler.cpp
    src/http/handlers/pagination_handler/offset_handlers.h src/http/handlers/pagination_handler/offset_handlers.cpp
    src/http/handlers/pagination_handler/cursor_handlers.h src/http/handlers/pagination_handler/cursor_handlers.cpp
    src/http/routing/paginated_match_rule.h src/http/routing/paginated_match_rule.cpp
    src/http/routing/pagination_routes.h src/http/routing/pagination_routes.cpp
    src/http/utils/cookie_helper.h src/http/utils/cookie_helper.cpp
    src/http/handlers/auth_handlers/refresh_handler.h src/http/handlers/auth_handlers/refresh_handler.cpp
    src/http/handlers/auth_handlers/logout_handler.h src/http/handlers/auth_handlers/logout_handler.cpp
    src/http/handlers/logs_handlers/logs_handler.h src/http/handlers/logs_handlers/logs_handler.cpp
    src/http/utils/multipart_parser.h src/http/utils/multipart_parser.cpp
    src/http/handlers/file_handlers/file_upload_extractor.h src/http/handlers/file_handlers/file_upload_extractor.cpp
    src/http/handlers/file_handlers/file_download_by_field_handler.h src/http/handlers/file_handlers/file_download_by_field_handler.cpp
    src/http/handlers/relation_handlers/attach_many_to_many_handler.h src/http/handlers/relation_handlers/attach_many_to_many_handler.cpp
    src/http/handlers/relation_handlers/detach_many_to_many_handler.h src/http/handlers/relation_handlers/detach_many_to_many_handler.cpp
    src/http/handlers/misc_handlers/preflight_handler.h
    src/http/handlers/misc_handlers/readiness_handler.h src/http/handlers/misc_handlers/readiness_handler.cpp







)
target_link_libraries(backend_seastar
    PRIVATE
        Seastar::seastar
        Sea::domain
        Sea::application
        Sea::infrastructure
        bcrypt
        spdlog::spdlog_header_only
)

target_include_directories(backend_seastar PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    # libbcrypt vit désormais à la racine : on utilise CMAKE_SOURCE_DIR
    # (racine du projet) et non CMAKE_CURRENT_SOURCE_DIR (ce dossier).
    ${CMAKE_SOURCE_DIR}/third_party/libbcrypt/include
)

target_compile_features(backend_seastar PRIVATE cxx_std_20)