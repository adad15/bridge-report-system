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
// 去掉首尾空白。纯空白的 group / number 必须与缺失同等对待——放过去会让
// like '%%' 一次命中全表。
std::string trimmed_query_value(const std::string& value);

// 分页参数：空值取默认；非整数或越下界返回 false（路由据此回 400）；
// 越上界按上限截断。
bool parse_bounded_query_int(
    const std::string& raw_value,
    const std::string& name,
    std::int64_t fallback,
    std::int64_t minimum,
    std::int64_t maximum,
    std::int64_t& output,
    std::string& message);

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
