add_library(sea_errors_handling STATIC)

add_library(Sea::errors ALIAS sea_errors_handling)

target_sources(sea_errors_handling
    PRIVATE
        exception_handling.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/exception_handling.h
)

target_include_directories(sea_errors_handling
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)