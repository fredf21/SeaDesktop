#pragma once

#include <QObject>

class TestAccessControlCore : public QObject
{
    Q_OBJECT

private slots:
    // DefaultPolicy / AccessControlConfig
    void defaultPolicy_toString_shouldReturnExpectedValues();
    void defaultPolicy_fromString_shouldBeCaseInsensitive();
    void accessControlConfig_disabled_shouldValidateAsNoop();
    void accessControlConfig_safeDefaults_shouldBeValid();
    void accessControlConfig_emptyRolesClaimName_shouldBeInvalid();
    void accessControlConfig_emptyAdminRole_shouldBeInvalid();
    void accessControlConfig_adminRoleNotDeclared_shouldBeInvalid();
    void accessControlConfig_roleDeclared_shouldRespectDeclaredRoles();

    // CrudOperation
    void crudOperation_toString_shouldReturnExpectedValues();
    void crudOperation_fromString_shouldBeCaseInsensitive();
    void crudOperation_fromString_getAliasShouldReturnGetById();
    void crudOperation_fromString_invalidShouldReturnNullopt();

    // PolicyConditionType
    void policyConditionType_toString_shouldReturnExpectedValues();
    void policyConditionType_fromString_shouldSupportAliases();

    // PolicyCondition
    void policyCondition_defaultShouldBeEmptyPredicate();
    void policyCondition_predicateConstructor_shouldCreatePredicate();
    void policyCondition_allOf_shouldCreateCompositeCondition();
    void policyCondition_anyOf_shouldCreateCompositeCondition();
    void policyCondition_notOf_shouldCreateSingleChildCondition();
    void policyCondition_addChildToPredicate_shouldThrow();
    void policyCondition_addSecondChildToNot_shouldThrow();
    void policyCondition_validateEmptyPredicate_shouldThrow();
    void policyCondition_validateValidComposite_shouldNotThrow();

    // AccessControlSpec
    void accessControlSpec_defaultShouldBeEmpty();
    void accessControlSpec_withSubjectOnlyPredicate_shouldNotRequireResource();
    void accessControlSpec_withResourcePredicate_shouldRequireResource();

    // EntityAccessControl
    void entityAccessControl_defaultShouldHaveNoSpec();
    void entityAccessControl_setSpec_shouldStoreSpec();
    void entityAccessControl_scopeAndOwnerFields_shouldBeStored();
    void entityAccessControl_abacModeOverride_shouldBeSetAndCleared();

    // EvaluationOptions
    void evaluationOptions_production_shouldUseBoolOnlyAndShortCircuit();
    void evaluationOptions_staging_shouldUseReasonAndShortCircuit();
    void evaluationOptions_development_shouldUseStrictVerboseNoShortCircuit();
    void evaluationOptions_preHandler_shouldIgnoreResourceRefs();

    // EvaluationResult
    void evaluationResult_formatAllowedWithoutReason_shouldBeReadable();
    void evaluationResult_formatDeniedWithReason_shouldIncludeReason();
    void evaluationResult_formatVerboseTrace_shouldIncludeTrace();

    // PolicyCompiler
    void policyCompiler_compileOneWithoutRegex_shouldReturnEmptyCache();
    void policyCompiler_compileOneWithRegex_shouldCompilePattern();
    void policyCompiler_compileOneWithInvalidRegex_shouldThrow();

    // OperatorEvaluator
    void operatorEvaluator_withRegisteredStrategy_shouldEvaluate();
    void operatorEvaluator_withEmptyLeftForEquals_shouldReturnFalse();

    // PolicyOperator
    void policyOperator_toString_shouldReturnExpectedValues();
    void policyOperator_fromString_shouldSupportAliases();
    void policyOperator_fromString_invalidShouldReturnNullopt();

    // PolicyValueSource
    void policyValueSource_toString_shouldReturnExpectedValues();
    void policyValueSource_fromString_shouldBeCaseInsensitive();

    // PolicySubject / PolicyResource / PolicyContext
    void policySubject_hasRole_shouldDetectExistingRole();
    void policySubject_getAttribute_shouldReturnExistingAttribute();
    void policyResource_empty_shouldReflectContent();
    void policyResource_getAttribute_shouldReturnExistingAttribute();
    void policyContext_getAttribute_shouldReturnExistingAttribute();

    // PolicyValueRef / PolicyPredicate
    void policyValueRef_factories_shouldInitializeCorrectSourceAndPath();
    void policyPredicate_make_shouldInitializeFields();

    // ValueResolver
    void valueResolver_literalScalar_shouldResolveScalar();
    void valueResolver_literalList_shouldResolveList();
    void valueResolver_subjectDirectFields_shouldResolveValues();
    void valueResolver_subjectRoles_shouldResolveList();
    void valueResolver_subjectAttribute_shouldResolveScalar();
    void valueResolver_resourceDirectFields_shouldResolveValues();
    void valueResolver_resourceAttribute_shouldResolveScalar();
    void valueResolver_contextDirectFields_shouldResolveValues();
    void valueResolver_contextAttributePath_shouldResolveScalar();
    void valueResolver_missingPathPermissive_shouldReturnEmpty();
    void valueResolver_missingPathStrict_shouldThrow();

    // PolicyEngine
    void policyEngine_simpleSubjectPredicate_shouldAllow();
    void policyEngine_simpleSubjectPredicate_shouldDenyWithReason();
    void policyEngine_allCondition_shouldRequireAllChildren();
    void policyEngine_anyCondition_shouldAllowWhenOneChildMatches();
    void policyEngine_notCondition_shouldInvertChild();
    void policyEngine_subjectOnly_shouldIgnoreResourcePredicate();
    void policyEngine_verbose_shouldAddPredicateTrace();
};