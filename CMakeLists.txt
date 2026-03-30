cmake_minimum_required(VERSION 3.20)
project(SeaDesktop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(libs/domain)
add_subdirectory(libs/application)
add_subdirectory(libs/infrastructure)

add_subdirectory(apps/Backend_Seastar)
add_subdirectory(apps/SeaUI)