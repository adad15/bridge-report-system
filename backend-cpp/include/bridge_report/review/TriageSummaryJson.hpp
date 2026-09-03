#pragma once

#include <json/value.h>

#include <map>
#include <string>
#include <vector>

#include "bridge_report/review/ThreadTriageGrouping.hpp"

namespace bridge_report::review {

/**
 * @brief 观测的展示字段：标度、尺寸、照片、观测编号。
 *
 * 归组模型只管跨年身份，这些一律不进那个模型，按观测 id 单独批量取。
 * 但异常簇是**人**在判断"这几条是不是同一处病害"，判据恰恰是它们——标度看恶化趋势、
 * 尺寸看连续性、照片是最终判据。只给位置写法和病害类型，等于把系统已经判不了的那个
 * 信号原样还给人看一遍。
 */
struct TriageObservationDisplay {
    struct Photo {
        std::string id;
        std::string photo_number;
    };

    std::string system_number;
    std::string scale;
    std::string description;
    std::vector<std::string> measurements;
    std::vector<Photo> photos;
};

/// 观测 id → 展示字段。缺键是正常的（那条观测没有尺寸或照片），按缺省渲染即可。
using TriageDisplayLookup = std::map<std::string, TriageObservationDisplay>;

/**
 * @brief 整理模型 → 工作台摘要 JSON。纯函数，不碰数据库。
 *
 * 摘要**不含批次的全部观测**：144 个批次里最大的一个就有 489 条，一次性推过去正是现有
 * 整理页的死法。每批只给几条样例，明细由前端展开时单独取。
 *
 * 异常簇相反，要给完整上下文——它总共只有十来组，而人正是靠"两个位置的历年观测摆在
 * 一起"才判断得出该合还是该分。
 */
[[nodiscard]] Json::Value triage_summary_json(
    const TriageModel& model, const TriageDisplayLookup& display = {});

/**
 * @brief 从一个批次里挑用于摘要展示的样例组。
 *
 * 取排序后的**头、中、尾**各一条，而不是前三条：163 个铰缝取前三个只能看到 1#、2#、3#，
 * 看不出这一批横跨全桥。也绝不随机——刷新一次样例换一批，人没法复核自己刚看过什么。
 */
[[nodiscard]] std::vector<const TriageGroup*> pick_sample_groups(const TriageBatch& batch);

}  // namespace bridge_report::review
