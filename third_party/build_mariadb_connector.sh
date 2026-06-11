#!/usr/bin/env bash
#
# build_mariadb_connector.sh
#
# Build et installe MariaDB Connector/C++ DANS le repo SeaDesktop.
# Aucune installation systeme — tout reste dans third_party/.
#
# Apres execution, on a :
#   third_party/mariadb-connector-cpp/install/lib/mariadb/libmariadbcpp.so
#   third_party/mariadb-connector-cpp/install/include/mariadb/conncpp.hpp
#   third_party/mariadb-connector-cpp/install/include/mariadb/conncpp/...
#
# Le CMakeLists de sea_infrastructure peut alors pointer vers ces fichiers.
#
# Usage :
#   cd third_party
#   ./build_mariadb_connector.sh
#
# Idempotent : ne rebuild pas si install/ existe deja.
#

set -e  # exit on error

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
CONNECTOR_DIR="$SCRIPT_DIR/mariadb-connector-cpp"
INSTALL_DIR="$CONNECTOR_DIR/install"

# ─── Verifications prealables ────────────────────────────────────
if [ ! -d "$CONNECTOR_DIR" ]; then
    echo "ERREUR : $CONNECTOR_DIR n'existe pas."
    echo "Verifier que le code source de MariaDB Connector/C++ est"
    echo "bien place dans third_party/mariadb-connector-cpp/."
    exit 1
fi

if ! command -v cmake &> /dev/null; then
    echo "ERREUR : cmake non installe."
    exit 1
fi

if ! dpkg -l libmariadb-dev &> /dev/null; then
    echo "ERREUR : libmariadb-dev non installe."
    echo "  sudo apt install libmariadb-dev"
    exit 1
fi

# ─── Skip si deja build ──────────────────────────────────────────
if [ -f "$INSTALL_DIR/lib/mariadb/libmariadbcpp.so" ]; then
    echo "MariaDB Connector/C++ deja installe dans $INSTALL_DIR"
    echo "Rien a faire. Pour rebuild : rm -rf $INSTALL_DIR"
    exit 0
fi

# ─── Build ───────────────────────────────────────────────────────
echo "═══ Build MariaDB Connector/C++ ═══"
echo "Source : $CONNECTOR_DIR"
echo "Install: $INSTALL_DIR"
echo

BUILD_DIR="$CONNECTOR_DIR/build"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake .. \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    || { echo "CMake configuration FAILED"; exit 1; }

echo
echo "═══ Compilation (peut prendre 5-15 min) ═══"
make -j"$(nproc)" || { echo "Build FAILED"; exit 1; }

echo
echo "═══ Installation ═══"
make install || { echo "Install FAILED"; exit 1; }

# ─── Verification finale ─────────────────────────────────────────
echo
echo "═══ Verification ═══"
if [ -f "$INSTALL_DIR/lib/mariadb/libmariadbcpp.so" ]; then
    echo "OK : libmariadbcpp.so installe"
    ls -la "$INSTALL_DIR/lib/mariadb/libmariadbcpp.so"
else
    echo "ERREUR : libmariadbcpp.so introuvable apres install"
    exit 1
fi

if [ -f "$INSTALL_DIR/include/mariadb/conncpp.hpp" ]; then
    echo "OK : conncpp.hpp installe"
else
    echo "ERREUR : conncpp.hpp introuvable apres install"
    exit 1
fi

echo
echo "═══ MariaDB Connector/C++ pret a l'emploi ═══"
echo "Le CMakeLists de sea_infrastructure peut maintenant linker"
echo "contre ${INSTALL_DIR}/lib/mariadb/libmariadbcpp.so"
