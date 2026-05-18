#pragma once

#include <spdlog/sinks/base_sink.h>
#include <spdlog/details/null_mutex.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace sea::application::logging {

// ─────────────────────────────────────────────────────────────────────
// LogEntry — une entree en memoire
//
// On stocke un sequence_id atomique pour permettre la pagination
// par "since=N" (plus fiable qu'un timestamp qui peut etre identique
// pour 2 logs sur la meme milliseconde).
// ─────────────────────────────────────────────────────────────────────
struct LogEntry {
    std::uint64_t                                       sequence_id;
    std::chrono::system_clock::time_point              timestamp;
    std::string                                         logger_name;
    spdlog::level::level_enum                           level;
    std::string                                         message;
};


// ─────────────────────────────────────────────────────────────────────
// RingBufferSink — sink spdlog en memoire
//
// Stocke les N derniers logs dans un ring buffer (FIFO avec ecrasement).
// Thread-safe (utilise std::mutex en interne, en heritant de base_sink_mt).
//
// Utilisation :
//   auto sink = std::make_shared<RingBufferSink>(10000);
//   logger->sinks().push_back(sink);
//
// Lecture :
//   auto entries = sink->snapshot();              // tout le buffer
//   auto recent  = sink->query({.since = 12345});  // depuis seq_id 12345
// ─────────────────────────────────────────────────────────────────────
class RingBufferSink : public spdlog::sinks::base_sink<std::mutex> {
public:
    /**
     * Filtre pour query().
     * Tous les champs sont optionnels — un filtre vide retourne tout.
     */
    struct Query {
        std::optional<std::uint64_t> since_sequence;   // sequence_id >
        std::optional<spdlog::level::level_enum> min_level;
        std::optional<std::string> logger_name;        // exact match
        std::optional<std::string> search;             // substring (case-insensitive)
        std::size_t limit = 100;                       // max entrees retournees
    };

    /**
     * @param capacity Nombre max d'entrees gardees en memoire (>=1)
     */
    explicit RingBufferSink(std::size_t capacity = 10000);

    /**
     * @return Le nombre courant d'entrees dans le buffer (0..capacity)
     */
    [[nodiscard]] std::size_t size() const;

    /**
     * @return La capacite max du buffer
     */
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

    /**
     * @return Le sequence_id de la prochaine entree (incremente a chaque log)
     */
    [[nodiscard]] std::uint64_t next_sequence_id() const noexcept {
        return sequence_counter_.load(std::memory_order_relaxed);
    }

    /**
     * Vide le buffer.
     */
    void clear();

    /**
     * Retourne toutes les entrees triees par sequence_id croissant.
     * Coute O(N) memoire.
     */
    [[nodiscard]] std::vector<LogEntry> snapshot() const;

    /**
     * Retourne les entrees correspondant au filtre, triees par seq_id
     * croissant, limitees a query.limit.
     */
    [[nodiscard]] std::vector<LogEntry> query(const Query& q) const;

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override;
    void flush_() override {}   // ring buffer en RAM, rien a flusher

private:
    const std::size_t                       capacity_;
    mutable std::mutex                      buffer_mutex_;
    std::vector<LogEntry>                   buffer_;     // taille fixe = capacity
    std::size_t                             write_pos_   = 0;
    std::size_t                             count_       = 0;
    std::atomic<std::uint64_t>              sequence_counter_{1};

    // Retourne les entrees brutes triees par seq_id (helper interne, sans mutex)
    [[nodiscard]] std::vector<LogEntry> snapshot_unlocked_() const;
};

} // namespace sea::application::logging
