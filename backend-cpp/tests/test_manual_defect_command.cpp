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
using bridge_report::resolution::ManualDefectRequest;
using bridge_report::resolution::ResolutionStatus;

class ManualDefectCommandTest : public testing::Test {
protected:
    void SetUp() override {
        if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) {
            GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL is not set";
        }
        client_ = bridge_report::db::create_db_client(bridge_report::config::PostgresConfig{});

        // 复用库里已发布的评定树：手工新增必须选一个**真的适用**于目标构件的节点，
        // 自己拼一棵树反而容易和真实适用性规则脱节。
        const auto tree = client_->execSqlSync(
            "select v.id::text as version_id, "
            "  v.technical_condition_package_id::text as technical_package_id, "
            "  v.maintenance_package_id::text as maintenance_package_id "
            "from rating_tree_versions v where v.status='published' limit 1");
        if (tree.empty()) {
            GTEST_SKIP() << "测试库里没有已发布的评定树";
        }
        tree_version_id_ = tree[0]["version_id"].as<std::string>();
        technical_package_id_ = tree[0]["technical_package_id"].as<std::string>();
        const auto maintenance_package_id =
            tree[0]["maintenance_package_id"].as<std::string>();

        const auto node = client_->execSqlSync(
            "select n.id::text as node_id, n.bridge_type_ids[1] as bridge_type_id, "
            "  n.component_category_ids[1] as component_category_id "
            "from rating_tree_nodes n "
            "where n.rating_tree_version_id=$1::uuid and n.is_selectable "
            "  and n.node_type='defect' and array_length(n.bridge_type_ids,1) >= 1 "
            "  and array_length(n.component_category_ids,1) >= 1 limit 1",
            tree_version_id_);
        if (node.empty()) {
            GTEST_SKIP() << "评定树里没有可选择的病害节点";
        }
        node_id_ = node[0]["node_id"].as<std::string>();
        bridge_type_id_ = node[0]["bridge_type_id"].as<std::string>();
        category_id_ = node[0]["component_category_id"].as<std::string>();
        // 另找一个类别不同的节点，用来验证"节点不适用于该构件"必须被挡下来。
        const auto foreign = client_->execSqlSync(
            "select n.id::text as node_id from rating_tree_nodes n "
            "where n.rating_tree_version_id=$1::uuid and n.is_selectable "
            "  and n.node_type='defect' and not ($2 = any(n.component_category_ids)) limit 1",
            tree_version_id_, category_id_);
        if (!foreign.empty()) {
            inapplicable_node_id_ = foreign[0]["node_id"].as<std::string>();
        }

        user_id_ = client_->execSqlSync(
            "select id::text from users where username='admin'")[0]["id"].as<std::string>();
        bridge_id_ = client_->execSqlSync(
            "insert into bridges (bridge_name) values ('手工新增测试桥') returning id::text"
        )[0]["id"].as<std::string>();
        profile_id_ = client_->execSqlSync(
            "insert into project_standard_profiles(technical_condition_package_id,"
            "maintenance_package_id,rating_tree_version_id,created_by_user_id,change_reason) "
            "values($1::uuid,$2::uuid,$3::uuid,$4::uuid,'手工新增测试') returning id::text",
            technical_package_id_, maintenance_package_id, tree_version_id_, user_id_
        )[0]["id"].as<std::string>();
        year_id_ = client_->execSqlSync(
            "insert into inspection_years (bridge_id, inspection_year, status, is_current,"
            "standard_profile_id) values ($1::uuid,2026,'待校对',true,$2::uuid) returning id::text",
            bridge_id_, profile_id_)[0]["id"].as<std::string>();
        import_id_ = client_->execSqlSync(
            "insert into import_records (bridge_id, inspection_year_id, import_name,"
            "source_type, import_status) values ($1::uuid,$2::uuid,'手工新增测试',"
            "'接口同步','解析中') returning id::text",
            bridge_id_, year_id_)[0]["id"].as<std::string>();
        source_file_id_ = client_->execSqlSync(
            "insert into import_source_files (import_record_id, original_file_name,"
            "storage_relative_path, file_extension, file_size_bytes, file_hash, status,"
            "parsing_started_at) values ($1::uuid,'sync.srcref',$1::text||'.srcref',"
            "'.srcref',9,$2,'解析中',now()) returning id::text",
            import_id_, std::string(64, 'a'))[0]["id"].as<std::string>();

        seed_inventory();
        import_empty_parse_result();
    }

    void TearDown() override {
        if (!client_ || import_id_.empty()) return;
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
        client_->execSqlSync("delete from project_standard_profiles where id=$1::uuid", profile_id_);
        client_->execSqlSync("delete from bridges where id=$1::uuid", bridge_id_);
        client_->closeAll();
    }

    void seed_inventory() {
        revision_id_ = client_->execSqlSync(
            "insert into bridge_component_inventory_revisions(bridge_id,revision_number,"
            "created_by_user_id) values($1::uuid,1,$2::uuid) returning id::text",
            bridge_id_, user_id_)[0]["id"].as<std::string>();
        for (int index = 1; index <= 2; ++index) {
            const auto number = std::to_string(index) + "#手工";
            const auto component_id = client_->execSqlSync(
                "insert into bridge_components(bridge_id,structure_part,component_type,"
                "business_component_code,normalized_component_key) "
                "values($1::uuid,'桥面系','手工构件',$2,$3) returning id::text",
                bridge_id_, number, "manual-" + std::to_string(index)
            )[0]["id"].as<std::string>();
            component_ids_.push_back(component_id);
            const auto entry_id = client_->execSqlSync(
                "insert into bridge_component_inventory_entries(inventory_revision_id,"
                "bridge_component_id,component_number,site_name,site_component_type,sort_order) "
                "values($1::uuid,$2::uuid,$3,'手工现场件','手工现场件',$4) returning id::text",
                revision_id_, component_id, number, index)[0]["id"].as<std::string>();
            client_->execSqlSync(
                "insert into bridge_component_standard_mappings(inventory_entry_id,"
                "standard_package_id,standard_bridge_type_id,standard_component_category_id,"
                "structure_part,mapping_source,confirmation_status,confirmed_by_user_id,"
                "confirmed_at) values($1::uuid,$2::uuid,$3,$4,'deck_system','规范模板',"
                "'已确认',$5::uuid,now())",
                entry_id, technical_package_id_, bridge_type_id_, category_id_, user_id_);
        }
        client_->execSqlSync(
            "update bridge_component_inventory_revisions set status='已确认',"
            "confirmed_by_user_id=$2::uuid,confirmed_at=now() where id=$1::uuid",
            revision_id_, user_id_);
    }

    /// 走真实导入路径落一份没有病害的解析结果，让导入进入待校对相。
    void import_empty_parse_result() {
        bridge_report::archive::ArchivedPhotoBatch batch;
        batch.data["contract"]["parser_name"] = "source-db-importer";
        batch.data["contract"]["parser_version"] = "1.0.0";
        batch.data["defects"] = Json::Value(Json::arrayValue);
        batch.data["photos"] = Json::Value(Json::arrayValue);
        const auto outcome =
            bridge_report::db::WordImportRepository(client_).persist_parse_result(
                import_id_, batch);
        ASSERT_TRUE(outcome.success) << outcome.error_code << ": " << outcome.error_message;
    }

    ManualDefectRequest request(const std::string& component_id, int draft_version) {
        ManualDefectRequest command;
        command.context.import_record_id = import_id_;
        command.context.actor_user_id = user_id_;
        command.context.edit_lock = std::nullopt;
        command.context.expected_inventory_revision_id = revision_id_;
        command.expected_draft_version = draft_version;
        command.bridge_component_id = component_id;
        command.rating_tree_node_id = node_id_;
        command.defect_facts["defect_type"] = "裂缝";
        command.defect_facts["defect_location"] = "顶面";
        command.defect_facts["defect_description"] = "人工补录的裂缝";
        command.defect_facts["defect_scale"] = 2;
        return command;
    }

    int draft_version() {
        return client_->execSqlSync(
            "select draft_version from import_records where id=$1::uuid", import_id_
        )[0]["draft_version"].as<int>();
    }

    /// 用真实命令把唯一那个组清回未解析。
    bridge_report::resolution::ResolutionOutcome clear_only_group() {
        const ImportResolutionService service(client_);
        const auto workspace = service.load_workspace(import_id_);
        EXPECT_EQ(workspace.status, ResolutionStatus::Ok) << workspace.error_message;
        EXPECT_EQ(workspace.workspace->groups.size(), 1u);
        bridge_report::resolution::ComponentResolutionRequest clear;
        clear.context.import_record_id = import_id_;
        clear.context.actor_user_id = user_id_;
        clear.context.edit_lock = std::nullopt;
        clear.context.expected_inventory_revision_id = revision_id_;
        clear.group_id = workspace.workspace->groups[0].group_id;
        clear.expected_version = workspace.workspace->groups[0].version;
        clear.action = "clear";
        return service.apply_component_resolution(clear);
    }

    drogon::orm::DbClientPtr client_;
    std::string user_id_;
    std::string bridge_id_;
    std::string year_id_;
    std::string profile_id_;
    std::string import_id_;
    std::string source_file_id_;
    std::string revision_id_;
    std::string tree_version_id_;
    std::string technical_package_id_;
    std::string node_id_;
    std::string inapplicable_node_id_;
    std::string bridge_type_id_;
    std::string category_id_;
    std::vector<std::string> component_ids_;
};

}  // namespace

TEST_F(ManualDefectCommandTest, WritesSourceFactAndExplicitResolutionInOneGo) {
    const auto outcome = ImportResolutionService(client_).add_manual_defect(
        request(component_ids_[0], draft_version()));
    ASSERT_EQ(outcome.status, ResolutionStatus::Ok) << outcome.error_message;
    ASSERT_TRUE(outcome.manual_defect.has_value());

    // 来源事实进 JSON。
    const auto& defect = outcome.manual_defect->source_defect;
    EXPECT_EQ(defect["component_name"].asString(), "手工现场件");
    EXPECT_EQ(defect["component_number"].asString(), "1#手工");
    EXPECT_EQ(defect["source_ref"]["source_type"].asString(), "manual");
    EXPECT_EQ(defect["review_status"].asString(), "已修改");
    // 5.0：解析字段一个都不该出现在来源事实里。
    for (const auto* field : {"bridge_component_id", "rating_tree_node_id",
                              "component_match_method", "range_split_origin"}) {
        EXPECT_FALSE(defect.isMember(field)) << field;
    }

    const auto stored = client_->execSqlSync(
        "select ir.parsed_result_json#>>'{defects,0,candidate_id}' as candidate_id "
        "from import_records ir where ir.id=$1::uuid", import_id_);
    EXPECT_EQ(stored[0]["candidate_id"].as<std::string>(),
              defect["candidate_id"].asString());

    // 解析状态进关系表，且保住用户选定的目标与节点——这正是这条命令存在的理由。
    ASSERT_EQ(outcome.manual_defect->command_result.affected_groups.size(), 1u);
    const auto& group = outcome.manual_defect->command_result.affected_groups[0];
    EXPECT_EQ(group.status, "bound");
    EXPECT_EQ(group.match_method.value_or(""), "manual");
    ASSERT_EQ(group.targets.size(), 1u);
    EXPECT_EQ(group.targets[0].bridge_component_id, component_ids_[0]);
    ASSERT_EQ(group.members.size(), 1u);
    ASSERT_EQ(group.members[0].instances.size(), 1u);
    const auto& instance = group.members[0].instances[0];
    EXPECT_TRUE(instance.has_rating);
    EXPECT_EQ(instance.rating_status, "matched");
    EXPECT_EQ(instance.rating_tree_node_id.value_or(""), node_id_);
    EXPECT_EQ(instance.rating_match_method.value_or(""), "manual");
    EXPECT_TRUE(instance.is_photo_owner);
}

TEST_F(ManualDefectCommandTest, BumpsTheDraftVersionAndReportsIt) {
    const auto before = draft_version();
    const auto outcome = ImportResolutionService(client_).add_manual_defect(
        request(component_ids_[0], before));
    ASSERT_EQ(outcome.status, ResolutionStatus::Ok) << outcome.error_message;

    EXPECT_EQ(outcome.manual_defect->draft_version, before + 1);
    EXPECT_EQ(draft_version(), before + 1);
}

// 陈旧的整份草稿不能把手工新增刚写进去的来源病害当成"用户删掉了"，反过来也一样：
// 后到的那个必须看见明确的版本冲突（§8.0）。
TEST_F(ManualDefectCommandTest, StaleDraftVersionIsRejected) {
    const auto stale = draft_version();
    const ImportResolutionService service(client_);
    ASSERT_EQ(service.add_manual_defect(request(component_ids_[0], stale)).status,
              ResolutionStatus::Ok);

    const auto rejected = service.add_manual_defect(request(component_ids_[1], stale));
    EXPECT_EQ(rejected.status, ResolutionStatus::VersionConflict);
    EXPECT_EQ(rejected.error_code, "review_draft_version_conflict");
    EXPECT_EQ(draft_version(), stale + 1) << "被拒的请求不该改动草稿版本";
}

// §4.6 第一行：命中的既有组已绑到同一个构件 → 复用该组，只加成员和实例。
TEST_F(ManualDefectCommandTest, ReusesAGroupAlreadyBoundToTheSameComponent) {
    const ImportResolutionService service(client_);
    ASSERT_EQ(service.add_manual_defect(request(component_ids_[0], draft_version())).status,
              ResolutionStatus::Ok);

    const auto second = service.add_manual_defect(
        request(component_ids_[0], draft_version()));
    ASSERT_EQ(second.status, ResolutionStatus::Ok) << second.error_message;

    const auto groups = client_->execSqlSync(
        "select count(*) as groups from import_component_resolution_groups "
        "where import_record_id=$1::uuid", import_id_);
    EXPECT_EQ(groups[0]["groups"].as<long long>(), 1) << "复用失败，编出了第二个组";

    ASSERT_EQ(second.manual_defect->command_result.affected_groups.size(), 1u);
    EXPECT_EQ(second.manual_defect->command_result.affected_groups[0].members.size(), 2u);
}

// §4.6 第二行：既有组已绑到别的构件 → 拒绝，不能借新增命令悄悄重绑一批旧病害。
TEST_F(ManualDefectCommandTest, RejectsWhenTheGroupIsBoundElsewhere) {
    const ImportResolutionService service(client_);
    ASSERT_EQ(service.add_manual_defect(request(component_ids_[0], draft_version())).status,
              ResolutionStatus::Ok);

    // 把组重绑到第二个构件，再用第一个构件新增：组键相同（现场类型 + 编号来自
    // 用户选的条目），但目标已经不是它了。
    client_->execSqlSync(
        "update import_component_resolution_targets t set bridge_component_id=$2::uuid "
        "from import_component_resolution_groups g "
        "where g.id=t.group_id and g.import_record_id=$1::uuid",
        import_id_, component_ids_[1]);

    const auto rejected = service.add_manual_defect(
        request(component_ids_[0], draft_version()));
    EXPECT_EQ(rejected.status, ResolutionStatus::Conflict);
    EXPECT_EQ(rejected.error_code, "manual_defect_group_conflict");
}

// §4.6 第三行：既有组还没解析且已有成员 → 引导先去绑定工作区，别在单条新增里
// 顺手把一批旧病害也绑上。
TEST_F(ManualDefectCommandTest, RejectsWhenTheGroupIsStillUnresolved) {
    const ImportResolutionService service(client_);
    ASSERT_EQ(service.add_manual_defect(request(component_ids_[0], draft_version())).status,
              ResolutionStatus::Ok);

    // 用真实命令把组打回未解析。手写 SQL 改状态要跟"bound 组必须有目标"的延迟约束
    // 打架，而且绕过的正是被测代码本身该走的那条路。
    ASSERT_EQ(clear_only_group().status, ResolutionStatus::Ok);

    const auto rejected = service.add_manual_defect(
        request(component_ids_[0], draft_version()));
    EXPECT_EQ(rejected.status, ResolutionStatus::Conflict);
    EXPECT_EQ(rejected.error_code, "manual_defect_group_requires_resolution");
}

TEST_F(ManualDefectCommandTest, RejectsAnInapplicableRatingTreeNode) {
    if (inapplicable_node_id_.empty()) {
        GTEST_SKIP() << "评定树里找不到类别不同的可选节点";
    }
    const auto before = draft_version();
    auto command = request(component_ids_[0], before);
    command.rating_tree_node_id = inapplicable_node_id_;

    const auto rejected = ImportResolutionService(client_).add_manual_defect(command);
    EXPECT_EQ(rejected.status, ResolutionStatus::Conflict);
    EXPECT_EQ(rejected.error_code, "target_not_allowed");
    EXPECT_EQ(draft_version(), before) << "被拒的请求不该改动草稿";
}

TEST_F(ManualDefectCommandTest, RequiresDefectTypeLocationAndDescription) {
    auto command = request(component_ids_[0], draft_version());
    command.defect_facts["defect_description"] = "";

    const auto rejected = ImportResolutionService(client_).add_manual_defect(command);
    EXPECT_EQ(rejected.status, ResolutionStatus::Invalid);
}

TEST_F(ManualDefectCommandTest, ResponseCarriesTheNewDefectAndVersionTogether) {
    const auto outcome = ImportResolutionService(client_).add_manual_defect(
        request(component_ids_[0], draft_version()));
    ASSERT_EQ(outcome.status, ResolutionStatus::Ok) << outcome.error_message;

    const auto json =
        bridge_report::http::resolution_manual_defect_json(*outcome.manual_defect);
    // 两者必须在同一个响应里：只更新版本却保留缺少新候选的旧草稿，下一次整份保存
    // 就会把它当成"用户删掉了"（§16.1）。
    ASSERT_TRUE(json.isMember("source_defect"));
    ASSERT_TRUE(json.isMember("draft_version"));
    ASSERT_TRUE(json.isMember("result"));
    EXPECT_TRUE(json["source_defect"].isMember("candidate_id"));
    EXPECT_TRUE(json["result"].isMember("affected_groups"));
    EXPECT_TRUE(json["result"].isMember("progress"));
}
