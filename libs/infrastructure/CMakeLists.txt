find_package(yaml-cpp REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Seastar REQUIRED)

find_path(MYSQLCPP_INCLUDE_DIR
    NAMES mysql_connection.h
    PATHS /usr/include
    REQUIRED
)

find_library(MYSQLCPP_LIBRARY
    NAMES mysqlcppconn
    PATHS /usr/lib/x86_64-linux-gnu
    NO_DEFAULT_PATH
    REQUIRED
)

message(STATUS "MYSQLCPP_INCLUDE_DIR = ${MYSQLCPP_INCLUDE_DIR}")
message(STATUS "MYSQLCPP_LIBRARY = ${MYSQLCPP_LIBRARY}")

include(FetchContent)

FetchContent_Declare(
    jwt-cpp
    GIT_REPOSITORY https://github.com/Thalhammer/jwt-cpp.git
    GIT_TAG v0.7.0
)

FetchContent_MakeAvailable(jwt-cpp)

add_library(sea_infrastructure STATIC
    yaml/yaml_schema_parser.cpp

    runtime/schema_runtime_registry.cpp
    runtime/generic_validator.cpp
    runtime/generic_crud_engine.cpp
    runtime/json_record_parser.cpp

    persistence/repository_factory.cpp
    persistence/memory/in_memory_generic_repository.cpp

    persistence/mysql/mysql_generic_repository.cpp

    yaml/yaml_project_repository.cpp

    security/secret_store.cpp
    security/jwt_service.cpp

    runtime/i_project_repository.h
    persistence/utilities.h
    persistence/mysql/mysqlconnexionpool.h persistence/mysql/mysqlconnexionpool.cpp
)

add_library(Sea::infrastructure ALIAS sea_infrastructure)

target_sources(sea_infrastructure
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
            yaml/yaml_schema_parser.h

            runtime/dynamic_value.h
            runtime/dynamic_record.h
            runtime/schema_runtime_registry.h
            runtime/generic_validator.h
            runtime/generic_crud_engine.h
            runtime/json_record_parser.h

            persistence/utilities.h
            persistence/i_generic_repository.h
            persistence/repository_factory.h
            persistence/memory/in_memory_generic_repository.h

            persistence/postgres/postgres_generic_repository.h
            persistence/postgres/postgres_connector.h

            persistence/mongo/mongo_generic_repository.h
            persistence/mongo/mongo_connector.h

            persistence/mysql/mysql_generic_repository.h
            persistence/mysql/mysql_connector.h

            security/secret_store.h
            security/jwt_service.h
)

target_include_directories(sea_infrastructure
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
        ${MYSQLCPP_INCLUDE_DIR}
)

target_link_libraries(sea_infrastructure
    PUBLIC
        Sea::domain
        yaml-cpp
        nlohmann_json::nlohmann_json
        jwt-cpp
        OpenSSL::Crypto
        ${MYSQLCPP_LIBRARY}
        Seastar::seastar

)

target_compile_features(sea_infrastructure PUBLIC cxx_std_20)