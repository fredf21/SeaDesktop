#pragma once

#include <seastar/http/reply.hh>

#include <stdexcept>
#include <string>
#include <utility>

namespace sea::http::errors {

class HttpException : public std::runtime_error {
public:
    using Status = seastar::http::reply::status_type;

    HttpException(Status status, std::string code, std::string message)
        : std::runtime_error(message),
        status_(status),
        code_(std::move(code))
    {}

    Status status() const noexcept {
        return status_;
    }

    const std::string& code() const noexcept {
        return code_;
    }

private:
    Status status_;
    std::string code_;
};

class BadRequestException : public HttpException {
public:
    explicit BadRequestException(std::string message)
        : HttpException(Status::bad_request, "BAD_REQUEST", std::move(message)) {}
};

class ValidationException : public HttpException {
public:
    explicit ValidationException(std::string message)
        : HttpException(Status::unprocessable_entity, "VALIDATION_ERROR", std::move(message)) {}
};

class AuthenticationException : public HttpException {
public:
    explicit AuthenticationException(std::string message)
        : HttpException(Status::unauthorized, "AUTHENTICATION_ERROR", std::move(message)) {}
};

class AuthorizationException : public HttpException {
public:
    explicit AuthorizationException(std::string message)
        : HttpException(Status::forbidden, "AUTHORIZATION_ERROR", std::move(message)) {}
};

class NotFoundException : public HttpException {
public:
    explicit NotFoundException(std::string message)
        : HttpException(Status::not_found, "NOT_FOUND", std::move(message)) {}
};

class ConflictException : public HttpException {
public:
    explicit ConflictException(std::string message)
        : HttpException(Status::conflict, "CONFLICT", std::move(message)) {}
};

class RateLimitException : public HttpException {
public:
    explicit RateLimitException(std::string message)
        : HttpException(Status::too_many_requests, "RATE_LIMIT_EXCEEDED", std::move(message)) {}
};

class InternalServerException : public HttpException {
public:
    explicit InternalServerException(std::string message)
        : HttpException(Status::internal_server_error, "INTERNAL_SERVER_ERROR", std::move(message)) {}
};

} // namespace sea::http::errors
