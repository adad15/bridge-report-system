#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/db/WordImportRepository.hpp"
#include "bridge_report/db/InspectionRatingTreeRepository.hpp"
#include "bridge_report/resolution/ImportResolutionService.hpp"
#include "bridge_report/resolution/ConfirmResolutionReader.hpp"
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
using bridge_report::resolution::SourceRatingResolutionRequest;

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

    /// 绑到两件构件：一条来源病害挂两条活动实例，用来验证按来源病害整体写。
    void bind_to_both() {
        const auto group = only_group();
        ComponentResolutionRequest request;
        request.context = context();
        request.group_id = group.group_id;
        request.expected_version = group.version;
        request.action = "bind";
        for (const auto& component_id : component_ids_) {
            ResolutionTargetSelection selection;
            selection.bridge_component_id = component_id;
            selection.target_role = "range_member";
            request.targets.push_back(selection);
        }
        const auto outcome = ImportResolutionService(client_).apply_component_resolution(request);
        ASSERT_EQ(outcome.status, ResolutionStatus::Ok) << outcome.error_message;
    }

    std::vector<bridge_report::resolution::WorkspaceDefectInstance> all_instances() {
        const auto group = only_group();
        EXPECT_EQ(group.members.size(), 1u);
        return group.members[0].instances;
    }

    std::string only_member_id() {
        const auto group = only_group();
        EXPECT_EQ(group.members.size(), 1u);
        return group.members[0].member_id;
    }

    /// 夹具里唯一那条来源病害（见 make_draft 里的 candidate_id）。
    static constexpr const char* kSourceCandidateId = "source_defect_0001";

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

// 可确认视图必须带着评定要读的那几个字段。
//
// 系统评定逐条读 bridge_component_id / standard_component_category_id / rating_tree_node_id
// 来判"这条病害挂在哪件构件、算哪个指标"。5.0 草稿里没有这些字段，直接拿草稿去算，
// 判定全部落空，每条病害都报"未关联到当前已确认台账中的规范构件"——整份试算作废，
// 而校对页会把这些评定问题挂到每一行上，于是一条都确认不了。
//
// 视图是评定、预检、写计划三者共同的数据来源，所以这里钉住它的输出形状。
TEST_F(RatingResolutionCommandTest, ConfirmableViewCarriesWhatAssessmentReads) {
    ASSERT_EQ(select_node(node_id_).status, ResolutionStatus::Ok);

    const auto stored = client_->execSqlSync(
        "select parsed_result_json::text as json from import_records where id=$1::uuid",
        import_id_)[0]["json"].as<std::string>();
    Json::Value draft;
    Json::CharReaderBuilder reader;
    std::string errors;
    const std::unique_ptr<Json::CharReader> parser(reader.newCharReader());
    ASSERT_TRUE(parser->parse(stored.data(), stored.data() + stored.size(), &draft, &errors))
        << errors;

    const auto view = bridge_report::resolution::build_confirmable_view(
        client_, import_id_, draft);

    ASSERT_TRUE(view["defects"].isArray());
    ASSERT_GT(view["defects"].size(), 0u);
    const auto& defect = view["defects"][0];
    EXPECT_FALSE(defect["bridge_component_id"].asString().empty());
    EXPECT_FALSE(defect["standard_component_category_id"].asString().empty());
    EXPECT_EQ(defect["rating_tree_node_id"].asString(), node_id_);
    // 来源身份要留着：预检靠它把展开出来的多条实例去重回一条报告行。
    EXPECT_FALSE(defect["source_candidate_id"].asString().empty());
}

// --- 按来源病害整体写（§22.6）-------------------------------------------
//
// 校对页一条来源病害显示一行，用户选一次节点，落到它的全部活动实例。逐实例接口逐条
// 发请求时，取草稿、装评定树、鉴权、查编辑锁、开事务、提交全部乘以实例数——区间展开
// 的病害是 25 次，实测 2 秒。

TEST_F(RatingResolutionCommandTest, SourceCommandWritesEveryInstance) {
    bind_to_both();
    const auto before = all_instances();
    ASSERT_EQ(before.size(), 2u);

    SourceRatingResolutionRequest request;
    request.context = context();
    request.source_candidate_id = kSourceCandidateId;
    request.rating_tree_node_id = node_id_;
    for (const auto& instance : before) {
        request.instances.push_back(
            {instance.instance_id, instance.has_rating ? instance.rating_version : 0});
    }
    const auto outcome =
        ImportResolutionService(client_).apply_source_rating_resolution(request);
    ASSERT_EQ(outcome.status, ResolutionStatus::Ok) << outcome.error_message;

    for (const auto& instance : all_instances()) {
        EXPECT_TRUE(instance.has_rating);
        EXPECT_EQ(instance.rating_status, "matched");
        EXPECT_EQ(instance.rating_tree_node_id.value_or(""), node_id_);
        EXPECT_EQ(instance.rating_match_method.value_or(""), "manual");
    }
}

// 全部实例同一事务：任一条版本过期就整批回滚。
//
// 写一半最难查——校对页那一行显示的是整条病害的结论，用户看到"已选择"，而实际上只有
// 一部分构件带着这个节点，直到正式入库评分才以扣分对不上暴露出来。
TEST_F(RatingResolutionCommandTest, SourceCommandRollsBackWhenOneVersionIsStale) {
    bind_to_both();
    const auto before = all_instances();
    ASSERT_EQ(before.size(), 2u);

    SourceRatingResolutionRequest request;
    request.context = context();
    request.source_candidate_id = kSourceCandidateId;
    request.rating_tree_node_id = node_id_;
    request.instances.push_back({before[0].instance_id, 0});
    // 第二条带一个过期版本。
    request.instances.push_back({before[1].instance_id, 99});

    const auto outcome =
        ImportResolutionService(client_).apply_source_rating_resolution(request);
    EXPECT_EQ(outcome.status, ResolutionStatus::VersionConflict);
    EXPECT_EQ(outcome.error_code, "resolution_version_conflict");

    // 第一条也不能落库。
    for (const auto& instance : all_instances()) {
        EXPECT_NE(instance.rating_match_method.value_or(""), "manual");
    }
}

// 逐实例接口（§13.2）保留，且与批量走同一份实现：单实例只是 instances 长度为 1。
TEST_F(RatingResolutionCommandTest, SingleInstanceEndpointStillWorks) {
    ASSERT_EQ(select_node(node_id_).status, ResolutionStatus::Ok);
    EXPECT_EQ(only_instance().rating_match_method.value_or(""), "manual");
}

TEST_F(RatingResolutionCommandTest, SourceCommandRejectsAnUnknownInstance) {
    bind_to_both();
    SourceRatingResolutionRequest request;
    request.context = context();
    request.source_candidate_id = kSourceCandidateId;
    request.rating_tree_node_id = node_id_;
    request.instances.push_back({all_instances()[0].instance_id, 0});
    request.instances.push_back({"11111111-1111-4111-8111-111111111111", 0});

    // 现在这条在集合校验就被拦下：一个不属于本病害活动实例的 id，无论存不存在都不该
    // 出现在这条命令里。
    const auto outcome =
        ImportResolutionService(client_).apply_source_rating_resolution(request);
    EXPECT_EQ(outcome.status, ResolutionStatus::Invalid);
    EXPECT_EQ(outcome.error_code, "invalid_resolution_request");
}

TEST_F(RatingResolutionCommandTest, SourceCommandClearsEveryInstance) {
    bind_to_both();
    {
        SourceRatingResolutionRequest select;
        select.context = context();
        select.source_candidate_id = kSourceCandidateId;
        select.rating_tree_node_id = node_id_;
        for (const auto& instance : all_instances()) {
            select.instances.push_back(
                {instance.instance_id, instance.has_rating ? instance.rating_version : 0});
        }
        ASSERT_EQ(ImportResolutionService(client_).apply_source_rating_resolution(select).status,
                  ResolutionStatus::Ok);
    }

    SourceRatingResolutionRequest clear;
    clear.context = context();
    clear.source_candidate_id = kSourceCandidateId;
    clear.rating_tree_node_id = "";
    for (const auto& instance : all_instances()) {
        clear.instances.push_back({instance.instance_id, instance.rating_version});
    }
    ASSERT_EQ(ImportResolutionService(client_).apply_source_rating_resolution(clear).status,
              ResolutionStatus::Ok);

    for (const auto& instance : all_instances()) {
        EXPECT_EQ(instance.rating_status, "unresolved");
        EXPECT_NE(instance.rating_match_method.value_or(""), "manual");
    }
}

// P1-1 回归：按来源病害整体写时，"整条病害是哪些实例"由服务端说了算。
//
// 只核对提交的 id 存不存在挡不住少提交。区间展开之后拿着旧页面提交，手里只有展开前
// 那一条实例，数量自洽、每条都存在，于是写完返回成功——而库里另外那些还停在未解析，
// 界面却显示整行已经选好。写一半正是这个接口存在的理由要排除的情形。

TEST_F(RatingResolutionCommandTest, SourceCommandRejectsAMissingActiveInstance) {
    bind_to_both();
    const auto before = all_instances();
    ASSERT_EQ(before.size(), 2u);

    SourceRatingResolutionRequest request;
    request.context = context();
    request.source_candidate_id = kSourceCandidateId;
    request.rating_tree_node_id = node_id_;
    // 只交一条：模拟展开前打开、展开后才提交的旧页面。
    request.instances.push_back({before[0].instance_id, 0});

    const auto outcome =
        ImportResolutionService(client_).apply_source_rating_resolution(request);
    EXPECT_EQ(outcome.status, ResolutionStatus::Conflict);
    EXPECT_EQ(outcome.error_code, "resolution_instance_set_stale");

    for (const auto& instance : all_instances()) {
        EXPECT_NE(instance.rating_match_method.value_or(""), "manual");
    }
}

TEST_F(RatingResolutionCommandTest, SourceCommandRejectsAnInstanceFromAnotherDefect) {
    bind_to_both();
    const auto mine = all_instances();
    ASSERT_EQ(mine.size(), 2u);
    // 另一条来源病害的实例：库里存在，也属于本次导入，但不属于这条病害。
    const auto stranger = client_->execSqlSync(
        "insert into import_component_group_members(import_record_id, group_id, "
        "  source_candidate_id, source_order) "
        "select $1::uuid, m.group_id, 'source_defect_other', 99 "
        "from import_component_group_members m where m.id=$2::uuid returning id::text",
        import_id_, only_member_id())[0]["id"].as<std::string>();
    const auto stranger_instance = client_->execSqlSync(
        "insert into import_resolved_defect_instances(group_member_id, target_id, "
        "  instance_order, instance_status, is_photo_owner, component_resolution_version) "
        "select $1::uuid, i.target_id, 1, 'active', true, i.component_resolution_version "
        "from import_resolved_defect_instances i where i.id=$2::uuid returning id::text",
        stranger, mine[0].instance_id)[0]["id"].as<std::string>();

    SourceRatingResolutionRequest request;
    request.context = context();
    request.source_candidate_id = kSourceCandidateId;
    request.rating_tree_node_id = node_id_;
    for (const auto& instance : mine) request.instances.push_back({instance.instance_id, 0});
    request.instances.push_back({stranger_instance, 0});

    const auto outcome =
        ImportResolutionService(client_).apply_source_rating_resolution(request);
    EXPECT_EQ(outcome.status, ResolutionStatus::Invalid);
    EXPECT_EQ(outcome.error_code, "invalid_resolution_request");
}

// 已忽略的实例不入库，也就不该被算进"整条病害"。它出现在请求里只可能是页面陈旧或
// 客户端算错，两种情况都不该照单写入。
TEST_F(RatingResolutionCommandTest, SourceCommandRejectsAnIgnoredInstance) {
    bind_to_both();
    const auto before = all_instances();
    ASSERT_EQ(before.size(), 2u);
    client_->execSqlSync(
        "update import_resolved_defect_instances set instance_status='ignored' "
        "where id=$1::uuid", before[1].instance_id);

    SourceRatingResolutionRequest request;
    request.context = context();
    request.source_candidate_id = kSourceCandidateId;
    request.rating_tree_node_id = node_id_;
    for (const auto& instance : before) request.instances.push_back({instance.instance_id, 0});

    const auto outcome =
        ImportResolutionService(client_).apply_source_rating_resolution(request);
    EXPECT_EQ(outcome.status, ResolutionStatus::Invalid);
    EXPECT_EQ(outcome.error_code, "invalid_resolution_request");
}

TEST_F(RatingResolutionCommandTest, SourceCommandRejectsADuplicateInstance) {
    bind_to_both();
    const auto before = all_instances();

    SourceRatingResolutionRequest request;
    request.context = context();
    request.source_candidate_id = kSourceCandidateId;
    request.rating_tree_node_id = node_id_;
    request.instances.push_back({before[0].instance_id, 0});
    request.instances.push_back({before[0].instance_id, 0});

    const auto outcome =
        ImportResolutionService(client_).apply_source_rating_resolution(request);
    EXPECT_EQ(outcome.status, ResolutionStatus::Invalid);
    EXPECT_EQ(outcome.error_code, "invalid_resolution_request");
}
