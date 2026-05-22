#pragma once

#include <QObject>

class TestDomainModels : public QObject
{
    Q_OBJECT

private slots:
    // DatabaseConfig
    void databaseConfig_defaultShouldUseMemory();
    void databaseConfig_mysqlShouldRequireNetworkConnection();
    void databaseConfig_externalDbWithoutConnectionInfoShouldBeInvalid();
    void databaseConfig_migrationModeFromStringShouldBeCaseInsensitive();
    void databaseConfig_seedsModeFromStringShouldBeCaseInsensitive();

    // FieldType
    void fieldType_toStringShouldReturnExpectedValues();
    void fieldType_fromStringShouldBeCaseInsensitive();
    void fieldType_helpersShouldClassifyTypes();

    // Field
    void field_makeFieldShouldInitializeNameAndType();
    void field_defaultValueShouldSetHasDefault();
    void field_hiddenShouldDisableSerialization();
    void field_renamedFromShouldSetPreviousName();
    void field_asFileShouldConfigureFileField();

    // FileFieldConfig
    void fileFieldConfig_defaultShouldAcceptEverything();
    void fileFieldConfig_mimeWhitelistShouldFilterValues();
    void fileFieldConfig_extensionWhitelistShouldBeCaseInsensitive();
    void fileFieldConfig_sizeLimitShouldRejectTooLargeFile();
    void fileFieldConfig_onDeleteFromStringShouldParseValidValues();

    // FileMetadata
    void fileMetadata_defaultReferenceCountShouldBeOrphan();
    void fileMetadata_positiveReferenceCountShouldNotBeOrphan();

    // Pagination
    void paginationConfig_emptyShouldHaveNoMode();
    void paginationConfig_pageModeShouldBeDetected();
    void paginationConfig_multipleModesShouldBeDetected();

    // Relation
    void relation_toStringShouldReturnExpectedValues();
    void relation_helpersShouldMatchRelationKind();

    // Entity
    void entity_routePrefixShouldPluralizeSimpleNames();
    void entity_routePrefixShouldConvertYToIes();
    void entity_findFieldShouldReturnExistingField();
    void entity_serializableFieldsShouldExcludeHiddenFields();
    void entity_findRelationShouldReturnExistingRelation();
    void entity_paginationHelpersShouldReflectConfig();
    void entity_hasFileFieldsShouldDetectConfiguredFileField();
    void entity_seedRecordShouldDetectAlias();

    // Schema
    void schema_emptyShouldBeTrueWhenNoEntities();
    void schema_findEntityShouldReturnExistingEntity();
    void schema_crudEntitiesShouldReturnOnlyCrudEnabledEntities();
    void schema_authEntitiesShouldReturnOnlyAuthSources();
    void schema_hasFileFieldsShouldDetectFileFields();

    // Service
    void service_defaultPortShouldBeValid();
    void service_hasEntitiesShouldReflectSchema();
    void service_databaseHelpersShouldReflectDatabaseType();
    void service_findEntityShouldDelegateToSchema();
    void service_hasFileFieldsShouldDelegateToSchema();

    // Project
    void project_emptyShouldBeTrueWhenNoServices();
    void project_findServiceShouldReturnExistingService();
    void project_memoryServicesShouldReturnOnlyMemoryServices();
    void project_externalDbServicesShouldReturnOnlyExternalServices();

    // StorageConfig
    void storageConfig_defaultShouldUseFilesystem();
    void storageConfig_rootPathShouldBeDetected();
};