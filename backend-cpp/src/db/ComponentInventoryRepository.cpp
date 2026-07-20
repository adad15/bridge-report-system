#include "bridge_report/db/ComponentInventoryRepository.hpp"

#include <memory>
#include <sstream>
#include <utility>

#include <json/json.h>

#include "bridge_report/db/CommitLatch.hpp"

namespace bridge_report::db {
namespace {

using TransactionPtr = std::shared_ptr<drogon::orm::Transaction>;

std::optional<std::string> optional_text(const drogon::orm::Field& field) {
    return field.isNull() ? std::nullopt : std::optional<std::string>(field.as<std::string>());
}

std::string compact_json(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

std::string legacy_structure_part(const std::string& value) {
    if (value == "superstructure") return "上部结构";
    if (value == "substructure") return "下部结构";
    if (value == "deck_system") return "桥面系";
    if (value == "overall") return "全桥";
    return "其他";
}

bool valid_structure_part(const std::string& value) {
    return value == "superstructure" || value == "substructure" ||
        value == "deck_system" || value == "overall" || value == "other";
}

bool valid_entry_update(const InventoryEntryUpdate& update) {
    return !update.component_number.empty() && !update.site_name.empty() &&
        !update.site_component_type.empty();
}

inventory::InventoryRevision revision_from_client(
    const auto& client,
    const std::string& revision_id) {
    const auto revisions = client->execSqlSync(
        "select id::text,bridge_id::text,revision_number,status,baseline_revision_id::text,"
        "confirmed_at::text from bridge_component_inventory_revisions where id=$1::uuid",
        revision_id);
    if (revisions.empty()) return {};
    inventory::InventoryRevision revision;
    revision.id = revisions[0]["id"].as<std::string>();
    revision.bridge_id = revisions[0]["bridge_id"].as<std::string>();
    revision.revision_number = revisions[0]["revision_number"].as<int>();
    revision.status = revisions[0]["status"].as<std::string>();
    revision.baseline_revision_id = optional_text(revisions[0]["baseline_revision_id"]);
    revision.confirmed_at = optional_text(revisions[0]["confirmed_at"]);

    const auto entries = client->execSqlSync(
        "select e.id::text,e.bridge_component_id::text,e.component_number,e.site_name,"
        "e.site_component_type,e.span_or_location,e.is_active,e.deactivated_at::text,"
        "e.deactivation_reason,e.sort_order,e.remarks,exists("
        "select 1 from defect_observations o where o.bridge_component_id=e.bridge_component_id "
        "union all select 1 from defect_threads t where t.bridge_component_id=e.bridge_component_id "
        "union all select 1 from condition_ratings cr where cr.bridge_component_id=e.bridge_component_id "
        "union all select 1 from inspection_years iy join bridge_component_inventory_entries ie "
        "on ie.inventory_revision_id=iy.component_inventory_revision_id "
        "where ie.bridge_component_id=e.bridge_component_id limit 1) as is_referenced "
        "from bridge_component_inventory_entries e where e.inventory_revision_id=$1::uuid "
        "order by e.sort_order,e.id",
        revision_id);
    for (const auto& row : entries) {
        inventory::InventoryEntry entry;
        entry.id = row["id"].as<std::string>();
        entry.bridge_component_id = row["bridge_component_id"].as<std::string>();
        entry.component_number = row["component_number"].as<std::string>();
        entry.site_name = row["site_name"].as<std::string>();
        entry.site_component_type = row["site_component_type"].as<std::string>();
        entry.span_or_location = optional_text(row["span_or_location"]);
        entry.is_active = row["is_active"].as<bool>();
        entry.deactivated_at = optional_text(row["deactivated_at"]);
        entry.deactivation_reason = optional_text(row["deactivation_reason"]);
        entry.sort_order = row["sort_order"].as<int>();
        entry.remarks = optional_text(row["remarks"]);
        entry.is_referenced = row["is_referenced"].as<bool>();
        const auto mappings = client->execSqlSync(
            "select id::text,standard_package_id::text,standard_bridge_type_id,"
            "standard_component_category_id,structure_part,mapping_source,"
            "confirmation_status,is_active from bridge_component_standard_mappings "
            "where inventory_entry_id=$1::uuid order by is_active desc,created_at,id",
            entry.id);
        for (const auto& mapping_row : mappings) {
            inventory::InventoryMapping mapping;
            mapping.id = mapping_row["id"].as<std::string>();
            mapping.standard_package_id = mapping_row["standard_package_id"].as<std::string>();
            mapping.standard_bridge_type_id =
                mapping_row["standard_bridge_type_id"].as<std::string>();
            mapping.standard_component_category_id =
                mapping_row["standard_component_category_id"].as<std::string>();
            mapping.structure_part = mapping_row["structure_part"].as<std::string>();
            mapping.mapping_source = mapping_row["mapping_source"].as<std::string>();
            mapping.confirmation_status = mapping_row["confirmation_status"].as<std::string>();
            mapping.is_active = mapping_row["is_active"].as<bool>();
            entry.mappings.push_back(std::move(mapping));
        }
        revision.entries.push_back(std::move(entry));
    }
    return revision;
}

struct EditableTarget {
    bool found{false};
    std::string revision_id;
    std::string entry_id;
    std::string component_id;
};

EditableTarget ensure_editable_target(
    const TransactionPtr& tx,
    const std::string& source_revision_id,
    const std::optional<std::string>& source_entry_id,
    const std::string& user_id) {
    const auto source = tx->execSqlSync(
        "select id::text,bridge_id::text,revision_number,status from "
        "bridge_component_inventory_revisions where id=$1::uuid for update",
        source_revision_id);
    if (source.empty()) return {};

    std::string component_id;
    if (source_entry_id.has_value()) {
        const auto entry = tx->execSqlSync(
            "select bridge_component_id::text from bridge_component_inventory_entries "
            "where id=$1::uuid and inventory_revision_id=$2::uuid",
            *source_entry_id, source_revision_id);
        if (entry.empty()) return {};
        component_id = entry[0]["bridge_component_id"].as<std::string>();
    }

    if (source[0]["status"].as<std::string>() == "草稿") {
        return {true, source_revision_id, source_entry_id.value_or(""), component_id};
    }

    const auto bridge_id = source[0]["bridge_id"].as<std::string>();
    auto draft = tx->execSqlSync(
        "select id::text from bridge_component_inventory_revisions "
        "where bridge_id=$1::uuid and status='草稿' and baseline_revision_id=$2::uuid "
        "order by revision_number desc limit 1 for update",
        bridge_id, source_revision_id);
    std::string draft_id;
    if (draft.empty()) {
        const auto inserted = tx->execSqlSync(
            "insert into bridge_component_inventory_revisions "
            "(bridge_id,revision_number,baseline_revision_id,created_by_user_id) "
            "select $1::uuid,coalesce(max(revision_number),0)+1,nullif($2,'')::uuid,$3::uuid "
            "from bridge_component_inventory_revisions where bridge_id=$1::uuid "
            "returning id::text",
            bridge_id, source_revision_id, user_id);
        draft_id = inserted[0]["id"].as<std::string>();
        tx->execSqlSync(
            "insert into bridge_component_inventory_entries "
            "(inventory_revision_id,bridge_component_id,generation_batch_id,component_number,"
            "site_name,site_component_type,span_or_location,is_active,deactivated_at,"
            "deactivation_reason,sort_order,remarks) "
            "select $1::uuid,bridge_component_id,generation_batch_id,component_number,site_name,"
            "site_component_type,span_or_location,is_active,deactivated_at,deactivation_reason,"
            "sort_order,remarks from bridge_component_inventory_entries "
            "where inventory_revision_id=$2::uuid",
            draft_id, source_revision_id);
        tx->execSqlSync(
            "insert into bridge_component_standard_mappings "
            "(inventory_entry_id,standard_package_id,standard_bridge_type_id,"
            "standard_component_category_id,structure_part,mapping_source,confirmation_status,"
            "confirmed_by_user_id,confirmed_at,is_active) "
            "select ne.id,m.standard_package_id,m.standard_bridge_type_id,"
            "m.standard_component_category_id,m.structure_part,m.mapping_source,"
            "m.confirmation_status,m.confirmed_by_user_id,m.confirmed_at,m.is_active "
            "from bridge_component_standard_mappings m "
            "join bridge_component_inventory_entries oe on oe.id=m.inventory_entry_id "
            "join bridge_component_inventory_entries ne on ne.inventory_revision_id=$1::uuid "
            "and ne.bridge_component_id=oe.bridge_component_id "
            "where oe.inventory_revision_id=$2::uuid",
            draft_id, source_revision_id);
    } else {
        draft_id = draft[0]["id"].as<std::string>();
    }

    std::string draft_entry_id;
    if (!component_id.empty()) {
        const auto target = tx->execSqlSync(
            "select id::text from bridge_component_inventory_entries "
            "where inventory_revision_id=$1::uuid and bridge_component_id=$2::uuid",
            draft_id, component_id);
        if (target.empty()) return {};
        draft_entry_id = target[0]["id"].as<std::string>();
    }
    return {true, draft_id, draft_entry_id, component_id};
}

bool duplicate_number(
    const TransactionPtr& tx,
    const std::string& revision_id,
    const std::string& entry_id,
    const std::string& type,
    const std::string& number) {
    return !tx->execSqlSync(
        "select 1 from bridge_component_inventory_entries where inventory_revision_id=$1::uuid "
        "and site_component_type=$2 and component_number=$3 and id<>$4::uuid limit 1",
        revision_id, type, number, entry_id).empty();
}

bool component_referenced(const TransactionPtr& tx, const std::string& component_id) {
    return tx->execSqlSync(
        "select exists("
        "select 1 from defect_observations where bridge_component_id=$1::uuid "
        "union all select 1 from defect_threads where bridge_component_id=$1::uuid "
        "union all select 1 from condition_ratings where bridge_component_id=$1::uuid "
        "union all select 1 from inspection_years iy join bridge_component_inventory_entries e "
        "on e.inventory_revision_id=iy.component_inventory_revision_id "
        "where e.bridge_component_id=$1::uuid limit 1) as value",
        component_id)[0]["value"].as<bool>();
}

ComponentInventoryOutcome finish(
    TransactionPtr& tx,
    const std::shared_ptr<CommitLatch>& latch,
    const std::string& revision_id,
    const std::optional<std::string>& entry_id = std::nullopt) {
    tx.reset();
    if (!latch->wait()) return {};
    ComponentInventoryOutcome outcome;
    outcome.status = ComponentInventoryStatus::Ok;
    outcome.revision = inventory::InventoryRevision{};
    outcome.revision->id = revision_id;
    outcome.entry_id = entry_id;
    return outcome;
}

}  // namespace

ComponentInventoryRepository::ComponentInventoryRepository(drogon::orm::DbClientPtr db_client)
    : db_client_(std::move(db_client)) {}

std::optional<inventory::InventoryRevision> ComponentInventoryRepository::get_revision(
    const std::string& revision_id) const {
    const auto revision = revision_from_client(db_client_, revision_id);
    if (revision.id.empty()) return std::nullopt;
    return revision;
}

std::optional<inventory::InventoryRevision> ComponentInventoryRepository::get_latest_revision(
    const std::string& bridge_id) const {
    const auto rows = db_client_->execSqlSync(
        "select id::text from bridge_component_inventory_revisions where bridge_id=$1::uuid "
        "order by (status='草稿') desc,revision_number desc limit 1",
        bridge_id);
    if (rows.empty()) return std::nullopt;
    return get_revision(rows[0]["id"].as<std::string>());
}

ComponentInventoryOutcome ComponentInventoryRepository::generate_draft(
    const std::string& bridge_id,
    const std::string& user_id,
    const inventory::GenerateInventoryInput& input,
    const std::vector<inventory::GeneratedInventoryEntry>& generated) {
    if (generated.empty()) return {ComponentInventoryStatus::Invalid};
    TransactionPtr tx;
    const auto latch = std::make_shared<CommitLatch>();
    try {
        tx = db_client_->newTransaction(latch->callback());
        const auto context = tx->execSqlSync(
            "select b.id::text from bridges b join standard_packages p on p.id=$2::uuid "
            "where b.id=$1::uuid and p.standard_family='technical_condition' "
            "and p.is_enabled and p.sync_status='正常' for update of b",
            bridge_id, input.standard_package_id);
        if (context.empty()) {
            tx->rollback();
            return {ComponentInventoryStatus::NotFound};
        }
        if (!tx->execSqlSync(
                "select 1 from bridge_component_inventory_revisions "
                "where bridge_id=$1::uuid and status='草稿' limit 1",
                bridge_id).empty()) {
            tx->rollback();
            return {ComponentInventoryStatus::Conflict};
        }
        const auto batch = tx->execSqlSync(
            "insert into bridge_component_generation_batches "
            "(bridge_id,template_standard_package_id,template_id,bridge_type_code,"
            "input_quantities,generated_by_user_id) "
            "values($1::uuid,$2::uuid,$3,$4,$5::jsonb,$6::uuid) returning id::text",
            bridge_id, input.standard_package_id, input.template_id, input.bridge_type_id,
            compact_json(input.input_quantities), user_id);
        const auto baseline = tx->execSqlSync(
            "select id::text from bridge_component_inventory_revisions "
            "where bridge_id=$1::uuid and status='已确认' order by revision_number desc limit 1",
            bridge_id);
        const auto revision = tx->execSqlSync(
            "insert into bridge_component_inventory_revisions "
            "(bridge_id,revision_number,baseline_revision_id,created_by_user_id) "
            "select $1::uuid,coalesce(max(revision_number),0)+1,nullif($2::text,'')::uuid,$3::uuid "
            "from bridge_component_inventory_revisions where bridge_id=$1::uuid returning id::text",
            bridge_id, baseline.empty() ? std::string() : baseline[0]["id"].as<std::string>(), user_id);
        const auto revision_id = revision[0]["id"].as<std::string>();
        const auto batch_id = batch[0]["id"].as<std::string>();
        for (const auto& item : generated) {
            const auto component = tx->execSqlSync(
                "with identity as(select gen_random_uuid() id) insert into bridge_components "
                "(id,bridge_id,structure_part,component_type,business_component_code,"
                "normalized_component_key,creation_source) "
                "select id,$1::uuid,$2,$3,$4,'inventory:'||id::text,'人工录入' from identity "
                "returning id::text",
                bridge_id, legacy_structure_part(item.structure_part), item.site_component_type,
                item.component_number);
            const auto entry = tx->execSqlSync(
                "insert into bridge_component_inventory_entries "
                "(inventory_revision_id,bridge_component_id,generation_batch_id,component_number,"
                "site_name,site_component_type,span_or_location,sort_order) "
                "values($1::uuid,$2::uuid,$3::uuid,$4,$5,$6,$7,$8) returning id::text",
                revision_id, component[0]["id"].as<std::string>(), batch_id,
                item.component_number, item.site_name, item.site_component_type,
                item.span_or_location.value_or(""), item.sort_order);
            // 向导中的规范类别由用户逐组显式选择，生成即视为该用户确认映射。
            tx->execSqlSync(
                "insert into bridge_component_standard_mappings "
                "(inventory_entry_id,standard_package_id,standard_bridge_type_id,"
                "standard_component_category_id,structure_part,mapping_source,"
                "confirmation_status,confirmed_by_user_id,confirmed_at) "
                "values($1::uuid,$2::uuid,$3,$4,$5,'模板生成','已确认',$6::uuid,now())",
                entry[0]["id"].as<std::string>(), input.standard_package_id,
                input.bridge_type_id, item.standard_component_category_id, item.structure_part,
                user_id);
        }
        auto outcome = finish(tx, latch, revision_id);
        if (outcome.status == ComponentInventoryStatus::Ok) {
            outcome.revision = get_revision(revision_id);
        }
        return outcome;
    } catch (...) {
        if (tx) { try { tx->rollback(); } catch (...) {} }
        return {ComponentInventoryStatus::Failed};
    }
}

ComponentInventoryOutcome ComponentInventoryRepository::update_entry(
    const std::string& revision_id,
    const std::string& entry_id,
    const std::string& user_id,
    const InventoryEntryUpdate& update) {
    if (!valid_entry_update(update)) return {ComponentInventoryStatus::Invalid};
    TransactionPtr tx;
    const auto latch = std::make_shared<CommitLatch>();
    try {
        tx = db_client_->newTransaction(latch->callback());
        const auto target = ensure_editable_target(tx, revision_id, entry_id, user_id);
        if (!target.found) { tx->rollback(); return {ComponentInventoryStatus::NotFound}; }
        if (duplicate_number(tx, target.revision_id, target.entry_id,
                             update.site_component_type, update.component_number)) {
            tx->rollback(); return {ComponentInventoryStatus::Conflict};
        }
        tx->execSqlSync(
            "update bridge_component_inventory_entries set component_number=$1,site_name=$2,"
            "site_component_type=$3,span_or_location=nullif($4,''),remarks=nullif($5,''),"
            "updated_at=now() where id=$6::uuid",
            update.component_number, update.site_name, update.site_component_type,
            update.span_or_location.value_or(""), update.remarks.value_or(""), target.entry_id);
        auto outcome = finish(tx, latch, target.revision_id, target.entry_id);
        if (outcome.status == ComponentInventoryStatus::Ok)
            outcome.revision = get_revision(target.revision_id);
        return outcome;
    } catch (...) {
        if (tx) { try { tx->rollback(); } catch (...) {} }
        return {ComponentInventoryStatus::Failed};
    }
}

ComponentInventoryOutcome ComponentInventoryRepository::add_entry(
    const std::string& revision_id,
    const std::string& user_id,
    const InventoryNewEntry& entry) {
    if (!valid_entry_update(entry) || entry.sort_order < 0)
        return {ComponentInventoryStatus::Invalid};
    TransactionPtr tx;
    const auto latch = std::make_shared<CommitLatch>();
    try {
        tx = db_client_->newTransaction(latch->callback());
        const auto target = ensure_editable_target(tx, revision_id, std::nullopt, user_id);
        if (!target.found) { tx->rollback(); return {ComponentInventoryStatus::NotFound}; }
        if (duplicate_number(tx, target.revision_id,
                             "00000000-0000-0000-0000-000000000000",
                             entry.site_component_type, entry.component_number)) {
            tx->rollback(); return {ComponentInventoryStatus::Conflict};
        }
        const auto context = tx->execSqlSync(
            "select bridge_id::text from bridge_component_inventory_revisions where id=$1::uuid",
            target.revision_id);
        const auto component = tx->execSqlSync(
            "with identity as(select gen_random_uuid() id) insert into bridge_components "
            "(id,bridge_id,structure_part,component_type,business_component_code,"
            "normalized_component_key,creation_source) "
            "select id,$1::uuid,'其他',$2,$3,'inventory-manual:'||id::text,'人工录入' "
            "from identity returning id::text",
            context[0]["bridge_id"].as<std::string>(), entry.site_component_type,
            entry.component_number);
        const auto inserted = tx->execSqlSync(
            "insert into bridge_component_inventory_entries "
            "(inventory_revision_id,bridge_component_id,component_number,site_name,"
            "site_component_type,span_or_location,sort_order,remarks) "
            "values($1::uuid,$2::uuid,$3,$4,$5,nullif($6,''),$7,nullif($8,'')) returning id::text",
            target.revision_id, component[0]["id"].as<std::string>(), entry.component_number,
            entry.site_name, entry.site_component_type, entry.span_or_location.value_or(""),
            entry.sort_order, entry.remarks.value_or(""));
        const auto new_entry_id = inserted[0]["id"].as<std::string>();
        auto outcome = finish(tx, latch, target.revision_id, new_entry_id);
        if (outcome.status == ComponentInventoryStatus::Ok)
            outcome.revision = get_revision(target.revision_id);
        return outcome;
    } catch (...) {
        if (tx) { try { tx->rollback(); } catch (...) {} }
        return {ComponentInventoryStatus::Failed};
    }
}

ComponentInventoryOutcome ComponentInventoryRepository::delete_entry(
    const std::string& revision_id,
    const std::string& entry_id,
    const std::string& user_id) {
    TransactionPtr tx;
    const auto latch = std::make_shared<CommitLatch>();
    try {
        tx = db_client_->newTransaction(latch->callback());
        const auto source = tx->execSqlSync(
            "select bridge_component_id::text from bridge_component_inventory_entries "
            "where id=$1::uuid and inventory_revision_id=$2::uuid",
            entry_id, revision_id);
        if (source.empty()) { tx->rollback(); return {ComponentInventoryStatus::NotFound}; }
        const auto component_id = source[0]["bridge_component_id"].as<std::string>();
        if (component_referenced(tx, component_id)) {
            tx->rollback(); return {ComponentInventoryStatus::Referenced};
        }
        const auto target = ensure_editable_target(tx, revision_id, entry_id, user_id);
        if (!target.found) { tx->rollback(); return {ComponentInventoryStatus::NotFound}; }
        tx->execSqlSync("delete from bridge_component_inventory_entries where id=$1::uuid",
                        target.entry_id);
        const auto remaining = tx->execSqlSync(
            "select count(*)::int as count from bridge_component_inventory_entries "
            "where bridge_component_id=$1::uuid",
            component_id)[0]["count"].as<int>();
        if (remaining == 0) {
            tx->execSqlSync("delete from bridge_components where id=$1::uuid", component_id);
        }
        auto outcome = finish(tx, latch, target.revision_id);
        if (outcome.status == ComponentInventoryStatus::Ok)
            outcome.revision = get_revision(target.revision_id);
        return outcome;
    } catch (...) {
        if (tx) { try { tx->rollback(); } catch (...) {} }
        return {ComponentInventoryStatus::Failed};
    }
}

ComponentInventoryOutcome ComponentInventoryRepository::deactivate_entry(
    const std::string& revision_id,
    const std::string& entry_id,
    const std::string& user_id,
    const std::string& reason) {
    if (reason.empty()) return {ComponentInventoryStatus::Invalid};
    TransactionPtr tx;
    const auto latch = std::make_shared<CommitLatch>();
    try {
        tx = db_client_->newTransaction(latch->callback());
        const auto target = ensure_editable_target(tx, revision_id, entry_id, user_id);
        if (!target.found) { tx->rollback(); return {ComponentInventoryStatus::NotFound}; }
        tx->execSqlSync(
            "update bridge_component_inventory_entries set is_active=false,deactivated_at=now(),"
            "deactivation_reason=$1,updated_at=now() where id=$2::uuid",
            reason, target.entry_id);
        auto outcome = finish(tx, latch, target.revision_id, target.entry_id);
        if (outcome.status == ComponentInventoryStatus::Ok)
            outcome.revision = get_revision(target.revision_id);
        return outcome;
    } catch (...) {
        if (tx) { try { tx->rollback(); } catch (...) {} }
        return {ComponentInventoryStatus::Failed};
    }
}

ComponentInventoryOutcome ComponentInventoryRepository::set_mapping(
    const std::string& revision_id,
    const std::string& entry_id,
    const std::string& user_id,
    const InventoryMappingUpdate& mapping) {
    if (mapping.standard_package_id.empty() || mapping.standard_bridge_type_id.empty() ||
        mapping.standard_component_category_id.empty() ||
        !valid_structure_part(mapping.structure_part))
        return {ComponentInventoryStatus::Invalid};
    TransactionPtr tx;
    const auto latch = std::make_shared<CommitLatch>();
    try {
        tx = db_client_->newTransaction(latch->callback());
        const auto target = ensure_editable_target(tx, revision_id, entry_id, user_id);
        if (!target.found) { tx->rollback(); return {ComponentInventoryStatus::NotFound}; }
        const auto package = tx->execSqlSync(
            "select 1 from standard_packages where id=$1::uuid "
            "and standard_family='technical_condition' and is_enabled and sync_status='正常'",
            mapping.standard_package_id);
        if (package.empty()) { tx->rollback(); return {ComponentInventoryStatus::Invalid}; }
        tx->execSqlSync(
            "update bridge_component_standard_mappings set is_active=false,updated_at=now() "
            "where inventory_entry_id=$1::uuid and standard_package_id=$2::uuid and is_active",
            target.entry_id, mapping.standard_package_id);
        const std::string mapping_source = mapping.mapping_source.empty()
            ? "人工选择" : mapping.mapping_source;
        tx->execSqlSync(
            "insert into bridge_component_standard_mappings "
            "(inventory_entry_id,standard_package_id,standard_bridge_type_id,"
            "standard_component_category_id,structure_part,mapping_source,confirmation_status,"
            "confirmed_by_user_id,confirmed_at) "
            "values($1::uuid,$2::uuid,$3,$4,$5,$6,'已确认',$7::uuid,now())",
            target.entry_id, mapping.standard_package_id, mapping.standard_bridge_type_id,
            mapping.standard_component_category_id, mapping.structure_part,
            mapping_source, user_id);
        auto outcome = finish(tx, latch, target.revision_id, target.entry_id);
        if (outcome.status == ComponentInventoryStatus::Ok)
            outcome.revision = get_revision(target.revision_id);
        return outcome;
    } catch (...) {
        if (tx) { try { tx->rollback(); } catch (...) {} }
        return {ComponentInventoryStatus::Failed};
    }
}

ComponentInventoryOutcome ComponentInventoryRepository::confirm_pending_mappings(
    const std::string& revision_id,
    const std::string& user_id,
    const std::string& site_component_type) {
    TransactionPtr tx;
    const auto latch = std::make_shared<CommitLatch>();
    try {
        tx = db_client_->newTransaction(latch->callback());
        const auto revision = tx->execSqlSync(
            "select status from bridge_component_inventory_revisions "
            "where id=$1::uuid for update",
            revision_id);
        if (revision.empty()) { tx->rollback(); return {ComponentInventoryStatus::NotFound}; }
        if (revision[0]["status"].as<std::string>() != "草稿") {
            tx->rollback();
            return {ComponentInventoryStatus::Conflict};
        }
        tx->execSqlSync(
            "update bridge_component_standard_mappings m "
            "set confirmation_status='已确认',confirmed_by_user_id=$2::uuid,"
            "confirmed_at=now(),updated_at=now() "
            "from bridge_component_inventory_entries e "
            "where m.inventory_entry_id=e.id and e.inventory_revision_id=$1::uuid "
            "and e.is_active and m.is_active and m.confirmation_status='待确认' "
            "and ($3='' or e.site_component_type=$3)",
            revision_id, user_id, site_component_type);
        auto outcome = finish(tx, latch, revision_id);
        if (outcome.status == ComponentInventoryStatus::Ok)
            outcome.revision = get_revision(revision_id);
        return outcome;
    } catch (...) {
        if (tx) { try { tx->rollback(); } catch (...) {} }
        return {ComponentInventoryStatus::Failed};
    }
}

ComponentInventoryOutcome ComponentInventoryRepository::confirm_revision(
    const std::string& revision_id,
    const std::string& user_id,
    const std::string& note) {
    TransactionPtr tx;
    const auto latch = std::make_shared<CommitLatch>();
    try {
        tx = db_client_->newTransaction(latch->callback());
        const auto revision = tx->execSqlSync(
            "select id::text,status from bridge_component_inventory_revisions "
            "where id=$1::uuid for update",
            revision_id);
        if (revision.empty()) { tx->rollback(); return {ComponentInventoryStatus::NotFound}; }
        if (revision[0]["status"].as<std::string>() == "已确认") {
            tx->rollback(); return {ComponentInventoryStatus::Conflict};
        }
        std::vector<inventory::InventoryBlocker> blockers;
        const auto entries = tx->execSqlSync(
            "select e.id::text,e.component_number,count(m.id)::int as confirmed_mapping_count "
            "from bridge_component_inventory_entries e left join bridge_component_standard_mappings m "
            "on m.inventory_entry_id=e.id and m.is_active and m.confirmation_status='已确认' "
            "where e.inventory_revision_id=$1::uuid and e.is_active "
            "group by e.id,e.component_number order by e.sort_order,e.id",
            revision_id);
        if (entries.empty()) {
            blockers.push_back({"inventory_empty", "inventory_revision", revision_id,
                                "entries", "构件台账至少需要一个启用构件。"});
        }
        for (const auto& row : entries) {
            if (row["confirmed_mapping_count"].as<int>() < 1) {
                blockers.push_back({
                    "component_mapping_required", "inventory_entry",
                    row["id"].as<std::string>(), "mappings",
                    "构件 " + row["component_number"].as<std::string>() +
                        " 至少需要一个已确认的有效规范映射。"});
            }
        }
        if (!blockers.empty()) {
            tx->rollback();
            ComponentInventoryOutcome outcome;
            outcome.status = ComponentInventoryStatus::Blocked;
            outcome.blockers = std::move(blockers);
            return outcome;
        }
        tx->execSqlSync(
            "update bridge_component_inventory_revisions set status='已确认',"
            "confirmed_by_user_id=$1::uuid,confirmed_at=now(),confirmation_note=nullif($2,''),"
            "updated_at=now() where id=$3::uuid",
            user_id, note, revision_id);
        auto outcome = finish(tx, latch, revision_id);
        if (outcome.status == ComponentInventoryStatus::Ok)
            outcome.revision = get_revision(revision_id);
        return outcome;
    } catch (...) {
        if (tx) { try { tx->rollback(); } catch (...) {} }
        return {ComponentInventoryStatus::Failed};
    }
}

}  // namespace bridge_report::db
