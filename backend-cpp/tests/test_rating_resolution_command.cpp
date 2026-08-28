#include <cstdlib>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/db/WordImportRepository.hpp"
#include "bridge_report/resolution/ImportResolutionService.hpp"

// 评分树解析命令（设计 §9.2）与验收标准 6/7/24。
//
// 这一组守的是人工选择的去留：改几个字不该把用户的判断冲掉，而适用性一旦变了，
// 旧结果就不能再混进正式确认。两条规则合起来才是"人工优先，但不越过适用性"——
// 只做其中一条，症状分别是"用户白选了"和"评定阶段才报该构件不适用此病害"。

namespace {

using bridge_report::resolution::ComponentResolutionRequest;
using bridge_report::resolution::FactOverrideRequest;
using bridge_report::resolution::ImportResolutionService;
using bridge_report::resolution::RatingResolutionRequest;
using bridge_report::resolution::ResolutionStatus;
using bridge_report::resolution::ResolutionTargetSelection;

class RatingResolutionCommandTest : public testing::Test {
protected:
    void SetUp() override {
        if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) {
            GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL is not set";
        }
        client_ = bridge_report::db::create_db_client(bridge_report::config::PostgresConfig{});

        // 复用库里已发布的评定树：适用性判定必须按真实规则走，自己拼一棵树容易脱节。
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
        const auto maintenance_package_id = tree[0]["maintenance_package_id"].as<std::string>();

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
            "insert into bridges (bridge_name) values ('评分树解析测试桥') returning id::text"
        )[0]["id"].as<std::string>();
        profile_id_ = client_->execSqlSync(
            "insert into project_standard_profiles(technical_condition_package_id,"
            "maintenance_package_id,rating_tree_version_id,created_by_user_id,change_reason) "
            "values($1::uuid,$2::uuid,$3::uuid,$4::uuid,'评分树解析测试') returning id::text",
            technical_package_id_, maintenance_package_id, tree_version_id_, user_id_
        )[0]["id"].as<std::string>();
        year_id_ = client_->execSqlSync(
            "insert into inspection_years (bridge_id, inspection_year, status, is_current,"
            "standard_profile_id) values ($1::uuid,2026,'待校对',true,$2::uuid) returning id::text",
            bridge_id_, profile_id_)[0]["id"].as<std::string>();
        import_id_ = client_->execSqlSync(
            "insert into import_records (bridge_id, inspection_year_id, import_name,"
            "source_type, import_status) values ($1::uuid,$2::uuid,'评分树解析测试',"
            "'接口同步','解析中') returning id::text",
            bridge_id_, year_id_)[0]["id"].as<std::string>();
        source_file_id_ = client_->execSqlSync(
            "insert into import_source_files (import_record_id, original_file_name,"
            "storage_relative_path, file_extension, file_size_bytes, file_hash, status,"
            "parsing_started_at) values ($1::uuid,'sync.srcref',$1::text||'.srcref',"
            "'.srcref',9,$2,'解析中',now()) returning id::text",
            import_id_, std::string(64, 'a'))[0]["id"].as<std::string>();

        seed_inventory();
        import_one_defect();
        // 不走自动匹配：它要求报告部件名称能通过对照表解出类别，而这里的类别
        // 是从评定树节点反推出来的，两者不一定对得上。本组用例要测的是评分树解析，
        // 构件怎么绑上去不是重点，直接用真实命令绑定。
        bind_to(0);
    }

    void TearDown() override {
        if (!client_ || import_id_.empty()) return;
        client_->execSqlSync("delete from import_source_files where id=$1::uuid", source_file_id_);
        client_->execSqlSync("delete from import_records where id=$1::uuid", import_id_);
        client_->execSqlSync(
            "update inspection_years set component_inventory_revision_id=null where id=$1::uuid",
            year_id_);
        client_->execSqlSync(
            "delete from bridge_component_inventory_revisions where bridge_id=$1::uuid", bridge_id_);
        client_->execSqlSync("delete from bridge_components where bridge_id=$1::uuid", bridge_id_);
        client_->execSqlSync("delete from inspection_years where id=$1::uuid", year_id_);
        client_->execSqlSync("delete from project_standard_profiles where id=$1::uuid", profile_id_);
        client_->execSqlSync("delete from bridges where id=$1::uuid", bridge_id_);
        client_->closeAll();
    }

    /// 两件构件同桥型同类别：换目标时变的只有 bridge_component_id 这一项适用性输入。
    void seed_inventory() {
        revision_id_ = client_->execSqlSync(
            "insert into bridge_component_inventory_revisions(bridge_id,revision_number,"
            "created_by_user_id) values($1::uuid,1,$2::uuid) returning id::text",
            bridge_id_, user_id_)[0]["id"].as<std::string>();
        for (int index = 1; index <= 2; ++index) {
            const auto number = std::to_string(index) + "-1#梁";
            const auto component_id = client_->execSqlSync(
                "insert into bridge_components(bridge_id,structure_part,component_type,"
                "business_component_code,normalized_component_key) "
                "values($1::uuid,'桥面系','评分构件',$2,$3) returning id::text",
                bridge_id_, number, "rating-" + std::to_string(index)
            )[0]["id"].as<std::string>();
            component_ids_.push_back(component_id);
            const auto entry_id = client_->execSqlSync(
                "insert into bridge_component_inventory_entries(inventory_revision_id,"
                "bridge_component_id,component_number,site_name,site_component_type,sort_order) "
                "values($1::uuid,$2::uuid,$3,'评分现场件','评分现场件',$4) returning id::text",
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

    /// 编号直接命中 1#评分件：导入即自动绑定，省掉一次手动绑定。
    void import_one_defect() {
        bridge_report::archive::ArchivedPhotoBatch batch;
        batch.data["contract"]["parser_name"] = "source-db-importer";
        batch.data["contract"]["parser_version"] = "1.0.0";
        batch.data["photos"] = Json::Value(Json::arrayValue);
        Json::Value defect(Json::objectValue);
        defect["candidate_id"] = "source_defect_0001";
        defect["component_name"] = "评分现场件";
        defect["component_number"] = "1-1#梁";
        defect["defect_type"] = "裂缝";
        defect["defect_location"] = "顶面";
        defect["defect_description"] = "顶面出现纵向裂缝";
        defect["warnings"] = Json::Value(Json::arrayValue);
        batch.data["defects"].append(defect);
        const auto outcome =
            bridge_report::db::WordImportRepository(client_).persist_parse_result(import_id_, batch);
        ASSERT_TRUE(outcome.success) << outcome.error_code << ": " << outcome.error_message;
    }

    bridge_report::resolution::ResolutionCommandContext context() const {
        bridge_report::resolution::ResolutionCommandContext ctx;
        ctx.import_record_id = import_id_;
        ctx.actor_user_id = user_id_;
        ctx.edit_lock = std::nullopt;
        ctx.expected_inventory_revision_id = revision_id_;
        return ctx;
    }

    /// 用真实命令把唯一那个组绑到第 index 件构件上。
    void bind_to(std::size_t index) {
        const auto group = only_group();
        ComponentResolutionRequest request;
        request.context = context();
        request.group_id = group.group_id;
        request.expected_version = group.version;
        request.action = "bind";
        ResolutionTargetSelection selection;
        selection.bridge_component_id = component_ids_[index];
        selection.target_role = "primary";
        request.targets.push_back(selection);
        const auto outcome = ImportResolutionService(client_).apply_component_resolution(request);
        ASSERT_EQ(outcome.status, ResolutionStatus::Ok) << outcome.error_message;
    }

    bridge_report::resolution::WorkspaceComponentGroup only_group() {
        const auto outcome = ImportResolutionService(client_).load_workspace(import_id_);
        EXPECT_EQ(outcome.status, ResolutionStatus::Ok) << outcome.error_message;
        EXPECT_EQ(outcome.workspace->groups.size(), 1u);
        return outcome.workspace->groups[0];
    }

    bridge_report::resolution::WorkspaceDefectInstance only_instance() {
        const auto group = only_group();
        EXPECT_EQ(group.members.size(), 1u);
        EXPECT_EQ(group.members[0].instances.size(), 1u);
        return group.members[0].instances[0];
    }

    /// 人工选定节点。这是评分树解析的唯一写入口。
    bridge_report::resolution::ResolutionOutcome select_node(const std::string& node_id) {
        const auto instance = only_instance();
        RatingResolutionRequest request;
        request.context = context();
        request.instance_id = instance.instance_id;
        request.expected_version = instance.has_rating ? instance.rating_version : 0;
        request.rating_tree_node_id = node_id;
        return ImportResolutionService(client_).apply_rating_resolution(request);
    }

    drogon::orm::DbClientPtr client_;
    std::string user_id_;
    std::string bridge_id_;
    std::string year_id_;
    std::string import_id_;
    std::string source_file_id_;
    std::string revision_id_;
    std::string profile_id_;
    std::string tree_version_id_;
    std::string technical_package_id_;
    std::string node_id_;
    std::string inapplicable_node_id_;
    std::string bridge_type_id_;
    std::string category_id_;
    std::vector<std::string> component_ids_;
};

}  // namespace

TEST_F(RatingResolutionCommandTest, ManualSelectionIsStoredAsManual) {
    ASSERT_EQ(only_group().status, "bound");
    ASSERT_EQ(select_node(node_id_).status, ResolutionStatus::Ok);

    const auto instance = only_instance();
    EXPECT_TRUE(instance.has_rating);
    EXPECT_EQ(instance.rating_status, "matched");
    EXPECT_EQ(instance.rating_tree_node_id.value_or(""), node_id_);
    EXPECT_EQ(instance.rating_match_method.value_or(""), "manual");
}

// 验收标准 24：普通文字修改后人工选择继续保留。
//
// 反过来做（按完整输入哈希一律失效）会让用户每改一个错别字就得重选一次节点，而且
// 重选前那条记录看着像"系统没匹配上"，根本分不清是谁把它清掉的。
TEST_F(RatingResolutionCommandTest, ManualSelectionSurvivesAnOrdinaryTextEdit) {
    ASSERT_EQ(select_node(node_id_).status, ResolutionStatus::Ok);
    const auto before = only_instance();
    const auto hash_before = client_->execSqlSync(
        "select match_input_hash from import_rating_resolutions "
        "where resolved_defect_instance_id=$1::uuid",
        before.instance_id)[0]["match_input_hash"].as<std::string>();

    FactOverrideRequest override_request;
    override_request.context = context();
    override_request.instance_id = before.instance_id;
    override_request.expected_version = before.version;
    override_request.overrides["defect_description"] = "顶面出现纵向裂缝（复核后改写）";
    ASSERT_EQ(ImportResolutionService(client_).apply_fact_overrides(override_request).status,
              ResolutionStatus::Ok);

    const auto after = only_instance();
    EXPECT_EQ(after.rating_tree_node_id.value_or(""), node_id_);
    EXPECT_EQ(after.rating_match_method.value_or(""), "manual");
    // 节点留着，但输入哈希跟着有效事实走——否则下一次适用性判定会拿旧输入去比。
    const auto hash_after = client_->execSqlSync(
        "select match_input_hash from import_rating_resolutions "
        "where resolved_defect_instance_id=$1::uuid",
        after.instance_id)[0]["match_input_hash"].as<std::string>();
    EXPECT_NE(hash_after, hash_before);
}

// 验收标准 6/24：构件目标变了就是适用性变了，人工选择不再保留。
//
// applicability_hash 里含 bridge_component_id，所以哪怕新目标同桥型同类别也照样失效：
// "这条病害挂在哪件构件上"本身就是判定输入。
TEST_F(RatingResolutionCommandTest, RetargetingTheComponentDropsTheManualSelection) {
    ASSERT_EQ(select_node(node_id_).status, ResolutionStatus::Ok);
    const auto before = only_instance();
    const auto applicability_before = client_->execSqlSync(
        "select applicability_hash from import_rating_resolutions "
        "where resolved_defect_instance_id=$1::uuid",
        before.instance_id)[0]["applicability_hash"].as<std::string>();

    const auto group = only_group();
    ComponentResolutionRequest rebind;
    rebind.context = context();
    rebind.group_id = group.group_id;
    rebind.expected_version = group.version;
    rebind.action = "bind";
    ResolutionTargetSelection selection;
    selection.bridge_component_id = component_ids_[1];
    selection.target_role = "primary";
    rebind.targets.push_back(selection);
    ASSERT_EQ(ImportResolutionService(client_).apply_component_resolution(rebind).status,
              ResolutionStatus::Ok);

    const auto after = only_instance();
    EXPECT_EQ(after.bridge_component_id, component_ids_[1]);
    // 人工那一条没了：要么被自动匹配重写，要么退回未解析，但不能还挂着人工标记。
    EXPECT_NE(after.rating_match_method.value_or(""), "manual");
    if (after.has_rating) {
        const auto applicability_after = client_->execSqlSync(
            "select applicability_hash from import_rating_resolutions "
            "where resolved_defect_instance_id=$1::uuid",
            after.instance_id)[0]["applicability_hash"].as<std::string>();
        EXPECT_NE(applicability_after, applicability_before);
    }
}

// 节点存在还不够，还得适用于目标构件的桥型与类别。存进去的话，要到评定阶段才以
// "该构件不适用此病害"暴露出来，那时已经隔了好几步。
TEST_F(RatingResolutionCommandTest, AnInapplicableNodeIsRejected) {
    if (inapplicable_node_id_.empty()) {
        GTEST_SKIP() << "评定树里找不到类别不同的可选节点";
    }
    const auto outcome = select_node(inapplicable_node_id_);
    EXPECT_EQ(outcome.status, ResolutionStatus::Conflict);
    const auto instance = only_instance();
    EXPECT_NE(instance.rating_tree_node_id.value_or(""), inapplicable_node_id_);
    EXPECT_NE(instance.rating_match_method.value_or(""), "manual");
}

// 传空节点即清除人工选择，退回未解析。约束上 unresolved 不许留着节点。
TEST_F(RatingResolutionCommandTest, AnEmptyNodeClearsTheSelection) {
    ASSERT_EQ(select_node(node_id_).status, ResolutionStatus::Ok);
    ASSERT_EQ(only_instance().rating_status, "matched");

    ASSERT_EQ(select_node("").status, ResolutionStatus::Ok);
    const auto instance = only_instance();
    EXPECT_EQ(instance.rating_status, "unresolved");
    EXPECT_FALSE(instance.rating_tree_node_id.has_value());
}

// 陈旧版本一律不写：并发下两个页面各自选一个节点时，后到的那个必须撞版本冲突，
// 而不是默默覆盖先到的。
TEST_F(RatingResolutionCommandTest, StaleInstanceVersionIsRejected) {
    ASSERT_EQ(select_node(node_id_).status, ResolutionStatus::Ok);
    const auto instance = only_instance();

    RatingResolutionRequest stale;
    stale.context = context();
    stale.instance_id = instance.instance_id;
    stale.expected_version = instance.rating_version - 1;
    stale.rating_tree_node_id = node_id_;
    const auto outcome = ImportResolutionService(client_).apply_rating_resolution(stale);
    EXPECT_EQ(outcome.status, ResolutionStatus::VersionConflict);
    EXPECT_EQ(outcome.error_code, "resolution_version_conflict");
}
