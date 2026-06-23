# ═══════════════════════════════════════════════════════════════
# tests/CMakeLists.txt
#
# Deux cibles de test, volontairement séparées :
#
#   1. test_seadesktop      — tests UNITAIRES (QtTest).
#                             Isolation pure, aucune dépendance externe.
#                             Boucle d'événements Qt.
#
#   2. sea_integration_tests — tests d'INTÉGRATION (doctest + Seastar).
#                             Traversent plusieurs couches DDD avec une
#                             vraie base MySQL. Boucle réactive Seastar.
#
# Pourquoi deux exécutables distincts :
#   La boucle d'événements Qt (QtTest) et le scheduler de Seastar
#   prennent tous les deux le contrôle du thread principal via leur
#   propre main(). Ils ne peuvent pas coexister dans le même process.
#   → un main() par cible, deux binaires séparés.
#
# sea_integration_tests ne linke NI Qt6::Test NI AUTOMOC : aucun
# QObject n'y vit, donc aucun conflit possible avec Seastar.
# ═══════════════════════════════════════════════════════════════


# ───────────────────────────────────────────────────────────────
# CIBLE 1 — Tests unitaires QtTest (inchangée)
# ───────────────────────────────────────────────────────────────
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


# ───────────────────────────────────────────────────────────────
# CIBLE 2 — Tests d'intégration Seastar / MySQL (nouvelle)
# ───────────────────────────────────────────────────────────────
#
# OFF par défaut : ces tests exigent un MySQL accessible. On ne veut
# pas les compiler lors d'un build courant, ni ralentir SeaUI.
#
# Activation :
#   cmake -DBUILD_QT_TESTS=ON -DBUILD_INTEGRATION_TESTS=ON ..
#
# (BUILD_QT_TESTS doit aussi être ON car c'est lui qui fait
#  add_subdirectory(tests) dans le CMakeLists racine.)
# ───────────────────────────────────────────────────────────────
option(BUILD_INTEGRATION_TESTS
       "Build Seastar/MySQL integration tests (requires a running MySQL)"
       OFF)

if(BUILD_INTEGRATION_TESTS)

    # doctest : framework de test header-only, sans boucle d'événements.
    # Choisi parce qu'il n'impose aucun runtime — on garde la main sur
    # le main() pour y lancer seastar::app_template nous-mêmes.
    include(FetchContent)

    FetchContent_Declare(
        doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG        v2.4.11
        GIT_SHALLOW    TRUE
    )

    set(CMAKE_SKIP_INSTALL_RULES ON)
    FetchContent_MakeAvailable(doctest)
    set(CMAKE_SKIP_INSTALL_RULES OFF)

    add_executable(sea_integration_tests
        # ── Point d'entrée + harnais ──────────────────────────
        integration/main.cpp
        integration/support/seastar_test_harness.h
        integration/support/seastar_test_harness.cpp
        integration/support/mysql_test_fixture.h
        integration/support/mysql_test_fixture.cpp

        # ── Tests : persistence MySQL ─────────────────────────
        integration/libs/infrastructure/persistence/mysql/mysql_generic_repository_itest.cpp
        integration/libs/infrastructure/persistence/mysql/mysql_bootstrapper_itest.cpp
        integration/libs/infrastructure/persistence/mysql/mysql_transaction_itest.cpp
        integration/libs/infrastructure/persistence/mysql/mysql_pivot_itest.cpp
        integration/libs/infrastructure/persistence/mysql/mysql_pagination_itest.cpp
        integration/libs/infrastructure/persistence/mysql/mysql_increment_field_itest.cpp
        integration/libs/infrastructure/persistence/mysql/mysql_migration_itest.cpp
        integration/libs/infrastructure/persistence/mysql/mysql_seed_orchestrator_itest.cpp
        integration/libs/infrastructure/persistence/mysql/mysql_file_service_itest.cpp
        integration/libs/infrastructure/persistence/mysql/mysql_file_hardening_itest.cpp
    )

    # IMPORTANT : ni Qt6::Test ni AUTOMOC ici.
    # Sea::infrastructure propage en PUBLIC : Seastar::seastar,
    # mysqlcppconn, yaml-cpp, Sea::domain, Sea::errors, OpenSSL,
    # ainsi que les include dirs MySQL. Linker Sea::infrastructure
    # suffit donc à tout récupérer par transitivité.
    #
    # EXCEPTION : spdlog. Sea::infrastructure le linke en PRIVATE
    # (c'est un détail d'implémentation de la couche infra, pas une
    # dépendance d'interface), donc il NE se propage PAS. Or le
    # harnais l'utilise directement dans init_test_loggers() pour
    # enregistrer les loggers nommés que le code de prod attend.
    # On le linke donc explicitement ici.
    target_link_libraries(sea_integration_tests
        PRIVATE
            Sea::application
            Sea::infrastructure
            doctest::doctest
            spdlog::spdlog_header_only
    )

    target_include_directories(sea_integration_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/integration
            ${CMAKE_SOURCE_DIR}/libs/infrastructure/include
            ${CMAKE_SOURCE_DIR}/libs/domain/include
            ${CMAKE_SOURCE_DIR}/libs/errors/include
    )

    # Seastar exige C++20 minimum ; sea_infrastructure le force déjà
    # en PUBLIC, mais on l'affirme ici pour être explicite.
    target_compile_features(sea_integration_tests PRIVATE cxx_std_20)

endif()