#include "bridge_report/review/TriageSummaryJson.hpp"

namespace bridge_report::review {

namespace {

Json::Value nullable(const std::string& value) {
    return value.empty() ? Json::Value(Json::nullValue) : Json::Value(value);
}

/// 样例组：只够人一眼看出"这批长什么样"，不够构造提交清单——那是明细接口的事。
Json::Value sample_group_json(const TriageGroup& group) {
    Json::Value item;
    item["group_id"] = group.group_id;
    item["business_component_code"] = group.observations.empty()
        ? Json::Value(Json::nullValue)
        : Json::Value(group.observations.front().business_component_code);
    item["years"] = Json::Value(Json::arrayValue);
    for (const auto& observation : group.observations) {
        item["years"].append(observation.inspection_year);
    }
    item["target_thread_id"] = group.matched_thread_id.has_value()
        ? Json::Value(*group.matched_thread_id)
        : Json::Value(Json::nullValue);
    return item;
}

/// 异常簇里的组要给全：人靠历年观测的对照判断该合该分，判据是标度、尺寸与照片。
Json::Value manual_group_json(const TriageGroup& group, const TriageDisplayLookup& display) {
    Json::Value item;
    item["group_id"] = group.group_id;
    item["bridge_component_id"] = group.key.bridge_component_id;
    item["business_component_code"] = group.observations.empty()
        ? Json::Value(Json::nullValue)
        : Json::Value(group.observations.front().business_component_code);
    item["defect_type"] = group.observations.empty()
        ? Json::Value(Json::nullValue)
        : Json::Value(group.observations.back().defect_type);
    item["defect_location"] = group.observations.empty()
        ? Json::Value(Json::nullValue)
        : nullable(group.observations.back().defect_location);
    item["target_thread_id"] = group.matched_thread_id.has_value()
        ? Json::Value(*group.matched_thread_id)
        : Json::Value(Json::nullValue);
    item["observations"] = Json::Value(Json::arrayValue);
    for (const auto& observation : group.observations) {
        Json::Value entry;
        entry["id"] = observation.id;
        entry["inspection_year"] = observation.inspection_year;
        entry["defect_type"] = observation.defect_type;
        entry["defect_location"] = nullable(observation.defect_location);
        entry["updated_at"] = observation.updated_at;
        const auto found = display.find(observation.id);
        if (found != display.end()) {
            entry["system_number"] = found->second.system_number;
            entry["scale"] = nullable(found->second.scale);
            entry["defect_description"] = found->second.description;
            entry["measurements"] = Json::Value(Json::arrayValue);
            for (const auto& raw_text : found->second.measurements) {
                entry["measurements"].append(raw_text);
            }
            entry["photos"] = Json::Value(Json::arrayValue);
            for (const auto& photo : found->second.photos) {
                Json::Value photo_json;
                photo_json["id"] = photo.id;
                photo_json["photo_number"] = photo.photo_number;
                entry["photos"].append(photo_json);
            }
        }
        item["observations"].append(entry);
    }
    return item;
}

Json::Value overlap_target_json(const TriageOverlapTarget& target) {
    Json::Value item;
    // 对方是组还是线索，界面要摆的东西完全不同：组要摆历年观测，线索要摆 BHXS 编号。
    item["kind"] = target.kind == TriageOverlapTarget::Kind::Thread ? "thread" : "group";
    item["id"] = target.id;
    item["display_name"] = nullable(target.display_name);
    item["system_number"] = nullable(target.system_number);
    item["normalized_location"] = nullable(target.normalized_location);
    return item;
}

Json::Value thread_json(const TriageThreadInput& thread) {
    Json::Value item;
    item["id"] = thread.id;
    item["system_number"] = thread.system_number;
    item["thread_name"] = thread.thread_name;
    item["bridge_component_id"] = thread.bridge_component_id;
    item["defect_type"] = thread.defect_type;
    item["defect_location"] = nullable(thread.defect_location);
    return item;
}

}  // namespace

std::vector<const TriageGroup*> pick_sample_groups(const TriageBatch& batch) {
    std::vector<const TriageGroup*> samples;
    if (batch.groups.empty()) return samples;
    if (batch.groups.size() <= 3) {
        for (const auto& group : batch.groups) samples.push_back(&group);
        return samples;
    }
    const auto last = batch.groups.size() - 1;
    for (const auto index : {std::size_t{0}, batch.groups.size() / 2, last}) {
        samples.push_back(&batch.groups[index]);
    }
    return samples;
}

Json::Value triage_summary_json(
    const TriageModel& model, const TriageDisplayLookup& display) {
    Json::Value body;
    body["snapshot_id"] = model.snapshot_fingerprint;
    body["unbound_observation_count"] = model.unbound_observation_count;
    body["batchable_group_count"] = model.batchable_group_count;
    body["batchable_observation_count"] = model.batchable_observation_count;
    body["manual_group_count"] = model.manual_group_count;
    body["manual_observation_count"] = model.manual_observation_count;

    body["batches"] = Json::Value(Json::arrayValue);
    for (const auto& batch : model.batches) {
        Json::Value item;
        item["batch_id"] = batch.batch_id;
        item["fingerprint"] = batch.fingerprint;
        item["action"] = to_string(batch.action);
        item["structure_part"] = batch.structure_part;
        item["component_type"] = batch.component_type;
        item["defect_type"] = batch.defect_type;
        item["defect_location"] = nullable(batch.defect_location);
        item["year_set"] = Json::Value(Json::arrayValue);
        for (const auto year : batch.year_set) item["year_set"].append(year);
        item["group_count"] = static_cast<int>(batch.groups.size());
        item["observation_count"] = batch.observation_count();
        item["sample_groups"] = Json::Value(Json::arrayValue);
        for (const auto* group : pick_sample_groups(batch)) {
            item["sample_groups"].append(sample_group_json(*group));
        }
        body["batches"].append(item);
    }

    body["manual_clusters"] = Json::Value(Json::arrayValue);
    for (const auto& cluster : model.manual_clusters) {
        Json::Value item;
        item["cluster_id"] = cluster.cluster_id;
        item["reason_codes"] = Json::Value(Json::arrayValue);
        for (const auto& reason : cluster.reason_codes) item["reason_codes"].append(reason);
        item["group_count"] = static_cast<int>(cluster.groups.size());
        item["observation_count"] = cluster.observation_count();
        item["groups"] = Json::Value(Json::arrayValue);
        for (const auto& group : cluster.groups) {
            item["groups"].append(manual_group_json(group, display));
        }
        item["overlap_targets"] = Json::Value(Json::arrayValue);
        for (const auto& target : cluster.overlap_targets) {
            item["overlap_targets"].append(overlap_target_json(target));
        }
        item["related_threads"] = Json::Value(Json::arrayValue);
        for (const auto& thread : cluster.related_threads) {
            item["related_threads"].append(thread_json(thread));
        }
        body["manual_clusters"].append(item);
    }
    return body;
}

}  // namespace bridge_report::review
