#pragma once

#include <optional>
#include <string>
#include <vector>

#include "bridge_report/review/ThreadCanonicalKey.hpp"

namespace bridge_report::review {

/**
 * @brief 把未绑定观测归成"待确认的线索候选"，并把有歧义的挑出来交人工。
 *
 * 纯函数，不访问数据库，**不持久化任何东西**——每次进入整理工作台按当前有效事实重算。
 * 批次只是展示与确认单位，构件改名、观测重绑、年度修订都会让它变，物化就得配一整套
 * 失效重算，不划算。
 *
 * 三层：批次 → 组 → 观测。组是未来的一条线索；批次是"长相相同、动作相同"的一批组，
 * 让人一次判断就覆盖多个构件。真正需要逐个看的进异常簇，绝不混进干净批次。
 */

struct TriageObservationInput {
    std::string id;
    std::string bridge_component_id;
    std::string structure_part;
    std::string component_type;
    std::string business_component_code;
    /// 跨年身份取它（迁移 029）。观测入库时必定带一个适用的评定树节点。
    std::string node_key;
    /// 只作展示与线索命名，不参与身份判定。
    std::string defect_type;
    std::string defect_location;
    std::string updated_at;
    int inspection_year{0};
};

struct TriageThreadInput {
    std::string id;
    std::string system_number;
    std::string thread_name;
    std::string bridge_component_id;
    /// 与观测同一口径；029 之前建的线索可能为空，那种线索匹不上任何观测，
    /// 等人工在整理台上重新归并。
    std::string node_key;
    /// 只作展示，不参与身份判定。
    std::string defect_type;
    std::string defect_location;
    std::string updated_at;
};

enum class TriageAction { Create, Bind };

[[nodiscard]] std::string to_string(TriageAction action);

struct TriageGroup {
    std::string group_id;
    ThreadCanonicalKey key;
    /// 按年度升序；同组内每个年度至多一条（多于一条的整组进异常簇）。
    std::vector<TriageObservationInput> observations;
    /// Bind 时有值：该组精确命中的那条已有线索。
    std::optional<std::string> matched_thread_id;
};

struct TriageBatch {
    std::string batch_id;
    TriageAction action{TriageAction::Create};
    std::string structure_part;
    std::string component_type;
    /// 展示用原文，取组内最新年度观测；归组用的是 key 里的规范值。
    std::string defect_type;
    std::string defect_location;
    std::vector<int> year_set;  // 升序
    /// 按 business_component_code 稳定排序。
    std::vector<TriageGroup> groups;
    std::string fingerprint;

    [[nodiscard]] int observation_count() const;
};

struct TriageOverlapTarget {
    enum class Kind { Group, Thread };

    Kind kind{Kind::Group};
    std::string id;
    std::string display_name;
    std::string system_number;
    std::string normalized_location;
};

/// 异常原因码。与前端和响应体共用同一套字符串。
inline constexpr const char* kReasonMultipleInYear = "multiple_in_year";
inline constexpr const char* kReasonLocationOverlap = "location_overlap";
inline constexpr const char* kReasonAmbiguousThread = "ambiguous_thread";

/**
 * @brief 必须放在一起判断的一簇东西。
 *
 * 位置重叠描述的是**组与组之间**（或组与已有线索之间）的关系，拆成互相孤立的单条卡片
 * 就丢掉了判断该合还是该分所需的上下文。`overlap_targets` 逐个标明对方是组还是线索——
 * 只给原因码不给对方是谁，人在异常簇里依然无从下手。
 */
struct TriageManualCluster {
    std::string cluster_id;
    std::vector<std::string> reason_codes;  // 去重、稳定排序
    std::vector<TriageGroup> groups;
    std::vector<TriageOverlapTarget> overlap_targets;
    std::vector<TriageThreadInput> related_threads;

    [[nodiscard]] int observation_count() const;
};

struct TriageModel {
    std::vector<TriageBatch> batches;               // 按覆盖观测数降序
    std::vector<TriageManualCluster> manual_clusters;
    std::string snapshot_fingerprint;
    int unbound_observation_count{0};
    int batchable_group_count{0};
    int batchable_observation_count{0};
    int manual_group_count{0};
    int manual_observation_count{0};
};

/**
 * @brief 由未绑定观测与同桥已有线索算出整理模型。
 *
 * @param observations 当前有效年度、正式状态、`defect_thread_id` 为空的观测。
 * @param existing_threads 相关构件上的已有线索（冷启动时为空）。
 *
 * 确定性：同一份输入重复调用得到完全相同的 id 与指纹。id 用 SHA-256 截断，
 * **不得改用 std::hash**——它不保证跨进程稳定，而 batch_id 要在前后两次请求间对得上。
 */
[[nodiscard]] TriageModel build_triage_model(
    std::vector<TriageObservationInput> observations,
    std::vector<TriageThreadInput> existing_threads);

}  // namespace bridge_report::review
