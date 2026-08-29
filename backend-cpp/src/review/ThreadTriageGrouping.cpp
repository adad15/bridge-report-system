#include "bridge_report/review/ThreadTriageGrouping.hpp"

#include <algorithm>
#include <array>
#include <iomanip>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <openssl/evp.h>

namespace bridge_report::review {

namespace {

// 局部实现：仓库里另一处 sha256 绑着 archive 专有的异常类型，不值得为这里去改动无关模块。
// 若第三处再需要，届时提取成共享工具。
std::string sha256_hex(std::string_view content) {
    using Context = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
    Context context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!context || EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1) {
        throw std::runtime_error("unable to initialize SHA-256 for triage grouping");
    }
    if (!content.empty()
        && EVP_DigestUpdate(context.get(), content.data(), content.size()) != 1) {
        throw std::runtime_error("unable to hash triage grouping key");
    }
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int length = 0;
    if (EVP_DigestFinal_ex(context.get(), digest.data(), &length) != 1) {
        throw std::runtime_error("unable to finalize SHA-256 for triage grouping");
    }
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (unsigned int index = 0; index < length; ++index) {
        output << std::setw(2) << static_cast<int>(digest[index]);
    }
    return output.str();
}

// 截到 32 位十六进制（128 bit）。够短能进 URL，碰撞概率在单桥几百个批次的量级上可以忽略。
std::string short_id(const std::string& canonical) {
    return sha256_hex(canonical).substr(0, 32);
}

constexpr char kFieldSeparator = '\x1f';
constexpr char kRecordSeparator = '\x1e';

std::string join_years(const std::vector<int>& years) {
    std::ostringstream output;
    for (std::size_t index = 0; index < years.size(); ++index) {
        if (index > 0) output << ',';
        output << years[index];
    }
    return output.str();
}

// 组的内容指纹：观测 id 与 updated_at。读取与提交之间只要有人动过其中一条，指纹就变。
std::string group_content_signature(const TriageGroup& group) {
    std::ostringstream output;
    for (const auto& observation : group.observations) {
        output << observation.id << kFieldSeparator << observation.updated_at << kRecordSeparator;
    }
    if (group.matched_thread_id.has_value()) {
        output << *group.matched_thread_id;
    }
    return output.str();
}

struct BatchKey {
    std::string structure_part;
    std::string component_type;
    std::string node_key;
    std::string normalized_defect_location;
    std::vector<int> year_set;
    TriageAction action{TriageAction::Create};

    [[nodiscard]] std::string canonical_string() const {
        std::ostringstream output;
        output << structure_part << kFieldSeparator
               << component_type << kFieldSeparator
               << node_key << kFieldSeparator
               << normalized_defect_location << kFieldSeparator
               << join_years(year_set) << kFieldSeparator
               << to_string(action);
        return output.str();
    }

    bool operator<(const BatchKey& other) const {
        return canonical_string() < other.canonical_string();
    }
};

/// 归组中间态：还没决定进批次还是进异常簇。
struct PendingGroup {
    TriageGroup group;
    std::string structure_part;
    std::string component_type;
    std::string business_component_code;
    std::vector<int> year_set;
    std::set<std::string> reason_codes;
    std::vector<TriageOverlapTarget> overlap_targets;
    std::vector<TriageThreadInput> related_threads;

    [[nodiscard]] bool is_clean() const { return reason_codes.empty(); }
};

/// 同构件同类型下的一批组与线索，overlap 只在这个范围内比较。
struct ComponentTypeScope {
    std::vector<std::size_t> group_indexes;
    std::vector<const TriageThreadInput*> threads;
};

void add_reason(PendingGroup& group, const char* reason) {
    group.reason_codes.insert(reason);
}

}  // namespace

std::string to_string(TriageAction action) {
    return action == TriageAction::Bind ? "bind" : "create";
}

int TriageBatch::observation_count() const {
    int total = 0;
    for (const auto& group : groups) total += static_cast<int>(group.observations.size());
    return total;
}

int TriageManualCluster::observation_count() const {
    int total = 0;
    for (const auto& group : groups) total += static_cast<int>(group.observations.size());
    return total;
}

TriageModel build_triage_model(
    std::vector<TriageObservationInput> observations,
    std::vector<TriageThreadInput> existing_threads) {
    TriageModel model;
    model.unbound_observation_count = static_cast<int>(observations.size());

    // ── 1. 按规范键聚合观测 ────────────────────────────────────────
    std::map<std::string, PendingGroup> groups_by_key;
    for (auto& observation : observations) {
        const auto key = make_thread_canonical_key(
            observation.bridge_component_id, observation.node_key, observation.defect_location);
        auto& pending = groups_by_key[key.canonical_string()];
        if (pending.group.observations.empty()) {
            pending.group.key = key;
            pending.structure_part = observation.structure_part;
            pending.component_type = observation.component_type;
            pending.business_component_code = observation.business_component_code;
        }
        pending.group.observations.push_back(std::move(observation));
    }

    std::vector<PendingGroup> pending_groups;
    pending_groups.reserve(groups_by_key.size());
    for (auto& [canonical, pending] : groups_by_key) {
        auto& group = pending.group;
        std::sort(group.observations.begin(), group.observations.end(),
                  [](const auto& left, const auto& right) {
                      return std::tie(left.inspection_year, left.id)
                          < std::tie(right.inspection_year, right.id);
                  });
        group.group_id = short_id(canonical);

        // 同一年出现多条：是两处病害还是记了两遍，机器判不了。
        std::set<int> years;
        for (const auto& observation : group.observations) {
            if (!years.insert(observation.inspection_year).second) {
                add_reason(pending, kReasonMultipleInYear);
            }
        }
        pending.year_set.assign(years.begin(), years.end());
        pending_groups.push_back(std::move(pending));
    }

    // ── 2. 精确匹配已有线索 ────────────────────────────────────────
    std::map<std::string, std::vector<const TriageThreadInput*>> threads_by_key;
    std::map<std::string, ComponentTypeScope> scopes;
    for (const auto& thread : existing_threads) {
        const auto key = make_thread_canonical_key(
            thread.bridge_component_id, thread.node_key, thread.defect_location);
        threads_by_key[key.canonical_string()].push_back(&thread);
        scopes[key.bridge_component_id + kFieldSeparator + key.node_key]
            .threads.push_back(&thread);
    }

    for (std::size_t index = 0; index < pending_groups.size(); ++index) {
        auto& pending = pending_groups[index];
        const auto& key = pending.group.key;
        scopes[key.bridge_component_id + kFieldSeparator + key.node_key]
            .group_indexes.push_back(index);

        const auto found = threads_by_key.find(key.canonical_string());
        if (found == threads_by_key.end()) continue;
        if (found->second.size() > 1) {
            // 命中多条，替人选就是替人担责。
            add_reason(pending, kReasonAmbiguousThread);
            for (const auto* thread : found->second) pending.related_threads.push_back(*thread);
            continue;
        }
        pending.group.matched_thread_id = found->second.front()->id;
    }

    // ── 3. 位置重叠：组与组、组与已有线索 ──────────────────────────
    for (const auto& [scope_key, scope] : scopes) {
        for (const auto group_index : scope.group_indexes) {
            auto& pending = pending_groups[group_index];
            const auto& location = pending.group.key.normalized_defect_location;

            for (const auto other_index : scope.group_indexes) {
                if (other_index == group_index) continue;
                const auto& other = pending_groups[other_index];
                if (!locations_overlap(location, other.group.key.normalized_defect_location)) {
                    continue;
                }
                add_reason(pending, kReasonLocationOverlap);
                pending.overlap_targets.push_back(TriageOverlapTarget{
                    TriageOverlapTarget::Kind::Group,
                    other.group.group_id,
                    other.business_component_code,
                    {},
                    other.group.key.normalized_defect_location,
                });
            }

            // 只查未绑定组之间是不够的：已有线索写"大小里程侧及左悬臂底部"、新观测写
            // "大小里程侧"时，两者不精确相等，若不比对就会判成 create，凭空造出一条
            // 与已有线索疑似重复的线索。
            for (const auto* thread : scope.threads) {
                const auto thread_key = make_thread_canonical_key(
                    thread->bridge_component_id, thread->node_key, thread->defect_location);
                if (!locations_overlap(location, thread_key.normalized_defect_location)) continue;
                add_reason(pending, kReasonLocationOverlap);
                pending.overlap_targets.push_back(TriageOverlapTarget{
                    TriageOverlapTarget::Kind::Thread,
                    thread->id,
                    thread->thread_name,
                    thread->system_number,
                    thread_key.normalized_defect_location,
                });
                pending.related_threads.push_back(*thread);
            }
        }
    }

    // ── 4. 分流：干净的进批次，其余进异常簇 ────────────────────────
    std::map<BatchKey, std::vector<PendingGroup*>> batches_by_key;
    std::vector<PendingGroup*> manual_groups;
    for (auto& pending : pending_groups) {
        if (!pending.is_clean()) {
            manual_groups.push_back(&pending);
            continue;
        }
        BatchKey batch_key{
            pending.structure_part,
            pending.component_type,
            pending.group.key.node_key,
            pending.group.key.normalized_defect_location,
            pending.year_set,
            pending.group.matched_thread_id.has_value() ? TriageAction::Bind : TriageAction::Create,
        };
        batches_by_key[batch_key].push_back(&pending);
    }

    for (auto& [batch_key, members] : batches_by_key) {
        std::sort(members.begin(), members.end(), [](const auto* left, const auto* right) {
            return std::tie(left->business_component_code, left->group.group_id)
                < std::tie(right->business_component_code, right->group.group_id);
        });

        TriageBatch batch;
        batch.action = batch_key.action;
        batch.structure_part = batch_key.structure_part;
        batch.component_type = batch_key.component_type;
        batch.year_set = batch_key.year_set;
        batch.batch_id = short_id(batch_key.canonical_string());
        // 展示用原文取最新年度：同一规范键下各年原文可能只差标点，最新的更贴近当前用语。
        const auto& newest = members.front()->group.observations.back();
        batch.defect_type = newest.defect_type;
        batch.defect_location = newest.defect_location;

        std::ostringstream signature;
        for (auto* member : members) {
            batch.groups.push_back(std::move(member->group));
            signature << group_content_signature(batch.groups.back()) << kRecordSeparator;
        }
        batch.fingerprint = sha256_hex(signature.str());
        model.batchable_group_count += static_cast<int>(batch.groups.size());
        model.batchable_observation_count += batch.observation_count();
        model.batches.push_back(std::move(batch));
    }

    std::sort(model.batches.begin(), model.batches.end(),
              [](const TriageBatch& left, const TriageBatch& right) {
                  const auto left_count = left.observation_count();
                  const auto right_count = right.observation_count();
                  if (left_count != right_count) return left_count > right_count;
                  return left.batch_id < right.batch_id;
              });

    // ── 5. 异常簇：把必须一起判断的组并到同一簇 ────────────────────
    // 用并查集把 overlap 相关的组连起来；multiple_in_year / ambiguous_thread 自成一簇。
    std::vector<TriageManualCluster> clusters;
    std::map<std::string, PendingGroup*> manual_by_group_id;
    for (auto* pending : manual_groups) manual_by_group_id[pending->group.group_id] = pending;
    // 按指针记已归属，不按 group_id：下面把 group 搬进簇之后 group_id 就空了，
    // 再用它判断"这组处理过没有"会把同一个组又开出一簇。
    std::set<const PendingGroup*> assigned;

    for (auto* pending : manual_groups) {
        if (assigned.contains(pending)) continue;

        TriageManualCluster cluster;
        std::vector<PendingGroup*> queue{pending};
        std::set<std::string> visited{pending->group.group_id};
        while (!queue.empty()) {
            auto* current = queue.back();
            queue.pop_back();
            assigned.insert(current);
            for (const auto& reason : current->reason_codes) cluster.reason_codes.push_back(reason);
            for (auto& target : current->overlap_targets) {
                cluster.overlap_targets.push_back(target);
                if (target.kind != TriageOverlapTarget::Kind::Group) continue;
                const auto neighbour = manual_by_group_id.find(target.id);
                if (neighbour == manual_by_group_id.end()) continue;
                if (!visited.insert(target.id).second) continue;
                queue.push_back(neighbour->second);
            }
            for (auto& thread : current->related_threads) cluster.related_threads.push_back(thread);
            cluster.groups.push_back(std::move(current->group));
        }

        std::sort(cluster.reason_codes.begin(), cluster.reason_codes.end());
        cluster.reason_codes.erase(
            std::unique(cluster.reason_codes.begin(), cluster.reason_codes.end()),
            cluster.reason_codes.end());
        std::sort(cluster.groups.begin(), cluster.groups.end(),
                  [](const TriageGroup& left, const TriageGroup& right) {
                      return left.group_id < right.group_id;
                  });
        cluster.cluster_id = short_id(cluster.groups.front().group_id);
        model.manual_group_count += static_cast<int>(cluster.groups.size());
        model.manual_observation_count += cluster.observation_count();
        clusters.push_back(std::move(cluster));
    }
    std::sort(clusters.begin(), clusters.end(),
              [](const TriageManualCluster& left, const TriageManualCluster& right) {
                  return left.cluster_id < right.cluster_id;
              });
    model.manual_clusters = std::move(clusters);

    // ── 6. 快照指纹：覆盖全部批次与异常簇 ──────────────────────────
    std::ostringstream snapshot;
    for (const auto& batch : model.batches) {
        snapshot << batch.batch_id << kFieldSeparator << batch.fingerprint << kRecordSeparator;
    }
    for (const auto& cluster : model.manual_clusters) {
        snapshot << cluster.cluster_id << kFieldSeparator;
        for (const auto& group : cluster.groups) {
            snapshot << group_content_signature(group) << kFieldSeparator;
        }
        snapshot << kRecordSeparator;
    }
    model.snapshot_fingerprint = sha256_hex(snapshot.str());
    return model;
}

}  // namespace bridge_report::review
