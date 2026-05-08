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
    access_control/policy_operator.h access_control/policy_operator.cpp
    access_control/policy_value_source.h access_control/policy_value_source.cpp
    access_control/policy_value_ref.h
    access_control/policy_predicate.h
    access_control/policy_condition_type.h access_control/policy_condition_type.cpp
    access_control/policy_condition.h access_control/policy_condition.cpp
    access_control/policy_subject.h
    access_control/policy_resource.h
    access_control/crud_operation.h access_control/crud_operation.cpp
    access_control/access_control_spec.h access_control/access_control_spec.cpp
    access_control/entity_access_control.h access_control/entity_access_control.cpp
    access_control/access_control_config.h access_control/access_control_config.cpp
    access_control/operators/operator_strategy.h
    access_control/operators/equals_strategy.h access_control/operators/equals_strategy.cpp
    access_control/value_resolver.h access_control/value_resolver.cpp
    access_control/evaluation_options.h access_control/evaluation_options.cpp
    access_control/evaluation_result.h access_control/evaluation_result.cpp
    access_control/policy_context.h
    access_control/operators/in_strategy.h access_control/operators/in_strategy.cpp
    access_control/operators/regex_match_strategy.h access_control/operators/regex_match_strategy.cpp
    access_control/operators/operator_registry.h access_control/operators/operator_registry.cpp
    access_control/operators/not_equals_strategy.h access_control/operators/not_equals_strategy.cpp
    access_control/operators/not_in_strategy.h access_control/operators/not_in_strategy.cpp
    access_control/operators/contains_strategy.h access_control/operators/contains_strategy.cpp
    access_control/operators/intersects_strategy.h access_control/operators/intersects_strategy.cpp
    access_control/operators/starts_with_strategy.h access_control/operators/starts_with_strategy.cpp
    access_control/operators/ends_with_strategy.h access_control/operators/ends_with_strategy.cpp
    access_control/operators/numeric_helper.h access_control/operators/numeric_helper.cpp
    access_control/operators/greater_than_strategy.h access_control/operators/greater_than_strategy.cpp
    access_control/operators/greater_than_or_equal_strategy.h access_control/operators/greater_than_or_equal_strategy.cpp
    access_control/operators/less_than_strategy.h access_control/operators/less_than_strategy.cpp
    access_control/operators/less_than_or_equal_strategy.h access_control/operators/less_than_or_equal_strategy.cpp
    access_control/operators/exists_strategy.h access_control/operators/exists_strategy.cpp
    access_control/operators/not_exists_strategy.h access_control/operators/not_exists_strategy.cpp
    access_control/operator_evaluator.h access_control/operator_evaluator.cpp
    access_control/policy_engine.h access_control/policy_engine.cpp
    access_control/policy_compiler.h access_control/policy_compiler.cpp
    security_scheme/abac_mode.h security_scheme/abac_mode.cpp
    database_mappings/mysql_type_mapping.h
    database_mappings/postgres_type_mapping.h


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