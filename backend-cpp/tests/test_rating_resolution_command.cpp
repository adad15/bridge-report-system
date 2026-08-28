#include <cstdlib>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/db/WordImportRepository.hpp"
#include "bridge_report/db/InspectionRatingTreeRepository.hpp"
#include "bridge_report/resolution/ImportResolutionService.hpp"
#include "RatingTreeFixture.hpp"

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

        user_id_ = client_->execSqlSync(
            "select id::text from users where username='admin'")[0]["id"].as<std::string>();
        // 夹具自带评定树：check-backend-tests.ps1 每次从空 schema 开始，那里没有任何
        // 已发布的树，靠"从库里找一棵"会让整套用例静默跳过。
        tree_ = bridge_report::testing::seed_rating_tree(client_, user_id_, "rating-res");
        tree_version_id_ = tree_.tree_version_id;
        technical_package_id_ = tree_.technical_package_id;
        node_id_ = tree_.node_id;
        inapplicable_node_id_ = tree_.inapplicable_node_id;
        bridge_type_id_ = tree_.bridge_type_id;
        category_id_ = tree_.component_category_id;
        profile_id_ = tree_.profile_id;

        bridge_id_ = client_->execSqlSync(
            "insert into bridges (bridge_name) values ('评分树解析测试桥') returning id::text"
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
        client_->execSqlSync("delete from bridges where id=$1::uuid", bridge_id_);
        bridge_report::testing::drop_rating_tree(client_, tree_);
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
    bridge_report::testing::RatingTreeFixture tree_;
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

// P1-1 回归：切换年度评定树不得改写来源草稿。
//
// 这个接口是 4.0 绑定链路里唯一活下来的一个。它当初的职责包含"顺手把草稿里的评定树
// 关联定稿"，于是会逐条病害写 rating_tree_node_id / standard_defect_indicator_id /
// rating_tree_match_method / rating_tree_match_evidence，再整份覆盖 parsed_result_json。
//
// 5.0 删掉了这些字段。写 null 同样会建出键，于是草稿当场变成非法契约——下一次打开
// 校对页就是一句"校对数据不符合 BridgeAnnualInspectionData 契约"，而且这次是真写进了库。
//
// 必须切到**另一棵**树才能复现：绑定同一棵树有一条早退分支，压根不走到改写那段。
TEST_F(RatingResolutionCommandTest, SwitchingTheRatingTreeLeavesTheSourceDraftAlone) {
    const auto other =
        bridge_report::testing::seed_rating_tree(client_, user_id_, "rating-res-alt");

    const auto before = client_->execSqlSync(
        "select parsed_result_json::text as json from import_records where id=$1::uuid",
        import_id_)[0]["json"].as<std::string>();

    const auto outcome = bridge_report::db::InspectionRatingTreeRepository(client_)
        .bind_rating_tree(import_id_, other.tree_version_id, user_id_, revision_id_);
    ASSERT_EQ(outcome.status, bridge_report::db::RatingTreeBindingStatus::Ok)
        << outcome.error_code << ": " << outcome.error_message;

    const auto after = client_->execSqlSync(
        "select parsed_result_json::text as json from import_records where id=$1::uuid",
        import_id_)[0]["json"].as<std::string>();
    EXPECT_EQ(after, before);

    // 逐字比对之外再点名一次：这几个键一个都不能出现。
    const auto smuggled = client_->execSqlSync(
        "select count(*)::int as n from ("
        "  select jsonb_array_elements(parsed_result_json->'defects') as d"
        "  from import_records where id=$1::uuid) s "
        "where d ? 'rating_tree_node_id' or d ? 'standard_defect_indicator_id' "
        "   or d ? 'rating_tree_match_method' or d ? 'rating_tree_match_evidence' "
        "   or d ? 'component_inventory_revision_id'",
        import_id_)[0]["n"].as<int>();
    EXPECT_EQ(smuggled, 0);

    client_->execSqlSync(
        "update inspection_years set standard_profile_id=$2::uuid where id=$1::uuid",
        year_id_, profile_id_);
    bridge_report::testing::drop_rating_tree(client_, other);
}

// P2-4 回归：人工裁决之后改了文字，节点要留住，同时给出复核提示。
//
// §8.5 的两半：前一半（改文字不冲掉人工选择）本来就做到了；后一半没有——保留裁决时
// 代码把 match_input_hash 一并更新成最新值，"裁决当时的输入"当场丢失，无从比较，
// content_changed_after_manual_resolution 于是永远是 false，那句提示永远不出现。
TEST_F(RatingResolutionCommandTest, FlagsContentChangedAfterAManualChoice) {
    ASSERT_EQ(select_node(node_id_).status, ResolutionStatus::Ok);
    // 刚裁决完：内容没变过，不该提示。
    EXPECT_FALSE(only_instance().content_changed_after_manual_resolution);

    const auto instance = only_instance();
    FactOverrideRequest override_request;
    override_request.context = context();
    override_request.instance_id = instance.instance_id;
    override_request.expected_version = instance.version;
    override_request.overrides["defect_description"] = "顶面出现纵向裂缝（复核后改写）";
    ASSERT_EQ(ImportResolutionService(client_).apply_fact_overrides(override_request).status,
              ResolutionStatus::Ok);

    const auto after = only_instance();
    // 节点仍是人选的那个——改文字不该冲掉人工判断。
    EXPECT_EQ(after.rating_tree_node_id.value_or(""), node_id_);
    EXPECT_EQ(after.rating_match_method.value_or(""), "manual");
    // 但要提示复核：文字变了，当初据以判断的依据已经不同。
    EXPECT_TRUE(after.content_changed_after_manual_resolution);
}

// 自动匹配的结果不带这个标记：它本来就会在输入变化时重新匹配，没有"人工判断待复核"
// 这回事，挂上提示只会让人以为有什么要处理。
TEST_F(RatingResolutionCommandTest, AutomaticResultsNeverFlagContentChange) {
    const auto instance = only_instance();
    if (!instance.has_rating || instance.rating_match_method.value_or("") == "manual") {
        GTEST_SKIP() << "这条病害没有自动匹配结果";
    }
    EXPECT_FALSE(instance.content_changed_after_manual_resolution);
}

// P2-8 回归：实例级命令也要校验台账版本。
//
// 用户打开工作区后台账被别处确认成新版本时，旧页面手里的候选、类别映射、可选节点全是
// 按旧版本算的。放它写进去，等于让一份基于过时语义的判断落库，而出错的症状会出现在很远
// 的地方——确认入库阶段才报"该构件不适用此病害"。
TEST_F(RatingResolutionCommandTest, RejectsAStaleInventoryRevisionOnRatingCommands) {
    const auto instance = only_instance();
    RatingResolutionRequest request;
    request.context = context();
    request.context.expected_inventory_revision_id = "00000000-0000-4000-8000-000000000000";
    request.instance_id = instance.instance_id;
    request.expected_version = instance.has_rating ? instance.rating_version : 0;
    request.rating_tree_node_id = node_id_;

    const auto outcome = ImportResolutionService(client_).apply_rating_resolution(request);
    EXPECT_EQ(outcome.status, ResolutionStatus::Conflict);
    EXPECT_EQ(outcome.error_code, "component_inventory_revision_changed");
}

TEST_F(RatingResolutionCommandTest, RejectsAStaleInventoryRevisionOnFactOverrides) {
    const auto instance = only_instance();
    FactOverrideRequest request;
    request.context = context();
    request.context.expected_inventory_revision_id = "00000000-0000-4000-8000-000000000000";
    request.instance_id = instance.instance_id;
    request.expected_version = instance.version;
    request.overrides["defect_description"] = "改一句";

    const auto outcome = ImportResolutionService(client_).apply_fact_overrides(request);
    EXPECT_EQ(outcome.status, ResolutionStatus::Conflict);
    EXPECT_EQ(outcome.error_code, "component_inventory_revision_changed");
}
