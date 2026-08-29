#pragma once

#include <string>

namespace bridge_report::review {

/**
 * @brief "同一处病害"的规范键：候选推荐与批量归组共用的唯一口径。
 *
 * 两处若各自拼字符串，迟早会出现"候选说这两条是同一处、批次说不是"的分裂——
 * 用户看到的建议和系统的归组结果互相打架，而谁也说不清哪边对。所以键的构造只此一份。
 *
 * 位置的空值是一条**业务规则**，不是规范化的自然产物：`normalize_suggestion_text`
 * 只负责去空白与字符归一，是这里显式规定 null / 空串 / 纯空白都落到同一个空值上。
 * 铰缝那类构件本来就不写更细的位置，全桥 166 个铰缝的渗水泛碱都是空位置；不认这条规则，
 * 它们会散成 166 个互不相干的孤组。
 *
 * 病害的那一维取**评定树节点**，不取病害名称文字（迁移 029）。文字来自报告原文：
 * 「失效」「破损」这类写法在同一构件上分不出是哪种病害，而换个年度写成「渗水、泛碱」
 * 又会和「渗水泛碱」算成两条——归一化只管全角半角与标点，不认同义词。节点是规范分类，
 * 观测入库时必定带一个（确认前校验保证），跨年天然对齐。
 *
 * 取 `node_key` 而不是 `rating_tree_node_id`：后者每发布一版评定树就是一批新 UUID，
 * 拿它做键，哪一年锁了新版树就全部断链。`node_key` 跨版本不变。
 */
struct ThreadCanonicalKey {
    std::string bridge_component_id;
    /// 评定树节点的跨版本稳定键，形如 org.bridge.defect.5_1_1_1。不做文本归一化：
    /// 它是标识符不是自然语言，改动它只会掩盖上游取错了键。
    std::string node_key;
    /// 空字符串表示"无位置"，它是一个合法取值，不是缺失。
    std::string normalized_defect_location;

    bool operator==(const ThreadCanonicalKey&) const = default;

    /// 稳定序列化，供 group_id 哈希与日志使用；字段间用业务文本不可能出现的分隔符。
    [[nodiscard]] std::string canonical_string() const;
};

[[nodiscard]] ThreadCanonicalKey make_thread_canonical_key(
    const std::string& bridge_component_id,
    const std::string& node_key,
    const std::string& defect_location);

/**
 * @brief 两个**已规范化**的位置是否构成"非全等的互相包含"。
 *
 * 相等不算重叠——那是精确匹配，走的是完全不同的分支。任一侧为空也不算，否则空位置会和
 * 同构件的一切位置纠缠在一起。
 */
[[nodiscard]] bool locations_overlap(
    const std::string& normalized_left,
    const std::string& normalized_right);

}  // namespace bridge_report::review
