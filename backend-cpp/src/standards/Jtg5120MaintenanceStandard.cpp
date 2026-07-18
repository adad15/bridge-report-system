#include "bridge_report/standards/Jtg5120MaintenanceStandard.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace bridge_report::standards {
namespace {

constexpr std::string_view kLevelCatalog = "jtg5120.maintenance_level.catalog";
constexpr std::string_view kInspectionTypeCatalog = "jtg5120.inspection_type.catalog";
constexpr std::string_view kPeriodicInterval = "jtg5120.periodic_inspection.interval";
constexpr std::string_view kPeriodicContent = "jtg5120.periodic_inspection.content";
constexpr std::string_view kPeriodicType = "jtg5120.inspection_type.periodic";

MaintenanceRuleIssue issue(
    std::string code,
    std::string message,
    std::string field = {},
    std::string rule_id = {}) {
    return {
        std::move(code),
        std::move(message),
        std::move(field),
        std::move(rule_id),
    };
}

const StandardDefinition* definition(const StandardPackage& package, std::string_view id) {
    const auto found = package.definitions.find(std::string(id));
    return found == package.definitions.end() ? nullptr : &found->second;
}

std::optional<MaintenanceRuleIssue> package_contract_issue(const StandardPackage& package) {
    if (package.manifest.family != StandardFamily::maintenance ||
        package.manifest.standard_id != "JTG_5120_2021") {
        return issue(
            "standard_identity_mismatch",
            "JTG 5120 养护适配器不能解释其他规范身份的规则包。");
    }
    if (package.manifest.algorithm_id != "jtg-5120-2021-maintenance-query") {
        return issue(
            "algorithm_unsupported",
            "养护适配器与规则包声明的算法不匹配。",
            {},
            package.manifest.algorithm_id);
    }
    return std::nullopt;
}

MaintenanceRuleSource source(const StandardDefinition& rule, std::string source_reference) {
    return {rule.id, std::move(source_reference)};
}

}  // namespace

Jtg5120MaintenanceStandard::Jtg5120MaintenanceStandard(StandardPackage package)
    : package_(std::move(package)) {}

std::string Jtg5120MaintenanceStandard::algorithm_id() const {
    return package_.manifest.algorithm_id;
}

const StandardManifest& Jtg5120MaintenanceStandard::metadata() const noexcept {
    return package_.manifest;
}

MaintenanceLevelsResult Jtg5120MaintenanceStandard::maintenance_levels() const {
    MaintenanceLevelsResult result;
    if (const auto contract_issue = package_contract_issue(package_); contract_issue.has_value()) {
        result.issues.push_back(*contract_issue);
        return result;
    }
    const auto* rule = definition(package_, kLevelCatalog);
    if (rule == nullptr || !rule->payload["levels"].isArray() ||
        !rule->payload["source_clause"].isString()) {
        result.issues.push_back(issue(
            "maintenance_rule_missing",
            "规范包缺少有效的养护等级目录。",
            {},
            std::string(kLevelCatalog)));
        return result;
    }

    std::vector<MaintenanceLevel> levels;
    for (const auto& item : rule->payload["levels"]) {
        if (!item["id"].isString() || !item["code"].isString() ||
            !item["name"].isString()) {
            result.issues.push_back(issue(
                "maintenance_rule_invalid",
                "养护等级目录包含无效条目。",
                {},
                rule->id));
            return result;
        }
        levels.push_back({
            item["id"].asString(),
            item["code"].asString(),
            item["name"].asString(),
            source(*rule, rule->payload["source_clause"].asString()),
        });
    }
    if (levels.empty()) {
        result.issues.push_back(issue(
            "maintenance_rule_invalid", "养护等级目录不能为空。", {}, rule->id));
        return result;
    }
    result.value = std::move(levels);
    return result;
}

InspectionTypesResult Jtg5120MaintenanceStandard::inspection_types() const {
    InspectionTypesResult result;
    if (const auto contract_issue = package_contract_issue(package_); contract_issue.has_value()) {
        result.issues.push_back(*contract_issue);
        return result;
    }
    const auto* rule = definition(package_, kInspectionTypeCatalog);
    if (rule == nullptr || !rule->payload["types"].isArray()) {
        result.issues.push_back(issue(
            "maintenance_rule_missing",
            "规范包缺少有效的检查类别目录。",
            {},
            std::string(kInspectionTypeCatalog)));
        return result;
    }

    std::vector<InspectionType> types;
    for (const auto& item : rule->payload["types"]) {
        if (!item["id"].isString() || !item["code"].isString() ||
            !item["name"].isString() || !item["source_clause"].isString()) {
            result.issues.push_back(issue(
                "maintenance_rule_invalid",
                "检查类别目录包含无效条目。",
                {},
                rule->id));
            return result;
        }
        types.push_back({
            item["id"].asString(),
            item["code"].asString(),
            item["name"].asString(),
            source(*rule, item["source_clause"].asString()),
        });
    }
    if (types.empty()) {
        result.issues.push_back(issue(
            "maintenance_rule_invalid", "检查类别目录不能为空。", {}, rule->id));
        return result;
    }
    result.value = std::move(types);
    return result;
}

PeriodicInspectionRequirementResult
Jtg5120MaintenanceStandard::periodic_inspection_requirements(
    const MaintenanceQueryContext& context) const {
    PeriodicInspectionRequirementResult result;
    if (context.maintenance_level_id.empty()) {
        result.issues.push_back(issue(
            "maintenance_level_required", "定期检查查询必须提供养护等级。",
            "maintenance_level_id"));
    }
    if (context.inspection_type_id.empty()) {
        result.issues.push_back(issue(
            "inspection_type_required", "定期检查查询必须提供检查类别。",
            "inspection_type_id"));
    }
    if (!result.issues.empty()) {
        return result;
    }

    const auto levels = maintenance_levels();
    if (!levels.ok()) {
        result.issues = levels.issues;
        return result;
    }
    const auto level = std::find_if(
        levels.value->begin(), levels.value->end(), [&](const MaintenanceLevel& item) {
            return item.id == context.maintenance_level_id;
        });
    if (level == levels.value->end()) {
        result.issues.push_back(issue(
            "maintenance_level_unsupported", "当前规范包不支持该养护等级。",
            "maintenance_level_id"));
        return result;
    }

    const auto types = inspection_types();
    if (!types.ok()) {
        result.issues = types.issues;
        return result;
    }
    const auto type = std::find_if(
        types.value->begin(), types.value->end(), [&](const InspectionType& item) {
            return item.id == context.inspection_type_id;
        });
    if (type == types.value->end()) {
        result.issues.push_back(issue(
            "inspection_type_unsupported", "当前规范包不支持该检查类别。",
            "inspection_type_id"));
        return result;
    }
    if (context.inspection_type_id != kPeriodicType) {
        result.issues.push_back(issue(
            "inspection_type_unsupported_for_periodic_query",
            "首期定期检查要求查询只接受定期检查类别。",
            "inspection_type_id",
            std::string(kInspectionTypeCatalog)));
        return result;
    }

    const auto* interval_rule = definition(package_, kPeriodicInterval);
    const auto* content_rule = definition(package_, kPeriodicContent);
    if (interval_rule == nullptr || content_rule == nullptr ||
        !interval_rule->payload["maximum_intervals"].isArray() ||
        !interval_rule->payload["source_clause"].isString() ||
        !content_rule->payload["content_groups"].isArray() ||
        !content_rule->payload["source_clauses"].isArray()) {
        result.issues.push_back(issue(
            "maintenance_rule_missing",
            "规范包缺少定期检查周期或检查内容规则。",
            {},
            interval_rule == nullptr ? std::string(kPeriodicInterval)
                                     : std::string(kPeriodicContent)));
        return result;
    }

    const Json::Value* matching_interval = nullptr;
    for (const auto& item : interval_rule->payload["maximum_intervals"]) {
        if (item["maintenance_level_id"].isString() &&
            item["maintenance_level_id"].asString() == context.maintenance_level_id) {
            matching_interval = &item;
            break;
        }
    }
    if (matching_interval == nullptr || !(*matching_interval)["years"].isNumeric() ||
        !std::isfinite((*matching_interval)["years"].asDouble()) ||
        (*matching_interval)["years"].asDouble() <= 0.0) {
        result.issues.push_back(issue(
            "maintenance_rule_invalid",
            "规范包未提供该养护等级的有效定期检查周期。",
            "maintenance_level_id",
            interval_rule->id));
        return result;
    }

    PeriodicInspectionRequirement requirement;
    requirement.standard_id = package_.manifest.standard_id;
    requirement.package_version = package_.manifest.package_version;
    requirement.maintenance_level_id = context.maintenance_level_id;
    requirement.inspection_type_id = context.inspection_type_id;
    requirement.maximum_interval_years = (*matching_interval)["years"].asDouble();
    for (const auto& item : content_rule->payload["content_groups"]) {
        if (!item["id"].isString() || !item["name"].isString()) {
            result.issues.push_back(issue(
                "maintenance_rule_invalid",
                "定期检查内容规则包含无效条目。",
                {},
                content_rule->id));
            return result;
        }
        requirement.content_groups.push_back({
            item["id"].asString(),
            item["name"].asString(),
        });
    }
    if (requirement.content_groups.empty()) {
        result.issues.push_back(issue(
            "maintenance_rule_invalid", "定期检查内容不能为空。", {}, content_rule->id));
        return result;
    }
    requirement.sources.push_back(source(
        *interval_rule, interval_rule->payload["source_clause"].asString()));
    for (const auto& clause : content_rule->payload["source_clauses"]) {
        if (!clause.isString() || clause.asString().empty()) {
            result.issues.push_back(issue(
                "maintenance_rule_invalid",
                "定期检查内容规则缺少有效来源条款。",
                {},
                content_rule->id));
            return result;
        }
        requirement.sources.push_back(source(*content_rule, clause.asString()));
    }
    result.value = std::move(requirement);
    return result;
}

ProjectRequirementValidation Jtg5120MaintenanceStandard::validate_project_requirements(
    const MaintenanceQueryContext& context) const {
    ProjectRequirementValidation validation;
    const auto requirement = periodic_inspection_requirements(context);
    if (!requirement.ok()) {
        validation.issues = requirement.issues;
        return validation;
    }
    validation.sources = requirement.value->sources;
    if (!context.planned_interval_years.has_value()) {
        validation.issues.push_back(issue(
            "planned_interval_required",
            "校验项目定期检查要求时必须提供拟定周期。",
            "planned_interval_years",
            std::string(kPeriodicInterval)));
        return validation;
    }
    if (!std::isfinite(*context.planned_interval_years) ||
        *context.planned_interval_years <= 0.0) {
        validation.issues.push_back(issue(
            "planned_interval_invalid",
            "拟定定期检查周期必须是大于 0 的有限年数。",
            "planned_interval_years",
            std::string(kPeriodicInterval)));
        return validation;
    }
    if (*context.planned_interval_years > requirement.value->maximum_interval_years) {
        validation.issues.push_back(issue(
            "periodic_interval_exceeds_maximum",
            "拟定定期检查周期超过当前养护等级允许的最大周期。",
            "planned_interval_years",
            std::string(kPeriodicInterval)));
        return validation;
    }
    validation.valid = true;
    return validation;
}

}  // namespace bridge_report::standards
