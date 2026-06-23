cmake_minimum_required(VERSION 3.19)
project(SeaUI LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network WebEngineWidgets LinguistTools)

qt_standard_project_setup()

qt_add_executable(SeaUI
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    projectlistmodel.h projectlistmodel.cpp
    servicelistmodel.h servicelistmodel.cpp
    entitylistmodel.h entitylistmodel.cpp



    fieldlistmodel.h fieldlistmodel.cpp
    servicestatuscheck.h servicestatuscheck.cpp
    routelistmodel.h routelistmodel.cpp
    routelistitemdelegate.h routelistitemdelegate.cpp
    translation_manager.h translation_manager.cpp
    IProjectRepository.h
    localprojectrepository.h localprojectrepository.cpp
    httpprojectrepository.h httpprojectrepository.cpp
    profile.h profile.cpp
    auth_client.h auth_client.cpp
    profilemanagerdialog.h profilemanagerdialog.cpp
    connectiondialog.h connectiondialog.cpp
    remotelogsviewer.h remotelogsviewer.cpp

)
# Embarque l'icone Windows dans le .exe via le fichier .rc.
# Ignore sur Linux/macOS.
if(WIN32)
    target_sources(SeaUI PRIVATE packaging/SeaUI.rc)
endif()
# Configuration du bundle macOS (.app).
# - L'icone .icns est copiee dans Resources/ et referencee par Info.plist.
# - MACOSX_BUNDLE_ICON_FILE doit etre le nom du fichier SANS extension.
if(APPLE)
    set(MACOSX_BUNDLE_ICON seaui.icns)
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/icons/${MACOSX_BUNDLE_ICON}"
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(SeaUI PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/icons/${MACOSX_BUNDLE_ICON}")
    set_target_properties(SeaUI PROPERTIES
        MACOSX_BUNDLE_ICON_FILE "${MACOSX_BUNDLE_ICON}"
        MACOSX_BUNDLE_BUNDLE_NAME "SeaUI"
        MACOSX_BUNDLE_BUNDLE_VERSION "1.0.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0.0"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.seadesktop.SeaUI"
    )
endif()
qt_add_resources(SeaUI "icons"
    PREFIX "/"
    FILES
        icons/seaui.png
)
# qt_add_translations(TARGETS ...) est nouveau (Qt 6.7+).
# Pour la compatibilite avec Qt 6.4 d'Ubuntu 24.04 (utilise pour
# packager le .deb), on utilise la forme classique qui marche dans
# les deux versions.
if(Qt6_VERSION VERSION_GREATER_EQUAL "6.7")
    qt_add_translations(
        TARGETS SeaUI
        TS_FILES SeaUI_fr_FR.ts
    )
else()
    qt6_add_translations(SeaUI TS_FILES SeaUI_fr_FR.ts)
endif()

target_link_libraries(SeaUI
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Network
        Sea::application
        Qt6::WebEngineWidgets
)

# ════════════════════════════════════════════════════════════════
# Option : skip toutes les install rules SeaUI quand on packagee
# UNIQUEMENT le backend (.deb backend-only).
# Usage : cmake -DPACKAGE_BACKEND_ONLY=ON ...
# ════════════════════════════════════════════════════════════════
option(PACKAGE_BACKEND_ONLY "Skip SeaUI install rules (for backend-only .deb)" OFF)

if(NOT PACKAGE_BACKEND_ONLY)

    # ── Installation Linux ─────────────────────────────────────────
    # Sur Linux, copie le .desktop et l'icone aux emplacements
    # standards freedesktop pour que GNOME/KDE/XFCE associent le
    # binaire installe a son icone et l'affichent dans les menus.
    if(UNIX AND NOT APPLE)
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/packaging/SeaUI.desktop.in"
            "${CMAKE_CURRENT_BINARY_DIR}/SeaUI.desktop"
            @ONLY
        )

        install(FILES "${CMAKE_CURRENT_BINARY_DIR}/SeaUI.desktop"
            DESTINATION /usr/share/applications COMPONENT seaui)

        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/icons/seaui.png"
            DESTINATION /usr/share/icons/hicolor/256x256/apps
            RENAME seaui.png COMPONENT seaui)
    endif()

    install(TARGETS SeaUI
        BUNDLE  DESTINATION .
        RUNTIME DESTINATION /usr/bin
        LIBRARY DESTINATION /usr/lib
        COMPONENT seaui
    )

    # Le deploy Qt automatique copie toutes les libs Qt dans
    # l'install tree. On l'active uniquement pour les packagings
    # autonomes (macOS .dmg, Windows installer), PAS pour le .deb
    # Linux qui declare les libs Qt comme dependances apt standards.
    if(NOT PACKAGE_SEAUI_ONLY)
        qt_generate_deploy_app_script(
            TARGET SeaUI
            OUTPUT_SCRIPT deploy_script
            NO_UNSUPPORTED_PLATFORM_ERROR
        )
        install(SCRIPT ${deploy_script} COMPONENT seaui)
    endif()

endif()  # NOT PACKAGE_BACKEND_ONLY
