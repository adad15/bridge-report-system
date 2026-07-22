#include "bridge_report/http/ComponentInventoryRoutes.hpp"

#include <initializer_list>
#include <map>
#include <optional>
#include <set>
#include <utility>

#include "bridge_report/db/StandardRepository.hpp"
#include "bridge_report/http/AuthRoutes.hpp"
#include "bridge_report/http/RouteHelpers.hpp"
#include "bridge_report/inventory/ComponentPartCatalog.hpp"
#include "bridge_report/inventory/ComponentInventoryGenerator.hpp"
#include "bridge_report/inventory/NumberingTemplate.hpp"

namespace bridge_report::http {
namespace {

std::optional<db::AuthUser> require_user(
    const drogon::orm::DbClientPtr& db_client,
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback) {
    auto user = authenticate_request(db_client, request);
    if (!user.has_value()) respond_unauthorized(callback);
    return user;
}

bool definition_supports_bridge_type(
    const standards::StandardDefinition& definition,
    const std::string& bridge_type_id) {
    const auto& types = definition.payload["bridge_type_ids"];
    if (!types.isArray()) return false;
    for (const auto& type : types) {
        if (type.isString() && type.asString() == bridge_type_id) return true;
    }
    return false;
}

void respond_inventory_outcome(
    const HttpCallback& callback,
    const db::ComponentInventoryOutcome& outcome,
    const drogon::HttpStatusCode success_status = drogon::k200OK) {
    if (outcome.status == db::ComponentInventoryStatus::Ok && outcome.revision.has_value()) {
        Json::Value body;
        body["revision"] = inventory::inventory_revision_json(*outcome.revision);
        if (outcome.entry_id.has_value()) body["entry_id"] = *outcome.entry_id;
        respond_json(callback, body, success_status);
        return;
    }
    if (outcome.status == db::ComponentInventoryStatus::NotFound) {
        respond_json(callback, make_error_body("component_inventory_not_found", "构件台账或条目不存在。"),
                     drogon::k404NotFound);
        return;
    }
    if (outcome.status == db::ComponentInventoryStatus::Invalid) {
        respond_json(callback, make_error_body("invalid_component_inventory", "构件台账输入无效。"),
                     drogon::k400BadRequest);
        return;
    }
    if (outcome.status == db::ComponentInventoryStatus::Conflict) {
        respond_json(callback, make_error_body("component_inventory_conflict", "构件台账已变化或编号重复。"),
                     drogon::k409Conflict);
        return;
    }
    if (outcome.status == db::ComponentInventoryStatus::Referenced) {
        respond_json(callback, make_error_body(
            "component_is_referenced", "该实际构件已被病害或正式项目引用，只能停用。"),
            drogon::k409Conflict);
        return;
    }
    if (outcome.status == db::ComponentInventoryStatus::Blocked) {
        Json::Value body = make_error_body("component_inventory_confirmation_blocked", "构件台账尚不能确认。");
        body["blockers"] = inventory::inventory_blockers_json(outcome.blockers);
        respond_json(callback, body, drogon::k409Conflict);
        return;
    }
    respond_db_unavailable(callback);
}

const standards::StandardPackage* load_request_package(
    const drogon::orm::DbClientPtr& db_client,
    const std::shared_ptr<const standards::StandardRegistry>& registry,
    const std::string& package_id) {
    db::StandardRepository repository(db_client);
    const auto record = repository.find_package_by_id(package_id);
    if (!record.has_value() || record->family != standards::StandardFamily::technical_condition ||
        !record->is_enabled || record->sync_status != "正常") return nullptr;
    const standards::StandardPackageKey key{
        record->family, record->standard_id, record->package_version};
    const auto* package = registry->find(key);
    if (package == nullptr || package->manifest.content_checksum != record->content_checksum)
        return nullptr;
    return package;
}

bool valid_path_ids(std::initializer_list<std::string> values) {
    for (const auto& value : values) if (!is_valid_uuid(value)) return false;
    return true;
}

// 逐维乘算展开规模，任一维只物化自身取值（≤1万），不构建全笛卡尔积。
long long expected_generation_size(
    const inventory::NumberingTemplate& tpl, const inventory::NumberingContext& ctx) {
    long long size = 1;
    for (const auto& slot : tpl.slots) {
        size *= static_cast<long long>(
            inventory::placeholder_values(ctx, slot.placeholder, slot.count).size());
        if (size > 50000) break;
    }
    return size;
}

// 目录路径校验：每个选中部件在梁式桥目录中、其规范类别在包 taxonomy 且适用桥型且可生成、
// 数量维数量与目录一致、生成总规模不超 50000。
bool validate_part_selection_standard(
    const inventory::GenerateInventoryInput& input,
    const standards::StandardPackage& package,
    std::string& error_code,
    std::string& error_message) {
    if (input.part_selections.empty()) {
        error_code = "inventory_part_selections_required";
        error_message = "至少需要选择一个构件生成部件。";
        return false;
    }
    const auto& parts = inventory::component_parts();
    long long total = 0;
    for (const auto& selection : input.part_selections) {
        const auto* part = inventory::find_part(parts, selection.part_key);
        if (part == nullptr) {
            error_code = "unknown_part_key";
            error_message = "构件部件不在梁式桥目录中：" + selection.part_key;
            return false;
        }
        if (selection.counts.size() != part->count_inputs.size()) {
            error_code = "inventory_part_count_mismatch";
            error_message = "构件数量维的个数与目录定义不一致：" + selection.part_key;
            return false;
        }
        const auto category = package.definitions.find(part->standard_component_category_id);
        if (category == package.definitions.end() ||
            category->second.source_file != "component-taxonomy.json" ||
            !definition_supports_bridge_type(category->second, input.bridge_type_id) ||
            category->second.payload["structure_part"].asString() != part->structure_part ||
            !category->second.payload.get("generatable", false).asBool()) {
            error_code = "inventory_component_category_not_supported";
            error_message = "构件类别不属于所选规范和桥型，不能静默归入其他类别。";
            return false;
        }
        const std::string name =
            selection.site_name.empty() ? part->default_name : selection.site_name;
        total += expected_generation_size(part->number_template_with(name, selection.counts),
                                          inventory::NumberingContext{input.span_count});
        if (total > 50000) {
            error_code = "inventory_generation_too_large";
            error_message = "单次生成的构件数量不能超过 50000。";
            return false;
        }
    }
    return true;
}

}  // namespace

Json::Value serialize_part_catalog(
    const standards::StandardPackage& package, const std::string& bridge_type_id) {
    Json::Value parts(Json::arrayValue);
    for (const auto& part : inventory::component_parts()) {
        const auto category = package.definitions.find(part.standard_component_category_id);
        if (category == package.definitions.end() ||
            category->second.source_file != "component-taxonomy.json" ||
            !definition_supports_bridge_type(category->second, bridge_type_id) ||
            category->second.payload["structure_part"].asString() != part.structure_part ||
            !category->second.payload.get("generatable", false).asBool())
            continue;
        Json::Value item;
        item["part_key"] = part.part_key;
        item["default_name"] = part.default_name;
        item["structure_part"] = part.structure_part;
        item["standard_component_category_id"] = part.standard_component_category_id;
        item["standard_component_category_name"] =
            category->second.payload["name"].isString()
                ? category->second.payload["name"].asString() : part.standard_component_category_id;
        item["number_template"] = part.number_template;
        item["provisional"] = part.provisional;
        item["count_inputs"] = Json::Value(Json::arrayValue);
        for (const auto& count_input : part.count_inputs) {
            Json::Value entry;
            entry["key"] = count_input.key;
            entry["label"] = count_input.label;
            item["count_inputs"].append(std::move(entry));
        }
        parts.append(std::move(item));
    }
    return parts;
}

bool validate_inventory_generation_standard(
    const inventory::GenerateInventoryInput& input,
    const standards::StandardPackage& package,
    std::string& error_code,
    std::string& error_message) {
    return validate_part_selection_standard(input, package, error_code, error_message);
}

bool parse_inventory_entry_update(
    const Json::Value& body,
    db::InventoryEntryUpdate& output,
    std::string& error_message) {
    if (!body.isObject() || !body["component_number"].isString() ||
        !body["site_name"].isString() || !body["site_component_type"].isString()) {
        error_message = "构件编号、现场名称和现场类型必须是文本。";
        return false;
    }
    output.component_number = body["component_number"].asString();
    output.site_name = body["site_name"].asString();
    output.site_component_type = body["site_component_type"].asString();
    if (output.component_number.empty() || output.site_name.empty() ||
        output.site_component_type.empty()) {
        error_message = "构件编号、现场名称和现场类型不能为空。";
        return false;
    }
    if (body.isMember("span_or_location") && !body["span_or_location"].isNull()) {
        if (!body["span_or_location"].isString()) {
            error_message = "所属跨或位置必须是文本。";
            return false;
        }
        output.span_or_location = body["span_or_location"].asString();
    }
    if (body.isMember("remarks") && !body["remarks"].isNull()) {
        if (!body["remarks"].isString()) {
            error_message = "备注必须是文本。";
            return false;
        }
        output.remarks = body["remarks"].asString();
    }
    return true;
}

bool parse_inventory_mapping_update(
    const Json::Value& body,
    db::InventoryMappingUpdate& output,
    std::string& error_message) {
    if (!body.isObject() || !body["standard_package_id"].isString() ||
        !body["standard_bridge_type_id"].isString() ||
        !body["standard_component_category_id"].isString() ||
        !body["structure_part"].isString()) {
        error_message = "规范映射字段不完整。";
        return false;
    }
    output.standard_package_id = body["standard_package_id"].asString();
    output.standard_bridge_type_id = body["standard_bridge_type_id"].asString();
    output.standard_component_category_id = body["standard_component_category_id"].asString();
    output.structure_part = body["structure_part"].asString();
    if (body.isMember("mapping_source") && body["mapping_source"].isString())
        output.mapping_source = body["mapping_source"].asString();
    return !output.standard_package_id.empty() && !output.standard_bridge_type_id.empty() &&
        !output.standard_component_category_id.empty() && !output.structure_part.empty();
}

void register_component_inventory_routes(
    const drogon::orm::DbClientPtr& db_client,
    std::shared_ptr<const standards::StandardRegistry> registry) {
    const std::string generate_path = "/api/bridges/{bridge_id}/component-inventories/generate";
    const std::string part_catalog_path = "/api/component-inventories/part-catalog";
    const std::string latest_path = "/api/bridges/{bridge_id}/component-inventories/latest";
    const std::string revision_path = "/api/component-inventories/{revision_id}";
    const std::string entries_path = "/api/component-inventories/{revision_id}/entries";
    const std::string entry_path = "/api/component-inventories/{revision_id}/entries/{entry_id}";
    const std::string deactivate_path = entry_path + "/deactivate";
    const std::string mapping_path = entry_path + "/mapping";
    const std::string confirm_path = revision_path + "/confirm";
    const std::string confirm_mappings_path = revision_path + "/mappings/confirm-pending";
    for (const auto& path : {generate_path, part_catalog_path, latest_path, revision_path,
                             entries_path, entry_path, deactivate_path, mapping_path, confirm_path,
                             confirm_mappings_path})
        register_options_handler(path);

    drogon::app().registerHandler(
        generate_path,
        [db_client, registry](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                              const std::string& bridge_id) {
            if (!is_valid_uuid(bridge_id)) {
                respond_json(callback, make_error_body("bridge_not_found", "桥梁不存在。"),
                             drogon::k404NotFound); return;
            }
            try {
                const auto user = require_user(db_client, request, callback);
                if (!user.has_value()) return;
                const auto body = request->getJsonObject();
                inventory::GenerateInventoryInput input;
                std::string code, message;
                if (body == nullptr || !inventory::parse_generate_inventory_input(
                        *body, input, code, message)) {
                    respond_json(callback, make_error_body(code.empty() ? "invalid_inventory_generation" : code,
                                                           message.empty() ? "构件生成输入无效。" : message),
                                 drogon::k400BadRequest); return;
                }
                if (!is_valid_uuid(input.standard_package_id)) {
                    respond_json(callback, make_error_body(
                        "invalid_standard_package_id", "规范包 ID 无效。"),
                        drogon::k400BadRequest); return;
                }
                const auto* package = load_request_package(
                    db_client, registry, input.standard_package_id);
                if (package == nullptr) {
                    respond_json(callback, make_error_body(
                        "standard_package_unavailable", "所选技术评定规范包不可用。"),
                        drogon::k409Conflict); return;
                }
                if (!validate_inventory_generation_standard(input, *package, code, message)) {
                    respond_json(callback, make_error_body(code, message), drogon::k400BadRequest); return;
                }
                const auto generated = inventory::generate_component_inventory(input);
                if (!generated.ok()) {
                    respond_json(callback, make_error_body(generated.error_code, generated.error_message),
                                 drogon::k400BadRequest); return;
                }
                db::ComponentInventoryRepository repository(db_client);
                respond_inventory_outcome(
                    callback, repository.generate_draft(
                        bridge_id, user->id, input, generated.entries), drogon::k201Created);
            } catch (...) { respond_db_unavailable(callback); }
        }, {drogon::Post});

    drogon::app().registerHandler(
        part_catalog_path,
        [db_client, registry](const drogon::HttpRequestPtr& request, HttpCallback&& callback) {
            try {
                if (!require_user(db_client, request, callback).has_value()) return;
                const std::string package_id = request->getParameter("standard_package_id");
                const std::string bridge_type_id = request->getParameter("bridge_type_id");
                if (!is_valid_uuid(package_id)) {
                    respond_json(callback, make_error_body(
                        "invalid_standard_package_id", "规范包 ID 无效。"),
                        drogon::k400BadRequest); return;
                }
                if (bridge_type_id.empty()) {
                    respond_json(callback, make_error_body(
                        "invalid_bridge_type_id", "桥型 ID 不能为空。"),
                        drogon::k400BadRequest); return;
                }
                const auto* package = load_request_package(db_client, registry, package_id);
                if (package == nullptr) {
                    respond_json(callback, make_error_body(
                        "standard_package_unavailable", "所选技术评定规范包不可用。"),
                        drogon::k409Conflict); return;
                }
                Json::Value body;
                body["parts"] = serialize_part_catalog(*package, bridge_type_id);
                respond_json(callback, body);
            } catch (...) { respond_db_unavailable(callback); }
        }, {drogon::Get});

    drogon::app().registerHandler(
        latest_path,
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& bridge_id) {
            if (!is_valid_uuid(bridge_id)) {
                respond_json(callback, make_error_body("component_inventory_not_found", "构件台账不存在。"),
                             drogon::k404NotFound); return;
            }
            try {
                if (!require_user(db_client, request, callback).has_value()) return;
                db::ComponentInventoryRepository repository(db_client);
                const auto revision = repository.get_latest_revision(bridge_id);
                if (!revision.has_value()) {
                    respond_json(callback, make_error_body("component_inventory_not_found", "构件台账不存在。"),
                                 drogon::k404NotFound); return;
                }
                Json::Value body; body["revision"] = inventory::inventory_revision_json(*revision);
                respond_json(callback, body);
            } catch (...) { respond_db_unavailable(callback); }
        }, {drogon::Get});

    drogon::app().registerHandler(
        revision_path,
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& revision_id) {
            if (!is_valid_uuid(revision_id)) {
                respond_json(callback, make_error_body("component_inventory_not_found", "构件台账不存在。"),
                             drogon::k404NotFound); return;
            }
            try {
                if (!require_user(db_client, request, callback).has_value()) return;
                db::ComponentInventoryRepository repository(db_client);
                const auto revision = repository.get_revision(revision_id);
                if (!revision.has_value()) {
                    respond_json(callback, make_error_body("component_inventory_not_found", "构件台账不存在。"),
                                 drogon::k404NotFound); return;
                }
                Json::Value body; body["revision"] = inventory::inventory_revision_json(*revision);
                respond_json(callback, body);
            } catch (...) { respond_db_unavailable(callback); }
        }, {drogon::Get});

    drogon::app().registerHandler(
        entry_path,
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& revision_id, const std::string& entry_id) {
            if (!valid_path_ids({revision_id, entry_id})) {
                respond_json(callback, make_error_body("component_inventory_not_found", "构件台账或条目不存在。"),
                             drogon::k404NotFound); return;
            }
            try {
                const auto user = require_user(db_client, request, callback);
                if (!user.has_value()) return;
                db::ComponentInventoryRepository repository(db_client);
                if (request->method() == drogon::Delete) {
                    respond_inventory_outcome(callback, repository.delete_entry(
                        revision_id, entry_id, user->id)); return;
                }
                const auto body = request->getJsonObject();
                db::InventoryEntryUpdate update; std::string message;
                if (body == nullptr || !parse_inventory_entry_update(*body, update, message)) {
                    respond_json(callback, make_error_body("invalid_inventory_entry", message),
                                 drogon::k400BadRequest); return;
                }
                respond_inventory_outcome(callback, repository.update_entry(
                    revision_id, entry_id, user->id, update));
            } catch (...) { respond_db_unavailable(callback); }
        }, {drogon::Patch, drogon::Delete});

    drogon::app().registerHandler(
        entries_path,
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& revision_id) {
            if (!is_valid_uuid(revision_id)) {
                respond_json(callback, make_error_body("component_inventory_not_found", "构件台账不存在。"),
                             drogon::k404NotFound); return;
            }
            try {
                const auto user = require_user(db_client, request, callback);
                if (!user.has_value()) return;
                const auto body = request->getJsonObject();
                db::InventoryEntryUpdate parsed; std::string message;
                if (body == nullptr || !parse_inventory_entry_update(*body, parsed, message)) {
                    respond_json(callback, make_error_body("invalid_inventory_entry", message),
                                 drogon::k400BadRequest); return;
                }
                db::InventoryNewEntry entry;
                static_cast<db::InventoryEntryUpdate&>(entry) = parsed;
                if (body->isMember("sort_order") && (*body)["sort_order"].isInt())
                    entry.sort_order = (*body)["sort_order"].asInt();
                db::ComponentInventoryRepository repository(db_client);
                respond_inventory_outcome(callback, repository.add_entry(
                    revision_id, user->id, entry), drogon::k201Created);
            } catch (...) { respond_db_unavailable(callback); }
        }, {drogon::Post});

    drogon::app().registerHandler(
        deactivate_path,
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& revision_id, const std::string& entry_id) {
            if (!valid_path_ids({revision_id, entry_id})) {
                respond_json(callback, make_error_body("component_inventory_not_found", "构件台账或条目不存在。"),
                             drogon::k404NotFound); return;
            }
            try {
                const auto user = require_user(db_client, request, callback);
                if (!user.has_value()) return;
                const auto body = request->getJsonObject();
                if (body == nullptr || !(*body)["reason"].isString() ||
                    (*body)["reason"].asString().empty()) {
                    respond_json(callback, make_error_body("deactivation_reason_required", "停用原因不能为空。"),
                                 drogon::k400BadRequest); return;
                }
                db::ComponentInventoryRepository repository(db_client);
                respond_inventory_outcome(callback, repository.deactivate_entry(
                    revision_id, entry_id, user->id, (*body)["reason"].asString()));
            } catch (...) { respond_db_unavailable(callback); }
        }, {drogon::Post});

    drogon::app().registerHandler(
        mapping_path,
        [db_client, registry](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                              const std::string& revision_id, const std::string& entry_id) {
            if (!valid_path_ids({revision_id, entry_id})) {
                respond_json(callback, make_error_body("component_inventory_not_found", "构件台账或条目不存在。"),
                             drogon::k404NotFound); return;
            }
            try {
                const auto user = require_user(db_client, request, callback);
                if (!user.has_value()) return;
                const auto body = request->getJsonObject();
                db::InventoryMappingUpdate mapping; std::string message;
                if (body == nullptr || !parse_inventory_mapping_update(*body, mapping, message)) {
                    respond_json(callback, make_error_body("invalid_component_mapping", message),
                                 drogon::k400BadRequest); return;
                }
                if (!is_valid_uuid(mapping.standard_package_id)) {
                    respond_json(callback, make_error_body(
                        "invalid_standard_package_id", "规范包 ID 无效。"),
                        drogon::k400BadRequest); return;
                }
                const auto* package = load_request_package(db_client, registry, mapping.standard_package_id);
                if (package == nullptr) {
                    respond_json(callback, make_error_body(
                        "standard_package_unavailable", "所选技术评定规范包不可用。"),
                        drogon::k409Conflict); return;
                }
                const auto category = package->definitions.find(
                    mapping.standard_component_category_id);
                if (category == package->definitions.end() ||
                    category->second.source_file != "component-taxonomy.json" ||
                    !definition_supports_bridge_type(category->second, mapping.standard_bridge_type_id) ||
                    category->second.payload["structure_part"].asString() != mapping.structure_part) {
                    respond_json(callback, make_error_body(
                        "component_mapping_not_supported", "所选规范类别不适用于该桥型。"),
                        drogon::k400BadRequest); return;
                }
                db::ComponentInventoryRepository repository(db_client);
                respond_inventory_outcome(callback, repository.set_mapping(
                    revision_id, entry_id, user->id, mapping));
            } catch (...) { respond_db_unavailable(callback); }
        }, {drogon::Put});

    drogon::app().registerHandler(
        confirm_mappings_path,
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& revision_id) {
            if (!is_valid_uuid(revision_id)) {
                respond_json(callback, make_error_body("component_inventory_not_found", "构件台账不存在。"),
                             drogon::k404NotFound); return;
            }
            try {
                const auto user = require_user(db_client, request, callback);
                if (!user.has_value()) return;
                const auto body = request->getJsonObject();
                std::string site_component_type;
                if (body != nullptr && body->isMember("site_component_type")) {
                    if (!(*body)["site_component_type"].isString()) {
                        respond_json(callback, make_error_body(
                            "invalid_component_mapping", "构件类别筛选必须是文本。"),
                            drogon::k400BadRequest); return;
                    }
                    site_component_type = (*body)["site_component_type"].asString();
                }
                db::ComponentInventoryRepository repository(db_client);
                respond_inventory_outcome(callback, repository.confirm_pending_mappings(
                    revision_id, user->id, site_component_type));
            } catch (...) { respond_db_unavailable(callback); }
        }, {drogon::Post});

    drogon::app().registerHandler(
        confirm_path,
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& revision_id) {
            if (!is_valid_uuid(revision_id)) {
                respond_json(callback, make_error_body("component_inventory_not_found", "构件台账不存在。"),
                             drogon::k404NotFound); return;
            }
            try {
                const auto user = require_user(db_client, request, callback);
                if (!user.has_value()) return;
                const auto body = request->getJsonObject();
                const std::string note = body != nullptr && (*body)["note"].isString()
                    ? (*body)["note"].asString() : "";
                db::ComponentInventoryRepository repository(db_client);
                respond_inventory_outcome(callback, repository.confirm_revision(
                    revision_id, user->id, note));
            } catch (...) { respond_db_unavailable(callback); }
        }, {drogon::Post});
}

}  // namespace bridge_report::http
