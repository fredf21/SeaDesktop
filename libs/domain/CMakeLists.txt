cmake_minimum_required(VERSION 3.20)
project(sea_domain LANGUAGES CXX)

add_library(sea_domain STATIC
    schema_validator.cpp
    security_scheme/security_config.cpp
    security_scheme/authentification_config.cpp
    security_scheme/rate_limit_rule.cpp
    security_scheme/cors_config.cpp
    protocol/http_protocol/http_method.cpp
    security_scheme/http_limit.cpp
    security_scheme/security_headers.cpp

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
            security_scheme/security_config.h
            protocol/http_protocol/http_method.h
            security_scheme/authentification_config.h
            security_scheme/http_limit.h
            security_scheme/cors_config.h
            security_scheme/security_headers.h
            security_scheme/rate_limit_rule.h

)

target_include_directories(sea_domain
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(sea_domain PUBLIC cxx_std_20)