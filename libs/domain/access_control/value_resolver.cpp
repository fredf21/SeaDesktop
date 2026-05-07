#include "value_resolver.h"

#include <stdexcept>

namespace sea::application::access_control {

namespace {

// Splite "attributes.department_id" en ["attributes", "department_id"]
std::vector<std::string> split_path(const std::string& path)
{
    std::vector<std::string> segments;
    std::string current;
    for (char c : path) {
        if (c == '.') {
            if (!current.empty()) {
                segments.push_back(std::move(current));
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        segments.push_back(std::move(current));
    }
    return segments;
}

ResolvedValue make_scalar(std::string value) {
    ResolvedValue r;
    r.scalar = std::move(value);
    return r;
}

ResolvedValue make_list(std::vector<std::string> values) {
    ResolvedValue r;
    r.list = std::move(values);
    return r;
}

} // namespace anonyme

ValueResolver::ValueResolver(const EvaluationOptions& options)
    : options_(options)
{
}

ResolvedValue ValueResolver::resolve(
    const sea::domain::access_control::PolicyValueRef& ref,
    const sea::domain::access_control::PolicySubject& subject,
    const sea::domain::access_control::PolicyResource& resource,
    const sea::domain::access_control::PolicyContext& context) const
{
    using namespace sea::domain::access_control;

    switch (ref.source) {
    case PolicyValueSource::Literal:
        return resolve_literal(ref);
    case PolicyValueSource::Subject:
        return resolve_subject(ref.path, subject);
    case PolicyValueSource::Resource:
        return resolve_resource(ref.path, resource);
    case PolicyValueSource::Context:
        return resolve_context(ref.path, context);
    }
    return {};
}

ResolvedValue ValueResolver::resolve_literal(
    const sea::domain::access_control::PolicyValueRef& ref) const
{
    // Si literal_list est rempli, c'est une liste
    if (!ref.literal_list.empty()) {
        return make_list(ref.literal_list);
    }
    // Sinon, scalaire (peut être vide string, c'est OK)
    return make_scalar(ref.literal);
}

ResolvedValue ValueResolver::resolve_subject(
    const std::string& path,
    const sea::domain::access_control::PolicySubject& subject) const
{
    const auto segments = split_path(path);
    if (segments.empty()) {
        return handle_not_found("subject." + path);
    }

    // Cas direct : "id", "email", "roles"
    if (segments.size() == 1) {
        const auto& field = segments[0];

        if (field == "id")    return make_scalar(subject.id);
        if (field == "email") return make_scalar(subject.email);
        if (field == "roles") return make_list(subject.roles);

        return handle_not_found("subject." + path);
    }

    // Cas "attributes.xxx"
    if (segments.size() == 2 && segments[0] == "attributes") {
        const auto value = subject.get_attribute(segments[1]);
        if (!value.has_value()) {
            return handle_not_found("subject." + path);
        }
        return make_scalar(*value);
    }

    return handle_not_found("subject." + path);
}

ResolvedValue ValueResolver::resolve_resource(
    const std::string& path,
    const sea::domain::access_control::PolicyResource& resource) const
{
    const auto segments = split_path(path);
    if (segments.empty()) {
        return handle_not_found("resource." + path);
    }

    // Cas direct : "id", "entity_name"
    if (segments.size() == 1) {
        const auto& field = segments[0];

        if (field == "id")          return make_scalar(resource.id);
        if (field == "entity_name") return make_scalar(resource.entity_name);

        return handle_not_found("resource." + path);
    }

    // Cas "attributes.xxx"
    if (segments.size() == 2 && segments[0] == "attributes") {
        const auto value = resource.get_attribute(segments[1]);
        if (!value.has_value()) {
            return handle_not_found("resource." + path);
        }
        return make_scalar(*value);
    }

    return handle_not_found("resource." + path);
}

ResolvedValue ValueResolver::resolve_context(
    const std::string& path,
    const sea::domain::access_control::PolicyContext& context) const
{
    const auto segments = split_path(path);
    if (segments.empty()) {
        return handle_not_found("context." + path);
    }

    // Cas direct : "method", "path", "ip"
    if (segments.size() == 1) {
        const auto& field = segments[0];

        if (field == "method") return make_scalar(context.method);
        if (field == "path")   return make_scalar(context.path);
        if (field == "ip")     return make_scalar(context.ip);

        return handle_not_found("context." + path);
    }

    // Cas "attributes.xxx" ou "time.xxx" (clés calculées dans attributes)
    // ex: "time.hour" est stocké comme attributes["time.hour"]
    // Pour une recherche imbriquée, on reconstruit la clé complète
    {
        const auto value = context.get_attribute(path);
        if (value.has_value()) {
            return make_scalar(*value);
        }
    }

    return handle_not_found("context." + path);
}

ResolvedValue ValueResolver::handle_not_found(const std::string& full_path) const
{
    if (options_.strict_mode == StrictMode::Strict) {
        throw std::runtime_error(
            "ValueResolver: path not found: " + full_path
            );
    }
    // Permissive mode : retourne ResolvedValue vide
    return {};
}

} // namespace sea::application::access_control