#include "bridge_report/db/WorkspaceRepository.hpp"

#include <memory>
#include <sstream>
#include <utility>

#include <json/json.h>
#include <trantor/utils/Logger.h>

#include "bridge_report/archive/TemporaryWordStorage.hpp"
#include "bridge_report/archive/WordInputArchive.hpp"
#include "bridge_report/db/CommitLatch.hpp"
#include "bridge_report/db/ComponentArchiveRepository.hpp"
#include "bridge_report/review/ReviewStatistics.hpp"

namespace bridge_report::db {
namespace {

std::optional<std::string> optional_text(const drogon::orm::Row& row, const char* column) {
    if (row[column].isNull()) return std::nullopt;
    return row[column].as<std::string>();
}

std::optional<double> optional_double(const drogon::orm::Row& row, const char* column) {
    if (row[column].isNull()) return std::nullopt;
    return row[column].as<double>();
}

review::WorkspaceBridge bridge_from_row(const drogon::orm::Row& row) {
    review::WorkspaceBridge bridge;
    bridge.id = row["bridge_id"].as<std::string>();
    bridge.system_number = row["bridge_system_number"].as<std::string>();
    bridge.bridge_name = row["bridge_name"].as<std::string>();
    bridge.route_name = optional_text(row, "route_name");
    bridge.status = row["bridge_status"].as<std::string>();
    return bridge;
}

review::WorkspaceInspection inspection_from_row(const drogon::orm::Row& row) {
    review::WorkspaceInspection inspection;
    inspection.id = row["inspection_id"].as<std::string>();
    inspection.system_number = row["inspection_system_number"].as<std::string>();
    inspection.inspection_year = row["inspection_year"].as<int>();
    inspection.status = row["inspection_status"].as<std::string>();
    inspection.version_number = row["version_number"].as<int>();
    inspection.is_current = row["is_current"].as<bool>();
    inspection.overall_score = optional_double(row, "overall_score");
    inspection.overall_grade = optional_text(row, "overall_grade");
    inspection.created_at = optional_text(row, "inspection_created_at");
    inspection.updated_at = optional_text(row, "inspection_updated_at");
    return inspection;
}

review::WorkspaceStandardPackage standard_package_from_row(
    const drogon::orm::Row& row,
    const char* prefix,
    const char* family) {
    review::WorkspaceStandardPackage package;
    package.id = row[std::string(prefix) + "_package_id"].as<std::string>();
    package.family = family;
    package.standard_code = row[std::string(prefix) + "_standard_code"].as<std::string>();
    package.standard_name = row[std::string(prefix) + "_standard_name"].as<std::string>();
    package.official_edition = row[std::string(prefix) + "_official_edition"].as<std::string>();
    package.package_version = row[std::string(prefix) + "_package_version"].as<std::string>();
    package.is_enabled = row[std::string(prefix) + "_is_enabled"].as<bool>();
    package.sync_status = row[std::string(prefix) + "_sync_status"].as<std::string>();
    return package;
}

Json::Value parse_json_or_empty(const std::string& text) {
    Json::CharReaderBuilder builder;
    Json::Value value;
    std::string errors;
    std::istringstream input(text);
    if (!Json::parseFromStream(builder, input, &value, &errors) || !value.isObject()) {
        return Json::Value(Json::objectValue);
    }
    return value;
}

int pending_import_count(const drogon::orm::DbClientPtr& client, const std::string& bridge_id) {
    const auto rows = client->execSqlSync(
        "select count(*)::int as count from import_records ir "
        "left join inspection_years iy on iy.id = ir.inspection_year_id "
        "where ir.bridge_id = $1::uuid and ir.import_status in ('已上传','解析中','待校对','解析失败') "
        "and (ir.inspection_year_id is null or iy.is_current)", bridge_id);
    return rows[0]["count"].as<int>();
}

/// 当前有效且已确认的最近两个年度。少于两个就没有可比对象。
constexpr const char* kRecentConfirmedYearsSql =
    "select id::text as id, inspection_year from inspection_years "
    "where bridge_id = $1::uuid and is_current and status = '已确认' "
    "order by inspection_year desc limit 2";


/// 逐构件两年条数：只用来算"多少构件有变化、增减各多少条"这类汇总，不直接出现在界面上。
constexpr const char* kComponentDefectDeltaSql =
    "select o.bridge_component_id::text as bridge_component_id, "
    "count(*) filter (where o.inspection_year_id = $2::uuid) as previous_count, "
    "count(*) filter (where o.inspection_year_id = $1::uuid) as latest_count "
    "from defect_observations o "
    "where o.inspection_year_id in ($1::uuid, $2::uuid) "
    "and o.review_status in ('已确认', '已修改') "
    "group by o.bridge_component_id";

/// 按「结构分部 + 构件类型」汇总的两年条数与构件数。
constexpr const char* kDefectGroupSql =
    "with per_component as ("
    "  select bc.structure_part, bc.component_type, o.bridge_component_id, "
    "         count(*) filter (where o.inspection_year_id = $2::uuid) as previous_count, "
    "         count(*) filter (where o.inspection_year_id = $1::uuid) as latest_count "
    "  from defect_observations o "
    "  join bridge_components bc on bc.id = o.bridge_component_id "
    "  where o.inspection_year_id in ($1::uuid, $2::uuid) "
    "  and o.review_status in ('已确认', '已修改') "
    "  group by bc.structure_part, bc.component_type, o.bridge_component_id"
    ") "
    "select structure_part, component_type, "
    "sum(previous_count)::int as previous_count, sum(latest_count)::int as latest_count, "
    "count(*)::int as component_count, "
    "count(*) filter (where previous_count <> latest_count)::int as changed_component_count "
    "from per_component group by structure_part, component_type "
    "order by sum(latest_count) desc, structure_part, component_type";

/// 每个类型分组下的病害类型构成。文字描述要写"横向裂缝 2 条"，光有合计写不出来。
constexpr const char* kDefectGroupTypeSql =
    "select bc.structure_part, bc.component_type, o.defect_type, "
    "count(*) filter (where o.inspection_year_id = $2::uuid) as previous_count, "
    "count(*) filter (where o.inspection_year_id = $1::uuid) as latest_count "
    "from defect_observations o "
    "join bridge_components bc on bc.id = o.bridge_component_id "
    "where o.inspection_year_id in ($1::uuid, $2::uuid) "
    "and o.review_status in ('已确认', '已修改') "
    "group by bc.structure_part, bc.component_type, o.defect_type "
    "order by bc.structure_part, bc.component_type, "
    "count(*) filter (where o.inspection_year_id = $1::uuid) desc, o.defect_type";

/// 最新年度与上一年度的病害对比，按构件类型汇总。
review::WorkspaceDefectComparison load_defect_comparison(
    const drogon::orm::DbClientPtr& db_client, const std::string& bridge_id) {
    review::WorkspaceDefectComparison comparison;
    const auto years = db_client->execSqlSync(kRecentConfirmedYearsSql, bridge_id);
    if (years.size() < 2) return comparison;

    const auto latest_id = years[0]["id"].as<std::string>();
    const auto previous_id = years[1]["id"].as<std::string>();
    comparison.available = true;
    comparison.latest_year = years[0]["inspection_year"].as<int>();
    comparison.previous_year = years[1]["inspection_year"].as<int>();

    // 增减在**构件**这一层统计：按类型统计会让同类型里"这个多两条、那个少两条"互相抵消，
    // 报出来就成了"没有变化"。
    for (const auto& row : db_client->execSqlSync(kComponentDefectDeltaSql, latest_id, previous_id)) {
        const auto change = row["latest_count"].as<int>() - row["previous_count"].as<int>();
        if (change == 0) {
            ++comparison.unchanged_component_count;
            continue;
        }
        ++comparison.changed_component_count;
        if (change > 0) comparison.increased_observation_count += change;
        else comparison.decreased_observation_count += -change;
    }

    for (const auto& row : db_client->execSqlSync(kDefectGroupSql, latest_id, previous_id)) {
        review::WorkspaceDefectGroupDelta group;
        group.structure_part = row["structure_part"].as<std::string>();
        group.component_type = row["component_type"].as<std::string>();
        group.previous_count = row["previous_count"].as<int>();
        group.latest_count = row["latest_count"].as<int>();
        group.component_count = row["component_count"].as<int>();
        group.changed_component_count = row["changed_component_count"].as<int>();
        comparison.previous_observation_count += group.previous_count;
        comparison.latest_observation_count += group.latest_count;
        comparison.groups.push_back(std::move(group));
    }

    // 类型构成单独取一遍再挂回去：和分组汇总合成一条语句会让每个分组重复多行，
    // 汇总数字得在应用层去重，反而更容易错。
    std::map<std::pair<std::string, std::string>,
             std::vector<review::WorkspaceDefectTypeDelta>> types_by_group;
    for (const auto& row : db_client->execSqlSync(kDefectGroupTypeSql, latest_id, previous_id)) {
        review::WorkspaceDefectTypeDelta type_delta;
        type_delta.defect_type = row["defect_type"].as<std::string>();
        type_delta.previous_count = row["previous_count"].as<int>();
        type_delta.latest_count = row["latest_count"].as<int>();
        types_by_group[{row["structure_part"].as<std::string>(),
                        row["component_type"].as<std::string>()}].push_back(std::move(type_delta));
    }
    for (auto& group : comparison.groups) {
        const auto found = types_by_group.find({group.structure_part, group.component_type});
        if (found != types_by_group.end()) group.defect_types = std::move(found->second);
    }
    return comparison;
}

}  // namespace

WorkspaceRepository::WorkspaceRepository(drogon::orm::DbClientPtr db_client) : db_client_(std::move(db_client)) {}

std::optional<review::BridgeOverview> WorkspaceRepository::get_bridge_overview(const std::string& bridge_id) {
    const auto bridge_rows = db_client_->execSqlSync(
        "select id::text as bridge_id, system_number as bridge_system_number, bridge_name, route_name, "
        "status as bridge_status from bridges where id = $1::uuid", bridge_id);
    if (bridge_rows.empty()) return std::nullopt;

    review::BridgeOverview overview;
    overview.bridge = bridge_from_row(bridge_rows[0]);

    const auto inspection_rows = db_client_->execSqlSync(
        "select id::text as inspection_id, system_number as inspection_system_number, inspection_year, "
        "status as inspection_status, version_number, is_current, overall_score, overall_grade, "
        "created_at::text as inspection_created_at, updated_at::text as inspection_updated_at "
        "from inspection_years where bridge_id = $1::uuid and is_current "
        "and status in ('已确认','已归档') order by inspection_year desc limit 5", bridge_id);
    for (const auto& row : inspection_rows) {
        overview.recent_inspections.push_back(inspection_from_row(row));
    }
    if (!overview.recent_inspections.empty()) {
        overview.latest_inspection = overview.recent_inspections.front();
        const auto rating_rows = db_client_->execSqlSync(
            "select rating_level, rating_item_name, score, grade from condition_ratings "
            "where inspection_year_id = $1::uuid and assessment_run_id is not null "
            "and rating_level in ('结构分部','部件') "
            "and review_status in ('已确认','已修改') order by rating_level, rating_item_name",
            overview.latest_inspection->id);
        for (const auto& row : rating_rows) {
            review::WorkspaceStructureRating rating;
            rating.rating_level = row["rating_level"].as<std::string>();
            rating.rating_item_name = row["rating_item_name"].as<std::string>();
            rating.score = optional_double(row, "score");
            rating.grade = optional_text(row, "grade");
            overview.structure_ratings.push_back(std::move(rating));
        }
    }

    ComponentArchiveRepository archive_repository(db_client_);
    const auto component_body = archive_repository.list_components(bridge_id);
    const auto& components = component_body["components"];
    overview.defect_archive.component_count = components.isArray() ? static_cast<int>(components.size()) : 0;
    if (components.isArray()) {
        for (const auto& component : components) {
            overview.defect_archive.thread_count += component["thread_count"].asInt();
            overview.defect_archive.unbound_observation_count += component["unbound_count"].asInt();
        }
    }
    overview.defect_comparison = load_defect_comparison(db_client_, bridge_id);
    overview.pending.import_count = pending_import_count(db_client_, bridge_id);
    overview.pending.unbound_observation_count = overview.defect_archive.unbound_observation_count;
    return overview;
}

std::optional<review::InspectionWorkspace> WorkspaceRepository::get_inspection_workspace(
    const std::string& inspection_year_id
) {
    const auto context_rows = db_client_->execSqlSync(
        "select b.id::text as bridge_id, b.system_number as bridge_system_number, b.bridge_name, b.route_name, "
        "b.status as bridge_status, iy.id::text as inspection_id, "
        "iy.system_number as inspection_system_number, iy.inspection_year, iy.status as inspection_status, "
        "iy.version_number, iy.is_current, iy.overall_score, iy.overall_grade, "
        "iy.created_at::text as inspection_created_at, iy.updated_at::text as inspection_updated_at, "
        "sp.id::text as standard_profile_id, sp.revision_number as standard_profile_revision, "
        "sp.status as standard_profile_status, "
        "tp.id::text as technical_package_id, tp.standard_code as technical_standard_code, "
        "tp.standard_name as technical_standard_name, tp.official_edition as technical_official_edition, "
        "tp.package_version as technical_package_version, tp.is_enabled as technical_is_enabled, "
        "tp.sync_status as technical_sync_status, "
        "mp.id::text as maintenance_package_id, mp.standard_code as maintenance_standard_code, "
        "mp.standard_name as maintenance_standard_name, mp.official_edition as maintenance_official_edition, "
        "mp.package_version as maintenance_package_version, mp.is_enabled as maintenance_is_enabled, "
        "mp.sync_status as maintenance_sync_status, "
        "rt.id::text as rating_tree_version_id,rt.tree_name as rating_tree_name,"
        "rt.package_version as rating_tree_package_version,"
        "rt.tree_content_checksum as rating_tree_content_checksum "
        "from inspection_years iy join bridges b on b.id = iy.bridge_id "
        "left join project_standard_profiles sp on sp.id=iy.standard_profile_id "
        "left join standard_packages tp on tp.id=sp.technical_condition_package_id "
        "left join standard_packages mp on mp.id=sp.maintenance_package_id "
        "left join rating_tree_versions rt on rt.id=sp.rating_tree_version_id "
        "where iy.id = $1::uuid",
        inspection_year_id);
    if (context_rows.empty()) return std::nullopt;

    review::InspectionWorkspace workspace;
    workspace.bridge = bridge_from_row(context_rows[0]);
    workspace.inspection_year = inspection_from_row(context_rows[0]);
    if (!context_rows[0]["standard_profile_id"].isNull()) {
        review::WorkspaceStandardProfile profile;
        profile.id = context_rows[0]["standard_profile_id"].as<std::string>();
        profile.revision_number = context_rows[0]["standard_profile_revision"].as<int>();
        profile.status = context_rows[0]["standard_profile_status"].as<std::string>();
        profile.technical_condition = standard_package_from_row(
            context_rows[0], "technical", "technical_condition");
        profile.maintenance = standard_package_from_row(
            context_rows[0], "maintenance", "maintenance");
        if (!context_rows[0]["rating_tree_version_id"].isNull()) {
            profile.rating_tree_version_id =
                context_rows[0]["rating_tree_version_id"].as<std::string>();
            profile.rating_tree_name =
                context_rows[0]["rating_tree_name"].as<std::string>();
            profile.rating_tree_package_version =
                context_rows[0]["rating_tree_package_version"].as<std::string>();
            profile.rating_tree_content_checksum =
                context_rows[0]["rating_tree_content_checksum"].as<std::string>();
        }
        workspace.standard_profile = std::move(profile);
    }

    const auto import_rows = db_client_->execSqlSync(
        "select ir.id::text, ir.system_number, ir.import_name, ir.source_type, ir.import_status, "
        "ir.importer_name, ir.parsed_result_json::text, ir.created_at::text, ir.updated_at::text, ir.error_message, "
        "u.username as lock_owner_username, u.display_name as lock_owner_display_name, "
        "l.acquired_at::text as lock_acquired_at, l.expires_at::text as lock_expires_at, "
        "sf.status as temporary_source_status, sf.expires_at::text as temporary_source_expires_at "
        "from import_records ir "
        "left join import_source_files sf on sf.import_record_id=ir.id "
        "left join import_record_edit_locks l on l.import_record_id = ir.id and l.expires_at > now() "
        "left join users u on u.id = l.user_id "
        "where ir.inspection_year_id = $1::uuid order by ir.created_at desc", inspection_year_id);

    for (const auto& row : import_rows) {
        review::WorkspaceImport item;
        item.id = row["id"].as<std::string>();
        item.system_number = row["system_number"].as<std::string>();
        item.import_name = row["import_name"].as<std::string>();
        item.source_type = row["source_type"].as<std::string>();
        item.import_status = row["import_status"].as<std::string>();
        item.importer_name = optional_text(row, "importer_name");
        item.created_at = optional_text(row, "created_at");
        item.updated_at = optional_text(row, "updated_at");
        item.error_message = optional_text(row, "error_message");
        item.temporary_source_status = optional_text(row, "temporary_source_status");
        item.temporary_source_expires_at = optional_text(row, "temporary_source_expires_at");
        item.statistics = review::build_review_statistics(
            parse_json_or_empty(row["parsed_result_json"].as<std::string>()));
        const auto owner_username = optional_text(row, "lock_owner_username");
        const auto owner_display_name = optional_text(row, "lock_owner_display_name");
        const auto acquired_at = optional_text(row, "lock_acquired_at");
        const auto expires_at = optional_text(row, "lock_expires_at");
        if (owner_username && owner_display_name && acquired_at && expires_at) {
            item.edit_lock = review::WorkspaceEditLock{*owner_username, *owner_display_name, *acquired_at, *expires_at};
        }
        if (item.import_status == "已上传" || item.import_status == "解析中" || item.import_status == "待校对"
            || item.import_status == "解析失败") {
            ++workspace.pending.import_count;
        }
        workspace.imports.push_back(std::move(item));
    }

    const auto unbound_rows = db_client_->execSqlSync(
        "select count(*)::int as count from defect_observations o "
        "join inspection_years iy on iy.id = o.inspection_year_id "
        "where o.inspection_year_id = $1::uuid and iy.is_current and iy.status = '已确认' "
        "and o.review_status in ('已确认','已修改') and o.defect_thread_id is null", inspection_year_id);
    workspace.pending.unbound_observation_count = unbound_rows[0]["count"].as<int>();
    return workspace;
}

CreateInspectionYearOutcome WorkspaceRepository::create_inspection_year(
    const std::string& bridge_id,
    const int inspection_year,
    const std::string& rating_tree_version_id,
    const std::string& created_by_user_id
) {
    std::shared_ptr<drogon::orm::Transaction> transaction;
    const auto latch = std::make_shared<CommitLatch>();
    try {
        transaction = db_client_->newTransaction(latch->callback());
        transaction->execSqlSync(
            "select pg_advisory_xact_lock(hashtext($1::text), $2::integer)",
            bridge_id, inspection_year);
        const auto bridge_rows = transaction->execSqlSync(
            "select 1 from bridges where id=$1::uuid for share", bridge_id);
        if (bridge_rows.empty()) {
            transaction->rollback();
            return {CreateInspectionYearStatus::BridgeNotFound, std::nullopt, std::nullopt};
        }
        const auto existing_rows = transaction->execSqlSync(
            "select id::text from inspection_years "
            "where bridge_id=$1::uuid and inspection_year=$2 and is_current limit 1",
            bridge_id, inspection_year);
        if (!existing_rows.empty()) {
            transaction->rollback();
            return {CreateInspectionYearStatus::AlreadyExists, std::nullopt,
                    existing_rows[0]["id"].as<std::string>()};
        }

        const auto tree_rows = transaction->execSqlSync(
            "select v.technical_condition_package_id::text as technical_id,"
            "v.maintenance_package_id::text as maintenance_id,v.status,"
            "t.is_enabled as technical_enabled,t.sync_status as technical_sync_status,"
            "m.is_enabled as maintenance_enabled,m.sync_status as maintenance_sync_status "
            "from rating_tree_versions v "
            "join standard_packages t on t.id=v.technical_condition_package_id "
            "join standard_packages m on m.id=v.maintenance_package_id "
            "where v.id=$1::uuid for share of v,t,m",
            rating_tree_version_id);
        if (tree_rows.empty()) {
            transaction->rollback();
            return {CreateInspectionYearStatus::RatingTreeNotFound, std::nullopt, std::nullopt};
        }
        const auto& tree = tree_rows[0];
        if (tree["status"].as<std::string>() != "published" ||
            !tree["technical_enabled"].as<bool>() ||
            !tree["maintenance_enabled"].as<bool>() ||
            tree["technical_sync_status"].as<std::string>() != "正常" ||
            tree["maintenance_sync_status"].as<std::string>() != "正常") {
            transaction->rollback();
            return {CreateInspectionYearStatus::RatingTreeUnavailable, std::nullopt, std::nullopt};
        }

        const auto profile_rows = transaction->execSqlSync(
            "with existing as ("
            "select id from project_standard_profiles "
            "where rating_tree_version_id=$1::uuid and status='生效' "
            "order by created_at limit 1"
            "), inserted as ("
            "insert into project_standard_profiles ("
            "technical_condition_package_id,maintenance_package_id,"
            "rating_tree_version_id,created_by_user_id,change_reason"
            ") select $2::uuid,$3::uuid,$1::uuid,$4::uuid,'创建年度检测时锁定评定树' "
            "where not exists(select 1 from existing) returning id"
            ") select id::text as id from existing "
            "union all select id::text as id from inserted limit 1",
            rating_tree_version_id,
            tree["technical_id"].as<std::string>(),
            tree["maintenance_id"].as<std::string>(),
            created_by_user_id);
        const auto inserted_rows = transaction->execSqlSync(
            "insert into inspection_years "
            "(bridge_id, inspection_year, status, version_number, is_current, standard_profile_id) "
            "values ($1::uuid, $2, '待校对', 1, true, $3::uuid) "
            "returning id::text as inspection_id, system_number as inspection_system_number, "
            "inspection_year, status as inspection_status, version_number, is_current, "
            "overall_score, overall_grade, created_at::text as inspection_created_at, "
            "updated_at::text as inspection_updated_at",
            bridge_id, inspection_year, profile_rows[0]["id"].as<std::string>());
        const auto created = inspection_from_row(inserted_rows[0]);
        transaction.reset();
        if (!latch->wait()) {
            return {CreateInspectionYearStatus::Failed, std::nullopt, std::nullopt};
        }
        return {CreateInspectionYearStatus::Created, created, std::nullopt};
    } catch (...) {
        if (transaction) {
            try { transaction->rollback(); } catch (...) {}
        }
        return {CreateInspectionYearStatus::Failed, std::nullopt, std::nullopt};
    }
}

UploadWordOutcome WorkspaceRepository::upload_word_import(
    const std::string& inspection_year_id,
    const std::string& source_type,
    const archive::WordInputMetadata& metadata,
    const std::string_view content,
    const std::filesystem::path& temporary_word_root
) {
    std::shared_ptr<drogon::orm::Transaction> transaction;
    auto latch = std::make_shared<CommitLatch>();
    std::filesystem::path stored_relative_path;
    bool file_stored = false;
    try {
        transaction = db_client_->newTransaction(latch->callback());
        const auto context_rows = transaction->execSqlSync(
            "select iy.inspection_year, iy.is_current, b.id::text as bridge_id, "
            "b.system_number as bridge_system_number, b.bridge_name "
            "from inspection_years iy join bridges b on b.id = iy.bridge_id "
            "where iy.id = $1::uuid for update", inspection_year_id);
        if (context_rows.empty()) {
            transaction->rollback();
            return {UploadWordStatus::InspectionYearNotFound, std::nullopt};
        }
        const auto& context = context_rows[0];
        if (!context["is_current"].as<bool>()) {
            transaction->rollback();
            return {UploadWordStatus::InspectionYearNotCurrent, std::nullopt};
        }

        const auto import_rows = transaction->execSqlSync(
            "insert into import_records (bridge_id, inspection_year_id, import_name, source_type, import_status) "
            "values ($1::uuid, $2::uuid, $3, $4, '已上传') "
            "returning id::text, system_number, created_at::text, updated_at::text",
            context["bridge_id"].as<std::string>(), inspection_year_id,
            metadata.original_file_name, source_type);
        const auto& import_row = import_rows[0];
        const auto import_id = import_row["id"].as<std::string>();
        const auto import_number = import_row["system_number"].as<std::string>();

        const auto source_rows = transaction->execSqlSync(
            "with source_id as (select gen_random_uuid() as id) "
            "insert into import_source_files "
            "(id, import_record_id, original_file_name, storage_relative_path, file_extension, "
            " file_size_bytes, file_hash, status) "
            // 扩展名跟着元数据走：Word 是 .docx，接口同步是指向本机离线库的 .srcref。
            "select id, $1::uuid, $2, id::text || $3, $3, $4, $5, '待解析' from source_id "
            "returning id::text, system_number, storage_relative_path",
            import_id, metadata.original_file_name, metadata.file_extension,
            static_cast<long long>(metadata.file_size_bytes), metadata.sha256);
        stored_relative_path = std::filesystem::path(
            source_rows[0]["storage_relative_path"].as<std::string>());

        archive::store_temporary_word(
            temporary_word_root, stored_relative_path, content, metadata.sha256);
        file_stored = true;

        transaction.reset();
        if (!latch->wait()) {
            try { archive::remove_temporary_word(temporary_word_root, stored_relative_path); }
            catch (...) {
            }
            return {UploadWordStatus::TemporaryStorageFailed, std::nullopt};
        }

        review::WorkspaceImport imported;
        imported.id = import_id;
        imported.system_number = import_number;
        imported.import_name = metadata.original_file_name;
        imported.source_type = source_type;
        imported.import_status = "已上传";
        imported.created_at = optional_text(import_row, "created_at");
        imported.updated_at = optional_text(import_row, "updated_at");
        return {UploadWordStatus::Created, std::move(imported)};
    } catch (const std::exception& error) {
        if (transaction) {
            try { transaction->rollback(); }
            catch (...) {
            }
        }
        if (file_stored) {
            try { archive::remove_temporary_word(temporary_word_root, stored_relative_path); }
            catch (...) {
            }
        }
        LOG_ERROR << "Temporary Word source transaction failed: " << error.what();
        return {UploadWordStatus::TemporaryStorageFailed, std::nullopt};
    } catch (...) {
        if (transaction) {
            try { transaction->rollback(); }
            catch (...) {
            }
        }
        if (file_stored) {
            try { archive::remove_temporary_word(temporary_word_root, stored_relative_path); }
            catch (...) {
            }
        }
        LOG_ERROR << "Temporary Word source transaction failed with an unknown exception";
        return {UploadWordStatus::TemporaryStorageFailed, std::nullopt};
    }
}

}  // namespace bridge_report::db
