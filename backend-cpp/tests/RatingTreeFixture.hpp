#pragma once

#include <string>

#include <drogon/orm/DbClient.h>

// 测试用的评定树夹具。
//
// 此前这两个套件是「从库里找一棵已发布的评定树」用。那在开发机上看着能跑——本机
// bridge_report_system 里有真实评定树——但 check-backend-tests.ps1 每次都从空 schema
// 开始，那里一棵树都没有，于是 SetUp 直接 GTEST_SKIP，十六个用例一个都不执行。
//
// 症状很难察觉：测试报告显示的是「跳过」而不是「失败」，总数也对得上，只有把跳过
// 清单逐条读一遍才会发现评分树解析命令根本没被验证过。夹具必须自带它依赖的数据。
namespace bridge_report::testing {

struct RatingTreeFixture {
    std::string technical_package_id;
    std::string maintenance_package_id;
    std::string tree_version_id;
    std::string profile_id;
    /// 可选的病害节点，适用于 bridge_type_id + component_category_id。
    std::string node_id;
    /// 另一个类别的可选节点，用来验证「节点不适用于该构件」必须被挡下。
    std::string inapplicable_node_id;
    std::string bridge_type_id;
    std::string component_category_id;
    std::string other_component_category_id;
};

/**
 * @brief 在当前 schema 里造一棵已发布的评定树，并绑定到一个生效的规范档案。
 *
 * @param tag 用于拼出唯一的规范包 / 树编码，避免同一 schema 内多个夹具互相撞唯一键。
 */
[[nodiscard]] RatingTreeFixture seed_rating_tree(
    const drogon::orm::DbClientPtr& client,
    const std::string& user_id,
    const std::string& tag);

/// 按创建顺序的反序清理。已发布的树不可变，删除前需临时停掉那个保护触发器。
void drop_rating_tree(
    const drogon::orm::DbClientPtr& client,
    const RatingTreeFixture& fixture);

}  // namespace bridge_report::testing
