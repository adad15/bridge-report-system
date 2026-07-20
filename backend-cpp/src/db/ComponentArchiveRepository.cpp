#include "bridge_report/db/ComponentArchiveRepository.hpp"

#include <sstream>
#include <unordered_map>
#include <utility>

#include <drogon/orm/Exception.h>
#include <drogon/orm/Result.h>

namespace bridge_report::db {

namespace {

// 当前有效事实口径（模块 06 全部默认查询统一使用）：
// 年度版本 is_current 且 已确认；观测 review_status 已确认/已修改。
constexpr const char* kCurrentYearJoin =
    "join inspection_years iy on iy.id = o.inspection_year_id "
    "and iy.is_current and iy.status = '已确认' ";
constexpr const char* kSettledObservation = "o.review_status in ('已确认', '已修改') ";

Json::Value parse_json_or_default(const std::string& text, Json::Value fallback) {
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;
    std::istringstream stream(text);
    if (!Json::parseFromStream(builder, stream, &root, &errors)) {
        return fallback;
    }
    return root;
}

template <typename Row>
Json::Value nullable_string(const Row& row, const char* column) {
    if (row[column].isNull()) {
        return Json::Value(Json::nullValue);
    }
    return Json::Value(row[column].template as<std::string>());
}

template <typename Row>
Json::Value nullable_double(const Row& row, const char* column) {
    if (row[column].isNull()) {
        return Json::Value(Json::nullValue);
    }
    return Json::Value(row[column].template as<double>());
}

template <typename Row>
Json::Value nullable_int(const Row& row, const char* column) {
    if (row[column].isNull()) {
        return Json::Value(Json::nullValue);
    }
    return Json::Value(row[column].template as<int>());
}

// 观测行的公共 JSON 形状：档案、修订与未绑定三个入口共用，保证字段一致。
template <typename Row>
Json::Value observation_to_json(const Row& row) {
    Json::Value item;
    item["id"] = row["id"].template as<std::string>();
    item["system_number"] = row["system_number"].template as<std::string>();
    item["inspection_year"] = row["inspection_year"].template as<int>();
    item["defect_thread_id"] = nullable_string(row, "defect_thread_id");
    item["defect_type"] = row["defect_type"].template as<std::string>();
    item["defect_location"] = nullable_string(row, "defect_location");
    item["scale"] = nullable_string(row, "scale");
    item["defect_deduction"] = nullable_double(row, "defect_deduction");
    item["defect_description"] = row["defect_description_raw"].template as<std::string>();
    item["review_status"] = row["review_status"].template as<std::string>();
    item["updated_at"] = row["updated_at"].template as<std::string>();
    item["measurements"] = Json::Value(Json::arrayValue);
    item["photos"] = Json::Value(Json::arrayValue);
    return item;
}

}  // namespace

ComponentArchiveRepository::ComponentArchiveRepository(drogon::orm::DbClientPtr db_client)
    : db_client_(std::move(db_client)) {}

Json::Value ComponentArchiveRepository::list_components(const std::string& bridge_id) {
    const auto rows = db_client_->execSqlSync(
        std::string(
            "select bc.id, bc.system_number, bc.structure_part, bc.component_type, bc.business_component_code, "
            "count(distinct o.defect_thread_id) as thread_count, "
            "count(*) filter (where o.defect_thread_id is null) as unbound_count, "
            "min(iy.inspection_year) as first_seen_year, "
            "max(iy.inspection_year) as latest_seen_year "
            "from bridge_components bc "
            "join defect_observations o on o.bridge_component_id = bc.id and ") + kSettledObservation +
            kCurrentYearJoin +
            "where bc.bridge_id = $1::uuid "
            "group by bc.id, bc.system_number, bc.structure_part, bc.component_type, bc.business_component_code "
            "order by bc.structure_part, bc.component_type, bc.system_number",
        bridge_id
    );

    // 最新年度的构件评分（如有）：单独查询后按构件 id 合并，避免 N+1。
    const auto score_rows = db_client_->execSqlSync(
        "select distinct on (cr.bridge_component_id) "
        "cr.bridge_component_id, cr.score, iy.inspection_year "
        "from condition_ratings cr "
        "join inspection_years iy on iy.id = cr.inspection_year_id and iy.is_current and iy.status = '已确认' "
        "join bridge_components bc on bc.id = cr.bridge_component_id "
        "where bc.bridge_id = $1::uuid and cr.rating_level = '构件' "
        "and cr.assessment_run_id is not null "
        "order by cr.bridge_component_id, iy.inspection_year desc",
        bridge_id
    );
    std::unordered_map<std::string, std::pair<Json::Value, int>> latest_score_by_component;
    for (const auto& row : score_rows) {
        latest_score_by_component.emplace(
            row["bridge_component_id"].as<std::string>(),
            std::make_pair(nullable_double(row, "score"), row["inspection_year"].as<int>())
        );
    }

    Json::Value components(Json::arrayValue);
    for (const auto& row : rows) {
        Json::Value item;
        const auto component_id = row["id"].as<std::string>();
        item["id"] = component_id;
        item["system_number"] = row["system_number"].as<std::string>();
        item["structure_part"] = row["structure_part"].as<std::string>();
        item["component_type"] = row["component_type"].as<std::string>();
        item["business_component_code"] = row["business_component_code"].as<std::string>();
        item["thread_count"] = row["thread_count"].as<int>();
        item["unbound_count"] = row["unbound_count"].as<int>();
        item["first_seen_year"] = row["first_seen_year"].as<int>();
        item["latest_seen_year"] = row["latest_seen_year"].as<int>();
        const auto score_it = latest_score_by_component.find(component_id);
        if (score_it != latest_score_by_component.end()) {
            item["latest_score"] = score_it->second.first;
            item["latest_score_year"] = score_it->second.second;
        } else {
            item["latest_score"] = Json::Value(Json::nullValue);
            item["latest_score_year"] = Json::Value(Json::nullValue);
        }
        components.append(item);
    }

    Json::Value body;
    body["components"] = components;
    return body;
}

std::optional<Json::Value> ComponentArchiveRepository::get_component(const std::string& component_id) {
    const auto rows = db_client_->execSqlSync(
        "select id, bridge_id, system_number, structure_part, component_type, business_component_code, "
        "current_status "
        "from bridge_components where id = $1::uuid",
        component_id
    );
    if (rows.empty()) {
        return std::nullopt;
    }
    const auto& row = rows[0];
    Json::Value component;
    component["id"] = row["id"].as<std::string>();
    component["bridge_id"] = row["bridge_id"].as<std::string>();
    component["system_number"] = row["system_number"].as<std::string>();
    component["structure_part"] = row["structure_part"].as<std::string>();
    component["component_type"] = row["component_type"].as<std::string>();
    component["business_component_code"] = row["business_component_code"].as<std::string>();
    component["current_status"] = row["current_status"].as<std::string>();
    return component;
}

Json::Value ComponentArchiveRepository::get_defect_archive(const std::string& component_id) {
    const auto component = get_component(component_id);

    // 各当前有效年度的构件级评分；正式系统评定同时返回运行 ID 与永久计算明细。
    const auto rating_rows = db_client_->execSqlSync(
        "select iy.inspection_year, cr.score, cr.source_score, cr.calculated_score, "
        "cr.score_validation_status, cr.score_resolution_reason, "
        "cr.calculation_details_json::text as legacy_calculation_details,"
        "cr.assessment_run_id::text as assessment_run_id,"
        "acr.result_json::text as system_calculation_details "
        "from condition_ratings cr "
        "join inspection_years iy on iy.id = cr.inspection_year_id and iy.is_current and iy.status = '已确认' "
        "left join assessment_component_results acr on acr.assessment_run_id=cr.assessment_run_id "
        "and acr.bridge_component_id=cr.bridge_component_id "
        "where cr.bridge_component_id = $1::uuid and cr.rating_level = '构件' "
        "and cr.assessment_run_id is not null "
        "order by iy.inspection_year desc",
        component_id
    );
    Json::Value ratings(Json::arrayValue);
    for (const auto& row : rating_rows) {
        Json::Value item;
        item["inspection_year"] = row["inspection_year"].as<int>();
        item["score"] = nullable_double(row, "score");
        const bool system_assessment = !row["assessment_run_id"].isNull();
        item["is_system_assessment"] = system_assessment;
        item["assessment_run_id"] = system_assessment
            ? Json::Value(row["assessment_run_id"].as<std::string>())
            : Json::Value(Json::nullValue);
        item["source_score"] = system_assessment
            ? Json::Value(Json::nullValue) : nullable_double(row, "source_score");
        item["calculated_score"] = system_assessment
            ? nullable_double(row, "score") : nullable_double(row, "calculated_score");
        item["score_validation_status"] = system_assessment
            ? Json::Value("系统评定") : nullable_string(row, "score_validation_status");
        item["score_resolution_reason"] = system_assessment
            ? Json::Value(Json::nullValue) : nullable_string(row, "score_resolution_reason");
        const auto details_column = system_assessment
            ? "system_calculation_details" : "legacy_calculation_details";
        item["calculation_details"] = row[details_column].isNull()
            ? Json::Value(Json::objectValue)
            : parse_json_or_default(
                row[details_column].as<std::string>(), Json::Value(Json::objectValue));
        item["has_validation_details"] = system_assessment ||
            !row["score_validation_status"].isNull();
        ratings.append(item);
    }

    // 构件全部线索（含暂无当前观测的线索），附首见/末见年份。
    const auto threads = list_threads_for_component(component_id);

    // 当前有效正式观测，年度倒序；尺寸与照片分两条同谓词查询后合并，避免 N+1。
    const auto observation_rows = db_client_->execSqlSync(
        std::string(
            "select o.id, o.system_number, iy.inspection_year, o.defect_thread_id, o.defect_type, "
            "o.defect_location, o.scale, o.defect_deduction, o.defect_description_raw, o.review_status, "
            "o.updated_at::text as updated_at "
            "from defect_observations o ") + kCurrentYearJoin +
            "where o.bridge_component_id = $1::uuid and " + kSettledObservation +
            "order by iy.inspection_year desc, o.system_number",
        component_id
    );
    Json::Value observations(Json::arrayValue);
    std::unordered_map<std::string, Json::ArrayIndex> observation_index_by_id;
    for (const auto& row : observation_rows) {
        observation_index_by_id.emplace(row["id"].as<std::string>(), observations.size());
        observations.append(observation_to_json(row));
    }

    const auto measurement_rows = db_client_->execSqlSync(
        std::string(
            "select dm.defect_observation_id, dm.measurement_type, dm.value_type, dm.numeric_value, "
            "dm.minimum_value, dm.maximum_value, dm.unit, dm.is_approximate, dm.raw_text "
            "from defect_measurements dm "
            "join defect_observations o on o.id = dm.defect_observation_id ") + kCurrentYearJoin +
            "where o.bridge_component_id = $1::uuid and " + kSettledObservation +
            "order by dm.created_at",
        component_id
    );
    for (const auto& row : measurement_rows) {
        const auto it = observation_index_by_id.find(row["defect_observation_id"].as<std::string>());
        if (it == observation_index_by_id.end()) {
            continue;
        }
        Json::Value measurement;
        measurement["measurement_type"] = row["measurement_type"].as<std::string>();
        measurement["value_type"] = nullable_string(row, "value_type");
        measurement["numeric_value"] = nullable_double(row, "numeric_value");
        measurement["minimum_value"] = nullable_double(row, "minimum_value");
        measurement["maximum_value"] = nullable_double(row, "maximum_value");
        measurement["unit"] = nullable_string(row, "unit");
        measurement["is_approximate"] = row["is_approximate"].as<bool>();
        measurement["raw_text"] = row["raw_text"].as<std::string>();
        observations[it->second]["measurements"].append(measurement);
    }

    const auto photo_rows = db_client_->execSqlSync(
        std::string(
            "select dp.id, dp.defect_observation_id, dp.photo_number, dp.photo_title "
            "from defect_photos dp "
            "join defect_observations o on o.id = dp.defect_observation_id ") + kCurrentYearJoin +
            "where o.bridge_component_id = $1::uuid and " + kSettledObservation +
            "order by dp.photo_number",
        component_id
    );
    for (const auto& row : photo_rows) {
        const auto it = observation_index_by_id.find(row["defect_observation_id"].as<std::string>());
        if (it == observation_index_by_id.end()) {
            continue;
        }
        Json::Value photo;
        photo["id"] = row["id"].as<std::string>();
        photo["photo_number"] = row["photo_number"].as<std::string>();
        photo["photo_title"] = nullable_string(row, "photo_title");
        observations[it->second]["photos"].append(photo);
    }

    return assemble_defect_archive(
        component.value_or(Json::Value(Json::objectValue)), ratings, threads, observations);
}

Json::Value ComponentArchiveRepository::get_revisions(const std::string& component_id, const std::string& bridge_id) {
    // 旧修订版：is_current=false 的版本行（含"已被修订"），只读展示，不入默认统计。
    const auto rows = db_client_->execSqlSync(
        "select o.id, o.system_number, iy.inspection_year, o.defect_thread_id, o.defect_type, "
        "o.defect_location, o.scale, o.defect_deduction, o.defect_description_raw, o.review_status, "
        "o.updated_at::text as updated_at, iy.version_number, iy.status as inspection_status "
        "from defect_observations o "
        "join inspection_years iy on iy.id = o.inspection_year_id and iy.is_current = false "
        "where o.bridge_component_id = $1::uuid and o.review_status in ('已确认', '已修改') "
        "order by iy.inspection_year desc, iy.version_number desc, o.system_number",
        component_id
    );

    const auto current_rows = db_client_->execSqlSync(
        "select inspection_year, version_number from inspection_years "
        "where bridge_id = $1::uuid and is_current",
        bridge_id
    );
    std::unordered_map<int, int> current_version_by_year;
    for (const auto& row : current_rows) {
        current_version_by_year.emplace(row["inspection_year"].as<int>(), row["version_number"].as<int>());
    }

    Json::Value revisions(Json::arrayValue);
    Json::Value* current_group = nullptr;
    int group_year = 0;
    int group_version = 0;
    for (const auto& row : rows) {
        const auto year = row["inspection_year"].as<int>();
        const auto version = row["version_number"].as<int>();
        if (current_group == nullptr || year != group_year || version != group_version) {
            Json::Value group;
            group["inspection_year"] = year;
            group["version_number"] = version;
            group["inspection_status"] = row["inspection_status"].as<std::string>();
            const auto current_it = current_version_by_year.find(year);
            group["superseded_by_version"] = current_it == current_version_by_year.end()
                ? Json::Value(Json::nullValue)
                : Json::Value(current_it->second);
            group["observations"] = Json::Value(Json::arrayValue);
            revisions.append(group);
            current_group = &revisions[revisions.size() - 1];
            group_year = year;
            group_version = version;
        }
        (*current_group)["observations"].append(observation_to_json(row));
    }

    Json::Value body;
    body["revisions"] = revisions;
    return body;
}

Json::Value ComponentArchiveRepository::list_unbound_observations(const std::string& bridge_id) {
    const auto rows = db_client_->execSqlSync(
        std::string(
            "select o.id, o.system_number, iy.inspection_year, o.defect_thread_id, o.defect_type, "
            "o.defect_location, o.scale, o.defect_deduction, o.defect_description_raw, o.review_status, "
            "o.updated_at::text as updated_at, "
            "bc.id as component_id, bc.system_number as component_system_number, bc.structure_part, "
            "bc.component_type, bc.business_component_code "
            "from defect_observations o "
            "join bridge_components bc on bc.id = o.bridge_component_id ") + kCurrentYearJoin +
            "where o.bridge_id = $1::uuid and o.defect_thread_id is null and " + kSettledObservation +
            "order by iy.inspection_year desc, bc.system_number, o.system_number",
        bridge_id
    );

    Json::Value observations(Json::arrayValue);
    std::unordered_map<std::string, Json::ArrayIndex> observation_index_by_id;
    for (const auto& row : rows) {
        auto item = observation_to_json(row);
        Json::Value component;
        component["id"] = row["component_id"].as<std::string>();
        component["system_number"] = row["component_system_number"].as<std::string>();
        component["structure_part"] = row["structure_part"].as<std::string>();
        component["component_type"] = row["component_type"].as<std::string>();
        component["business_component_code"] = row["business_component_code"].as<std::string>();
        item["component"] = component;
        observation_index_by_id.emplace(item["id"].asString(), observations.size());
        observations.append(item);
    }

    const auto photo_rows = db_client_->execSqlSync(
        std::string(
            "select dp.id, dp.defect_observation_id, dp.photo_number, dp.photo_title "
            "from defect_photos dp "
            "join defect_observations o on o.id = dp.defect_observation_id ") + kCurrentYearJoin +
            "where o.bridge_id = $1::uuid and o.defect_thread_id is null and " + kSettledObservation +
            "order by dp.photo_number",
        bridge_id
    );
    for (const auto& row : photo_rows) {
        const auto it = observation_index_by_id.find(row["defect_observation_id"].as<std::string>());
        if (it == observation_index_by_id.end()) {
            continue;
        }
        Json::Value photo;
        photo["id"] = row["id"].as<std::string>();
        photo["photo_number"] = row["photo_number"].as<std::string>();
        photo["photo_title"] = nullable_string(row, "photo_title");
        observations[it->second]["photos"].append(photo);
    }

    Json::Value body;
    body["unbound_observations"] = observations;
    return body;
}

std::optional<Json::Value> ComponentArchiveRepository::get_observation_evidence(const std::string& observation_id) {
    const auto rows = db_client_->execSqlSync(
        "select o.source_raw_cells_json::text as raw_cells, o.source_table_title, o.source_table_index, "
        "o.source_row_number, ir.system_number as import_record_system_number, "
        "coalesce(af.system_number,sf.system_number) as source_file_system_number, "
        "coalesce(af.original_file_name,sf.original_file_name) as source_file_name, "
        "sf.status as temporary_source_status "
        "from defect_observations o "
        "left join import_records ir on ir.id = o.source_import_record_id "
        "left join archived_files af on af.id = o.source_file_id "
        "left join import_source_files sf on sf.import_record_id=ir.id "
        "where o.id = $1::uuid",
        observation_id
    );
    if (rows.empty()) {
        return std::nullopt;
    }
    const auto& row = rows[0];
    Json::Value evidence;
    evidence["source_raw_cells"] =
        parse_json_or_default(row["raw_cells"].as<std::string>(), Json::Value(Json::objectValue));
    evidence["source_table_title"] = nullable_string(row, "source_table_title");
    evidence["source_table_index"] = nullable_int(row, "source_table_index");
    evidence["source_row_number"] = nullable_int(row, "source_row_number");
    evidence["import_record_system_number"] = nullable_string(row, "import_record_system_number");
    evidence["source_file_system_number"] = nullable_string(row, "source_file_system_number");
    evidence["source_file_name"] = nullable_string(row, "source_file_name");
    evidence["temporary_source_status"] = nullable_string(row, "temporary_source_status");
    evidence["original_word_retained"] = row["temporary_source_status"].isNull();
    return evidence;
}

std::optional<Json::Value> ComponentArchiveRepository::get_observation_summary(const std::string& observation_id) {
    const auto rows = db_client_->execSqlSync(
        "select id, bridge_component_id, defect_type, defect_location "
        "from defect_observations where id = $1::uuid",
        observation_id
    );
    if (rows.empty()) {
        return std::nullopt;
    }
    const auto& row = rows[0];
    Json::Value summary;
    summary["id"] = row["id"].as<std::string>();
    summary["bridge_component_id"] = row["bridge_component_id"].as<std::string>();
    summary["defect_type"] = row["defect_type"].as<std::string>();
    summary["defect_location"] = nullable_string(row, "defect_location");
    return summary;
}

Json::Value ComponentArchiveRepository::list_threads_for_component(const std::string& component_id) {
    const auto thread_rows = db_client_->execSqlSync(
        "select t.id, t.system_number, t.thread_name, t.defect_type, t.defect_location, "
        "t.current_status, t.confirmation_status, "
        "fy.inspection_year as first_seen_year, ly.inspection_year as latest_seen_year "
        "from defect_threads t "
        "left join inspection_years fy on fy.id = t.first_seen_inspection_id "
        "left join inspection_years ly on ly.id = t.latest_seen_inspection_id "
        "where t.bridge_component_id = $1::uuid "
        "order by ly.inspection_year desc nulls last, t.system_number",
        component_id
    );
    Json::Value threads(Json::arrayValue);
    for (const auto& row : thread_rows) {
        Json::Value item;
        item["id"] = row["id"].as<std::string>();
        item["system_number"] = row["system_number"].as<std::string>();
        item["thread_name"] = row["thread_name"].as<std::string>();
        item["defect_type"] = row["defect_type"].as<std::string>();
        item["defect_location"] = nullable_string(row, "defect_location");
        item["current_status"] = row["current_status"].as<std::string>();
        item["confirmation_status"] = row["confirmation_status"].as<std::string>();
        item["first_seen_year"] = nullable_int(row, "first_seen_year");
        item["latest_seen_year"] = nullable_int(row, "latest_seen_year");
        threads.append(item);
    }
    return threads;
}

std::optional<PhotoContentRef> ComponentArchiveRepository::get_defect_photo_content_ref(
    const std::string& defect_photo_id
) {
    const auto rows = db_client_->execSqlSync(
        "select af.id as archived_file_id, af.storage_relative_path, lower(coalesce(af.file_extension, '')) as ext "
        "from defect_photos dp "
        "join archived_files af on af.id = dp.archived_file_id "
        "where dp.id = $1::uuid",
        defect_photo_id
    );
    if (rows.empty()) {
        return std::nullopt;
    }
    const auto& row = rows[0];
    const auto extension = row["ext"].as<std::string>();
    static const std::unordered_map<std::string, std::string> content_types = {
        {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"}, {".png", "image/png"},
        {".gif", "image/gif"}, {".bmp", "image/bmp"}, {".webp", "image/webp"},
        {".tif", "image/tiff"}, {".tiff", "image/tiff"},
    };
    const auto type_it = content_types.find(extension);
    PhotoContentRef ref;
    ref.archived_file_id = row["archived_file_id"].as<std::string>();
    ref.storage_relative_path = row["storage_relative_path"].as<std::string>();
    ref.content_type = type_it == content_types.end() ? "application/octet-stream" : type_it->second;
    return ref;
}

Json::Value assemble_defect_archive(
    const Json::Value& component,
    const Json::Value& ratings,
    const Json::Value& threads,
    const Json::Value& observations
) {
    Json::Value body;
    body["component"] = component;
    body["ratings"] = ratings.isArray() ? ratings : Json::Value(Json::arrayValue);

    Json::Value grouped_threads(Json::arrayValue);
    std::unordered_map<std::string, Json::ArrayIndex> thread_index_by_id;
    if (threads.isArray()) {
        for (const auto& thread : threads) {
            Json::Value item = thread;
            item["observations"] = Json::Value(Json::arrayValue);
            thread_index_by_id.emplace(item["id"].asString(), grouped_threads.size());
            grouped_threads.append(item);
        }
    }

    Json::Value unbound(Json::arrayValue);
    if (observations.isArray()) {
        for (const auto& observation : observations) {
            const auto& thread_id = observation["defect_thread_id"];
            if (thread_id.isString() && !thread_id.asString().empty()) {
                const auto it = thread_index_by_id.find(thread_id.asString());
                if (it != thread_index_by_id.end()) {
                    grouped_threads[it->second]["observations"].append(observation);
                    continue;
                }
            }
            unbound.append(observation);
        }
    }

    body["threads"] = grouped_threads;
    body["unbound_observations"] = unbound;
    return body;
}

}  // namespace bridge_report::db
