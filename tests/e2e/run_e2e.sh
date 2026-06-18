#!/usr/bin/env bash
# ════════════════════════════════════════════════════════════════
# run_e2e.sh — Lance la suite e2e SeaDesktop en deux passes
# isolees.
#
# POURQUOI DEUX PASSES :
#   test_admin_restart.py tue son backend a la fin de chaque test
#   (c'est ce qu'il teste). Quand il est execute APRES d'autres tests
#   dans le meme process pytest, le demarrage du backend dedie echoue
#   sur une assertion Seastar interne :
#
#     seastar::sharded<MysqlConnexionPool>::~sharded():
#         Assertion `_instances.empty()` failed.
#
#   La cause precise est dans la facon dont Seastar partage des
#   ressources entre processus enfants successifs lances depuis le
#   meme parent pytest. Ce comportement n'apparait pas quand
#   test_admin_restart.py est lance seul.
#
#   La solution la plus propre est de lancer ce fichier dans un
#   PROCESS pytest separe : c'est ce que ce script fait.
#
# USAGE :
#   cd tests/e2e
#   source .venv/bin/activate
#   ./run_e2e.sh
#
# Variables d'environnement (cf. conftest.py) :
#   SEA_E2E_BACKEND_BIN   chemin du binaire backend_seastar
#   SEA_ITEST_DB_HOST     hote MySQL          [127.0.0.1]
#   SEA_ITEST_DB_PORT     port MySQL          [13306]
#   SEA_ITEST_DB_USER     utilisateur MySQL   [sea_itest]
#   SEA_ITEST_DB_PASSWORD mot de passe        [sea_itest_pwd]
#
# Code de sortie :
#   0 si les DEUX passes reussissent, 1 sinon.
# ════════════════════════════════════════════════════════════════

set -e

# ── Couleurs pour la lisibilite ──────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# ── On se place dans le dossier du script ────────────────────────
cd "$(dirname "$0")"

# ── Verification que MySQL itest tourne ──────────────────────────
DB_PORT="${SEA_ITEST_DB_PORT:-13306}"
if ! nc -z 127.0.0.1 "${DB_PORT}" 2>/dev/null; then
    echo -e "${RED}ERREUR : MySQL itest n'est pas joignable sur le port ${DB_PORT}.${NC}"
    echo "Lance-le avec :"
    echo "  docker compose -f ../docker-compose.test.yml up -d --wait"
    exit 1
fi

# ── Passe 1 : toute la suite SAUF test_admin_restart.py ─────────
echo -e "${YELLOW}═══════════════════════════════════════════════════════════${NC}"
echo -e "${YELLOW}Passe 1/2 : suite e2e principale (sans test_admin_restart)${NC}"
echo -e "${YELLOW}═══════════════════════════════════════════════════════════${NC}"
pytest --ignore=test_admin_restart.py "$@"
PASS1_EXIT=$?

if [ ${PASS1_EXIT} -ne 0 ]; then
    echo -e "${RED}La passe 1 a echoue (code ${PASS1_EXIT}). On n'execute pas la passe 2.${NC}"
    exit ${PASS1_EXIT}
fi

# ── Passe 2 : test_admin_restart.py dans un process pytest dedie ─
echo
echo -e "${YELLOW}═══════════════════════════════════════════════════════════${NC}"
echo -e "${YELLOW}Passe 2/2 : test_admin_restart.py (process pytest dedie)${NC}"
echo -e "${YELLOW}═══════════════════════════════════════════════════════════${NC}"
pytest test_admin_restart.py "$@"
PASS2_EXIT=$?

if [ ${PASS2_EXIT} -ne 0 ]; then
    echo -e "${RED}La passe 2 a echoue (code ${PASS2_EXIT}).${NC}"
    exit ${PASS2_EXIT}
fi

echo
echo -e "${GREEN}═══════════════════════════════════════════════════════════${NC}"
echo -e "${GREEN}Les deux passes ont reussi.${NC}"
echo -e "${GREEN}═══════════════════════════════════════════════════════════${NC}"
exit 0
