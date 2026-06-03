// ═══════════════════════════════════════════════════════════════
// integration/main.cpp
//
// Point d'entrée unique de la suite de tests d'intégration.
//
// Deux responsabilités, et deux seulement :
//
//  1. Fournir l'implémentation de doctest. La macro
//     DOCTEST_CONFIG_IMPLEMENT (et NON ..._WITH_MAIN) demande à
//     doctest de compiler son moteur ICI, mais SANS générer son
//     propre main() — car c'est nous qui pilotons le démarrage,
//     via Seastar. Cette macro ne doit apparaître que dans CE
//     fichier de toute la cible.
//
//  2. Déléguer au harnais : run_seastar_doctest lance l'unique
//     seastar::app_template et exécute le runner doctest dedans.
//
// Aucun TEST_CASE n'est défini ici. Les tests vivent dans les
// fichiers *_itest.cpp, à côté, organisés en miroir des couches
// DDD (integration/libs/infrastructure/persistence/mysql/...).
// ═══════════════════════════════════════════════════════════════

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "support/seastar_test_harness.h"

int main(int argc, char* argv[])
{
    // Tout le travail réel (app_template + runner doctest dans un
    // seastar::thread) est dans le harnais. On se contente de
    // propager le code de sortie : 0 si tous les tests passent.
    const int status = sea::itest::run_seastar_doctest(argc, argv);

    return status;
}
