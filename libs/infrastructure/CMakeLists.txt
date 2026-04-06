cmake_minimum_required(VERSION 3.20)
project(sea_infrastructure LANGUAGES CXX)

find_package(yaml-cpp REQUIRED)
find_package(nlohmann_json REQUIRED)

add_library(sea_infrastructure STATIC
    yaml/yaml_schema_parser.cpp

    runtime/schema_runtime_registry.cpp
    runtime/generic_validator.cpp
    runtime/generic_crud_engine.cpp
    runtime/json_record_parser.cpp

    persistence/repository_factory.cpp
    persistence/memory/in_memory_generic_repository.cpp
    yaml/yaml_project_repository.h yaml/yaml_project_repository.cpp
    runtime/i_project_repository.h
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


            persistence/i_generic_repository.h
            persistence/repository_factory.h
            persistence/memory/in_memory_generic_repository.h

            persistence/postgres/postgres_generic_repository.h
            persistence/postgres/postgres_connector.h

            persistence/mongo/mongo_generic_repository.h
            persistence/mongo/mongo_connector.h
            
            persistence/mysql/mysql_generic_repository.h
            persistence/mysql/mysql_connector.h
)

target_include_directories(sea_infrastructure
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(sea_infrastructure
    PUBLIC
        Sea::domain
        yaml-cpp
        nlohmann_json::nlohmann_json
)

target_compile_features(sea_infrastructure PUBLIC cxx_std_20)
