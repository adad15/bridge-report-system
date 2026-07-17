#include "bridge_report/standards/StandardCatalogModels.hpp"

#include <string>

namespace bridge_report::standards {
namespace {

Json::Value array_or_empty(const Json::Value& value) {
    return value.isArray() ? value : Json::Value(Json::arrayValue);
}

const Json::Value* document(const StandardPackage& package, const char* name) {
    const auto found = package.documents.find(name);
    return found == package.documents.end() ? nullptr : &found->second;
}

Json::Value definitions(const StandardPackage& package, const char* name) {
    const auto* value = document(package, name);
    if (value == nullptr || !value->isObject()) {
        return Json::Value(Json::arrayValue);
    }
    return array_or_empty((*value)["definitions"]);
}

Json::Value technical_catalog(const StandardPackage& package) {
    Json::Value catalog;
    catalog["bridge_types"] = definitions(package, "bridge-types.json");
    catalog["component_categories"] = definitions(package, "component-taxonomy.json");
    catalog["defect_catalogs"] = definitions(package, "defect-indicators.json");
    catalog["maintenance_levels"] = Json::Value(Json::arrayValue);
    catalog["inspection_types"] = Json::Value(Json::arrayValue);
    catalog["periodic_inspection_requirements"] = Json::Value(Json::arrayValue);
    return catalog;
}

Json::Value maintenance_catalog(const StandardPackage& package) {
    Json::Value catalog;
    catalog["bridge_types"] = Json::Value(Json::arrayValue);
    catalog["component_categories"] = Json::Value(Json::arrayValue);
    catalog["defect_catalogs"] = Json::Value(Json::arrayValue);
    catalog["maintenance_levels"] = definitions(package, "maintenance-levels.json");
    catalog["inspection_types"] = definitions(package, "inspection-types.json");
    catalog["periodic_inspection_requirements"] =
        definitions(package, "periodic-inspection-requirements.json");
    return catalog;
}

}  // namespace

Json::Value standard_package_summary_json(const db::StandardPackageRecord& package) {
    Json::Value json;
    json["id"] = package.id;
    json["family"] = to_string(package.family);
    json["standard_id"] = package.standard_id;
    json["standard_code"] = package.standard_code;
    json["standard_name"] = package.standard_name;
    json["official_edition"] = package.official_edition;
    json["package_version"] = package.package_version;
    json["contract_version"] = package.contract_version;
    json["algorithm_id"] = package.algorithm_id;
    json["effective_date"] = package.effective_date;
    json["content_checksum"] = package.content_checksum;
    json["is_enabled"] = package.is_enabled;
    json["sync_status"] = package.sync_status;
    json["sync_error_code"] = package.sync_error_code.has_value()
        ? Json::Value(*package.sync_error_code) : Json::Value(Json::nullValue);
    json["sync_error_message"] = package.sync_error_message.has_value()
        ? Json::Value(*package.sync_error_message) : Json::Value(Json::nullValue);
    return json;
}

Json::Value standard_catalog_json(
    const db::StandardPackageRecord& record,
    const StandardPackage& package) {
    Json::Value json = record.family == StandardFamily::technical_condition
        ? technical_catalog(package) : maintenance_catalog(package);
    json["package"] = standard_package_summary_json(record);
    return json;
}

}  // namespace bridge_report::standards
