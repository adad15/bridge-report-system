#pragma once

#include <optional>
#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>

namespace bridge_report::db {

// 绑定视图的一行 = 报告里出现的一个不同 (部件名称, 归一化编号)。
struct BindingRow {
    std::string component_number;                     // 报告编号原文（代表）
    int defect_count{0};                              // 引用该编号的病害数
    std::string status;                               // bound|ambiguous|unmatched|missing
    std::optional<std::string> bridge_component_id;   // 已绑定的实际构件
    std::vector<std::string> candidate_component_ids; // 歧义候选
};

// 按报告"部件名称"（规范固定用词）分组。
struct BindingGroup {
    std::string part_name;
    int total{0};      // 行数（不同编号数）
    int bound{0};
    int unmatched{0};
    int ambiguous{0};
    int missing{0};
    std::vector<BindingRow> rows;
};

struct BindingOverview {
    bool inventory_confirmed{false};
    std::vector<BindingGroup> groups;
};

enum class BindingStatus {
    Ok,
    NotFound,     // 导入记录不存在
    Conflict,     // 非"待校对"相 / 台账未确认 / 所选构件类别与部件名称不符
    Invalid,      // 入参无效
    Failed,       // 数据库异常
};

struct BindingOutcome {
    BindingStatus status{BindingStatus::Ok};
    std::optional<BindingOverview> overview;
};

// 在导入记录"待校对"相内读/改 parsed_result_json.defects[] 的构件绑定。
// 行单位与批量作用域 = (部件名称, 归一化编号)：绑一次挂上所有引用它的病害。
class ImportBindingRepository {
public:
    explicit ImportBindingRepository(drogon::orm::DbClientPtr db_client);

    [[nodiscard]] BindingOutcome overview(const std::string& import_id);
    [[nodiscard]] BindingOutcome bind(
        const std::string& import_id, const std::string& part_name,
        const std::string& component_number, const std::string& bridge_component_id);
    [[nodiscard]] BindingOutcome mark_missing(
        const std::string& import_id, const std::string& part_name,
        const std::string& component_number);
    [[nodiscard]] BindingOutcome clear(
        const std::string& import_id, const std::string& part_name,
        const std::string& component_number);

private:
    drogon::orm::DbClientPtr db_client_;
};

}  // namespace bridge_report::db
