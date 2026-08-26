#include "bridge_report/db/TriageQueryRepository.hpp"

#include <algorithm>
#include <map>
#include <utility>
#include <vector>

#include "bridge_report/review/TriageSummaryJson.hpp"

namespace bridge_report::db {

namespace {

// 与 ComponentArchiveRepository 同一口径：只认当前有效且已确认年度里的正式观测。
// 旧修订版年度的观测不该出现在整理台上——它们已经被新版本取代，绑了也没有意义。
constexpr const char* kUnboundObservationsSql =
    "select o.id::text as id, o.bridge_component_id::text as bridge_component_id, "
    "bc.structure_part, bc.component_type, "
    "coalesce(bc.business_component_code, bc.system_number) as business_component_code, "
    "o.defect_type, coalesce(o.defect_location, '') as defect_location, "
    "o.updated_at::text as updated_at, iy.inspection_year "
    "from defect_observations o "
    "join bridge_components bc on bc.id = o.bridge_component_id "
    "join inspection_years iy on iy.id = o.inspection_year_id "
    "and iy.is_current and iy.status = '已确认' "
    "where o.bridge_id = $1::uuid and o.defect_thread_id is null "
    "and o.review_status in ('已确认', '已修改') "
    "order by o.id";

constexpr const char* kThreadsSql =
    "select t.id::text as id, t.system_number, t.thread_name, "
    "t.bridge_component_id::text as bridge_component_id, "
    "t.defect_type, coalesce(t.defect_location, '') as defect_location, "
    "t.updated_at::text as updated_at "
    "from defect_threads t where t.bridge_id = $1::uuid order by t.id";

// 展示字段：归组模型只管身份，标度/尺寸/照片不进那个模型，明细时按观测 id 单独取。
constexpr const char* kDisplayFieldsSql =
    "select o.id::text as id, o.system_number, o.scale, o.defect_description_raw "
    "from defect_observations o where o.id = any($1::uuid[]) order by o.id";

constexpr const char* kMeasurementsSql =
    "select dm.defect_observation_id::text as observation_id, dm.raw_text "
    "from defect_measurements dm where dm.defect_observation_id = any($1::uuid[]) "
    "order by dm.defect_observation_id, dm.created_at";

constexpr const char* kPhotosSql =
    "select dp.defect_observation_id::text as observation_id, dp.id::text as id, dp.photo_number "
    "from defect_photos dp where dp.defect_observation_id = any($1::uuid[]) "
    "order by dp.defect_observation_id, dp.photo_number";

/// PostgreSQL 数组字面量。观测 id 是 uuid，不含逗号或引号，直接拼即可。
std::string uuid_array_literal(const std::vector<std::string>& ids) {
    std::string literal = "{";
    for (std::size_t index = 0; index < ids.size(); ++index) {
        if (index > 0) literal += ',';
        literal += ids[index];
    }
    literal += '}';
    return literal;
}

Json::Value nullable(const std::string& value) {
    return value.empty() ? Json::Value(Json::nullValue) : Json::Value(value);
}

struct ObservationDisplay {
    std::string system_number;
    std::string scale;
    std::string description;
    Json::Value measurements{Json::arrayValue};
    Json::Value photos{Json::arrayValue};
};

}  // namespace

TriageQueryRepository::TriageQueryRepository(drogon::orm::DbClientPtr db_client)
    : db_client_(std::move(db_client)) {}

review::TriageModel TriageQueryRepository::load_model(const std::string& bridge_id) const {
    std::vector<review::TriageObservationInput> observations;
    for (const auto& row : db_client_->execSqlSync(kUnboundObservationsSql, bridge_id)) {
        review::TriageObservationInput observation;
        observation.id = row["id"].as<std::string>();
        observation.bridge_component_id = row["bridge_component_id"].as<std::string>();
        observation.structure_part = row["structure_part"].as<std::string>();
        observation.component_type = row["component_type"].as<std::string>();
        observation.business_component_code = row["business_component_code"].as<std::string>();
        observation.defect_type = row["defect_type"].as<std::string>();
        observation.defect_location = row["defect_location"].as<std::string>();
        observation.updated_at = row["updated_at"].as<std::string>();
        observation.inspection_year = row["inspection_year"].as<int>();
        observations.push_back(std::move(observation));
    }

    return review::build_triage_model(std::move(observations), load_threads(bridge_id));
}

std::vector<review::TriageThreadInput> TriageQueryRepository::load_threads(
    const std::string& bridge_id) const {
    std::vector<review::TriageThreadInput> threads;
    for (const auto& row : db_client_->execSqlSync(kThreadsSql, bridge_id)) {
        review::TriageThreadInput thread;
        thread.id = row["id"].as<std::string>();
        thread.system_number = row["system_number"].as<std::string>();
        thread.thread_name = row["thread_name"].as<std::string>();
        thread.bridge_component_id = row["bridge_component_id"].as<std::string>();
        thread.defect_type = row["defect_type"].as<std::string>();
        thread.defect_location = row["defect_location"].as<std::string>();
        thread.updated_at = row["updated_at"].as<std::string>();
        threads.push_back(std::move(thread));
    }
    return threads;
}

Json::Value TriageQueryRepository::summary(const std::string& bridge_id) const {
    return review::triage_summary_json(load_model(bridge_id));
}

std::optional<Json::Value> TriageQueryRepository::batch_detail(
    const std::string& bridge_id, const std::string& batch_id) const {
    const auto model = load_model(bridge_id);
    const auto found = std::find_if(
        model.batches.begin(), model.batches.end(),
        [&batch_id](const review::TriageBatch& batch) { return batch.batch_id == batch_id; });
    if (found == model.batches.end()) return std::nullopt;
    const auto& batch = *found;

    std::vector<std::string> observation_ids;
    for (const auto& group : batch.groups) {
        for (const auto& observation : group.observations) observation_ids.push_back(observation.id);
    }

    // 三条语句取完全部展示字段。按观测循环就是服务端版的 N+1——正是现有整理页的死法。
    std::map<std::string, ObservationDisplay> display_by_observation;
    if (!observation_ids.empty()) {
        const auto ids = uuid_array_literal(observation_ids);
        for (const auto& row : db_client_->execSqlSync(kDisplayFieldsSql, ids)) {
            auto& display = display_by_observation[row["id"].as<std::string>()];
            display.system_number = row["system_number"].as<std::string>();
            display.scale = row["scale"].isNull() ? std::string() : row["scale"].as<std::string>();
            display.description = row["defect_description_raw"].as<std::string>();
        }
        for (const auto& row : db_client_->execSqlSync(kMeasurementsSql, ids)) {
            display_by_observation[row["observation_id"].as<std::string>()]
                .measurements.append(row["raw_text"].as<std::string>());
        }
        for (const auto& row : db_client_->execSqlSync(kPhotosSql, ids)) {
            Json::Value photo;
            photo["id"] = row["id"].as<std::string>();
            photo["photo_number"] = row["photo_number"].as<std::string>();
            display_by_observation[row["observation_id"].as<std::string>()].photos.append(photo);
        }
    }

    // bind 批次里每个组绑各自构件的那条线索，不存在批次级单一线索，目标摘要必须逐组给。
    std::map<std::string, review::TriageThreadInput> thread_by_id;
    for (auto& thread : load_threads(bridge_id)) {
        const auto id = thread.id;
        thread_by_id.emplace(id, std::move(thread));
    }

    Json::Value body;
    body["snapshot_id"] = model.snapshot_fingerprint;
    body["batch_id"] = batch.batch_id;
    body["fingerprint"] = batch.fingerprint;
    body["action"] = review::to_string(batch.action);
    body["structure_part"] = batch.structure_part;
    body["component_type"] = batch.component_type;
    body["defect_type"] = batch.defect_type;
    body["defect_location"] = nullable(batch.defect_location);
    body["year_set"] = Json::Value(Json::arrayValue);
    for (const auto year : batch.year_set) body["year_set"].append(year);
    body["group_count"] = static_cast<int>(batch.groups.size());
    body["observation_count"] = batch.observation_count();

    body["groups"] = Json::Value(Json::arrayValue);
    for (const auto& group : batch.groups) {
        Json::Value group_json;
        group_json["group_id"] = group.group_id;
        group_json["bridge_component_id"] = group.key.bridge_component_id;
        group_json["business_component_code"] = group.observations.empty()
            ? Json::Value(Json::nullValue)
            : Json::Value(group.observations.front().business_component_code);
        if (group.matched_thread_id.has_value()) {
            const auto thread = thread_by_id.find(*group.matched_thread_id);
            Json::Value target;
            target["thread_id"] = *group.matched_thread_id;
            target["system_number"] = thread == thread_by_id.end()
                ? Json::Value(Json::nullValue) : Json::Value(thread->second.system_number);
            target["thread_name"] = thread == thread_by_id.end()
                ? Json::Value(Json::nullValue) : Json::Value(thread->second.thread_name);
            group_json["target_thread"] = target;
        } else {
            group_json["target_thread"] = Json::Value(Json::nullValue);
        }

        group_json["observations"] = Json::Value(Json::arrayValue);
        for (const auto& observation : group.observations) {
            Json::Value entry;
            entry["id"] = observation.id;
            // 提交时拿它做并发校验，明细里一条都不能少。
            entry["updated_at"] = observation.updated_at;
            entry["inspection_year"] = observation.inspection_year;
            entry["defect_type"] = observation.defect_type;
            entry["defect_location"] = nullable(observation.defect_location);
            const auto display = display_by_observation.find(observation.id);
            if (display != display_by_observation.end()) {
                entry["system_number"] = display->second.system_number;
                entry["scale"] = nullable(display->second.scale);
                entry["defect_description"] = display->second.description;
                entry["measurements"] = display->second.measurements;
                entry["photos"] = display->second.photos;
            }
            group_json["observations"].append(entry);
        }
        body["groups"].append(group_json);
    }
    return body;
}

}  // namespace bridge_report::db
