#include "json_record_parser.h"
#include "exception_handling.h"
#include "persistence/utilities.h"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace sea::infrastructure::runtime {

DynamicRecord JsonRecordParser::parse(const sea::domain::Entity& entity,
                                      const std::string& json_body) const {
    using json = nlohmann::json;

    json j;

    try {
        j = json::parse(json_body);
    } catch (const std::exception& e) {
        throw sea::sea_errors_handling::RUNTIME_EXECUTION(std::string("[RUNTIME EXECUTION] Invalid JSON: ") + e.what());
    }

    if (!j.is_object()) {
        throw std::runtime_error("The JSON body must be an object.");
    }

    DynamicRecord record;

    for (const auto& field : entity.fields) {
        try{
            if (!j.contains(field.name)) {
                continue;
            }
            bool unsignedvalue = false;
            if(field.unsigned_value){
                unsignedvalue = true;
            }
            const auto& value = j[field.name];

            if (value.is_null()) {
                record[field.name] = std::monostate{};
                continue;
            }

            switch (field.type) {
            case sea::domain::FieldType::String:
            case sea::domain::FieldType::Text:
            case sea::domain::FieldType::UUID:
            case sea::domain::FieldType::Password:
            case sea::domain::FieldType::Email:
            case sea::domain::FieldType::Timestamp:
            case sea::domain::FieldType::Decimal:
            case sea::domain::FieldType::File:{
                if (!value.is_string()) {
                    throw sea::sea_errors_handling::RUNTIME_EXECUTION("[RUNTIME EXECUTION] Field '" + field.name + "' must be a string.");
                }
                record[field.name] = value.get<std::string>();
                break;
            }
            case sea::domain::FieldType::Json: {
                record[field.name] = value;
                break;
            }
            case sea::domain::FieldType::BigInt:
            {
                if (!value.is_number_integer()) {
                    throw sea::sea_errors_handling::RUNTIME_EXECUTION("[RUNTIME EXECUTION] Field '" + field.name + "' must be an integer.");
                }
                if (unsignedvalue) {
                    // Un BIGINT non signé refuse les valeurs négatives.
                    // is_number_unsigned() est vrai uniquement si la
                    // valeur JSON tient dans un entier non signé.
                    if (!value.is_number_unsigned()) {
                        throw sea::sea_errors_handling::RUNTIME_EXECUTION("[RUNTIME EXECUTION] Field '" + field.name + "' must be a positive integer (unsigned BIGINT).");
                    }
                    record[field.name] = value.get<std::uint64_t>();
                }
                else {
                    record[field.name] = value.get<std::int64_t>();
                }
                break;
            }
            case sea::domain::FieldType::SmallInt:
            {
                if (!value.is_number_integer()) {
                    throw sea::sea_errors_handling::RUNTIME_EXECUTION("[RUNTIME EXECUTION] Field '" + field.name + "' must be an integer.");
                }
                // On lit la valeur dans un type large (int64_t) AVANT
                // toute conversion, puis on vérifie qu'elle tient dans
                // le type cible. Lire directement en int16_t tronque
                // la valeur et rend le contrôle de plage inopérant.
                const std::int64_t raw = value.get<std::int64_t>();
                if (unsignedvalue) {
                    if (raw < 0 ||
                        raw > static_cast<std::int64_t>(std::numeric_limits<std::uint16_t>::max())) {
                        throw sea::sea_errors_handling::RUNTIME_EXECUTION("[RUNTIME EXECUTION] Field '" + field.name + "' is outside the range of an unsigned SMALLINT.");
                    }
                    record[field.name] = static_cast<std::uint16_t>(raw);
                }
                else {
                    if (raw < static_cast<std::int64_t>(std::numeric_limits<std::int16_t>::min()) ||
                        raw > static_cast<std::int64_t>(std::numeric_limits<std::int16_t>::max())) {
                        throw sea::sea_errors_handling::RUNTIME_EXECUTION("[RUNTIME EXECUTION] Field '" + field.name + "' is outside the range of a SMALLINT.");
                    }
                    record[field.name] = static_cast<std::int16_t>(raw);
                }
                break;
            }

            case sea::domain::FieldType::Int:
            {
                if (!value.is_number_integer()) {
                    throw sea::sea_errors_handling::RUNTIME_EXECUTION("[RUNTIME EXECUTION] Field '" + field.name + "' must be an integer.");
                }
                // Lecture en type large AVANT conversion : voir le
                // commentaire de la branche SmallInt.
                const std::int64_t raw = value.get<std::int64_t>();
                if (unsignedvalue) {
                    if (raw < 0 ||
                        raw > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
                        throw sea::sea_errors_handling::RUNTIME_EXECUTION("[RUNTIME EXECUTION] Field '" + field.name + "' is outside the range of an unsigned INT.");
                    }
                    record[field.name] = static_cast<std::uint32_t>(raw);
                }
                else {
                    if (raw < static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min()) ||
                        raw > static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max())) {
                        throw sea::sea_errors_handling::RUNTIME_EXECUTION("[RUNTIME EXECUTION] Field '" + field.name + "' is outside the range of an INT.");
                    }
                    record[field.name] = static_cast<std::int32_t>(raw);
                }
                break;
            }
            case sea::domain::FieldType::Float: {
                if (!value.is_number()) {
                    throw sea::sea_errors_handling::RUNTIME_EXECUTION("[RUNTIME EXECUTION] Field '" + field.name + "' must be a number.");
                }
                record[field.name] = value.get<double>();
                break;
            }

            case sea::domain::FieldType::Bool: {
                if (!value.is_boolean()) {
                    throw sea::sea_errors_handling::RUNTIME_EXECUTION("[RUNTIME EXECUTION] Field '" + field.name + "' must be a boolean.");
                }
                record[field.name] = value.get<bool>();
                break;
            }
            case sea::domain::FieldType::Binary: {
                if (!value.is_string()) {
                    throw sea::sea_errors_handling::RUNTIME_EXECUTION(
                        "[RUNTIME EXECUTION] Field '" + field.name + "' must be a base64 string."
                        );
                }

                const auto encoded = value.get<std::string>();

                auto bytes =
                    sea::infrastructure::persistence::utilities::base64_decode(encoded);

                record[field.name] = bytes;
                break;
            }
            case sea::domain::FieldType::Native: {
                record[field.name] = NativeValue{value};
                break;
            }
            }
        } catch (const sea::sea_errors_handling::RUNTIME_EXECUTION&) {
            // Une erreur de type sur un champ doit faire échouer tout
            // le parsing : on ne masque pas un champ invalide en
            // l'omettant silencieusement du record. Le message porté
            // par l'exception identifie déjà le champ fautif ; on le
            // propage tel quel pour que le handler réponde 400.
            throw;
        }
    }

    return record;
}

} // namespace sea::infrastructure::runtime