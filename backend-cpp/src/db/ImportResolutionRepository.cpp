#include "bridge_report/db/ImportResolutionRepository.hpp"

#include <memory>
#include <utility>

#include <json/json.h>

namespace bridge_report::db {
namespace {

using resolution::ComponentGroupMember;
using resolution::ComponentResolutionGroup;
using resolution::ComponentResolutionTarget;
using resolution::RatingResolution;
using resolution::ResolutionEvent;
using resolution::ResolvedDefectInstance;

std::string compact_json(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

Json::Value parse_json_object(const std::string& text) {
    Json::CharReaderBuilder builder;
    Json::Value value;
    std::string errors;
    const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader->parse(text.data(), text.data() + text.size(), &value, &errors) ||
        !value.isObject()) {
        return Json::Value(Json::objectValue);
    }
    return value;
}

std::optional<std::string> optional_text(
    const drogon::orm::Row& row, const char* column) {
    if (row[column].isNull()) return std::nullopt;
    return row[column].as<std::string>();
}

// drogon 的参数绑定没有 optional 重载：空串 + nullif 是本仓库既有的写法
// （ReviewRepository 的 nullif($13,'') 同源），比到处铺重载省事且不易漏。
std::string bind_optional(const std::optional<std::string>& value) {
    return value.value_or(std::string{});
}

const char* group_columns() {
    return "id::text as id, import_record_id::text as import_record_id, "
           "source_component_name, source_component_number, "
           "normalized_component_number, resolution_mode, status, match_method, "
           "inventory_revision_id::text as inventory_revision_id, version, "
           "resolved_by_user_id::text as resolved_by_user_id, "
           "resolved_at::text as resolved_at";
}

ComponentResolutionGroup row_to_group(const drogon::orm::Row& row) {
    ComponentResolutionGroup group;
    group.id = row["id"].as<std::string>();
    group.import_record_id = row["import_record_id"].as<std::string>();
    group.source_component_name = row["source_component_name"].as<std::string>();
    group.source_component_number = optional_text(row, "source_component_number");
    group.normalized_component_number =
        row["normalized_component_number"].as<std::string>();
    group.resolution_mode = row["resolution_mode"].as<std::string>();
    group.status = row["status"].as<std::string>();
    group.match_method = optional_text(row, "match_method");
    group.inventory_revision_id = optional_text(row, "inventory_revision_id");
    group.version = row["version"].as<int>();
    group.resolved_by_user_id = optional_text(row, "resolved_by_user_id");
    group.resolved_at = optional_text(row, "resolved_at");
    return group;
}

const char* member_columns() {
    return "id::text as id, import_record_id::text as import_record_id, "
           "group_id::text as group_id, source_candidate_id, source_order";
}

ComponentGroupMember row_to_member(const drogon::orm::Row& row) {
    ComponentGroupMember member;
    member.id = row["id"].as<std::string>();
    member.import_record_id = row["import_record_id"].as<std::string>();
    member.group_id = row["group_id"].as<std::string>();
    member.source_candidate_id = row["source_candidate_id"].as<std::string>();
    member.source_order = row["source_order"].as<int>();
    return member;
}

const char* target_columns() {
    return "id::text as id, group_id::text as group_id, "
           "bridge_component_id::text as bridge_component_id, target_order, target_role";
}

// 与组表 join 时 id 两边都有，必须带前缀消歧。
const char* joined_target_columns() {
    return "t.id::text as id, t.group_id::text as group_id, "
           "t.bridge_component_id::text as bridge_component_id, t.target_order, t.target_role";
}

ComponentResolutionTarget row_to_target(const drogon::orm::Row& row) {
    ComponentResolutionTarget target;
    target.id = row["id"].as<std::string>();
    target.group_id = row["group_id"].as<std::string>();
    target.bridge_component_id = row["bridge_component_id"].as<std::string>();
    target.target_order = row["target_order"].as<int>();
    target.target_role = row["target_role"].as<std::string>();
    return target;
}

// 列表查询要 join 成员表拿导入范围和顺序，所以带 i. 前缀；单表 insert 的 returning
// 里前缀不合法，只能另留一份不带前缀的。两份必须同时改。
const char* instance_columns() {
    return "i.id::text as id, i.group_member_id::text as group_member_id, "
           "i.target_id::text as target_id, i.instance_order, i.instance_status, "
           "i.is_photo_owner, i.fact_overrides_json::text as fact_overrides_json, "
           "i.component_resolution_version, i.version";
}

const char* instance_returning_columns() {
    return "id::text as id, group_member_id::text as group_member_id, "
           "target_id::text as target_id, instance_order, instance_status, "
           "is_photo_owner, fact_overrides_json::text as fact_overrides_json, "
           "component_resolution_version, version";
}

ResolvedDefectInstance row_to_instance(const drogon::orm::Row& row) {
    ResolvedDefectInstance instance;
    instance.id = row["id"].as<std::string>();
    instance.group_member_id = row["group_member_id"].as<std::string>();
    instance.target_id = row["target_id"].as<std::string>();
    instance.instance_order = row["instance_order"].as<int>();
    instance.instance_status = row["instance_status"].as<std::string>();
    instance.is_photo_owner = row["is_photo_owner"].as<bool>();
    instance.fact_overrides_json =
        parse_json_object(row["fact_overrides_json"].as<std::string>());
    instance.component_resolution_version =
        row["component_resolution_version"].as<int>();
    instance.version = row["version"].as<int>();
    return instance;
}

const char* rating_columns() {
    return "r.resolved_defect_instance_id::text as resolved_defect_instance_id, "
           "r.rating_tree_version_id::text as rating_tree_version_id, "
           "r.rating_tree_node_id::text as rating_tree_node_id, "
           "r.standard_defect_indicator_id, r.status, r.match_method, "
           "r.match_evidence_json::text as match_evidence_json, "
           "r.component_resolution_version, r.applicability_hash, r.match_input_hash, "
           "r.resolved_match_input_hash, "
           "r.version, r.resolved_by_user_id::text as resolved_by_user_id, "
           "r.resolved_at::text as resolved_at";
}

RatingResolution row_to_rating(const drogon::orm::Row& row) {
    RatingResolution resolution;
    resolution.resolved_defect_instance_id =
        row["resolved_defect_instance_id"].as<std::string>();
    resolution.rating_tree_version_id = row["rating_tree_version_id"].as<std::string>();
    resolution.rating_tree_node_id = optional_text(row, "rating_tree_node_id");
    resolution.standard_defect_indicator_id =
        optional_text(row, "standard_defect_indicator_id");
    resolution.status = row["status"].as<std::string>();
    resolution.match_method = optional_text(row, "match_method");
    resolution.match_evidence_json =
        parse_json_object(row["match_evidence_json"].as<std::string>());
    resolution.component_resolution_version =
        row["component_resolution_version"].as<int>();
    resolution.applicability_hash = row["applicability_hash"].as<std::string>();
    resolution.match_input_hash = row["match_input_hash"].as<std::string>();
    if (!row["resolved_match_input_hash"].isNull()) {
        resolution.resolved_match_input_hash =
            row["resolved_match_input_hash"].as<std::string>();
    }
    resolution.version = row["version"].as<int>();
    resolution.resolved_by_user_id = optional_text(row, "resolved_by_user_id");
    resolution.resolved_at = optional_text(row, "resolved_at");
    return resolution;
}

}  // namespace

ImportResolutionRepository::ImportResolutionRepository(
    drogon::orm::DbClientPtr db_client)
    : db_client_(std::move(db_client)) {}

ComponentResolutionGroup ImportResolutionRepository::insert_group(
    const ComponentResolutionGroup& group) const {
    const auto result = db_client_->execSqlSync(
        std::string(
            "insert into import_component_resolution_groups "
            "(import_record_id, source_component_name, source_component_number, "
            " normalized_component_number, resolution_mode, status, match_method, "
            " inventory_revision_id, resolved_by_user_id) "
            "values ($1::uuid, $2, nullif($3,''), $4, $5, $6, nullif($7,''), "
            "        nullif($8,'')::uuid, nullif($9,'')::uuid) "
            "returning ") + group_columns(),
        group.import_record_id,
        group.source_component_name,
        bind_optional(group.source_component_number),
        group.normalized_component_number,
        group.resolution_mode,
        group.status,
        bind_optional(group.match_method),
        bind_optional(group.inventory_revision_id),
        bind_optional(group.resolved_by_user_id));
    return row_to_group(result[0]);
}

std::vector<ComponentResolutionGroup> ImportResolutionRepository::list_groups(
    const std::string& import_record_id) const {
    const auto rows = db_client_->execSqlSync(
        std::string("select ") + group_columns() +
            " from import_component_resolution_groups where import_record_id = $1::uuid "
            "order by source_component_name, normalized_component_number",
        import_record_id);
    std::vector<ComponentResolutionGroup> groups;
    groups.reserve(rows.size());
    for (const auto& row : rows) groups.push_back(row_to_group(row));
    return groups;
}

std::optional<ComponentResolutionGroup> ImportResolutionRepository::find_group(
    const std::string& group_id) const {
    const auto rows = db_client_->execSqlSync(
        std::string("select ") + group_columns() +
            " from import_component_resolution_groups where id = $1::uuid",
        group_id);
    if (rows.empty()) return std::nullopt;
    return row_to_group(rows[0]);
}

std::optional<int> ImportResolutionRepository::update_group_resolution(
    const std::string& group_id,
    const int expected_version,
    const std::string& status,
    const std::optional<std::string>& match_method,
    const std::optional<std::string>& inventory_revision_id,
    const std::string& resolution_mode,
    const std::optional<std::string>& resolved_by_user_id) const {
    // 条件写而不是"先读再写"：两个标签页同时提交时，后一条必须落空而不是覆盖。
    const auto rows = db_client_->execSqlSync(
        "update import_component_resolution_groups set "
        "  status = $3, match_method = nullif($4,''), "
        "  inventory_revision_id = nullif($5,'')::uuid, resolution_mode = $6, "
        "  resolved_by_user_id = nullif($7,'')::uuid, "
        "  resolved_at = case when nullif($7,'') is null then resolved_at else now() end, "
        "  version = version + 1, updated_at = now() "
        "where id = $1::uuid and version = $2 returning version",
        group_id,
        expected_version,
        status,
        bind_optional(match_method),
        bind_optional(inventory_revision_id),
        resolution_mode,
        bind_optional(resolved_by_user_id));
    if (rows.empty()) return std::nullopt;
    return rows[0]["version"].as<int>();
}

ComponentGroupMember ImportResolutionRepository::insert_member(
    const ComponentGroupMember& member) const {
    const auto result = db_client_->execSqlSync(
        std::string(
            "insert into import_component_group_members "
            "(import_record_id, group_id, source_candidate_id, source_order) "
            "values ($1::uuid, $2::uuid, $3, $4) returning ") + member_columns(),
        member.import_record_id,
        member.group_id,
        member.source_candidate_id,
        member.source_order);
    return row_to_member(result[0]);
}

std::vector<ComponentGroupMember> ImportResolutionRepository::list_members(
    const std::string& import_record_id) const {
    const auto rows = db_client_->execSqlSync(
        std::string("select ") + member_columns() +
            " from import_component_group_members where import_record_id = $1::uuid "
            "order by source_order",
        import_record_id);
    std::vector<ComponentGroupMember> members;
    members.reserve(rows.size());
    for (const auto& row : rows) members.push_back(row_to_member(row));
    return members;
}

void ImportResolutionRepository::delete_member(const std::string& member_id) const {
    db_client_->execSqlSync(
        "delete from import_component_group_members where id = $1::uuid", member_id);
}

int ImportResolutionRepository::delete_empty_groups(
    const std::string& import_record_id) const {
    const auto rows = db_client_->execSqlSync(
        "delete from import_component_resolution_groups g "
        "where g.import_record_id = $1::uuid and not exists ("
        "  select 1 from import_component_group_members m where m.group_id = g.id) "
        "returning g.id",
        import_record_id);
    return static_cast<int>(rows.size());
}

ComponentResolutionTarget ImportResolutionRepository::insert_target(
    const ComponentResolutionTarget& target) const {
    const auto result = db_client_->execSqlSync(
        std::string(
            "insert into import_component_resolution_targets "
            "(group_id, bridge_component_id, target_order, target_role) "
            "values ($1::uuid, $2::uuid, $3, $4) returning ") + target_columns(),
        target.group_id,
        target.bridge_component_id,
        target.target_order,
        target.target_role);
    return row_to_target(result[0]);
}

std::vector<ComponentResolutionTarget> ImportResolutionRepository::list_targets(
    const std::string& group_id) const {
    const auto rows = db_client_->execSqlSync(
        std::string("select ") + target_columns() +
            " from import_component_resolution_targets where group_id = $1::uuid "
            "order by target_order",
        group_id);
    std::vector<ComponentResolutionTarget> targets;
    targets.reserve(rows.size());
    for (const auto& row : rows) targets.push_back(row_to_target(row));
    return targets;
}

std::vector<ComponentResolutionTarget>
ImportResolutionRepository::list_targets_by_import(
    const std::string& import_record_id) const {
    const auto rows = db_client_->execSqlSync(
        std::string("select ") + joined_target_columns() +
            " from import_component_resolution_targets t "
            "join import_component_resolution_groups g on g.id = t.group_id "
            "where g.import_record_id = $1::uuid "
            "order by g.source_component_name, g.normalized_component_number, t.target_order",
        import_record_id);
    std::vector<ComponentResolutionTarget> targets;
    targets.reserve(rows.size());
    for (const auto& row : rows) targets.push_back(row_to_target(row));
    return targets;
}

void ImportResolutionRepository::delete_targets(const std::string& group_id) const {
    db_client_->execSqlSync(
        "delete from import_component_resolution_targets where group_id = $1::uuid",
        group_id);
}

ResolvedDefectInstance ImportResolutionRepository::insert_instance(
    const ResolvedDefectInstance& instance) const {
    const auto result = db_client_->execSqlSync(
        std::string(
            "insert into import_resolved_defect_instances "
            "(group_member_id, target_id, instance_order, instance_status, "
            " is_photo_owner, fact_overrides_json, component_resolution_version) "
            "values ($1::uuid, $2::uuid, $3, $4, $5, $6::jsonb, $7) "
            "returning ") + instance_returning_columns(),
        instance.group_member_id,
        instance.target_id,
        instance.instance_order,
        instance.instance_status,
        instance.is_photo_owner,
        compact_json(instance.fact_overrides_json),
        instance.component_resolution_version);
    return row_to_instance(result[0]);
}

std::vector<ResolvedDefectInstance>
ImportResolutionRepository::list_instances_by_import(
    const std::string& import_record_id) const {
    const auto rows = db_client_->execSqlSync(
        std::string("select ") + instance_columns() +
            " from import_resolved_defect_instances i "
            "join import_component_group_members m on m.id = i.group_member_id "
            "where m.import_record_id = $1::uuid "
            "order by m.source_order, i.instance_order",
        import_record_id);
    std::vector<ResolvedDefectInstance> instances;
    instances.reserve(rows.size());
    for (const auto& row : rows) instances.push_back(row_to_instance(row));
    return instances;
}

std::vector<ResolvedDefectInstance>
ImportResolutionRepository::list_instances_by_member(
    const std::string& group_member_id) const {
    const auto rows = db_client_->execSqlSync(
        std::string("select ") + instance_columns() +
            " from import_resolved_defect_instances i "
            "where i.group_member_id = $1::uuid order by i.instance_order",
        group_member_id);
    std::vector<ResolvedDefectInstance> instances;
    instances.reserve(rows.size());
    for (const auto& row : rows) instances.push_back(row_to_instance(row));
    return instances;
}

void ImportResolutionRepository::delete_instance(
    const std::string& instance_id) const {
    db_client_->execSqlSync(
        "delete from import_resolved_defect_instances where id = $1::uuid", instance_id);
}

void ImportResolutionRepository::restamp_instance(
    const std::string& instance_id,
    const int instance_order,
    const int component_resolution_version) const {
    db_client_->execSqlSync(
        "update import_resolved_defect_instances set instance_order = $2, "
        "  component_resolution_version = $3, updated_at = now() "
        "where id = $1::uuid",
        instance_id, instance_order, component_resolution_version);
}

std::optional<int> ImportResolutionRepository::set_instance_status(
    const std::string& instance_id,
    const int expected_version,
    const std::string& instance_status) const {
    // 转 ignored 的同时清掉照片归属：CHECK 不允许"已忽略却仍持有照片"，分两句写会在
    // 中间那一刻被挡下来。归属者归谁由随后的 recompute_photo_owner 决定。
    const auto rows = db_client_->execSqlSync(
        "update import_resolved_defect_instances set instance_status = $3, "
        "  is_photo_owner = case when $3 = 'ignored' then false else is_photo_owner end, "
        "  version = version + 1, updated_at = now() "
        "where id = $1::uuid and version = $2 "
        "returning version, group_member_id::text as group_member_id",
        instance_id, expected_version, instance_status);
    if (rows.empty()) return std::nullopt;
    recompute_photo_owner(rows[0]["group_member_id"].as<std::string>());
    return rows[0]["version"].as<int>();
}

void ImportResolutionRepository::recompute_photo_owner(
    const std::string& group_member_id) const {
    // 先清零再点名，两句都在调用方的事务里：反过来做会撞上"至多一个归属者"的
    // 部分唯一索引。
    db_client_->execSqlSync(
        "update import_resolved_defect_instances set is_photo_owner = false, "
        "  updated_at = now() "
        "where group_member_id = $1::uuid and is_photo_owner",
        group_member_id);
    db_client_->execSqlSync(
        "update import_resolved_defect_instances set is_photo_owner = true, "
        "  updated_at = now() "
        "where id = ("
        "  select id from import_resolved_defect_instances "
        "  where group_member_id = $1::uuid and instance_status = 'active' "
        "  order by instance_order limit 1)",
        group_member_id);
}

void ImportResolutionRepository::upsert_rating_resolution(
    const RatingResolution& resolution) const {
    db_client_->execSqlSync(
        "insert into import_rating_resolutions "
        "(resolved_defect_instance_id, rating_tree_version_id, rating_tree_node_id, "
        " standard_defect_indicator_id, status, match_method, match_evidence_json, "
        " component_resolution_version, applicability_hash, match_input_hash, "
        " resolved_match_input_hash, resolved_by_user_id, resolved_at) "
        "values ($1::uuid, $2::uuid, nullif($3,'')::uuid, nullif($4,''), $5, "
        "        nullif($6,''), $7::jsonb, $8, $9, $10, nullif($12,''), "
        "        nullif($11,'')::uuid, "
        "        case when nullif($11,'') is null then null else now() end) "
        "on conflict (resolved_defect_instance_id) do update set "
        "  rating_tree_version_id = excluded.rating_tree_version_id, "
        "  rating_tree_node_id = excluded.rating_tree_node_id, "
        "  standard_defect_indicator_id = excluded.standard_defect_indicator_id, "
        "  status = excluded.status, match_method = excluded.match_method, "
        "  match_evidence_json = excluded.match_evidence_json, "
        "  component_resolution_version = excluded.component_resolution_version, "
        "  applicability_hash = excluded.applicability_hash, "
        "  match_input_hash = excluded.match_input_hash, "
        "  resolved_match_input_hash = excluded.resolved_match_input_hash, "
        "  resolved_by_user_id = excluded.resolved_by_user_id, "
        "  resolved_at = excluded.resolved_at, "
        "  version = import_rating_resolutions.version + 1, updated_at = now()",
        resolution.resolved_defect_instance_id,
        resolution.rating_tree_version_id,
        bind_optional(resolution.rating_tree_node_id),
        bind_optional(resolution.standard_defect_indicator_id),
        resolution.status,
        bind_optional(resolution.match_method),
        compact_json(resolution.match_evidence_json),
        resolution.component_resolution_version,
        resolution.applicability_hash,
        resolution.match_input_hash,
        bind_optional(resolution.resolved_by_user_id),
        bind_optional(resolution.resolved_match_input_hash));
}

std::vector<RatingResolution>
ImportResolutionRepository::list_rating_resolutions_by_import(
    const std::string& import_record_id) const {
    const auto rows = db_client_->execSqlSync(
        std::string("select ") + rating_columns() +
            " from import_rating_resolutions r "
            "join import_resolved_defect_instances i "
            "  on i.id = r.resolved_defect_instance_id "
            "join import_component_group_members m on m.id = i.group_member_id "
            "where m.import_record_id = $1::uuid "
            "order by m.source_order, i.instance_order",
        import_record_id);
    std::vector<RatingResolution> resolutions;
    resolutions.reserve(rows.size());
    for (const auto& row : rows) resolutions.push_back(row_to_rating(row));
    return resolutions;
}

std::optional<RatingResolution> ImportResolutionRepository::find_rating_resolution(
    const std::string& instance_id) const {
    const auto rows = db_client_->execSqlSync(
        std::string("select ") + rating_columns() +
            " from import_rating_resolutions r "
            "where r.resolved_defect_instance_id = $1::uuid",
        instance_id);
    if (rows.empty()) return std::nullopt;
    return row_to_rating(rows[0]);
}

void ImportResolutionRepository::delete_rating_resolution(
    const std::string& instance_id) const {
    db_client_->execSqlSync(
        "delete from import_rating_resolutions "
        "where resolved_defect_instance_id = $1::uuid",
        instance_id);
}

void ImportResolutionRepository::append_event(const ResolutionEvent& event) const {
    db_client_->execSqlSync(
        // id 快照与外键同时写：外键在实体被删时会置空（迁移 028），
        // 而重绑定、删候选、重开恢复都会删那些行。只靠外键的话，历史行虽然留下了，
        // 却再也看不出当初说的是哪个对象。
        "insert into import_resolution_events "
        "(import_record_id, group_id, resolved_defect_instance_id, plan_id, "
        " operation_type, before_json, after_json, actor_user_id, "
        " group_id_snapshot, resolved_defect_instance_id_snapshot) "
        "values ($1::uuid, nullif($2,'')::uuid, nullif($3,'')::uuid, "
        "        nullif($4,'')::uuid, $5, $6::jsonb, $7::jsonb, nullif($8,'')::uuid, "
        "        nullif($2,'')::uuid, nullif($3,'')::uuid)",
        event.import_record_id,
        bind_optional(event.group_id),
        bind_optional(event.resolved_defect_instance_id),
        bind_optional(event.plan_id),
        event.operation_type,
        compact_json(event.before_json),
        compact_json(event.after_json),
        bind_optional(event.actor_user_id));
}

std::optional<int> ImportResolutionRepository::read_draft_version(
    const std::string& import_record_id) const {
    const auto rows = db_client_->execSqlSync(
        "select draft_version from import_records where id = $1::uuid",
        import_record_id);
    if (rows.empty()) return std::nullopt;
    return rows[0]["draft_version"].as<int>();
}

std::optional<int> ImportResolutionRepository::bump_draft_version(
    const std::string& import_record_id, const int expected_version) const {
    const auto rows = db_client_->execSqlSync(
        "update import_records set draft_version = draft_version + 1, updated_at = now() "
        "where id = $1::uuid and draft_version = $2 returning draft_version",
        import_record_id, expected_version);
    if (rows.empty()) return std::nullopt;
    return rows[0]["draft_version"].as<int>();
}

}  // namespace bridge_report::db
