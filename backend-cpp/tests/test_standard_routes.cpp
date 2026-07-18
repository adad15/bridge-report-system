#include <string>

#include <gtest/gtest.h>

#include "bridge_report/http/StandardRoutes.hpp"
#include "bridge_report/standards/StandardCatalogModels.hpp"

TEST(StandardRoutesTest, EnabledRequestRequiresBoolean) {
    bool enabled = false;
    Json::Value valid;
    valid["enabled"] = true;
    EXPECT_TRUE(bridge_report::http::parse_standard_enabled_request(valid, enabled));
    EXPECT_TRUE(enabled);

    Json::Value invalid;
    invalid["enabled"] = "true";
    EXPECT_FALSE(bridge_report::http::parse_standard_enabled_request(invalid, enabled));
}

TEST(StandardRoutesTest, CatalogResponseDoesNotExposePackageFilePaths) {
    bridge_report::db::StandardPackageRecord record;
    record.id = "11111111-1111-1111-1111-111111111111";
    record.family = bridge_report::standards::StandardFamily::technical_condition;
    record.standard_id = "TEST";
    record.standard_code = "TEST 2026";
    record.standard_name = "测试规范";
    record.official_edition = "2026";
    record.package_version = "1.0.0";
    record.contract_version = 1;
    record.algorithm_id = "test";
    record.effective_date = "2026-01-01";
    record.content_checksum = "sha256:" + std::string(64, '1');
    record.is_enabled = true;
    record.sync_status = "正常";

    bridge_report::standards::StandardPackage package;
    package.manifest.family = record.family;
    Json::Value bridge_types;
    bridge_types["definitions"] = Json::Value(Json::arrayValue);
    Json::Value bridge_type;
    bridge_type["id"] = "test.bridge_type";
    bridge_type["name"] = "测试桥型";
    bridge_types["definitions"].append(bridge_type);
    package.documents["bridge-types.json"] = bridge_types;
    Json::Value inventory_templates;
    inventory_templates["definitions"] = Json::Value(Json::arrayValue);
    Json::Value inventory_template;
    inventory_template["id"] = "test.inventory_template";
    inventory_template["bridge_type_id"] = "test.bridge_type";
    inventory_template["quantity_inputs"] = Json::Value(Json::arrayValue);
    inventory_template["quantity_inputs"].append("span_count");
    inventory_templates["definitions"].append(inventory_template);
    package.documents["inventory-templates.json"] = inventory_templates;

    const auto response = bridge_report::standards::standard_catalog_json(record, package);
    const auto serialized = response.toStyledString();
    EXPECT_EQ(response["bridge_types"].size(), 1u);
    ASSERT_EQ(response["inventory_templates"].size(), 1u);
    EXPECT_EQ(response["inventory_templates"][0]["id"].asString(), "test.inventory_template");
    EXPECT_EQ(serialized.find("source_file"), std::string::npos);
    EXPECT_EQ(serialized.find("absolute_path"), std::string::npos);
    EXPECT_EQ(serialized.find("standards/technical-condition"), std::string::npos);
}
