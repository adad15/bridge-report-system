// Offline audit: reuse production matching functions without database writes.
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <json/json.h>
#include "bridge_report/inventory/ComponentMatcher.hpp"
#include "bridge_report/inventory/ComponentCategoryLexicon.hpp"
#include "bridge_report/inventory/ComponentRangeParser.hpp"
#include "bridge_report/inventory/SideComponentPair.hpp"

namespace inv = bridge_report::inventory;

Json::Value read_json(const char* path) {
    std::ifstream input(path);
    Json::CharReaderBuilder reader;
    Json::Value value;
    std::string error;
    if (!input || !Json::parseFromStream(reader, input, &value, &error))
        throw std::runtime_error("Cannot read JSON: " + std::string(path) + " " + error);
    return value;
}

Json::Value match_json(const inv::ComponentMatchResult& match) {
    Json::Value out(Json::objectValue);
    out["method"] = inv::component_match_method_name(match.method);
    out["candidate_ids"] = Json::Value(Json::arrayValue);
    for (const auto& id : match.candidate_component_ids) out["candidate_ids"].append(id);
    out["matched"] = match.matched_entry.has_value();
    if (match.matched_entry) {
        out["component_id"] = match.matched_entry->bridge_component_id;
        out["number"] = match.matched_entry->component_number;
        out["category"] = match.matched_mapping->standard_component_category_id;
        out["mapping_confirmed"] = match.matched_mapping->confirmation_status == "已确认";
    }
    return out;
}

int main(int argc, char** argv) {
    try {
        if (argc != 4) throw std::runtime_error("Usage: probe inventory.json profile.json result.json");
        const auto snapshot = read_json(argv[1]);
        const auto profile = read_json(argv[2]);
        inv::InventoryRevision revision;
        revision.id = snapshot["revision"]["id"].asString();
        revision.bridge_id = snapshot["revision"]["bridge_id"].asString();
        revision.status = snapshot["revision"]["status"].asString();
        if (revision.status != "已确认" && revision.status != "confirmed")
            throw std::runtime_error("Expected a confirmed revision.");
        if (snapshot["read_only"].asString() != "on")
            throw std::runtime_error("Expected a read-only inventory export.");
        for (const auto& value : snapshot["entries"]) {
            inv::InventoryEntry entry;
            entry.id = value["id"].asString();
            entry.bridge_component_id = value["bridge_component_id"].asString();
            entry.component_number = value["component_number"].asString();
            entry.site_component_type = value["site_component_type"].asString();
            entry.is_active = value["is_active"].asBool();
            for (const auto& mapping : value["mappings"]) {
                inv::InventoryMapping item;
                item.standard_component_category_id = mapping["standard_component_category_id"].asString();
                item.structure_part = mapping["structure_part"].asString();
                item.confirmation_status = mapping["confirmation_status"].asString();
                item.is_active = mapping["is_active"].asBool();
                entry.mappings.push_back(item);
            }
            revision.entries.push_back(entry);
        }
        std::vector<inv::ConfirmedComponentAlias> aliases;
        for (const auto& alias : snapshot["aliases"])
            aliases.push_back({alias["bridge_component_id"].asString(), alias["alias_text"].asString()});
        Json::Value result(Json::objectValue);
        result["inventory_revision_id"] = revision.id;
        result["inventory_entry_count"] = static_cast<Json::UInt64>(revision.entries.size());
        result["business_writes_performed"] = false;
        result["years"] = Json::Value(Json::arrayValue);
        for (const auto& source_year : profile["years"]) {
            bool pinned_to_revision = false;
            for (const auto& annual : snapshot["years"]) {
                if (annual["inspection_year"] == source_year["year"]
                    && annual["component_inventory_revision_id"].asString() == revision.id)
                    pinned_to_revision = true;
            }
            if (!pinned_to_revision)
                throw std::runtime_error("Source year is not pinned to the exported revision.");
            Json::Value year(Json::objectValue);
            year["year"] = source_year["year"];
            year["counts"] = Json::Value(Json::objectValue);
            year["rows"] = Json::Value(Json::arrayValue);
            for (const auto& source : source_year["binding_prerequisites"]) {
                const auto number = source["number"].asString();
                const auto category = source["category"].asString();
                Json::Value row = source;
                row["direct"] = match_json(inv::match_defect_component({number, category}, revision, aliases));
                row["targets"] = Json::Value(Json::arrayValue);
                std::string status = "unmatched";
                if (row["direct"]["matched"].asBool()) {
                    status = "unique_direct_match";
                    row["targets"].append(row["direct"]);
                } else {
                    const auto range = inv::parse_component_range(number);
                    if (range.status == inv::ComponentRangeParseStatus::Ok) {
                        bool complete = true;
                        std::set<std::string> targets;
                        Json::Value actual_numbers(Json::arrayValue);
                        for (const auto& item : range.numbers) {
                            actual_numbers.append(item);
                            auto match = match_json(inv::match_defect_component({item, category}, revision, aliases));
                            complete = complete && match["matched"].asBool();
                            if (match["matched"].asBool()) targets.insert(match["component_id"].asString());
                            match["source_number"] = item;
                            row["targets"].append(match);
                        }
                        if (source["expected_range_numbers"] != actual_numbers)
                            throw std::runtime_error("Independent range expectation differs: " + number);
                        complete = complete && targets.size() == range.numbers.size();
                        status = complete ? "range_all_targets_unique_requires_apply" : "range_missing_or_ambiguous";
                    } else if (number.find("两侧") != std::string::npos) {
                        for (const auto& id : inv::resolve_component_categories(category)) {
                            const auto pair = inv::find_side_component_pair(revision, id);
                            if (!pair) continue;
                            Json::Value option(Json::objectValue);
                            option["left_component_id"] = pair->left_bridge_component_id;
                            option["right_component_id"] = pair->right_bridge_component_id;
                            option["left_number"] = pair->left_component_number;
                            option["right_number"] = pair->right_component_number;
                            row["targets"].append(option);
                        }
                        if (row["targets"].size() == 1) status = "side_pair_available_requires_choice";
                    } else if (!row["direct"]["candidate_ids"].empty()) {
                        status = "ambiguous";
                    }
                }
                if (source["suggested_number_not_applied"].isString()) {
                    row["suggestion_check"] = match_json(inv::match_defect_component(
                        {source["suggested_number_not_applied"].asString(), category}, revision, aliases));
                }
                row["status"] = status;
                year["counts"][status] = year["counts"].get(status, 0).asInt() + 1;
                year["rows"].append(row);
            }
            result["years"].append(year);
        }
        std::ofstream output(argv[3]);
        Json::StreamWriterBuilder writer;
        writer["emitUTF8"] = true;
        output << Json::writeString(writer, result) << "\n";
        if (!output) throw std::runtime_error("Cannot write probe result.");
        for (const auto& year : result["years"])
            std::cout << year["year"] << " " << year["counts"] << "\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
