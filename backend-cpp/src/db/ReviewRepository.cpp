#include "bridge_report/db/ReviewRepository.hpp"

#include <optional>
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

std::string build_raw_cells_json(const std::optional<std::string>& raw_row_text) {
    Json::Value json(Json::objectValue);
    if (raw_row_text.has_value()) {
        json["raw_row_text"] = *raw_row_text;
    }
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
 * version_number=1 占位行会在步骤 3 另建新行后变成永久孤儿（修订分支不回滚，占位行插入
 * 会随事务一起提交）。调整顺序后行为对规格列出的四个测试场景完全一致（inspection_year_id
 * 已挂载时两种顺序均不会在步骤 2/3 产生插入），仅在“未挂载 + 需要修订”这一未覆盖场景下
 * 避免产生垃圾行，因此认为是更安全的实现选择。
 *
 * @return 目标年度行 id；若存在修订冲突且调用方未确认修订，返回 std::nullopt
 * （调用方据此直接回滚并返回 revision_confirmation_required，不再插入任何行）。
 */
std::optional<std::string> resolve_target_inspection_year_id(
    const TransactionPtr& tx,
    const std::string& bridge_id,
    int inspection_year,
    const std::optional<std::string>& existing_inspection_year_id,
    bool confirm_revision
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
            "(bridge_id, inspection_year, version_number, status, is_current, revision_source_inspection_id) "
            "values ($1::uuid, $2, $3, '待校对', true, $4::uuid) "
            "returning id",
            bridge_id,
            inspection_year,
            current_version + 1,
            current_year_id
        );
        return inserted[0]["id"].as<std::string>();
    }

    if (existing_inspection_year_id.has_value()) {
        return *existing_inspection_year_id;
    }

    const auto inserted = tx->execSqlSync(
        "insert into inspection_years (bridge_id, inspection_year, version_number, status, is_current) "
        "values ($1::uuid, $2, 1, '待校对', false) "
        "returning id",
        bridge_id,
        inspection_year
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
        " defect_location, defect_type, defect_description_raw, scale, "
        " extraction_confidence, review_status, review_note) "
        "values ($1::uuid, $2::uuid, $3::uuid, $4::uuid, "
        "        $5, $6, $7, $8::jsonb, "
        "        $9, $10, $11, $12, "
        "        $13, $14, $15, $16, "
        "        $17, $18, $19) "
        "returning id",
        inspection_year_id,
        bridge_id,
        component.bridge_component_id,
        import_record_id,
        defect.source_table_title,
        defect.source_table_index,
        defect.source_row_number,
        build_raw_cells_json(defect.raw_row_text),
        defect.structure_part,
        defect.part_name,
        component.component_type,
        component.business_component_code,
        defect.defect_location,
        defect.defect_type,
        defect.defect_description_raw,
        defect.scale,
        defect.extraction_confidence,
        defect.review_status,
        defect.review_note
    );
    return result[0]["id"].as<std::string>();
}

void insert_defect_measurement(
    const TransactionPtr& tx, const std::string& defect_observation_id, const review::MeasurementPlan& measurement
) {
    tx->execSqlSync(
        "insert into defect_measurements "
        "(defect_observation_id, measurement_type, numeric_value, unit, raw_text, is_auto_parsed, is_manually_confirmed) "
        "values ($1::uuid, $2, $3, $4, $5, $6, true)",
        defect_observation_id,
        measurement.measurement_type,
        measurement.numeric_value,
        measurement.unit,
        measurement.raw_text,
        measurement.is_auto_parsed
    );
}

void insert_defect_photo(
    const TransactionPtr& tx,
    const std::string& defect_observation_id,
    const std::string& import_record_id,
    const review::PhotoPlan& photo
) {
    tx->execSqlSync(
        "insert into defect_photos "
        "(defect_observation_id, source_import_record_id, photo_number, photo_title, match_status, archived_file_id) "
        "values ($1::uuid, $2::uuid, $3, $4, '已确认', null)",
        defect_observation_id,
        import_record_id,
        photo.photo_number,
        photo.photo_title
    );
}

void insert_condition_rating(
    const TransactionPtr& tx,
    const std::string& inspection_year_id,
    const std::string& import_record_id,
    const review::RatingPlan& rating
) {
    tx->execSqlSync(
        "insert into condition_ratings "
        "(inspection_year_id, source_import_record_id, rating_level, structure_part, rating_item_name, "
        " score, grade, weight, remarks, review_status) "
        "values ($1::uuid, $2::uuid, $3, $4, $5, $6, $7, $8, $9, $10)",
        inspection_year_id,
        import_record_id,
        rating.rating_level,
        rating.structure_part,
        rating.rating_item_name,
        rating.score,
        rating.grade,
        rating.weight,
        rating.remarks,
        rating.review_status
    );
}

}  // 匿名命名空间

ReviewRepository::ReviewRepository(drogon::orm::DbClientPtr db_client) : db_client_(std::move(db_client)) {}

std::vector<review::BridgeSummary> ReviewRepository::list_bridges() {
    const auto result = db_client_->execSqlSync(
        "select id, system_number, bridge_name, route_name, status "
        "from bridges "
        "order by system_number"
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
        "select id, system_number, import_name, source_type, import_status, "
        "inspection_year_id, importer_name, created_at::text "
        "from import_records "
        "where bridge_id = $1::uuid "
        "order by created_at desc",
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
        "ir.import_name, ir.source_type, ir.import_status, "
        "ir.importer_name, ir.importer_version, ir.parsed_result_json::text as parsed_result_json, "
        "ir.created_at::text as created_at, ir.updated_at::text as updated_at, "
        "b.system_number as bridge_system_number, b.bridge_name as bridge_name, b.route_name as bridge_route_name, "
        "iy.system_number as inspection_year_system_number, iy.inspection_year as inspection_year, "
        "iy.status as inspection_year_status, iy.version_number as inspection_year_version_number, "
        "iy.is_current as inspection_year_is_current "
        "from import_records ir "
        "join bridges b on b.id = ir.bridge_id "
        "left join inspection_years iy on iy.id = ir.inspection_year_id "
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
    detail.importer_name = optional_text(row, "importer_name");
    detail.importer_version = optional_text(row, "importer_version");
    detail.parsed_result_json = row["parsed_result_json"].as<std::string>();
    detail.created_at = row["created_at"].as<std::string>();
    detail.updated_at = row["updated_at"].as<std::string>();

    detail.bridge_system_number = row["bridge_system_number"].as<std::string>();
    detail.bridge_name = row["bridge_name"].as<std::string>();
    detail.bridge_route_name = optional_text(row, "bridge_route_name");

    detail.inspection_year_system_number = optional_text(row, "inspection_year_system_number");
    const auto inspection_year_field = row["inspection_year"];
    detail.inspection_year = inspection_year_field.isNull()
        ? std::nullopt
        : std::make_optional(inspection_year_field.as<int>());
    detail.inspection_year_status = optional_text(row, "inspection_year_status");
    const auto version_number_field = row["inspection_year_version_number"];
    detail.inspection_year_version_number = version_number_field.isNull()
        ? std::nullopt
        : std::make_optional(version_number_field.as<int>());
    const auto is_current_field = row["inspection_year_is_current"];
    detail.inspection_year_is_current = is_current_field.isNull()
        ? std::nullopt
        : std::make_optional(is_current_field.as<bool>());

    return detail;
}

bool ReviewRepository::save_review_draft(const std::string& import_record_id, const std::string& parsed_json_text) {
    // 与 cancel_import_record 同一惯用法：把状态谓词放进 UPDATE，
    // 避免“处理器读到待校对 -> 并发取消/确认 -> 草稿仍写入”的 TOCTOU 竞态。
    const auto result = db_client_->execSqlSync(
        "update import_records "
        "set parsed_result_json = $2::jsonb, updated_at = now() "
        "where id = $1::uuid "
        "and import_status = '待校对' "
        "returning id",
        import_record_id,
        parsed_json_text
    );
    return !result.empty();
}

bool ReviewRepository::cancel_import_record(const std::string& import_record_id) {
    const auto result = db_client_->execSqlSync(
        "update import_records "
        "set import_status = '已取消', updated_at = now() "
        "where id = $1::uuid "
        "and import_status in ('已上传', '解析中', '待校对', '解析失败') "
        "returning id",
        import_record_id
    );
    return !result.empty();
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
    const review::ConfirmPlan& plan,
    int inspection_year,
    bool confirm_revision,
    const std::string& confirmation_note
) {
    // db_client_ 必须是裸 DbClient（不能已经是另一个 Transaction）——newTransaction()
    // 在一个 Transaction 上调用不构成合法的嵌套事务，调用方（路由层 / 测试）需保证这一点。
    std::shared_ptr<drogon::orm::Transaction> tx;

    // 统一的失败出口：显式回滚后返回携带 code/message 的失败结果。
    // tx 可能在 newTransaction() 本身抛出时仍为空，因此判空后才回滚。
    const auto fail = [&](std::string code, std::string message) -> ConfirmOutcome {
        if (tx != nullptr) {
            tx->rollback();
        }
        ConfirmOutcome failed;
        failed.success = false;
        failed.error_code = std::move(code);
        failed.error_message = std::move(message);
        return failed;
    };

    try {
        tx = db_client_->newTransaction();

        // 步骤 1：select ... for update 重新读取导入记录，杜绝 preflight 之后、confirm 之前
        // 状态被并发改变（如已被取消/已被另一次 confirm 确认）的 TOCTOU 竞态。
        const auto record_result = tx->execSqlSync(
            "select import_status, bridge_id, inspection_year_id "
            "from import_records where id = $1::uuid for update",
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
        const auto bridge_id = record_row["bridge_id"].as<std::string>();
        const auto existing_inspection_year_id = optional_text(record_row, "inspection_year_id");

        // 步骤 2/3：解析目标年度行（含修订判定与降级/新建）。
        const auto target_year_id_opt = resolve_target_inspection_year_id(
            tx, bridge_id, inspection_year, existing_inspection_year_id, confirm_revision
        );
        if (!target_year_id_opt.has_value()) {
            return fail("revision_confirmation_required", "同桥同年已有当前有效事实，需显式确认修订版。");
        }
        const auto& target_year_id = *target_year_id_opt;

        // 步骤 4：终态化目标年度行；RETURNING 顺带拿到 version_number，
        // 无论 target_year_id 来自“复用已挂载行”“新建占位行”还是“修订新建行”都统一在这一步收口。
        const auto year_update_result = tx->execSqlSync(
            "update inspection_years "
            "set status = '已确认', is_current = true, overall_score = $2, overall_grade = $3, updated_at = now() "
            "where id = $1::uuid "
            "returning version_number",
            target_year_id,
            plan.overall_score,
            plan.overall_grade
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
            insert_defect_photo(tx, observation_it->second, import_record_id, photo);
            ++written_defect_photos;
        }

        // 步骤 6c：condition_ratings。
        int written_condition_ratings = 0;
        for (const auto& rating : plan.ratings) {
            insert_condition_rating(tx, target_year_id, import_record_id, rating);
            ++written_condition_ratings;
        }

        // 步骤 7：导入记录终态化。
        Json::Value written_json;
        written_json["defect_observations"] = written_defect_observations;
        written_json["defect_measurements"] = written_defect_measurements;
        written_json["defect_photos"] = written_defect_photos;
        written_json["condition_ratings"] = written_condition_ratings;

        tx->execSqlSync(
            "update import_records "
            "set import_status = '已确认', finished_at = now(), inspection_year_id = $2::uuid, "
            "    validation_result_json = jsonb_build_object("
            "        'confirmed_at', now(), 'confirmation_note', $3::text, 'written', $4::jsonb"
            "    ), "
            "    updated_at = now() "
            "where id = $1::uuid",
            import_record_id,
            target_year_id,
            confirmation_note,
            write_compact_json(written_json)
        );

        // 未显式调用 rollback()：tx 离开作用域时析构提交事务。
        ConfirmOutcome outcome;
        outcome.success = true;
        outcome.inspection_year_id = target_year_id;
        outcome.version_number = version_number;
        outcome.written.defect_observations = written_defect_observations;
        outcome.written.defect_measurements = written_defect_measurements;
        outcome.written.defect_photos = written_defect_photos;
        outcome.written.condition_ratings = written_condition_ratings;
        return outcome;
    } catch (const drogon::orm::DrogonDbException& exception) {
        return fail("db_write_failed", exception.base().what());
    } catch (const std::exception& exception) {
        return fail("db_write_failed", exception.what());
    }
}

}  // 命名空间 bridge_report::db
