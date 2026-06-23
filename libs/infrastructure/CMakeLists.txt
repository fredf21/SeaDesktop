find_package(yaml-cpp REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Seastar REQUIRED)

# MariaDB Connector/C++ — pre-build dans third_party/mariadb-connector-cpp/install/.
# Le script third_party/build_mariadb_connector.sh compile le source et
# installe les binaires dans third_party/mariadb-connector-cpp/install/.
set(MARIADB_CPP_ROOT
    "${CMAKE_SOURCE_DIR}/third_party/mariadb-connector-cpp/install")

set(MARIADB_CPP_INCLUDE "${MARIADB_CPP_ROOT}/include")
set(MARIADB_CPP_LIB "${MARIADB_CPP_ROOT}/lib/mariadb/libmariadbcpp.so")

if(NOT EXISTS "${MARIADB_CPP_LIB}")
    message(FATAL_ERROR
        "MariaDB Connector/C++ non build. Lancer :\n"
        "  cd third_party && ./build_mariadb_connector.sh\n"
        "Chemin attendu : ${MARIADB_CPP_LIB}")
endif()

if(NOT EXISTS "${MARIADB_CPP_INCLUDE}/mariadb/conncpp.hpp")
    message(FATAL_ERROR
        "MariaDB Connector/C++ headers manquants.\n"
        "Chemin attendu : ${MARIADB_CPP_INCLUDE}/mariadb/conncpp.hpp")
endif()

message(STATUS "MARIADB_CPP_INCLUDE = ${MARIADB_CPP_INCLUDE}")
message(STATUS "MARIADB_CPP_LIB     = ${MARIADB_CPP_LIB}")

include(FetchContent)

FetchContent_Declare(
    jwt-cpp
    GIT_REPOSITORY https://github.com/Thalhammer/jwt-cpp.git
    GIT_TAG v0.7.0
    EXCLUDE_FROM_ALL
)
# JWT_BUILD_EXAMPLES OFF pour eviter les nombreuses cibles d'exemple
# qui font du bruit dans le build et qui ont leurs propres install.
set(JWT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
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
    thread_pool_execution/i_blocking_executor.h
    thread_pool_execution/std_thread_pool_executor.h
    persistence/mysql/mysql_introspector.h persistence/mysql/mysql_introspector.cpp
    persistence/mysql/mysql_schema_generator.h persistence/mysql/mysql_schema_generator.cpp
    persistence/mysql/mysql_bootstrapper.h persistence/mysql/mysql_bootstrapper.cpp
    persistence/mysql/schema_differ.h persistence/mysql/schema_differ.cpp
    persistence/mysql/seed_orchestrator.h persistence/mysql/seed_orchestrator.cpp
    storage/i_file_storage.h
    storage/filesystem_storage.h storage/filesystem_storage.cpp
    persistence/mysql/sea_files_table.h persistence/mysql/sea_files_table.cpp

    file_repository/filerepository.h file_repository/filerepository.cpp
)

add_library(Sea::infrastructure ALIAS sea_infrastructure)

target_sources(sea_infrastructure
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
            yaml/yaml_schema_parser.h
            yaml/yaml_project_repository.h

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
        ${MARIADB_CPP_INCLUDE}
        # libbcrypt : seed_orchestrator.cpp inclut <bcrypt/BCrypt.hpp>.
        # En PUBLIC pour que les cibles qui linkent sea_infrastructure
        # (sea_integration_tests notamment) trouvent aussi le header.
        # Le code de libbcrypt vit dans third_party/bcrypt à la racine.
        ${CMAKE_SOURCE_DIR}/third_party/libbcrypt/include
)

target_link_libraries(sea_infrastructure
    PUBLIC
        Sea::domain
        yaml-cpp
        nlohmann_json::nlohmann_json
        jwt-cpp
        OpenSSL::Crypto
        ${MARIADB_CPP_LIB}
        Seastar::seastar
        Sea::errors
        pthread
        # bcrypt : seed_orchestrator.cpp (dans cette lib) appelle
        # BCrypt::generateHash → bcrypt_gensalt. En PUBLIC pour que
        # le symbole soit résolu par TOUTE cible liant
        # sea_infrastructure, sans qu'elle ait à relinker bcrypt
        # elle-même. La cible `bcrypt` est déclarée au CMakeLists
        # racine, donc visible ici.
        bcrypt
    PRIVATE
        spdlog::spdlog_header_only
)
# RPATH : permet au binaire de trouver libmariadbcpp.so au runtime
# sans avoir besoin de LD_LIBRARY_PATH ou d'installer dans le systeme.
set_target_properties(sea_infrastructure PROPERTIES
    INSTALL_RPATH "${MARIADB_CPP_ROOT}/lib/mariadb"
    BUILD_RPATH   "${MARIADB_CPP_ROOT}/lib/mariadb"
)
target_compile_features(sea_infrastructure PUBLIC cxx_std_20)