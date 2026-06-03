#pragma once

// ═══════════════════════════════════════════════════════════════
// integration/support/seastar_test_harness.h
//
// Pont entre doctest et le runtime Seastar.
//
// Le problème :
//   doctest veut piloter main() ; Seastar veut aussi piloter main()
//   via seastar::app_template, qui prend le thread principal et
//   lance son reactor. On ne peut pas avoir les deux runners.
//
// La solution (approche "Seastar pilote, doctest dedans") :
//   main() lance UN SEUL seastar::app_template. À l'intérieur du
//   reactor, dans un seastar::thread (une pile dédiée où l'on a le
//   droit d'appeler .get() de façon bloquante), on invoque le
//   runner doctest programmatiquement. Le runtime Seastar démarre
//   donc une seule fois pour TOUTE la suite.
//
// Conséquence pour l'écriture des tests :
//   Chaque corps de TEST_CASE qui touche Seastar doit s'exécuter
//   lui-même dans un seastar::thread. La fonction run_on_reactor()
//   ci-dessous encapsule ce besoin : on lui passe un lambda qui
//   peut faire des co_await / .get() librement, et elle bloque le
//   thread doctest jusqu'à la fin.
//
// Utilisation dans un fichier de test :
//
//   TEST_CASE("le repository insère et relit un enregistrement") {
//       sea::itest::run_on_reactor([] {
//           // ici, .get() et co_await fonctionnent
//           auto pool = sea::itest::MysqlTestFixture::make_pool().get();
//           // ... assertions doctest classiques (CHECK / REQUIRE) ...
//       });
//   }
// ═══════════════════════════════════════════════════════════════

#include <functional>

namespace sea::itest {

// ───────────────────────────────────────────────────────────────
// run_seastar_doctest
//
// Appelée UNE SEULE FOIS depuis main(). Démarre seastar::app_template,
// entre dans le reactor, et y exécute le runner doctest dans un
// seastar::thread.
//
// argc/argv : ceux du main(). Les arguments doctest (--test-suite=,
//             --success, etc.) sont transmis au runner ; on filtre
//             en amont ceux destinés à Seastar (cf. main.cpp).
//
// Retour : le code de sortie doctest (0 = tous les tests passent),
//          à propager tel quel comme code de retour du processus.
// ───────────────────────────────────────────────────────────────
int run_seastar_doctest(int argc, char** argv);

// ───────────────────────────────────────────────────────────────
// run_on_reactor
//
// À appeler DANS un corps de TEST_CASE. Exécute `body` à l'intérieur
// d'un seastar::thread : la pile dédiée autorise les appels
// bloquants .get() sur des seastar::future, ce qui est interdit
// directement sur la pile du reactor.
//
// La fonction bloque le thread appelant (le thread du runner
// doctest) jusqu'à ce que `body` soit terminé. Toute exception
// levée dans `body` — y compris un échec d'assertion doctest, qui
// est implémenté comme une exception — est re-propagée ici, donc
// remonte correctement au rapport doctest.
//
// `body` n'est PAS marqué noexcept : il a le droit de lever.
// ───────────────────────────────────────────────────────────────
void run_on_reactor(std::function<void()> body);

} // namespace sea::itest
