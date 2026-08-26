#include <cstdlib>
#include <set>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/db/TriageQueryRepository.hpp"

namespace {

// 一座桥、两个已确认年度，用来验取数口径。故意掺进三种**不该出现在整理台上**的观测：
// 旧修订版年度的、还没定稿的、以及已经绑过线索的。
class TriageQueryRepositoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) {
            GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL 未设置，跳过需要真实数据库的集成测试";
        }
        const bridge_report::config::PostgresConfig config{};
        client_ = bridge_report::db::create_db_client(config, 1);

        bridge_id_ = insert_id("insert into bridges(bridge_name) values('整理取数测试桥') returning id");
        component_id_ = insert_id(
            "insert into bridge_components(bridge_id,structure_part,component_type,"
            "business_component_code,normalized_component_key,creation_source) "
            "values($1::uuid,'上部结构','铰缝','1#铰缝','triage-hinge-1','人工录入') returning id",
            bridge_id_);
        other_component_id_ = insert_id(
            "insert into bridge_components(bridge_id,structure_part,component_type,"
            "business_component_code,normalized_component_key,creation_source) "
            "values($1::uuid,'上部结构','铰缝','2#铰缝','triage-hinge-2','人工录入') returning id",
            bridge_id_);

        year_2025_ = insert_year(2025, "已确认", true);
        year_2026_ = insert_year(2026, "已确认", true);
        // 同年度的旧修订版：已被 v2 取代，其观测不该进整理台。
        superseded_year_ = insert_year(2024, "已被修订", false);

        insert_observation(year_2025_, component_id_, "渗水泛碱", "", "已确认");
        insert_observation(year_2026_, component_id_, "渗水泛碱", "", "已修改");
        insert_observation(superseded_year_, component_id_, "渗水泛碱", "", "已确认");
        // 观测表的未定稿状态是"待校对"，不是草稿里的"待确认"——两张表用词不同。
        insert_observation(year_2026_, other_component_id_, "渗水泛碱", "", "待校对");
    }

    void TearDown() override {
        if (client_ == nullptr || bridge_id_.empty()) return;
        client_->execSqlSync("delete from defect_observations where bridge_id=$1::uuid", bridge_id_);
        client_->execSqlSync("delete from defect_threads where bridge_id=$1::uuid", bridge_id_);
        client_->execSqlSync("delete from inspection_years where bridge_id=$1::uuid", bridge_id_);
        client_->execSqlSync("delete from bridge_components where bridge_id=$1::uuid", bridge_id_);
        client_->execSqlSync("delete from bridges where id=$1::uuid", bridge_id_);
    }

    template <typename... Args>
    std::string insert_id(const std::string& sql, Args&&... args) {
        return client_->execSqlSync(sql, std::forward<Args>(args)...)[0]["id"]
            .template as<std::string>();
    }

    std::string insert_year(int year, const std::string& status, bool is_current) {
        return insert_id(
            "insert into inspection_years(bridge_id,inspection_year,status,version_number,is_current) "
            "values($1::uuid,$2,$3,1,$4) returning id",
            bridge_id_, year, status, is_current);
    }

    std::string insert_observation(
        const std::string& year_id, const std::string& component_id,
        const std::string& defect_type, const std::string& location,
        const std::string& review_status) {
        return insert_id(
            "insert into defect_observations(inspection_year_id,bridge_id,bridge_component_id,"
            "structure_part,defect_type,defect_description_raw,defect_location,review_status) "
            "values($1::uuid,$2::uuid,$3::uuid,'上部结构',$4,$4,nullif($5,''),$6) returning id",
            year_id, bridge_id_, component_id, defect_type, location, review_status);
    }

    std::string insert_thread(const std::string& component_id, const std::string& defect_type,
                              const std::string& location) {
        return insert_id(
            "insert into defect_threads(bridge_id,bridge_component_id,thread_name,defect_type,"
            "defect_location) values($1::uuid,$2::uuid,$3,$4,nullif($5,'')) returning id",
            bridge_id_, component_id, defect_type + "｜" + location, defect_type, location);
    }

    drogon::orm::DbClientPtr client_;
    std::string bridge_id_;
    std::string component_id_;
    std::string other_component_id_;
    std::string year_2025_;
    std::string year_2026_;
    std::string superseded_year_;
};

}  // namespace

// 三种观测必须被挡在外面：旧修订版年度的、还没定稿的、已绑线索的。
TEST_F(TriageQueryRepositoryTest, TakesOnlyUnboundFormalObservationsOfCurrentYears) {
    const bridge_report::db::TriageQueryRepository repository(client_);

    const auto model = repository.load_model(bridge_id_);

    EXPECT_EQ(model.unbound_observation_count, 2)
        << "只有 2025 已确认与 2026 已修改这两条算数（待校对的、旧修订版的都不算）";
    ASSERT_EQ(model.batches.size(), 1u);
    EXPECT_EQ(model.batches[0].year_set, (std::vector<int>{2025, 2026}))
        << "旧修订版的 2024 不该混进年份集合";
    EXPECT_EQ(model.batches[0].groups.size(), 1u);
}

TEST_F(TriageQueryRepositoryTest, DropsObservationsThatAlreadyHaveAThread) {
    const auto thread_id = insert_thread(component_id_, "渗水泛碱", "");
    client_->execSqlSync(
        "update defect_observations set defect_thread_id=$1::uuid "
        "where inspection_year_id=$2::uuid and bridge_component_id=$3::uuid",
        thread_id, year_2025_, component_id_);

    const auto model = bridge_report::db::TriageQueryRepository(client_).load_model(bridge_id_);

    EXPECT_EQ(model.unbound_observation_count, 1) << "已绑的那条不再是待整理";
}

// 空位置在库里是 null；取数必须把它归到规范空值上，否则 166 组那种批次会散架。
TEST_F(TriageQueryRepositoryTest, TreatsANullLocationAsTheEmptyCanonicalValue) {
    const auto model = bridge_report::db::TriageQueryRepository(client_).load_model(bridge_id_);

    ASSERT_EQ(model.batches.size(), 1u);
    EXPECT_TRUE(model.batches[0].groups[0].key.normalized_defect_location.empty());
    EXPECT_TRUE(model.batches[0].defect_location.empty());
}

// 有已有线索时应当认出精确命中并转成 bind——增量年度靠的就是这条。
TEST_F(TriageQueryRepositoryTest, TurnsAnExactlyMatchedGroupIntoABind) {
    const auto thread_id = insert_thread(component_id_, "渗水泛碱", "");

    const auto model = bridge_report::db::TriageQueryRepository(client_).load_model(bridge_id_);

    ASSERT_EQ(model.batches.size(), 1u);
    EXPECT_EQ(model.batches[0].action, bridge_report::review::TriageAction::Bind);
    ASSERT_TRUE(model.batches[0].groups[0].matched_thread_id.has_value());
    EXPECT_EQ(*model.batches[0].groups[0].matched_thread_id, thread_id);
}

TEST_F(TriageQueryRepositoryTest, SummarisesWithoutTheBatchObservations) {
    const auto body = bridge_report::db::TriageQueryRepository(client_).summary(bridge_id_);

    EXPECT_EQ(body["unbound_observation_count"].asInt(), 2);
    ASSERT_EQ(body["batches"].size(), 1u);
    EXPECT_FALSE(body["batches"][0].isMember("groups"));
    EXPECT_FALSE(body["batches"][0]["sample_groups"].empty());
    EXPECT_FALSE(body["snapshot_id"].asString().empty());
}

// 明细必须自带每条观测的 updated_at：提交时拿它做并发校验，少一条那一组就提交不了。
TEST_F(TriageQueryRepositoryTest, CarriesEveryObservationTokenInTheBatchDetail) {
    const bridge_report::db::TriageQueryRepository repository(client_);
    const auto summary = repository.summary(bridge_id_);
    ASSERT_EQ(summary["batches"].size(), 1u);

    const auto detail = repository.batch_detail(
        bridge_id_, summary["batches"][0]["batch_id"].asString());

    ASSERT_TRUE(detail.has_value());
    EXPECT_EQ((*detail)["observation_count"].asInt(), 2);
    ASSERT_EQ((*detail)["groups"].size(), 1u);
    const auto& observations = (*detail)["groups"][0]["observations"];
    ASSERT_EQ(observations.size(), 2u);
    for (const auto& observation : observations) {
        EXPECT_FALSE(observation["updated_at"].asString().empty());
        EXPECT_FALSE(observation["id"].asString().empty());
    }
    EXPECT_EQ((*detail)["groups"][0]["business_component_code"].asString(), "1#铰缝")
        << "构件业务编号必须给：只显示'铰缝'的话人分不出是哪一个";
}

// 批次跨多个构件时各绑各的线索，目标必须逐组给，不能是批次级的单一编号。
TEST_F(TriageQueryRepositoryTest, GivesEachGroupItsOwnTargetThread) {
    const auto first_thread = insert_thread(component_id_, "渗水泛碱", "");
    const auto second_component = insert_id(
        "insert into bridge_components(bridge_id,structure_part,component_type,"
        "business_component_code,normalized_component_key,creation_source) "
        "values($1::uuid,'上部结构','铰缝','3#铰缝','triage-hinge-3','人工录入') returning id",
        bridge_id_);
    const auto second_thread = insert_thread(second_component, "渗水泛碱", "");
    insert_observation(year_2025_, second_component, "渗水泛碱", "", "已确认");
    insert_observation(year_2026_, second_component, "渗水泛碱", "", "已确认");

    const bridge_report::db::TriageQueryRepository repository(client_);
    const auto summary = repository.summary(bridge_id_);
    ASSERT_EQ(summary["batches"].size(), 1u);
    const auto detail = repository.batch_detail(
        bridge_id_, summary["batches"][0]["batch_id"].asString());

    ASSERT_TRUE(detail.has_value());
    EXPECT_EQ((*detail)["action"].asString(), "bind");
    ASSERT_EQ((*detail)["groups"].size(), 2u);
    std::set<std::string> targets;
    for (const auto& group : (*detail)["groups"]) {
        ASSERT_FALSE(group["target_thread"].isNull());
        targets.insert(group["target_thread"]["thread_id"].asString());
        EXPECT_FALSE(group["target_thread"]["system_number"].asString().empty())
            << "展开后要显示各自的 BHXS 编号";
    }
    EXPECT_EQ(targets, (std::set<std::string>{first_thread, second_thread}));
}

// 数据变了导致批次消失时，不能把过期批次当空批次交给用户。
TEST_F(TriageQueryRepositoryTest, ReportsAVanishedBatchAsMissing) {
    const bridge_report::db::TriageQueryRepository repository(client_);

    EXPECT_FALSE(repository.batch_detail(bridge_id_, "0123456789abcdef0123456789abcdef")
                     .has_value());
}
