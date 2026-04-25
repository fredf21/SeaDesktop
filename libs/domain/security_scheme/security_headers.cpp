#include "security_headers.h"

namespace sea::domain::security {

// ===== Setters =====

SecurityHeaders& SecurityHeaders::set_hsts(std::string value)
{
    hsts_ = std::move(value);
    return *this;
}

SecurityHeaders& SecurityHeaders::disable_hsts()
{
    hsts_ = std::nullopt;
    return *this;
}

SecurityHeaders& SecurityHeaders::set_content_type_options(std::string value)
{
    content_type_options_ = std::move(value);
    return *this;
}

SecurityHeaders& SecurityHeaders::disable_content_type_options()
{
    content_type_options_ = std::nullopt;
    return *this;
}

SecurityHeaders& SecurityHeaders::set_frame_options(std::string value)
{
    frame_options_ = std::move(value);
    return *this;
}

SecurityHeaders& SecurityHeaders::disable_frame_options()
{
    frame_options_ = std::nullopt;
    return *this;
}

SecurityHeaders& SecurityHeaders::set_referrer_policy(std::string value)
{
    referrer_policy_ = std::move(value);
    return *this;
}

SecurityHeaders& SecurityHeaders::disable_referrer_policy()
{
    referrer_policy_ = std::nullopt;
    return *this;
}

SecurityHeaders& SecurityHeaders::set_content_security_policy(std::string value)
{
    content_security_policy_ = std::move(value);
    return *this;
}

SecurityHeaders& SecurityHeaders::disable_content_security_policy()
{
    content_security_policy_ = std::nullopt;
    return *this;
}

SecurityHeaders& SecurityHeaders::set_permissions_policy(std::string value)
{
    permissions_policy_ = std::move(value);
    return *this;
}

SecurityHeaders& SecurityHeaders::disable_permissions_policy()
{
    permissions_policy_ = std::nullopt;
    return *this;
}

SecurityHeaders& SecurityHeaders::set_cross_origin_opener_policy(std::string value)
{
    cross_origin_opener_policy_ = std::move(value);
    return *this;
}

SecurityHeaders& SecurityHeaders::disable_cross_origin_opener_policy()
{
    cross_origin_opener_policy_ = std::nullopt;
    return *this;
}

SecurityHeaders& SecurityHeaders::set_cross_origin_resource_policy(std::string value)
{
    cross_origin_resource_policy_ = std::move(value);
    return *this;
}

SecurityHeaders& SecurityHeaders::disable_cross_origin_resource_policy()
{
    cross_origin_resource_policy_ = std::nullopt;
    return *this;
}

// ===== Getters =====

const std::optional<std::string>& SecurityHeaders::hsts() const
{
    return hsts_;
}

const std::optional<std::string>& SecurityHeaders::content_type_options() const
{
    return content_type_options_;
}

const std::optional<std::string>& SecurityHeaders::frame_options() const
{
    return frame_options_;
}

const std::optional<std::string>& SecurityHeaders::referrer_policy() const
{
    return referrer_policy_;
}

const std::optional<std::string>& SecurityHeaders::content_security_policy() const
{
    return content_security_policy_;
}

const std::optional<std::string>& SecurityHeaders::permissions_policy() const
{
    return permissions_policy_;
}

const std::optional<std::string>& SecurityHeaders::cross_origin_opener_policy() const
{
    return cross_origin_opener_policy_;
}

const std::optional<std::string>& SecurityHeaders::cross_origin_resource_policy() const
{
    return cross_origin_resource_policy_;
}

// ===== Factories =====

SecurityHeaders SecurityHeaders::recommended()
{
    SecurityHeaders headers;
    headers.set_hsts("max-age=31536000; includeSubDomains");
    headers.set_content_type_options("nosniff");
    headers.set_frame_options("DENY");
    headers.set_referrer_policy("strict-origin-when-cross-origin");
    headers.set_content_security_policy("default-src 'self'");
    headers.set_permissions_policy("geolocation=(), microphone=(), camera=()");
    return headers;
}

SecurityHeaders SecurityHeaders::strict()
{
    SecurityHeaders headers;
    headers.set_hsts("max-age=63072000; includeSubDomains; preload");
    headers.set_content_type_options("nosniff");
    headers.set_frame_options("DENY");
    headers.set_referrer_policy("no-referrer");
    headers.set_content_security_policy(
        "default-src 'self'; "
        "script-src 'self'; "
        "style-src 'self'; "
        "img-src 'self' data:; "
        "font-src 'self'; "
        "object-src 'none'; "
        "base-uri 'self'; "
        "form-action 'self'; "
        "frame-ancestors 'none'"
        );
    headers.set_permissions_policy(
        "geolocation=(), microphone=(), camera=(), "
        "payment=(), usb=(), magnetometer=(), gyroscope=()"
        );
    headers.set_cross_origin_opener_policy("same-origin");
    headers.set_cross_origin_resource_policy("same-origin");
    return headers;
}

SecurityHeaders SecurityHeaders::none()
{
    return SecurityHeaders{};  // tous nullopt par défaut
}

} // namespace sea::domain::security