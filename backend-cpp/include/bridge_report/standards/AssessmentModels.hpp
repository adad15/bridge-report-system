#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <json/json.h>

namespace bridge_report::standards {

enum class StructurePart {
    superstructure,
    substructure,
    deck_system,
};

std::string to_string(StructurePart part);
std::optional<StructurePart> parse_structure_part(std::string_view value);

struct DefectAssessmentInput {
    std::string defect_indicator_id;
    int scale{0};
};

struct ComponentAssessmentInput {
    std::string component_instance_id;
    std::string component_type_id;
    std::vector<DefectAssessmentInput> defects;
};

struct BridgeAssessmentInput {
    std::string bridge_type_id;
    std::vector<ComponentAssessmentInput> components;
    std::vector<std::string> triggered_control_ids;
    bool worst_major_component_affects_safety{false};
};

struct AssessmentIssue {
    std::string code;
    std::string message;
    std::string entity_id;
    std::string rule_id;
};

struct AssessmentTraceEntry {
    std::string step;
    std::string rule_id;
    std::string entity_id;
    std::string source_reference;
    Json::Value inputs{Json::objectValue};
    Json::Value output{Json::objectValue};
};

struct DefectAssessmentResult {
    std::string defect_indicator_id;
    int scale{0};
    double deduction{0.0};
    std::string deduction_rule_id;
    std::string source_reference;
};

struct ComponentAssessmentResult {
    std::string component_instance_id;
    std::string component_type_id;
    StructurePart structure_part{StructurePart::superstructure};
    bool major{false};
    double score{0.0};
    std::vector<double> ordered_deductions;
    std::vector<DefectAssessmentResult> defects;
};

struct ComponentCategoryAssessmentResult {
    std::string component_type_id;
    std::string component_type_name;
    StructurePart structure_part{StructurePart::superstructure};
    bool major{false};
    double score{0.0};
    int grade{0};
    double mean_component_score{0.0};
    double minimum_component_score{0.0};
    std::optional<double> component_count_factor;
    bool low_score_passthrough{false};
    double configured_weight{0.0};
    double effective_weight{0.0};
    std::vector<ComponentAssessmentResult> components;
};

struct StructurePartAssessmentResult {
    StructurePart structure_part{StructurePart::superstructure};
    double score{0.0};
    int grade{0};
    double overall_weight{0.0};
    std::vector<ComponentCategoryAssessmentResult> categories;
};

struct TriggeredControlResult {
    std::string control_id;
    std::string source_reference;
    std::string label;
    std::optional<int> result_grade;
};

struct BridgeAssessmentResult {
    std::string standard_id;
    std::string package_version;
    std::string bridge_type_id;
    double overall_score{0.0};
    int calculated_grade{0};
    int final_grade{0};
    std::vector<StructurePartAssessmentResult> structure_parts;
    std::vector<TriggeredControlResult> triggered_controls;
    std::vector<AssessmentTraceEntry> trace;
    std::string explanation;
};

struct AssessmentOutcome {
    std::optional<BridgeAssessmentResult> result;
    std::vector<AssessmentIssue> issues;

    bool ok() const noexcept { return result.has_value() && issues.empty(); }
};

}  // namespace bridge_report::standards
