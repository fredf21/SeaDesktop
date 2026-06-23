# ════════════════════════════════════════════════════════════════
# cpack_clean_dependencies.cmake
#
# Pre-build script execute par CPack juste avant la creation du
# paquet. Supprime les fichiers installes par les dependances
# tierces (jwt-cpp, bcrypt, doctest) qui n'ont pas a se retrouver
# dans le paquet seadesktop-backend.
# ════════════════════════════════════════════════════════════════

set(_STAGING_DIR "${CPACK_TEMPORARY_INSTALL_DIRECTORY}")

message(STATUS "Cleaning third-party install artifacts from ${_STAGING_DIR}")

# Headers et CMake configs des dependances build-time
file(REMOVE_RECURSE
    "${_STAGING_DIR}/include/bcrypt"
    "${_STAGING_DIR}/include/doctest"
    "${_STAGING_DIR}/include/jwt-cpp"
    "${_STAGING_DIR}/include/picojson"
    "${_STAGING_DIR}/cmake"
    "${_STAGING_DIR}/lib/cmake"
    "${_STAGING_DIR}/lib/libbcrypt.a"
)

# Si le dossier include est devenu vide, le supprimer aussi
file(GLOB _include_remainder "${_STAGING_DIR}/include/*")
if(NOT _include_remainder)
    file(REMOVE_RECURSE "${_STAGING_DIR}/include")
endif()
