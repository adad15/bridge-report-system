#include "bridge_report/db/TriageQueryRepository.hpp"

#include <utility>

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

    return review::build_triage_model(std::move(observations), std::move(threads));
}

Json::Value TriageQueryRepository::summary(const std::string& bridge_id) const {
    return review::triage_summary_json(load_model(bridge_id));
}

}  // namespace bridge_report::db
