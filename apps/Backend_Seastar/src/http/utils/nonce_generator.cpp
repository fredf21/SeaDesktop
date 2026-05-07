#include "nonce_generator.h"

#include <random>

namespace sea::http::utils {

namespace {

// 64 caracteres base64 URL-safe (pas de + / = qui posent souci en HTML)
constexpr char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789-_";

} // namespace

std::string generate_csp_nonce()
{
    constexpr std::size_t kNonceLen = 24;

    // Source d'entropie hardware si disponible
    thread_local std::random_device rd;
    thread_local std::mt19937_64 gen(rd());

    std::uniform_int_distribution<std::uint32_t> dist(0, 63);

    std::string nonce;
    nonce.reserve(kNonceLen);

    for (std::size_t i = 0; i < kNonceLen; ++i) {
        nonce.push_back(kBase64Alphabet[dist(gen)]);
    }

    return nonce;
}

} // namespace sea::http::utils