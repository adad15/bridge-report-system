#include <algorithm>
#include <memory>
#include <cstdlib>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/db/ImportResolutionRepository.hpp"
#include "bridge_report/db/CommitLatch.hpp"
#include "bridge_report/db/ComponentInventoryRepository.hpp"
#include "bridge_report/resolution/DraftResolutionSynchronizer.hpp"
#include "bridge_report/db/WordImportRepository.hpp"
#include "bridge_report/resolution/ImportResolutionService.hpp"

namespace {

using bridge_report::db::ImportResolutionRepository;
using bridge_report::resolution::ComponentResolutionRequest;
using bridge_report::resolution::FactOverrideRequest;
using bridge_report::resolution::ImportResolutionService;
using bridge_report::resolution::InstanceStatusRequest;
using bridge_report::resolution::RatingResolutionRequest;
using bridge_report::resolution::ResolutionStatus;
using bridge_report::resolution::ResolutionTargetSelection;

class ResolutionCommandTest : public testing::Test {
protected:
    void SetUp() override {
        if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) {
            GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL is not set";
        }
        client_ = bridge_report::db::create_db_client(bridge_report::config::PostgresConfig{});
        user_id_ = client_->execSqlSync(
            "select id::text from users where username='admin'")[0]["id"].as<std::string>();
        bridge_id_ = client_->execSqlSync(
            "insert into bridges (bridge_name) values ('解析命令测试桥') returning id::text"
        )[0]["id"].as<std::string>();
        year_id_ = client_->execSqlSync(
            "insert into inspection_years (bridge_id, inspection_year, status, is_current) "
            "values ($1::uuid, 2026, '待校对', true) returning id::text",
            bridge_id_)[0]["id"].as<std::string>();
        import_id_ = client_->execSqlSync(
            "insert into import_records (bridge_id, inspection_year_id, import_name, "
            "source_type, import_status) values ($1::uuid,$2::uuid,'解析命令测试',"
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

    void seed_inventory() {
        package_id_ = client_->execSqlSync(
            "insert into standard_packages(standard_family,standard_id,standard_code,"
            "standard_name,official_edition,package_version,contract_version,algorithm_id,"
            "effective_date,content_checksum) values('technical_condition',"
            "'RC-'||gen_random_uuid()::text,'RC','解析命令测试规范','2026','1.0.0',1,'rc',"
            "'2026-01-01','sha256:'||repeat('e',64)) returning id::text"
        )[0]["id"].as<std::string>();
        revision_id_ = client_->execSqlSync(
            "insert into bridge_component_inventory_revisions(bridge_id,revision_number,"
            "created_by_user_id) values($1::uuid,1,$2::uuid) returning id::text",
            bridge_id_, user_id_)[0]["id"].as<std::string>();
        for (int index = 1; index <= 3; ++index) {
            const auto number = std::to_string(index) + "-1#梁";
            const auto component_id = client_->execSqlSync(
                "insert into bridge_components(bridge_id,structure_part,component_type,"
                "business_component_code,normalized_component_key) "
                "values($1::uuid,'上部结构','主梁',$2,$3) returning id::text",
                bridge_id_, number, "rc-" + std::to_string(index)
            )[0]["id"].as<std::string>();
            component_ids_.push_back(component_id);
            const auto entry_id = client_->execSqlSync(
                "insert into bridge_component_inventory_entries(inventory_revision_id,"
                "bridge_component_id,component_number,site_name,site_component_type,sort_order) "
                "values($1::uuid,$2::uuid,$3,'空心板','空心板',$4) returning id::text",
                revision_id_, component_id, number, index)[0]["id"].as<std::string>();
            client_->execSqlSync(
                "insert into bridge_component_standard_mappings(inventory_entry_id,"
                "standard_package_id,standard_bridge_type_id,standard_component_category_id,"
                "structure_part,mapping_source,confirmation_status,confirmed_by_user_id,"
                "confirmed_at) values($1::uuid,$2::uuid,'h21.bridge_type.beam',"
                "'h21.component.beam.upper_bearing','superstructure','规范模板','已确认',"
                "$3::uuid,now())",
                entry_id, package_id_, user_id_);
        }
        client_->execSqlSync(
            "update bridge_component_inventory_revisions set status='已确认',"
            "confirmed_by_user_id=$2::uuid,confirmed_at=now() where id=$1::uuid",
            revision_id_, user_id_);
    }

    /// 落一条报告编号对不上台账的病害：组停在 unresolved，供命令自行绑定。
    void import_one_unresolved_defect() {
        bridge_report::archive::ArchivedPhotoBatch batch;
        batch.data["contract"]["parser_name"] = "source-db-importer";
        batch.data["contract"]["parser_version"] = "1.0.0";
        batch.data["photos"] = Json::Value(Json::arrayValue);
        Json::Value defect(Json::objectValue);
        defect["candidate_id"] = "source_defect_0001";
        defect["component_name"] = "上部承重构件";
        defect["component_number"] = "1~3#梁";
        defect["defect_type"] = "裂缝";
        defect["defect_location"] = "底板";
        defect["defect_description"] = "底板出现纵向裂缝";
        defect["warnings"] = Json::Value(Json::arrayValue);
        batch.data["defects"].append(defect);
        const auto outcome =
            bridge_report::db::WordImportRepository(client_).persist_parse_result(
                import_id_, batch);
        ASSERT_TRUE(outcome.success) << outcome.error_code << ": " << outcome.error_message;
    }

    bridge_report::resolution::ResolutionCommandContext context() const {
        bridge_report::resolution::ResolutionCommandContext ctx;
        ctx.import_record_id = import_id_;
        ctx.actor_user_id = user_id_;
        // 夹具不持锁：仓储对 nullopt 的约定是"调用方不校验锁"。
        ctx.edit_lock = std::nullopt;
        ctx.expected_inventory_revision_id = revision_id_;
        return ctx;
    }

    bridge_report::resolution::WorkspaceComponentGroup only_group() {
        const auto outcome = ImportResolutionService(client_).load_workspace(import_id_);
        EXPECT_EQ(outcome.status, ResolutionStatus::Ok) << outcome.error_message;
        EXPECT_EQ(outcome.workspace->groups.size(), 1u);
        return outcome.workspace->groups[0];
    }

    ComponentResolutionRequest bind_request(
        const bridge_report::resolution::WorkspaceComponentGroup& group,
        const std::vector<std::string>& component_ids) {
        ComponentResolutionRequest request;
        request.context = context();
        request.group_id = group.group_id;
        request.expected_version = group.version;
        request.action = "bind";
        for (const auto& id : component_ids) {
            ResolutionTargetSelection selection;
            selection.bridge_component_id = id;
            selection.target_role = "range_member";
            request.targets.push_back(selection);
        }
        return request;
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

TEST_F(ResolutionCommandTest, BindingCreatesTargetsInstancesAndBumpsVersion) {
    import_one_unresolved_defect();
    const auto before = only_group();
    ASSERT_EQ(before.status, "unresolved");

    const auto outcome = ImportResolutionService(client_).apply_component_resolution(
        bind_request(before, {component_ids_[0]}));
    ASSERT_EQ(outcome.status, ResolutionStatus::Ok) << outcome.error_message;
    ASSERT_TRUE(outcome.command_result.has_value());
    // 写操作只回受影响对象和最新统计，不让前端整页重载。
    ASSERT_EQ(outcome.command_result->affected_groups.size(), 1u);

    const auto after = outcome.command_result->affected_groups[0];
    EXPECT_EQ(after.status, "bound");
    EXPECT_EQ(after.version, before.version + 1);
    ASSERT_EQ(after.targets.size(), 1u);
    EXPECT_EQ(after.targets[0].bridge_component_id, component_ids_[0]);
    ASSERT_EQ(after.members.size(), 1u);
    ASSERT_EQ(after.members[0].instances.size(), 1u);
    EXPECT_TRUE(after.members[0].instances[0].is_photo_owner);
    EXPECT_EQ(outcome.command_result->progress.bound_count, 1);
}

TEST_F(ResolutionCommandTest, StaleGroupVersionIsRejectedWithoutWriting) {
    import_one_unresolved_defect();
    const auto before = only_group();

    auto request = bind_request(before, {component_ids_[0]});
    request.expected_version = before.version + 5;
    const auto outcome =
        ImportResolutionService(client_).apply_component_resolution(request);

    EXPECT_EQ(outcome.status, ResolutionStatus::VersionConflict);
    EXPECT_EQ(outcome.error_code, "resolution_version_conflict");
    EXPECT_EQ(only_group().status, "unresolved");
}

// 候选来自后端不等于可以信任客户端目标：别的桥的构件必须被挡下来。
TEST_F(ResolutionCommandTest, TargetFromAnotherBridgeIsRejected) {
    import_one_unresolved_defect();
    const auto other_bridge = client_->execSqlSync(
        "insert into bridges (bridge_name) values ('解析命令测试邻桥') returning id::text"
    )[0]["id"].as<std::string>();
    const auto foreign_component = client_->execSqlSync(
        "insert into bridge_components(bridge_id,structure_part,component_type,"
        "business_component_code,normalized_component_key) "
        "values($1::uuid,'上部结构','主梁','1-1#梁','rc-foreign') returning id::text",
        other_bridge)[0]["id"].as<std::string>();

    const auto outcome = ImportResolutionService(client_).apply_component_resolution(
        bind_request(only_group(), {foreign_component}));

    EXPECT_EQ(outcome.status, ResolutionStatus::Conflict);
    EXPECT_EQ(outcome.error_code, "target_not_allowed");
    EXPECT_EQ(only_group().status, "unresolved");

    client_->execSqlSync("delete from bridge_components where id=$1::uuid", foreign_component);
    client_->execSqlSync("delete from bridges where id=$1::uuid", other_bridge);
}

TEST_F(ResolutionCommandTest, DuplicateTargetsAreRejected) {
    import_one_unresolved_defect();
    const auto outcome = ImportResolutionService(client_).apply_component_resolution(
        bind_request(only_group(), {component_ids_[0], component_ids_[0]}));

    EXPECT_EQ(outcome.status, ResolutionStatus::Invalid);
    EXPECT_EQ(only_group().status, "unresolved");
}

TEST_F(ResolutionCommandTest, MarkMissingAndClearMoveThroughTheStates) {
    import_one_unresolved_defect();
    const ImportResolutionService service(client_);

    auto request = bind_request(only_group(), {});
    request.action = "mark_missing";
    auto outcome = service.apply_component_resolution(request);
    ASSERT_EQ(outcome.status, ResolutionStatus::Ok) << outcome.error_message;
    EXPECT_EQ(outcome.command_result->affected_groups[0].status, "missing");
    EXPECT_TRUE(outcome.command_result->affected_groups[0].targets.empty());

    request = bind_request(only_group(), {});
    request.action = "clear";
    outcome = service.apply_component_resolution(request);
    ASSERT_EQ(outcome.status, ResolutionStatus::Ok) << outcome.error_message;
    EXPECT_EQ(outcome.command_result->affected_groups[0].status, "unresolved");
}

// §9.1 第 3 步的核心：三目标里换掉一个，另外两条实例上的覆盖与忽略状态必须活下来。
// 整组重建的话，用户只想换第三个目标，却会连带丢掉前两条逐条调过的东西。
TEST_F(ResolutionCommandTest, ReplacingOneTargetKeepsTheOtherInstancesIntact) {
    import_one_unresolved_defect();
    const ImportResolutionService service(client_);

    auto outcome = service.apply_component_resolution(bind_request(
        only_group(), {component_ids_[0], component_ids_[1], component_ids_[2]}));
    ASSERT_EQ(outcome.status, ResolutionStatus::Ok) << outcome.error_message;
    auto group = outcome.command_result->affected_groups[0];
    ASSERT_EQ(group.members[0].instances.size(), 3u);

    // 第一条写覆盖，第二条忽略。
    const auto first = group.members[0].instances[0];
    FactOverrideRequest override_request;
    override_request.context = context();
    override_request.instance_id = first.instance_id;
    override_request.expected_version = first.version;
    override_request.overrides["defect_scale"] = 4;
    ASSERT_EQ(service.apply_fact_overrides(override_request).status, ResolutionStatus::Ok);

    const auto second = group.members[0].instances[1];
    InstanceStatusRequest status_request;
    status_request.context = context();
    status_request.instance_id = second.instance_id;
    status_request.expected_version = second.version;
    status_request.instance_status = "ignored";
    ASSERT_EQ(service.apply_instance_status(status_request).status, ResolutionStatus::Ok);

    // 只把第三个目标换掉。
    group = only_group();
    outcome = service.apply_component_resolution(bind_request(
        group, {component_ids_[0], component_ids_[1]}));
    ASSERT_EQ(outcome.status, ResolutionStatus::Ok) << outcome.error_message;

    const auto after = outcome.command_result->affected_groups[0];
    ASSERT_EQ(after.members[0].instances.size(), 2u);
    const auto& kept_first = after.members[0].instances[0];
    const auto& kept_second = after.members[0].instances[1];
    EXPECT_EQ(kept_first.bridge_component_id, component_ids_[0]);
    EXPECT_EQ(kept_first.effective_facts["defect_scale"].asInt(), 4)
        << "换目标把别的实例的覆盖清掉了";
    EXPECT_TRUE(std::find(kept_first.overridden_fields.begin(),
                          kept_first.overridden_fields.end(),
                          "defect_scale") != kept_first.overridden_fields.end());
    EXPECT_EQ(kept_second.instance_status, "ignored")
        << "换目标把别的实例的忽略状态清掉了";
    // 第二条被忽略，照片归属留在第一条。
    EXPECT_TRUE(kept_first.is_photo_owner);
    EXPECT_FALSE(kept_second.is_photo_owner);
}

TEST_F(ResolutionCommandTest, ClearingAnOverrideRestoresTheSourceValue) {
    import_one_unresolved_defect();
    const ImportResolutionService service(client_);
    auto outcome =
        service.apply_component_resolution(bind_request(only_group(), {component_ids_[0]}));
    ASSERT_EQ(outcome.status, ResolutionStatus::Ok) << outcome.error_message;

    auto instance = outcome.command_result->affected_groups[0].members[0].instances[0];
    FactOverrideRequest request;
    request.context = context();
    request.instance_id = instance.instance_id;
    request.expected_version = instance.version;
    request.overrides["defect_location"] = "腹板";
    outcome = service.apply_fact_overrides(request);
    ASSERT_EQ(outcome.status, ResolutionStatus::Ok) << outcome.error_message;
    instance = outcome.command_result->affected_groups[0].members[0].instances[0];
    EXPECT_EQ(instance.effective_facts["defect_location"].asString(), "腹板");

    // 清除是删键，不是写 null。
    FactOverrideRequest clear;
    clear.context = context();
    clear.instance_id = instance.instance_id;
    clear.expected_version = instance.version;
    clear.cleared_fields = {"defect_location"};
    outcome = service.apply_fact_overrides(clear);
    ASSERT_EQ(outcome.status, ResolutionStatus::Ok) << outcome.error_message;
    instance = outcome.command_result->affected_groups[0].members[0].instances[0];
    EXPECT_EQ(instance.effective_facts["defect_location"].asString(), "底板");
    EXPECT_TRUE(instance.overridden_fields.empty());
}

TEST_F(ResolutionCommandTest, RequiredFactCannotBeOverriddenToNull) {
    import_one_unresolved_defect();
    const ImportResolutionService service(client_);
    const auto outcome =
        service.apply_component_resolution(bind_request(only_group(), {component_ids_[0]}));
    ASSERT_EQ(outcome.status, ResolutionStatus::Ok) << outcome.error_message;
    const auto instance =
        outcome.command_result->affected_groups[0].members[0].instances[0];

    FactOverrideRequest request;
    request.context = context();
    request.instance_id = instance.instance_id;
    request.expected_version = instance.version;
    request.overrides["defect_type"] = Json::Value();

    const auto rejected = service.apply_fact_overrides(request);
    EXPECT_EQ(rejected.status, ResolutionStatus::Invalid);
    EXPECT_EQ(rejected.error_code, "invalid_fact_override");
}

TEST_F(ResolutionCommandTest, StaleInstanceVersionIsRejected) {
    import_one_unresolved_defect();
    const ImportResolutionService service(client_);
    const auto outcome =
        service.apply_component_resolution(bind_request(only_group(), {component_ids_[0]}));
    ASSERT_EQ(outcome.status, ResolutionStatus::Ok) << outcome.error_message;
    const auto instance =
        outcome.command_result->affected_groups[0].members[0].instances[0];

    InstanceStatusRequest request;
    request.context = context();
    request.instance_id = instance.instance_id;
    request.expected_version = instance.version + 3;
    request.instance_status = "ignored";

    const auto rejected = service.apply_instance_status(request);
    EXPECT_EQ(rejected.status, ResolutionStatus::VersionConflict);
}

TEST_F(ResolutionCommandTest, BindingWithoutTargetsIsRejected) {
    import_one_unresolved_defect();
    const auto outcome = ImportResolutionService(client_).apply_component_resolution(
        bind_request(only_group(), {}));
    EXPECT_EQ(outcome.status, ResolutionStatus::Invalid);
}

TEST_F(ResolutionCommandTest, StaleInventoryRevisionIsRejected) {
    import_one_unresolved_defect();
    auto request = bind_request(only_group(), {component_ids_[0]});
    request.context.expected_inventory_revision_id =
        "00000000-0000-0000-0000-000000000000";

    const auto outcome =
        ImportResolutionService(client_).apply_component_resolution(request);
    EXPECT_EQ(outcome.status, ResolutionStatus::Conflict);
    EXPECT_EQ(outcome.error_code, "component_inventory_revision_changed");
}

// 审计表只表达历史，当前状态表只表达当前结果，两者不混用。
TEST_F(ResolutionCommandTest, EveryCommandAppendsAnAuditEvent) {
    import_one_unresolved_defect();
    const ImportResolutionService service(client_);
    ASSERT_EQ(service.apply_component_resolution(
                  bind_request(only_group(), {component_ids_[0]})).status,
              ResolutionStatus::Ok);

    const auto events = client_->execSqlSync(
        "select operation_type from import_resolution_events "
        "where import_record_id=$1::uuid order by occurred_at", import_id_);
    ASSERT_FALSE(events.empty());
    EXPECT_EQ(events[events.size() - 1]["operation_type"].as<std::string>(), "bind");
}

// 验收标准 2：绑定、标记缺失、清除、实例覆盖与忽略一律不碰 parsed_result_json。
//
// 这条是整次改造的分界线。少了它，"解析状态搬出 JSON"随时可能被一次顺手的回写悄悄
// 退回去——回写本身不报错，只是让两处各存一份，等到不一致时已经查不出是谁写的。
TEST_F(ResolutionCommandTest, ResolutionWritesNeverTouchTheSourceDraft) {
    import_one_unresolved_defect();
    const auto source_before = client_->execSqlSync(
        "select parsed_result_json::text as json from import_records where id=$1::uuid",
        import_id_)[0]["json"].as<std::string>();

    ImportResolutionService service(client_);
    auto group = only_group();
    ASSERT_EQ(service.apply_component_resolution(bind_request(group, {component_ids_[0]})).status,
              ResolutionStatus::Ok);

    // 实例级覆盖与忽略同样只动关系表。
    const auto workspace = service.load_workspace(import_id_);
    ASSERT_EQ(workspace.status, ResolutionStatus::Ok);
    const auto& instance = workspace.workspace->groups[0].members[0].instances[0];
    FactOverrideRequest override_request;
    override_request.context = context();
    override_request.instance_id = instance.instance_id;
    override_request.expected_version = instance.version;
    override_request.overrides["defect_description"] = "改成实例自己的描述";
    ASSERT_EQ(service.apply_fact_overrides(override_request).status, ResolutionStatus::Ok);

    group = only_group();
    ComponentResolutionRequest clear_request;
    clear_request.context = context();
    clear_request.group_id = group.group_id;
    clear_request.expected_version = group.version;
    clear_request.action = "clear";
    ASSERT_EQ(service.apply_component_resolution(clear_request).status, ResolutionStatus::Ok);

    group = only_group();
    ComponentResolutionRequest missing_request;
    missing_request.context = context();
    missing_request.group_id = group.group_id;
    missing_request.expected_version = group.version;
    missing_request.action = "mark_missing";
    ASSERT_EQ(service.apply_component_resolution(missing_request).status, ResolutionStatus::Ok);

    const auto source_after = client_->execSqlSync(
        "select parsed_result_json::text as json from import_records where id=$1::uuid",
        import_id_)[0]["json"].as<std::string>();
    EXPECT_EQ(source_after, source_before);
    // draft_version 也不该被解析写操作推进：它是来源草稿的并发边界，不是解析的。
    EXPECT_EQ(
        client_->execSqlSync(
            "select draft_version from import_records where id=$1::uuid",
            import_id_)[0]["draft_version"].as<int>(),
        1);
}

// 验收标准 12：普通草稿保存不能覆盖、清除或伪造构件解析状态。
//
// 草稿里已经没有解析字段了（5.0），所以"伪造"这条由契约边界挡；这里守的是另外两条：
// 改病害文字不会顺手把绑定冲掉，改来源构件身份则必须被拒——允许它就等于让一次普通
// 保存把病害搬到另一个组，而两边的实例与评分树各自失效，代价与使用频率不成比例。
TEST_F(ResolutionCommandTest, DraftSaveKeepsTheBindingAndRefusesToMoveTheSourceIdentity) {
    import_one_unresolved_defect();
    const auto group = only_group();
    ASSERT_EQ(ImportResolutionService(client_)
                  .apply_component_resolution(bind_request(group, {component_ids_[0]}))
                  .status,
              ResolutionStatus::Ok);
    ASSERT_EQ(only_group().status, "bound");

    const auto stored_text = client_->execSqlSync(
        "select parsed_result_json::text as json from import_records where id=$1::uuid",
        import_id_)[0]["json"].as<std::string>();
    Json::Value stored;
    Json::CharReaderBuilder reader;
    std::string errors;
    const std::unique_ptr<Json::CharReader> parser(reader.newCharReader());
    ASSERT_TRUE(parser->parse(stored_text.data(), stored_text.data() + stored_text.size(),
                              &stored, &errors))
        << errors;

    // 只改描述：绑定必须原样留着。
    Json::Value edited = stored;
    edited["defects"][0]["defect_description"] = "底板出现纵向裂缝（校对时改写）";
    {
        std::shared_ptr<drogon::orm::Transaction> tx;
        auto latch = std::make_shared<bridge_report::db::CommitLatch>();
        tx = client_->newTransaction(latch->callback());
        const auto sync = bridge_report::resolution::synchronize_draft_resolution(
            tx, import_id_, bridge_id_, year_id_, stored, edited,
            bridge_report::db::ComponentInventoryRepository(tx).resolve_confirmed_revision(
                bridge_id_, revision_id_));
        EXPECT_TRUE(sync.ok) << sync.error_code << ": " << sync.error_message;
        EXPECT_EQ(sync.members_removed, 0);
        EXPECT_EQ(sync.members_added, 0);
        tx.reset();
        ASSERT_TRUE(latch->wait());
    }
    EXPECT_EQ(only_group().status, "bound");
    EXPECT_EQ(only_group().targets.size(), 1u);

    // 改来源构件编号：拒绝，且一个字都不写。
    Json::Value moved = stored;
    moved["defects"][0]["component_number"] = "9-9#梁";
    {
        std::shared_ptr<drogon::orm::Transaction> tx;
        auto latch = std::make_shared<bridge_report::db::CommitLatch>();
        tx = client_->newTransaction(latch->callback());
        const auto sync = bridge_report::resolution::synchronize_draft_resolution(
            tx, import_id_, bridge_id_, year_id_, stored, moved,
            bridge_report::db::ComponentInventoryRepository(tx).resolve_confirmed_revision(
                bridge_id_, revision_id_));
        EXPECT_FALSE(sync.ok);
        EXPECT_EQ(sync.error_code, "source_component_identity_immutable");
        tx->rollback();
        tx.reset();
    }
    EXPECT_EQ(only_group().status, "bound");

    // 删掉候选：成员随之消失，空组一并清掉——绑定不能变成没人认领的孤儿。
    Json::Value removed = stored;
    removed["defects"] = Json::Value(Json::arrayValue);
    {
        std::shared_ptr<drogon::orm::Transaction> tx;
        auto latch = std::make_shared<bridge_report::db::CommitLatch>();
        tx = client_->newTransaction(latch->callback());
        const auto sync = bridge_report::resolution::synchronize_draft_resolution(
            tx, import_id_, bridge_id_, year_id_, stored, removed,
            bridge_report::db::ComponentInventoryRepository(tx).resolve_confirmed_revision(
                bridge_id_, revision_id_));
        EXPECT_TRUE(sync.ok) << sync.error_code << ": " << sync.error_message;
        EXPECT_EQ(sync.members_removed, 1);
        EXPECT_EQ(sync.groups_removed, 1);
        tx.reset();
        ASSERT_TRUE(latch->wait());
    }
    EXPECT_EQ(
        client_->execSqlSync(
            "select count(*)::int as n from import_component_resolution_groups "
            "where import_record_id=$1::uuid", import_id_)[0]["n"].as<int>(),
        0);
}
