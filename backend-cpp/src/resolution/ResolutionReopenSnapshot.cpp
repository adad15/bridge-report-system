#include "bridge_report/resolution/ResolutionReopenSnapshot.hpp"

#include <string>

#include <json/json.h>

#include "bridge_report/auth/PasswordHash.hpp"

namespace bridge_report::resolution {
namespace {

// 快照按 id 排序聚合：校验和要能在两次捕获之间比较，行顺序不能靠数据库返回次序。
// to_jsonb(row) 连时间戳与 version 一起带走，还原时原样写回——版本号也被还原正是
// 计划必须显式作废的原因（§8.8）。
constexpr const char* kCaptureSql =
    "select jsonb_build_object("
    "  'version', 1,"
    "  'groups', coalesce((select jsonb_agg(to_jsonb(g) order by g.id) "
    "     from import_component_resolution_groups g "
    "     where g.import_record_id = $1::uuid), '[]'::jsonb),"
    "  'members', coalesce((select jsonb_agg(to_jsonb(m) order by m.id) "
    "     from import_component_group_members m "
    "     where m.import_record_id = $1::uuid), '[]'::jsonb),"
    "  'targets', coalesce((select jsonb_agg(to_jsonb(t) order by t.id) "
    "     from import_component_resolution_targets t "
    "     join import_component_resolution_groups g on g.id = t.group_id "
    "     where g.import_record_id = $1::uuid), '[]'::jsonb),"
    "  'instances', coalesce((select jsonb_agg(to_jsonb(i) order by i.id) "
    "     from import_resolved_defect_instances i "
    "     join import_component_group_members m on m.id = i.group_member_id "
    "     where m.import_record_id = $1::uuid), '[]'::jsonb),"
    "  'ratings', coalesce((select jsonb_agg(to_jsonb(r) order by r.resolved_defect_instance_id) "
    "     from import_rating_resolutions r "
    "     join import_resolved_defect_instances i on i.id = r.resolved_defect_instance_id "
    "     join import_component_group_members m on m.id = i.group_member_id "
    "     where m.import_record_id = $1::uuid), '[]'::jsonb)"
    ")::text as snapshot";

std::string compact_json(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

void append_snapshot_event(
    const ReopenSnapshotTransaction& tx,
    const std::string& import_record_id,
    const std::string& actor_user_id,
    const std::string& operation_type,
    const std::string& checksum,
    int invalidated_plan_count) {
    Json::Value after(Json::objectValue);
    after["checksum"] = checksum;
    after["invalidated_plan_count"] = invalidated_plan_count;
    tx->execSqlSync(
        "insert into import_resolution_events "
        "(import_record_id, operation_type, before_json, after_json, actor_user_id) "
        "values ($1::uuid, $2, '{}'::jsonb, $3::jsonb, nullif($4,'')::uuid)",
        import_record_id, operation_type, compact_json(after), actor_user_id);
}

}  // namespace

int invalidate_ready_plans(
    const ReopenSnapshotTransaction& tx,
    const std::string& import_record_id,
    const std::string& reason) {
    const auto rows = tx->execSqlSync(
        "update import_resolution_operation_plans "
        "set status = 'invalidated', invalidated_reason = $2 "
        "where import_record_id = $1::uuid and status = 'ready' "
        "returning id",
        import_record_id, reason);
    return static_cast<int>(rows.size());
}

ReopenSnapshotOutcome capture_reopen_snapshot(
    const ReopenSnapshotTransaction& tx,
    const std::string& import_record_id,
    const std::string& actor_user_id) {
    ReopenSnapshotOutcome outcome;
    const auto rows = tx->execSqlSync(kCaptureSql, import_record_id);
    if (rows.empty()) {
        outcome.error_code = "resolution_snapshot_capture_failed";
        outcome.error_message = "无法读取当前解析状态，重开未执行。";
        return outcome;
    }
    const auto snapshot = rows[0]["snapshot"].as<std::string>();
    outcome.checksum = "sha256:" + auth::sha256_hex(snapshot);

    // 每个重开态导入记录至多一条：重复重开覆盖上一份，而不是堆叠。
    tx->execSqlSync(
        "insert into import_resolution_reopen_snapshots "
        "(import_record_id, snapshot_json, checksum, created_by_user_id) "
        "values ($1::uuid, $2::jsonb, $3, nullif($4,'')::uuid) "
        "on conflict (import_record_id) do update "
        "set snapshot_json = excluded.snapshot_json, checksum = excluded.checksum, "
        "    created_by_user_id = excluded.created_by_user_id, created_at = now()",
        import_record_id, snapshot, outcome.checksum, actor_user_id);

    outcome.invalidated_plan_count =
        invalidate_ready_plans(tx, import_record_id, "import_record_reopened");
    append_snapshot_event(tx, import_record_id, actor_user_id, "reopen_snapshot_captured",
                          outcome.checksum, outcome.invalidated_plan_count);
    outcome.success = true;
    return outcome;
}

ReopenSnapshotOutcome restore_reopen_snapshot(
    const ReopenSnapshotTransaction& tx,
    const std::string& import_record_id,
    const std::string& actor_user_id) {
    ReopenSnapshotOutcome outcome;
    const auto stored = tx->execSqlSync(
        "select snapshot_json::text as snapshot, checksum "
        "from import_resolution_reopen_snapshots where import_record_id = $1::uuid",
        import_record_id);
    if (stored.empty()) {
        // 重开发生在本次改造之前的记录没有快照。此时来源 JSON 仍会照常还原，
        // 关系态维持现状——比整片清空强，且下一次重开就会补上快照。
        outcome.success = true;
        outcome.invalidated_plan_count =
            invalidate_ready_plans(tx, import_record_id, "reopen_snapshot_missing");
        return outcome;
    }
    const auto snapshot = stored[0]["snapshot"].as<std::string>();
    outcome.checksum = stored[0]["checksum"].as<std::string>();

    // 组是整棵树的根：成员、目标、实例、评分树解析都靠级联跟着走。
    tx->execSqlSync(
        "delete from import_component_resolution_groups where import_record_id = $1::uuid",
        import_record_id);

    // 顺序即依赖顺序。目标与实例之间那条可延迟约束在提交时才校验，因此中途出现
    // "组已有目标但实例还没回来"不是问题。
    tx->execSqlSync(
        "insert into import_component_resolution_groups "
        "select * from jsonb_populate_recordset("
        "  null::import_component_resolution_groups, $1::jsonb -> 'groups')",
        snapshot);
    tx->execSqlSync(
        "insert into import_component_group_members "
        "select * from jsonb_populate_recordset("
        "  null::import_component_group_members, $1::jsonb -> 'members')",
        snapshot);
    tx->execSqlSync(
        "insert into import_component_resolution_targets "
        "select * from jsonb_populate_recordset("
        "  null::import_component_resolution_targets, $1::jsonb -> 'targets')",
        snapshot);
    tx->execSqlSync(
        "insert into import_resolved_defect_instances "
        "select * from jsonb_populate_recordset("
        "  null::import_resolved_defect_instances, $1::jsonb -> 'instances')",
        snapshot);
    tx->execSqlSync(
        "insert into import_rating_resolutions "
        "select * from jsonb_populate_recordset("
        "  null::import_rating_resolutions, $1::jsonb -> 'ratings')",
        snapshot);

    tx->execSqlSync(
        "delete from import_resolution_reopen_snapshots where import_record_id = $1::uuid",
        import_record_id);

    outcome.invalidated_plan_count =
        invalidate_ready_plans(tx, import_record_id, "reopen_snapshot_restored");
    append_snapshot_event(tx, import_record_id, actor_user_id, "reopen_snapshot_restored",
                          outcome.checksum, outcome.invalidated_plan_count);
    outcome.success = true;
    return outcome;
}

ReopenSnapshotOutcome discard_reopen_snapshot(
    const ReopenSnapshotTransaction& tx,
    const std::string& import_record_id,
    const std::string& actor_user_id) {
    ReopenSnapshotOutcome outcome;
    const auto removed = tx->execSqlSync(
        "delete from import_resolution_reopen_snapshots where import_record_id = $1::uuid "
        "returning checksum",
        import_record_id);
    if (!removed.empty()) {
        outcome.checksum = removed[0]["checksum"].as<std::string>();
    }
    outcome.invalidated_plan_count =
        invalidate_ready_plans(tx, import_record_id, "import_record_reconfirmed");
    append_snapshot_event(tx, import_record_id, actor_user_id, "reopen_snapshot_discarded",
                          outcome.checksum, outcome.invalidated_plan_count);
    outcome.success = true;
    return outcome;
}

}  // namespace bridge_report::resolution
