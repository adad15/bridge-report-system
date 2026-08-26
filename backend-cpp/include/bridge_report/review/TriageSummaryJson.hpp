#pragma once

#include <json/value.h>

#include "bridge_report/review/ThreadTriageGrouping.hpp"

namespace bridge_report::review {

/**
 * @brief 整理模型 → 工作台摘要 JSON。纯函数，不碰数据库。
 *
 * 摘要**不含批次的全部观测**：144 个批次里最大的一个就有 489 条，一次性推过去正是现有
 * 整理页的死法。每批只给几条样例，明细由前端展开时单独取。
 *
 * 异常簇相反，要给完整上下文——它总共只有十来组，而人正是靠"两个位置的历年观测摆在
 * 一起"才判断得出该合还是该分。
 */
[[nodiscard]] Json::Value triage_summary_json(const TriageModel& model);

/**
 * @brief 从一个批次里挑用于摘要展示的样例组。
 *
 * 取排序后的**头、中、尾**各一条，而不是前三条：163 个铰缝取前三个只能看到 1#、2#、3#，
 * 看不出这一批横跨全桥。也绝不随机——刷新一次样例换一批，人没法复核自己刚看过什么。
 */
[[nodiscard]] std::vector<const TriageGroup*> pick_sample_groups(const TriageBatch& batch);

}  // namespace bridge_report::review
