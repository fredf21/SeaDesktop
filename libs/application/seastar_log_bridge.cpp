#include "seastar_log_bridge.h"

#include "logging_initializer.h"   // pour Loggers::seastar

#include <iostream>
#include <seastar/util/log.hh>
#include <spdlog/spdlog.h>
#include <atomic>
#include <cstdio>
#include <memory>
#include <mutex>
#include <ostream>
#include <streambuf>
#include <string>
#include <string_view>
#include <utility>

namespace sea::application::logging {

namespace {

// ─────────────────────────────────────────────────────────────────────
// SeastarLineStreambuf
//
// streambuf qui accumule les caracteres dans un buffer interne et,
// a chaque '\n', appelle un callback avec la ligne complete (sans le \n).
//
// Thread-safe : chaque thread Seastar a son propre buffer via thread_local
// pour eviter la contention sur le streambuf partage (sinon les logs
// de plusieurs shards arrivant en parallele s'entremelent).
// ─────────────────────────────────────────────────────────────────────

class SeastarLineStreambuf : public std::streambuf {
public:
    SeastarLineStreambuf() = default;

protected:
    // Appele pour chaque caractere ecrit dans l'ostream
    int_type overflow(int_type ch) override {
        if (ch == traits_type::eof()) {
            flush_current_line();
            return traits_type::not_eof(ch);
        }
        const char c = traits_type::to_char_type(ch);
        if (c == '\n') {
            flush_current_line();
        } else if (c != '\r') {   // ignore CR (eviter \r\n -> ligne vide)
            // Limite a 64 KB pour eviter la consommation memoire delirante
            // (un message de log normal fait < 1 KB)
            if (current_line_.size() < 64 * 1024) {
                current_line_.push_back(c);
            }
        }
        return ch;
    }

    int sync() override {
        flush_current_line();
        return 0;
    }

private:
    // Buffer ligne-en-cours, PAR THREAD pour eviter la contention
    thread_local static std::string current_line_;

    void flush_current_line() {
        if (current_line_.empty()) return;
        SeastarLineStreambuf::dispatch_line(current_line_);
        current_line_.clear();
    }

    // Parse une ligne de log Seastar et envoie au logger spdlog "seastar".
    //
    // Format attendu :
    //   LEVEL  YYYY-MM-DD HH:MM:SS,mmm [shard N:thread] module - message
    //
    // Exemples :
    //   "WARN  2026-05-13 14:32:01,234 [shard 2:main] net - listen failed"
    //   "INFO  2026-05-13 14:32:02,100 [shard 0:main] reactor - shutting down"
    //
    // Si le parsing echoue (format inattendu), on log la ligne brute au
    // niveau info pour ne rien perdre.
    static void dispatch_line(const std::string& line) {
        auto logger = spdlog::get(std::string(LoggingInitializer::Loggers::seastar));
        if (!logger) {
            // Bridge installe mais logger non encore cree : fallback stderr.
            // Ne devrait pas arriver vu que LoggingInitializer::init() cree
            // le logger AVANT que le bridge ne soit installe.
            std::fputs(line.c_str(), stderr);
            std::fputc('\n', stderr);
            return;
        }

        // Note : on n'utilise pas de structured binding (auto [level, msg] = ...)
        // pour compat avec les compilations qui ont eu un souci dessus
        // dans ce fichier.
        const std::pair<spdlog::level::level_enum, std::string_view> parsed
            = parse_line(line);

        // Log au bon niveau
        logger->log(parsed.first, "{}", parsed.second);
    }

    // Retourne {niveau spdlog, message extrait}.
    // En cas de parsing echoue : {info, ligne brute}.
    [[nodiscard]] static std::pair<spdlog::level::level_enum, std::string_view>
    parse_line(const std::string& line)
    {
        // 1. Extraire le niveau (avant le premier espace)
        const auto first_space = line.find(' ');
        if (first_space == std::string::npos) {
            return {spdlog::level::info, line};
        }

        const std::string_view level_str(line.data(), first_space);

        spdlog::level::level_enum level = spdlog::level::info;
        bool level_known = true;

        // Seastar log levels : DEBUG, INFO, WARN, ERROR, TRACE
        // (parfois suivis d'espaces de padding pour alignement)
        if (level_str == "TRACE")        level = spdlog::level::trace;
        else if (level_str == "DEBUG")   level = spdlog::level::debug;
        else if (level_str == "INFO")    level = spdlog::level::info;
        else if (level_str == "WARN")    level = spdlog::level::warn;
        else if (level_str == "ERROR")   level = spdlog::level::err;
        else level_known = false;

        if (!level_known) {
            // Format inattendu : on log la ligne entiere
            return {spdlog::level::info,
                    std::string_view(line.data(), line.size())};
        }

        // 2. Trouve " - " qui sepere le prefixe du message
        const auto sep = line.find(" - ", first_space);
        if (sep == std::string::npos) {
            // Pas de separateur trouve : on prend tout apres le niveau
            // (en sautant les espaces eventuels de padding)
            std::size_t start = first_space;
            while (start < line.size() && line[start] == ' ') ++start;
            return {level,
                    std::string_view(line.data() + start, line.size() - start)};
        }

        const std::size_t msg_start = sep + 3;   // skip " - "
        if (msg_start >= line.size()) {
            return {level, std::string_view{}};
        }

        return {level,
                std::string_view(line.data() + msg_start, line.size() - msg_start)};
    }
};

thread_local std::string SeastarLineStreambuf::current_line_;


// ─────────────────────────────────────────────────────────────────────
// Etat global
// ─────────────────────────────────────────────────────────────────────

std::mutex                                g_install_mutex;
std::atomic<bool>                          g_installed{false};
std::unique_ptr<SeastarLineStreambuf>      g_streambuf;
std::unique_ptr<std::ostream>              g_ostream;

} // namespace anonyme


// ═════════════════════════════════════════════════════════════════════
// API publique
// ═════════════════════════════════════════════════════════════════════

void SeastarLogBridge::install()
{
    std::lock_guard<std::mutex> lock(g_install_mutex);

    if (g_installed.load()) {
        // Idempotent : si deja installe, on detruit l'ancien d'abord
        seastar::logger::set_ostream(std::cerr);
        g_ostream.reset();
        g_streambuf.reset();
        g_installed.store(false);
    }

    // Cree le streambuf + l'ostream personnalise.
    // Ils doivent vivre tant que le bridge est installe.
    g_streambuf = std::make_unique<SeastarLineStreambuf>();
    g_ostream   = std::make_unique<std::ostream>(g_streambuf.get());

    // Pointe seastar::logger vers notre ostream
    seastar::logger::set_ostream(*g_ostream);

    g_installed.store(true);
}

void SeastarLogBridge::uninstall()
{
    std::lock_guard<std::mutex> lock(g_install_mutex);

    if (!g_installed.load()) return;

    // Restaure stderr comme destination par defaut
    seastar::logger::set_ostream(std::cerr);

    // Libere les ressources
    g_ostream.reset();
    g_streambuf.reset();

    g_installed.store(false);
}

bool SeastarLogBridge::is_installed() noexcept
{
    return g_installed.load();
}

} // namespace sea::application::logging