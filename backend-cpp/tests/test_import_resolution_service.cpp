#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/db/WordImportRepository.hpp"
#include "bridge_report/http/ImportResolutionRoutes.hpp"
#include "bridge_report/resolution/ImportResolutionService.hpp"

namespace {

using bridge_report::resolution::ImportResolutionService;
using bridge_report::resolution::ResolutionStatus;

bool contains(const std::vector<std::string>& values, const std::string& needle) {
    return std::find(values.begin(), values.end(), needle) != values.end();
}

class ImportResolutionServiceTest : public testing::Test {
protected:
    void SetUp() override {
        if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) {
            GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL is not set";
        }
        client_ = bridge_report::db::create_db_client(bridge_report::config::PostgresConfig{});
        user_id_ = client_->execSqlSync(
            "select id::text from users where username='admin'")[0]["id"].as<std::string>();
        bridge_id_ = client_->execSqlSync(
            "insert into bridges (bridge_name) values ('解析工作区测试桥') returning id::text"
        )[0]["id"].as<std::string>();
        year_id_ = client_->execSqlSync(
            "insert into inspection_years (bridge_id, inspection_year, status, is_current) "
            "values ($1::uuid, 2026, '待校对', true) returning id::text",
            bridge_id_)[0]["id"].as<std::string>();
        import_id_ = client_->execSqlSync(
            "insert into import_records (bridge_id, inspection_year_id, import_name, "
            "source_type, import_status) values ($1::uuid, $2::uuid, '解析工作区测试', "
            "'接口同步', '解析中') returning id::text",
            bridge_id_, year_id_)[0]["id"].as<std::string>();
        source_file_id_ = client_->execSqlSync(
            "insert into import_source_files (import_record_id, original_file_name,"
            "storage_relative_path, file_extension, file_size_bytes, file_hash, status,"
            "parsing_started_at) values ($1::uuid,'sync.srcref',$1::text||'.srcref',"
            "'.srcref',9,$2,'解析中',now()) returning id::text",
            import_id_, std::string(64, 'a'))[0]["id"].as<std::string>();
    }

    void TearDown() override {
        if (!client_) return;
        client_->execSqlSync("delete from import_source_files where id=$1::uuid", source_file_id_);
        client_->execSqlSync("delete from import_records where id=$1::uuid", import_id_);
        // 导入会把年度锁到解析出的台账版本上，那条外键是 RESTRICT：不先松开锁，
        // 下面删版本就会失败，而错误发生在 TearDown 里，看着像被测代码出了问题。
        client_->execSqlSync(
            "update inspection_years set component_inventory_revision_id=null "
            "where id=$1::uuid", year_id_);
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

    /// 一份已确认台账。每项是 (编号, 现场部件类型)，都挂上已确认的规范映射。
    ///
    /// 现场部件类型要能分别指定：台账的唯一键是 (版本, 现场类型, 编号)，同一编号想
    /// 出现两次就只能靠不同的现场类型——而这正是真实的歧义形态（报告写"1-1#梁"，
    /// 台账里空心板和 T 梁各有一件 1-1#，两者同属一个 H21 类别）。
    void seed_confirmed_inventory(
        const std::vector<std::pair<std::string, std::string>>& entries) {
        package_id_ = client_->execSqlSync(
            "insert into standard_packages(standard_family,standard_id,standard_code,"
            "standard_name,official_edition,package_version,contract_version,algorithm_id,"
            "effective_date,content_checksum) values('technical_condition',"
            "'WS-'||gen_random_uuid()::text,'WS','工作区测试规范','2026','1.0.0',1,'ws',"
            "'2026-01-01','sha256:'||repeat('d',64)) returning id::text"
        )[0]["id"].as<std::string>();
        revision_id_ = client_->execSqlSync(
            "insert into bridge_component_inventory_revisions(bridge_id,revision_number,"
            "created_by_user_id) values($1::uuid,1,$2::uuid) returning id::text",
            bridge_id_, user_id_)[0]["id"].as<std::string>();
        int order = 1;
        for (const auto& [number, site_type] : entries) {
            const auto component_id = client_->execSqlSync(
                "insert into bridge_components(bridge_id,structure_part,component_type,"
                "business_component_code,normalized_component_key) "
                "values($1::uuid,'上部结构','主梁',$2,$3) returning id::text",
                bridge_id_, number, "ws-" + std::to_string(order)
            )[0]["id"].as<std::string>();
            const auto entry_id = client_->execSqlSync(
                "insert into bridge_component_inventory_entries(inventory_revision_id,"
                "bridge_component_id,component_number,site_name,site_component_type,sort_order) "
                "values($1::uuid,$2::uuid,$3,$4,$4,$5) returning id::text",
                revision_id_, component_id, number, site_type, order)[0]["id"].as<std::string>();
            client_->execSqlSync(
                "insert into bridge_component_standard_mappings(inventory_entry_id,"
                "standard_package_id,standard_bridge_type_id,standard_component_category_id,"
                "structure_part,mapping_source,confirmation_status,confirmed_by_user_id,"
                "confirmed_at) values($1::uuid,$2::uuid,'h21.bridge_type.beam',"
                "'h21.component.beam.upper_bearing','superstructure','规范模板','已确认',"
                "$3::uuid,now())",
                entry_id, package_id_, user_id_);
            ++order;
        }
        client_->execSqlSync(
            "update bridge_component_inventory_revisions set status='已确认',"
            "confirmed_by_user_id=$2::uuid,confirmed_at=now() where id=$1::uuid",
            revision_id_, user_id_);
    }

    /// 走真实导入路径落一批病害，解析状态由导入初始化建立。
    void import_defects(const std::vector<std::pair<std::string, std::string>>& defects) {
        bridge_report::archive::ArchivedPhotoBatch batch;
        batch.data["contract"]["parser_name"] = "source-db-importer";
        batch.data["contract"]["parser_version"] = "1.0.0";
        batch.data["photos"] = Json::Value(Json::arrayValue);
        int index = 1;
        for (const auto& [part_name, number] : defects) {
            Json::Value defect(Json::objectValue);
            defect["candidate_id"] = "source_defect_" + std::to_string(index);
            defect["component_name"] = part_name;
            if (number.empty()) {
                defect["component_number"] = Json::Value();
            } else {
                defect["component_number"] = number;
            }
            defect["defect_type"] = "裂缝";
            defect["defect_location"] = "底板";
            defect["defect_description"] = "底板出现纵向裂缝";
            defect["warnings"] = Json::Value(Json::arrayValue);
            batch.data["defects"].append(defect);
            ++index;
        }
        const auto outcome =
            bridge_report::db::WordImportRepository(client_).persist_parse_result(
                import_id_, batch);
        ASSERT_TRUE(outcome.success) << outcome.error_code << ": " << outcome.error_message;
    }

    drogon::orm::DbClientPtr client_;
    std::string user_id_;
    std::string bridge_id_;
    std::string year_id_;
    std::string import_id_;
    std::string source_file_id_;
    std::string revision_id_;
    std::string package_id_;
};

}  // namespace

TEST_F(ImportResolutionServiceTest, BoundGroupCarriesTargetsInstancesAndProgress) {
    seed_confirmed_inventory({{"1-1#梁", "空心板"}});
    import_defects({{"上部承重构件", "1-1#梁"}});

    const auto outcome = ImportResolutionService(client_).load_workspace(import_id_);
    ASSERT_EQ(outcome.status, ResolutionStatus::Ok) << outcome.error_message;
    const auto& workspace = *outcome.workspace;

    EXPECT_TRUE(workspace.inventory_confirmed);
    ASSERT_TRUE(workspace.inventory_revision_id.has_value());
    EXPECT_EQ(*workspace.inventory_revision_id, revision_id_);
    EXPECT_EQ(workspace.draft_version, 1);

    ASSERT_EQ(workspace.groups.size(), 1u);
    const auto& group = workspace.groups[0];
    EXPECT_EQ(group.status, "bound");
    EXPECT_FALSE(group.ambiguous);
    ASSERT_EQ(group.targets.size(), 1u);
    // 目标必须带可读信息，否则前端还得自己再查一次台账。
    EXPECT_EQ(group.targets[0].component_number, "1-1#梁");
    EXPECT_EQ(group.targets[0].site_component_type, "空心板");

    ASSERT_EQ(group.members.size(), 1u);
    ASSERT_EQ(group.members[0].instances.size(), 1u);
    const auto& instance = group.members[0].instances[0];
    EXPECT_EQ(instance.instance_status, "active");
    EXPECT_TRUE(instance.is_photo_owner);
    EXPECT_EQ(instance.bridge_component_id, group.targets[0].bridge_component_id);
    // 没有覆盖时有效事实就是来源事实。
    EXPECT_TRUE(instance.overridden_fields.empty());
    EXPECT_EQ(instance.effective_facts["defect_type"].asString(), "裂缝");

    EXPECT_EQ(workspace.progress.group_count, 1);
    EXPECT_EQ(workspace.progress.bound_count, 1);
    EXPECT_EQ(workspace.progress.active_instance_count, 1);
}

// 部件层级与歧义都是读模型派生物（§4.7）：关系表只建行级组，
// 但批量替换入口与分组表头挂在部件上，计数必须由后端给。
TEST_F(ImportResolutionServiceTest, PartLevelAggregationIsDerivedByTheBackend) {
    seed_confirmed_inventory({{"1-1#梁", "空心板"}});
    import_defects({
        {"上部承重构件", "1-1#梁"},   // 命中
        {"上部承重构件", "9-9#梁"},   // 未命中
        {"桥面铺装", ""},             // 另一个部件，无编号
    });

    const auto outcome = ImportResolutionService(client_).load_workspace(import_id_);
    ASSERT_EQ(outcome.status, ResolutionStatus::Ok) << outcome.error_message;
    const auto& workspace = *outcome.workspace;

    ASSERT_EQ(workspace.parts.size(), 2u);
    const auto beam = std::find_if(
        workspace.parts.begin(), workspace.parts.end(),
        [](const auto& part) { return part.source_component_name == "上部承重构件"; });
    ASSERT_NE(beam, workspace.parts.end());
    EXPECT_EQ(beam->total, 2);
    EXPECT_EQ(beam->bound, 1);
    EXPECT_EQ(beam->unresolved, 1);
    EXPECT_EQ(beam->group_ids.size(), 2u);

    EXPECT_EQ(workspace.progress.group_count, 3);
    EXPECT_EQ(workspace.progress.bound_count, 1);
    EXPECT_EQ(workspace.progress.unresolved_count, 2);
}

// 同一部件类别下两件构件的归一化编号相同 → 未解析组带两个候选，标签为歧义。
// 候选不持久化（§10 末段），所以这条同时验证读模型确实在现算。
TEST_F(ImportResolutionServiceTest, AmbiguousLabelIsRecomputedFromCandidates) {
    seed_confirmed_inventory({{"1-1#梁", "空心板"}, {"1-1#梁", "T梁"}});
    import_defects({{"上部承重构件", "1-1#梁"}});

    const auto outcome = ImportResolutionService(client_).load_workspace(import_id_);
    ASSERT_EQ(outcome.status, ResolutionStatus::Ok) << outcome.error_message;
    const auto& group = outcome.workspace->groups[0];

    EXPECT_EQ(group.status, "unresolved");
    EXPECT_TRUE(group.ambiguous);
    EXPECT_EQ(group.candidates.size(), 2u);
    for (const auto& candidate : group.candidates) {
        EXPECT_EQ(candidate.component_number, "1-1#梁");
    }
    EXPECT_EQ(outcome.workspace->progress.ambiguous_count, 1);
}

// 台账未确认不是错误：工作区照常返回，只是绑定类动作全部不可用。
TEST_F(ImportResolutionServiceTest, UnconfirmedInventoryGreysOutBindingActions) {
    import_defects({{"上部承重构件", "1-1#梁"}});

    const auto outcome = ImportResolutionService(client_).load_workspace(import_id_);
    ASSERT_EQ(outcome.status, ResolutionStatus::Ok) << outcome.error_message;
    const auto& workspace = *outcome.workspace;

    EXPECT_FALSE(workspace.inventory_confirmed);
    EXPECT_FALSE(workspace.inventory_revision_id.has_value());
    ASSERT_EQ(workspace.groups.size(), 1u);
    const auto& group = workspace.groups[0];
    EXPECT_EQ(group.status, "unresolved");
    EXPECT_TRUE(group.allowed_actions.empty());
    EXPECT_TRUE(contains(group.blocked_reasons, "component_inventory_not_confirmed"));
    // 候选现算依赖台账，没有台账就不该编出候选来。
    EXPECT_TRUE(group.candidates.empty());
    EXPECT_FALSE(group.ambiguous);
}

TEST_F(ImportResolutionServiceTest, AllowedActionsFollowTheGroupStatus) {
    seed_confirmed_inventory({{"1-1#梁", "空心板"}});
    import_defects({{"上部承重构件", "1-1#梁"}, {"上部承重构件", "9-9#梁"}});

    const auto outcome = ImportResolutionService(client_).load_workspace(import_id_);
    ASSERT_EQ(outcome.status, ResolutionStatus::Ok) << outcome.error_message;

    for (const auto& group : outcome.workspace->groups) {
        if (group.status == "bound") {
            EXPECT_TRUE(contains(group.allowed_actions, "rebind"));
            EXPECT_TRUE(contains(group.allowed_actions, "clear"));
            // 已绑定的行不参与批量替换：守住"已核对过的结果不被批量操作推翻"。
            EXPECT_FALSE(contains(group.allowed_actions, "bulk_replace"));
        } else {
            EXPECT_TRUE(contains(group.allowed_actions, "bind"));
            EXPECT_TRUE(contains(group.allowed_actions, "bulk_replace"));
        }
    }
}

TEST_F(ImportResolutionServiceTest, MissingImportRecordIsNotFound) {
    const auto outcome = ImportResolutionService(client_).load_workspace(
        "00000000-0000-0000-0000-000000000000");
    EXPECT_EQ(outcome.status, ResolutionStatus::NotFound);
    EXPECT_EQ(bridge_report::http::resolution_error_response(outcome).http_status, 404);
}

// 序列化契约：前端拿到的字段名必须明确区分来源病害与解析实例，
// 不能出现含糊的 defect_id（§22.2）。
TEST_F(ImportResolutionServiceTest, WorkspaceJsonNamesIdentitiesExplicitly) {
    seed_confirmed_inventory({{"1-1#梁", "空心板"}});
    import_defects({{"上部承重构件", "1-1#梁"}});

    const auto outcome = ImportResolutionService(client_).load_workspace(import_id_);
    ASSERT_EQ(outcome.status, ResolutionStatus::Ok) << outcome.error_message;
    const auto json = bridge_report::http::resolution_workspace_json(*outcome.workspace);

    EXPECT_TRUE(json.isMember("draft_version"));
    EXPECT_TRUE(json.isMember("parts"));
    EXPECT_TRUE(json.isMember("progress"));
    ASSERT_TRUE(json["groups"].isArray());
    ASSERT_EQ(json["groups"].size(), 1u);
    const auto& group_json = json["groups"][0];
    EXPECT_TRUE(group_json.isMember("allowed_actions"));
    EXPECT_TRUE(group_json.isMember("ambiguous"));
    const auto& member_json = group_json["members"][0];
    EXPECT_TRUE(member_json.isMember("source_candidate_id"));
    EXPECT_FALSE(member_json.isMember("defect_id"));
    const auto& instance_json = member_json["instances"][0];
    EXPECT_TRUE(instance_json.isMember("resolved_defect_instance_id"));
    EXPECT_FALSE(instance_json.isMember("defect_id"));
    EXPECT_TRUE(instance_json.isMember("overridden_fields"));
    EXPECT_TRUE(instance_json["rating_resolution"].isMember("present"));
}
