#pragma once

#include <optional>
#include <string>
#include <vector>

#include <json/value.h>

// 解析工作区的读模型（设计 §13.1）。
//
// 前端不得根据原始病害数组自行聚合绑定组或推导状态：部件层级、歧义标签、进度统计和
// 允许动作全部由后端算好随响应下发。两边各算一份的话，症状是"界面显示可绑、后端拒绝"，
// 而且只在归一化或类别判定分叉的那几行上出现。
namespace bridge_report::resolution {

/// 台账构件的可读信息，用于展示目标与候选。
struct WorkspaceComponentSummary {
    std::string bridge_component_id;
    std::string component_number;
    std::string site_component_type;
    std::string site_name;
    /// 由组所钉的台账版本当场派生（§17.2），不再冻在草稿 JSON 里。校对页靠它
    /// 算"这件构件适用哪些评定树节点"；缺了这一对，整页会显示成"节点不适用于当前构件"。
    std::string standard_component_category_id;
    std::string standard_bridge_type_id;
};

/// 一条病害解析实例在读模型里的样子。
struct WorkspaceDefectInstance {
    std::string instance_id;
    std::string target_id;
    std::string bridge_component_id;
    int instance_order{1};
    std::string instance_status;
    bool is_photo_owner{false};
    int version{1};
    int component_resolution_version{1};
    /// 实际脱离来源值的字段，供界面标出"已按本实例单独设定"（§8.4）。
    std::vector<std::string> overridden_fields;
    /// 有效病害事实：来源值叠加本实例覆盖。界面展示与下游计算都以它为准。
    Json::Value effective_facts{Json::objectValue};

    // 评分树解析。没有解析行时 has_rating 为假——这在台账刚绑好、评分树还没跑过时
    // 是正常状态，不是错误。
    bool has_rating{false};
    std::string rating_status;
    std::optional<std::string> rating_tree_node_id;
    std::optional<std::string> rating_match_method;
    int rating_version{1};
    /// 人工选择后内容变了：保留节点，但提示复核（§8.5）。
    bool content_changed_after_manual_resolution{false};
};

/// 组内的一条来源病害及其展开出的实例。
struct WorkspaceGroupMember {
    std::string member_id;
    std::string source_candidate_id;
    int source_order{0};
    std::vector<WorkspaceDefectInstance> instances;
};

/// 一个来源构件组。行级，对应关系表里的一行。
struct WorkspaceComponentGroup {
    std::string group_id;
    std::string source_component_name;
    std::optional<std::string> source_component_number;
    std::string normalized_component_number;
    std::string resolution_mode;
    std::string status;  // unresolved | bound | missing
    std::optional<std::string> match_method;
    std::optional<std::string> inventory_revision_id;
    int version{1};

    /// 派生标签：status = unresolved 且候选数大于一（§4.7）。不入库。
    bool ambiguous{false};
    /// 派生：区间编号可展开，且组尚未解析。
    bool split_eligible{false};
    std::optional<int> split_expanded_count;

    std::vector<WorkspaceComponentSummary> targets;
    std::vector<WorkspaceComponentSummary> candidates;
    std::vector<WorkspaceGroupMember> members;

    /// 后端判定的允许动作；前端只按它决定按钮可用性，不自己推。
    std::vector<std::string> allowed_actions;
    /// 动作被禁用的原因码，供界面直接展示，不必自己拼话术。
    std::vector<std::string> blocked_reasons;
};

/// 部件层级聚合。按 source_component_name 派生，不入库（§4.7）。
struct WorkspacePartSummary {
    std::string source_component_name;
    int total{0};
    int bound{0};
    int unresolved{0};
    int ambiguous{0};
    int missing{0};
    std::vector<std::string> group_ids;
};

struct WorkspaceProgress {
    int group_count{0};
    int bound_count{0};
    int unresolved_count{0};
    int ambiguous_count{0};
    int missing_count{0};
    int instance_count{0};
    int active_instance_count{0};
    int rating_matched_count{0};
    int rating_unresolved_count{0};
    /// 活动实例里还没有评分树解析行的数量。
    int rating_missing_count{0};
};

struct WorkspaceRatingTree {
    std::string version_id;
    std::string tree_name;
    std::string package_version;
};

struct ResolutionWorkspace {
    std::string import_record_id;
    std::string bridge_id;
    /// 来源草稿并发版本；写来源事实的接口要用 If-Match 带回来（§8.0）。
    int draft_version{1};

    /// 该桥有没有已确认台账版本。为假时全部绑定类动作不可用，但病害与照片校对照常。
    bool inventory_confirmed{false};
    std::optional<std::string> inventory_revision_id;
    std::optional<WorkspaceRatingTree> rating_tree;

    std::vector<WorkspaceComponentGroup> groups;
    std::vector<WorkspacePartSummary> parts;
    WorkspaceProgress progress;
};

}  // namespace bridge_report::resolution
