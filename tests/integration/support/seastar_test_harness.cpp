// ═══════════════════════════════════════════════════════════════
// integration/support/seastar_test_harness.cpp
//
// Implémentation du pont doctest <-> Seastar. Voir le header pour
// l'explication détaillée de l'approche.
// ═══════════════════════════════════════════════════════════════

#include "seastar_test_harness.h"

#include <doctest/doctest.h>

#include <seastar/core/app-template.hh>
#include <seastar/core/future.hh>
#include <seastar/core/thread.hh>
#include <seastar/core/reactor.hh>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <array>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace sea::itest {

// ───────────────────────────────────────────────────────────────
// run_on_reactor
//
// On enrobe `body` dans un seastar::async(...), qui exécute le
// lambda sur un seastar::thread (pile dédiée autorisant .get()).
// Le .get() final, lui, est appelé depuis le runner doctest qui
// tourne DÉJÀ dans un seastar::thread (cf. run_seastar_doctest),
// donc il est légal.
//
// seastar::async re-propage automatiquement toute exception levée
// dans `body` à travers la future qu'il renvoie ; le .get() la
// relance donc dans le thread du runner. Les macros d'assertion
// doctest (REQUIRE notamment) lèvent une exception en cas d'échec :
// ce mécanisme garantit qu'un échec dans `body` est bien vu par
// doctest et n'est pas silencieusement avalé.
// ───────────────────────────────────────────────────────────────
void run_on_reactor(std::function<void()> body)
{
    seastar::async([body = std::move(body)]() mutable {
        body();
    }).get();
}

namespace {

// ───────────────────────────────────────────────────────────────
// init_test_loggers
//
// Enregistre les loggers nommés que le code de production attend.
//
// Pourquoi c'est indispensable : partout dans sea_infrastructure
// (MysqlBootstrapper, repositories…), le code fait directement
//
//     spdlog::get("sea.persistence")->info(...)
//
// SANS vérifier le nullptr. En production, LoggingInitializer::init()
// (couche sea_application) enregistre ces loggers au boot. Mais la
// cible sea_integration_tests ne linke QUE sea_infrastructure et
// n'appelle jamais LoggingInitializer. Sans enregistrement, chaque
// spdlog::get(...) renvoie nullptr → ->info() déréférence nullptr
// → SIGSEGV.
//
// Le harnais doit donc reproduire cette partie de l'environnement
// d'exécution. On enregistre des loggers console minimalistes, sans
// dépendre de sea_application : un sink stdout suffit pour des tests.
//
// Idempotent : spdlog::get vérifie avant de recréer, donc un second
// appel ne casse rien.
// ───────────────────────────────────────────────────────────────
void init_test_loggers()
{
    // Les noms attendus par le code de production (relevés dans les
    // spdlog::get(...) de sea_infrastructure).
    static constexpr std::array<std::string_view, 5> kLoggerNames{
        "sea.boot",
        "sea.http",
        "sea.application",
        "sea.persistence",
        "sea.default",
    };

    // Un sink console partagé par tous les loggers de test.
    auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

    for (const auto name : kLoggerNames) {
        const std::string logger_name{name};

        // N'enregistre que si absent (idempotence).
        if (spdlog::get(logger_name) == nullptr) {
            auto logger = std::make_shared<spdlog::logger>(logger_name, sink);

            // Niveau warn : on ne veut pas noyer la sortie doctest
            // sous les info() du bootstrapper à chaque test.
            logger->set_level(spdlog::level::warn);

            spdlog::register_logger(logger);
        }
    }
}

// Le runner doctest, exécuté à l'intérieur du reactor.
//
// On s'exécute ici sur un seastar::thread (ouvert par run_seastar_doctest),
// donc run_on_reactor — et les .get() qu'il contient — sont légaux.
//
// On garde une copie de argc/argv pour les passer au contexte doctest.
int g_doctest_argc = 0;
char** g_doctest_argv = nullptr;

int execute_doctest_runner()
{
    doctest::Context context;

    // Transmet les arguments de ligne de commande à doctest
    // (--test-case=, --test-suite=, --success, --exit, etc.).
    // Les arguments propres à Seastar ont déjà été retirés en amont
    // dans main(), donc tout ce qui arrive ici est pour doctest.
    context.applyCommandLine(g_doctest_argc, g_doctest_argv);

    // run() exécute tous les TEST_CASE. Chaque TEST_CASE qui touche
    // Seastar appelle run_on_reactor en interne — légal car on est
    // déjà dans un seastar::thread.
    const int result = context.run();

    return result;
}

} // namespace

// ───────────────────────────────────────────────────────────────
// run_seastar_doctest
//
// Démarre l'unique app_template. La lambda passée à app.run() est
// invoquée sur la pile du reactor : on n'y fait donc PAS de .get()
// directement. À la place, on ouvre un seastar::async(...) — un
// seastar::thread — et c'est LUI qui exécute le runner doctest.
//
// Le code de sortie doctest est capturé puis renvoyé par la future,
// pour devenir le code de retour du processus.
// ───────────────────────────────────────────────────────────────
int run_seastar_doctest(int argc, char** argv)
{
    g_doctest_argc = argc;
    g_doctest_argv = argv;

    // Enregistre les loggers nommés AVANT tout test. Le code de
    // production (MysqlBootstrapper, etc.) fait spdlog::get(...)->...
    // sans garde nullptr ; sans cet enregistrement, le premier appel
    // crashe en SIGSEGV. Fait ici, hors du reactor : spdlog n'a aucun
    // lien avec Seastar, l'ordre est donc sans importance.
    init_test_loggers();

    seastar::app_template app;

    // app.run() renvoie le code de sortie du processus. On y passe
    // argc/argv : app_template ignore proprement les arguments qu'il
    // ne reconnaît pas, donc les flags doctest ne le gênent pas.
    return app.run(argc, argv, [] () -> seastar::future<int> {
        // On bascule sur un seastar::thread pour pouvoir bloquer.
        return seastar::async([] {
            return execute_doctest_runner();
        });
    });
}

} // namespace sea::itest