#include "bridge_report/db/ThreadResolutionRepository.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <utility>

#include <drogon/orm/Exception.h>
#include <trantor/utils/Logger.h>

#include "bridge_report/db/CommitLatch.hpp"
#include "bridge_report/db/TriageQueryRepository.hpp"
#include "bridge_report/review/ThreadCanonicalKey.hpp"

namespace bridge_report::db {

namespace {

using TransactionPtr = std::shared_ptr<drogon::orm::Transaction>;

/// 锁定后的观测事实。**这些才是校验依据**，不是请求里那份。
struct LockedObservation {
    std::string id;
    std::string bridge_id;
    std::string bridge_component_id;
    std::string defect_type;
    std::string defect_location;
    std::string review_status;
    std::string updated_at;
    std::optional<std::string> defect_thread_id;
    bool year_is_current{false};
    std::string year_status;
};

/// 按 UUID 固定顺序一次锁完。固定锁序让两个观测集合交叉的请求不至于互相死锁。
std::map<std::string, LockedObservation> lock_observations(
    const TransactionPtr& tx, const std::vector<std::string>& ids) {
    std::map<std::string, LockedObservation> locked;
    if (ids.empty()) return locked;

    std::string literal = "{";
    for (std::size_t index = 0; index < ids.size(); ++index) {
        if (index > 0) literal += ',';
        literal += ids[index];
    }
    literal += '}';

    const auto rows = tx->execSqlSync(
        "select o.id::text as id, o.bridge_id::text as bridge_id, "
        "o.bridge_component_id::text as bridge_component_id, o.defect_type, "
        "coalesce(o.defect_location,'') as defect_location, o.review_status, "
        "o.updated_at::text as updated_at, o.defect_thread_id::text as defect_thread_id, "
        "iy.is_current, iy.status as year_status "
        "from defect_observations o "
        "join inspection_years iy on iy.id = o.inspection_year_id "
        "where o.id = any($1::uuid[]) order by o.id for update of o",
        literal);

    for (const auto& row : rows) {
        LockedObservation observation;
        observation.id = row["id"].as<std::string>();
        observation.bridge_id = row["bridge_id"].as<std::string>();
        observation.bridge_component_id = row["bridge_component_id"].as<std::string>();
        observation.defect_type = row["defect_type"].as<std::string>();
        observation.defect_location = row["defect_location"].as<std::string>();
        observation.review_status = row["review_status"].as<std::string>();
        observation.updated_at = row["updated_at"].as<std::string>();
        if (!row["defect_thread_id"].isNull()) {
            observation.defect_thread_id = row["defect_thread_id"].as<std::string>();
        }
        observation.year_is_current = row["is_current"].as<bool>();
        observation.year_status = row["year_status"].as<std::string>();
        locked.emplace(observation.id, std::move(observation));
    }
    return locked;
}

/// 现有代码只在重绑路径上查这个，首次绑定根本不查——批量必须显式调用。
bool referenced_by_confirmed_comparison(const TransactionPtr& tx, const std::string& observation_id) {
    const auto rows = tx->execSqlSync(
        "select 1 from defect_comparisons c "
        "where (c.current_defect_observation_id = $1::uuid "
        "or c.previous_defect_observation_id = $1::uuid) "
        "and c.confirmation_status = '人工已确认' limit 1",
        observation_id);
    return !rows.empty();
}

/// 往被已确认对比引用的线索里追加历年观测，会改变它的跨年跨度，动摇既有结论。
bool thread_referenced_by_confirmed_comparison(
    const TransactionPtr& tx, const std::string& thread_id) {
    const auto rows = tx->execSqlSync(
        "select 1 from defect_comparisons c where c.defect_thread_id = $1::uuid "
        "and c.confirmation_status = '人工已确认' limit 1",
        thread_id);
    return !rows.empty();
}

struct ThreadRow {
    std::string id;
    std::string system_number;
    std::string bridge_component_id;
    std::string defect_type;
    std::string defect_location;
};

std::vector<ThreadRow> load_component_threads(
    const TransactionPtr& tx, const std::string& bridge_id) {
    std::vector<ThreadRow> threads;
    for (const auto& row : tx->execSqlSync(
             "select id::text as id, system_number, bridge_component_id::text as bridge_component_id, "
             "defect_type, coalesce(defect_location,'') as defect_location "
             "from defect_threads where bridge_id = $1::uuid order by id",
             bridge_id)) {
        threads.push_back(ThreadRow{
            row["id"].as<std::string>(),
            row["system_number"].as<std::string>(),
            row["bridge_component_id"].as<std::string>(),
            row["defect_type"].as<std::string>(),
            row["defect_location"].as<std::string>(),
        });
    }
    return threads;
}

void recompute_thread_span(const TransactionPtr& tx, const std::string& thread_id) {
    tx->execSqlSync(
        "with current_obs as ("
        "  select o.inspection_year_id, iy.inspection_year "
        "  from defect_observations o "
        "  join inspection_years iy on iy.id = o.inspection_year_id "
        "  where o.defect_thread_id = $1::uuid and iy.is_current and iy.status = '已确认' "
        "  and o.review_status in ('已确认', '已修改')"
        ") "
        "update defect_threads set "
        "first_seen_inspection_id = (select inspection_year_id from current_obs order by inspection_year asc limit 1), "
        "latest_seen_inspection_id = (select inspection_year_id from current_obs order by inspection_year desc limit 1), "
        "updated_at = now() where id = $1::uuid",
        thread_id);
}

void add_issue(
    std::vector<TriageApplyIssue>& issues, std::string code, std::string message,
    const std::string& group_id, const std::string& component_id = {},
    const std::string& observation_id = {}) {
    issues.push_back(TriageApplyIssue{
        std::move(code), std::move(message), group_id, component_id, observation_id});
}

const char* status_text(TriageApplyStatus status) {
    switch (status) {
        case TriageApplyStatus::Applied: return "applied";
        case TriageApplyStatus::AlreadyCompleted: return "already_completed";
        case TriageApplyStatus::Rejected: return "rejected";
        case TriageApplyStatus::BatchChanged: return "batch_changed";
    }
    return "unknown";
}

/**
 * 批量整理的规模与耗时记一行，格式对齐 ComponentRangeSplitRepository。
 *
 * 设计 §12.6 要求把性能基准“纳入回归测试或基准记录”。一次性压测量出来的数字留不下来：
 * 换台机器、换个批次大小就得重量，而真正出问题的那次线上慢查询谁也没量过。日志才是能
 * 一直看到的那个口径。
 *
 * 写成 RAII 是因为 apply 有十几个早退分支——校验不过、并发冲突、提交失败各走各的
 * return，逐个补日志一定会漏掉一个。
 */
class ScopeTimer {
public:
    ScopeTimer(const char* operation, const TriageApplyOutcome& outcome, std::size_t group_count)
        : operation_(operation),
          outcome_(outcome),
          group_count_(group_count),
          started_(std::chrono::steady_clock::now()) {}

    ScopeTimer(const ScopeTimer&) = delete;
    ScopeTimer& operator=(const ScopeTimer&) = delete;

    ~ScopeTimer() {
        const auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - started_)
                                  .count();
        LOG_INFO << "thread triage " << operation_
                 << " groups=" << group_count_
                 << " status=" << status_text(outcome_.status)
                 << " threads_created=" << outcome_.threads_created
                 << " observations_bound=" << outcome_.observations_bound
                 << " issues=" << outcome_.issues.size()
                 << " total_ms=" << total_ms;
    }

private:
    const char* operation_;
    const TriageApplyOutcome& outcome_;
    std::size_t group_count_;
    std::chrono::steady_clock::time_point started_;
};

}  // namespace

ThreadResolutionRepository::ThreadResolutionRepository(drogon::orm::DbClientPtr db_client)
    : db_client_(std::move(db_client)) {}

TriageApplyOutcome ThreadResolutionRepository::apply(const TriageApplyRequest& request) const {
    TriageApplyOutcome outcome;
    const ScopeTimer timer("apply", outcome, request.groups.size());
    TransactionPtr tx;
    const auto latch = std::make_shared<CommitLatch>();
    const auto rollback = [&tx]() {
        if (tx != nullptr) {
            try { tx->rollback(); } catch (...) {}
        }
    };

    if (request.groups.empty()) {
        add_issue(outcome.issues, "empty_group_selection", "本次提交没有任何组。", {});
        return outcome;
    }

    try {
        tx = db_client_->newTransaction(latch->callback());

        // ── 阶段一：收集去重、固定顺序、批量锁定 ────────────────────
        std::vector<std::string> observation_ids;
        std::set<std::string> seen_observation_ids;
        bool duplicated = false;
        for (const auto& group : request.groups) {
            for (const auto& observation : group.observations) {
                if (!seen_observation_ids.insert(observation.id).second) {
                    duplicated = true;
                    add_issue(outcome.issues, "duplicate_observation",
                              "同一条观测出现在多个组里。", group.group_id, {}, observation.id);
                    continue;
                }
                observation_ids.push_back(observation.id);
            }
        }
        std::sort(observation_ids.begin(), observation_ids.end());
        if (duplicated) {
            rollback();
            return outcome;
        }

        const auto locked = lock_observations(tx, observation_ids);
        const auto threads = load_component_threads(tx, request.bridge_id);

        // ── 阶段二：只读校验，收齐全部错误 ──────────────────────────
        // 组的构件、类型、位置一律从锁定后的行重算，不看请求里那份。
        struct ResolvedGroup {
            const TriageApplyGroup* request_group{nullptr};
            std::string bridge_component_id;
            review::ThreadCanonicalKey key;
            std::string defect_type_raw;
            std::string defect_location_raw;
            std::string matched_thread_id;
            std::string matched_system_number;
            bool already_completed{false};
        };
        std::vector<ResolvedGroup> resolved_groups;
        int already_completed_groups = 0;

        for (const auto& group : request.groups) {
            ResolvedGroup resolved;
            resolved.request_group = &group;
            if (group.observations.empty()) {
                add_issue(outcome.issues, "empty_group_selection",
                          "组内没有观测。", group.group_id);
                continue;
            }

            // ── 幂等判定必须先于令牌校验 ──────────────────────────
            // 第一次成功会把 updated_at 推新，响应丢失后的重试必然携带旧令牌。若先校验
            // 令牌，服务端只会报冲突，永远走不到"这批其实已经做完了"的判断。
            std::set<std::string> bound_threads;
            int bound_count = 0;
            bool all_locked = true;
            for (const auto& requested : group.observations) {
                const auto found = locked.find(requested.id);
                if (found == locked.end()) {
                    all_locked = false;
                    break;
                }
                if (found->second.defect_thread_id.has_value()) {
                    ++bound_count;
                    bound_threads.insert(*found->second.defect_thread_id);
                }
            }

            if (all_locked && bound_count > 0) {
                const auto total = static_cast<int>(group.observations.size());
                const auto& first_locked = locked.at(group.observations.front().id);
                resolved.bridge_component_id = first_locked.bridge_component_id;

                if (bound_count < total) {
                    // 批次是原子的，半绑说明是别人动过，不能当已完成放过。
                    add_issue(outcome.issues, "partially_bound",
                              "组内只有部分观测已绑定线索，无法认定为已完成。",
                              group.group_id, resolved.bridge_component_id);
                    continue;
                }
                if (bound_threads.size() > 1) {
                    add_issue(outcome.issues, "thread_split_conflict",
                              "组内观测分散在多条线索上。", group.group_id,
                              resolved.bridge_component_id);
                    continue;
                }

                const auto& existing_thread_id = *bound_threads.begin();
                const auto group_key = review::make_thread_canonical_key(
                    first_locked.bridge_component_id, first_locked.defect_type,
                    first_locked.defect_location);
                const auto thread = std::find_if(
                    threads.begin(), threads.end(),
                    [&existing_thread_id](const ThreadRow& row) {
                        return row.id == existing_thread_id;
                    });
                const bool key_matches = thread != threads.end()
                    && review::make_thread_canonical_key(
                           thread->bridge_component_id, thread->defect_type,
                           thread->defect_location) == group_key;
                // bind 还要求就是请求指定的那条：规范键相同不代表是同一条线索。
                const bool target_matches = request.action != review::TriageAction::Bind
                    || group.target_thread_id == existing_thread_id;

                if (!key_matches || !target_matches) {
                    add_issue(outcome.issues, "bound_to_other_thread",
                              "组内观测已绑定到另一条线索。", group.group_id,
                              resolved.bridge_component_id);
                    continue;
                }

                resolved.already_completed = true;
                resolved.key = group_key;
                resolved.matched_thread_id = existing_thread_id;
                resolved.matched_system_number = thread->system_number;
                ++already_completed_groups;
                resolved_groups.push_back(std::move(resolved));
                continue;
            }

            std::set<std::string> components;
            std::set<std::string> canonical_keys;
            bool group_ok = true;
            for (const auto& requested : group.observations) {
                const auto found = locked.find(requested.id);
                if (found == locked.end()) {
                    add_issue(outcome.issues, "defect_observation_not_found",
                              "观测不存在或已被删除。", group.group_id, {}, requested.id);
                    group_ok = false;
                    continue;
                }
                const auto& observation = found->second;

                if (observation.bridge_id != request.bridge_id) {
                    add_issue(outcome.issues, "observation_bridge_mismatch",
                              "观测不属于本桥。", group.group_id,
                              observation.bridge_component_id, observation.id);
                    group_ok = false;
                }
                if (!observation.year_is_current || observation.year_status != "已确认") {
                    add_issue(outcome.issues, "observation_not_current",
                              "该观测属于旧修订版或未确认的年度版本。", group.group_id,
                              observation.bridge_component_id, observation.id);
                    group_ok = false;
                }
                if (observation.review_status != "已确认" && observation.review_status != "已修改") {
                    add_issue(outcome.issues, "observation_not_formal",
                              "该观测不是正式事实。", group.group_id,
                              observation.bridge_component_id, observation.id);
                    group_ok = false;
                }
                if (observation.updated_at != requested.updated_at) {
                    add_issue(outcome.issues, "observation_revision_conflict",
                              "该观测已被其他操作更新，请刷新后重试。", group.group_id,
                              observation.bridge_component_id, observation.id);
                    group_ok = false;
                }
                if (referenced_by_confirmed_comparison(tx, observation.id)) {
                    add_issue(outcome.issues, "observation_referenced_by_confirmed_comparison",
                              "该观测被模块 07 已确认的对比结论引用，需先撤销对比。",
                              group.group_id, observation.bridge_component_id, observation.id);
                    group_ok = false;
                }

                components.insert(observation.bridge_component_id);
                const auto key = review::make_thread_canonical_key(
                    observation.bridge_component_id, observation.defect_type,
                    observation.defect_location);
                canonical_keys.insert(key.canonical_string());
                resolved.key = key;
                resolved.bridge_component_id = observation.bridge_component_id;
                resolved.defect_type_raw = observation.defect_type;
                resolved.defect_location_raw = observation.defect_location;
            }

            if (components.size() > 1) {
                add_issue(outcome.issues, "group_component_mismatch",
                          "同一组里的观测属于不同构件。", group.group_id);
                group_ok = false;
            }
            if (canonical_keys.size() > 1) {
                add_issue(outcome.issues, "group_key_mismatch",
                          "同一组里的观测病害类型或位置不一致。", group.group_id);
                group_ok = false;
            }

            if (!group_ok) continue;

            // 每个年度至多一条：同年两条到底是两处还是记了两遍，机器判不了。
            const auto year_rows = tx->execSqlSync(
                "select iy.inspection_year, count(*) as n from defect_observations o "
                "join inspection_years iy on iy.id = o.inspection_year_id "
                "where o.id = any($1::uuid[]) group by iy.inspection_year having count(*) > 1",
                [&group]() {
                    std::string literal = "{";
                    for (std::size_t index = 0; index < group.observations.size(); ++index) {
                        if (index > 0) literal += ',';
                        literal += group.observations[index].id;
                    }
                    return literal + "}";
                }());
            if (!year_rows.empty()) {
                add_issue(outcome.issues, "group_multiple_in_year",
                          "组内某个年度有多条观测。", group.group_id, resolved.bridge_component_id);
                continue;
            }

            // 精确命中重算：不看客户端给的 target_thread_id。
            std::vector<const ThreadRow*> matches;
            for (const auto& thread : threads) {
                const auto thread_key = review::make_thread_canonical_key(
                    thread.bridge_component_id, thread.defect_type, thread.defect_location);
                if (thread_key == resolved.key) matches.push_back(&thread);
            }
            if (matches.size() > 1) {
                add_issue(outcome.issues, "ambiguous_thread",
                          "该组精确命中多于一条线索。", group.group_id, resolved.bridge_component_id);
                continue;
            }

            const bool wants_bind = request.action == review::TriageAction::Bind;
            if (wants_bind) {
                if (matches.empty()) {
                    add_issue(outcome.issues, "bind_target_not_matched",
                              "该组当前没有精确匹配的线索。", group.group_id,
                              resolved.bridge_component_id);
                    continue;
                }
                if (group.target_thread_id.empty()
                    || group.target_thread_id != matches.front()->id) {
                    add_issue(outcome.issues, "bind_target_not_matched",
                              "指定的目标线索不是服务端重算出的精确匹配。", group.group_id,
                              resolved.bridge_component_id);
                    continue;
                }
                if (thread_referenced_by_confirmed_comparison(tx, matches.front()->id)) {
                    add_issue(outcome.issues, "thread_referenced_by_confirmed_comparison",
                              "目标线索被模块 07 已确认的对比结论引用，追加观测会改变其跨年跨度。",
                              group.group_id, resolved.bridge_component_id);
                    continue;
                }
                resolved.matched_thread_id = matches.front()->id;
                resolved.matched_system_number = matches.front()->system_number;
            } else {
                if (!group.target_thread_id.empty()) {
                    add_issue(outcome.issues, "unexpected_target_thread",
                              "create 组不应指定目标线索。", group.group_id,
                              resolved.bridge_component_id);
                    continue;
                }
                if (!matches.empty()) {
                    add_issue(outcome.issues, "unexpected_existing_thread",
                              "该组已存在精确匹配的线索，请刷新后改走绑定。", group.group_id,
                              resolved.bridge_component_id);
                    continue;
                }
            }
            resolved_groups.push_back(std::move(resolved));
        }

        if (!outcome.issues.empty()) {
            rollback();
            return outcome;
        }

        if (already_completed_groups > 0) {
            const auto total = static_cast<int>(resolved_groups.size());
            if (already_completed_groups < total) {
                // 批次原子提交，不该出现"一半做过一半没做"，出现即说明有人动过。
                add_issue(outcome.issues, "batch_partially_applied",
                          "本批次只有部分组已完成，请刷新整理工作台后重试。", {});
                rollback();
                return outcome;
            }
            for (const auto& resolved : resolved_groups) {
                outcome.results.push_back(TriageGroupResult{
                    resolved.request_group->group_id, resolved.bridge_component_id,
                    resolved.matched_thread_id, resolved.matched_system_number,
                    "already_completed"});
            }
            rollback();
            outcome.status = TriageApplyStatus::AlreadyCompleted;
            return outcome;
        }

        // ── 阶段三：批量写入 ────────────────────────────────────────
        std::set<std::string> touched_threads;
        for (const auto& resolved : resolved_groups) {
            std::string thread_id = resolved.matched_thread_id;
            std::string system_number = resolved.matched_system_number;
            std::string result_outcome = "bound";

            if (thread_id.empty()) {
                // 线索字段从锁定后的观测派生，不用客户端那份文本。
                // 位置为空时线索名只有病害类型——旧默认逻辑会产出"渗水泛碱｜"这种悬空分隔符。
                const auto& location = resolved.defect_location_raw;
                const auto thread_name = location.empty()
                    ? resolved.defect_type_raw
                    : resolved.defect_type_raw + "｜" + location;
                const auto inserted = tx->execSqlSync(
                    "insert into defect_threads(bridge_id,bridge_component_id,thread_name,"
                    "defect_type,defect_location,confirmation_status) "
                    "values($1::uuid,$2::uuid,$3,$4,nullif($5,''),'人工已确认') "
                    "returning id::text as id, system_number",
                    request.bridge_id, resolved.bridge_component_id, thread_name,
                    resolved.defect_type_raw, location);
                thread_id = inserted[0]["id"].as<std::string>();
                system_number = inserted[0]["system_number"].as<std::string>();
                result_outcome = "created";
                ++outcome.threads_created;
            }

            for (const auto& observation : resolved.request_group->observations) {
                tx->execSqlSync(
                    "update defect_observations set defect_thread_id=$1::uuid, updated_at=now() "
                    "where id=$2::uuid",
                    thread_id, observation.id);
                ++outcome.observations_bound;
            }
            touched_threads.insert(thread_id);
            outcome.results.push_back(TriageGroupResult{
                resolved.request_group->group_id, resolved.bridge_component_id,
                thread_id, system_number, result_outcome});
        }

        // 集中重算，不在每组写入时各算一次。
        for (const auto& thread_id : touched_threads) recompute_thread_span(tx, thread_id);

        tx.reset();
        if (!latch->wait()) {
            add_issue(outcome.issues, "database_commit_failed", "数据库提交失败。", {});
            outcome.results.clear();
            outcome.threads_created = 0;
            outcome.observations_bound = 0;
            return outcome;
        }
        outcome.status = TriageApplyStatus::Applied;
        return outcome;
    } catch (const drogon::orm::DrogonDbException& exception) {
        rollback();
        outcome.issues.clear();
        add_issue(outcome.issues, "db_write_failed", exception.base().what(), {});
        return outcome;
    } catch (const std::exception& exception) {
        rollback();
        outcome.issues.clear();
        add_issue(outcome.issues, "db_write_failed", exception.what(), {});
        return outcome;
    }
}


TriageApplyOutcome ThreadResolutionRepository::resolve(
    const TriageResolveRequest& request) const {
    TriageApplyOutcome outcome;
    // 异常簇一次只落一组，规模记 1 组即可；口径与 apply 一致才好横向比。
    const ScopeTimer timer("resolve", outcome, 1);
    TransactionPtr tx;
    const auto latch = std::make_shared<CommitLatch>();
    const auto rollback = [&tx]() {
        if (tx != nullptr) {
            try { tx->rollback(); } catch (...) {}
        }
    };

    if (request.observations.empty()) {
        add_issue(outcome.issues, "empty_group_selection", "本次决策没有选中任何观测。", {});
        return outcome;
    }
    if (request.bridge_component_id.empty()) {
        add_issue(outcome.issues, "resolve_component_required", "必须指定构件。", {});
        return outcome;
    }

    try {
        tx = db_client_->newTransaction(latch->callback());

        std::vector<std::string> observation_ids;
        std::set<std::string> seen;
        for (const auto& observation : request.observations) {
            if (!seen.insert(observation.id).second) {
                add_issue(outcome.issues, "duplicate_observation",
                          "同一条观测重复出现。", {}, {}, observation.id);
                continue;
            }
            observation_ids.push_back(observation.id);
        }
        if (!outcome.issues.empty()) {
            rollback();
            return outcome;
        }
        std::sort(observation_ids.begin(), observation_ids.end());
        const auto locked = lock_observations(tx, observation_ids);

        std::set<std::string> canonical_keys;
        for (const auto& requested : request.observations) {
            const auto found = locked.find(requested.id);
            if (found == locked.end()) {
                add_issue(outcome.issues, "defect_observation_not_found",
                          "观测不存在或已被删除。", {}, {}, requested.id);
                continue;
            }
            const auto& observation = found->second;
            if (observation.bridge_id != request.bridge_id) {
                add_issue(outcome.issues, "observation_bridge_mismatch", "观测不属于本桥。",
                          {}, observation.bridge_component_id, observation.id);
            }
            if (observation.bridge_component_id != request.bridge_component_id) {
                // 线索属于具体构件，跨构件合并不是"人工判断"能豁免的。
                add_issue(outcome.issues, "group_component_mismatch",
                          "选中的观测不属于指定构件。", {},
                          observation.bridge_component_id, observation.id);
            }
            if (!observation.year_is_current || observation.year_status != "已确认") {
                add_issue(outcome.issues, "observation_not_current",
                          "该观测属于旧修订版或未确认的年度版本。", {},
                          observation.bridge_component_id, observation.id);
            }
            if (observation.review_status != "已确认" && observation.review_status != "已修改") {
                add_issue(outcome.issues, "observation_not_formal", "该观测不是正式事实。",
                          {}, observation.bridge_component_id, observation.id);
            }
            if (observation.updated_at != requested.updated_at) {
                add_issue(outcome.issues, "observation_revision_conflict",
                          "该观测已被其他操作更新，请刷新后重试。", {},
                          observation.bridge_component_id, observation.id);
            }
            if (observation.defect_thread_id.has_value()) {
                add_issue(outcome.issues, "observation_already_bound", "该观测已绑定线索。",
                          {}, observation.bridge_component_id, observation.id);
            }
            if (referenced_by_confirmed_comparison(tx, observation.id)) {
                add_issue(outcome.issues, "observation_referenced_by_confirmed_comparison",
                          "该观测被模块 07 已确认的对比结论引用。", {},
                          observation.bridge_component_id, observation.id);
            }
            canonical_keys.insert(review::make_thread_canonical_key(
                observation.bridge_component_id, observation.defect_type,
                observation.defect_location).canonical_string());
        }

        // 键不止一种 = 非精确合并。这正是异常簇的用途，但必须由人显式担责。
        if (canonical_keys.size() > 1 && !request.confirm_inexact_merge) {
            add_issue(outcome.issues, "inexact_merge_requires_confirmation",
                      "选中的观测位置或类型不一致，合并需要显式确认。", {},
                      request.bridge_component_id);
        }

        std::string thread_id = request.target_thread_id;
        std::string system_number;
        std::string result_outcome = "bound";

        if (request.action == review::TriageAction::Bind) {
            if (thread_id.empty()) {
                add_issue(outcome.issues, "bind_target_required", "绑定必须指定目标线索。", {});
            } else {
                const auto threads = load_component_threads(tx, request.bridge_id);
                const auto target = std::find_if(
                    threads.begin(), threads.end(),
                    [&thread_id](const ThreadRow& row) { return row.id == thread_id; });
                if (target == threads.end()) {
                    add_issue(outcome.issues, "defect_thread_not_found", "目标线索不存在。", {});
                } else if (target->bridge_component_id != request.bridge_component_id) {
                    add_issue(outcome.issues, "thread_component_mismatch",
                              "目标线索不属于指定构件。", {}, request.bridge_component_id);
                } else if (thread_referenced_by_confirmed_comparison(tx, thread_id)) {
                    add_issue(outcome.issues, "thread_referenced_by_confirmed_comparison",
                              "目标线索被模块 07 已确认的对比结论引用。", {},
                              request.bridge_component_id);
                } else {
                    system_number = target->system_number;
                }
            }
        } else if (request.defect_type.empty()) {
            add_issue(outcome.issues, "resolve_defect_type_required",
                      "新建线索必须指定标准病害类型。", {}, request.bridge_component_id);
        }

        if (!outcome.issues.empty()) {
            rollback();
            return outcome;
        }

        if (request.action == review::TriageAction::Create) {
            const auto thread_name = request.defect_location.empty()
                ? request.defect_type
                : request.defect_type + "｜" + request.defect_location;
            const auto inserted = tx->execSqlSync(
                "insert into defect_threads(bridge_id,bridge_component_id,thread_name,"
                "defect_type,defect_location,confirmation_status) "
                "values($1::uuid,$2::uuid,$3,$4,nullif($5,''),'人工已确认') "
                "returning id::text as id, system_number",
                request.bridge_id, request.bridge_component_id, thread_name,
                request.defect_type, request.defect_location);
            thread_id = inserted[0]["id"].as<std::string>();
            system_number = inserted[0]["system_number"].as<std::string>();
            result_outcome = "created";
            ++outcome.threads_created;
        }

        for (const auto& observation : request.observations) {
            tx->execSqlSync(
                "update defect_observations set defect_thread_id=$1::uuid, updated_at=now() "
                "where id=$2::uuid",
                thread_id, observation.id);
            ++outcome.observations_bound;
        }
        recompute_thread_span(tx, thread_id);

        outcome.results.push_back(TriageGroupResult{
            {}, request.bridge_component_id, thread_id, system_number, result_outcome});

        tx.reset();
        if (!latch->wait()) {
            outcome.results.clear();
            outcome.threads_created = 0;
            outcome.observations_bound = 0;
            add_issue(outcome.issues, "database_commit_failed", "数据库提交失败。", {});
            return outcome;
        }
        outcome.status = TriageApplyStatus::Applied;
        return outcome;
    } catch (const drogon::orm::DrogonDbException& exception) {
        rollback();
        outcome.issues.clear();
        add_issue(outcome.issues, "db_write_failed", exception.base().what(), {});
        return outcome;
    } catch (const std::exception& exception) {
        rollback();
        outcome.issues.clear();
        add_issue(outcome.issues, "db_write_failed", exception.what(), {});
        return outcome;
    }
}

}  // namespace bridge_report::db
