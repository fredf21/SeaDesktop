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
find_package(fmt QUIET)
if(fmt_FOUND)
    message(STATUS "Using system fmt (found at ${fmt_DIR})")
    set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)
else()
    message(WARNING
        "System fmt not found. spdlog will use its bundled fmt. "
        "If your build links Seastar (which depends on system fmt), "
        "expect ABI mismatches at runtime. Install libfmt-dev to fix.")
    set(SPDLOG_FMT_EXTERNAL OFF CACHE BOOL "" FORCE)
endif()

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.17.0
    GIT_SHALLOW    TRUE
    # PAS de EXCLUDE_FROM_ALL ici
)

FetchContent_MakeAvailable(spdlog)
# ───────────────────────────────────────────────────
# bcrypt (libbcrypt)
# ───────────────────────────────────────────────────
add_subdirectory(third_party/libbcrypt EXCLUDE_FROM_ALL)
# Options de packaging visibles globalement.
option(PACKAGE_BACKEND_ONLY "Skip SeaUI install rules (for backend-only .deb)" OFF)

add_subdirectory(libs/domain)
add_subdirectory(libs/errors)
add_subdirectory(libs/application)
add_subdirectory(libs/infrastructure)
add_subdirectory(apps/Backend_Seastar)

option(BUILD_SEAUI "Build the SeaUI desktop application" ON)
if(BUILD_SEAUI)
    add_subdirectory(apps/SeaUI)
endif()

if (BUILD_QT_TESTS)
    add_subdirectory(tests)
endif()

# ════════════════════════════════════════════════════════════════
# CPack : selectionne backend ou SeaUI selon les options
#
# Usage :
#   .deb backend :
#     cmake -B build/debian-backend -DPACKAGE_BACKEND_ONLY=ON
#     cd build/debian-backend && cpack -G DEB
#     → seadesktop-backend-1.0.0-Linux.deb
#
#   .deb SeaUI :
#     cmake -B build/debian-seaui -DPACKAGE_SEAUI_ONLY=ON
#     cd build/debian-seaui && cpack -G DEB
#     → seaui-1.0.0-Linux.deb
# ════════════════════════════════════════════════════════════════

option(PACKAGE_SEAUI_ONLY "Build .deb for SeaUI only (skip backend)" OFF)

# Installation a la racine /, pas dans /usr.
set(CPACK_PACKAGING_INSTALL_PREFIX "/")

if(PACKAGE_SEAUI_ONLY)
    # ── Configuration .deb SeaUI ──────────────────────────────
    set(CPACK_PACKAGE_NAME "seaui")
    set(CPACK_PACKAGE_VERSION "1.0.1")
    set(CPACK_PACKAGE_VENDOR "SeaDesktop")
    set(CPACK_PACKAGE_CONTACT "Tooko Ky Tanguy Frederic <fredtanguy.ca@gmail.com>")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
        "SeaUI - Desktop client for managing SeaDesktop projects")
    set(CPACK_PACKAGE_DESCRIPTION
        "SeaUI is the Qt 6 desktop client that lets developers manage
SeaDesktop projects locally or against a remote backend. It provides
YAML project editing, service lifecycle control, Swagger UI access,
and live log viewing.")
    set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/fredf21/SeaDesktop")

    set(CPACK_DEBIAN_PACKAGE_MAINTAINER
        "Tooko Ky Tanguy Frederic <fredtanguy.ca@gmail.com>")
    set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")

    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "libc6 (>= 2.39), \
libstdc++6 (>= 13), \
libgcc-s1 (>= 4.2), \
libqt6core6t64 (>= 6.4.0), \
libqt6widgets6t64 (>= 6.4.0), \
libqt6network6t64 (>= 6.4.0), \
libqt6webenginewidgets6 (>= 6.4.0), \
libqt6webenginecore6-bin (>= 6.4.0)"
    )

    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "seadesktop-backend")

    set(CPACK_COMPONENTS_ALL seaui)

else()
    # ── Configuration .deb backend (defaut) ─────────────────────
    set(CPACK_PACKAGE_NAME "seadesktop-backend")
    set(CPACK_PACKAGE_VERSION "1.0.1")
    set(CPACK_PACKAGE_VENDOR "SeaDesktop")
    set(CPACK_PACKAGE_CONTACT "Tooko Ky Tanguy Frederic <fredtanguy.ca@gmail.com>")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
        "SeaDesktop backend - low-code REST API generator built on Seastar")
    set(CPACK_PACKAGE_DESCRIPTION
        "SeaDesktop generates production-ready REST APIs from YAML declarations.
        The backend uses Seastar for high-performance HTTP serving with MySQL
        storage, JWT authentication, and fine-grained access control.")
    set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/fredf21/SeaDesktop")

    set(CPACK_DEBIAN_PACKAGE_MAINTAINER
        "Tooko Ky Tanguy Frederic <fredtanguy.ca@gmail.com>")
    set(CPACK_DEBIAN_PACKAGE_SECTION "web")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")

    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "libc6 (>= 2.39), \
        libstdc++6 (>= 13), \
        libgcc-s1 (>= 4.2), \
        libssl3t64 (>= 3.0.0), \
        libgnutls30t64 (>= 3.7.0), \
        libboost-program-options1.83.0, \
        libhwloc15, \
        liburing2, \
        libyaml-cpp0.8, \
        libfmt9, \
        libmariadb3, \
        libsystemd0"
    )

    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "mysql-server | mariadb-server")
    set(CPACK_DEBIAN_PACKAGE_SUGGESTS "nginx | caddy | traefik")

    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_SOURCE_DIR}/apps/Backend_Seastar/packaging/debian/postinst;${CMAKE_SOURCE_DIR}/apps/Backend_Seastar/packaging/debian/prerm")

    set(CPACK_COMPONENTS_ALL backend)
endif()

# Communes aux 2 modes
set(CPACK_DEB_COMPONENT_INSTALL OFF)
set(CPACK_PRE_BUILD_SCRIPTS
    "${CMAKE_SOURCE_DIR}/cmake/cpack_clean_dependencies.cmake")
set(CPACK_GENERATOR "DEB")
include(CPack)