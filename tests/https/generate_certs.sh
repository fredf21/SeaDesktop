#!/usr/bin/env bash
# ════════════════════════════════════════════════════════════════
# tests/https/generate_certs.sh
#
# Genere un certificat auto-signe valide 365 jours pour localhost
# et 127.0.0.1, destine au test de la chaine SeaUI Remote -> HTTPS
# -> nginx reverse proxy -> backend.
#
# USAGE :
#   ./tests/https/generate_certs.sh
#
# Sortie :
#   tests/https/cert.pem  (certificat public, monte read-only dans nginx)
#   tests/https/key.pem   (cle privee, monte read-only dans nginx)
#
# Aucun de ces fichiers n'est versionne dans git (cf. .gitignore).
# Chaque developpeur/contributeur regenere les siens.
#
# Une fois le cert genere, demarrer la stack HTTPS :
#   docker compose -f docker-compose.yml -f docker-compose.https.yml up -d
#
# Et tester :
#   curl -k https://localhost/health
# ════════════════════════════════════════════════════════════════

set -e

# ── On se place dans le dossier du script ────────────────────────
cd "$(dirname "$0")"

# ── Verification d'openssl ──────────────────────────────────────
if ! command -v openssl >/dev/null 2>&1; then
    echo "ERREUR : openssl n'est pas installe."
    echo "  Ubuntu/Debian : sudo apt install openssl"
    echo "  macOS          : openssl est livre avec macOS"
    exit 1
fi

# ── Avertir si les certs existent deja ──────────────────────────
if [ -f cert.pem ] || [ -f key.pem ]; then
    echo "ATTENTION : cert.pem et/ou key.pem existent deja."
    read -p "Ecraser ? [y/N] " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "Annule."
        exit 0
    fi
    rm -f cert.pem key.pem
fi

# ── Generation ──────────────────────────────────────────────────
# -nodes : pas de passphrase (test only)
# SAN    : DNS:localhost + IP:127.0.0.1 indispensables, Qt et curl
#          en mode strict refusent les certs sans SAN compatible.
echo "Generation du certificat auto-signe..."
openssl req -x509 -newkey rsa:2048 -nodes \
    -days 365 \
    -keyout key.pem -out cert.pem \
    -subj "/CN=localhost" \
    -addext "subjectAltName=DNS:localhost,IP:127.0.0.1" \
    2>&1 | grep -E "writing|error" || true

# ── Verification ─────────────────────────────────────────────────
if [ ! -f cert.pem ] || [ ! -f key.pem ]; then
    echo "ERREUR : la generation a echoue."
    exit 1
fi

echo
echo "Certificat genere avec succes :"
openssl x509 -in cert.pem -noout -subject -dates -ext subjectAltName

echo
echo "Prochaines etapes :"
echo "  1. Demarrer la stack HTTPS :"
echo "     docker compose -f docker-compose.yml -f docker-compose.https.yml up -d"
echo "  2. Tester avec curl :"
echo "     curl -k https://localhost/health"
echo "  3. Pour que SeaUI accepte ce cert sans modification :"
echo "     sudo cp tests/https/cert.pem /usr/local/share/ca-certificates/seadesktop-test.crt"
echo "     sudo update-ca-certificates"
