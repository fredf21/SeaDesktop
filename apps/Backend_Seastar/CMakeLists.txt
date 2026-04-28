cmake_minimum_required(VERSION 3.20)
project(backend_seastar LANGUAGES CXX)

find_package(Seastar REQUIRED)

add_subdirectory(third_party/libbcrypt)

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






)
target_link_libraries(backend_seastar
    PRIVATE
        Seastar::seastar
        Sea::domain
        Sea::application
        Sea::infrastructure
        bcrypt
)

target_include_directories(backend_seastar PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libbcrypt/include
)

target_compile_features(backend_seastar PRIVATE cxx_std_20)