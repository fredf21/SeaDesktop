#include "test_yaml_schema_parser.h"

#include "exception_handling.h"
#include "yaml/yaml_schema_parser.h"

#include <cstdint>
#include <cstdlib>

using sea::infrastructure::yaml::YamlSchemaParser;
using YamlError = sea::sea_errors_handling::YamlParsingException;

// ═════════════════════════════════════════════════════════════
// 1. Document racine / project
// ═════════════════════════════════════════════════════════════

void TestYamlSchemaParser::parseMinimalProject_shouldSucceed() {
    const QString path = writeTempYaml(R"(
project:
  name: SeaDesktopDemo

services:
  - name: ApiService
    port: 8080
)");

    YamlSchemaParser parser;
    [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());

    QCOMPARE(QString::fromStdString(project.name), QString("SeaDesktopDemo"));
    QCOMPARE(project.services.size(), std::size_t(1));
    QCOMPARE(QString::fromStdString(project.services[0].name), QString("ApiService"));
    QCOMPARE(project.services[0].port, static_cast<std::uint16_t>(8080));
}

void TestYamlSchemaParser::rootMustBeMap_shouldThrow() {
    // Le document racine est une séquence, pas un objet.
    const QString path = writeTempYaml(R"(
- one
- two
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::rootEmptyDocument_shouldThrow() {
    const QString path = writeTempYaml("");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::projectMustBeObject_shouldThrow() {
    const QString path = writeTempYaml(R"(
project: NotAnObject

services:
  - name: ApiService
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::projectWithoutName_shouldThrow() {
    const QString path = writeTempYaml(R"(
project:
  description: missing the name field

services:
  - name: ApiService
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::missingProjectBlock_usesUnnamedProject() {
    // Le bloc project: est optionnel : sans lui, name == "UnnamedProject".
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
)");

    YamlSchemaParser parser;
    [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());

    QCOMPARE(QString::fromStdString(project.name), QString("UnnamedProject"));
}

void TestYamlSchemaParser::missingServices_shouldThrow() {
    const QString path = writeTempYaml(R"(
project:
  name: SeaDesktopDemo
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::servicesMustBeSequence_shouldThrow() {
    const QString path = writeTempYaml(R"(
project:
  name: SeaDesktopDemo

services:
  name: BadService
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::multipleServices_shouldSucceed() {
    const QString path = writeTempYaml(R"(
services:
  - name: ServiceA
    port: 8080
  - name: ServiceB
    port: 8081
  - name: ServiceC
    port: 8082
)");

    YamlSchemaParser parser;
    [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());

    QCOMPARE(project.services.size(), std::size_t(3));
    QCOMPARE(QString::fromStdString(project.services[1].name), QString("ServiceB"));
    QCOMPARE(project.services[2].port, static_cast<std::uint16_t>(8082));
}

// ═════════════════════════════════════════════════════════════
// 2. Service
// ═════════════════════════════════════════════════════════════

void TestYamlSchemaParser::serviceWithoutName_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - port: 8080
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::serviceNotAnObject_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - JustAString
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::invalidServicePortTooHigh_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    port: 70000
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::invalidServicePortZero_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    port: 0
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::servicePortDefault_shouldBe8080() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
)");

    YamlSchemaParser parser;
    [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());

    QCOMPARE(project.services[0].port, static_cast<std::uint16_t>(8080));
}

void TestYamlSchemaParser::servicePortBoundaryLow_shouldSucceed() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    port: 1
)");

    YamlSchemaParser parser;
    [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());

    QCOMPARE(project.services[0].port, static_cast<std::uint16_t>(1));
}

void TestYamlSchemaParser::servicePortBoundaryHigh_shouldSucceed() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    port: 65535
)");

    YamlSchemaParser parser;
    [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());

    QCOMPARE(project.services[0].port, static_cast<std::uint16_t>(65535));
}

void TestYamlSchemaParser::databaseNotAnObject_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    database: mysql
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::storageNotAnObject_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    storage: filesystem
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::securityNotAnObject_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    security: enabled
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

// ═════════════════════════════════════════════════════════════
// 3. Entity
// ═════════════════════════════════════════════════════════════

void TestYamlSchemaParser::parseEntityWithFields_shouldSucceed() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: User
        fields:
          - name: id
            type: uuid
            required: true
            unique: true

          - name: email
            type: email
            required: true
            unique: true

          - name: password
            type: password
            required: true
)");

    YamlSchemaParser parser;
    [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());

    const auto& service = project.services[0];
    QCOMPARE(service.schema.entities.size(), std::size_t(1));

    const auto& user = service.schema.entities[0];
    QCOMPARE(QString::fromStdString(user.name), QString("User"));
    QCOMPARE(QString::fromStdString(user.table_name), QString("users"));
    QCOMPARE(user.fields.size(), std::size_t(3));

    QCOMPARE(QString::fromStdString(user.fields[0].name), QString("id"));
    QCOMPARE(user.fields[0].required, true);
    QCOMPARE(user.fields[0].unique, true);

    QCOMPARE(QString::fromStdString(user.fields[2].name), QString("password"));
    QCOMPARE(user.fields[2].serializable, false);
}

void TestYamlSchemaParser::entityWithoutName_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - fields:
          - name: id
            type: uuid
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::entityNotAnObject_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - JustAString
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::entityCustomTableName_shouldOverrideDefault() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: Person
        table_name: t_people
        fields:
          - name: id
            type: uuid
)");

    YamlSchemaParser parser;
    [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());

    QCOMPARE(QString::fromStdString(project.services[0].schema.entities[0].table_name),
             QString("t_people"));
}

void TestYamlSchemaParser::entityDerivedTableName_shouldBePlural() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: Category
        fields:
          - name: id
            type: uuid
)");

    YamlSchemaParser parser;
    [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());

    // Aucune table_name explicite : le parser dérive le pluriel.
    const auto table = project.services[0].schema.entities[0].table_name;
    QVERIFY(!table.empty());
    QVERIFY(QString::fromStdString(table) != QString("Category"));
}

void TestYamlSchemaParser::entityOptions_shouldBeParsed() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: User
        options:
          enable_crud: false
          is_auth_source: true
          public_routes: true
          enable_websocket: true
          soft_delete: true
          timestamps: false
        fields:
          - name: id
            type: uuid
)");

    YamlSchemaParser parser;
    [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());

    const auto& opts = project.services[0].schema.entities[0].options;
    QCOMPARE(opts.enable_crud, false);
    QCOMPARE(opts.is_auth_source, true);
    QCOMPARE(opts.public_routes, true);
    QCOMPARE(opts.enable_websocket, true);
    QCOMPARE(opts.soft_delete, true);
    QCOMPARE(opts.timestamps, false);
}

void TestYamlSchemaParser::entityOptionsNotAnObject_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: User
        options: enabled
        fields:
          - name: id
            type: uuid
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::fieldsNotASequence_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: User
        fields:
          name: id
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::relationsNotASequence_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: User
        relations:
          name: roles
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::seedsNotASequence_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: User
        seeds:
          name: admin
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::paginationNotAMap_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: User
        pagination:
          - page
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

// ═════════════════════════════════════════════════════════════
// 4. Field
// ═════════════════════════════════════════════════════════════

void TestYamlSchemaParser::fieldWithoutName_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: User
        fields:
          - type: uuid
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::fieldWithoutType_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: User
        fields:
          - name: id
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::fieldNotAnObject_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: User
        fields:
          - justastring
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::unknownFieldType_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: User
        fields:
          - name: custom
            type: unknown_type
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::allSimpleFieldTypes_shouldSucceed() {
    // Couvre tous les types acceptés par field_type_from_string.
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: AllTypes
        fields:
          - name: f_string
            type: string
          - name: f_int
            type: int
          - name: f_float
            type: float
          - name: f_bool
            type: bool
          - name: f_timestamp
            type: timestamp
          - name: f_uuid
            type: uuid
          - name: f_password
            type: password
          - name: f_email
            type: email
          - name: f_text
            type: text
          - name: f_bigint
            type: bigint
          - name: f_smallint
            type: smallint
          - name: f_decimal
            type: decimal
          - name: f_json
            type: json
          - name: f_binary
            type: binary
)");

    YamlSchemaParser parser;
    [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());

    QCOMPARE(project.services[0].schema.entities[0].fields.size(), std::size_t(14));
}

void TestYamlSchemaParser::fieldRequiredUniqueIndexed_shouldBeParsed() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: User
        fields:
          - name: email
            type: email
            required: true
            unique: true
            indexed: true
)");

    YamlSchemaParser parser;
    [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());

    const auto& field = project.services[0].schema.entities[0].fields[0];
    QCOMPARE(field.required, true);
    QCOMPARE(field.unique, true);
    QCOMPARE(field.indexed, true);
}

void TestYamlSchemaParser::passwordFieldNotSerializableByDefault_shouldBeParsed() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: User
        fields:
          - name: password
            type: password
)");

    YamlSchemaParser parser;
    [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());

    QCOMPARE(project.services[0].schema.entities[0].fields[0].serializable, false);
}

void TestYamlSchemaParser::passwordFieldExplicitSerializable_shouldBeParsed() {
    // Si serializable est explicitement fourni, le parser le respecte.
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: User
        fields:
          - name: password
            type: password
            serializable: true
)");

    YamlSchemaParser parser;
    [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());

    QCOMPARE(project.services[0].schema.entities[0].fields[0].serializable, true);
}

void TestYamlSchemaParser::fieldMaxLength_shouldBeParsed() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: User
        fields:
          - name: name
            type: string
            max_length: 255
)");

    YamlSchemaParser parser;
    verifyNoThrow([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::fieldMinMaxValueInteger_shouldBeParsed() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: Product
        fields:
          - name: quantity
            type: int
            min_value: 0
            max_value: 1000
)");

    YamlSchemaParser parser;
    verifyNoThrow([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::fieldMinMaxValueFloat_shouldBeParsed() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: Product
        fields:
          - name: rating
            type: float
            min_value: 0.0
            max_value: 5.5
)");

    YamlSchemaParser parser;
    verifyNoThrow([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::fieldDefaultString_shouldBeParsed() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: User
        fields:
          - name: status
            type: string
            default: active
)");

    YamlSchemaParser parser;
    verifyNoThrow([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::fieldDefaultInteger_shouldBeParsed() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: User
        fields:
          - name: login_count
            type: int
            default: 0
)");

    YamlSchemaParser parser;
    verifyNoThrow([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::fieldDefaultBool_shouldBeParsed() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: User
        fields:
          - name: is_active
            type: bool
            default: true
)");

    YamlSchemaParser parser;
    verifyNoThrow([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::fieldDefaultOnBinary_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: Blob
        fields:
          - name: data
            type: binary
            default: somevalue
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::fieldDefaultOnFile_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: Document
        fields:
          - name: attachment
            type: file
            default: somefile
            file:
              storage_path: docs
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::fieldPreviousName_shouldBeParsed() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: User
        fields:
          - name: full_name
            previous_name: name
            type: string
)");

    YamlSchemaParser parser;
    [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());

    const auto& field = project.services[0].schema.entities[0].fields[0];
    QVERIFY(field.previous_name.has_value());
    QCOMPARE(QString::fromStdString(*field.previous_name), QString("name"));
}

void TestYamlSchemaParser::fieldUnsignedValue_shouldBeParsed() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: Product
        fields:
          - name: stock
            type: int
            unsigned_value: true
)");

    YamlSchemaParser parser;
    [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());

    QCOMPARE(project.services[0].schema.entities[0].fields[0].unsigned_value, true);
}

void TestYamlSchemaParser::nativeNodeOnNonNativeType_shouldThrow() {
    // Un sous-bloc native: sur un champ non-native est une incohérence.
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: User
        fields:
          - name: id
            type: string
            native:
              dialect: mysql
              type: VARCHAR(36)
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

// ═════════════════════════════════════════════════════════════
// 5. File field
// ═════════════════════════════════════════════════════════════

void TestYamlSchemaParser::fileFieldWithoutFileBlock_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: Document
        fields:
          - name: attachment
            type: file
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::fileBlockOnNonFileField_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: Document
        fields:
          - name: title
            type: string
            file:
              storage_path: documents
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::fileBlockNotAnObject_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: Document
        fields:
          - name: attachment
            type: file
            file: notanobject
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::fileFieldComplete_shouldSucceed() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: User
        fields:
          - name: avatar
            type: file
            file:
              max_size: 5MB
              allowed_mime_types:
                - image/png
                - image/jpeg
              allowed_extensions:
                - .png
                - .jpg
              storage_path: users/avatars
              on_delete: cascade
)");

    YamlSchemaParser parser;
    [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());

    const auto& field = project.services[0].schema.entities[0].fields[0];
    QVERIFY(field.file_config.has_value());
    QCOMPARE(QString::fromStdString(field.file_config->storage_path),
             QString("users/avatars"));
}

void TestYamlSchemaParser::fileFieldMaxSizeZero_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: Document
        fields:
          - name: attachment
            type: file
            file:
              max_size: 0
              storage_path: docs
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::fileFieldInvalidMimeType_shouldThrow() {
    // MIME type sans '/' : rejeté.
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: Document
        fields:
          - name: attachment
            type: file
            file:
              allowed_mime_types:
                - imagepng
              storage_path: docs
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::fileFieldEmptyExtension_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: Document
        fields:
          - name: attachment
            type: file
            file:
              allowed_extensions:
                - ""
              storage_path: docs
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::fileFieldExtensionWithSeparator_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: Document
        fields:
          - name: attachment
            type: file
            file:
              allowed_extensions:
                - "img/png"
              storage_path: docs
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::fileFieldExtensionNormalization_shouldSucceed() {
    // "PNG" doit être normalisé en ".png" (point ajouté, lowercase).
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: Document
        fields:
          - name: attachment
            type: file
            file:
              allowed_extensions:
                - PNG
                - .JPG
              storage_path: docs
)");

    YamlSchemaParser parser;
    [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());

    const auto& cfg = project.services[0].schema.entities[0].fields[0].file_config;
    QVERIFY(cfg.has_value());
    QCOMPARE(cfg->allowed_extensions.size(), std::size_t(2));
    QCOMPARE(QString::fromStdString(cfg->allowed_extensions[0]), QString(".png"));
    QCOMPARE(QString::fromStdString(cfg->allowed_extensions[1]), QString(".jpg"));
}

void TestYamlSchemaParser::fileFieldInvalidOnDelete_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: Document
        fields:
          - name: attachment
            type: file
            file:
              storage_path: docs
              on_delete: explode
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::fileFieldOnDeleteValues_shouldSucceed() {
    // cascade, set_null, restrict sont les trois valeurs acceptées.
    for (const char* val : {"cascade", "set_null", "restrict"}) {
        const QString path = writeTempYaml(QString(R"(
services:
  - name: ApiService
    entities:
      - name: Document
        fields:
          - name: attachment
            type: file
            file:
              storage_path: docs
              on_delete: %1
)").arg(val));

        YamlSchemaParser parser;
        verifyNoThrow([&]() {
            [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
        });
    }
}

// ═════════════════════════════════════════════════════════════
// 6. Storage
// ═════════════════════════════════════════════════════════════

void TestYamlSchemaParser::parseStorageConfig_shouldSucceed() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    storage:
      backend: filesystem
      root_path: ./uploads
      file_mode: "0640"
      directory_mode: "0750"
)");

    YamlSchemaParser parser;
    verifyNoThrow([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::invalidStorageBackend_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    storage:
      backend: s3
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::storageBackendCaseInsensitive_shouldSucceed() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    storage:
      backend: FileSystem
)");

    YamlSchemaParser parser;
    verifyNoThrow([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::storageInvalidFileMode_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    storage:
      backend: filesystem
      file_mode: "notoctal"
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::storageMinimalBlock_shouldSucceed() {
    // Un bloc storage vide est valide (défauts appliqués).
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    storage:
      backend: filesystem
)");

    YamlSchemaParser parser;
    verifyNoThrow([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

// ═════════════════════════════════════════════════════════════
// 7. Relations
// ═════════════════════════════════════════════════════════════

void TestYamlSchemaParser::parseRelation_shouldSucceed() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: Department
        fields:
          - name: id
            type: uuid
        relations:
          - name: employees
            kind: has_many
            target_entity: Employee
            fk_column: department_id
            on_delete: cascade
)");

    YamlSchemaParser parser;
    [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());

    const auto& entity = project.services[0].schema.entities[0];
    QCOMPARE(entity.relations.size(), std::size_t(1));
    QCOMPARE(QString::fromStdString(entity.relations[0].name), QString("employees"));
    QCOMPARE(QString::fromStdString(entity.relations[0].target_entity), QString("Employee"));
    QCOMPARE(QString::fromStdString(entity.relations[0].fk_column), QString("department_id"));
    QCOMPARE(entity.relations[0].kind, sea::domain::RelationKind::HasMany);
    QCOMPARE(entity.relations[0].on_delete, sea::domain::OnDelete::Cascade);
}

void TestYamlSchemaParser::relationWithoutName_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: Department
        relations:
          - kind: has_many
            target_entity: Employee
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::relationWithoutTargetEntity_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: Department
        relations:
          - name: employees
            kind: has_many
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::relationWithoutKind_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: Department
        relations:
          - name: employees
            target_entity: Employee
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::relationUnknownKind_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: Department
        relations:
          - name: employees
            kind: has_plenty
            target_entity: Employee
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::relationAllKinds_shouldSucceed() {
    // belongs_to, has_one, has_many sont parsables sans contrainte pivot.
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: Employee
        fields:
          - name: id
            type: uuid
        relations:
          - name: department
            kind: belongs_to
            target_entity: Department
            fk_column: department_id
          - name: badge
            kind: has_one
            target_entity: Badge
            fk_column: employee_id
          - name: tasks
            kind: has_many
            target_entity: Task
            fk_column: employee_id
)");

    YamlSchemaParser parser;
    [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());

    const auto& rels = project.services[0].schema.entities[0].relations;
    QCOMPARE(rels.size(), std::size_t(3));
    QCOMPARE(rels[0].kind, sea::domain::RelationKind::BelongsTo);
    QCOMPARE(rels[1].kind, sea::domain::RelationKind::HasOne);
    QCOMPARE(rels[2].kind, sea::domain::RelationKind::HasMany);
}

void TestYamlSchemaParser::relationInvalidOnDelete_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: Department
        relations:
          - name: employees
            kind: has_many
            target_entity: Employee
            on_delete: nuke
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::manyToManyWithoutPivotTable_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: User
        relations:
          - name: roles
            kind: many_to_many
            target_entity: Role
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::manyToManyWithoutSourceFk_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: User
        relations:
          - name: roles
            kind: many_to_many
            target_entity: Role
            pivot_table: user_roles
            target_fk_column: role_id
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::manyToManyWithoutTargetFk_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: User
        relations:
          - name: roles
            kind: many_to_many
            target_entity: Role
            pivot_table: user_roles
            source_fk_column: user_id
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::manyToManyComplete_shouldSucceed() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: User
        fields:
          - name: id
            type: uuid
        relations:
          - name: roles
            kind: many_to_many
            target_entity: Role
            pivot_table: user_roles
            source_fk_column: user_id
            target_fk_column: role_id
)");

    YamlSchemaParser parser;
    [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());

    const auto& rel = project.services[0].schema.entities[0].relations[0];
    QCOMPARE(rel.kind, sea::domain::RelationKind::ManyToMany);
    QCOMPARE(QString::fromStdString(rel.pivot_table), QString("user_roles"));
    QCOMPARE(QString::fromStdString(rel.source_fk_column), QString("user_id"));
    QCOMPARE(QString::fromStdString(rel.target_fk_column), QString("role_id"));
}

void TestYamlSchemaParser::relationNotAnObject_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: Department
        relations:
          - justastring
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

// ═════════════════════════════════════════════════════════════
// 8. Database
// ═════════════════════════════════════════════════════════════

void TestYamlSchemaParser::parseDatabaseMemoryByDefault_shouldSucceed() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    database:
      type: memory
)");

    YamlSchemaParser parser;
    [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());

    QCOMPARE(project.services[0].database_config.type,
             sea::domain::DatabaseType::Memory);
}

void TestYamlSchemaParser::invalidDatabaseType_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    database:
      type: oracle
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::databaseAllTypes_shouldSucceed() {
    struct Case { const char* yaml; sea::domain::DatabaseType expected; };
    const Case cases[] = {
                          {"memory",     sea::domain::DatabaseType::Memory},
                          {"mysql",      sea::domain::DatabaseType::MySQL},
                          {"postgres",   sea::domain::DatabaseType::PostgreSQL},
                          {"postgresql", sea::domain::DatabaseType::PostgreSQL},
                          {"mongo",      sea::domain::DatabaseType::MongoDB},
                          {"mongodb",    sea::domain::DatabaseType::MongoDB},
                          };

    for (const auto& c : cases) {
        const QString path = writeTempYaml(QString(R"(
services:
  - name: ApiService
    database:
      type: %1
)").arg(c.yaml));

        YamlSchemaParser parser;
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
        QCOMPARE(project.services[0].database_config.type, c.expected);
    }
}

void TestYamlSchemaParser::databaseFullConfig_shouldBeParsed() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    database:
      type: mysql
      host: 127.0.0.1
      port: 3306
      database_name: seademo
      username: seauser
      password: seapass
)");

    YamlSchemaParser parser;
    [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());

    const auto& db = project.services[0].database_config;
    QCOMPARE(db.type, sea::domain::DatabaseType::MySQL);
    QCOMPARE(QString::fromStdString(db.host), QString("127.0.0.1"));
    QCOMPARE(db.port, 3306);
    QCOMPARE(QString::fromStdString(db.database_name), QString("seademo"));
    QCOMPARE(QString::fromStdString(db.username), QString("seauser"));
    QCOMPARE(QString::fromStdString(db.password), QString("seapass"));
}

void TestYamlSchemaParser::migrationsNotAnObject_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    database:
      type: mysql
      migrations: enabled
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::migrationsValidModes_shouldSucceed() {
    for (const char* mode : {"conservative", "modified", "aggressive"}) {
        const QString path = writeTempYaml(QString(R"(
services:
  - name: ApiService
    database:
      type: mysql
      migrations:
        enabled: true
        mode: %1
)").arg(mode));

        YamlSchemaParser parser;
        verifyNoThrow([&]() {
            [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
        });
    }
}

void TestYamlSchemaParser::migrationsInvalidMode_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    database:
      type: mysql
      migrations:
        mode: reckless
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

// ═════════════════════════════════════════════════════════════
// 9. Seeds
// ═════════════════════════════════════════════════════════════

void TestYamlSchemaParser::seedsBasicRecord_shouldSucceed() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: Role
        fields:
          - name: id
            type: uuid
          - name: name
            type: string
        seeds:
          - name: admin
          - name: user
)");

    YamlSchemaParser parser;
    [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());

    QCOMPARE(project.services[0].schema.entities[0].seeds.size(), std::size_t(2));
}

void TestYamlSchemaParser::seedsInvalidMode_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    database:
      type: mysql
      migrations:
        seeds:
          enabled: true
          mode: sometimes
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::seedsInvalidOnError_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    database:
      type: mysql
      migrations:
        seeds:
          enabled: true
          on_error: panic
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::seedRecordNotAnObject_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: Role
        seeds:
          - justastring
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::seedRecordWithAlias_shouldBeParsed() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: Role
        fields:
          - name: id
            type: uuid
          - name: name
            type: string
        seeds:
          - alias: role_admin
            name: admin
)");

    YamlSchemaParser parser;
    [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());

    const auto& seed = project.services[0].schema.entities[0].seeds[0];
    QCOMPARE(QString::fromStdString(seed.alias), QString("role_admin"));
}

void TestYamlSchemaParser::seedRecordM2MNotASequence_shouldThrow() {
    // Le champ M2M dans un seed doit être une liste d'aliases.
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: User
        fields:
          - name: id
            type: uuid
        relations:
          - name: roles
            kind: many_to_many
            target_entity: Role
            pivot_table: user_roles
            source_fk_column: user_id
            target_fk_column: role_id
        seeds:
          - alias: user_admin
            roles: not_a_list
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::seedRecordM2MWithAliases_shouldBeParsed() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: User
        fields:
          - name: id
            type: uuid
        relations:
          - name: roles
            kind: many_to_many
            target_entity: Role
            pivot_table: user_roles
            source_fk_column: user_id
            target_fk_column: role_id
        seeds:
          - alias: user_admin
            roles:
              - role_admin
              - role_user
)");

    YamlSchemaParser parser;
    [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());

    const auto& seed = project.services[0].schema.entities[0].seeds[0];
    QVERIFY(seed.m2m_relations.count("roles") == 1);
    QCOMPARE(seed.m2m_relations.at("roles").size(), std::size_t(2));
}

// ═════════════════════════════════════════════════════════════
// 10. Pagination
// ═════════════════════════════════════════════════════════════

void TestYamlSchemaParser::parsePaginationPage_shouldSucceed() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: Employee
        fields:
          - name: id
            type: uuid
        pagination:
          page:
            default_page_size: 20
            max_page_size: 100
            default_sort: name
            sortable_fields:
              - name
              - created_at
)");

    YamlSchemaParser parser;
    [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());

    const auto& pag_opt = project.services[0].schema.entities[0].pagination;
    QVERIFY(pag_opt.has_value());
    const auto& pag = *pag_opt;
    QVERIFY(pag.has_page());
    QCOMPARE(pag.page->default_page_size, std::size_t(20));
    QCOMPARE(pag.page->max_page_size, std::size_t(100));
}

void TestYamlSchemaParser::emptyPagination_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: Employee
        pagination: {}
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::paginationOffset_shouldSucceed() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: Employee
        fields:
          - name: id
            type: uuid
        pagination:
          offset:
            default_limit: 25
            max_limit: 200
            default_sort: created_at
            sortable_fields:
              - created_at
)");

    YamlSchemaParser parser;
    [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());

    const auto& pag_opt = project.services[0].schema.entities[0].pagination;
    QVERIFY(pag_opt.has_value());
    const auto& pag = *pag_opt;
    QVERIFY(pag.has_offset());
    QCOMPARE(pag.offset->default_limit, std::size_t(25));
    QCOMPARE(pag.offset->max_limit, std::size_t(200));
}

void TestYamlSchemaParser::paginationCursor_shouldSucceed() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: Employee
        fields:
          - name: id
            type: uuid
        pagination:
          cursor:
            default_limit: 30
            max_limit: 100
            cursor_field: id
            sort: id:asc
)");

    YamlSchemaParser parser;
    [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());

    const auto& pag_opt = project.services[0].schema.entities[0].pagination;
    QVERIFY(pag_opt.has_value());
    const auto& pag = *pag_opt;
    QVERIFY(pag.has_cursor());
    QCOMPARE(QString::fromStdString(pag.cursor->cursor_field), QString("id"));
    QCOMPARE(QString::fromStdString(pag.cursor->sort), QString("id:asc"));
}

void TestYamlSchemaParser::paginationCursorWithoutCursorField_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: Employee
        pagination:
          cursor:
            sort: id:asc
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::paginationCursorWithoutSort_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: Employee
        pagination:
          cursor:
            cursor_field: id
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::paginationAllModes_shouldSucceed() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: Employee
        fields:
          - name: id
            type: uuid
        pagination:
          page:
            default_page_size: 20
          offset:
            default_limit: 20
          cursor:
            cursor_field: id
            sort: id:asc
)");

    YamlSchemaParser parser;
    [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());

    const auto& pag_opt = project.services[0].schema.entities[0].pagination;
    QVERIFY(pag_opt.has_value());
    const auto& pag = *pag_opt;
    QVERIFY(pag.has_page());
    QVERIFY(pag.has_offset());
    QVERIFY(pag.has_cursor());
}

void TestYamlSchemaParser::paginationPageNotAMap_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: Employee
        pagination:
          page: 20
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

// ═════════════════════════════════════════════════════════════
// 11. Security
// ═════════════════════════════════════════════════════════════

void TestYamlSchemaParser::securityNoBlock_disabledByDefault() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
)");

    YamlSchemaParser parser;
    verifyNoThrow([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::securityAuthNone_shouldSucceed() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    security:
      authentication:
        type: none
)");

    YamlSchemaParser parser;
    verifyNoThrow([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::securityAuthJwtComplete_shouldSucceed() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    security:
      authentication:
        type: jwt
        algorithm: HS256
        secret: a_very_long_secret_key_at_least_32c
        issuer: sea-desktop
        audience: sea-clients
        access_token_ttl: 15m
        refresh_token_ttl: 7d
)");

    YamlSchemaParser parser;
    verifyNoThrow([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::securityAuthJwtShortSecret_shouldThrow() {
    // Un secret JWT symétrique de moins de 32 caractères est rejeté
    // par la validation finale de SecurityConfig.
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    security:
      authentication:
        type: jwt
        algorithm: HS256
        secret: tooshort
        access_token_ttl: 15m
        refresh_token_ttl: 7d
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::securityAuthInvalidType_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    security:
      authentication:
        type: telepathy
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::securityAuthInvalidJwtAlgorithm_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    security:
      authentication:
        type: jwt
        algorithm: XX999
        secret: a_very_long_secret_key_at_least_32c
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::securityRateLimitComplete_shouldSucceed() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    security:
      rate_limits:
        - scope: per_ip
          requests: 100
          window: 1m
          burst: 150
        - scope: global
          requests: 1000
          window: 1h
)");

    YamlSchemaParser parser;
    verifyNoThrow([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::securityRateLimitWithoutRequests_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    security:
      rate_limits:
        - scope: per_ip
          window: 1m
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::securityRateLimitWithoutWindow_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    security:
      rate_limits:
        - scope: per_ip
          requests: 100
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::securityRateLimitInvalidScope_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    security:
      rate_limits:
        - scope: per_galaxy
          requests: 100
          window: 1m
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::securityRateLimitsNotASequence_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    security:
      rate_limits:
        scope: per_ip
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::securityHttpLimits_shouldSucceed() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    security:
      http_limits:
        max_body_size: 10MB
        max_header_size: 8KB
        max_headers_count: 100
        max_url_length: 2KB
        max_query_params: 50
        request_timeout: 30s
        keep_alive_timeout: 60s
        max_connections_per_ip: 20
)");

    YamlSchemaParser parser;
    verifyNoThrow([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::securityHeadersPresets_shouldSucceed() {
    for (const char* preset : {"recommended", "strict", "none"}) {
        const QString path = writeTempYaml(QString(R"(
services:
  - name: ApiService
    security:
      headers:
        preset: %1
)").arg(preset));

        YamlSchemaParser parser;
        verifyNoThrow([&]() {
            [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
        });
    }
}

void TestYamlSchemaParser::securityHeadersInvalidPreset_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    security:
      headers:
        preset: paranoid
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::securityCookieConfig_shouldSucceed() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    security:
      authentication:
        type: jwt
        algorithm: HS256
        secret: a_very_long_secret_key_at_least_32c
        access_token_ttl: 15m
        refresh_token_ttl: 7d
        token_delivery: cookie
        cookie:
          domain: example.com
          path: /
          secure: true
          same_site: strict
          access_token_name: sea_access
          refresh_token_name: sea_refresh
)");

    YamlSchemaParser parser;
    verifyNoThrow([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

// ═════════════════════════════════════════════════════════════
// 12. Authorization / ABAC
// ═════════════════════════════════════════════════════════════

void TestYamlSchemaParser::authorizationDisabled_shouldSucceed() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    security:
      authorization:
        enabled: false
)");

    YamlSchemaParser parser;
    verifyNoThrow([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::authorizationEnabledWithRoles_shouldSucceed() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    security:
      authorization:
        enabled: true
        default_policy: deny
        roles_claim_name: role
        admin_role: admin
        abac_mode: permissive
        roles:
          - admin
          - user
          - guest
)");

    YamlSchemaParser parser;
    verifyNoThrow([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::authorizationInvalidDefaultPolicy_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    security:
      authorization:
        enabled: true
        default_policy: maybe
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::authorizationInvalidAbacMode_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    security:
      authorization:
        enabled: true
        abac_mode: chaotic
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::entityAccessControlAllowRoles_shouldSucceed() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    security:
      authorization:
        enabled: true
        default_policy: deny
        roles:
          - admin
          - user
    entities:
      - name: Document
        fields:
          - name: id
            type: uuid
        access_control:
          list:
            allow_roles:
              - admin
              - user
          delete:
            allow_roles:
              - admin
)");

    YamlSchemaParser parser;
    verifyNoThrow([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::entityAccessControlUnknownOperation_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    security:
      authorization:
        enabled: true
        roles:
          - admin
    entities:
      - name: Document
        fields:
          - name: id
            type: uuid
        access_control:
          teleport:
            allow_roles:
              - admin
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::entityAccessControlUndeclaredRole_shouldThrow() {
    // Un rôle non déclaré dans authorization.roles est rejeté.
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    security:
      authorization:
        enabled: true
        roles:
          - admin
    entities:
      - name: Document
        fields:
          - name: id
            type: uuid
        access_control:
          list:
            allow_roles:
              - superuser
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::entityAccessControlSameScopeWithoutScopeField_shouldThrow() {
    // same_scope nommant explicitement un champ alors qu'aucun scope_field
    // effectif n'est défini : le parser rejette.
    // (Note : same_scope: true sans scope_field est un no-op silencieux ;
    //  c'est la forme string qui force la résolution et donc l'erreur.)
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    security:
      authorization:
        enabled: true
        roles:
          - admin
    entities:
      - name: Document
        fields:
          - name: id
            type: uuid
        access_control:
          list:
            same_scope: tenant_id
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::entityAccessControlOwnResourceWithoutOwnerField_shouldThrow() {
    // own_resource nommant explicitement un champ alors qu'aucun owner_field
    // effectif n'est défini : le parser rejette.
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    security:
      authorization:
        enabled: true
        roles:
          - admin
    entities:
      - name: Document
        fields:
          - name: id
            type: uuid
        access_control:
          update:
            own_resource: created_by
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

// ═════════════════════════════════════════════════════════════
// 13. Logging
// ═════════════════════════════════════════════════════════════

void TestYamlSchemaParser::parseLoggingConfig_shouldSucceed() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    logging:
      enabled: true
      level: info
      modules:
        sea.http: debug
        sea.persistence: warn
      sinks:
        - type: console
          format: text
          enabled: true
        - type: file
          format: json
          enabled: true
          path: ./logs/api.log
          rotation:
            max_size: 100MB
            time_pattern: daily
            max_files: 10
            compress: false
      flush_level: error
      async:
        enabled: true
        queue_size: 8192
        overflow_policy: overrun_oldest
)");

    YamlSchemaParser parser;
    verifyNoThrow([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::invalidLoggingLevel_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    logging:
      level: verbose
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::loggingModulesNotAMap_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    logging:
      modules:
        - sea.http
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::loggingSinksNotASequence_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    logging:
      sinks:
        type: console
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::loggingSinkWithoutType_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    logging:
      sinks:
        - format: text
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::loggingInvalidSinkType_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    logging:
      sinks:
        - type: telegram
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::loggingInvalidFlushLevel_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    logging:
      flush_level: panic
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::loggingInvalidOverflowPolicy_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    logging:
      async:
        enabled: true
        overflow_policy: drop_everything
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::loggingMinimalBlock_shouldSucceed() {
    // Un bloc logging avec juste un level valide : sink console par défaut.
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    logging:
      level: debug
)");

    YamlSchemaParser parser;
    verifyNoThrow([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

// ═════════════════════════════════════════════════════════════
// 14. Helpers (parse_duration / parse_size / resolve_env)
// ═════════════════════════════════════════════════════════════

void TestYamlSchemaParser::durationSuffixes_shouldBeParsed() {
    // parse_duration accepte s, m, h, d. On teste les suffixes via
    // access_token_ttl (qui doit rester <= 24h, contrainte de sécurité)
    // et refresh_token_ttl (qui accepte les durées en jours).
    struct Case { const char* access; const char* refresh; };
    const Case cases[] = {
        {"30s", "1d"},
        {"15m", "7d"},
        {"2h",  "30d"},
        {"45",  "1d"},   // 45 sans suffixe == 45 secondes
    };

    for (const auto& c : cases) {
        const QString path = writeTempYaml(QString(R"(
services:
  - name: ApiService
    security:
      authentication:
        type: jwt
        algorithm: HS256
        secret: a_very_long_secret_key_at_least_32c
        access_token_ttl: %1
        refresh_token_ttl: %2
)").arg(c.access, c.refresh));

        YamlSchemaParser parser;
        verifyNoThrow([&]() {
            [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
        });
    }

}

void TestYamlSchemaParser::invalidDurationSuffix_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    security:
      authentication:
        type: jwt
        algorithm: HS256
        secret: a_very_long_secret_key_at_least_32c
        access_token_ttl: 15y
        refresh_token_ttl: 30d
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::sizeSuffixes_shouldBeParsed() {
    // parse_size accepte B, KB/K, MB/M, GB/G (via http_limits.max_body_size).
    for (const char* size : {"1024", "500KB", "10MB", "1GB", "2048B"}) {
        const QString path = writeTempYaml(QString(R"(
services:
  - name: ApiService
    security:
      http_limits:
        max_body_size: %1
)").arg(size));

        YamlSchemaParser parser;
        verifyNoThrow([&]() {
            [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
        });
    }
}

void TestYamlSchemaParser::invalidSizeSuffix_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    security:
      http_limits:
        max_body_size: 10TB
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::envVariableResolution_shouldSucceed() {
    // resolve_env est utilisé pour le secret JWT.
    qputenv("SEA_TEST_JWT_SECRET", "a_very_long_secret_key_at_least_32c");

    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    security:
      authentication:
        type: jwt
        algorithm: HS256
        secret: ${SEA_TEST_JWT_SECRET}
        access_token_ttl: 15m
        refresh_token_ttl: 7d
)");

    YamlSchemaParser parser;
    verifyNoThrow([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });

    qunsetenv("SEA_TEST_JWT_SECRET");
}

void TestYamlSchemaParser::envVariableMissing_shouldThrow() {
    qunsetenv("SEA_TEST_MISSING_VAR");

    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    security:
      authentication:
        type: jwt
        algorithm: HS256
        secret: ${SEA_TEST_MISSING_VAR}
        access_token_ttl: 15m
        refresh_token_ttl: 7d
)");

    YamlSchemaParser parser;
    verifyThrows<YamlError>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}