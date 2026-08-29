#include "bridge_report/db/ReviewRepository.hpp"
#include "bridge_report/contracts/AnnualInspectionContract.hpp"
#include "bridge_report/auth/PasswordHash.hpp"
#include "bridge_report/assessment/AssessmentConfirmationService.hpp"
#include "bridge_report/db/ComponentInventoryRepository.hpp"
#include "bridge_report/db/CommitLatch.hpp"
#include "bridge_report/db/RatingTreeRepository.hpp"
#include "bridge_report/review/ContractCompatibility.hpp"
#include "bridge_report/review/DraftValidation.hpp"
#include "bridge_report/review/PreflightReport.hpp"
#include "bridge_report/resolution/ConfirmResolutionReader.hpp"
#include "bridge_report/resolution/ResolutionReopenSnapshot.hpp"
#include "bridge_report/resolution/DraftResolutionSynchronizer.hpp"

#include <optional>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include <drogon/orm/Exception.h>
#include <json/json.h>

namespace bridge_report::db {

namespace {

std::optional<std::string> optional_text(const drogon::orm::Row& row, const std::string& column) {
    const auto field = row[column];
    if (field.isNull()) {
        return std::nullopt;
    }
    return field.as<std::string>();
}

// jsonb/文本列不保留输入格式，紧凑序列化即可，与 ReviewRoutes/ConfirmPlan 的既有约定一致。
std::string write_compact_json(const Json::Value& value) {
    Json::StreamWriterBuilder writer_builder;
    writer_builder["indentation"] = "";
    return Json::writeString(writer_builder, value);
}

std::string build_raw_cells_json(
    const std::optional<std::string>& raw_row_text,
    const std::optional<Json::Value>& range_split_origin,
    const Json::Value& photo_references) {
    Json::Value json(Json::objectValue);
    if (raw_row_text.has_value()) {
        json["raw_row_text"] = *raw_row_text;
    }
    if (range_split_origin.has_value()) {
        json["range_split_origin"] = *range_split_origin;
    }
    json["photo_references"] = photo_references;
    return write_compact_json(json);
}

// -----------------------------------------------------------------------
// confirm_annual_facts 内部使用的小型写入辅助：均在调用方已持有的事务上执行，
// 任何 execSqlSync 抛出的异常都交由 confirm_annual_facts 顶层 catch 处理（回滚 + db_write_failed）。
// -----------------------------------------------------------------------

using TransactionPtr = std::shared_ptr<drogon::orm::Transaction>;

/**
 * @brief 解析本次入库的目标年度行 id。
 *
 * 先查（bridge_id, inspection_year）是否存在“已确认 + 当前版本”行（对应 Task 8 规格步骤 3），
 * 再决定是否需要新建修订版；仅当不存在修订冲突时才复用/新建“步骤 2”意义上的占位年度行。
 * 这个顺序与规格文本中“先步骤 2 再步骤 3”的写法不同：若严格按步骤 2 先行——当
 * import_records.inspection_year_id 本来为空、又确实需要走修订分支时，步骤 2 插入的
 * version_number=1 占位行会在步骤 3 另建新行后变成永久孤儿。调整顺序后行为对规格列出的四个
 * 测试场景完全一致（inspection_year_id 已挂载时两种顺序均不会在步骤 2/3 产生插入），仅在
 * “未挂载 + 需要修订”这一未覆盖场景下避免产生垃圾行，因此认为是更安全的实现选择。
 *
 * 另有一类被遗弃的占位行——记录已挂在待校对占位行 X，但同桥同年另一条导入先确认、修订分支
 * 又新建了 Z——不由本函数处理，而是在 confirm_annual_facts 把导入记录改指向 Z 之后，用一条
 * 收紧谓词的 DELETE 清理（见该函数步骤 7 收尾）。
 *
 * @return 目标年度行 id；若存在修订冲突且调用方未确认修订，返回 std::nullopt
 * （调用方据此直接回滚并返回 revision_confirmation_required，不再插入任何行）。
 */
std::optional<std::string> resolve_target_inspection_year_id(
    const TransactionPtr& tx,
    const std::string& bridge_id,
    int inspection_year,
    const std::optional<std::string>& existing_inspection_year_id,
    bool confirm_revision,
    const std::string& standard_profile_id,
    const std::string& inventory_revision_id
) {
    const auto current_result = tx->execSqlSync(
        "select id, version_number from inspection_years "
        "where bridge_id = $1::uuid and inspection_year = $2 and is_current and status = '已确认' "
        "for update",
        bridge_id,
        inspection_year
    );

    if (!current_result.empty()) {
        if (!confirm_revision) {
            return std::nullopt;
        }

        const auto& current_row = current_result[0];
        const auto current_year_id = current_row["id"].as<std::string>();
        const auto current_version = current_row["version_number"].as<int>();

        // IMPORTANT: 必须先降级旧行再插入新的 is_current 行——
        // 部分唯一索引 ux_inspection_years_current_bridge_year (bridge_id, inspection_year)
        // where is_current 不允许同一 (bridge_id, inspection_year) 同时存在两个 is_current 行。
        tx->execSqlSync(
            "update inspection_years set is_current = false, status = '已被修订', updated_at = now() "
            "where id = $1::uuid",
            current_year_id
        );

        const auto inserted = tx->execSqlSync(
            "insert into inspection_years "
            "(bridge_id, inspection_year, version_number, status, is_current, revision_source_inspection_id,"
            " standard_profile_id,component_inventory_revision_id) "
            "values ($1::uuid, $2, $3, '待校对', true, $4::uuid,$5::uuid,$6::uuid) "
            "returning id",
            bridge_id,
            inspection_year,
            current_version + 1,
            current_year_id,
            standard_profile_id,
            inventory_revision_id
        );
        return inserted[0]["id"].as<std::string>();
    }

    if (existing_inspection_year_id.has_value()) {
        return *existing_inspection_year_id;
    }

    const auto inserted = tx->execSqlSync(
        "insert into inspection_years (bridge_id, inspection_year, version_number, status, is_current,"
        " standard_profile_id,component_inventory_revision_id) "
        "values ($1::uuid, $2, 1, '待校对', false,$3::uuid,$4::uuid) "
        "returning id",
        bridge_id,
        inspection_year,
        standard_profile_id,
        inventory_revision_id
    );
    return inserted[0]["id"].as<std::string>();
}

/**
 * @brief 一个已 upsert 完成的构件：bridge_component_id 供事实表外键使用，
 * component_type/business_component_code 取自本次写计划（而非命中已存在行时该行原有的文本），
 * 用于回填 defect_observations 上冗余的展示字段。
 */
struct UpsertedComponent {
    std::string bridge_component_id;
    std::string component_type;
    std::string business_component_code;
};

UpsertedComponent upsert_component(
    const TransactionPtr& tx, const std::string& bridge_id, const review::ComponentPlan& component
) {
    if (component.existing_bridge_component_id.has_value()) {
        const auto owned = tx->execSqlSync(
            "select component_type,business_component_code from bridge_components "
            "where id=$1::uuid and bridge_id=$2::uuid",
            *component.existing_bridge_component_id,
            bridge_id);
        if (owned.empty()) {
            throw std::runtime_error(
                "confirm_annual_facts: linked bridge component is not owned by bridge");
        }
        return UpsertedComponent{
            *component.existing_bridge_component_id,
            owned[0]["component_type"].as<std::string>(),
            owned[0]["business_component_code"].as<std::string>()};
    }
    const auto existing = tx->execSqlSync(
        "select id from bridge_components where bridge_id = $1::uuid and normalized_component_key = $2",
        bridge_id,
        component.normalized_component_key
    );

    std::string bridge_component_id;
    if (!existing.empty()) {
        bridge_component_id = existing[0]["id"].as<std::string>();
    } else {
        const auto inserted = tx->execSqlSync(
            "insert into bridge_components "
            "(bridge_id, structure_part, component_type, business_component_code, normalized_component_key, "
            " current_status, creation_source) "
            "values ($1::uuid, $2, $3, $4, $5, '已确认', '导入沉淀') "
            "returning id",
            bridge_id,
            component.structure_part,
            component.component_type,
            component.business_component_code,
            component.normalized_component_key
        );
        bridge_component_id = inserted[0]["id"].as<std::string>();
    }

    if (component.alias_text.has_value() && !component.alias_text->empty()) {
        tx->execSqlSync(
            "insert into component_aliases (bridge_component_id, alias_text, is_manually_confirmed) "
            "values ($1::uuid, $2, true) "
            "on conflict (bridge_component_id, alias_text) do nothing",
            bridge_component_id,
            *component.alias_text
        );
    }

    return UpsertedComponent{bridge_component_id, component.component_type, component.business_component_code};
}

std::string insert_defect_observation(
    const TransactionPtr& tx,
    const std::string& inspection_year_id,
    const std::string& bridge_id,
    const std::string& import_record_id,
    const UpsertedComponent& component,
    const review::DefectPlan& defect
) {
    const auto result = tx->execSqlSync(
        "insert into defect_observations "
        "(inspection_year_id, bridge_id, bridge_component_id, source_import_record_id, "
        " source_table_title, source_table_index, source_row_number, source_raw_cells_json, "
        " structure_part, part_name, component_type, business_component_code, "
        " defect_location, rating_tree_node_id, standard_defect_indicator_id, "
        " defect_type, defect_description_raw, scale, "
        " extraction_confidence, review_status) "
        "values ($1::uuid, $2::uuid, $3::uuid, $4::uuid, "
        "        $5, $6, $7, $8::jsonb, "
        "        $9, $10, $11, $12, "
        "        nullif($13,''), nullif($14,'')::uuid, nullif($15,''), $16, $17, $18, "
        "        $19, $20) "
        "returning id",
        inspection_year_id,
        bridge_id,
        component.bridge_component_id,
        import_record_id,
        defect.source_table_title,
        defect.source_table_index,
        defect.source_row_number,
        build_raw_cells_json(
            defect.raw_row_text,
            defect.range_split_origin,
            defect.photo_references),
        defect.structure_part,
        defect.part_name,
        component.component_type,
        component.business_component_code,
        defect.defect_location,
        defect.rating_tree_node_id,
        defect.standard_defect_indicator_id,
        defect.defect_type,
        defect.defect_description_raw,
        defect.scale,
        defect.extraction_confidence,
        defect.review_status
    );
    return result[0]["id"].as<std::string>();
}

void insert_defect_measurement(
    const TransactionPtr& tx, const std::string& defect_observation_id, const review::MeasurementPlan& measurement
) {
    tx->execSqlSync(
        "insert into defect_measurements "
        "(defect_observation_id, measurement_type, value_type, numeric_value, minimum_value, maximum_value, unit, "
        "is_approximate, raw_text, is_auto_parsed, is_manually_confirmed) "
        "values ($1::uuid, $2, $3, $4, $5, $6, $7, $8, $9, $10, true)",
        defect_observation_id,
        measurement.measurement_type,
        measurement.value_type,
        measurement.numeric_value,
        measurement.minimum_value,
        measurement.maximum_value,
        measurement.unit,
        measurement.is_approximate,
        measurement.raw_text,
        measurement.is_auto_parsed
    );
}

void insert_defect_photo(
    const TransactionPtr& tx,
    const std::string& defect_observation_id,
    const std::string& archived_file_id,
    const std::string& import_record_id,
    const review::PhotoPlan& photo
) {
    tx->execSqlSync(
        "insert into defect_photos "
        "(defect_observation_id, archived_file_id, source_import_record_id, photo_number, photo_title) "
        "values ($1::uuid, $2::uuid, $3::uuid, $4, $5)",
        defect_observation_id,
        archived_file_id,
        import_record_id,
        photo.photo_number,
        photo.photo_title
    );
}

Json::Value parse_json_strict(const std::string& text) {
    Json::CharReaderBuilder builder;
    Json::Value data;
    std::string errors;
    std::istringstream stream(text);
    if (!Json::parseFromStream(builder, stream, &data, &errors)) {
        throw std::runtime_error("stored parsed_result_json is invalid: " + errors);
    }
    return data;
}

}  // 匿名命名空间

ReviewRepository::ReviewRepository(
    drogon::orm::DbClientPtr db_client,
    std::shared_ptr<const standards::StandardRegistry> standard_registry)
    : db_client_(std::move(db_client)),
      standard_registry_(std::move(standard_registry)) {}

std::vector<review::BridgeSummary> ReviewRepository::list_bridges() {
    const auto result = db_client_->execSqlSync(
        "select b.id, b.system_number, b.bridge_name, b.route_name, b.status, b.bridge_scale, "
        "latest.inspection_year as latest_inspection_year, latest.overall_score as latest_overall_score, "
        "latest.overall_grade as latest_overall_grade, "
        "(coalesce((select count(*) from import_records ir "
        " left join inspection_years piy on piy.id = ir.inspection_year_id "
        " where ir.bridge_id = b.id and ir.import_status in ('已上传','解析中','待校对','解析失败') "
        " and (ir.inspection_year_id is null or piy.is_current)), 0) + "
        " coalesce((select count(*) from defect_observations o "
        " join inspection_years oiy on oiy.id = o.inspection_year_id "
        " where o.bridge_id = b.id and oiy.is_current and oiy.status = '已确认' "
        " and o.review_status in ('已确认','已修改') and o.defect_thread_id is null), 0))::int as pending_count "
        "from bridges b "
        "left join lateral (select iy.inspection_year, iy.overall_score, iy.overall_grade "
        " from inspection_years iy where iy.bridge_id = b.id and iy.is_current "
        " and iy.status in ('已确认','已归档') order by iy.inspection_year desc limit 1) latest on true "
        "order by b.system_number"
    );

    std::vector<review::BridgeSummary> bridges;
    bridges.reserve(result.size());
    for (const auto& row : result) {
        review::BridgeSummary summary;
        summary.id = row["id"].as<std::string>();
        summary.system_number = row["system_number"].as<std::string>();
        summary.bridge_name = row["bridge_name"].as<std::string>();
        summary.route_name = optional_text(row, "route_name");
        summary.status = row["status"].as<std::string>();
        summary.bridge_scale = optional_text(row, "bridge_scale");
        if (!row["latest_inspection_year"].isNull()) {
            summary.latest_inspection_year = row["latest_inspection_year"].as<int>();
        }
        if (!row["latest_overall_score"].isNull()) {
            summary.latest_overall_score = row["latest_overall_score"].as<double>();
        }
        summary.latest_overall_grade = optional_text(row, "latest_overall_grade");
        summary.pending_count = row["pending_count"].as<int>();
        bridges.push_back(std::move(summary));
    }
    return bridges;
}

std::vector<review::InspectionYearSummary> ReviewRepository::list_inspection_years(const std::string& bridge_id) {
    const auto result = db_client_->execSqlSync(
        "select id, system_number, inspection_year, status, version_number, is_current "
        "from inspection_years "
        "where bridge_id = $1::uuid "
        "order by inspection_year desc, version_number desc",
        bridge_id
    );

    std::vector<review::InspectionYearSummary> years;
    years.reserve(result.size());
    for (const auto& row : result) {
        review::InspectionYearSummary summary;
        summary.id = row["id"].as<std::string>();
        summary.system_number = row["system_number"].as<std::string>();
        summary.inspection_year = row["inspection_year"].as<int>();
        summary.status = row["status"].as<std::string>();
        summary.version_number = row["version_number"].as<int>();
        summary.is_current = row["is_current"].as<bool>();
        years.push_back(std::move(summary));
    }
    return years;
}

std::vector<review::ImportRecordSummary> ReviewRepository::list_import_records(const std::string& bridge_id) {
    const auto result = db_client_->execSqlSync(
        "select ir.id, ir.system_number, ir.import_name, ir.source_type, ir.import_status, "
        "ir.inspection_year_id, ir.importer_name, ir.created_at::text, "
        "u.username as lock_owner_username, u.display_name as lock_owner_display_name, "
        "l.acquired_at::text as lock_acquired_at, l.expires_at::text as lock_expires_at "
        "from import_records ir "
        "left join import_record_edit_locks l on l.import_record_id = ir.id and l.expires_at > now() "
        "left join users u on u.id = l.user_id "
        "where ir.bridge_id = $1::uuid order by ir.created_at desc",
        bridge_id
    );

    std::vector<review::ImportRecordSummary> records;
    records.reserve(result.size());
    for (const auto& row : result) {
        review::ImportRecordSummary summary;
        summary.id = row["id"].as<std::string>();
        summary.system_number = row["system_number"].as<std::string>();
        summary.import_name = row["import_name"].as<std::string>();
        summary.source_type = row["source_type"].as<std::string>();
        summary.import_status = row["import_status"].as<std::string>();
        summary.inspection_year_id = optional_text(row, "inspection_year_id");
        summary.importer_name = optional_text(row, "importer_name");
        summary.created_at = row["created_at"].as<std::string>();
        summary.edit_lock_owner_username = optional_text(row, "lock_owner_username");
        summary.edit_lock_owner_display_name = optional_text(row, "lock_owner_display_name");
        summary.edit_lock_acquired_at = optional_text(row, "lock_acquired_at");
        summary.edit_lock_expires_at = optional_text(row, "lock_expires_at");
        records.push_back(std::move(summary));
    }
    return records;
}

std::optional<review::ImportRecordDetail> ReviewRepository::get_import_record_detail(
    const std::string& import_record_id
) {
    const auto result = db_client_->execSqlSync(
        "select "
        "ir.id, ir.system_number, ir.bridge_id, ir.inspection_year_id, "
        "ir.import_name, ir.source_type, ir.import_status, ir.draft_version, "
        "ir.importer_name, ir.importer_version, ir.parsed_result_json::text as parsed_result_json, "
        "ir.created_at::text as created_at, ir.updated_at::text as updated_at, "
        "ir.reopened_at::text as reopened_at, ir.reopened_by_username, ir.reopen_scope, "
        "b.system_number as bridge_system_number, b.bridge_name as bridge_name, b.route_name as bridge_route_name, "
        "iy.system_number as inspection_year_system_number, iy.inspection_year as inspection_year, "
        "iy.status as inspection_year_status, iy.version_number as inspection_year_version_number, "
        "iy.is_current as inspection_year_is_current, "
        "tsp.id::text as technical_standard_package_id, "
        "tsp.standard_code as technical_standard_code, "
        "tsp.standard_name as technical_standard_name, "
        "tsp.official_edition as technical_standard_official_edition, "
        "tsp.package_version as technical_standard_package_version, "
        "rtv.id::text as rating_tree_version_id,rtv.tree_name as rating_tree_name,"
        "rtv.package_version as rating_tree_package_version,"
        "rtv.tree_content_checksum as rating_tree_content_checksum,"
        "iy.component_inventory_revision_id::text as inspection_year_inventory_revision_id "
        "from import_records ir "
        "join bridges b on b.id = ir.bridge_id "
        "left join inspection_years iy on iy.id = ir.inspection_year_id "
        "left join project_standard_profiles psp on psp.id = iy.standard_profile_id "
        "left join standard_packages tsp on tsp.id = psp.technical_condition_package_id "
        "left join rating_tree_versions rtv on rtv.id = psp.rating_tree_version_id "
        "where ir.id = $1::uuid",
        import_record_id
    );

    if (result.empty()) {
        return std::nullopt;
    }

    const auto& row = result[0];
    review::ImportRecordDetail detail;
    detail.id = row["id"].as<std::string>();
    detail.system_number = row["system_number"].as<std::string>();
    detail.bridge_id = row["bridge_id"].as<std::string>();
    detail.inspection_year_id = optional_text(row, "inspection_year_id");
    detail.import_name = row["import_name"].as<std::string>();
    detail.source_type = row["source_type"].as<std::string>();
    detail.import_status = row["import_status"].as<std::string>();
    detail.draft_version = row["draft_version"].as<int>();
    detail.importer_name = optional_text(row, "importer_name");
    detail.importer_version = optional_text(row, "importer_version");
    detail.parsed_result_json = row["parsed_result_json"].as<std::string>();
    detail.created_at = row["created_at"].as<std::string>();
    detail.updated_at = row["updated_at"].as<std::string>();
    detail.reopened_at = optional_text(row, "reopened_at");
    detail.reopened_by_username = optional_text(row, "reopened_by_username");
    detail.reopen_scope = optional_text(row, "reopen_scope");

    detail.bridge_system_number = row["bridge_system_number"].as<std::string>();
    detail.bridge_name = row["bridge_name"].as<std::string>();
    detail.bridge_route_name = optional_text(row, "bridge_route_name");

    detail.inspection_year_system_number = optional_text(row, "inspection_year_system_number");
    const auto inspection_year_field = row["inspection_year"];
    detail.inspection_year = inspection_year_field.isNull()
        ? std::nullopt
        : std::make_optional(inspection_year_field.as<int>());
    detail.inspection_year_status = optional_text(row, "inspection_year_status");
    detail.inspection_year_inventory_revision_id =
        optional_text(row, "inspection_year_inventory_revision_id");
    const auto version_number_field = row["inspection_year_version_number"];
    detail.inspection_year_version_number = version_number_field.isNull()
        ? std::nullopt
        : std::make_optional(version_number_field.as<int>());
    const auto is_current_field = row["inspection_year_is_current"];
    detail.inspection_year_is_current = is_current_field.isNull()
        ? std::nullopt
        : std::make_optional(is_current_field.as<bool>());
    detail.technical_standard_package_id =
        optional_text(row, "technical_standard_package_id");
    detail.technical_standard_code =
        optional_text(row, "technical_standard_code");
    detail.technical_standard_name =
        optional_text(row, "technical_standard_name");
    detail.technical_standard_official_edition =
        optional_text(row, "technical_standard_official_edition");
    detail.technical_standard_package_version =
        optional_text(row, "technical_standard_package_version");
    detail.rating_tree_version_id =
        optional_text(row, "rating_tree_version_id");
    detail.rating_tree_name = optional_text(row, "rating_tree_name");
    detail.rating_tree_package_version =
        optional_text(row, "rating_tree_package_version");
    detail.rating_tree_content_checksum =
        optional_text(row, "rating_tree_content_checksum");

    return detail;
}

std::optional<PhotoContentRef> ReviewRepository::get_photo_content_ref(
    const std::string& import_record_id,
    const std::string& photo_candidate_id
) {
    const auto result = db_client_->execSqlSync(
        "select af.id::text as archived_file_id, af.storage_relative_path, lower(af.file_extension) as extension "
        "from import_records ir "
        "cross join lateral jsonb_array_elements("
        "case when jsonb_typeof(ir.parsed_result_json->'photos') = 'array' "
        "then ir.parsed_result_json->'photos' else '[]'::jsonb end) photo "
        "join import_record_files irf on irf.import_record_id = ir.id and irf.file_role = '附件' "
        "and irf.process_status = '处理成功' "
        "join archived_files af on af.id = irf.archived_file_id "
        "and af.file_type = '图片' "
        "and af.storage_relative_path = photo->'extracted_file'->>'archive_relative_path' "
        "where ir.id = $1::uuid and photo->>'candidate_id' = $2 limit 1",
        import_record_id,
        photo_candidate_id
    );
    if (result.empty()) return std::nullopt;

    auto extension = result[0]["extension"].isNull()
        ? std::string() : result[0]["extension"].as<std::string>();
    if (!extension.empty() && extension.front() != '.') extension.insert(extension.begin(), '.');
    std::string content_type;
    if (extension == ".jpg" || extension == ".jpeg") content_type = "image/jpeg";
    else if (extension == ".png") content_type = "image/png";
    else if (extension == ".gif") content_type = "image/gif";
    else if (extension == ".bmp") content_type = "image/bmp";
    else if (extension == ".webp") content_type = "image/webp";
    else if (extension == ".tif" || extension == ".tiff") content_type = "image/tiff";
    else return std::nullopt;

    return PhotoContentRef{
        result[0]["archived_file_id"].as<std::string>(),
        result[0]["storage_relative_path"].as<std::string>(),
        std::move(content_type)
    };
}

bool ReviewRepository::save_review_draft(
    const std::string& import_record_id,
    const std::string& parsed_json_text,
    const std::optional<EditLockCredentials>& edit_lock,
    const std::string& defect_change_audit_json
) {
    // 与 cancel_import_record 同一惯用法：把状态谓词放进 UPDATE，
    // 避免“处理器读到待校对 -> 并发取消/确认 -> 草稿仍写入”的 TOCTOU 竞态。
    const auto result = edit_lock.has_value()
        ? db_client_->execSqlSync(
            "update import_records "
            "set parsed_result_json = $2::jsonb, "
            "validation_result_json = case when $6::text = '' then validation_result_json else "
            "jsonb_set(coalesce(validation_result_json, '{}'::jsonb), '{draft_audit_events}', "
            "coalesce(validation_result_json->'draft_audit_events', '[]'::jsonb) "
            "|| jsonb_build_array($6::jsonb || jsonb_build_object('saved_at', now())), true) end, "
            "updated_at = now() "
            "where id = $1::uuid and import_status = '待校对' "
            "and exists(select 1 from import_record_edit_locks l "
            "  where l.import_record_id = import_records.id and l.user_id = $3::uuid "
            "  and l.user_session_id = $4::uuid and l.lock_token_hash = $5 and l.expires_at > now()) "
            "returning id",
            import_record_id, parsed_json_text, edit_lock->user_id, edit_lock->session_id,
            auth::sha256_hex(edit_lock->lock_token), defect_change_audit_json)
        : db_client_->execSqlSync(
            "update import_records "
            "set parsed_result_json = $2::jsonb, "
            "validation_result_json = case when $3::text = '' then validation_result_json else "
            "jsonb_set(coalesce(validation_result_json, '{}'::jsonb), '{draft_audit_events}', "
            "coalesce(validation_result_json->'draft_audit_events', '[]'::jsonb) "
            "|| jsonb_build_array($3::jsonb || jsonb_build_object('saved_at', now())), true) end, "
            "updated_at = now() "
            "where id = $1::uuid and import_status = '待校对' returning id",
            import_record_id, parsed_json_text, defect_change_audit_json);
    return !result.empty();
}

namespace {

// 存量 parsed_result_json 的宽松解析：与路由层 parse_parsed_result_json 同语义。
// 解析不了时退回空对象而不是抛异常——存量脏数据不该表现成“数据库写入失败”。
Json::Value parse_stored_draft(const std::string& text) {
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;
    std::istringstream stream(text);
    if (!Json::parseFromStream(builder, stream, &root, &errors)) {
        return Json::Value(Json::objectValue);
    }
    return root;
}

}  // 匿名命名空间

SaveReviewDraftOutcome ReviewRepository::save_review_draft(const SaveReviewDraftInput& input) {
    // 与 confirm_annual_facts 同一套事务骨架：db_client_ 必须是裸 DbClient。
    std::shared_ptr<drogon::orm::Transaction> tx;
    const auto latch = std::make_shared<CommitLatch>();

    const auto fail = [&](std::string code, std::string message) -> SaveReviewDraftOutcome {
        if (tx != nullptr) {
            try { tx->rollback(); }
            catch (...) {
            }
        }
        SaveReviewDraftOutcome failed;
        failed.success = false;
        failed.error_code = std::move(code);
        failed.error_message = std::move(message);
        return failed;
    };
    // 没带期望版本就不得写：整份覆盖的写入必须知道自己基于哪一版。
    // 放行等于允许一个不知道自己看的是哪一版的客户端覆盖别人刚存的东西。
    if (!input.expected_draft_version.has_value()) {
        return fail("review_draft_version_required",
                    "缺少 If-Match: \"draft-<version>\" 请求头，无法安全保存草稿。");
    }
    // 校验类失败：error_code 与 validation.code 保持一致，逐项问题原样带出。
    const auto fail_validation = [&](review::DraftValidationResult validation) -> SaveReviewDraftOutcome {
        auto failed = fail(validation.code, validation.message);
        failed.validation = std::move(validation);
        return failed;
    };

    try {
        tx = db_client_->newTransaction(latch->callback());

        // 步骤 1：锁住并重新读取导入记录。状态、编辑锁与存量草稿都必须取自本事务，
        // 否则路由加载之后到写入之前的并发改动会被旧快照覆盖。
        const auto record_result = tx->execSqlSync(
            "select import_status, bridge_id::text as bridge_id, "
            "inspection_year_id::text as inspection_year_id, "
            "parsed_result_json::text as parsed_result_json, "
            "reopened_at::text as reopened_at, reopen_scope "
            "from import_records where id = $1::uuid for update",
            input.import_record_id);
        if (record_result.empty()) {
            return fail("import_record_not_found", "导入记录不存在。");
        }
        const auto& record_row = record_result[0];
        const auto import_status = record_row["import_status"].as<std::string>();
        if (import_status != "待校对") {
            return fail(
                "import_record_not_editable",
                "导入记录当前状态为「" + import_status + "」，不是待校对，无法保存草稿。");
        }
        if (input.edit_lock.has_value()) {
            const auto lock_result = tx->execSqlSync(
                "select exists(select 1 from import_record_edit_locks "
                "where import_record_id = $1::uuid and user_id = $2::uuid and user_session_id = $3::uuid "
                "and lock_token_hash = $4 and expires_at > now()) as active",
                input.import_record_id, input.edit_lock->user_id, input.edit_lock->session_id,
                auth::sha256_hex(input.edit_lock->lock_token));
            if (lock_result.empty() || !lock_result[0]["active"].as<bool>()) {
                return fail("edit_lock_invalid", "编辑锁已失效，草稿未保存。");
            }
        }
        const auto bridge_id = record_row["bridge_id"].as<std::string>();
        const auto inspection_year_id = optional_text(record_row, "inspection_year_id");
        const auto reopened_at = optional_text(record_row, "reopened_at");
        const auto reopen_scope = optional_text(record_row, "reopen_scope");

        // 步骤 2：锁顺序 import_records -> inspection_years，与绑定、Word 导入和确认事务一致。
        // 年度锁定版本、规范组合与评定树版本全部在年度行锁内读取。
        std::optional<std::string> locked_revision_id;
        std::optional<std::string> rating_tree_version_id;
        std::optional<std::string> technical_package_id;
        if (inspection_year_id.has_value()) {
            const auto year_row = tx->execSqlSync(
                "select iy.component_inventory_revision_id::text as inventory_revision_id, "
                "psp.rating_tree_version_id::text as rating_tree_version_id, "
                "psp.technical_condition_package_id::text as technical_package_id "
                "from inspection_years iy "
                "left join project_standard_profiles psp on psp.id = iy.standard_profile_id "
                "where iy.id = $1::uuid for update of iy",
                *inspection_year_id);
            if (!year_row.empty()) {
                locked_revision_id = optional_text(year_row[0], "inventory_revision_id");
                rating_tree_version_id = optional_text(year_row[0], "rating_tree_version_id");
                technical_package_id = optional_text(year_row[0], "technical_package_id");
            }
        }

        const auto stored_draft = parse_stored_draft(
            record_row["parsed_result_json"].as<std::string>());
        // 存量草稿仍是旧版合同时拒绝保存，不能靠客户端伪造请求绕过重新解析。
        // 提示语不写死版本号：合同升一版就要跟着改一次，而它对用户没有增量信息。
        if (review::stored_contract_requires_reparse(stored_draft)) {
            return fail(
                "contract_version_outdated",
                "该导入记录的候选数据仍是旧版合同，请删除该导入并重新解析。");
        }
        const auto evidence_validation =
            review::validate_imported_defect_evidence(stored_draft, input.draft);
        if (!evidence_validation.ok) {
            return fail_validation(evidence_validation);
        }

        Json::Value draft_to_save = input.draft;

        // 步骤 3：台账版本解析——年度锁定优先，否则该桥最新已确认，与绑定写入病害时
        // 同一条规则。仓库对象一律用临时量：它按值持有 DbClientPtr，留成具名变量会让
        // 事务活过 tx.reset()，提交回调永远不来。
        const auto resolved_inventory = ComponentInventoryRepository(tx)
                                            .resolve_confirmed_revision(bridge_id, locked_revision_id);
        const auto resolved_revision_id = resolved_inventory.has_value()
            ? std::optional<std::string>(resolved_inventory->id) : std::nullopt;
        // 5.0：草稿里已经没有构件解析字段可校验了。绑定状态住在关系表里，草稿保存
        // 要做的是把来源病害的增删改**同步**过去，而不是校验草稿自带的绑定。

        // 版本在这里就定下来了，因此锁也在这里上：草稿写的是版本化数据，与绑定和
        // Word 导入同类，同样要把版本锁进年度。否则草稿按 R1 存下、年度仍未锁定，
        // 别人确认 R2 之后下次加载会解析成 R2，刚存的绑定立刻变成旧版本数据。
        // 年度已锁定时 lock_pending_year_revision() 只做一致性确认，不覆盖。
        // 后面任何一步失败，这次锁定都随事务一起回滚。
        //
        // "有没有绑定"5.0 之后要问关系表：草稿里已经没有 bridge_component_id 可数了。
        // 不问就锁的话，一份一条构件都没绑的草稿也会把年度钉死在某个版本上——那正是
        // 这条规则当初要避免的。
        const bool has_any_binding = [&] {
            const auto rows = tx->execSqlSync(
                "select exists(select 1 from import_component_resolution_groups "
                "where import_record_id = $1::uuid and status = 'bound') as bound",
                input.import_record_id);
            return !rows.empty() && rows[0]["bound"].as<bool>();
        }();
        if (has_any_binding && resolved_revision_id.has_value()) {
            if (!ComponentInventoryRepository(tx).lock_pending_year_revision(
                    inspection_year_id, bridge_id, locked_revision_id, *resolved_revision_id)) {
                return fail(
                    "component_inventory_revision_changed",
                    "本检测年度的构件台账版本已被其他操作锁定，请刷新后重试。");
            }
        }

        // 步骤 4：评定树规范化。年度锁定的树版本与技术规范包同样取自年度行锁内。
        if (!rating_tree_version_id.has_value() || !technical_package_id.has_value()) {
            return fail("rating_tree_required", "本年度尚未锁定评定树，不能保存病害校对结果。");
        }
        // 只确认这一版树可用；解析本身由 DraftResolutionSynchronizer 装它自己那份做。
        // 已发布的树在进程内有缓存（RatingTreeRepository），所以这里付的是一次拷贝而
        // 不是一次查询——留着它换来的是一句明确的错误，而不是同步阶段一个含糊的失败。
        if (!RatingTreeRepository(tx).load_published_tree(*rating_tree_version_id)
                 .has_value()) {
            return fail("rating_tree_unavailable", "本年度锁定的评定树不可用。");
        }
        // 评分树解析同样住在关系表里；草稿不再携带节点，也就没有可规范化的引用。
        (void)technical_package_id;

        // 步骤 5：重开态的范围与角色校验（后端兜底，不依赖前端按钮显隐）：
        //   full 重开由管理员发起，其草稿保存同样只认管理员；
        //   warnings_only 重开允许任何登录用户，但只能改带警告的病害候选。
        if (reopened_at.has_value()) {
            if (reopen_scope.value_or("") == "full" && !input.actor_is_admin) {
                return fail("forbidden", "full 重开态的草稿保存仅限管理员。");
            }
            if (reopen_scope.value_or("") == "warnings_only") {
                const auto scope_validation = review::validate_warnings_only_scope(
                    stored_draft, input.draft, &draft_to_save);
                if (!scope_validation.ok) {
                    return fail_validation(scope_validation);
                }
            }
        }

        // 步骤 6：把来源病害的增删改同步到解析关系表（§11.1）。与 JSON 的写入同事务：
        // "JSON 已保存而成员未同步"的中间态里，新增的病害在绑定工作区里根本不存在，
        // 删掉的病害却还占着组。
        const auto sync = resolution::synchronize_draft_resolution(
            tx, input.import_record_id, bridge_id,
            inspection_year_id.value_or(std::string{}),
            stored_draft, draft_to_save, resolved_inventory);
        if (!sync.ok) {
            return fail(sync.error_code, sync.error_message);
        }

        const auto audit_event = review::build_defect_change_audit_event(
            stored_draft, draft_to_save, input.actor_username);
        const auto audit_json = audit_event.isNull()
            ? std::string() : write_compact_json(audit_event);

        // 步骤 6：写入。状态与编辑锁谓词保留在 UPDATE 里，与事务开头的显式检查一起
        // 兜住“检查通过后锁在同一事务外被撤销”这类窄竞态。
        const auto updated = input.edit_lock.has_value()
            ? tx->execSqlSync(
                "update import_records "
                "set parsed_result_json = $2::jsonb, "
                "validation_result_json = case when $6::text = '' then validation_result_json else "
                "jsonb_set(coalesce(validation_result_json, '{}'::jsonb), '{draft_audit_events}', "
                "coalesce(validation_result_json->'draft_audit_events', '[]'::jsonb) "
                "|| jsonb_build_array($6::jsonb || jsonb_build_object('saved_at', now())), true) end, "
                "draft_version = draft_version + 1, updated_at = now() "
                "where id = $1::uuid and import_status = '待校对' "
                "and draft_version = $7 "
                "and exists(select 1 from import_record_edit_locks l "
                "  where l.import_record_id = import_records.id and l.user_id = $3::uuid "
                "  and l.user_session_id = $4::uuid and l.lock_token_hash = $5 and l.expires_at > now()) "
                "returning draft_version",
                input.import_record_id, write_compact_json(draft_to_save),
                input.edit_lock->user_id, input.edit_lock->session_id,
                auth::sha256_hex(input.edit_lock->lock_token), audit_json,
                *input.expected_draft_version)
            : tx->execSqlSync(
                "update import_records "
                "set parsed_result_json = $2::jsonb, "
                "validation_result_json = case when $3::text = '' then validation_result_json else "
                "jsonb_set(coalesce(validation_result_json, '{}'::jsonb), '{draft_audit_events}', "
                "coalesce(validation_result_json->'draft_audit_events', '[]'::jsonb) "
                "|| jsonb_build_array($3::jsonb || jsonb_build_object('saved_at', now())), true) end, "
                "draft_version = draft_version + 1, updated_at = now() "
                "where id = $1::uuid and import_status = '待校对' "
                "and draft_version = $4 returning draft_version",
                input.import_record_id, write_compact_json(draft_to_save), audit_json,
                *input.expected_draft_version);
        if (updated.empty()) {
            // 谓词里同时含状态、锁和版本，回来空行分不出是哪一个挂的。
            // 版本对不上时要告诉客户端当前版本，它才能取回最新草稿再合并。
            const auto current = tx->execSqlSync(
                "select draft_version from import_records where id = $1::uuid",
                input.import_record_id);
            if (!current.empty()
                && current[0]["draft_version"].as<int>() != *input.expected_draft_version) {
                auto conflict = fail(
                    "review_draft_version_conflict",
                    "草稿已被其他页面保存，请刷新后重试。");
                conflict.draft_version = current[0]["draft_version"].as<int>();
                return conflict;
            }
            return fail("import_record_not_editable", "导入记录状态已变化，无法保存草稿。");
        }

        SaveReviewDraftOutcome outcome;
        outcome.draft_version = updated[0]["draft_version"].as<int>();
        tx.reset();
        if (!latch->wait()) {
            outcome.error_code = "database_commit_failed";
            outcome.error_message = "数据库提交失败。";
            return outcome;
        }
        outcome.success = true;
        return outcome;
    } catch (const drogon::orm::DrogonDbException& exception) {
        return fail("db_write_failed", exception.base().what());
    } catch (const std::exception& exception) {
        return fail("db_write_failed", exception.what());
    }
}

bool ReviewRepository::cancel_import_record(
    const std::string& import_record_id,
    const std::optional<EditLockCredentials>& edit_lock
) {
    const auto result = edit_lock.has_value()
        ? db_client_->execSqlSync(
            "with updated as ("
            "  update import_records set import_status = '已取消', updated_at = now() "
            "  where id = $1::uuid and import_status in ('已上传', '解析中', '待校对', '解析失败') "
            "  and reopened_at is null and exists(select 1 from import_record_edit_locks l "
            "    where l.import_record_id = import_records.id and l.user_id = $2::uuid "
            "    and l.user_session_id = $3::uuid and l.lock_token_hash = $4 and l.expires_at > now()) "
            "  returning id"
            ") delete from import_record_edit_locks l using updated u "
            "where l.import_record_id = u.id and l.user_id = $2::uuid and l.user_session_id = $3::uuid "
            "and l.lock_token_hash = $4 returning l.import_record_id",
            import_record_id, edit_lock->user_id, edit_lock->session_id,
            auth::sha256_hex(edit_lock->lock_token))
        : db_client_->execSqlSync(
            "update import_records set import_status = '已取消', updated_at = now() "
            "where id = $1::uuid and import_status in ('已上传', '解析中', '待校对', '解析失败') "
            "and reopened_at is null returning id",
            import_record_id);
    return !result.empty();
}

bool ReviewRepository::reopen_import_record(
    const std::string& import_record_id,
    const std::string& scope,
    const std::string& username
) {
    const auto result = db_client_->execSqlSync(
        "update import_records "
        "set import_status = '待校对', reopened_at = now(), reopened_by_username = $2, "
        "    reopen_scope = $3, reopen_backup_parsed_result_json = parsed_result_json, "
        "    updated_at = now() "
        "where id = $1::uuid and import_status = '已确认' "
        "returning id",
        import_record_id,
        username,
        scope
    );
    return !result.empty();
}

bool ReviewRepository::restore_reopened_import_record(
    const std::string& import_record_id,
    const std::optional<EditLockCredentials>& edit_lock
) {
    // coalesce 兜底：备份列理论上在重开态必非空（reopen 时同步快照），
    // 万一为空则保留现草稿，宁可多显示修改也不清空数据。
    //
    // 来源 JSON 与关系态快照必须同事务还原（§8.8）。只回滚其中一半时，病害文字回到
    // 确认时的样子、绑定却停在重开期间改成的样子，两半各自看着都对。
    const std::string update_sql =
        "update import_records set import_status = '已确认', "
        "parsed_result_json = coalesce(reopen_backup_parsed_result_json, parsed_result_json), "
        "reopened_at = null, reopened_by_username = null, reopen_scope = null, "
        "reopen_backup_parsed_result_json = null, updated_at = now() ";
    std::shared_ptr<drogon::orm::Transaction> tx;
    auto latch = std::make_shared<CommitLatch>();
    try {
        tx = db_client_->newTransaction(latch->callback());
        const auto result = edit_lock.has_value()
            ? tx->execSqlSync(
                "with updated as (" + update_sql +
                "  where id = $1::uuid and import_status = '待校对' and reopened_at is not null "
                "  and exists(select 1 from import_record_edit_locks l "
                "    where l.import_record_id = import_records.id and l.user_id = $2::uuid "
                "    and l.user_session_id = $3::uuid and l.lock_token_hash = $4 and l.expires_at > now()) "
                "  returning id"
                ") delete from import_record_edit_locks l using updated u "
                "where l.import_record_id = u.id and l.user_id = $2::uuid and l.user_session_id = $3::uuid "
                "and l.lock_token_hash = $4 returning l.import_record_id",
                import_record_id, edit_lock->user_id, edit_lock->session_id,
                auth::sha256_hex(edit_lock->lock_token))
            : tx->execSqlSync(
                update_sql +
                "where id = $1::uuid and import_status = '待校对' and reopened_at is not null returning id",
                import_record_id);
        if (result.empty()) {
            tx->rollback();
            tx.reset();
            return false;
        }
        const auto restored = resolution::restore_reopen_snapshot(
            tx, import_record_id, edit_lock.has_value() ? edit_lock->user_id : std::string{});
        if (!restored.success) {
            tx->rollback();
            tx.reset();
            return false;
        }
        tx.reset();
        return latch->wait();
    } catch (const std::exception&) {
        if (tx) {
            tx->rollback();
            tx.reset();
        }
        throw;
    }
}

bool ReviewRepository::has_current_annual_facts(const std::string& bridge_id, int inspection_year) {
    const auto result = db_client_->execSqlSync(
        "select exists("
        "select 1 from inspection_years "
        "where bridge_id = $1::uuid and inspection_year = $2 and is_current and status = '已确认'"
        ") as found",
        bridge_id,
        inspection_year
    );
    return !result.empty() && result[0]["found"].as<bool>();
}

ConfirmOutcome ReviewRepository::confirm_annual_facts(
    const std::string& import_record_id,
    bool confirm_revision,
    const std::string& confirmation_note,
    const std::string& confirmed_by_user_id,
    const std::optional<EditLockCredentials>& edit_lock
) {
    // db_client_ 必须是裸 DbClient（不能已经是另一个 Transaction）——newTransaction()
    // 在一个 Transaction 上调用不构成合法的嵌套事务，调用方（路由层 / 测试）需保证这一点。
    std::shared_ptr<drogon::orm::Transaction> tx;
    const auto latch = std::make_shared<CommitLatch>();

    // 统一的失败出口：显式回滚后返回携带 code/message 的失败结果。
    // tx 可能在 newTransaction() 本身抛出时仍为空，因此判空后才回滚。
    const auto fail = [&](std::string code, std::string message) -> ConfirmOutcome {
        if (tx != nullptr) {
            try { tx->rollback(); }
            catch (...) {
            }
        }
        ConfirmOutcome failed;
        failed.success = false;
        failed.error_code = std::move(code);
        failed.error_message = std::move(message);
        return failed;
    };

    try {
        tx = db_client_->newTransaction(latch->callback());

        // 步骤 1：select ... for update 重新读取导入记录，杜绝 preflight 之后、confirm 之前
        // 状态被并发改变（如已被取消/已被另一次 confirm 确认）的 TOCTOU 竞态。
        const auto record_result = tx->execSqlSync(
            "select ir.import_status, ir.bridge_id::text as bridge_id, "
            "ir.inspection_year_id::text as inspection_year_id, ir.parsed_result_json::text as parsed_result_json, "
            "ir.system_number as import_number, b.system_number as bridge_number, iy.inspection_year, "
            "iy.bridge_id::text as inspection_year_bridge_id,"
            "iy.standard_profile_id::text as standard_profile_id,"
            "psp.rating_tree_version_id::text as rating_tree_version_id,"
            "psp.technical_condition_package_id::text as technical_package_id,"
            "iy.component_inventory_revision_id::text as inventory_revision_id "
            "from import_records ir join bridges b on b.id = ir.bridge_id "
            "left join inspection_years iy on iy.id = ir.inspection_year_id "
            "left join project_standard_profiles psp on psp.id=iy.standard_profile_id "
            "where ir.id = $1::uuid for update of ir",
            import_record_id
        );
        if (record_result.empty()) {
            return fail("import_record_wrong_status", "导入记录不存在。");
        }

        const auto& record_row = record_result[0];
        const auto import_status = record_row["import_status"].as<std::string>();
        if (import_status != "待校对") {
            return fail(
                "import_record_wrong_status",
                "导入记录当前状态为「" + import_status + "」，不是待校对，无法入库。"
            );
        }
        if (edit_lock.has_value()) {
            const auto lock_result = tx->execSqlSync(
                "select exists(select 1 from import_record_edit_locks "
                "where import_record_id = $1::uuid and user_id = $2::uuid and user_session_id = $3::uuid "
                "and lock_token_hash = $4 and expires_at > now()) as active",
                import_record_id,
                edit_lock->user_id,
                edit_lock->session_id,
                auth::sha256_hex(edit_lock->lock_token)
            );
            if (lock_result.empty() || !lock_result[0]["active"].as<bool>()) {
                return fail("edit_lock_invalid", "编辑锁已失效，确认入库事务已回滚。");
            }
        }
        const auto bridge_id = record_row["bridge_id"].as<std::string>();
        const auto existing_inspection_year_id = optional_text(record_row, "inspection_year_id");
        if (existing_inspection_year_id.has_value()
            && (record_row["inspection_year_bridge_id"].isNull()
                || record_row["inspection_year_bridge_id"].as<std::string>() != bridge_id)) {
            review::PreflightReport report;
            report.blocking_errors.push_back({
                "inspection_year_bridge_mismatch", "导入记录挂载的检测年度不属于当前桥梁。", std::string()});
            report.can_confirm = false;
            auto failed = fail("preflight_failed", "导入记录年度关联异常。");
            failed.preflight_details = report.to_json();
            return failed;
        }

        const auto data = parse_json_strict(record_row["parsed_result_json"].as<std::string>());
        const auto contract_result =
            contracts::validate_bridge_annual_inspection_data(data);
        if (!contract_result.ok()) {
            review::PreflightReport report;
            report.blocking_errors.push_back({"contract_validation_failed", contract_result.summary(), std::string()});
            report.can_confirm = false;
            auto failed = fail("preflight_failed", "最新草稿未通过契约校验。");
            failed.preflight_details = report.to_json();
            return failed;
        }

        std::optional<int> inspection_year;
        if (!record_row["inspection_year"].isNull()) inspection_year = record_row["inspection_year"].as<int>();
        else if (data["inspection"]["inspection_year"].isNumeric()) inspection_year = data["inspection"]["inspection_year"].asInt();
        if (!inspection_year.has_value()) {
            review::PreflightReport report;
            report.blocking_errors.push_back({
                "effective_inspection_year_unresolved", "无法从最新草稿解析检测年度。", std::string()});
            report.can_confirm = false;
            auto failed = fail("preflight_failed", "无法从最新草稿解析检测年度。");
            failed.preflight_details = report.to_json();
            return failed;
        }

        // 锁顺序 import_records → inspection_years，与绑定和 Word 导入一致。
        // 上面那句只 for update of ir：年度字段是在**没有年度行锁**的情况下读的，而两条
        // 导入记录可以关联同一个年度，最后那句写年度又没有版本条件，足以互相覆盖。
        //
        // 解析与锁定都必须早于 build_preflight_report()：preflight 一旦判出
        // "台账未确认"就直接返回，锁在评定服务之前根本走不到。
        std::optional<std::string> locked_revision_id;
        if (existing_inspection_year_id.has_value()) {
            const auto year_row = tx->execSqlSync(
                "select component_inventory_revision_id::text as inventory_revision_id "
                "from inspection_years where id=$1::uuid for update",
                *existing_inspection_year_id);
            if (!year_row.empty() && !year_row[0]["inventory_revision_id"].isNull()) {
                locked_revision_id = year_row[0]["inventory_revision_id"].as<std::string>();
            }
        }
        // 仓库对象一律用临时量：它按值持有 DbClientPtr，留成具名变量会让事务活过
        // tx.reset()，提交回调永远不来。
        const auto resolved_revision = ComponentInventoryRepository(tx)
                                           .resolve_confirmed_revision(bridge_id, locked_revision_id);
        // 年度还没锁版本、而桥上有可用的已确认台账 → 就在本事务里锁上。解析不出版本时
        // 不锁，preflight 会照旧以"台账未确认"挡住，与改动前一致。
        if (existing_inspection_year_id.has_value() && !locked_revision_id.has_value()
            && resolved_revision.has_value()) {
            if (!ComponentInventoryRepository(tx).lock_pending_year_revision(
                    existing_inspection_year_id, bridge_id, std::nullopt,
                    resolved_revision->id)) {
                return fail(
                    "component_inventory_revision_changed",
                    "检测年度的构件台账版本已被其他操作锁定，请刷新后重试。");
            }
        }

        const auto current = tx->execSqlSync(
            "select exists(select 1 from inspection_years where bridge_id = $1::uuid and inspection_year = $2 "
            "and is_current and status = '已确认') as found", bridge_id, *inspection_year);
        review::PreflightContext context;
        context.import_status = import_status;
        context.record_system_number = record_row["import_number"].as<std::string>();
        context.bridge_system_number = record_row["bridge_number"].as<std::string>();
        context.inspection_year = inspection_year;
        context.has_current_annual_facts = !current.empty() && current[0]["found"].as<bool>();
        // 该解析器按定义只返回已确认版本，所以"是否已确认"就是它有没有值；
        // 不必再单独查一次 status。
        context.component_inventory_revision_id = resolved_revision.has_value()
            ? std::optional<std::string>(resolved_revision->id) : std::nullopt;
        context.component_inventory_confirmed = resolved_revision.has_value();
        // 5.0：预检与写计划都不再从病害 JSON 读解析字段。可确认病害视图把来源事实
        // 与关系表里的解析状态组合起来，两者在同一事务里读，看到的是同一份快照。
        const auto confirmable_view =
            resolution::build_confirmable_view(tx, import_record_id, data);
        const auto preflight =
            review::build_preflight_report(data, confirmable_view, context);
        if (!preflight.can_confirm) {
            auto failed = fail("preflight_failed", "最新草稿未通过入库前检查。");
            failed.preflight_details = preflight.to_json();
            return failed;
        }
        if (preflight.requires_revision_confirmation && !confirm_revision) {
            return fail("revision_confirmation_required", "同桥同年已有当前有效事实，需显式确认修订版。");
        }

        const auto rating_tree_version_id =
            optional_text(record_row, "rating_tree_version_id");
        const auto technical_package_id =
            optional_text(record_row, "technical_package_id");
        if (rating_tree_version_id.has_value() &&
            technical_package_id.has_value()) {
            RatingTreeRepository tree_repository(tx);
            const auto tree =
                tree_repository.load_published_tree(*rating_tree_version_id);
            if (!tree.has_value()) {
                review::PreflightReport report = preflight;
                report.blocking_errors.push_back({
                    "rating_tree_unavailable",
                    "检测年度锁定的评定树不可用。",
                    *rating_tree_version_id});
                report.can_confirm = false;
                auto failed = fail(
                    "preflight_failed", "检测年度锁定的评定树不可用。");
                failed.preflight_details = report.to_json();
                return failed;
            }
            // 评分树校验也要看可确认视图：节点住在关系表里，草稿里没有它。
            const auto tree_validation =
                review::validate_defect_rating_tree_for_confirmation(
                    confirmable_view,
                    *rating_tree_version_id,
                    *technical_package_id,
                    *tree,
                    resolved_revision);
            if (!tree_validation.ok) {
                review::PreflightReport report = preflight;
                for (const auto& issue : tree_validation.issues) {
                    report.blocking_errors.push_back({
                        tree_validation.code, issue.message, issue.path});
                }
                report.can_confirm = false;
                auto failed = fail(
                    "preflight_failed",
                    "病害评定树关联未通过正式入库校验。");
                failed.preflight_details = report.to_json();
                return failed;
            }
        }

        const auto standard_profile_id = optional_text(record_row, "standard_profile_id");
        if (!existing_inspection_year_id.has_value() || !standard_profile_id.has_value() ||
            !context.component_inventory_revision_id.has_value()) {
            review::PreflightReport report = preflight;
            report.blocking_errors.push_back({
                "assessment_context_incomplete",
                "检测年度尚未锁定规范组合和已确认构件台账。",
                std::string()});
            report.can_confirm = false;
            auto failed = fail("preflight_failed", "系统评定上下文不完整。");
            failed.preflight_details = report.to_json();
            return failed;
        }

        assessment::AssessmentConfirmationOutcome assessment_outcome;
        {
            assessment::AssessmentConfirmationService assessment_service(
                tx, standard_registry_);
            // 评定输入同样按可确认视图：它读的是绑定构件与评分树节点，那两样都在
            // 关系表里；给来源草稿的话算出来的是"一条病害都没绑构件"。
            assessment_outcome = assessment_service.calculate(
                *existing_inspection_year_id, confirmable_view);
        }
        if (assessment_outcome.status != assessment::AssessmentConfirmationStatus::Completed) {
            review::PreflightReport report = preflight;
            for (const auto& item : assessment_outcome.preview.issues) {
                report.blocking_errors.push_back(
                    {item.code, item.message, item.entity_id});
            }
            report.can_confirm = false;
            auto failed = fail("preflight_failed", "系统自主评定未通过。");
            failed.preflight_details = report.to_json();
            return failed;
        }

        const auto plan = review::build_confirm_plan(confirmable_view);
        std::unordered_map<std::string, std::string> archived_file_id_by_photo_candidate;
        archived_file_id_by_photo_candidate.reserve(plan.photos.size());
        for (const auto& photo : plan.photos) {
            const auto archived = tx->execSqlSync(
                "select af.id::text as id from import_record_files irf "
                "join archived_files af on af.id = irf.archived_file_id and af.file_type = '图片' "
                "where irf.import_record_id = $1::uuid and irf.file_role = '附件' "
                "and irf.process_status = '处理成功' and af.storage_relative_path = $2 limit 1",
                import_record_id, photo.archive_relative_path);
            if (archived.empty()) {
                review::PreflightReport report;
                report.blocking_errors.push_back({
                    "photo_archive_missing",
                    "照片候选 " + photo.candidate_id + " 的归档文件未关联到当前导入记录。",
                    photo.candidate_id});
                report.can_confirm = false;
                auto failed = fail("preflight_failed", "照片归档关联缺失。");
                failed.preflight_details = report.to_json();
                return failed;
            }
            archived_file_id_by_photo_candidate.emplace(
                photo.candidate_id, archived[0]["id"].as<std::string>());
        }

        // 步骤 2/3：解析目标年度行（含修订判定与降级/新建）。
        const auto target_year_id_opt = resolve_target_inspection_year_id(
            tx, bridge_id, *inspection_year, existing_inspection_year_id,
            confirm_revision, *standard_profile_id,
            *context.component_inventory_revision_id
        );
        if (!target_year_id_opt.has_value()) {
            return fail("revision_confirmation_required", "同桥同年已有当前有效事实，需显式确认修订版。");
        }
        const auto& target_year_id = *target_year_id_opt;

        // 步骤 4：终态化目标年度行；RETURNING 顺带拿到 version_number，
        // 无论 target_year_id 来自“复用已挂载行”“新建占位行”还是“修订新建行”都统一在这一步收口。
        const auto year_update_result = tx->execSqlSync(
            "update inspection_years "
            "set status = '已确认', is_current = true, overall_score = $2, overall_grade = $3, "
            "component_inventory_revision_id=nullif($4,'')::uuid, updated_at = now() "
            "where id = $1::uuid "
            "returning version_number",
            target_year_id,
            assessment_outcome.preview.result->overall_score,
            std::to_string(assessment_outcome.preview.result->final_grade) + "类",
            context.component_inventory_revision_id.value_or("")
        );
        if (year_update_result.empty()) {
            throw std::runtime_error("confirm_annual_facts: inspection_years row vanished: " + target_year_id);
        }
        const auto version_number = year_update_result[0]["version_number"].as<int>();

        // 步骤 5：构件 upsert，按 normalized_component_key 建立到 bridge_component_id 的映射，
        // 供下面的病害插入回填外键。
        std::unordered_map<std::string, UpsertedComponent> component_by_key;
        component_by_key.reserve(plan.components.size());
        for (const auto& component : plan.components) {
            component_by_key.emplace(component.normalized_component_key, upsert_component(tx, bridge_id, component));
        }

        // 步骤 6a：defect_observations + defect_measurements。
        std::unordered_map<std::string, std::string> observation_id_by_candidate_id;
        observation_id_by_candidate_id.reserve(plan.defects.size());
        int written_defect_observations = 0;
        int written_defect_measurements = 0;
        for (const auto& defect : plan.defects) {
            const auto component_it = component_by_key.find(defect.component_key);
            if (component_it == component_by_key.end()) {
                // 不应发生：build_confirm_plan 保证每个进入计划的病害都有对应的构件条目。
                throw std::runtime_error(
                    "confirm_annual_facts: component_key not found in plan: " + defect.component_key
                );
            }

            const auto observation_id =
                insert_defect_observation(tx, target_year_id, bridge_id, import_record_id, component_it->second, defect);
            observation_id_by_candidate_id.emplace(defect.candidate_id, observation_id);
            ++written_defect_observations;

            for (const auto& measurement : defect.measurements) {
                insert_defect_measurement(tx, observation_id, measurement);
                ++written_defect_measurements;
            }
        }

        // 步骤 6b：defect_photos，经候选 id 映射定位所属病害观测行。
        int written_defect_photos = 0;
        for (const auto& photo : plan.photos) {
            const auto observation_it = observation_id_by_candidate_id.find(photo.defect_candidate_id);
            if (observation_it == observation_id_by_candidate_id.end()) {
                // 不应发生：build_confirm_plan 保证照片只在其关联病害也进入计划时才进入计划。
                throw std::runtime_error(
                    "confirm_annual_facts: defect_candidate_id not found in plan: " + photo.defect_candidate_id
                );
            }
            const auto archived = archived_file_id_by_photo_candidate.find(photo.candidate_id);
            if (archived == archived_file_id_by_photo_candidate.end()) {
                throw std::runtime_error("confirm_annual_facts: archived photo id missing from resolved plan");
            }
            insert_defect_photo(
                tx, observation_it->second, archived->second, import_record_id, photo);
            ++written_defect_photos;
        }

        // 步骤 6c：保存系统正式评定、各级结果、控制、轨迹和档案查询投影。
        assessment::AssessmentConfirmationWritten assessment_written;
        {
            assessment::AssessmentConfirmationService assessment_service(
                tx, standard_registry_);
            assessment_written = assessment_service.persist(
                assessment_outcome.preview,
                target_year_id,
                import_record_id,
                confirmed_by_user_id);
        }
        const int written_condition_ratings =
            assessment_written.condition_rating_projections;

        // 步骤 7：导入记录终态化。
        Json::Value written_json;
        written_json["defect_observations"] = written_defect_observations;
        written_json["defect_measurements"] = written_defect_measurements;
        written_json["defect_photos"] = written_defect_photos;
        written_json["condition_ratings"] = written_condition_ratings;
        written_json["assessment_run_id"] = assessment_written.assessment_run_id;
        written_json["assessment_component_results"] = assessment_written.component_results;
        written_json["assessment_part_results"] = assessment_written.part_results;
        written_json["assessment_control_results"] = assessment_written.control_results;
        written_json["assessment_rule_traces"] = assessment_written.rule_traces;

        // 重开列一并清空：重开后的再确认（修订版）完成即退出重开态，备份快照不再需要。
        tx->execSqlSync(
            "update import_records "
            "set import_status = '已确认', finished_at = now(), inspection_year_id = $2::uuid, "
            "    validation_result_json = jsonb_build_object("
            "        'confirmed_at', now(), 'confirmation_note', $3::text, 'written', $4::jsonb"
            "    ) || case when jsonb_typeof(validation_result_json->'draft_audit_events') = 'array' "
            "        then jsonb_build_object('draft_audit_events', validation_result_json->'draft_audit_events') "
            "        else '{}'::jsonb end, "
            "    reopened_at = null, reopened_by_username = null, reopen_scope = null, "
            "    reopen_backup_parsed_result_json = null, "
            "    updated_at = now() "
            "where id = $1::uuid",
            import_record_id,
            target_year_id,
            confirmation_note,
            write_compact_json(written_json)
        );

        // 重新确认成功也是一个世代边界（§8.8）：删掉关系态快照，并把重开期间生成、
        // 还没执行的计划一律作废——它们预览的是已经不存在的那一版状态。
        resolution::discard_reopen_snapshot(tx, import_record_id, confirmed_by_user_id);

        // 步骤 7 收尾：清理被遗弃的挂载占位年度行。
        // 场景：导入记录原本挂在占位年度行 X（待校对、is_current=false）；同桥同年的另一条导入先被
        // 确认、建立了当前有效行 Y；本次确认走修订分支新建了行 Z 并把记录改指向 Z（上面的 update）——
        // 此时 X 已无人引用，若不删除会永久沉积为死数据。仅当原挂载行确实是"另一行、非当前、待校对"
        // 的占位行时才删除；谓词收得很紧（is_current=false AND status='待校对' AND id=旧挂载id AND
        // id<>新目标id），保证绝不会误删真正的已确认/当前/已被修订行。
        // 非修订/复用挂载行的场景下 target_year_id == existing_inspection_year_id，id<>新目标id 直接
        // 落空，本语句为无操作。
        if (existing_inspection_year_id.has_value() && *existing_inspection_year_id != target_year_id) {
            tx->execSqlSync(
                "delete from inspection_years "
                "where id = $1::uuid and id <> $2::uuid and is_current = false and status = '待校对'",
                *existing_inspection_year_id,
                target_year_id
            );
        }

        if (edit_lock.has_value()) {
            const auto released = tx->execSqlSync(
                "delete from import_record_edit_locks "
                "where import_record_id = $1::uuid and user_id = $2::uuid and user_session_id = $3::uuid "
                "and lock_token_hash = $4 returning import_record_id",
                import_record_id,
                edit_lock->user_id,
                edit_lock->session_id,
                auth::sha256_hex(edit_lock->lock_token)
            );
            if (released.empty()) {
                return fail("edit_lock_invalid", "编辑锁在确认入库期间失效，事务已回滚。");
            }
        }

        ConfirmOutcome outcome;
        outcome.inspection_year_id = target_year_id;
        outcome.version_number = version_number;
        outcome.assessment_run_id = assessment_written.assessment_run_id;
        outcome.written.defect_observations = written_defect_observations;
        outcome.written.defect_measurements = written_defect_measurements;
        outcome.written.defect_photos = written_defect_photos;
        outcome.written.condition_ratings = written_condition_ratings;
        outcome.written.assessment_component_results = assessment_written.component_results;
        outcome.written.assessment_part_results = assessment_written.part_results;
        outcome.written.assessment_control_results = assessment_written.control_results;
        outcome.written.assessment_rule_traces = assessment_written.rule_traces;
        tx.reset();
        if (!latch->wait()) {
            outcome.success = false;
            outcome.error_code = "database_commit_failed";
            outcome.error_message = "数据库提交失败。";
            return outcome;
        }
        outcome.success = true;
        return outcome;
    } catch (const drogon::orm::DrogonDbException& exception) {
        return fail("db_write_failed", exception.base().what());
    } catch (const std::exception& exception) {
        return fail("db_write_failed", exception.what());
    }
}

}  // 命名空间 bridge_report::db
