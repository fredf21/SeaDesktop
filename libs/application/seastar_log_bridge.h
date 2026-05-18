#pragma once

namespace sea::application::logging {

/**
 * SeastarLogBridge
 *
 * Redirige les logs internes de seastar::logger vers le logger spdlog
 * nomme "seastar" (cf LoggingInitializer::Loggers::seastar).
 *
 * Mecanisme :
 *   1. Construit un std::ostream personnalise dont le streambuf accumule
 *      les caracteres et detecte les fins de ligne ('\n')
 *   2. A chaque ligne complete, parse le format Seastar
 *        "LEVEL  YYYY-MM-DD HH:MM:SS,mmm [shard N:thread] module - message"
 *      pour extraire le niveau (INFO/WARN/...) et le message
 *   3. Envoie au logger spdlog "seastar" avec le bon niveau
 *
 * Apres install(), tous les seastar::logger.info/warn/error/... du framework
 * (reseau, reactor, mysql connector si il log via seastar, etc.) passent
 * par spdlog -> sinks dual, rotation, format JSON, etc.
 *
 * Idempotent : si install() est appele plusieurs fois, l'ancien bridge
 * est detruit et remplace proprement.
 *
 * Important : install() doit etre appele APRES LoggingInitializer::init()
 * (sinon le logger "seastar" n'existe pas encore).
 */
class SeastarLogBridge {
public:
    /**
     * Installe le bridge : seastar::logger -> spdlog::get("seastar")
     */
    static void install();

    /**
     * Restaure le comportement par defaut (logs Seastar -> stderr).
     * Appele automatiquement au destructeur du bridge global.
     */
    static void uninstall();

    /**
     * @return true si install() a deja ete appele (et pas uninstall).
     */
    [[nodiscard]] static bool is_installed() noexcept;

private:
    SeastarLogBridge() = delete;
};

} // namespace sea::application::logging