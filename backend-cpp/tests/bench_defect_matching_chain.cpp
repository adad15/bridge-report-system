// 一次性基准：量"打开病害与照片页"那条链路各段的真实耗时。
//
// 直连数据库、不走 HTTP，因此不需要登录态。只读，不写任何数据。
// 需要显式设置 BRIDGE_REPORT_BENCH_BRIDGE 才会跑，正常测试套件里自动跳过。
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/ComponentInventoryRepository.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/db/RatingTreeRepository.hpp"
#include "bridge_report/review/DefectRatingTreeMatching.hpp"

namespace {

using Clock = std::chrono::steady_clock;

long long ms_since(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now() - start).count();
}

bool parse_json_text(const std::string& text, Json::Value& out) {
    Json::CharReaderBuilder builder;
    std::string errors;
    const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    return reader->parse(text.c_str(), text.c_str() + text.size(), &out, &errors);
}

TEST(DefectMatchingChainBench, TimesEachStageOfTheFirstPaint) {
    if (std::getenv("BRIDGE_REPORT_BENCH") == nullptr) {
        GTEST_SKIP() << "设置 BRIDGE_REPORT_BENCH 才跑";
    }

    auto client = bridge_report::db::create_db_client(
        bridge_report::config::PostgresConfig{}, 1);

    const auto context = client->execSqlSync(
        "select ir.id::text as import_id, ir.bridge_id::text as bridge_id,"
        "iy.component_inventory_revision_id::text as year_revision,"
        "psp.rating_tree_version_id::text as tree_version,"
        "psp.technical_condition_package_id::text as technical_package,"
        "coalesce(ir.parsed_result_json::text,'{}') as parsed "
        "from import_records ir "
        "join inspection_years iy on iy.id=ir.inspection_year_id "
        "join bridges b on b.id=iy.bridge_id "
        "left join project_standard_profiles psp on psp.id=iy.standard_profile_id "
        // 不按桥名选：Windows 控制台传进来的中文是 GBK，喂给 libpq 会直接报编码错。
        // 取病害最多的那条待校对记录，就是最能代表首屏代价的那一条。
        "where ir.import_status='待校对' "
        "order by jsonb_array_length(coalesce(ir.parsed_result_json->'defects','[]'::jsonb)) "
        "desc limit 1");
    ASSERT_FALSE(context.empty()) << "库里没有待校对的导入记录";

    const auto bridge_id = context[0]["bridge_id"].as<std::string>();
    const auto tree_version = context[0]["tree_version"].as<std::string>();
    const auto technical_package = context[0]["technical_package"].as<std::string>();
    Json::Value draft;
    ASSERT_TRUE(parse_json_text(context[0]["parsed"].as<std::string>(), draft));
    std::cerr << "\n=== 病害数 " << draft["defects"].size() << " ===\n";

    auto start = Clock::now();
    const auto tree = bridge_report::db::RatingTreeRepository(client)
                          .load_published_tree(tree_version);
    std::cerr << "load_published_tree      " << ms_since(start) << " ms（首次）\n";
    ASSERT_TRUE(tree.has_value());

    start = Clock::now();
    const auto tree_again = bridge_report::db::RatingTreeRepository(client)
                                .load_published_tree(tree_version);
    std::cerr << "load_published_tree      " << ms_since(start)
              << " ms（第二次，走缓存）\n";
    ASSERT_TRUE(tree_again.has_value());

    // 草稿里真正引用到的构件——匹配只需要这些。
    std::vector<std::string> referenced;
    {
        std::set<std::string> unique_ids;
        for (const auto& defect : draft["defects"]) {
            const auto& id = defect["bridge_component_id"];
            if (id.isString() && !id.asString().empty()) unique_ids.insert(id.asString());
        }
        referenced.assign(unique_ids.begin(), unique_ids.end());
    }

    start = Clock::now();
    const auto inventory = bridge_report::db::ComponentInventoryRepository(client)
        .resolve_confirmed_revision(
            bridge_id,
            context[0]["year_revision"].isNull()
                ? std::optional<std::string>{}
                : std::optional<std::string>{
                      context[0]["year_revision"].as<std::string>()});
    std::cerr << "resolve_confirmed_revision " << ms_since(start) << " ms  构件 "
              << (inventory ? inventory->entries.size() : 0) << " 条\n";

    start = Clock::now();
    const auto narrow = bridge_report::db::ComponentInventoryRepository(client)
        .resolve_confirmed_revision_for_components(
            bridge_id,
            context[0]["year_revision"].isNull()
                ? std::optional<std::string>{}
                : std::optional<std::string>{
                      context[0]["year_revision"].as<std::string>()},
            referenced);
    std::cerr << "  只取引用到的构件       " << ms_since(start) << " ms  构件 "
              << (narrow ? narrow->entries.size() : 0) << " 条\n";

    ASSERT_TRUE(inventory.has_value());
    start = Clock::now();
    const auto summary = bridge_report::db::ComponentInventoryRepository(client)
                             .load_summary(inventory->id);
    std::cerr << "load_summary             " << ms_since(start) << " ms\n";

    start = Clock::now();
    const auto report = bridge_report::review::match_defect_rating_tree_nodes(
        draft, tree_version, technical_package, *tree, narrow,
        bridge_report::review::DefectMatchScope{}, false);
    std::cerr << "match_defect_rating_tree_nodes " << ms_since(start)
              << " ms  记录 " << report.records.size() << " 条\n";

    // 精简装配必须与完整装配得出**完全一样**的匹配结果——快没有意义，
    // 如果它顺带改了归类。拿这座桥真实的 362 条逐条比。
    Json::Value draft_copy;
    ASSERT_TRUE(parse_json_text(context[0]["parsed"].as<std::string>(), draft_copy));
    const auto full_report = bridge_report::review::match_defect_rating_tree_nodes(
        draft_copy, tree_version, technical_package, *tree, inventory,
        bridge_report::review::DefectMatchScope{}, false);
    ASSERT_EQ(report.records.size(), full_report.records.size());
    std::size_t divergent = 0;
    for (std::size_t i = 0; i < report.records.size(); ++i) {
        if (report.records[i].candidate_id != full_report.records[i].candidate_id
            || report.records[i].outcome != full_report.records[i].outcome
            || report.records[i].node_id != full_report.records[i].node_id
            || report.records[i].reason_code != full_report.records[i].reason_code
            || report.records[i].candidates.size()
                   != full_report.records[i].candidates.size()) {
            ++divergent;
        }
    }
    EXPECT_EQ(divergent, 0u) << "精简装配改变了匹配结果";

    // 走查顺序：把排好的构件按部件去重打印，直接对照期望的清单核对。
    const auto ordered = bridge_report::db::ComponentInventoryRepository(client)
        .order_components_for_review(bridge_id, std::nullopt, referenced);
    std::cerr << "  走查顺序：";
    std::string previous;
    for (const auto& item : ordered) {
        if (item.part_name == previous) continue;
        std::cerr << item.part_name << " ";
        previous = item.part_name;
    }
    std::cerr << "\n";
    std::cerr << "  与完整装配逐条比对：" << (divergent == 0 ? "全部一致" : "有差异")
              << "\n\n";
}

}  // namespace
