cmake_minimum_required(VERSION 3.20)
project(SeaDesktop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

#-----------------------------
# tests
#-----------------------------
# Option pour compiler ou non les tests Qt
option(BUILD_QT_TESTS "Build Qt unit tests" OFF)


# ───────────────────────────────────────────────────
# spdlog via FetchContent
# ───────────────────────────────────────────────────
include(FetchContent)

# Empêche spdlog d'installer ses fichiers dans /usr/local
# (on l'utilise juste pour notre build interne)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)

# Compile spdlog en bibliothèque (au lieu de header-only)
# temps de compilation des cibles fortement réduit
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(SPDLOG_FMT_EXTERNAL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.17.0                  # version épinglée
    GIT_SHALLOW    TRUE                     # clone rapide
)

FetchContent_MakeAvailable(spdlog)

add_subdirectory(libs/domain)
add_subdirectory(libs/errors)
add_subdirectory(libs/application)
add_subdirectory(libs/infrastructure)
add_subdirectory(apps/Backend_Seastar)
add_subdirectory(apps/SeaUI)
if (BUILD_QT_TESTS)
    add_subdirectory(tests)
endif()