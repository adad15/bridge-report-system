#include <cstdlib>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/db/WordImportRepository.hpp"
#include "bridge_report/resolution/ImportResolutionService.hpp"

namespace {

using bridge_report::resolution::BulkReplaceIntent;
using bridge_report::resolution::ImportResolutionService;
using bridge_report::resolution::InventoryRepointIntent;
using bridge_report::resolution::RangeExpandIntent;
using bridge_report::resolution::ResolutionCommandContext;
using bridge_report::resolution::ResolutionStatus;

class ResolutionPlanTest : public testing::Test {
protected:
    void SetUp() override {
        if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) {
            GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL is not set";
        }
        client_ = bridge_report::db::create_db_client(bridge_report::config::PostgresConfig{});
        user_id_ = client_->execSqlSync(
            "select id::text from users where username='admin'")[0]["id"].as<std::string>();
        bridge_id_ = client_->execSqlSync(
            "insert into bridges (bridge_name) values ('预览计划测试桥') returning id::text"
        )[0]["id"].as<std::string>();
        year_id_ = client_->execSqlSync(
            "insert into inspection_years (bridge_id, inspection_year, status, is_current) "
            "values ($1::uuid,2026,'待校对',true) returning id::text",
            bridge_id_)[0]["id"].as<std::string>();
        import_id_ = client_->execSqlSync(
            "insert into import_records (bridge_id, inspection_year_id, import_name,"
            "source_type, import_status) values ($1::uuid,$2::uuid,'预览计划测试',"
            "'接口同步','解析中') returning id::text",
            bridge_id_, year_id_)[0]["id"].as<std::string>();
        source_file_id_ = client_->execSqlSync(
            "insert into import_source_files (import_record_id, original_file_name,"
            "storage_relative_path, file_extension, file_size_bytes, file_hash, status,"
            "parsing_started_at) values ($1::uuid,'sync.srcref',$1::text||'.srcref',"
            "'.srcref',9,$2,'解析中',now()) returning id::text",
            import_id_, std::string(64, 'a'))[0]["id"].as<std::string>();
        seed_inventory();
    }

    void TearDown() override {
        if (!client_) return;
        client_->execSqlSync("delete from import_source_files where id=$1::uuid", source_file_id_);
        client_->execSqlSync("delete from import_records where id=$1::uuid", import_id_);
        client_->execSqlSync(
            "update inspection_years set component_inventory_revision_id=null where id=$1::uuid",
            year_id_);
        client_->execSqlSync(
            "delete from bridge_component_inventory_revisions where bridge_id=$1::uuid",
            bridge_id_);
        client_->execSqlSync("delete from bridge_components where bridge_id=$1::uuid", bridge_id_);
        client_->execSqlSync("delete from inspection_years where id=$1::uuid", year_id_);
        client_->execSqlSync("delete from bridges where id=$1::uuid", bridge_id_);
        if (!package_id_.empty()) {
            client_->execSqlSync("delete from standard_packages where id=$1::uuid", package_id_);
        }
        client_->closeAll();
    }

    /// 台账里放 1#跨桥面铺装 / 2#跨桥面铺装 / 3#跨桥面铺装。
    void seed_inventory() {
        package_id_ = client_->execSqlSync(
            "insert into standard_packages(standard_family,standard_id,standard_code,"
            "standard_name,official_edition,package_version,contract_version,algorithm_id,"
            "effective_date,content_checksum) values('technical_condition',"
            "'PL-'||gen_random_uuid()::text,'PL','预览计划测试规范','2026','1.0.0',1,'pl',"
            "'2026-01-01','sha256:'||repeat('9',64)) returning id::text"
        )[0]["id"].as<std::string>();
        revision_id_ = client_->execSqlSync(
            "insert into bridge_component_inventory_revisions(bridge_id,revision_number,"
            "created_by_user_id) values($1::uuid,1,$2::uuid) returning id::text",
            bridge_id_, user_id_)[0]["id"].as<std::string>();
        for (int index = 1; index <= 3; ++index) {
            const auto number = std::to_string(index) + "#跨桥面铺装";
            const auto component_id = client_->execSqlSync(
                "insert into bridge_components(bridge_id,structure_part,component_type,"
                "business_component_code,normalized_component_key) "
                "values($1::uuid,'桥面系','桥面铺装',$2,$3) returning id::text",
                bridge_id_, number, "plan-" + std::to_string(index)
            )[0]["id"].as<std::string>();
            component_ids_.push_back(component_id);
            const auto entry_id = client_->execSqlSync(
                "insert into bridge_component_inventory_entries(inventory_revision_id,"
                "bridge_component_id,component_number,site_name,site_component_type,sort_order) "
                "values($1::uuid,$2::uuid,$3,'沥青铺装','沥青铺装',$4) returning id::text",
                revision_id_, component_id, number, index)[0]["id"].as<std::string>();
            client_->execSqlSync(
                "insert into bridge_component_standard_mappings(inventory_entry_id,"
                "standard_package_id,standard_bridge_type_id,standard_component_category_id,"
                "structure_part,mapping_source,confirmation_status,confirmed_by_user_id,"
                "confirmed_at) values($1::uuid,$2::uuid,'h21.bridge_type.beam',"
                "'h21.component.deck.pavement','deck_system','规范模板','已确认',"
                "$3::uuid,now())",
                entry_id, package_id_, user_id_);
        }
        client_->execSqlSync(
            "update bridge_component_inventory_revisions set status='已确认',"
            "confirmed_by_user_id=$2::uuid,confirmed_at=now() where id=$1::uuid",
            revision_id_, user_id_);
    }

    void import_defects(const std::vector<std::string>& numbers) {
        bridge_report::archive::ArchivedPhotoBatch batch;
        batch.data["contract"]["parser_name"] = "source-db-importer";
        batch.data["contract"]["parser_version"] = "1.0.0";
        batch.data["photos"] = Json::Value(Json::arrayValue);
        int index = 1;
        for (const auto& number : numbers) {
            Json::Value defect(Json::objectValue);
            defect["candidate_id"] = "source_defect_" + std::to_string(index++);
            defect["component_name"] = "桥面铺装";
            defect["component_number"] = number;
            defect["defect_type"] = "坑槽";
            defect["defect_location"] = "行车道";
            defect["defect_description"] = "路面出现坑槽";
            defect["warnings"] = Json::Value(Json::arrayValue);
            batch.data["defects"].append(defect);
        }
        const auto outcome =
            bridge_report::db::WordImportRepository(client_).persist_parse_result(
                import_id_, batch);
        ASSERT_TRUE(outcome.success) << outcome.error_code << ": " << outcome.error_message;
    }

    ResolutionCommandContext context() const {
        ResolutionCommandContext ctx;
        ctx.import_record_id = import_id_;
        ctx.actor_user_id = user_id_;
        ctx.edit_lock = std::nullopt;
        ctx.expected_inventory_revision_id = revision_id_;
        return ctx;
    }

    drogon::orm::DbClientPtr client_;
    std::string user_id_;
    std::string bridge_id_;
    std::string year_id_;
    std::string import_id_;
    std::string source_file_id_;
    std::string revision_id_;
    std::string package_id_;
    std::vector<std::string> component_ids_;
};

}  // namespace

// 设计里的原例：报告写"第32孔桥面"，一条规则把整组带到台账写法上。
TEST_F(ResolutionPlanTest, BulkReplacePreviewsBindsSkipsAndCounts) {
    import_defects({"第1孔桥面", "第2孔桥面", "第9孔桥面", "看不懂的编号"});
    BulkReplaceIntent intent;
    intent.find = "第*孔桥面";
    intent.replace = "*#跨桥面铺装";

    const auto outcome =
        ImportResolutionService(client_).build_bulk_replace_plan(context(), intent);
    ASSERT_EQ(outcome.status, ResolutionStatus::Ok) << outcome.error_message;
    const auto& plan = *outcome.plan;

    EXPECT_EQ(plan.operation_type, "bulk_replace");
    EXPECT_FALSE(plan.plan_token.empty());
    EXPECT_EQ(plan.will_apply_count, 2);
    // 第9孔在台账里没有；"看不懂的编号"不符合查找模式。两者都要在预览里显形。
    EXPECT_EQ(plan.skipped_count, 2);

    int not_found = 0;
    int not_matched = 0;
    for (const auto& row : plan.rows) {
        if (row.reason_code == "component_not_found") ++not_found;
        if (row.reason_code == "pattern_not_matched") ++not_matched;
    }
    EXPECT_EQ(not_found, 1);
    EXPECT_EQ(not_matched, 1);
}

// 已绑定与已标记缺失的组不参与、不受影响：守住"已核对过的结果不被批量操作推翻"。
TEST_F(ResolutionPlanTest, BulkReplaceSkipsGroupsThatAreAlreadyResolved) {
    import_defects({"第1孔桥面", "第2孔桥面"});
    const ImportResolutionService service(client_);

    const auto workspace = service.load_workspace(import_id_);
    ASSERT_EQ(workspace.status, ResolutionStatus::Ok);
    bridge_report::resolution::ComponentResolutionRequest mark;
    mark.context = context();
    mark.group_id = workspace.workspace->groups[0].group_id;
    mark.expected_version = workspace.workspace->groups[0].version;
    mark.action = "mark_missing";
    ASSERT_EQ(service.apply_component_resolution(mark).status, ResolutionStatus::Ok);

    BulkReplaceIntent intent;
    intent.find = "第*孔桥面";
    intent.replace = "*#跨桥面铺装";
    const auto outcome = service.build_bulk_replace_plan(context(), intent);
    ASSERT_EQ(outcome.status, ResolutionStatus::Ok) << outcome.error_message;
    EXPECT_EQ(outcome.plan->rows.size(), 1u) << "已标记缺失的组不该出现在计划里";
    EXPECT_EQ(outcome.plan->will_apply_count, 1);
}

TEST_F(ResolutionPlanTest, ApplyingABulkReplacePlanBindsExactlyThePreviewedRows) {
    import_defects({"第1孔桥面", "第9孔桥面"});
    const ImportResolutionService service(client_);
    BulkReplaceIntent intent;
    intent.find = "第*孔桥面";
    intent.replace = "*#跨桥面铺装";
    const auto plan = service.build_bulk_replace_plan(context(), intent);
    ASSERT_EQ(plan.status, ResolutionStatus::Ok) << plan.error_message;

    const auto applied =
        service.apply_resolution_plan(context(), plan.plan->plan_token);
    ASSERT_EQ(applied.status, ResolutionStatus::Ok) << applied.error_message;
    ASSERT_TRUE(applied.apply_result.has_value());
    EXPECT_EQ((*applied.apply_result)["applied_group_count"].asInt(), 1);
    ASSERT_TRUE(applied.command_result.has_value());
    ASSERT_EQ(applied.command_result->affected_groups.size(), 1u);
    EXPECT_EQ(applied.command_result->affected_groups[0].status, "bound");
    EXPECT_EQ(applied.command_result->progress.bound_count, 1);
    // 跳过的那条必须原样停在未解析。
    EXPECT_EQ(applied.command_result->progress.unresolved_count, 1);
}

// §14：重复提交已成功应用的 token 返回首次结果，不重复写入。
TEST_F(ResolutionPlanTest, ReapplyingASucceededPlanReplaysTheFirstResult) {
    import_defects({"第1孔桥面"});
    const ImportResolutionService service(client_);
    BulkReplaceIntent intent;
    intent.find = "第*孔桥面";
    intent.replace = "*#跨桥面铺装";
    const auto plan = service.build_bulk_replace_plan(context(), intent);
    ASSERT_EQ(plan.status, ResolutionStatus::Ok) << plan.error_message;

    const auto first = service.apply_resolution_plan(context(), plan.plan->plan_token);
    ASSERT_EQ(first.status, ResolutionStatus::Ok) << first.error_message;
    const auto version_after_first = client_->execSqlSync(
        "select version from import_component_resolution_groups "
        "where import_record_id=$1::uuid", import_id_)[0]["version"].as<int>();

    const auto replay = service.apply_resolution_plan(context(), plan.plan->plan_token);
    ASSERT_EQ(replay.status, ResolutionStatus::Ok) << replay.error_message;
    ASSERT_TRUE(replay.apply_result.has_value());
    EXPECT_EQ(*replay.apply_result, *first.apply_result);

    const auto version_after_replay = client_->execSqlSync(
        "select version from import_component_resolution_groups "
        "where import_record_id=$1::uuid", import_id_)[0]["version"].as<int>();
    EXPECT_EQ(version_after_replay, version_after_first)
        << "重放不该再执行一遍状态变更";
}

// 预览后组被改动：整批拒绝，不尝试"尽可能执行"。
TEST_F(ResolutionPlanTest, GroupChangedAfterPreviewInvalidatesThePlan) {
    import_defects({"第1孔桥面"});
    const ImportResolutionService service(client_);
    BulkReplaceIntent intent;
    intent.find = "第*孔桥面";
    intent.replace = "*#跨桥面铺装";
    const auto plan = service.build_bulk_replace_plan(context(), intent);
    ASSERT_EQ(plan.status, ResolutionStatus::Ok) << plan.error_message;

    // 预览之后有人先把这个组标成了缺失。
    const auto workspace = service.load_workspace(import_id_);
    bridge_report::resolution::ComponentResolutionRequest mark;
    mark.context = context();
    mark.group_id = workspace.workspace->groups[0].group_id;
    mark.expected_version = workspace.workspace->groups[0].version;
    mark.action = "mark_missing";
    ASSERT_EQ(service.apply_component_resolution(mark).status, ResolutionStatus::Ok);

    const auto applied = service.apply_resolution_plan(context(), plan.plan->plan_token);
    EXPECT_EQ(applied.status, ResolutionStatus::Conflict);
    EXPECT_EQ(applied.error_code, "resolution_plan_invalidated");
    // 整批不写入：组仍停在人工标记的缺失上。
    EXPECT_EQ(service.load_workspace(import_id_).workspace->groups[0].status, "missing");
}

TEST_F(ResolutionPlanTest, ExpiredPlanIsRejectedAndMarked) {
    import_defects({"第1孔桥面"});
    const ImportResolutionService service(client_);
    BulkReplaceIntent intent;
    intent.find = "第*孔桥面";
    intent.replace = "*#跨桥面铺装";
    const auto plan = service.build_bulk_replace_plan(context(), intent);
    ASSERT_EQ(plan.status, ResolutionStatus::Ok) << plan.error_message;

    client_->execSqlSync(
        "update import_resolution_operation_plans set expires_at = now() - interval '1 minute' "
        "where id=$1::uuid", plan.plan->plan_token);

    const auto applied = service.apply_resolution_plan(context(), plan.plan->plan_token);
    EXPECT_EQ(applied.status, ResolutionStatus::Conflict);
    EXPECT_EQ(applied.error_code, "resolution_plan_expired");
    const auto status = client_->execSqlSync(
        "select status from import_resolution_operation_plans where id=$1::uuid",
        plan.plan->plan_token)[0]["status"].as<std::string>();
    EXPECT_EQ(status, "expired");
}

// 有效期是创建后 15 分钟，不随编辑锁的 2 分钟 TTL 截断——按锁到期时刻算的话，
// 批量替换预览 26 行这种要逐行读的对话框会常态性地过期。
TEST_F(ResolutionPlanTest, PlanLivesFifteenMinutesNotTheTwoMinuteLockTtl) {
    import_defects({"第1孔桥面"});
    BulkReplaceIntent intent;
    intent.find = "第*孔桥面";
    intent.replace = "*#跨桥面铺装";
    const auto plan =
        ImportResolutionService(client_).build_bulk_replace_plan(context(), intent);
    ASSERT_EQ(plan.status, ResolutionStatus::Ok) << plan.error_message;

    const auto minutes = client_->execSqlSync(
        "select round(extract(epoch from (expires_at - created_at)) / 60) as minutes "
        "from import_resolution_operation_plans where id=$1::uuid",
        plan.plan->plan_token)[0]["minutes"].as<double>();
    EXPECT_DOUBLE_EQ(minutes, 15.0);
}

TEST_F(ResolutionPlanTest, RangeExpandPreviewsEveryComponentInTheRange) {
    import_defects({"1#跨桥面铺装~3#跨桥面铺装"});
    const ImportResolutionService service(client_);
    const auto workspace = service.load_workspace(import_id_);
    ASSERT_EQ(workspace.status, ResolutionStatus::Ok);
    ASSERT_EQ(workspace.workspace->groups.size(), 1u);

    RangeExpandIntent intent;
    intent.group_ids = {workspace.workspace->groups[0].group_id};
    const auto plan = service.build_range_expand_plan(context(), intent);
    ASSERT_EQ(plan.status, ResolutionStatus::Ok) << plan.error_message;
    ASSERT_EQ(plan.plan->rows.size(), 1u);
    const auto& row = plan.plan->rows[0];
    EXPECT_EQ(row.outcome, "will_bind");
    EXPECT_EQ(row.target_component_ids.size(), 3u);
    EXPECT_EQ(plan.plan->instances_before, 1);
    EXPECT_EQ(plan.plan->instances_after, 3);

    const auto applied = service.apply_resolution_plan(context(), plan.plan->plan_token);
    ASSERT_EQ(applied.status, ResolutionStatus::Ok) << applied.error_message;
    const auto& group = applied.command_result->affected_groups[0];
    EXPECT_EQ(group.status, "bound");
    EXPECT_EQ(group.resolution_mode, "range");
    ASSERT_EQ(group.members.size(), 1u);
    ASSERT_EQ(group.members[0].instances.size(), 3u);
    // 照片只跟第一条活动实例走，不随区间拆分重复。
    EXPECT_TRUE(group.members[0].instances[0].is_photo_owner);
    EXPECT_FALSE(group.members[0].instances[1].is_photo_owner);
    EXPECT_FALSE(group.members[0].instances[2].is_photo_owner);
}

// 区间要么整条展开、要么不展开：只绑上其中几跨，剩下几跨会静默消失。
TEST_F(ResolutionPlanTest, RangeExpandBlocksWhenOneComponentIsMissing) {
    import_defects({"1#跨桥面铺装~5#跨桥面铺装"});
    const ImportResolutionService service(client_);
    const auto workspace = service.load_workspace(import_id_);
    RangeExpandIntent intent;
    intent.group_ids = {workspace.workspace->groups[0].group_id};

    const auto plan = service.build_range_expand_plan(context(), intent);
    ASSERT_EQ(plan.status, ResolutionStatus::Ok) << plan.error_message;
    ASSERT_EQ(plan.plan->rows.size(), 1u);
    EXPECT_EQ(plan.plan->rows[0].outcome, "blocked");
    EXPECT_EQ(plan.plan->rows[0].reason_code, "component_not_found");
    EXPECT_EQ(plan.plan->blocked_count, 1);
    EXPECT_EQ(plan.plan->will_apply_count, 0);
    EXPECT_TRUE(plan.plan->rows[0].target_component_ids.empty());
}

TEST_F(ResolutionPlanTest, PlanFromAnotherUserIsRejected) {
    import_defects({"第1孔桥面"});
    const ImportResolutionService service(client_);
    BulkReplaceIntent intent;
    intent.find = "第*孔桥面";
    intent.replace = "*#跨桥面铺装";
    const auto plan = service.build_bulk_replace_plan(context(), intent);
    ASSERT_EQ(plan.status, ResolutionStatus::Ok) << plan.error_message;

    auto other = context();
    other.actor_user_id = "00000000-0000-0000-0000-000000000000";
    const auto applied = service.apply_resolution_plan(other, plan.plan->plan_token);
    EXPECT_EQ(applied.status, ResolutionStatus::Conflict);
    EXPECT_EQ(applied.error_code, "resolution_plan_invalidated");
}

TEST_F(ResolutionPlanTest, RejectsAnUncompilablePattern) {
    import_defects({"第1孔桥面"});
    BulkReplaceIntent intent;
    intent.find = "第*孔";
    intent.replace = "*-*";  // 替换里的 * 多于查找，无从取值

    const auto outcome =
        ImportResolutionService(client_).build_bulk_replace_plan(context(), intent);
    EXPECT_EQ(outcome.status, ResolutionStatus::Invalid);
    EXPECT_EQ(outcome.error_code, "invalid_resolution_request");
}

// P1-3 回归：给尚未解析的组执行台账版本重指。
//
// 这是"导入时该桥还没有已确认台账，之后台账确认了再统一重指"这一路（§9.4、验收 16）。
// 预览端给这类组的是 will_repoint + 空目标——语义就是"只改所依据的版本"。执行端却
// 无条件把它写成 status=bound，而 bound 组按约束必须至少有一个目标，于是整批计划在
// 提交阶段炸掉、全部回滚。
TEST_F(ResolutionPlanTest, RepointingAnUnresolvedGroupKeepsItUnresolved) {
    // 台账里没有这个编号，组停在 unresolved。
    import_defects({"看不懂的编号"});

    // 模拟"导入时该桥还没有已确认台账"：那种情况下组不钉任何版本（§8.1 允许为空），
    // 等台账确认后再由重指计划统一钉上。
    client_->execSqlSync(
        "update import_component_resolution_groups set inventory_revision_id=null "
        "where import_record_id=$1::uuid", import_id_);

    const auto before = ImportResolutionService(client_).load_workspace(import_id_);
    ASSERT_EQ(before.status, ResolutionStatus::Ok) << before.error_message;
    ASSERT_EQ(before.workspace->groups.size(), 1u);
    ASSERT_EQ(before.workspace->groups[0].status, "unresolved");

    InventoryRepointIntent intent;
    intent.group_ids = {before.workspace->groups[0].group_id};
    const auto plan = ImportResolutionService(client_)
        .build_inventory_repoint_plan(context(), intent);
    ASSERT_EQ(plan.status, ResolutionStatus::Ok) << plan.error_message;
    ASSERT_EQ(plan.plan->will_apply_count, 1);

    const auto applied = ImportResolutionService(client_)
        .apply_resolution_plan(context(), plan.plan->plan_token);
    ASSERT_EQ(applied.status, ResolutionStatus::Ok) << applied.error_message;

    // 组仍未解析：重指只换所依据的台账版本，不代表有人挑好了构件。
    const auto after = ImportResolutionService(client_).load_workspace(import_id_);
    ASSERT_EQ(after.status, ResolutionStatus::Ok) << after.error_message;
    ASSERT_EQ(after.workspace->groups.size(), 1u);
    EXPECT_EQ(after.workspace->groups[0].status, "unresolved");
    EXPECT_TRUE(after.workspace->groups[0].targets.empty());
    ASSERT_TRUE(after.workspace->groups[0].inventory_revision_id.has_value());
    EXPECT_EQ(*after.workspace->groups[0].inventory_revision_id, revision_id_);
}
