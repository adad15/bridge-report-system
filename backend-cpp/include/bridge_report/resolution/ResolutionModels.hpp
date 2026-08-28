#pragma once

#include <optional>
#include <string>
#include <vector>

#include <json/value.h>

// 构件解析与评分树解析的领域模型。
//
// 设计：docs/superpowers/specs/2026-08-27-import-component-rating-resolution-separation-design.md
//
// 这些结构对应 027 迁移建的关系表，是 5.0 之后"这条病害绑到哪个构件、匹到哪个评分树
// 节点"的唯一权威表达。它们**不**出现在 parsed_result_json 里：那份 JSON 只承载来源
// 事实和一般校对事实（§4.4）。
namespace bridge_report::resolution {

// 组身份里的编号一律是 C++ 权威归一化结果，缺失时是空串而不是 nullopt——
// 空串让唯一约束仍然成立，NULL 会让它静默失效（§8.1）。
struct ComponentResolutionGroup {
    std::string id;
    std::string import_record_id;
    std::string source_component_name;
    std::optional<std::string> source_component_number;
    std::string normalized_component_number;
    std::string resolution_mode{"single"};   // single | multi | range
    std::string status{"unresolved"};        // unresolved | bound | missing
    std::optional<std::string> match_method; // exact | confirmed_alias | manual | side_pair | range
    std::optional<std::string> inventory_revision_id;
    int version{1};
    std::optional<std::string> resolved_by_user_id;
    std::optional<std::string> resolved_at;
};

struct ComponentGroupMember {
    std::string id;
    std::string import_record_id;
    std::string group_id;
    std::string source_candidate_id;
    int source_order{0};
};

struct ComponentResolutionTarget {
    std::string id;
    std::string group_id;
    std::string bridge_component_id;
    int target_order{1};
    std::string target_role{"primary"};  // primary | left | right | range_member
};

struct ResolvedDefectInstance {
    std::string id;
    std::string group_member_id;
    std::string target_id;
    int instance_order{1};
    std::string instance_status{"active"};  // active | ignored
    bool is_photo_owner{false};
    Json::Value fact_overrides_json{Json::objectValue};
    int component_resolution_version{1};
    int version{1};
};

struct RatingResolution {
    std::string resolved_defect_instance_id;
    std::string rating_tree_version_id;
    std::optional<std::string> rating_tree_node_id;
    std::optional<std::string> standard_defect_indicator_id;
    std::string status{"unresolved"};        // unresolved | matched
    std::optional<std::string> match_method;
    Json::Value match_evidence_json{Json::objectValue};
    int component_resolution_version{1};
    std::string applicability_hash;
    std::string match_input_hash;
    /// 人工裁决那一刻的 match_input_hash；与当前值不同即"裁决后内容变过"（§8.5）。
    /// 仅 match_method = manual 时有值。
    std::optional<std::string> resolved_match_input_hash;
    int version{1};
    std::optional<std::string> resolved_by_user_id;
    std::optional<std::string> resolved_at;
};

// 追加式审计事件（§8.7）。当前状态表只表达当前结果，这里只表达历史，两者不混用。
struct ResolutionEvent {
    std::string import_record_id;
    std::optional<std::string> group_id;
    std::optional<std::string> resolved_defect_instance_id;
    std::optional<std::string> plan_id;
    std::string operation_type;
    Json::Value before_json{Json::objectValue};
    Json::Value after_json{Json::objectValue};
    std::optional<std::string> actor_user_id;
};

// 一次导入初始化的结果摘要。用于日志、测试断言和后续的工作区统计校对。
struct ResolutionInitializationSummary {
    int group_count{0};
    int member_count{0};
    int bound_group_count{0};
    int unresolved_group_count{0};
    int ambiguous_group_count{0};  // 派生口径：unresolved 且有候选（§4.7）
    int instance_count{0};
    int rating_matched_count{0};
    int rating_unresolved_count{0};
    // 该桥没有已确认台账版本时为真：全部组停在 unresolved，但导入照常可校对（§10）。
    bool inventory_unconfirmed{false};
};

}  // namespace bridge_report::resolution
