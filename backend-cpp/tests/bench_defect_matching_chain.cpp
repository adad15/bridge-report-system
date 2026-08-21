// 一次性基准：量"打开病害与照片页"那条链路各段的真实耗时。
//
// 直连数据库、不走 HTTP，因此不需要登录态。只读，不写任何数据。
// 需要显式设置 BRIDGE_REPORT_BENCH_BRIDGE 才会跑，正常测试套件里自动跳过。
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

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
    std::cerr << "load_published_tree      " << ms_since(start) << " ms\n";
    ASSERT_TRUE(tree.has_value());

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

    ASSERT_TRUE(inventory.has_value());
    start = Clock::now();
    const auto summary = bridge_report::db::ComponentInventoryRepository(client)
                             .load_summary(inventory->id);
    std::cerr << "load_summary             " << ms_since(start) << " ms\n";

    start = Clock::now();
    const auto report = bridge_report::review::match_defect_rating_tree_nodes(
        draft, tree_version, technical_package, *tree, inventory,
        bridge_report::review::DefectMatchScope{}, false);
    std::cerr << "match_defect_rating_tree_nodes " << ms_since(start)
              << " ms  记录 " << report.records.size() << " 条\n\n";
}

}  // namespace
