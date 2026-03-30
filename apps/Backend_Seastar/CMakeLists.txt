cmake_minimum_required(VERSION 3.20)
project(backend_seastar LANGUAGES CXX)

find_package(Seastar REQUIRED)

add_executable(backend_seastar
    src/main.cpp
)

target_link_libraries(backend_seastar
    PRIVATE
        Seastar::seastar
        Sea::application
        Sea::infrastructure
        Sea::domain
)

target_compile_features(backend_seastar PRIVATE cxx_std_20)