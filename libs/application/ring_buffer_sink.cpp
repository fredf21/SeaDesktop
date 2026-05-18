#include "ring_buffer_sink.h"

#include <algorithm>
#include <cctype>

namespace sea::application::logging {

namespace {

/**
 * Recherche case-insensitive de needle dans haystack.
 * Implementation simple sans dependance externe.
 */
[[nodiscard]] bool contains_ci(std::string_view haystack, std::string_view needle)
{
    if (needle.empty()) return true;
    if (needle.size() > haystack.size()) return false;

    auto lower = [](unsigned char c) -> unsigned char {
        return static_cast<unsigned char>(std::tolower(c));
    };

    for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        bool match = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
            if (lower(static_cast<unsigned char>(haystack[i + j])) !=
                lower(static_cast<unsigned char>(needle[j]))) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

} // namespace anonyme


// ═════════════════════════════════════════════════════════════════════
// RingBufferSink — implementation
// ═════════════════════════════════════════════════════════════════════

RingBufferSink::RingBufferSink(std::size_t capacity)
    : capacity_(capacity == 0 ? 1 : capacity)
    , buffer_(capacity_)
{
}

std::size_t RingBufferSink::size() const
{
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    return count_;
}

void RingBufferSink::clear()
{
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    write_pos_ = 0;
    count_     = 0;
    // Pas besoin de vider buffer_, les slots seront ecrases au prochain insert
}

void RingBufferSink::sink_it_(const spdlog::details::log_msg& msg)
{
    // sink_it_() est appele sous le mutex de base_sink (grace au template _mt)
    // donc on n'a PAS besoin de re-verrouiller buffer_mutex_ ici.
    // MAIS : le mutex de base_sink est different du buffer_mutex_,
    // donc on doit verrouiller buffer_mutex_ explicitement pour proteger
    // les lecteurs (snapshot/query).
    std::lock_guard<std::mutex> lock(buffer_mutex_);

    LogEntry entry;
    entry.sequence_id = sequence_counter_.fetch_add(1, std::memory_order_relaxed);
    entry.timestamp   = msg.time;
    entry.logger_name = std::string(msg.logger_name.data(), msg.logger_name.size());
    entry.level       = msg.level;
    entry.message     = std::string(msg.payload.data(), msg.payload.size());

    buffer_[write_pos_] = std::move(entry);
    write_pos_ = (write_pos_ + 1) % capacity_;
    if (count_ < capacity_) {
        ++count_;
    }
}

std::vector<LogEntry> RingBufferSink::snapshot_unlocked_() const
{
    std::vector<LogEntry> result;
    result.reserve(count_);

    if (count_ < capacity_) {
        // Le buffer n'est pas encore plein : les N premieres entrees sont
        // a partir de l'index 0.
        for (std::size_t i = 0; i < count_; ++i) {
            result.push_back(buffer_[i]);
        }
    } else {
        // Le buffer est plein : on lit a partir de write_pos_ (= l'entree
        // la plus ancienne) et on tourne.
        for (std::size_t i = 0; i < capacity_; ++i) {
            const std::size_t idx = (write_pos_ + i) % capacity_;
            result.push_back(buffer_[idx]);
        }
    }

    return result;
}

std::vector<LogEntry> RingBufferSink::snapshot() const
{
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    return snapshot_unlocked_();
}

std::vector<LogEntry> RingBufferSink::query(const Query& q) const
{
    std::lock_guard<std::mutex> lock(buffer_mutex_);

    auto all = snapshot_unlocked_();
    std::vector<LogEntry> filtered;
    filtered.reserve(std::min(all.size(), q.limit));

    for (const auto& entry : all) {
        if (filtered.size() >= q.limit) break;

        // Filtre : since_sequence
        if (q.since_sequence.has_value() &&
            entry.sequence_id <= *q.since_sequence) {
            continue;
        }

        // Filtre : min_level
        if (q.min_level.has_value() &&
            entry.level < *q.min_level) {
            continue;
        }

        // Filtre : logger_name (exact match)
        if (q.logger_name.has_value() &&
            entry.logger_name != *q.logger_name) {
            continue;
        }

        // Filtre : search (substring case-insensitive dans le message)
        if (q.search.has_value() &&
            !contains_ci(entry.message, *q.search)) {
            continue;
        }

        filtered.push_back(entry);
    }

    return filtered;
}

} // namespace sea::application::logging