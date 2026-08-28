#pragma once

#include <optional>
#include <string>
#include <vector>

#include <json/value.h>

// 预览计划（设计 §8.6、§13.3、§14）。
//
// 批量替换、区间展开和台账版本重指必须先出计划再执行：用户看到的计划与实际执行的
// 计划必须是同一份。前端执行时只提交 plan token，不提交自己算出来的结果集合。
namespace bridge_report::resolution {

/// 计划里的一行：一个来源构件组会发生什么。
struct ResolutionPlanRow {
    std::string group_id;
    std::string source_component_name;
    std::string source_component_number;
    /// 该组下的来源病害数，用于"展开前后数量"。
    int member_count{0};
    /// 变换后用于查找台账的编号（批量替换）或展开出的编号（区间展开）。
    std::vector<std::string> resolved_numbers;
    std::vector<std::string> target_component_ids;
    /// will_bind | will_clear | will_repoint | skipped | blocked
    std::string outcome;
    /// 跳过或阻断的原因码，供界面直接展示，不必自己拼话术。
    std::string reason_code;
    std::string reason_message;
};

struct ResolutionPlanPreview {
    std::string plan_token;
    std::string operation_type;
    std::string expires_at;
    std::vector<ResolutionPlanRow> rows;
    int will_apply_count{0};
    int skipped_count{0};
    int blocked_count{0};
    /// 展开前后数量：展开前是参与的来源病害数，展开后是将生成的实例数。
    int instances_before{0};
    int instances_after{0};
    /// 评分树将失效或重算的实例数。
    int rating_recomputed_count{0};
    std::optional<std::string> inventory_revision_id;
    std::optional<std::string> rating_tree_version_id;
};

/// 批量替换的用户意图。参与范围只含未解析组（§13.3）。
struct BulkReplaceIntent {
    /// 限定在某个部件层级下；为空表示整份导入的未解析组。
    std::string source_component_name;
    std::string find;
    std::string replace;
};

/// 区间展开的用户意图。
struct RangeExpandIntent {
    std::vector<std::string> group_ids;
};

/// 台账版本重指的用户意图：把钉在旧版本的组带到当前版本（§9.4）。
struct InventoryRepointIntent {
    /// 空表示整份导入里所有钉着非当前版本的组。
    std::vector<std::string> group_ids;
};

}  // namespace bridge_report::resolution
