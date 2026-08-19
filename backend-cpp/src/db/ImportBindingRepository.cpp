#include "bridge_report/db/ImportBindingRepository.hpp"

#include "bridge_report/db/RatingTreeRepository.hpp"
#include "bridge_report/review/DraftValidation.hpp"
#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>

#include <json/json.h>

#include "bridge_report/auth/PasswordHash.hpp"
#include "bridge_report/db/CommitLatch.hpp"
#include "bridge_report/db/ComponentInventoryRepository.hpp"
#include "bridge_report/inventory/ComponentCategoryLexicon.hpp"
#include "bridge_report/inventory/ComponentMatcher.hpp"
#include "bridge_report/inventory/ComponentRangeParser.hpp"
#include "bridge_report/review/ContractCompatibility.hpp"

namespace bridge_report::db {
namespace {

using TransactionPtr = std::shared_ptr<drogon::orm::Transaction>;

// 在写事务内复查编辑锁。路由层已经拦过一道，但那是**事务外**的检查：从那一刻到
// 真正写入之间，锁可能过期或被管理员强制收回。与 save_review_draft /
// confirm_annual_facts 同一套做法。传 nullopt 表示调用方不校验锁（测试夹具用）。
bool edit_lock_still_active(
    const TransactionPtr& tx, const std::string& import_id,
    const std::optional<EditLockCredentials>& edit_lock) {
    if (!edit_lock.has_value()) return true;
    const auto rows = tx->execSqlSync(
        "select exists(select 1 from import_record_edit_locks "
        "where import_record_id=$1::uuid and user_id=$2::uuid and user_session_id=$3::uuid "
        "and lock_token_hash=$4 and expires_at>now()) as active",
        import_id, edit_lock->user_id, edit_lock->session_id,
        auth::sha256_hex(edit_lock->lock_token));
    return !rows.empty() && rows[0]["active"].as<bool>();
}

std::string compact_json(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

bool parse_json(const std::string& text, Json::Value& out) {
    Json::CharReaderBuilder builder;
    std::string errors;
    const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    return reader->parse(text.c_str(), text.c_str() + text.size(), &out, &errors);
}

std::string defect_str(const Json::Value& defect, const char* key) {
    return defect.isObject() && defect[key].isString() ? defect[key].asString() : std::string();
}

std::string contract_structure_part(const std::string& value) {
    if (value == "superstructure") return "上部结构";
    if (value == "substructure") return "下部结构";
    if (value == "deck_system") return "桥面系";
    if (value == "overall") return "全桥";
    return "其他";
}

BindingOverview aggregate(const Json::Value& parsed, bool inventory_confirmed) {
    BindingOverview overview;
    overview.inventory_confirmed = inventory_confirmed;
    if (!parsed["defects"].isArray()) return overview;

    std::unordered_map<std::string, std::size_t> group_index;
    std::vector<std::unordered_map<std::string, std::size_t>> row_index;
    for (const auto& defect : parsed["defects"]) {
        const auto part_name = defect_str(defect, "component_name");
        const auto number_raw = defect_str(defect, "component_number");
        const auto normalized = inventory::normalize_component_number(number_raw);

        auto git = group_index.find(part_name);
        if (git == group_index.end()) {
            git = group_index.emplace(part_name, overview.groups.size()).first;
            BindingGroup group;
            group.part_name = part_name;
            overview.groups.push_back(std::move(group));
            row_index.emplace_back();
        }
        auto& group = overview.groups[git->second];
        auto& rows = row_index[git->second];
        auto rit = rows.find(normalized);
        if (rit == rows.end()) {
            BindingRow row;
            row.component_number = number_raw;
            const auto method = defect_str(defect, "component_match_method");
            const bool has_component = defect["bridge_component_id"].isString()
                && !defect["bridge_component_id"].asString().empty();
            if (defect["component_match_candidate_ids"].isArray()) {
                for (const auto& candidate : defect["component_match_candidate_ids"]) {
                    if (candidate.isString()) row.candidate_component_ids.push_back(candidate.asString());
                }
            }
            if (method == "missing") {
                row.status = "missing";
            } else if (has_component) {
                row.status = "bound";
                row.bridge_component_id = defect["bridge_component_id"].asString();
            } else if (!row.candidate_component_ids.empty()) {
                row.status = "ambiguous";
            } else {
                row.status = "unmatched";
            }
            if (row.status == "unmatched" || row.status == "ambiguous") {
                const auto range = inventory::parse_component_range(number_raw);
                row.split_eligible =
                    range.status == inventory::ComponentRangeParseStatus::Ok;
                if (row.split_eligible) {
                    row.split_expanded_count = static_cast<int>(range.numbers.size());
                }
            }
            rit = rows.emplace(normalized, group.rows.size()).first;
            group.rows.push_back(std::move(row));
        }
        group.rows[rit->second].defect_count += 1;
    }

    for (auto& group : overview.groups) {
        group.total = static_cast<int>(group.rows.size());
        for (const auto& row : group.rows) {
            if (row.status == "bound") ++group.bound;
            else if (row.status == "missing") ++group.missing;
            else if (row.status == "ambiguous") ++group.ambiguous;
            else ++group.unmatched;
        }
    }
    return overview;
}

// 对匹配 (部件名称, 归一化编号) 的所有病害应用 mutator，返回命中条数。
int apply_to_group(Json::Value& parsed, const std::string& part_name,
                   const std::string& normalized_number,
                   const std::function<void(Json::Value&)>& mutator) {
    if (!parsed["defects"].isArray()) return 0;
    int applied = 0;
    for (auto& defect : parsed["defects"]) {
        if (defect_str(defect, "component_name") == part_name
            && inventory::normalize_component_number(defect_str(defect, "component_number"))
                   == normalized_number) {
            mutator(defect);
            ++applied;
        }
    }
    return applied;
}

std::optional<std::string> optional_row_text(
    const drogon::orm::Row& row, const std::string& column) {
    return row[column].isNull()
        ? std::nullopt
        : std::optional<std::string>(row[column].as<std::string>());
}

// 只要版本 id 的路径走这条：概览、标记缺失/清除、评定树绑定都不需要构件内容，
// 为拿一个 id 去装配五千多条构件是纯粹的浪费。
std::optional<ComponentInventoryRepository::ConfirmedRevisionRef> resolve_confirmed_revision_ref(
    const drogon::orm::DbClientPtr& client,
    const std::string& bridge_id,
    const std::optional<std::string>& locked_revision_id) {
    return ComponentInventoryRepository(client).resolve_confirmed_revision_ref(
        bridge_id, locked_revision_id);
}

// 规则本身住在 ComponentInventoryRepository，范围拆分与这里共用同一份。
std::optional<inventory::InventoryRevision> resolve_confirmed_revision(
    const drogon::orm::DbClientPtr& client,
    const std::string& bridge_id,
    const std::optional<std::string>& locked_revision_id) {
    return ComponentInventoryRepository(client).resolve_confirmed_revision(
        bridge_id, locked_revision_id);
}

// 概览此前会把整份台账（五千多条构件加同样多的映射）装配一遍，只为把行上的几十个
// id 换成可读信息。改成按这批 id 定向查一次。
void fill_component_summaries(
    const drogon::orm::DbClientPtr& client,
    const std::string& revision_id,
    BindingOverview& overview) {
    std::vector<std::string> wanted;
    for (const auto& group : overview.groups) {
        for (const auto& row : group.rows) {
            if (row.bridge_component_id.has_value()) wanted.push_back(*row.bridge_component_id);
            wanted.insert(wanted.end(), row.candidate_component_ids.begin(),
                          row.candidate_component_ids.end());
        }
    }
    if (wanted.empty()) return;
    std::sort(wanted.begin(), wanted.end());
    wanted.erase(std::unique(wanted.begin(), wanted.end()), wanted.end());

    std::unordered_map<std::string, BindingComponentSummary> by_component;
    for (auto& entry : ComponentInventoryRepository(client)
                           .load_bindable_entries_by_component_ids(revision_id, wanted)) {
        BindingComponentSummary summary;
        summary.entry_id = entry.id;
        summary.bridge_component_id = entry.bridge_component_id;
        summary.component_number = entry.component_number;
        summary.site_component_type = entry.site_component_type;
        summary.site_name = entry.site_name;
        by_component.emplace(entry.bridge_component_id, std::move(summary));
    }

    for (auto& group : overview.groups) {
        for (auto& row : group.rows) {
            if (row.bridge_component_id.has_value()) {
                const auto found = by_component.find(*row.bridge_component_id);
                if (found != by_component.end()) row.bound_component = found->second;
            }
            // 按 candidate_component_ids 的原顺序回填：SQL 的返回顺序不保证与它一致，
            // 照返回顺序装的话候选显示顺序会漂，相关断言也跟着不稳。
            for (const auto& id : row.candidate_component_ids) {
                const auto found = by_component.find(id);
                if (found != by_component.end()) row.candidate_components.push_back(found->second);
            }
        }
    }
}

// 台账版本在两次请求之间被人换掉了。必须让用户看见这件事：静默改用新版本的话，
// 他看到的候选来自旧版本，校验却按新版本走，被拒时无从理解发生了什么。
BindingOutcome revision_changed_outcome() {
    BindingOutcome outcome{BindingStatus::Conflict};
    outcome.error_code = "component_inventory_revision_changed";
    outcome.error_message = "构件台账版本已变化，请刷新后重试。";
    return outcome;
}

// 锁定规则同样住在 ComponentInventoryRepository，范围拆分应用与这里共用一份。
bool attach_revision_to_pending_year(
    const drogon::orm::DbClientPtr& client,
    const std::optional<std::string>& year_id,
    const std::string& bridge_id,
    const std::optional<std::string>& locked_revision_id,
    const std::string& revision_id) {
    return ComponentInventoryRepository(client).lock_pending_year_revision(
        year_id, bridge_id, locked_revision_id, revision_id);
}

}  // namespace

ImportBindingRepository::ImportBindingRepository(drogon::orm::DbClientPtr db_client)
    : db_client_(std::move(db_client)) {}

BindingOutcome ImportBindingRepository::load_replace_inventory(
    const std::string& import_id, const std::string& expected_revision_id) {
    try {
        const auto rows = db_client_->execSqlSync(
            "select ir.bridge_id::text as bridge_id,ir.import_status,"
            "iy.component_inventory_revision_id::text as inventory_revision_id "
            "from import_records ir "
            "left join inspection_years iy on iy.id=ir.inspection_year_id "
            "where ir.id=$1::uuid",
            import_id);
        if (rows.empty()) return {BindingStatus::NotFound};
        if (rows[0]["import_status"].as<std::string>() != "待校对") {
            return {BindingStatus::Conflict};
        }
        const auto revision = resolve_confirmed_revision_ref(
            db_client_, rows[0]["bridge_id"].as<std::string>(),
            optional_row_text(rows[0], "inventory_revision_id"));
        if (!revision.has_value()) {
            BindingOutcome outcome{BindingStatus::Conflict};
            outcome.error_code = "component_binding_inventory_unavailable";
            outcome.error_message = "该桥尚无可用的已确认构件台账。";
            return outcome;
        }
        // 只校验，不锁定——这是读操作。
        if (revision->id != expected_revision_id) return revision_changed_outcome();

        BindingOutcome outcome;
        outcome.replace_revision_id = revision->id;
        outcome.replace_entries =
            ComponentInventoryRepository(db_client_).load_bindable_replace_entries(revision->id);
        return outcome;
    } catch (...) {
        return {BindingStatus::Failed};
    }
}

BindingOutcome ImportBindingRepository::overview(const std::string& import_id) {
    try {
        const auto rows = db_client_->execSqlSync(
            "select ir.bridge_id::text as bridge_id,ir.import_status,"
            "iy.component_inventory_revision_id::text as inventory_revision_id,"
            "rtv.id::text as rating_tree_version_id,rtv.tree_name,"
            "rtv.package_version as rating_tree_package_version,"
            "technical.package_version as h21_package_version,"
            "maintenance.package_version as maintenance_package_version,"
            "coalesce(ir.parsed_result_json::text,'{}') as parsed "
            "from import_records ir "
            "left join inspection_years iy on iy.id=ir.inspection_year_id "
            "left join project_standard_profiles profile on profile.id=iy.standard_profile_id "
            "left join rating_tree_versions rtv on rtv.id=profile.rating_tree_version_id "
            "left join standard_packages technical "
            "on technical.id=profile.technical_condition_package_id "
            "left join standard_packages maintenance "
            "on maintenance.id=profile.maintenance_package_id "
            "where ir.id=$1::uuid",
            import_id);
        if (rows.empty()) return {BindingStatus::NotFound};
        if (rows[0]["import_status"].as<std::string>() != "待校对") return {BindingStatus::Conflict};
        Json::Value parsed;
        parse_json(rows[0]["parsed"].as<std::string>(), parsed);
        const auto revision = resolve_confirmed_revision_ref(
            db_client_, rows[0]["bridge_id"].as<std::string>(),
            optional_row_text(rows[0], "inventory_revision_id"));
        const bool confirmed = revision.has_value();
        BindingOutcome outcome;
        outcome.overview = aggregate(parsed, confirmed);
        // 与 inventory_confirmed 严格同生共死：两者不得出现矛盾组合。
        if (confirmed) {
            outcome.overview->inventory_revision_id = revision->id;
            fill_component_summaries(db_client_, revision->id, *outcome.overview);
        }
        if (const auto version_id =
                optional_row_text(rows[0], "rating_tree_version_id");
            version_id.has_value()) {
            BindingRatingTree tree;
            tree.version_id = *version_id;
            tree.tree_name = rows[0]["tree_name"].as<std::string>();
            tree.package_version =
                rows[0]["rating_tree_package_version"].as<std::string>();
            tree.h21_package_version =
                rows[0]["h21_package_version"].as<std::string>();
            tree.maintenance_package_version =
                rows[0]["maintenance_package_version"].as<std::string>();
            outcome.overview->rating_tree = std::move(tree);
        }
        return outcome;
    } catch (...) {
        return {BindingStatus::Failed};
    }
}

namespace {

// 单条与批量共用的目标校验：所选构件须属于已确认台账，且其活动映射类别与报告
// 部件名称的对照相符。返回该构件的活动映射，nullptr 表示不合法。
const inventory::InventoryMapping* validate_target(
    const inventory::InventoryRevision& revision, const std::string& part_name,
    const std::string& bridge_component_id) {
    const inventory::InventoryMapping* mapping = nullptr;
    for (const auto& entry : revision.entries) {
        if (!entry.is_active || entry.bridge_component_id != bridge_component_id) continue;
        for (const auto& candidate : entry.mappings) {
            if (candidate.is_active) { mapping = &candidate; break; }
        }
        break;
    }
    if (mapping == nullptr) return nullptr;
    const auto categories = inventory::resolve_component_categories(part_name);
    if (!categories.empty()
        && std::find(categories.begin(), categories.end(),
                     mapping->standard_component_category_id) == categories.end()) {
        return nullptr;
    }
    return mapping;
}

void write_binding(
    Json::Value& defect, const std::string& bridge_component_id,
    const std::string& category_id, const std::string& structure_part,
    const std::string& revision_id) {
    defect["bridge_component_id"] = bridge_component_id;
    defect["component_match_method"] = "manual";
    defect["standard_component_category_id"] = category_id;
    defect["resolved_structure_part"] = structure_part;
    defect["component_inventory_revision_id"] = revision_id;
    defect["rating_tree_node_id"] = Json::Value(Json::nullValue);
    defect["standard_defect_indicator_id"] = Json::Value(Json::nullValue);
    defect["rating_tree_match_method"] = Json::Value(Json::nullValue);
    defect["rating_tree_match_evidence"] = Json::Value(Json::nullValue);
    review::reconcile_defect_component_match_warning(defect);
}

}  // namespace

BindingOutcome ImportBindingRepository::bind_batch(
    const std::string& import_id, const std::vector<BindingTarget>& targets,
    const std::string& expected_revision_id,
    const std::optional<EditLockCredentials>& edit_lock) {
    if (targets.empty()) return {BindingStatus::Invalid};
    for (const auto& target : targets) {
        if (target.part_name.empty() || target.component_number.empty()
            || target.bridge_component_id.empty()) {
            return {BindingStatus::Invalid, std::nullopt, target.component_number};
        }
    }
    TransactionPtr tx;
    const auto latch = std::make_shared<CommitLatch>();
    const auto rollback = [&]() { if (tx) { try { tx->rollback(); } catch (...) {} } };
    try {
        tx = db_client_->newTransaction(latch->callback());
        const auto rows = tx->execSqlSync(
            "select ir.bridge_id::text as bridge_id,ir.inspection_year_id::text as inspection_year_id,"
            "ir.import_status,iy.component_inventory_revision_id::text as inventory_revision_id,"
            "psp.rating_tree_version_id::text as rating_tree_version_id,"
            "psp.technical_condition_package_id::text as technical_package_id,"
            "coalesce(ir.parsed_result_json::text,'{}') as parsed "
            "from import_records ir "
            "left join inspection_years iy on iy.id=ir.inspection_year_id "
            "left join project_standard_profiles psp on psp.id=iy.standard_profile_id "
            "where ir.id=$1::uuid for update of ir",
            import_id);
        if (rows.empty()) { rollback(); return {BindingStatus::NotFound}; }
        if (rows[0]["import_status"].as<std::string>() != "待校对") {
            rollback(); return {BindingStatus::Conflict};
        }
        if (!edit_lock_still_active(tx, import_id, edit_lock)) {
            rollback(); return {BindingStatus::EditLockInvalid};
        }
        const auto bridge_id = rows[0]["bridge_id"].as<std::string>();
        const auto year_id = optional_row_text(rows[0], "inspection_year_id");
        const auto locked_revision_id =
            optional_row_text(rows[0], "inventory_revision_id");
        const auto revision =
            resolve_confirmed_revision(tx, bridge_id, locked_revision_id);
        if (!revision.has_value()) { rollback(); return {BindingStatus::Conflict}; }
        if (revision->id != expected_revision_id) {
            rollback(); return revision_changed_outcome();
        }
        if (!attach_revision_to_pending_year(
            tx, year_id, bridge_id, locked_revision_id, revision->id)) {
            rollback(); return {BindingStatus::Conflict};
        }

        Json::Value parsed;
        parse_json(rows[0]["parsed"].as<std::string>(), parsed);
        const Json::Value stored = parsed;
        // 先全量校验并逐个改写，任一目标不合法即整批回滚——半绑状态会让用户
        // 无从判断哪些生效了。
        for (const auto& target : targets) {
            const auto* mapping =
                validate_target(*revision, target.part_name, target.bridge_component_id);
            if (mapping == nullptr) {
                rollback();
                return {BindingStatus::Conflict, std::nullopt, target.component_number};
            }
            const auto normalized = inventory::normalize_component_number(target.component_number);
            const auto structure_part = contract_structure_part(mapping->structure_part);
            const auto category_id = mapping->standard_component_category_id;
            const auto revision_id = revision->id;
            const auto component_id = target.bridge_component_id;
            const int applied = apply_to_group(parsed, target.part_name, normalized,
                [&](Json::Value& defect) {
                    write_binding(defect, component_id, category_id, structure_part, revision_id);
                });
            if (applied == 0) {
                rollback();
                return {BindingStatus::Invalid, std::nullopt, target.component_number};
            }
        }

        const auto tree_version_id =
            optional_row_text(rows[0], "rating_tree_version_id");
        const auto technical_package_id =
            optional_row_text(rows[0], "technical_package_id");
        if (tree_version_id.has_value() &&
            technical_package_id.has_value()) {
            RatingTreeRepository tree_repository(tx);
            const auto tree =
                tree_repository.load_published_tree(*tree_version_id);
            if (!tree.has_value() ||
                !review::normalize_defect_rating_tree_associations(
                     parsed,
                     stored,
                     *tree_version_id,
                     *technical_package_id,
                     *tree,
                     revision).ok) {
                rollback();
                return {BindingStatus::Conflict};
            }
        }

        tx->execSqlSync(
            "update import_records set parsed_result_json=$2::jsonb where id=$1::uuid",
            import_id, compact_json(parsed));
        tx.reset();
        if (!latch->wait()) return {BindingStatus::Failed};
        return overview(import_id);
    } catch (...) {
        rollback();
        return {BindingStatus::Failed};
    }
}

BindingOutcome ImportBindingRepository::bind(
    const std::string& import_id, const std::string& part_name,
    const std::string& component_number, const std::string& bridge_component_id,
    const std::string& expected_revision_id,
    const std::optional<EditLockCredentials>& edit_lock) {
    if (part_name.empty() || component_number.empty() || bridge_component_id.empty()) {
        return {BindingStatus::Invalid};
    }
    TransactionPtr tx;
    const auto latch = std::make_shared<CommitLatch>();
    const auto rollback = [&]() { if (tx) { try { tx->rollback(); } catch (...) {} } };
    try {
        tx = db_client_->newTransaction(latch->callback());
        const auto rows = tx->execSqlSync(
            "select ir.bridge_id::text as bridge_id,ir.inspection_year_id::text as inspection_year_id,"
            "ir.import_status,iy.component_inventory_revision_id::text as inventory_revision_id,"
            "psp.rating_tree_version_id::text as rating_tree_version_id,"
            "psp.technical_condition_package_id::text as technical_package_id,"
            "coalesce(ir.parsed_result_json::text,'{}') as parsed "
            "from import_records ir "
            "left join inspection_years iy on iy.id=ir.inspection_year_id "
            "left join project_standard_profiles psp on psp.id=iy.standard_profile_id "
            "where ir.id=$1::uuid for update of ir",
            import_id);
        if (rows.empty()) { rollback(); return {BindingStatus::NotFound}; }
        if (rows[0]["import_status"].as<std::string>() != "待校对") {
            rollback(); return {BindingStatus::Conflict};
        }
        if (!edit_lock_still_active(tx, import_id, edit_lock)) {
            rollback(); return {BindingStatus::EditLockInvalid};
        }
        const auto bridge_id = rows[0]["bridge_id"].as<std::string>();
        const auto year_id = optional_row_text(rows[0], "inspection_year_id");
        const auto locked_revision_id =
            optional_row_text(rows[0], "inventory_revision_id");
        const auto revision =
            resolve_confirmed_revision(tx, bridge_id, locked_revision_id);
        if (!revision.has_value()) { rollback(); return {BindingStatus::Conflict}; }
        if (revision->id != expected_revision_id) {
            rollback(); return revision_changed_outcome();
        }
        if (!attach_revision_to_pending_year(
            tx, year_id, bridge_id, locked_revision_id, revision->id)) {
            rollback(); return {BindingStatus::Conflict};
        }
        // 校验所选构件属于已确认台账，且其活动映射类别符合报告部件名称对照。
        const auto* mapping = validate_target(*revision, part_name, bridge_component_id);
        if (mapping == nullptr) { rollback(); return {BindingStatus::Conflict}; }

        Json::Value parsed;
        parse_json(rows[0]["parsed"].as<std::string>(), parsed);
        const Json::Value stored = parsed;
        const auto normalized = inventory::normalize_component_number(component_number);
        const auto structure_part = contract_structure_part(mapping->structure_part);
        const auto revision_id = revision->id;
        const auto category_id = mapping->standard_component_category_id;
        const int applied = apply_to_group(parsed, part_name, normalized, [&](Json::Value& defect) {
            write_binding(defect, bridge_component_id, category_id, structure_part, revision_id);
        });
        if (applied == 0) { rollback(); return {BindingStatus::Invalid}; }
        const auto tree_version_id =
            optional_row_text(rows[0], "rating_tree_version_id");
        const auto technical_package_id =
            optional_row_text(rows[0], "technical_package_id");
        if (tree_version_id.has_value() &&
            technical_package_id.has_value()) {
            RatingTreeRepository tree_repository(tx);
            const auto tree =
                tree_repository.load_published_tree(*tree_version_id);
            if (!tree.has_value() ||
                !review::normalize_defect_rating_tree_associations(
                     parsed,
                     stored,
                     *tree_version_id,
                     *technical_package_id,
                     *tree,
                     revision).ok) {
                rollback();
                return {BindingStatus::Conflict};
            }
        }
        tx->execSqlSync(
            "update import_records set parsed_result_json=$2::jsonb where id=$1::uuid",
            import_id, compact_json(parsed));
        tx.reset();
        if (!latch->wait()) return {BindingStatus::Failed};
        return overview(import_id);
    } catch (...) {
        rollback();
        return {BindingStatus::Failed};
    }
}

BindingOutcome ImportBindingRepository::bind_rating_tree(
    const std::string& import_id,
    const std::string& rating_tree_version_id,
    const std::string& actor_user_id,
    const std::string& expected_revision_id,
    const std::optional<EditLockCredentials>& edit_lock) {
    if (import_id.empty() || rating_tree_version_id.empty() ||
        actor_user_id.empty()) {
        return {BindingStatus::Invalid};
    }

    TransactionPtr tx;
    const auto latch = std::make_shared<CommitLatch>();
    const auto rollback = [&]() {
        if (tx) {
            try { tx->rollback(); } catch (...) {}
        }
    };
    try {
        tx = db_client_->newTransaction(latch->callback());
        const auto context = tx->execSqlSync(
            "select ir.bridge_id::text as bridge_id,"
            "ir.inspection_year_id::text as inspection_year_id,"
            "ir.import_status,iy.status as inspection_year_status,"
            "iy.component_inventory_revision_id::text as inventory_revision_id,"
            "profile.rating_tree_version_id::text as current_tree_version_id,"
            "profile.technical_condition_package_id::text as current_technical_package_id,"
            "coalesce(ir.parsed_result_json::text,'{}') as parsed "
            "from import_records ir "
            "left join inspection_years iy on iy.id=ir.inspection_year_id "
            "left join project_standard_profiles profile "
            "on profile.id=iy.standard_profile_id "
            "where ir.id=$1::uuid for update of ir",
            import_id);
        if (context.empty()) {
            rollback();
            return {BindingStatus::NotFound};
        }
        if (!edit_lock_still_active(tx, import_id, edit_lock)) {
            rollback();
            return {BindingStatus::EditLockInvalid};
        }
        const auto year_id =
            optional_row_text(context[0], "inspection_year_id");
        if (!year_id.has_value() ||
            context[0]["import_status"].as<std::string>() != "待校对" ||
            context[0]["inspection_year_status"].as<std::string>() !=
                "待校对") {
            rollback();
            return {BindingStatus::Conflict};
        }
        const auto locked_year = tx->execSqlSync(
            "select id from inspection_years "
            "where id=$1::uuid and status='待校对' for update",
            *year_id);
        if (locked_year.empty()) {
            rollback();
            return {BindingStatus::Conflict};
        }

        const auto target = tx->execSqlSync(
            "select tree.id::text as tree_id,tree.tree_name,"
            "tree.package_version as tree_package_version,"
            "tree.technical_condition_package_id::text as technical_package_id,"
            "tree.maintenance_package_id::text as maintenance_package_id,"
            "technical.package_version as h21_package_version,"
            "maintenance.package_version as maintenance_package_version "
            "from rating_tree_versions tree "
            "join standard_packages technical "
            "on technical.id=tree.technical_condition_package_id "
            "join standard_packages maintenance "
            "on maintenance.id=tree.maintenance_package_id "
            "where tree.id=$1::uuid and tree.status='published' "
            "and technical.is_enabled and technical.sync_status='正常' "
            "and maintenance.is_enabled and maintenance.sync_status='正常'",
            rating_tree_version_id);
        if (target.empty()) {
            const auto exists = tx->execSqlSync(
                "select 1 from rating_tree_versions where id=$1::uuid",
                rating_tree_version_id);
            rollback();
            return {exists.empty() ? BindingStatus::TreeNotFound
                                   : BindingStatus::TreeUnavailable};
        }

        const auto current_tree =
            optional_row_text(context[0], "current_tree_version_id");
        if (current_tree.has_value() &&
            *current_tree == rating_tree_version_id) {
            tx.reset();
            if (!latch->wait()) return {BindingStatus::Failed};
            return overview(import_id);
        }

        const auto successful_formal = tx->execSqlSync(
            "select 1 from assessment_runs "
            "where inspection_year_id=$1::uuid and run_kind='正式' "
            "and result_status='成功' limit 1",
            *year_id);
        if (!successful_formal.empty()) {
            rollback();
            return {BindingStatus::Conflict};
        }

        const auto bridge_id = context[0]["bridge_id"].as<std::string>();
        const auto locked_revision_id =
            optional_row_text(context[0], "inventory_revision_id");
        const auto source_revision =
            resolve_confirmed_revision_ref(tx, bridge_id, locked_revision_id);
        if (!source_revision.has_value()) {
            rollback();
            return {BindingStatus::Conflict};
        }
        // 迁移的起点必须是用户看到的那份台账，否则派生出来的新版本基线就不对了。
        if (source_revision->id != expected_revision_id) {
            rollback(); return revision_changed_outcome();
        }

        const auto target_technical_package_id =
            target[0]["technical_package_id"].as<std::string>();
        std::string target_revision_id = source_revision->id;
        const auto target_mapping_complete = tx->execSqlSync(
            "select not exists ("
            "select 1 from bridge_component_inventory_entries entry "
            "where entry.inventory_revision_id=$1::uuid and entry.is_active "
            "and not exists ("
            "select 1 from bridge_component_standard_mappings mapping "
            "where mapping.inventory_entry_id=entry.id "
            "and mapping.standard_package_id=$2::uuid "
            "and mapping.is_active and mapping.confirmation_status='已确认'"
            ")) as complete",
            source_revision->id, target_technical_package_id);
        const bool can_reuse_inventory =
            !target_mapping_complete.empty() &&
            target_mapping_complete[0]["complete"].as<bool>();

        if (!can_reuse_inventory) {
            const auto current_technical_package_id =
                optional_row_text(context[0], "current_technical_package_id");
            bool source_complete = false;
            if (current_technical_package_id.has_value()) {
                const auto source_mapping_complete = tx->execSqlSync(
                    "select not exists ("
                    "select 1 from bridge_component_inventory_entries entry "
                    "where entry.inventory_revision_id=$1::uuid and entry.is_active "
                    "and not exists ("
                    "select 1 from bridge_component_standard_mappings mapping "
                    "where mapping.inventory_entry_id=entry.id "
                    "and mapping.standard_package_id=$2::uuid "
                    "and mapping.is_active and mapping.confirmation_status='已确认'"
                    ")) as complete",
                    source_revision->id, *current_technical_package_id);
                source_complete = !source_mapping_complete.empty() &&
                    source_mapping_complete[0]["complete"].as<bool>();
            } else {
                const auto source_mapping_complete = tx->execSqlSync(
                    "select not exists ("
                    "select 1 from bridge_component_inventory_entries entry "
                    "where entry.inventory_revision_id=$1::uuid and entry.is_active "
                    "and not exists ("
                    "select 1 from bridge_component_standard_mappings mapping "
                    "where mapping.inventory_entry_id=entry.id "
                    "and mapping.is_active and mapping.confirmation_status='已确认'"
                    ")) as complete",
                    source_revision->id);
                source_complete = !source_mapping_complete.empty() &&
                    source_mapping_complete[0]["complete"].as<bool>();
            }
            if (!source_complete) {
                rollback();
                return {BindingStatus::MappingIncompatible};
            }

            const auto new_revision = tx->execSqlSync(
                "insert into bridge_component_inventory_revisions ("
                "bridge_id,revision_number,baseline_revision_id,created_by_user_id"
                ") select $1::uuid,coalesce(max(revision_number),0)+1,"
                "$2::uuid,$3::uuid "
                "from bridge_component_inventory_revisions "
                "where bridge_id=$1::uuid returning id::text",
                bridge_id, source_revision->id, actor_user_id);
            if (new_revision.empty()) {
                rollback();
                return {BindingStatus::Failed};
            }
            target_revision_id =
                new_revision[0]["id"].as<std::string>();

            tx->execSqlSync(
                "insert into bridge_component_inventory_entries ("
                "inventory_revision_id,bridge_component_id,generation_batch_id,"
                "component_number,site_name,site_component_type,span_or_location,"
                "is_active,deactivated_at,deactivation_reason,sort_order,remarks"
                ") select $2::uuid,bridge_component_id,generation_batch_id,"
                "component_number,site_name,site_component_type,span_or_location,"
                "is_active,deactivated_at,deactivation_reason,sort_order,remarks "
                "from bridge_component_inventory_entries "
                "where inventory_revision_id=$1::uuid",
                source_revision->id, target_revision_id);

            if (current_technical_package_id.has_value()) {
                tx->execSqlSync(
                    "insert into bridge_component_standard_mappings ("
                    "inventory_entry_id,standard_package_id,"
                    "standard_bridge_type_id,standard_component_category_id,"
                    "structure_part,mapping_source,confirmation_status,"
                    "confirmed_by_user_id,confirmed_at,is_active"
                    ") select new_entry.id,$3::uuid,"
                    "source_mapping.standard_bridge_type_id,"
                    "source_mapping.standard_component_category_id,"
                    "source_mapping.structure_part,'评定树切换继承','已确认',"
                    "$4::uuid,now(),source_mapping.is_active "
                    "from bridge_component_inventory_entries old_entry "
                    "join bridge_component_inventory_entries new_entry "
                    "on new_entry.inventory_revision_id=$2::uuid "
                    "and new_entry.bridge_component_id=old_entry.bridge_component_id "
                    "join lateral ("
                    "select mapping.* "
                    "from bridge_component_standard_mappings mapping "
                    "where mapping.inventory_entry_id=old_entry.id "
                    "and mapping.standard_package_id=$5::uuid "
                    "and mapping.is_active "
                    "and mapping.confirmation_status='已确认' "
                    "order by mapping.created_at desc,mapping.id limit 1"
                    ") source_mapping on true "
                    "where old_entry.inventory_revision_id=$1::uuid",
                    source_revision->id, target_revision_id,
                    target_technical_package_id, actor_user_id,
                    *current_technical_package_id);
            } else {
                tx->execSqlSync(
                    "insert into bridge_component_standard_mappings ("
                    "inventory_entry_id,standard_package_id,"
                    "standard_bridge_type_id,standard_component_category_id,"
                    "structure_part,mapping_source,confirmation_status,"
                    "confirmed_by_user_id,confirmed_at,is_active"
                    ") select new_entry.id,$3::uuid,"
                    "source_mapping.standard_bridge_type_id,"
                    "source_mapping.standard_component_category_id,"
                    "source_mapping.structure_part,'评定树绑定继承','已确认',"
                    "$4::uuid,now(),source_mapping.is_active "
                    "from bridge_component_inventory_entries old_entry "
                    "join bridge_component_inventory_entries new_entry "
                    "on new_entry.inventory_revision_id=$2::uuid "
                    "and new_entry.bridge_component_id=old_entry.bridge_component_id "
                    "join lateral ("
                    "select mapping.* "
                    "from bridge_component_standard_mappings mapping "
                    "where mapping.inventory_entry_id=old_entry.id "
                    "and mapping.is_active "
                    "and mapping.confirmation_status='已确认' "
                    "order by mapping.created_at desc,mapping.id limit 1"
                    ") source_mapping on true "
                    "where old_entry.inventory_revision_id=$1::uuid",
                    source_revision->id, target_revision_id,
                    target_technical_package_id, actor_user_id);
            }

            const auto inherited_complete = tx->execSqlSync(
                "select not exists ("
                "select 1 from bridge_component_inventory_entries entry "
                "where entry.inventory_revision_id=$1::uuid and entry.is_active "
                "and not exists ("
                "select 1 from bridge_component_standard_mappings mapping "
                "where mapping.inventory_entry_id=entry.id "
                "and mapping.standard_package_id=$2::uuid "
                "and mapping.is_active and mapping.confirmation_status='已确认'"
                ")) as complete",
                target_revision_id, target_technical_package_id);
            if (inherited_complete.empty() ||
                !inherited_complete[0]["complete"].as<bool>()) {
                rollback();
                return {BindingStatus::MappingIncompatible};
            }
            tx->execSqlSync(
                "update bridge_component_inventory_revisions set "
                "status='已确认',confirmed_by_user_id=$2::uuid,"
                "confirmed_at=now(),confirmation_note="
                "'绑定评定树：继承原台账构件及已确认规范映射',updated_at=now() "
                "where id=$1::uuid",
                target_revision_id, actor_user_id);
        }

        auto profiles = tx->execSqlSync(
            "select id::text as id from project_standard_profiles "
            "where rating_tree_version_id=$1::uuid and status='生效' "
            "order by created_at,id limit 1",
            rating_tree_version_id);
        std::string profile_id;
        if (profiles.empty()) {
            profiles = tx->execSqlSync(
                "insert into project_standard_profiles ("
                "technical_condition_package_id,maintenance_package_id,"
                "rating_tree_version_id,created_by_user_id,change_reason"
                ") values($1::uuid,$2::uuid,$3::uuid,$4::uuid,"
                "'在构件绑定页面绑定评定树') returning id::text as id",
                target_technical_package_id,
                target[0]["maintenance_package_id"].as<std::string>(),
                rating_tree_version_id, actor_user_id);
        }
        if (profiles.empty()) {
            rollback();
            return {BindingStatus::Failed};
        }
        profile_id = profiles[0]["id"].as<std::string>();

        Json::Value parsed;
        if (!parse_json(context[0]["parsed"].as<std::string>(), parsed)) {
            rollback();
            return {BindingStatus::Invalid};
        }
        const Json::Value stored = parsed;
        if (parsed["defects"].isArray()) {
            for (auto& defect : parsed["defects"]) {
                defect["rating_tree_node_id"] =
                    Json::Value(Json::nullValue);
                defect["standard_defect_indicator_id"] =
                    Json::Value(Json::nullValue);
                defect["rating_tree_match_method"] =
                    Json::Value(Json::nullValue);
                defect["rating_tree_match_evidence"] =
                    Json::Value(Json::nullValue);
                if (defect["bridge_component_id"].isString() &&
                    !defect["bridge_component_id"].asString().empty()) {
                    defect["component_inventory_revision_id"] =
                        target_revision_id;
                }
            }
        }

        const auto tree =
            RatingTreeRepository(tx).load_published_tree(
                rating_tree_version_id);
        const auto target_revision =
            ComponentInventoryRepository(tx).get_revision(
                target_revision_id);
        if (!tree.has_value() || !target_revision.has_value() ||
            !review::normalize_defect_rating_tree_associations(
                 parsed, stored, rating_tree_version_id,
                 target_technical_package_id, *tree,
                 target_revision).ok) {
            rollback();
            return {BindingStatus::MappingIncompatible};
        }

        tx->execSqlSync(
            "update inspection_years set standard_profile_id=$2::uuid,"
            "component_inventory_revision_id=$3::uuid,updated_at=now() "
            "where id=$1::uuid and status='待校对'",
            *year_id, profile_id, target_revision_id);
        tx->execSqlSync(
            "update import_records set parsed_result_json=$2::jsonb "
            "where id=$1::uuid",
            import_id, compact_json(parsed));

        tx.reset();
        if (!latch->wait()) return {BindingStatus::Failed};
        return overview(import_id);
    } catch (...) {
        rollback();
        return {BindingStatus::Failed};
    }
}

namespace {

// 标记缺失 / 取消绑定共用：仅改 parsed_result_json，不校验台账（前置由路由把关）。
BindingOutcome mutate_group(
    const drogon::orm::DbClientPtr& client, const std::string& import_id,
    const std::string& part_name, const std::string& component_number,
    const std::string& expected_revision_id,
    const std::optional<EditLockCredentials>& edit_lock,
    const std::function<void(Json::Value&)>& mutator,
    const std::function<BindingOutcome(const std::string&)>& reload) {
    if (part_name.empty() || component_number.empty()) return {BindingStatus::Invalid};
    TransactionPtr tx;
    const auto latch = std::make_shared<CommitLatch>();
    const auto rollback = [&]() { if (tx) { try { tx->rollback(); } catch (...) {} } };
    try {
        tx = client->newTransaction(latch->callback());
        const auto rows = tx->execSqlSync(
            "select ir.bridge_id::text as bridge_id,ir.inspection_year_id::text as inspection_year_id,"
            "ir.import_status,iy.component_inventory_revision_id::text as inventory_revision_id,"
            "coalesce(ir.parsed_result_json::text,'{}') as parsed "
            "from import_records ir "
            "left join inspection_years iy on iy.id=ir.inspection_year_id "
            "where ir.id=$1::uuid for update of ir",
            import_id);
        if (rows.empty()) { rollback(); return {BindingStatus::NotFound}; }
        if (rows[0]["import_status"].as<std::string>() != "待校对") {
            rollback(); return {BindingStatus::Conflict};
        }
        if (!edit_lock_still_active(tx, import_id, edit_lock)) {
            rollback(); return {BindingStatus::EditLockInvalid};
        }
        const auto bridge_id = rows[0]["bridge_id"].as<std::string>();
        const auto year_id = optional_row_text(rows[0], "inspection_year_id");
        const auto locked_revision_id =
            optional_row_text(rows[0], "inventory_revision_id");
        const auto revision =
            resolve_confirmed_revision_ref(tx, bridge_id, locked_revision_id);
        // 标记缺失/清除绑定不需要台账内容，台账未确认时照样可用——这里保持原样，
        // 只在确实解析出版本（也就是下面真会锁定它）时才校验期望版本。
        if (revision.has_value()) {
            if (revision->id != expected_revision_id) {
                rollback(); return revision_changed_outcome();
            }
            if (!attach_revision_to_pending_year(
                tx, year_id, bridge_id, locked_revision_id, revision->id)) {
                rollback(); return {BindingStatus::Conflict};
            }
        }
        Json::Value parsed;
        parse_json(rows[0]["parsed"].as<std::string>(), parsed);
        const auto normalized = inventory::normalize_component_number(component_number);
        const int applied = apply_to_group(parsed, part_name, normalized, mutator);
        if (applied == 0) { rollback(); return {BindingStatus::Invalid}; }
        tx->execSqlSync(
            "update import_records set parsed_result_json=$2::jsonb where id=$1::uuid",
            import_id, compact_json(parsed));
        tx.reset();
        if (!latch->wait()) return {BindingStatus::Failed};
        return reload(import_id);
    } catch (...) {
        rollback();
        return {BindingStatus::Failed};
    }
}

}  // namespace

BindingOutcome ImportBindingRepository::mark_missing(
    const std::string& import_id, const std::string& part_name,
    const std::string& component_number, const std::string& expected_revision_id,
    const std::optional<EditLockCredentials>& edit_lock) {
    return mutate_group(db_client_, import_id, part_name, component_number,
        expected_revision_id, edit_lock,
        [](Json::Value& defect) {
            defect["component_match_method"] = "missing";
            defect["bridge_component_id"] = Json::Value(Json::nullValue);
            defect["standard_component_category_id"] = Json::Value(Json::nullValue);
            defect["resolved_structure_part"] = Json::Value(Json::nullValue);
            defect["rating_tree_node_id"] = Json::Value(Json::nullValue);
            defect["standard_defect_indicator_id"] =
                Json::Value(Json::nullValue);
            defect["rating_tree_match_method"] =
                Json::Value(Json::nullValue);
            defect["rating_tree_match_evidence"] =
                Json::Value(Json::nullValue);
            review::reconcile_defect_component_match_warning(defect);
        },
        [this](const std::string& id) { return overview(id); });
}

BindingOutcome ImportBindingRepository::clear(
    const std::string& import_id, const std::string& part_name,
    const std::string& component_number, const std::string& expected_revision_id,
    const std::optional<EditLockCredentials>& edit_lock) {
    return mutate_group(db_client_, import_id, part_name, component_number,
        expected_revision_id, edit_lock,
        [](Json::Value& defect) {
            defect["component_match_method"] = Json::Value(Json::nullValue);
            defect["bridge_component_id"] = Json::Value(Json::nullValue);
            defect["standard_component_category_id"] = Json::Value(Json::nullValue);
            defect["resolved_structure_part"] = Json::Value(Json::nullValue);
            defect["rating_tree_node_id"] = Json::Value(Json::nullValue);
            defect["standard_defect_indicator_id"] =
                Json::Value(Json::nullValue);
            defect["rating_tree_match_method"] =
                Json::Value(Json::nullValue);
            defect["rating_tree_match_evidence"] =
                Json::Value(Json::nullValue);
            review::reconcile_defect_component_match_warning(defect);
        },
        [this](const std::string& id) { return overview(id); });
}

}  // namespace bridge_report::db
