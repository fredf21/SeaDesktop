#pragma once

#include <stdexcept>

namespace sea::sea_errors_handling {

enum class StatusType {
    PERSISTENCE_ERROR = 101,
    YAML_PARSING_ERROR = 102,
    SECURITY_ERROR = 103,
    THREAD_POOL_EXECUTION_ERROR = 104,
    RUNTIME_EXECUTION = 105,
    STORAGE_ERROR = 106     // NEW : erreurs liées au IFileStorage (filesystem, S3, ...)
};

class ErrorException : public std::runtime_error{
public :
    explicit ErrorException(StatusType status_type, std::string status_code, const std::string& what_arg) : _status_type(status_type),
        _status_code(std::move(status_code)), std::runtime_error(what_arg) {}
private:
    std::string _status_code;
    StatusType _status_type;
};

class PersistenceException : public ErrorException{
public :
    explicit PersistenceException(const std::string& what_arg) : ErrorException(StatusType::PERSISTENCE_ERROR, "PERSISTENCE ERROR", std::move(what_arg)) {}

};

class YamlParsingException : public ErrorException{
public :
    explicit YamlParsingException(const std::string& what_arg) : ErrorException(StatusType::YAML_PARSING_ERROR, "YAML PARSING ERROR", std::move(what_arg)) {}

};

class SECURITY_ERROR : public ErrorException{
public :
    explicit SECURITY_ERROR(const std::string& what_arg) : ErrorException(StatusType::SECURITY_ERROR, "SECURITY ERROR", std::move(what_arg)) {}

};

class THREAD_POOL_EXECUTION_ERROR : public ErrorException{
public :
    explicit THREAD_POOL_EXECUTION_ERROR(const std::string& what_arg) : ErrorException(StatusType::THREAD_POOL_EXECUTION_ERROR, "SECURITY ERROR", std::move(what_arg)) {}
};

class RUNTIME_EXECUTION : public ErrorException{
public :
    explicit RUNTIME_EXECUTION(const std::string& what_arg) : ErrorException(StatusType::RUNTIME_EXECUTION, "RUNTIME EXECUTION ERROR", std::move(what_arg)) {}
};

// ─────────────────────────────────────────────────────────────
// StorageException
//
// Levée par les implémentations de IFileStorage en cas d'échec
// d'I/O, de path invalide (path traversal détecté), ou de
// dépassement de capacité disque.
//
// L'utilisation est volontairement large : les handlers HTTP
// catcheront cette exception et la traduiront en 500 Internal
// Server Error (ou 400 si le path vient du client, cf. Étape 7).
// ─────────────────────────────────────────────────────────────
class StorageException : public ErrorException {
public:
    explicit StorageException(const std::string& what_arg)
        : ErrorException(StatusType::STORAGE_ERROR, "STORAGE ERROR", what_arg) {}
};

}