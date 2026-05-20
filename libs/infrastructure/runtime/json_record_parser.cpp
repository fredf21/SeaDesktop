#include "json_record_parser.h"
#include "exception_handling.h"
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
            case sea::domain::FieldType::Decimal:{
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
                if(unsignedvalue)
                    record[field.name] = static_cast<std::uint64_t>(value.get<std::uint64_t>());
                else record[field.name] = static_cast<std::int64_t>(value.get<std::int64_t>());
                break;
            }
            case sea::domain::FieldType::SmallInt:

                {
                    if (!value.is_number_integer()) {
                        throw sea::sea_errors_handling::RUNTIME_EXECUTION("[RUNTIME EXECUTION] Field '" + field.name + "' must be an integer.");
                    }
                    if(unsignedvalue){
                        if(value.get<std::uint16_t>() < std::numeric_limits<std::uint16_t>::min() ||  value.get<std::uint16_t>() > std::numeric_limits<std::uint16_t>::max()){
                            throw sea::sea_errors_handling::RUNTIME_EXECUTION("[RUNTIME EXECUTION] Field '" + field.name + "' is outside the range of an unsigned SMALLINT.");
                        }

                        else record[field.name] = static_cast<std::uint16_t>(value.get<std::uint16_t>());
                    }
                    else{
                        if(value.get<std::int16_t>() < std::numeric_limits<std::int16_t>::min() ||  value.get<std::int16_t>() > std::numeric_limits<std::int16_t>::max()){
                            throw sea::sea_errors_handling::RUNTIME_EXECUTION("[RUNTIME EXECUTION] Field '" + field.name + "' is outside the range of a SMALLINT.");
                        }
                        else record[field.name] = static_cast<std::int16_t>(value.get<std::int16_t>());
                    }
                    break;
                }

            case sea::domain::FieldType::Int:

                {
                    if (!value.is_number_integer()) {
                        throw sea::sea_errors_handling::RUNTIME_EXECUTION("[RUNTIME EXECUTION] Field '" + field.name + "' must be an integer.");
                    }
                    if(unsignedvalue){
                        if(value.get<std::uint32_t>() < std::numeric_limits<std::uint32_t>::min() ||  value.get<std::uint32_t>() > std::numeric_limits<std::uint32_t>::max()){
                            throw sea::sea_errors_handling::RUNTIME_EXECUTION("[RUNTIME EXECUTION] Field '" + field.name + "' is outside the range of an unsigned INT.");
                        }

                        else record[field.name] = static_cast<std::uint32_t>(value.get<std::uint32_t>());
                    }
                    else{
                        if(value.get<std::int32_t>() < std::numeric_limits<std::int32_t>::min() ||  value.get<std::int32_t>() > std::numeric_limits<std::int32_t>::max()){
                            throw sea::sea_errors_handling::RUNTIME_EXECUTION("[RUNTIME EXECUTION] Field '" + field.name + "' is outside the range of an INT.");
                        }
                        else record[field.name] = static_cast<std::int64_t>(value.get<std::int32_t>());
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
        } catch(const sea::sea_errors_handling::RUNTIME_EXECUTION& e){

        }
    }

    return record;
}

} // namespace sea::infrastructure::runtime