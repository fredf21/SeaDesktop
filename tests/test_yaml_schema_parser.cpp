#include "test_yaml_schema_parser.h"

#include "exception_handling.h"
#include "yaml/yaml_schema_parser.h"

#include <cstdint>

void TestYamlSchemaParser::parseMinimalProject_shouldSucceed() {
    const QString path = writeTempYaml(R"(
project:
  name: SeaDesktopDemo

services:
  - name: ApiService
    port: 8080
)");

    sea::infrastructure::yaml::YamlSchemaParser parser;

    [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());

    QCOMPARE(QString::fromStdString(project.name), QString("SeaDesktopDemo"));
    QCOMPARE(project.services.size(), std::size_t(1));
    QCOMPARE(QString::fromStdString(project.services[0].name), QString("ApiService"));
    QCOMPARE(project.services[0].port, static_cast<std::uint16_t>(8080));
}

void TestYamlSchemaParser::missingServices_shouldThrow() {
    const QString path = writeTempYaml(R"(
project:
  name: SeaDesktopDemo
)");

    sea::infrastructure::yaml::YamlSchemaParser parser;

    verifyThrows<sea::sea_errors_handling::YamlParsingException>([&]() {
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

    sea::infrastructure::yaml::YamlSchemaParser parser;

    verifyThrows<sea::sea_errors_handling::YamlParsingException>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::invalidServicePort_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    port: 70000
)");

    sea::infrastructure::yaml::YamlSchemaParser parser;

    verifyThrows<sea::sea_errors_handling::YamlParsingException>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

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

    sea::infrastructure::yaml::YamlSchemaParser parser;

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

    sea::infrastructure::yaml::YamlSchemaParser parser;

    verifyThrows<sea::sea_errors_handling::YamlParsingException>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

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

    sea::infrastructure::yaml::YamlSchemaParser parser;

    [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());

    const auto& entity = project.services[0].schema.entities[0];

    QCOMPARE(entity.relations.size(), std::size_t(1));
    QCOMPARE(QString::fromStdString(entity.relations[0].name), QString("employees"));
    QCOMPARE(QString::fromStdString(entity.relations[0].target_entity), QString("Employee"));
    QCOMPARE(QString::fromStdString(entity.relations[0].fk_column), QString("department_id"));
    QCOMPARE(entity.relations[0].kind, sea::domain::RelationKind::HasMany);
    QCOMPARE(entity.relations[0].on_delete, sea::domain::OnDelete::Cascade);
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

    sea::infrastructure::yaml::YamlSchemaParser parser;

    verifyThrows<sea::sea_errors_handling::YamlParsingException>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::parseDatabaseMemoryByDefault_shouldSucceed() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    database:
      type: memory
)");

    sea::infrastructure::yaml::YamlSchemaParser parser;

    [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());

    const auto& db = project.services[0].database_config;

    QCOMPARE(db.type, sea::domain::DatabaseType::Memory);
}

void TestYamlSchemaParser::invalidDatabaseType_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    database:
      type: oracle
)");

    sea::infrastructure::yaml::YamlSchemaParser parser;

    verifyThrows<sea::sea_errors_handling::YamlParsingException>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

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

    sea::infrastructure::yaml::YamlSchemaParser parser;

    verifyNoThrow([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

void TestYamlSchemaParser::emptyPagination_shouldThrow() {
    const QString path = writeTempYaml(R"(
services:
  - name: ApiService
    entities:
      - name: Employee
        pagination: {}
)");

    sea::infrastructure::yaml::YamlSchemaParser parser;

    verifyThrows<sea::sea_errors_handling::YamlParsingException>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

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

    sea::infrastructure::yaml::YamlSchemaParser parser;

    verifyThrows<sea::sea_errors_handling::YamlParsingException>([&]() {
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

    sea::infrastructure::yaml::YamlSchemaParser parser;

    verifyThrows<sea::sea_errors_handling::YamlParsingException>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

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

    sea::infrastructure::yaml::YamlSchemaParser parser;

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

    sea::infrastructure::yaml::YamlSchemaParser parser;

    verifyThrows<sea::sea_errors_handling::YamlParsingException>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

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

    sea::infrastructure::yaml::YamlSchemaParser parser;

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

    sea::infrastructure::yaml::YamlSchemaParser parser;

    verifyThrows<sea::sea_errors_handling::YamlParsingException>([&]() {
        [[maybe_unused]] auto project = parser.parse_project_file(path.toStdString());
    });
}

QTEST_MAIN(TestYamlSchemaParser)