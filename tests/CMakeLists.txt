find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(test_yaml_schema_parser
    test_yaml_schema_parser.cpp
    test_yaml_schema_parser.h
)

set_target_properties(test_yaml_schema_parser PROPERTIES
    AUTOMOC ON
)

target_link_libraries(test_yaml_schema_parser
    PRIVATE
        Qt6::Test
        Sea::infrastructure
        Sea::domain
        Sea::errors
        yaml-cpp
)

target_include_directories(test_yaml_schema_parser
    PRIVATE
        ${CMAKE_SOURCE_DIR}/libs/infrastructure/include
        ${CMAKE_SOURCE_DIR}/libs/domain/include
        ${CMAKE_SOURCE_DIR}/libs/errors/include
)