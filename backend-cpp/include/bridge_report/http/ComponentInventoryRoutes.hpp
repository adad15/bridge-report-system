#pragma once

#include <memory>
#include <string>

#include <drogon/orm/DbClient.h>
#include <json/json.h>

#include "bridge_report/db/ComponentInventoryRepository.hpp"
#include "bridge_report/standards/StandardModels.hpp"
#include "bridge_report/standards/StandardRegistry.hpp"

namespace bridge_report::http {

bool validate_inventory_generation_standard(
    const inventory::GenerateInventoryInput& input,
    const standards::StandardPackage& package,
    std::string& error_code,
    std::string& error_message);

// 按规范包 taxonomy 对某桥型 generatable 的类别，返回该桥型可用的部件目录（JSON 数组）。
Json::Value serialize_part_catalog(
    const standards::StandardPackage& package,
    const std::string& bridge_type_id);
bool parse_inventory_entry_update(
    const Json::Value& body,
    db::InventoryEntryUpdate& output,
    std::string& error_message);
bool parse_inventory_mapping_update(
    const Json::Value& body,
    db::InventoryMappingUpdate& output,
    std::string& error_message);

void register_component_inventory_routes(
    const drogon::orm::DbClientPtr& db_client,
    std::shared_ptr<const standards::StandardRegistry> registry);

}  // namespace bridge_report::http
