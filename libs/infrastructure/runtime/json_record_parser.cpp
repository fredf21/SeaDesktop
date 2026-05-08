#include "json_record_parser.h"
#include "persistence/utilities.h"

#include <cstdint>
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
        throw std::runtime_error(std::string("JSON invalide: ") + e.what());
    }

    if (!j.is_object()) {
        throw std::runtime_error("Le body JSON doit etre un objet.");
    }

    DynamicRecord record;

    for (const auto& field : entity.fields) {
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
        case sea::domain::FieldType::Decimal:{
            if (!value.is_string()) {
                throw std::runtime_error("Le champ '" + field.name + "' doit etre une chaine.");
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
                throw std::runtime_error("Le champ '" + field.name + "' doit etre un entier.");
            }
            if(unsignedvalue)
                record[field.name] = static_cast<std::uint64_t>(value.get<std::uint64_t>());
            else record[field.name] = static_cast<std::int64_t>(value.get<std::int64_t>());
            break;
        }
        case sea::domain::FieldType::SmallInt:

        {
            if (!value.is_number_integer()) {
                throw std::runtime_error("Le champ '" + field.name + "' doit etre un entier.");
            }
            if(unsignedvalue){
                if(value.get<std::uint16_t>() < std::numeric_limits<std::uint16_t>::min() ||  value.get<std::uint16_t>() > std::numeric_limits<std::uint16_t>::max()){
                    throw std::runtime_error("Le champ '" + field.name + "' n'est pas dans la plage des tailles d'un SMALLINT Non Signé.");
                }

                else record[field.name] = static_cast<std::uint16_t>(value.get<std::uint16_t>());
            }
            else{
                if(value.get<std::int16_t>() < std::numeric_limits<std::int16_t>::min() ||  value.get<std::int16_t>() > std::numeric_limits<std::int16_t>::max()){
                    throw std::runtime_error("Le champ '" + field.name + "' n'est pas dans la plage des tailles d'un SMALLINT.");
                }
                else record[field.name] = static_cast<std::int16_t>(value.get<std::int16_t>());
            }
            break;
        }

        case sea::domain::FieldType::Int:

            {
                if (!value.is_number_integer()) {
                    throw std::runtime_error("Le champ '" + field.name + "' doit etre un entier.");
                }
                if(unsignedvalue){
                    if(value.get<std::uint32_t>() < std::numeric_limits<std::uint32_t>::min() ||  value.get<std::uint32_t>() > std::numeric_limits<std::uint32_t>::max()){
                        throw std::runtime_error("Le champ '" + field.name + "' n'est pas dans la plage des tailles d'un INT Non Signé.");
                    }

                    else record[field.name] = static_cast<std::uint32_t>(value.get<std::uint32_t>());
                }
                else{
                    if(value.get<std::int32_t>() < std::numeric_limits<std::int32_t>::min() ||  value.get<std::int32_t>() > std::numeric_limits<std::int32_t>::max()){
                        throw std::runtime_error("Le champ '" + field.name + "' n'est pas dans la plage des tailles d'un INT.");
                    }
                    else record[field.name] = static_cast<std::int64_t>(value.get<std::int32_t>());
                }
                break;
            }
        case sea::domain::FieldType::Float: {
            if (!value.is_number()) {
                throw std::runtime_error("Le champ '" + field.name + "' doit etre un nombre.");
            }
            record[field.name] = value.get<double>();
            break;
        }

        case sea::domain::FieldType::Bool: {
            if (!value.is_boolean()) {
                throw std::runtime_error("Le champ '" + field.name + "' doit etre un booleen.");
            }
            record[field.name] = value.get<bool>();
            break;
        }
        case sea::domain::FieldType::Binary: {
            if (!value.is_string()) {
                throw std::runtime_error(
                    "Le champ '" + field.name + "' doit etre une chaine base64."
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
    }

    return record;
}

} // namespace sea::infrastructure::runtime