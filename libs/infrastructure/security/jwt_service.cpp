#include "jwt_service.h"

#include <jwt-cpp/jwt.h>

#include <stdexcept>

namespace sea::infrastructure::security {

// ===== Helpers =====

std::string JwtService::token_type_to_string(TokenType t) noexcept
{
    switch (t) {
    case TokenType::Access:  return "access";
    case TokenType::Refresh: return "refresh";
    }
    return "unknown";
}

std::optional<TokenType> JwtService::token_type_from_string(
    const std::string& s) noexcept
{
    if (s == "access")  return TokenType::Access;
    if (s == "refresh") return TokenType::Refresh;
    return std::nullopt;
}

// ===== Génération =====

std::string JwtService::generate_token(const GenerateTokenParams& params)
{
    if (params.secret.empty()) {
        throw std::invalid_argument("JwtService: secret cannot be empty");
    }
    if (params.user_id.empty()) {
        throw std::invalid_argument("JwtService: user_id cannot be empty");
    }
    if (params.issuer.empty()) {
        throw std::invalid_argument("JwtService: issuer cannot be empty");
    }
    if (params.ttl.count() <= 0) {
        throw std::invalid_argument("JwtService: ttl must be > 0");
    }

    const auto now = std::chrono::system_clock::now();
    const auto expiry = now + params.ttl;

    auto builder = jwt::create()
                       .set_type("JWT")
                       .set_issuer(params.issuer)
                       .set_subject(params.user_id)
                       .set_issued_at(now)
                       .set_expires_at(expiry)
                       .set_payload_claim("token_type",
                                          jwt::claim(token_type_to_string(params.token_type)));

    // Pour les access tokens uniquement, on ajoute email et role
    if (params.token_type == TokenType::Access) {
        if (!params.email.empty()) {
            builder = builder.set_payload_claim("email",
                                                jwt::claim(params.email));
        }
        if (!params.role.empty()) {
            builder = builder.set_payload_claim("role",
                                                jwt::claim(params.role));
        }
        // injecter les claims custom (department_id, etc.)
        // Convention : on les met tous comme strings.
        for (const auto& [key, value] : params.additional_claims) {
            if (key.empty() || value.empty()) {
                continue;
            }
            builder = builder.set_payload_claim(key, jwt::claim(value));
        }
    }

    return builder.sign(jwt::algorithm::hs256{params.secret});
}

// ===== Vérification =====

std::optional<JwtClaims> JwtService::verify_token(
    const VerifyTokenParams& params)
{
    if (params.token.empty() || params.secret.empty()) {
        return std::nullopt;
    }

    try {
        // 1. Décoder le JWT
        const auto decoded = jwt::decode(params.token);

        // 2. Vérifier la signature, l'issuer et l'expiration
        auto verifier = jwt::verify()
                            .allow_algorithm(jwt::algorithm::hs256{params.secret})
                            .with_issuer(params.expected_issuer);

        verifier.verify(decoded);  // throw si invalide

        // 3. Vérifier le type de token (access vs refresh)
        if (!decoded.has_payload_claim("token_type")) {
            return std::nullopt;  // pas de token_type = refus
        }

        const std::string type_str =
            decoded.get_payload_claim("token_type").as_string();

        const auto type_opt = token_type_from_string(type_str);
        if (!type_opt.has_value()) {
            return std::nullopt;
        }

        if (*type_opt != params.expected_type) {
            // Ex: refresh token utilisé comme access token = refus
            return std::nullopt;
        }

        // 4. Construire les claims de retour
        JwtClaims claims;
        claims.user_id = decoded.get_subject();
        claims.issuer = decoded.get_issuer();
        claims.token_type = *type_opt;

        if (decoded.has_payload_claim("email")) {
            claims.email = decoded.get_payload_claim("email").as_string();
        }
        if (decoded.has_payload_claim("role")) {
            claims.role = decoded.get_payload_claim("role").as_string();
        }

        const auto issued_at = decoded.get_issued_at();
        const auto expires_at = decoded.get_expires_at();

        claims.issued_at = std::chrono::duration_cast<std::chrono::seconds>(
                               issued_at.time_since_epoch()).count();
        claims.expires_at = std::chrono::duration_cast<std::chrono::seconds>(
                                expires_at.time_since_epoch()).count();
        // extraire tous les claims custom (non-standards)
        // On itère sur le payload JSON et on garde tout ce qui n'est pas standard.
        static const std::set<std::string> standard_claims = {
            "iss", "sub", "aud", "exp", "iat", "nbf", "jti",
            "email", "role", "token_type"
        };

        try {
            const auto& payload = decoded.get_payload_json();
            for (const auto& [claim_name, claim_value] : payload) {
                if (standard_claims.count(claim_name)) {
                    continue;
                }

                // Convertit la valeur en string selon son type JSON
                std::string value_str;
                try {
                    if (claim_value.is<std::string>()) {
                        value_str = claim_value.get<std::string>();
                    } else if (claim_value.is<bool>()) {
                        value_str = claim_value.get<bool>() ? "true" : "false";
                    } else if (claim_value.is<int64_t>()) {
                        value_str = std::to_string(claim_value.get<int64_t>());
                    } else if (claim_value.is<double>()) {
                        value_str = std::to_string(claim_value.get<double>());
                    } else {
                        // Fallback : sérialise en JSON
                        value_str = claim_value.serialize();
                    }
                } catch (...) {
                    continue;  // skip si conversion échoue
                }

                if (!value_str.empty()) {
                    claims.additional_claims[claim_name] = value_str;
                }
            }
        } catch (...) {
            // Si l'API d'itération sur le payload n'est pas dispo,
            // on continue avec juste les claims standards.
        }
        return claims;

    } catch (const std::exception&) {
        // Toute exception (signature invalide, expiré, mal formé, etc.)
        return std::nullopt;
    }
}

} // namespace sea::infrastructure::security