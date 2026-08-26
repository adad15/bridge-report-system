#include <algorithm>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/db/ThreadResolutionRepository.hpp"
#include "bridge_report/db/TriageQueryRepository.hpp"

namespace db = bridge_report::db;
namespace review = bridge_report::review;

namespace {

// 两个铰缝各三年，长相相同 → 一个 create 批次两组。用来验三阶段事务与服务端重算。
class ThreadResolutionRepositoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) {
            GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL 未设置，跳过需要真实数据库的集成测试";
        }
        const bridge_report::config::PostgresConfig config{};
        client_ = bridge_report::db::create_db_client(config, 1);

        bridge_id_ = insert_id("insert into bridges(bridge_name) values('线索落库测试桥') returning id");
        other_bridge_id_ = insert_id("insert into bridges(bridge_name) values('线索落库旁桥') returning id");
        component_a_ = insert_component(bridge_id_, "1#铰缝", "resolve-hinge-1");
        component_b_ = insert_component(bridge_id_, "2#铰缝", "resolve-hinge-2");
        year_2025_ = insert_year(bridge_id_, 2025);
        year_2026_ = insert_year(bridge_id_, 2026);

        for (const auto& component : {component_a_, component_b_}) {
            insert_observation(year_2025_, bridge_id_, component, "渗水泛碱", "");
            insert_observation(year_2026_, bridge_id_, component, "渗水泛碱", "");
        }
    }

    void TearDown() override {
        if (client_ == nullptr || bridge_id_.empty()) return;
        for (const auto& bridge : {bridge_id_, other_bridge_id_}) {
            client_->execSqlSync("delete from defect_comparisons where bridge_id=$1::uuid", bridge);
            client_->execSqlSync("delete from defect_observations where bridge_id=$1::uuid", bridge);
            client_->execSqlSync("delete from defect_threads where bridge_id=$1::uuid", bridge);
            client_->execSqlSync("delete from inspection_years where bridge_id=$1::uuid", bridge);
            client_->execSqlSync("delete from bridge_components where bridge_id=$1::uuid", bridge);
            client_->execSqlSync("delete from bridges where id=$1::uuid", bridge);
        }
    }

    template <typename... Args>
    std::string insert_id(const std::string& sql, Args&&... args) {
        return client_->execSqlSync(sql, std::forward<Args>(args)...)[0]["id"]
            .template as<std::string>();
    }

    std::string insert_component(const std::string& bridge, const std::string& code,
                                 const std::string& key) {
        return insert_id(
            "insert into bridge_components(bridge_id,structure_part,component_type,"
            "business_component_code,normalized_component_key,creation_source) "
            "values($1::uuid,'上部结构','铰缝',$2,$3,'人工录入') returning id",
            bridge, code, key);
    }

    std::string insert_year(const std::string& bridge, int year) {
        return insert_id(
            "insert into inspection_years(bridge_id,inspection_year,status,version_number,is_current) "
            "values($1::uuid,$2,'已确认',1,true) returning id",
            bridge, year);
    }

    std::string insert_observation(const std::string& year_id, const std::string& bridge,
                                   const std::string& component_id,
                                   const std::string& defect_type, const std::string& location) {
        return insert_id(
            "insert into defect_observations(inspection_year_id,bridge_id,bridge_component_id,"
            "structure_part,defect_type,defect_description_raw,defect_location,review_status) "
            "values($1::uuid,$2::uuid,$3::uuid,'上部结构',$4,$4,nullif($5,''),'已确认') returning id",
            year_id, bridge, component_id, defect_type, location);
    }

    /// 照工作台明细那样构造一份请求：批次里的全部组、全部观测、各自的并发令牌。
    db::TriageApplyRequest request_from_batch(const review::TriageBatch& batch) const {
        db::TriageApplyRequest request;
        request.bridge_id = bridge_id_;
        request.batch_id = batch.batch_id;
        request.batch_fingerprint = batch.fingerprint;
        request.action = batch.action;
        for (const auto& group : batch.groups) {
            db::TriageApplyGroup apply_group;
            apply_group.group_id = group.group_id;
            apply_group.target_thread_id = group.matched_thread_id.value_or("");
            for (const auto& observation : group.observations) {
                apply_group.observations.push_back(
                    db::TriageApplyObservation{observation.id, observation.updated_at});
            }
            request.groups.push_back(std::move(apply_group));
        }
        return request;
    }

    review::TriageBatch only_batch() const {
        const auto model = db::TriageQueryRepository(client_).load_model(bridge_id_);
        EXPECT_EQ(model.batches.size(), 1u);
        return model.batches.front();
    }

    int count_threads() const {
        return client_->execSqlSync(
            "select count(*) as n from defect_threads where bridge_id=$1::uuid", bridge_id_)
            [0]["n"].as<int>();
    }

    int count_bound() const {
        return client_->execSqlSync(
            "select count(*) as n from defect_observations "
            "where bridge_id=$1::uuid and defect_thread_id is not null", bridge_id_)
            [0]["n"].as<int>();
    }

    bool has_issue(const db::TriageApplyOutcome& outcome, const std::string& code) const {
        return std::any_of(outcome.issues.begin(), outcome.issues.end(),
                           [&code](const db::TriageApplyIssue& issue) { return issue.code == code; });
    }

    drogon::orm::DbClientPtr client_;
    std::string bridge_id_;
    std::string other_bridge_id_;
    std::string component_a_;
    std::string component_b_;
    std::string year_2025_;
    std::string year_2026_;
};

}  // namespace

TEST_F(ThreadResolutionRepositoryTest, CreatesOneThreadPerGroupAndBindsEveryObservation) {
    const auto batch = only_batch();
    ASSERT_EQ(batch.groups.size(), 2u);

    const auto outcome = db::ThreadResolutionRepository(client_).apply(request_from_batch(batch));

    ASSERT_EQ(outcome.status, db::TriageApplyStatus::Applied) << outcome.issues.size() << " 条问题";
    EXPECT_EQ(outcome.threads_created, 2);
    EXPECT_EQ(outcome.observations_bound, 4);
    EXPECT_EQ(count_threads(), 2);
    EXPECT_EQ(count_bound(), 4);

    // 跨年跨度集中重算：两个年度都绑上了，首见与末见必须分别指向 2025 与 2026。
    const auto spans = client_->execSqlSync(
        "select first_seen_inspection_id::text as first_id, latest_seen_inspection_id::text as last_id "
        "from defect_threads where bridge_id=$1::uuid order by system_number", bridge_id_);
    for (const auto& row : spans) {
        EXPECT_EQ(row["first_id"].as<std::string>(), year_2025_);
        EXPECT_EQ(row["last_id"].as<std::string>(), year_2026_);
    }
}

// 空位置的线索名只有病害类型。旧默认逻辑会产出"渗水泛碱｜"这种带悬空分隔符的名字，
// 而系统当前没有改名接口，这个名字要长期挂在档案上。
TEST_F(ThreadResolutionRepositoryTest, NamesAnUnlocatedThreadWithoutADanglingSeparator) {
    db::ThreadResolutionRepository(client_).apply(request_from_batch(only_batch()));

    const auto rows = client_->execSqlSync(
        "select thread_name, defect_location from defect_threads where bridge_id=$1::uuid limit 1",
        bridge_id_);
    ASSERT_FALSE(rows.empty());
    EXPECT_EQ(rows[0]["thread_name"].as<std::string>(), "渗水泛碱");
    EXPECT_TRUE(rows[0]["defect_location"].isNull()) << "库里存 null，不是空字符串";
}

// 全成或全败：一条令牌过期，整批回滚，数据库里一条线索都不许留下。
TEST_F(ThreadResolutionRepositoryTest, RollsBackTheWholeBatchWhenOneTokenIsStale) {
    auto request = request_from_batch(only_batch());
    request.groups[1].observations[0].updated_at = "1970-01-01 00:00:00+00";

    const auto outcome = db::ThreadResolutionRepository(client_).apply(request);

    EXPECT_EQ(outcome.status, db::TriageApplyStatus::Rejected);
    EXPECT_TRUE(has_issue(outcome, "observation_revision_conflict"));
    EXPECT_EQ(count_threads(), 0) << "回滚不彻底的话这里会留下第一组的线索";
    EXPECT_EQ(count_bound(), 0);
}

// 失败明细要一次给全，不是遇到第一个就返回——否则用户得反复提交才能看完问题。
TEST_F(ThreadResolutionRepositoryTest, CollectsEveryProblemRatherThanTheFirst) {
    auto request = request_from_batch(only_batch());
    request.groups[0].observations[0].updated_at = "1970-01-01 00:00:00+00";
    request.groups[1].observations[0].updated_at = "1970-01-01 00:00:00+00";

    const auto outcome = db::ThreadResolutionRepository(client_).apply(request);

    EXPECT_EQ(outcome.status, db::TriageApplyStatus::Rejected);
    EXPECT_GE(outcome.issues.size(), 2u) << "两个组各有一处问题，两条都要报出来";
}

// 客户端把跨桥的观测塞进来：服务端从锁定后的行重算，必须整批拒绝。
TEST_F(ThreadResolutionRepositoryTest, RejectsAnObservationFromAnotherBridge) {
    const auto foreign_component = insert_component(other_bridge_id_, "9#铰缝", "resolve-foreign");
    const auto foreign_year = insert_year(other_bridge_id_, 2026);
    const auto foreign_observation = insert_observation(
        foreign_year, other_bridge_id_, foreign_component, "渗水泛碱", "");
    const auto token = client_->execSqlSync(
        "select updated_at::text as t from defect_observations where id=$1::uuid",
        foreign_observation)[0]["t"].as<std::string>();

    auto request = request_from_batch(only_batch());
    request.groups[0].observations.push_back(
        db::TriageApplyObservation{foreign_observation, token});

    const auto outcome = db::ThreadResolutionRepository(client_).apply(request);

    EXPECT_EQ(outcome.status, db::TriageApplyStatus::Rejected);
    EXPECT_TRUE(has_issue(outcome, "observation_bridge_mismatch"));
    EXPECT_EQ(count_threads(), 0);
}

// 把两个构件的观测混进同一组：请求说它们是一处病害，数据库说不是——听数据库的。
TEST_F(ThreadResolutionRepositoryTest, RejectsAGroupSpanningTwoComponents) {
    auto request = request_from_batch(only_batch());
    ASSERT_EQ(request.groups.size(), 2u);
    request.groups[0].observations.push_back(request.groups[1].observations.front());
    request.groups[1].observations.erase(request.groups[1].observations.begin());

    const auto outcome = db::ThreadResolutionRepository(client_).apply(request);

    EXPECT_EQ(outcome.status, db::TriageApplyStatus::Rejected);
    EXPECT_TRUE(has_issue(outcome, "group_component_mismatch"));
    EXPECT_EQ(count_threads(), 0);
}

TEST_F(ThreadResolutionRepositoryTest, RejectsTheSameObservationInTwoGroups) {
    auto request = request_from_batch(only_batch());
    request.groups[1].observations[0] = request.groups[0].observations[0];

    const auto outcome = db::ThreadResolutionRepository(client_).apply(request);

    EXPECT_EQ(outcome.status, db::TriageApplyStatus::Rejected);
    EXPECT_TRUE(has_issue(outcome, "duplicate_observation"));
    EXPECT_EQ(count_threads(), 0);
}

// create 组若已经存在精确匹配的线索，说明读取之后有人建过——不能再造第二条。
TEST_F(ThreadResolutionRepositoryTest, RefusesToCreateWhenAThreadAlreadyMatches) {
    const auto request = request_from_batch(only_batch());
    client_->execSqlSync(
        "insert into defect_threads(bridge_id,bridge_component_id,thread_name,defect_type) "
        "values($1::uuid,$2::uuid,'渗水泛碱','渗水泛碱')",
        bridge_id_, component_a_);

    const auto outcome = db::ThreadResolutionRepository(client_).apply(request);

    EXPECT_EQ(outcome.status, db::TriageApplyStatus::Rejected);
    EXPECT_TRUE(has_issue(outcome, "unexpected_existing_thread"));
    EXPECT_EQ(count_bound(), 0);
}

// 旧修订版年度的观测不是当前事实，绑了没有意义。
TEST_F(ThreadResolutionRepositoryTest, RejectsObservationsOfASupersededYear) {
    const auto request = request_from_batch(only_batch());
    client_->execSqlSync(
        "update inspection_years set is_current=false, status='已被修订' where id=$1::uuid",
        year_2026_);

    const auto outcome = db::ThreadResolutionRepository(client_).apply(request);

    EXPECT_EQ(outcome.status, db::TriageApplyStatus::Rejected);
    EXPECT_TRUE(has_issue(outcome, "observation_not_current"));
    EXPECT_EQ(count_threads(), 0);
}

// 首次绑定路径原本根本不查模块 07 引用（现有代码只在重绑时查），批量必须显式补上。
TEST_F(ThreadResolutionRepositoryTest, RejectsAnObservationHeldByAConfirmedComparison) {
    const auto request = request_from_batch(only_batch());
    const auto observation_id = request.groups[0].observations[0].id;
    client_->execSqlSync(
        "insert into defect_comparisons(bridge_id,current_inspection_year_id,"
        "compared_inspection_year_id,current_defect_observation_id,comparison_result,"
        "confirmation_status) values($1::uuid,$2::uuid,$3::uuid,$4::uuid,'延续','人工已确认')",
        bridge_id_, year_2026_, year_2025_, observation_id);

    const auto outcome = db::ThreadResolutionRepository(client_).apply(request);

    EXPECT_EQ(outcome.status, db::TriageApplyStatus::Rejected);
    EXPECT_TRUE(has_issue(outcome, "observation_referenced_by_confirmed_comparison"));
    EXPECT_EQ(count_threads(), 0);
}

// ── 幂等：判定必须先于令牌校验 ────────────────────────────────────────
// 第一次成功会把观测的 updated_at 推新，响应丢失后原样重试必然携带旧令牌。若按常规顺序
// 先校验令牌，服务端会直接报冲突，根本走不到"这批其实已经做完了"的判断。

TEST_F(ThreadResolutionRepositoryTest, AnswersAlreadyCompletedToARetryCarryingStaleTokens) {
    const auto request = request_from_batch(only_batch());
    const db::ThreadResolutionRepository repository(client_);
    ASSERT_EQ(repository.apply(request).status, db::TriageApplyStatus::Applied);
    ASSERT_EQ(count_threads(), 2);

    // 原封不动重放：令牌已经过期了。
    const auto retry = repository.apply(request);

    EXPECT_EQ(retry.status, db::TriageApplyStatus::AlreadyCompleted);
    EXPECT_EQ(count_threads(), 2) << "重试绝不能再建一遍线索";
    EXPECT_EQ(count_bound(), 4);
    EXPECT_EQ(retry.threads_created, 0);
    ASSERT_EQ(retry.results.size(), 2u);
    for (const auto& result : retry.results) {
        EXPECT_EQ(result.outcome, "already_completed");
    }
}

TEST_F(ThreadResolutionRepositoryTest, AnswersAlreadyCompletedToABindRetry) {
    const db::ThreadResolutionRepository repository(client_);
    ASSERT_EQ(repository.apply(request_from_batch(only_batch())).status,
              db::TriageApplyStatus::Applied);
    // 解绑 2026 那两条，让它们重新成为一个 bind 批次。
    client_->execSqlSync(
        "update defect_observations set defect_thread_id=null, updated_at=now() "
        "where inspection_year_id=$1::uuid", year_2026_);
    const auto bind_request = request_from_batch(only_batch());
    ASSERT_EQ(bind_request.action, review::TriageAction::Bind);
    ASSERT_EQ(repository.apply(bind_request).status, db::TriageApplyStatus::Applied);

    const auto retry = repository.apply(bind_request);

    EXPECT_EQ(retry.status, db::TriageApplyStatus::AlreadyCompleted);
    EXPECT_EQ(count_threads(), 2);
}

// 部分绑定不是幂等：批次是原子的，出现这种状态说明是别人动过，不能当"已完成"放过。
TEST_F(ThreadResolutionRepositoryTest, RefusesToCallAPartiallyBoundGroupCompleted) {
    const auto request = request_from_batch(only_batch());
    const db::ThreadResolutionRepository repository(client_);
    ASSERT_EQ(repository.apply(request).status, db::TriageApplyStatus::Applied);
    // 把第一组里的一条解绑，制造"半绑"。
    client_->execSqlSync(
        "update defect_observations set defect_thread_id=null, updated_at=now() where id=$1::uuid",
        request.groups[0].observations[0].id);

    const auto retry = repository.apply(request);

    EXPECT_EQ(retry.status, db::TriageApplyStatus::Rejected);
    EXPECT_TRUE(has_issue(retry, "partially_bound"));
}

// create 的幂等判据是"终态达成"，不是"这条线索是我建的"。别人先建了一条规范键完全相同
// 的线索并绑上了这些观测，用户要的结果就已经在那儿了——判成冲突只会让人对着一个已经
// 正确的状态发懵。
TEST_F(ThreadResolutionRepositoryTest, TreatsASameKeyThreadAsCompletionForCreate) {
    const auto request = request_from_batch(only_batch());
    const auto other_thread = insert_id(
        "insert into defect_threads(bridge_id,bridge_component_id,thread_name,defect_type) "
        "values($1::uuid,$2::uuid,'别人建的渗水泛碱','渗水泛碱') returning id",
        bridge_id_, component_a_);
    for (const auto& observation : request.groups[0].observations) {
        client_->execSqlSync(
            "update defect_observations set defect_thread_id=$1::uuid, updated_at=now() "
            "where id=$2::uuid", other_thread, observation.id);
    }

    const auto outcome = db::ThreadResolutionRepository(client_).apply(request);

    // 第一组已达成、第二组还没有 → 批次不是原子完成的，让人刷新后重来。
    EXPECT_EQ(outcome.status, db::TriageApplyStatus::Rejected);
    EXPECT_TRUE(has_issue(outcome, "batch_partially_applied"));
    EXPECT_FALSE(has_issue(outcome, "bound_to_other_thread"))
        << "同键线索是达成，不是走岔了";
}

// 绑到了规范键**不同**的线索：这才是真的走岔了。
TEST_F(ThreadResolutionRepositoryTest, RefusesWhenObservationsWentToAThreadOfAnotherKind) {
    const auto request = request_from_batch(only_batch());
    const auto unrelated_thread = insert_id(
        "insert into defect_threads(bridge_id,bridge_component_id,thread_name,defect_type,"
        "defect_location) values($1::uuid,$2::uuid,'横向裂缝｜端部','横向裂缝','端部') returning id",
        bridge_id_, component_a_);
    for (const auto& observation : request.groups[0].observations) {
        client_->execSqlSync(
            "update defect_observations set defect_thread_id=$1::uuid, updated_at=now() "
            "where id=$2::uuid", unrelated_thread, observation.id);
    }

    const auto outcome = db::ThreadResolutionRepository(client_).apply(request);

    EXPECT_EQ(outcome.status, db::TriageApplyStatus::Rejected);
    EXPECT_TRUE(has_issue(outcome, "bound_to_other_thread"));
}

// bind 比 create 严一格：规范键相同还不够，必须就是请求指定的那条。
TEST_F(ThreadResolutionRepositoryTest, RefusesABindWhoseObservationsWentToADifferentThread) {
    const db::ThreadResolutionRepository repository(client_);
    ASSERT_EQ(repository.apply(request_from_batch(only_batch())).status,
              db::TriageApplyStatus::Applied);
    client_->execSqlSync(
        "update defect_observations set defect_thread_id=null, updated_at=now() "
        "where inspection_year_id=$1::uuid", year_2026_);
    auto bind_request = request_from_batch(only_batch());
    ASSERT_EQ(bind_request.action, review::TriageAction::Bind);
    ASSERT_EQ(repository.apply(bind_request).status, db::TriageApplyStatus::Applied);

    // 请求仍指向原目标，实际却被挪到了另一条同键线索上。
    const auto decoy = insert_id(
        "insert into defect_threads(bridge_id,bridge_component_id,thread_name,defect_type) "
        "values($1::uuid,$2::uuid,'另一条渗水泛碱','渗水泛碱') returning id",
        bridge_id_, component_a_);
    for (const auto& observation : bind_request.groups[0].observations) {
        client_->execSqlSync(
            "update defect_observations set defect_thread_id=$1::uuid where id=$2::uuid",
            decoy, observation.id);
    }

    const auto outcome = repository.apply(bind_request);

    EXPECT_EQ(outcome.status, db::TriageApplyStatus::Rejected);
    EXPECT_TRUE(has_issue(outcome, "bound_to_other_thread"));
}

// 组内观测散落在两条线索上：既不是已完成，也不是简单的重复绑定。
TEST_F(ThreadResolutionRepositoryTest, ReportsAGroupSplitAcrossTwoThreads) {
    const auto request = request_from_batch(only_batch());
    const auto thread_one = insert_id(
        "insert into defect_threads(bridge_id,bridge_component_id,thread_name,defect_type) "
        "values($1::uuid,$2::uuid,'渗水泛碱 A','渗水泛碱') returning id",
        bridge_id_, component_a_);
    const auto thread_two = insert_id(
        "insert into defect_threads(bridge_id,bridge_component_id,thread_name,defect_type) "
        "values($1::uuid,$2::uuid,'渗水泛碱 B','渗水泛碱') returning id",
        bridge_id_, component_a_);
    ASSERT_EQ(request.groups[0].observations.size(), 2u);
    client_->execSqlSync(
        "update defect_observations set defect_thread_id=$1::uuid, updated_at=now() where id=$2::uuid",
        thread_one, request.groups[0].observations[0].id);
    client_->execSqlSync(
        "update defect_observations set defect_thread_id=$1::uuid, updated_at=now() where id=$2::uuid",
        thread_two, request.groups[0].observations[1].id);

    const auto retry = db::ThreadResolutionRepository(client_).apply(request);

    EXPECT_EQ(retry.status, db::TriageApplyStatus::Rejected);
    EXPECT_TRUE(has_issue(retry, "thread_split_conflict"));
}
