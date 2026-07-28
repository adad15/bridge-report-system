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

#include "bridge_report/db/CommitLatch.hpp"
#include "bridge_report/db/ComponentInventoryRepository.hpp"
#include "bridge_report/inventory/ComponentCategoryLexicon.hpp"
#include "bridge_report/inventory/ComponentMatcher.hpp"
#include "bridge_report/inventory/ComponentRangeParser.hpp"
#include "bridge_report/review/ContractCompatibility.hpp"

namespace bridge_report::db {
namespace {

using TransactionPtr = std::shared_ptr<drogon::orm::Transaction>;

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

std::optional<inventory::InventoryRevision> resolve_confirmed_revision(
    const drogon::orm::DbClientPtr& client,
    const std::string& bridge_id,
    const std::optional<std::string>& locked_revision_id) {
    const auto revision = locked_revision_id.has_value()
        ? ComponentInventoryRepository(client).get_revision(*locked_revision_id)
        : ComponentInventoryRepository(client).get_latest_revision(bridge_id);
    if (!revision.has_value() || revision->bridge_id != bridge_id
        || !(revision->status == "已确认" || revision->status == "confirmed")) {
        return std::nullopt;
    }
    return revision;
}

bool attach_revision_to_pending_year(
    const drogon::orm::DbClientPtr& client,
    const std::optional<std::string>& year_id,
    const std::string& bridge_id,
    const std::optional<std::string>& locked_revision_id,
    const std::string& revision_id) {
    if (locked_revision_id.has_value()) return *locked_revision_id == revision_id;
    if (!year_id.has_value()) return true;
    const auto updated = client->execSqlSync(
        "update inspection_years "
        "set component_inventory_revision_id=$2::uuid,updated_at=now() "
        "where id=$1::uuid and bridge_id=$3::uuid and status='待校对' "
        "and component_inventory_revision_id is null returning id",
        *year_id, revision_id, bridge_id);
    if (!updated.empty()) return true;
    const auto current = client->execSqlSync(
        "select component_inventory_revision_id::text as revision_id "
        "from inspection_years where id=$1::uuid and bridge_id=$2::uuid",
        *year_id, bridge_id);
    return !current.empty() && !current[0]["revision_id"].isNull()
        && current[0]["revision_id"].as<std::string>() == revision_id;
}

}  // namespace

ImportBindingRepository::ImportBindingRepository(drogon::orm::DbClientPtr db_client)
    : db_client_(std::move(db_client)) {}

BindingOutcome ImportBindingRepository::overview(const std::string& import_id) {
    try {
        const auto rows = db_client_->execSqlSync(
            "select ir.bridge_id::text as bridge_id,ir.import_status,"
            "iy.component_inventory_revision_id::text as inventory_revision_id,"
            "coalesce(ir.parsed_result_json::text,'{}') as parsed "
            "from import_records ir "
            "left join inspection_years iy on iy.id=ir.inspection_year_id "
            "where ir.id=$1::uuid",
            import_id);
        if (rows.empty()) return {BindingStatus::NotFound};
        if (rows[0]["import_status"].as<std::string>() != "待校对") return {BindingStatus::Conflict};
        Json::Value parsed;
        parse_json(rows[0]["parsed"].as<std::string>(), parsed);
        const auto revision = resolve_confirmed_revision(
            db_client_, rows[0]["bridge_id"].as<std::string>(),
            optional_row_text(rows[0], "inventory_revision_id"));
        const bool confirmed = revision.has_value();
        BindingOutcome outcome;
        outcome.overview = aggregate(parsed, confirmed);
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
    const std::string& import_id, const std::vector<BindingTarget>& targets) {
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
        const auto bridge_id = rows[0]["bridge_id"].as<std::string>();
        const auto year_id = optional_row_text(rows[0], "inspection_year_id");
        const auto locked_revision_id =
            optional_row_text(rows[0], "inventory_revision_id");
        const auto revision =
            resolve_confirmed_revision(tx, bridge_id, locked_revision_id);
        if (!revision.has_value() || !attach_revision_to_pending_year(
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
    const std::string& component_number, const std::string& bridge_component_id) {
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
        const auto bridge_id = rows[0]["bridge_id"].as<std::string>();
        const auto year_id = optional_row_text(rows[0], "inspection_year_id");
        const auto locked_revision_id =
            optional_row_text(rows[0], "inventory_revision_id");
        const auto revision =
            resolve_confirmed_revision(tx, bridge_id, locked_revision_id);
        if (!revision.has_value() || !attach_revision_to_pending_year(
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

namespace {

// 标记缺失 / 取消绑定共用：仅改 parsed_result_json，不校验台账（前置由路由把关）。
BindingOutcome mutate_group(
    const drogon::orm::DbClientPtr& client, const std::string& import_id,
    const std::string& part_name, const std::string& component_number,
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
        const auto bridge_id = rows[0]["bridge_id"].as<std::string>();
        const auto year_id = optional_row_text(rows[0], "inspection_year_id");
        const auto locked_revision_id =
            optional_row_text(rows[0], "inventory_revision_id");
        const auto revision =
            resolve_confirmed_revision(tx, bridge_id, locked_revision_id);
        if (revision.has_value() && !attach_revision_to_pending_year(
            tx, year_id, bridge_id, locked_revision_id, revision->id)) {
            rollback(); return {BindingStatus::Conflict};
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
    const std::string& component_number) {
    return mutate_group(db_client_, import_id, part_name, component_number,
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
    const std::string& component_number) {
    return mutate_group(db_client_, import_id, part_name, component_number,
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
