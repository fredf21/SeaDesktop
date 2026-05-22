find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(test_seadesktop
    main.cpp

    libs/infrastructure/yaml/test_yaml_schema_parser.cpp
    libs/infrastructure/yaml/test_yaml_schema_parser.h

    libs/infrastructure/storage/test_filesystem_storage.h
    libs/infrastructure/storage/test_filesystem_storage.cpp
    libs/infrastructure/security/test_jwt_service.h libs/infrastructure/security/test_jwt_service.cpp
    libs/infrastructure/security/test_secret_store.h libs/infrastructure/security/test_secret_store.cpp
    libs/infrastructure/runtime/test_schema_runtime_registry.h libs/infrastructure/runtime/test_schema_runtime_registry.cpp
    libs/infrastructure/runtime/test_generic_validator.h libs/infrastructure/runtime/test_generic_validator.cpp
    libs/infrastructure/runtime/test_json_record_parser.h libs/infrastructure/runtime/test_json_record_parser.cpp
    libs/infrastructure/persistence/memory/test_in_memory_generic_repository.h libs/infrastructure/persistence/memory/test_in_memory_generic_repository.cpp
    libs/domain/schema_validator/test_schema_validator.h libs/domain/schema_validator/test_schema_validator.cpp
    libs/domain/models/test_domain_models.h libs/domain/models/test_domain_models.cpp
    libs/domain/access_control/operators/test_operator_strategies.h libs/domain/access_control/operators/test_operator_strategies.cpp
    libs/domain/access_control/test_access_control_core.h libs/domain/access_control/test_access_control_core.cpp
    libs/domain/database_mappings/test_mysql_type_mapping.h libs/domain/database_mappings/test_mysql_type_mapping.cpp
    libs/domain/logging/test_logging_config.h libs/domain/logging/test_logging_config.cpp
    libs/domain/protocol/http_protocol/test_http_method.h libs/domain/protocol/http_protocol/test_http_method.cpp
    libs/domain/security_scheme/test_security_scheme_config.h libs/domain/security_scheme/test_security_scheme_config.cpp
)

set_target_properties(test_seadesktop PROPERTIES
    AUTOMOC ON
)

target_link_libraries(test_seadesktop
    PRIVATE
        Qt6::Test
        Sea::infrastructure
        Sea::domain
        Sea::errors
        yaml-cpp
)

target_include_directories(test_seadesktop
    PRIVATE
        ${CMAKE_SOURCE_DIR}/libs/infrastructure/include
        ${CMAKE_SOURCE_DIR}/libs/domain/include
        ${CMAKE_SOURCE_DIR}/libs/errors/include
)