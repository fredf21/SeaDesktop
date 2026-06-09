#include "test_access_control_core.h"

#include "access_control/access_control_config.h"
#include "access_control/access_control_spec.h"
#include "access_control/crud_operation.h"
#include "access_control/entity_access_control.h"
#include "access_control/evaluation_result.h"
#include "access_control/operator_evaluator.h"
#include "access_control/policy_condition.h"
#include "access_control/policy_condition_type.h"
#include "access_control/policy_context.h"
#include "access_control/policy_engine.h"
#include "access_control/policy_operator.h"
#include "access_control/policy_predicate.h"
#include "access_control/policy_resource.h"
#include "access_control/policy_subject.h"
#include "access_control/policy_value_ref.h"
#include "access_control/policy_value_source.h"
#include "access_control/value_resolver.h"
#include "access_control/policy_compiler.h"


#include <QtTest>

#include <string>
#include <vector>

using namespace sea::domain::access_control;

namespace {

QString qs(std::string_view value)
{
    return QString::fromStdString(std::string(value));
}

PolicyValueRef literalValue(std::string value)
{
    PolicyValueRef v{};
    v.source = PolicyValueSource::Literal;
    v.literal = std::move(value);
    return v;
}

PolicyValueRef subjectValue(std::string path)
{
    PolicyValueRef v{};
    v.source = PolicyValueSource::Subject;
    v.path = std::move(path);
    return v;
}

PolicyValueRef resourceValue(std::string path)
{
    PolicyValueRef v{};
    v.source = PolicyValueSource::Resource;
    v.path = std::move(path);
    return v;
}

PolicyPredicate equalsSubjectRoleAdmin()
{
    PolicyPredicate p{};
    p.left = subjectValue("role");
    p.op = PolicyOperator::Equals;
    p.right = literalValue("admin");
    return p;
}

PolicyPredicate regexEmailPredicate(std::string pattern)
{
    PolicyPredicate p{};
    p.left = subjectValue("email");
    p.op = PolicyOperator::RegexMatch;
    p.right = literalValue(std::move(pattern));
    return p;
}

sea::domain::access_control::ResolvedValue emptyResolvedValue()
{
    return sea::domain::access_control::ResolvedValue{};
}

sea::domain::access_control::ResolvedValue scalarResolvedValue(std::string value)
{
    sea::domain::access_control::ResolvedValue v{};
    v.scalar = std::move(value);
    return v;
}
PolicySubject makeSubject()
{
    PolicySubject subject;
    subject.id = "user_1";
    subject.email = "admin@example.com";
    subject.roles = { "admin", "manager" };
    subject.attributes["department_id"] = "dept_it";
    subject.attributes["tenant_id"] = "tenant_1";
    return subject;
}

PolicyResource makeResource()
{
    PolicyResource resource;
    resource.entity_name = "Document";
    resource.id = "doc_1";
    resource.attributes["department_id"] = "dept_it";
    resource.attributes["owner_id"] = "user_1";
    return resource;
}

PolicyContext makeContext()
{
    PolicyContext context;
    context.method = "GET";
    context.path = "/documents/doc_1";
    context.ip = "127.0.0.1";
    context.attributes["time.hour"] = "14";
    context.attributes["request.is_secure"] = "true";
    return context;
}

PolicyPredicate makeSubjectRoleContainsAdminPredicate()
{
    return PolicyPredicate::make(
        PolicyValueRef::from_subject("roles"),
        PolicyOperator::Contains,
        PolicyValueRef::from_literal("admin")
        );
}

PolicyPredicate makeSubjectDepartmentEqualsResourceDepartmentPredicate()
{
    return PolicyPredicate::make(
        PolicyValueRef::from_subject("attributes.department_id"),
        PolicyOperator::Equals,
        PolicyValueRef::from_resource("attributes.department_id")
        );
}

PolicyPredicate makeSubjectRoleEqualsGuestPredicate()
{
    return PolicyPredicate::make(
        PolicyValueRef::from_subject("roles"),
        PolicyOperator::Contains,
        PolicyValueRef::from_literal("guest")
        );
}

} // namespace
void TestAccessControlCore::defaultPolicy_toString_shouldReturnExpectedValues()
{
    QCOMPARE(qs(to_string(DefaultPolicy::Deny)), QString("deny"));
    QCOMPARE(qs(to_string(DefaultPolicy::Allow)), QString("allow"));
}

void TestAccessControlCore::defaultPolicy_fromString_shouldBeCaseInsensitive()
{
    QCOMPARE(default_policy_from_string("deny").value(), DefaultPolicy::Deny);
    QCOMPARE(default_policy_from_string("DENY").value(), DefaultPolicy::Deny);
    QCOMPARE(default_policy_from_string("allow").value(), DefaultPolicy::Allow);
    QCOMPARE(default_policy_from_string("ALLOW").value(), DefaultPolicy::Allow);

    QVERIFY(!default_policy_from_string("unknown").has_value());
}

void TestAccessControlCore::accessControlConfig_disabled_shouldValidateAsNoop()
{
    auto cfg = AccessControlConfig::disabled();

    QVERIFY(!cfg.enabled());

    const auto result = cfg.validate();

    QVERIFY(!result.is_valid.has_value());
    QCOMPARE(QString::fromStdString(result.message), QString("rien a valider"));
}

void TestAccessControlCore::accessControlConfig_safeDefaults_shouldBeValid()
{
    auto cfg = AccessControlConfig::safe_defaults();

    QVERIFY(cfg.enabled());
    QCOMPARE(cfg.default_policy(), DefaultPolicy::Deny);
    QCOMPARE(QString::fromStdString(cfg.roles_claim_name()), QString("role"));
    QCOMPARE(QString::fromStdString(cfg.admin_role()), QString("admin"));
    QVERIFY(cfg.default_allow_admin());

    const auto result = cfg.validate();

    QVERIFY(result.is_valid.has_value());
    QVERIFY(result.is_valid.value());
}

void TestAccessControlCore::accessControlConfig_emptyRolesClaimName_shouldBeInvalid()
{
    auto cfg = AccessControlConfig::safe_defaults();
    cfg.set_roles_claim_name("");

    const auto result = cfg.validate();

    QVERIFY(result.is_valid.has_value());
    QVERIFY(!result.is_valid.value());
    QVERIFY(QString::fromStdString(result.message).contains("roles_claim_name"));
}

void TestAccessControlCore::accessControlConfig_emptyAdminRole_shouldBeInvalid()
{
    auto cfg = AccessControlConfig::safe_defaults();
    cfg.set_admin_role("");

    const auto result = cfg.validate();

    QVERIFY(result.is_valid.has_value());
    QVERIFY(!result.is_valid.value());
    QVERIFY(QString::fromStdString(result.message).contains("admin_role"));
}

void TestAccessControlCore::accessControlConfig_adminRoleNotDeclared_shouldBeInvalid()
{
    auto cfg = AccessControlConfig::safe_defaults();
    cfg.set_declared_roles({ "user", "manager" });
    cfg.set_admin_role("admin");

    const auto result = cfg.validate();

    QVERIFY(result.is_valid.has_value());
    QVERIFY(!result.is_valid.value());
    QVERIFY(QString::fromStdString(result.message).contains("not in declared_roles"));
}

void TestAccessControlCore::accessControlConfig_roleDeclared_shouldRespectDeclaredRoles()
{
    auto cfg = AccessControlConfig::safe_defaults();

    QVERIFY(cfg.is_role_declared("anything"));

    cfg.set_declared_roles({ "admin", "user" });

    QVERIFY(cfg.is_role_declared("admin"));
    QVERIFY(cfg.is_role_declared("user"));
    QVERIFY(!cfg.is_role_declared("manager"));
}

void TestAccessControlCore::crudOperation_toString_shouldReturnExpectedValues()
{
    QCOMPARE(qs(to_string(CrudOperation::List)), QString("list"));
    QCOMPARE(qs(to_string(CrudOperation::GetById)), QString("get_by_id"));
    QCOMPARE(qs(to_string(CrudOperation::Create)), QString("create"));
    QCOMPARE(qs(to_string(CrudOperation::Update)), QString("update"));
    QCOMPARE(qs(to_string(CrudOperation::Delete)), QString("delete"));
}

void TestAccessControlCore::crudOperation_fromString_shouldBeCaseInsensitive()
{
    QCOMPARE(crud_operation_from_string("LIST").value(), CrudOperation::List);
    QCOMPARE(crud_operation_from_string("Create").value(), CrudOperation::Create);
    QCOMPARE(crud_operation_from_string("UPDATE").value(), CrudOperation::Update);
    QCOMPARE(crud_operation_from_string("delete").value(), CrudOperation::Delete);
}

void TestAccessControlCore::crudOperation_fromString_getAliasShouldReturnGetById()
{
    QCOMPARE(crud_operation_from_string("get").value(), CrudOperation::GetById);
    QCOMPARE(crud_operation_from_string("get_by_id").value(), CrudOperation::GetById);
}

void TestAccessControlCore::crudOperation_fromString_invalidShouldReturnNullopt()
{
    QVERIFY(!crud_operation_from_string("patch").has_value());
}

void TestAccessControlCore::policyConditionType_toString_shouldReturnExpectedValues()
{
    QCOMPARE(qs(to_string(PolicyConditionType::Predicate)), QString("predicate"));
    QCOMPARE(qs(to_string(PolicyConditionType::All)), QString("all"));
    QCOMPARE(qs(to_string(PolicyConditionType::Any)), QString("any"));
    QCOMPARE(qs(to_string(PolicyConditionType::Not)), QString("not"));
}

void TestAccessControlCore::policyConditionType_fromString_shouldSupportAliases()
{
    QCOMPARE(policy_condition_type_from_string("predicate").value(),
             PolicyConditionType::Predicate);

    QCOMPARE(policy_condition_type_from_string("all").value(),
             PolicyConditionType::All);

    QCOMPARE(policy_condition_type_from_string("and").value(),
             PolicyConditionType::All);

    QCOMPARE(policy_condition_type_from_string("any").value(),
             PolicyConditionType::Any);

    QCOMPARE(policy_condition_type_from_string("or").value(),
             PolicyConditionType::Any);

    QCOMPARE(policy_condition_type_from_string("NOT").value(),
             PolicyConditionType::Not);

    QVERIFY(!policy_condition_type_from_string("xor").has_value());
}

void TestAccessControlCore::policyCondition_defaultShouldBeEmptyPredicate()
{
    PolicyCondition condition;

    QCOMPARE(condition.type(), PolicyConditionType::Predicate);
    QVERIFY(condition.is_empty());
    QVERIFY(!condition.predicate().has_value());
    QVERIFY(condition.children().empty());
}

void TestAccessControlCore::policyCondition_predicateConstructor_shouldCreatePredicate()
{
    PolicyCondition condition(equalsSubjectRoleAdmin());

    QCOMPARE(condition.type(), PolicyConditionType::Predicate);
    QVERIFY(!condition.is_empty());
    QVERIFY(condition.predicate().has_value());
    QVERIFY(condition.children().empty());

    QVERIFY_THROWS_NO_EXCEPTION(condition.validate());
}

void TestAccessControlCore::policyCondition_allOf_shouldCreateCompositeCondition()
{
    PolicyCondition child(equalsSubjectRoleAdmin());

    auto condition = PolicyCondition::all_of({ child });

    QCOMPARE(condition.type(), PolicyConditionType::All);
    QVERIFY(!condition.is_empty());
    QCOMPARE(condition.children().size(), std::size_t(1));

    QVERIFY_THROWS_NO_EXCEPTION(condition.validate());
}

void TestAccessControlCore::policyCondition_anyOf_shouldCreateCompositeCondition()
{
    PolicyCondition child(equalsSubjectRoleAdmin());

    auto condition = PolicyCondition::any_of({ child });

    QCOMPARE(condition.type(), PolicyConditionType::Any);
    QVERIFY(!condition.is_empty());
    QCOMPARE(condition.children().size(), std::size_t(1));

    QVERIFY_THROWS_NO_EXCEPTION(condition.validate());
}

void TestAccessControlCore::policyCondition_notOf_shouldCreateSingleChildCondition()
{
    PolicyCondition child(equalsSubjectRoleAdmin());

    auto condition = PolicyCondition::not_of(child);

    QCOMPARE(condition.type(), PolicyConditionType::Not);
    QVERIFY(!condition.is_empty());
    QCOMPARE(condition.children().size(), std::size_t(1));

    QVERIFY_THROWS_NO_EXCEPTION(condition.validate());
}

void TestAccessControlCore::policyCondition_addChildToPredicate_shouldThrow()
{
    PolicyCondition condition;
    PolicyCondition child(equalsSubjectRoleAdmin());

    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, condition.add_child(child));
}

void TestAccessControlCore::policyCondition_addSecondChildToNot_shouldThrow()
{
    auto condition = PolicyCondition::not_of(
        PolicyCondition(equalsSubjectRoleAdmin())
        );

    QVERIFY_THROWS_EXCEPTION(
        std::invalid_argument,
        condition.add_child(PolicyCondition(equalsSubjectRoleAdmin()))
        );
}

void TestAccessControlCore::policyCondition_validateEmptyPredicate_shouldThrow()
{
    PolicyCondition condition;

    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, condition.validate());
}

void TestAccessControlCore::policyCondition_validateValidComposite_shouldNotThrow()
{
    auto condition = PolicyCondition::all_of({
        PolicyCondition(equalsSubjectRoleAdmin()),
        PolicyCondition(equalsSubjectRoleAdmin())
    });

    QVERIFY_THROWS_NO_EXCEPTION(condition.validate());
}
void TestAccessControlCore::policyOperator_toString_shouldReturnExpectedValues()
{
    QCOMPARE(qs(to_string(PolicyOperator::Equals)), QString("equals"));
    QCOMPARE(qs(to_string(PolicyOperator::NotEquals)), QString("not_equals"));
    QCOMPARE(qs(to_string(PolicyOperator::In)), QString("in"));
    QCOMPARE(qs(to_string(PolicyOperator::NotIn)), QString("not_in"));
    QCOMPARE(qs(to_string(PolicyOperator::Contains)), QString("contains"));
    QCOMPARE(qs(to_string(PolicyOperator::StartsWith)), QString("starts_with"));
    QCOMPARE(qs(to_string(PolicyOperator::EndsWith)), QString("ends_with"));
    QCOMPARE(qs(to_string(PolicyOperator::GreaterThan)), QString("greater_than"));
    QCOMPARE(qs(to_string(PolicyOperator::GreaterThanOrEqual)), QString("greater_than_or_equal"));
    QCOMPARE(qs(to_string(PolicyOperator::LessThan)), QString("less_than"));
    QCOMPARE(qs(to_string(PolicyOperator::LessThanOrEqual)), QString("less_than_or_equal"));
    QCOMPARE(qs(to_string(PolicyOperator::Exists)), QString("exists"));
    QCOMPARE(qs(to_string(PolicyOperator::NotExists)), QString("not_exists"));
    QCOMPARE(qs(to_string(PolicyOperator::RegexMatch)), QString("regex_match"));
    QCOMPARE(qs(to_string(PolicyOperator::Intersects)), QString("intersects"));
}

void TestAccessControlCore::accessControlSpec_defaultShouldBeEmpty()
{
    AccessControlSpec spec;

    QVERIFY(spec.is_empty());
    QVERIFY(!spec.requires_resource());
}

void TestAccessControlCore::accessControlSpec_withSubjectOnlyPredicate_shouldNotRequireResource()
{
    AccessControlSpec spec(
        (PolicyCondition(equalsSubjectRoleAdmin()))
        );

    QVERIFY(!spec.is_empty());
    QVERIFY(!spec.requires_resource());
}

void TestAccessControlCore::accessControlSpec_withResourcePredicate_shouldRequireResource()
{
    PolicyPredicate predicate = PolicyPredicate::make(
        PolicyValueRef::from_subject("attributes.department_id"),
        PolicyOperator::Equals,
        PolicyValueRef::from_resource("attributes.department_id")
        );

    AccessControlSpec spec(
        (PolicyCondition(predicate))
        );

    QVERIFY(!spec.is_empty());
    QVERIFY(spec.requires_resource());
}

void TestAccessControlCore::entityAccessControl_defaultShouldHaveNoSpec()
{
    EntityAccessControl access;

    QVERIFY(!access.has_any_spec());
    QVERIFY(!access.has_spec(CrudOperation::Update));
    QVERIFY(access.find_spec(CrudOperation::Update) == nullptr);
}

void TestAccessControlCore::entityAccessControl_setSpec_shouldStoreSpec()
{
    EntityAccessControl access;

    AccessControlSpec spec(
        (PolicyCondition(equalsSubjectRoleAdmin()))
        );

    access.set_spec(CrudOperation::Update, spec);

    QVERIFY(access.has_any_spec());
    QVERIFY(access.has_spec(CrudOperation::Update));
    QVERIFY(access.find_spec(CrudOperation::Update) != nullptr);
    QVERIFY(access.find_spec(CrudOperation::Delete) == nullptr);

    QVERIFY_THROWS_NO_EXCEPTION(access.validate());
}

void TestAccessControlCore::entityAccessControl_scopeAndOwnerFields_shouldBeStored()
{
    EntityAccessControl access;

    access.set_scope_field("department_id");
    access.set_owner_field("owner_id");

    QCOMPARE(QString::fromStdString(access.scope_field()), QString("department_id"));
    QCOMPARE(QString::fromStdString(access.owner_field()), QString("owner_id"));
}

void TestAccessControlCore::entityAccessControl_abacModeOverride_shouldBeSetAndCleared()
{
    EntityAccessControl access;

    QVERIFY(!access.abac_mode_override().has_value());

    access.set_abac_mode_override(AbacMode::Strict);

    QVERIFY(access.abac_mode_override().has_value());
    QCOMPARE(access.abac_mode_override().value(), AbacMode::Strict);

    access.clear_abac_mode_override();

    QVERIFY(!access.abac_mode_override().has_value());
}

void TestAccessControlCore::evaluationOptions_production_shouldUseBoolOnlyAndShortCircuit()
{
    const auto opts = EvaluationOptions::production();

    QCOMPARE(opts.strict_mode, StrictMode::Permissive);
    QCOMPARE(opts.detail_level, DetailLevel::BoolOnly);
    QVERIFY(opts.short_circuit);
    QVERIFY(!opts.ignore_resource_refs);
}

void TestAccessControlCore::evaluationOptions_staging_shouldUseReasonAndShortCircuit()
{
    const auto opts = EvaluationOptions::staging();

    QCOMPARE(opts.strict_mode, StrictMode::Permissive);
    QCOMPARE(opts.detail_level, DetailLevel::WithReason);
    QVERIFY(opts.short_circuit);
    QVERIFY(!opts.ignore_resource_refs);
}

void TestAccessControlCore::evaluationOptions_development_shouldUseStrictVerboseNoShortCircuit()
{
    const auto opts = EvaluationOptions::development();

    QCOMPARE(opts.strict_mode, StrictMode::Strict);
    QCOMPARE(opts.detail_level, DetailLevel::Verbose);
    QVERIFY(!opts.short_circuit);
    QVERIFY(!opts.ignore_resource_refs);
}

void TestAccessControlCore::evaluationOptions_preHandler_shouldIgnoreResourceRefs()
{
    const auto opts = EvaluationOptions::pre_handler();

    QCOMPARE(opts.strict_mode, StrictMode::Permissive);
    QCOMPARE(opts.detail_level, DetailLevel::WithReason);
    QVERIFY(opts.short_circuit);
    QVERIFY(opts.ignore_resource_refs);
}

void TestAccessControlCore::evaluationResult_formatAllowedWithoutReason_shouldBeReadable()
{
    EvaluationResult result;
    result.allowed = true;
    result.predicates_evaluated = 2;

    const auto log = result.format_for_log();

    QVERIFY(QString::fromStdString(log).contains("ALLOW"));
    QVERIFY(QString::fromStdString(log).contains("predicates_evaluated=2"));
}

void TestAccessControlCore::evaluationResult_formatDeniedWithReason_shouldIncludeReason()
{
    EvaluationResult result;
    result.allowed = false;
    result.predicates_evaluated = 1;
    result.reason = "role mismatch";

    const auto log = result.format_for_log();

    QVERIFY(QString::fromStdString(log).contains("DENY"));
    QVERIFY(QString::fromStdString(log).contains("reason=role mismatch"));
}

void TestAccessControlCore::evaluationResult_formatVerboseTrace_shouldIncludeTrace()
{
    EvaluationResult result;
    result.allowed = false;
    result.predicates_evaluated = 1;

    PredicateTrace trace;
    trace.description = "subject.role equals admin";
    trace.left_resolved = "user";
    trace.right_resolved = "admin";
    trace.result = false;
    trace.error = "not equal";

    result.traces.push_back(trace);

    const auto log = result.format_for_log();

    QVERIFY(QString::fromStdString(log).contains("trace"));
    QVERIFY(QString::fromStdString(log).contains("subject.role equals admin"));
    QVERIFY(QString::fromStdString(log).contains("ERROR: not equal"));
}

void TestAccessControlCore::policyCompiler_compileOneWithoutRegex_shouldReturnEmptyCache()
{
    PolicyCondition condition(equalsSubjectRoleAdmin());

    const auto cache = PolicyCompiler::compile_one(condition);

    QVERIFY(cache.empty());
}

void TestAccessControlCore::policyCompiler_compileOneWithRegex_shouldCompilePattern()
{
    const std::string pattern = R"(^[^@]+@[^@]+\.[^@]+$)";

    PolicyCondition condition(
        regexEmailPredicate(pattern)
        );

    const auto cache = PolicyCompiler::compile_one(condition);

    QCOMPARE(cache.size(), std::size_t(1));
    QVERIFY(cache.find(pattern) != cache.end());
}

void TestAccessControlCore::policyCompiler_compileOneWithInvalidRegex_shouldThrow()
{
    PolicyCondition condition(
        regexEmailPredicate("[invalid")
        );

    QVERIFY_THROWS_EXCEPTION(std::runtime_error, PolicyCompiler::compile_one(condition));
}
void TestAccessControlCore::operatorEvaluator_withRegisteredStrategy_shouldEvaluate()
{
    auto registry = OperatorRegistry::create_default();
    OperatorEvaluator evaluator(registry);

    const bool result = evaluator.evaluate(
        PolicyOperator::Equals,
        scalarResolvedValue("admin"),
        scalarResolvedValue("admin")
        );

    QVERIFY(result);
}

void TestAccessControlCore::operatorEvaluator_withEmptyLeftForEquals_shouldReturnFalse()
{
    auto registry = OperatorRegistry::create_default();
    OperatorEvaluator evaluator(registry);

    const bool result = evaluator.evaluate(
        PolicyOperator::Equals,
        emptyResolvedValue(),
        scalarResolvedValue("admin")
        );

    QVERIFY(!result);
}
void TestAccessControlCore::policyOperator_fromString_shouldSupportAliases()
{
    QCOMPARE(policy_operator_from_string("equals").value(), PolicyOperator::Equals);
    QCOMPARE(policy_operator_from_string("eq").value(), PolicyOperator::Equals);
    QCOMPARE(policy_operator_from_string("==").value(), PolicyOperator::Equals);

    QCOMPARE(policy_operator_from_string("not_equals").value(), PolicyOperator::NotEquals);
    QCOMPARE(policy_operator_from_string("ne").value(), PolicyOperator::NotEquals);
    QCOMPARE(policy_operator_from_string("!=").value(), PolicyOperator::NotEquals);

    QCOMPARE(policy_operator_from_string("gt").value(), PolicyOperator::GreaterThan);
    QCOMPARE(policy_operator_from_string(">").value(), PolicyOperator::GreaterThan);

    QCOMPARE(policy_operator_from_string("gte").value(), PolicyOperator::GreaterThanOrEqual);
    QCOMPARE(policy_operator_from_string(">=").value(), PolicyOperator::GreaterThanOrEqual);

    QCOMPARE(policy_operator_from_string("lt").value(), PolicyOperator::LessThan);
    QCOMPARE(policy_operator_from_string("<").value(), PolicyOperator::LessThan);

    QCOMPARE(policy_operator_from_string("lte").value(), PolicyOperator::LessThanOrEqual);
    QCOMPARE(policy_operator_from_string("<=").value(), PolicyOperator::LessThanOrEqual);

    QCOMPARE(policy_operator_from_string("regex").value(), PolicyOperator::RegexMatch);
    QCOMPARE(policy_operator_from_string("matches").value(), PolicyOperator::RegexMatch);
}
void TestAccessControlCore::policyOperator_fromString_invalidShouldReturnNullopt()
{
    QVERIFY(!policy_operator_from_string("between").has_value());
}

void TestAccessControlCore::policyValueSource_toString_shouldReturnExpectedValues()
{
    QCOMPARE(qs(to_string(PolicyValueSource::Literal)), QString("literal"));
    QCOMPARE(qs(to_string(PolicyValueSource::Subject)), QString("subject"));
    QCOMPARE(qs(to_string(PolicyValueSource::Resource)), QString("resource"));
    QCOMPARE(qs(to_string(PolicyValueSource::Context)), QString("context"));
}

void TestAccessControlCore::policyValueSource_fromString_shouldBeCaseInsensitive()
{
    QCOMPARE(policy_value_source_from_string("literal").value(), PolicyValueSource::Literal);
    QCOMPARE(policy_value_source_from_string("SUBJECT").value(), PolicyValueSource::Subject);
    QCOMPARE(policy_value_source_from_string("Resource").value(), PolicyValueSource::Resource);
    QCOMPARE(policy_value_source_from_string("context").value(), PolicyValueSource::Context);

    QVERIFY(!policy_value_source_from_string("session").has_value());
}

void TestAccessControlCore::policySubject_hasRole_shouldDetectExistingRole()
{
    auto subject = makeSubject();

    QVERIFY(subject.has_role("admin"));
    QVERIFY(subject.has_role("manager"));
    QVERIFY(!subject.has_role("guest"));
}

void TestAccessControlCore::policySubject_getAttribute_shouldReturnExistingAttribute()
{
    auto subject = makeSubject();

    QCOMPARE(subject.get_attribute("department_id").value(), std::string("dept_it"));
    QVERIFY(!subject.get_attribute("missing").has_value());
}

void TestAccessControlCore::policyResource_empty_shouldReflectContent()
{
    PolicyResource resource;

    QVERIFY(resource.is_empty());

    resource.id = "doc_1";

    QVERIFY(!resource.is_empty());
}

void TestAccessControlCore::policyResource_getAttribute_shouldReturnExistingAttribute()
{
    auto resource = makeResource();

    QCOMPARE(resource.get_attribute("owner_id").value(), std::string("user_1"));
    QVERIFY(!resource.get_attribute("missing").has_value());
}

void TestAccessControlCore::policyContext_getAttribute_shouldReturnExistingAttribute()
{
    auto context = makeContext();

    QCOMPARE(context.get_attribute("time.hour").value(), std::string("14"));
    QVERIFY(!context.get_attribute("missing").has_value());
}

void TestAccessControlCore::policyValueRef_factories_shouldInitializeCorrectSourceAndPath()
{
    auto subject = PolicyValueRef::from_subject("roles");
    QCOMPARE(subject.source, PolicyValueSource::Subject);
    QCOMPARE(QString::fromStdString(subject.path), QString("roles"));

    auto resource = PolicyValueRef::from_resource("attributes.owner_id");
    QCOMPARE(resource.source, PolicyValueSource::Resource);
    QCOMPARE(QString::fromStdString(resource.path), QString("attributes.owner_id"));

    auto context = PolicyValueRef::from_context("time.hour");
    QCOMPARE(context.source, PolicyValueSource::Context);
    QCOMPARE(QString::fromStdString(context.path), QString("time.hour"));

    auto literal = PolicyValueRef::from_literal("admin");
    QCOMPARE(literal.source, PolicyValueSource::Literal);
    QCOMPARE(QString::fromStdString(literal.literal), QString("admin"));

    auto list = PolicyValueRef::from_literal_list({ "admin", "manager" });
    QCOMPARE(list.source, PolicyValueSource::Literal);
    QCOMPARE(list.literal_list.size(), std::size_t(2));
}

void TestAccessControlCore::policyPredicate_make_shouldInitializeFields()
{
    auto predicate = PolicyPredicate::make(
        PolicyValueRef::from_subject("roles"),
        PolicyOperator::Contains,
        PolicyValueRef::from_literal("admin")
        );

    QCOMPARE(predicate.left.source, PolicyValueSource::Subject);
    QCOMPARE(predicate.op, PolicyOperator::Contains);
    QCOMPARE(predicate.right.source, PolicyValueSource::Literal);
}

void TestAccessControlCore::valueResolver_literalScalar_shouldResolveScalar()
{
    ValueResolver resolver(EvaluationOptions::production());

    auto value = resolver.resolve(
        PolicyValueRef::from_literal("admin"),
        makeSubject(),
        makeResource(),
        makeContext()
        );

    QVERIFY(value.is_scalar());
    QCOMPARE(*value.scalar, std::string("admin"));
}

void TestAccessControlCore::valueResolver_literalList_shouldResolveList()
{
    ValueResolver resolver(EvaluationOptions::production());

    auto value = resolver.resolve(
        PolicyValueRef::from_literal_list({ "admin", "manager" }),
        makeSubject(),
        makeResource(),
        makeContext()
        );

    QVERIFY(value.is_list());
    QCOMPARE(value.list->size(), std::size_t(2));
    QCOMPARE((*value.list)[0], std::string("admin"));
}

void TestAccessControlCore::valueResolver_subjectDirectFields_shouldResolveValues()
{
    ValueResolver resolver(EvaluationOptions::production());
    auto subject = makeSubject();

    auto id = resolver.resolve(
        PolicyValueRef::from_subject("id"),
        subject,
        makeResource(),
        makeContext()
        );

    auto email = resolver.resolve(
        PolicyValueRef::from_subject("email"),
        subject,
        makeResource(),
        makeContext()
        );

    QVERIFY(id.is_scalar());
    QCOMPARE(*id.scalar, std::string("user_1"));

    QVERIFY(email.is_scalar());
    QCOMPARE(*email.scalar, std::string("admin@example.com"));
}

void TestAccessControlCore::valueResolver_subjectRoles_shouldResolveList()
{
    ValueResolver resolver(EvaluationOptions::production());

    auto roles = resolver.resolve(
        PolicyValueRef::from_subject("roles"),
        makeSubject(),
        makeResource(),
        makeContext()
        );

    QVERIFY(roles.is_list());
    QCOMPARE(roles.list->size(), std::size_t(2));
    QCOMPARE((*roles.list)[0], std::string("admin"));
}

void TestAccessControlCore::valueResolver_subjectAttribute_shouldResolveScalar()
{
    ValueResolver resolver(EvaluationOptions::production());

    auto dept = resolver.resolve(
        PolicyValueRef::from_subject("attributes.department_id"),
        makeSubject(),
        makeResource(),
        makeContext()
        );

    QVERIFY(dept.is_scalar());
    QCOMPARE(*dept.scalar, std::string("dept_it"));
}

void TestAccessControlCore::valueResolver_resourceDirectFields_shouldResolveValues()
{
    ValueResolver resolver(EvaluationOptions::production());
    auto resource = makeResource();

    auto id = resolver.resolve(
        PolicyValueRef::from_resource("id"),
        makeSubject(),
        resource,
        makeContext()
        );

    auto entity = resolver.resolve(
        PolicyValueRef::from_resource("entity_name"),
        makeSubject(),
        resource,
        makeContext()
        );

    QVERIFY(id.is_scalar());
    QCOMPARE(*id.scalar, std::string("doc_1"));

    QVERIFY(entity.is_scalar());
    QCOMPARE(*entity.scalar, std::string("Document"));
}

void TestAccessControlCore::valueResolver_resourceAttribute_shouldResolveScalar()
{
    ValueResolver resolver(EvaluationOptions::production());

    auto owner = resolver.resolve(
        PolicyValueRef::from_resource("attributes.owner_id"),
        makeSubject(),
        makeResource(),
        makeContext()
        );

    QVERIFY(owner.is_scalar());
    QCOMPARE(*owner.scalar, std::string("user_1"));
}

void TestAccessControlCore::valueResolver_contextDirectFields_shouldResolveValues()
{
    ValueResolver resolver(EvaluationOptions::production());
    auto context = makeContext();

    auto method = resolver.resolve(
        PolicyValueRef::from_context("method"),
        makeSubject(),
        makeResource(),
        context
        );

    auto path = resolver.resolve(
        PolicyValueRef::from_context("path"),
        makeSubject(),
        makeResource(),
        context
        );

    auto ip = resolver.resolve(
        PolicyValueRef::from_context("ip"),
        makeSubject(),
        makeResource(),
        context
        );

    QCOMPARE(*method.scalar, std::string("GET"));
    QCOMPARE(*path.scalar, std::string("/documents/doc_1"));
    QCOMPARE(*ip.scalar, std::string("127.0.0.1"));
}

void TestAccessControlCore::valueResolver_contextAttributePath_shouldResolveScalar()
{
    ValueResolver resolver(EvaluationOptions::production());

    auto hour = resolver.resolve(
        PolicyValueRef::from_context("time.hour"),
        makeSubject(),
        makeResource(),
        makeContext()
        );

    QVERIFY(hour.is_scalar());
    QCOMPARE(*hour.scalar, std::string("14"));
}

void TestAccessControlCore::valueResolver_missingPathPermissive_shouldReturnEmpty()
{
    ValueResolver resolver(EvaluationOptions::production());

    auto value = resolver.resolve(
        PolicyValueRef::from_subject("attributes.unknown"),
        makeSubject(),
        makeResource(),
        makeContext()
        );

    QVERIFY(value.is_empty());
}

void TestAccessControlCore::valueResolver_missingPathStrict_shouldThrow()
{
    auto options = sea::domain::access_control::EvaluationOptions::development();
    sea::domain::access_control::ValueResolver resolver(options);

    QVERIFY_THROWS_EXCEPTION(
        std::runtime_error,
        resolver.resolve(
            PolicyValueRef::from_subject("attributes.unknown"),
            makeSubject(),
            makeResource(),
            makeContext()
            )
        );
}
void TestAccessControlCore::policyEngine_simpleSubjectPredicate_shouldAllow()
{
    auto registry = OperatorRegistry::create_default();
    PolicyEngine engine(registry);

    auto condition = PolicyCondition(makeSubjectRoleContainsAdminPredicate());

    auto result = engine.evaluate(
        condition,
        makeSubject(),
        makeResource(),
        makeContext(),
        EvaluationOptions::production()
        );

    QVERIFY(result.allowed);
    QCOMPARE(result.predicates_evaluated, std::size_t(1));
}

void TestAccessControlCore::policyEngine_simpleSubjectPredicate_shouldDenyWithReason()
{
    auto registry = OperatorRegistry::create_default();
    PolicyEngine engine(registry);

    auto condition = PolicyCondition(makeSubjectRoleEqualsGuestPredicate());

    auto result = engine.evaluate(
        condition,
        makeSubject(),
        makeResource(),
        makeContext(),
        EvaluationOptions::staging()
        );

    QVERIFY(!result.allowed);
    QVERIFY(result.reason.has_value());
    QVERIFY(QString::fromStdString(*result.reason).contains("Failed"));
}

void TestAccessControlCore::policyEngine_allCondition_shouldRequireAllChildren()
{
    auto registry = OperatorRegistry::create_default();
    PolicyEngine engine(registry);

    auto condition = PolicyCondition::all_of({
        PolicyCondition(makeSubjectRoleContainsAdminPredicate()),
        PolicyCondition(makeSubjectRoleEqualsGuestPredicate())
    });

    auto result = engine.evaluate(
        condition,
        makeSubject(),
        makeResource(),
        makeContext(),
        EvaluationOptions::production()
        );

    QVERIFY(!result.allowed);
}

void TestAccessControlCore::policyEngine_anyCondition_shouldAllowWhenOneChildMatches()
{
    auto registry = OperatorRegistry::create_default();
    PolicyEngine engine(registry);

    auto condition = PolicyCondition::any_of({
        PolicyCondition(makeSubjectRoleEqualsGuestPredicate()),
        PolicyCondition(makeSubjectRoleContainsAdminPredicate())
    });

    auto result = engine.evaluate(
        condition,
        makeSubject(),
        makeResource(),
        makeContext(),
        EvaluationOptions::production()
        );

    QVERIFY(result.allowed);
}

void TestAccessControlCore::policyEngine_notCondition_shouldInvertChild()
{
    auto registry = OperatorRegistry::create_default();
    PolicyEngine engine(registry);

    auto condition = PolicyCondition::not_of(
        PolicyCondition(makeSubjectRoleEqualsGuestPredicate())
        );

    auto result = engine.evaluate(
        condition,
        makeSubject(),
        makeResource(),
        makeContext(),
        EvaluationOptions::production()
        );

    QVERIFY(result.allowed);
}

void TestAccessControlCore::policyEngine_subjectOnly_shouldIgnoreResourcePredicate()
{
    auto registry = OperatorRegistry::create_default();
    PolicyEngine engine(registry);

    auto condition = PolicyCondition::all_of({
        PolicyCondition(makeSubjectRoleContainsAdminPredicate()),
        PolicyCondition(makeSubjectDepartmentEqualsResourceDepartmentPredicate())
    });

    auto result = engine.evaluate_subject_only(
        condition,
        makeSubject(),
        makeContext(),
        EvaluationOptions::pre_handler()
        );

    QVERIFY(result.allowed);
    QCOMPARE(result.predicates_evaluated, std::size_t(1));
}

void TestAccessControlCore::policyEngine_verbose_shouldAddPredicateTrace()
{
    auto registry = OperatorRegistry::create_default();
    PolicyEngine engine(registry);

    auto condition = PolicyCondition(makeSubjectRoleContainsAdminPredicate());

    auto result = engine.evaluate(
        condition,
        makeSubject(),
        makeResource(),
        makeContext(),
        EvaluationOptions::development()
        );

    QVERIFY(result.allowed);
    QCOMPARE(result.predicates_evaluated, std::size_t(1));
    QCOMPARE(result.traces.size(), std::size_t(1));
    QVERIFY(QString::fromStdString(result.traces[0].description).contains("subject.roles"));
    QVERIFY(result.traces[0].result);
}
// ─────────────────────────────────────────────
// COUVERTURE ADDITIONNELLE
// ─────────────────────────────────────────────

void TestAccessControlCore::operatorEvaluator_missingStrategy_shouldReturnFalse()
{
    // Un registry vide : OperatorEvaluator::evaluate sur un opérateur
    // sans stratégie enregistrée retourne false (filet de sécurité).
    OperatorRegistry emptyRegistry;
    OperatorEvaluator evaluator(emptyRegistry);

    const bool result = evaluator.evaluate(
        PolicyOperator::Equals,
        scalarResolvedValue("admin"),
        scalarResolvedValue("admin")
        );

    QVERIFY(!result);
}

void TestAccessControlCore::operatorEvaluator_notEqualsWithEmptyLeft_shouldNotShortCircuit()
{
    // NotEquals est exempté du court-circuit "left vide => false" :
    // un left vide comparé à un scalaire doit être évalué comme
    // "différents" (true), pas court-circuité à false.
    auto registry = OperatorRegistry::create_default();
    OperatorEvaluator evaluator(registry);

    const bool result = evaluator.evaluate(
        PolicyOperator::NotEquals,
        emptyResolvedValue(),
        scalarResolvedValue("admin")
        );

    QVERIFY(result);
}

void TestAccessControlCore::policyEngine_doubleNotCondition_shouldReturnOriginal()
{
    // not(not(X)) doit retrouver la valeur de X.
    auto registry = OperatorRegistry::create_default();
    PolicyEngine engine(registry);

    auto condition = PolicyCondition::not_of(
        PolicyCondition::not_of(
            PolicyCondition(makeSubjectRoleContainsAdminPredicate())
            )
        );

    auto result = engine.evaluate(
        condition,
        makeSubject(),
        makeResource(),
        makeContext(),
        EvaluationOptions::production()
        );

    // X = "subject a le rôle admin" est vrai -> not(not(X)) vrai.
    QVERIFY(result.allowed);
}

void TestAccessControlCore::policyEngine_nestedComposite_shouldEvaluateCorrectly()
{
    // Composite imbriqué : all_of[ adminRole, any_of[ guest, admin ] ].
    // adminRole = vrai ; any_of[guest(faux), admin(vrai)] = vrai ;
    // all_of[vrai, vrai] = vrai.
    auto registry = OperatorRegistry::create_default();
    PolicyEngine engine(registry);

    auto condition = PolicyCondition::all_of({
        PolicyCondition(makeSubjectRoleContainsAdminPredicate()),
        PolicyCondition::any_of({
            PolicyCondition(makeSubjectRoleEqualsGuestPredicate()),
            PolicyCondition(makeSubjectRoleContainsAdminPredicate())
        })
    });

    auto result = engine.evaluate(
        condition,
        makeSubject(),
        makeResource(),
        makeContext(),
        EvaluationOptions::production()
        );

    QVERIFY(result.allowed);
}// ═════════════════════════════════════════════════════════════
// Gaps P0 : couverture supplementaire PolicyEngine
//
// Comble les trous identifies par l'audit du 09 juin 2026 :
//   - short_circuit ON/OFF en All (AND) et Any (OR)
//   - reason "Resolution error: ..." en strict mode
//   - condition Predicate sans predicate (edge case is_empty)
//   - accumulation du compteur predicates_evaluated sur arbre composite
// ═════════════════════════════════════════════════════════════

void TestAccessControlCore::policyEngine_allShortCircuitOn_shouldStopAtFirstFalse()
{
    // AND avec short_circuit=true : des qu'un enfant evalue false, on
    // s'arrete. Le compteur predicates_evaluated doit refleter ce stop
    // (1 seul predicat evalue, pas 2).
    auto registry = OperatorRegistry::create_default();
    PolicyEngine engine(registry);

    // all_of[guest (false), admin (true)] : avec short_circuit, on
    // s'arrete au premier (guest = false) sans evaluer le second.
    auto condition = PolicyCondition::all_of({
        PolicyCondition(makeSubjectRoleEqualsGuestPredicate()),
        PolicyCondition(makeSubjectRoleContainsAdminPredicate())
    });

    auto opts = EvaluationOptions::production();
    opts.short_circuit = true;

    auto result = engine.evaluate(
        condition,
        makeSubject(),
        makeResource(),
        makeContext(),
        opts
        );

    QVERIFY(!result.allowed);
    QCOMPARE(result.predicates_evaluated, std::size_t(1));
}

void TestAccessControlCore::policyEngine_allShortCircuitOff_shouldEvaluateAllChildren()
{
    // AND avec short_circuit=false : meme si un enfant est false, on
    // continue d'evaluer les suivants (utile en mode debug verbose
    // pour voir toutes les regles qui echouent). Compteur = 2.
    auto registry = OperatorRegistry::create_default();
    PolicyEngine engine(registry);

    auto condition = PolicyCondition::all_of({
        PolicyCondition(makeSubjectRoleEqualsGuestPredicate()),
        PolicyCondition(makeSubjectRoleContainsAdminPredicate())
    });

    auto opts = EvaluationOptions::production();
    opts.short_circuit = false;

    auto result = engine.evaluate(
        condition,
        makeSubject(),
        makeResource(),
        makeContext(),
        opts
        );

    QVERIFY(!result.allowed);
    // Les deux predicats ont ete evalues
    QCOMPARE(result.predicates_evaluated, std::size_t(2));
}

void TestAccessControlCore::policyEngine_anyShortCircuitOn_shouldStopAtFirstTrue()
{
    // OR avec short_circuit=true : des qu'un enfant evalue true, on
    // s'arrete. Compteur = 1.
    auto registry = OperatorRegistry::create_default();
    PolicyEngine engine(registry);

    // any_of[admin (true), guest (false)] : avec short_circuit, on
    // s'arrete au premier sans evaluer le second.
    auto condition = PolicyCondition::any_of({
        PolicyCondition(makeSubjectRoleContainsAdminPredicate()),
        PolicyCondition(makeSubjectRoleEqualsGuestPredicate())
    });

    auto opts = EvaluationOptions::production();
    opts.short_circuit = true;

    auto result = engine.evaluate(
        condition,
        makeSubject(),
        makeResource(),
        makeContext(),
        opts
        );

    QVERIFY(result.allowed);
    QCOMPARE(result.predicates_evaluated, std::size_t(1));
}

void TestAccessControlCore::policyEngine_anyShortCircuitOff_shouldEvaluateAllChildren()
{
    // OR avec short_circuit=false : on evalue tous les enfants meme
    // si un est deja true (utile en mode debug). Compteur = 2.
    auto registry = OperatorRegistry::create_default();
    PolicyEngine engine(registry);

    auto condition = PolicyCondition::any_of({
        PolicyCondition(makeSubjectRoleContainsAdminPredicate()),
        PolicyCondition(makeSubjectRoleEqualsGuestPredicate())
    });

    auto opts = EvaluationOptions::production();
    opts.short_circuit = false;

    auto result = engine.evaluate(
        condition,
        makeSubject(),
        makeResource(),
        makeContext(),
        opts
        );

    QVERIFY(result.allowed);
    // Les deux predicats ont ete evalues
    QCOMPARE(result.predicates_evaluated, std::size_t(2));
}

void TestAccessControlCore::policyEngine_strictModeResolutionError_shouldSetResolutionReason()
{
    // En strict mode + detail_level >= WithReason, une erreur de
    // resolution (path inexistant) doit etre catchee par
    // evaluate_predicate et mettre reason="Resolution error: ...".
    auto registry = OperatorRegistry::create_default();
    PolicyEngine engine(registry);

    // Predicat sur un attribut subject inexistant
    auto predicate = PolicyPredicate::make(
        PolicyValueRef::from_subject("attributes.nonexistent_attr"),
        PolicyOperator::Equals,
        PolicyValueRef::from_literal("anything")
        );
    PolicyCondition condition(std::move(predicate));

    // EvaluationOptions strict + WithReason :
    // - strict_mode=Strict → ValueResolver throw runtime_error
    // - detail_level >= WithReason → result.reason est rempli
    EvaluationOptions opts;
    opts.strict_mode = StrictMode::Strict;
    opts.detail_level = DetailLevel::WithReason;
    opts.short_circuit = true;

    auto result = engine.evaluate(
        condition,
        makeSubject(),
        makeResource(),
        makeContext(),
        opts
        );

    QVERIFY(!result.allowed);
    QVERIFY(result.reason.has_value());
    QVERIFY(QString::fromStdString(*result.reason).contains("Resolution error"));
}

void TestAccessControlCore::policyEngine_emptyPredicateCondition_shouldDenyAndNotCount()
{
    // Edge case : condition de type Predicate mais sans predicate
    // (state invalide mais constructible via PolicyCondition() default).
    // evaluate_condition retourne false dans ce cas, sans incrementer
    // le compteur (rien n'est evalue).
    auto registry = OperatorRegistry::create_default();
    PolicyEngine engine(registry);

    // PolicyCondition par defaut : type=Predicate, predicate=nullopt
    PolicyCondition condition;
    QVERIFY(condition.is_empty());

    auto result = engine.evaluate(
        condition,
        makeSubject(),
        makeResource(),
        makeContext(),
        EvaluationOptions::production()
        );

    QVERIFY(!result.allowed);
    QCOMPARE(result.predicates_evaluated, std::size_t(0));
}

void TestAccessControlCore::policyEngine_predicatesEvaluatedCounter_shouldAccumulateAcrossTree()
{
    // Sur un arbre composite imbrique evalue completement (sans
    // short_circuit), le compteur predicates_evaluated doit refleter
    // chaque predicat evalue (les composites comme all_of/any_of/not
    // ne comptent PAS, seuls les Predicate feuilles incrementent).
    //
    // Arbre : all_of[ admin, any_of[ guest, admin ] ]
    // Avec short_circuit=false :
    //   - admin (1)
    //   - any_of evalue ses 2 enfants : guest (2), admin (3)
    // Total : 3 predicats evalues.
    auto registry = OperatorRegistry::create_default();
    PolicyEngine engine(registry);

    auto condition = PolicyCondition::all_of({
        PolicyCondition(makeSubjectRoleContainsAdminPredicate()),
        PolicyCondition::any_of({
            PolicyCondition(makeSubjectRoleEqualsGuestPredicate()),
            PolicyCondition(makeSubjectRoleContainsAdminPredicate())
        })
    });

    auto opts = EvaluationOptions::production();
    opts.short_circuit = false;  // force l'evaluation complete

    auto result = engine.evaluate(
        condition,
        makeSubject(),
        makeResource(),
        makeContext(),
        opts
        );

    QVERIFY(result.allowed);
    QCOMPARE(result.predicates_evaluated, std::size_t(3));
}