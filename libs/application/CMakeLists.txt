cmake_minimum_required(VERSION 3.20)
project(sea_application LANGUAGES CXX)

add_library(sea_application STATIC
    import_yaml_schema_usecase.cpp
    validate_schema_usecase.cpp
    start_service_usecase.cpp
    route_generator.cpp
    load_project_use_case.h
    openapigenerator.h openapigenerator.cpp
    authservice.h authservice.cpp

)

add_library(Sea::application ALIAS sea_application)

target_sources(sea_application
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
            import_yaml_schema_usecase.h
            validate_schema_usecase.h
            start_service_usecase.h
            route_generator.h
)

target_include_directories(sea_application
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(sea_application
    PUBLIC
        Sea::domain
        Sea::infrastructure
)

target_compile_features(sea_application PUBLIC cxx_std_20)