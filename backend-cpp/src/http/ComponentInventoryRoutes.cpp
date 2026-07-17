#include "bridge_report/http/ComponentInventoryRoutes.hpp"

#include <initializer_list>
#include <map>
#include <optional>
#include <set>
#include <utility>

#include "bridge_report/db/StandardRepository.hpp"
#include "bridge_report/http/AuthRoutes.hpp"
#include "bridge_report/http/RouteHelpers.hpp"
#include "bridge_report/inventory/ComponentInventoryGenerator.hpp"

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

}  // namespace

bool validate_inventory_generation_standard(
    const inventory::GenerateInventoryInput& input,
    const standards::StandardPackage& package,
    std::string& error_code,
    std::string& error_message) {
    const auto template_it = package.definitions.find(input.template_id);
    if (template_it == package.definitions.end() ||
        template_it->second.source_file != "inventory-templates.json" ||
        template_it->second.payload["bridge_type_id"].asString() != input.bridge_type_id) {
        error_code = "inventory_template_not_found";
        error_message = "所选规范没有适用于该桥型的构件模板。";
        return false;
    }

    const auto& quantity_inputs = template_it->second.payload["quantity_inputs"];
    if (!quantity_inputs.isArray()) {
        error_code = "inventory_template_quantities_invalid";
        error_message = "规范构件模板缺少数量输入定义。";
        return false;
    }
    std::set<std::string> allowed_quantity_keys;
    for (const auto& quantity : quantity_inputs) {
        if (!quantity.isString() || quantity.asString().empty()) {
            error_code = "inventory_template_quantities_invalid";
            error_message = "规范构件模板的数量输入定义无效。";
            return false;
        }
        const auto key = quantity.asString();
        allowed_quantity_keys.insert(key);
        if (!input.input_quantities.isMember(key) || !input.input_quantities[key].isInt() ||
            input.input_quantities[key].asInt() < 0 || input.input_quantities[key].asInt() > 10000) {
            error_code = "inventory_template_quantity_required";
            error_message = "必须完整填写规范构件模板要求的非负整数数量。";
            return false;
        }
    }
    if (allowed_quantity_keys.contains("span_count") &&
        input.input_quantities["span_count"].asInt() != input.span_count) {
        error_code = "inventory_span_count_mismatch";
        error_message = "跨数与规范模板数量输入不一致。";
        return false;
    }

    std::map<std::string, int> group_counts;
    for (const auto& group : input.groups) {
        if (!allowed_quantity_keys.contains(group.quantity_key) ||
            group.quantity_key == "span_count" ||
            input.input_quantities[group.quantity_key].asInt() != group.quantity) {
            error_code = "inventory_group_quantity_mismatch";
            error_message = "构件分组数量必须对应规范模板中的同名数量项。";
            return false;
        }
        ++group_counts[group.quantity_key];
        const auto category = package.definitions.find(group.standard_component_category_id);
        if (category == package.definitions.end() ||
            category->second.source_file != "component-taxonomy.json" ||
            !definition_supports_bridge_type(category->second, input.bridge_type_id) ||
            category->second.payload["structure_part"].asString() != group.structure_part ||
            !category->second.payload.get("generatable", false).asBool()) {
            error_code = "inventory_component_category_not_supported";
            error_message = "构件类别不属于所选规范和桥型，不能静默归入其他类别。";
            return false;
        }
    }
    for (const auto& key : allowed_quantity_keys) {
        if (key == "span_count") continue;
        const auto expected_groups = input.input_quantities[key].asInt() > 0 ? 1 : 0;
        if (group_counts[key] != expected_groups) {
            error_code = "inventory_template_quantity_group_incomplete";
            error_message = "每个非零模板数量项必须且只能对应一个构件生成分组。";
            return false;
        }
    }
    return true;
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
    const std::string latest_path = "/api/bridges/{bridge_id}/component-inventories/latest";
    const std::string revision_path = "/api/component-inventories/{revision_id}";
    const std::string entries_path = "/api/component-inventories/{revision_id}/entries";
    const std::string entry_path = "/api/component-inventories/{revision_id}/entries/{entry_id}";
    const std::string deactivate_path = entry_path + "/deactivate";
    const std::string mapping_path = entry_path + "/mapping";
    const std::string confirm_path = revision_path + "/confirm";
    for (const auto& path : {generate_path, latest_path, revision_path, entries_path,
                             entry_path, deactivate_path, mapping_path, confirm_path})
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
