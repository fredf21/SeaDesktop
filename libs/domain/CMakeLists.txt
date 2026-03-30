cmake_minimum_required(VERSION 3.20)
project(sea_domain LANGUAGES CXX)

add_library(sea_domain STATIC
    schema_validator.cpp
)

add_library(Sea::domain ALIAS sea_domain)

target_sources(sea_domain
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
            project.h
            service.h
            schema.h
            entity.h
            field.h
            field_type.h
            relation.h
            database_config.h
            schema_validator.h
)

target_include_directories(sea_domain
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(sea_domain PUBLIC cxx_std_20)