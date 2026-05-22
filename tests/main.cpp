#include <QCoreApplication>
#include <QtTest>

#include "libs/domain/access_control/test_access_control_core.h"
#include "libs/domain/database_mappings/test_mysql_type_mapping.h"
#include "libs/domain/logging/test_logging_config.h"
#include "libs/domain/models/test_domain_models.h"
#include "libs/domain/protocol/http_protocol/test_http_method.h"
#include "libs/infrastructure/persistence/memory/test_in_memory_generic_repository.h"
#include "libs/infrastructure/runtime/test_generic_validator.h"
#include "libs/infrastructure/runtime/test_json_record_parser.h"
#include "libs/infrastructure/runtime/test_schema_runtime_registry.h"
#include "libs/infrastructure/yaml/test_yaml_schema_parser.h"
#include "libs/infrastructure/storage/test_filesystem_storage.h"
#include "libs/infrastructure/security/test_jwt_service.h"
#include "libs/infrastructure/security/test_secret_store.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    int status = 0;

    TestYamlSchemaParser testYamlSchemaParser;
    status |= QTest::qExec(&testYamlSchemaParser, argc, argv);

    TestFilesystemStorage testFilesystemStorage;
    status |= QTest::qExec(&testFilesystemStorage, argc, argv);

    TestJwtService testJwtService;
    status |= QTest::qExec(&testJwtService, argc, argv);

    TestSecretStore testSecretStore;
    status |= QTest::qExec(&testSecretStore, argc, argv);

    TestSchemaRuntimeRegistry testSchemaRuntimeRegistry;
    status |= QTest::qExec(&testSchemaRuntimeRegistry, argc, argv);

    TestGenericValidator testGenericValidator;
    status |= QTest::qExec(&testGenericValidator, argc, argv);

    TestJsonRecordParser testJsonRecordParser;
    status |= QTest::qExec(&testJsonRecordParser, argc, argv);

    TestInMemoryGenericRepository testInMemoryGenericRepository;
    status |= QTest::qExec(&testInMemoryGenericRepository, argc, argv);

    TestDomainModels testDomainModels;
    status |= QTest::qExec(&testDomainModels, argc, argv);

    TestAccessControlCore testAccessControlCore;
    status |= QTest::qExec(&testAccessControlCore, argc, argv);

    TestMysqlTypeMapping testMysqlTypeMapping;
    status |= QTest::qExec(&testMysqlTypeMapping, argc, argv);

    TestLoggingConfig testLoggingConfig;
    status |= QTest::qExec(&testLoggingConfig, argc, argv);

    TestHttpMethod testHttpMethod;
    status |= QTest::qExec(&testHttpMethod, argc, argv);

    return status;
}