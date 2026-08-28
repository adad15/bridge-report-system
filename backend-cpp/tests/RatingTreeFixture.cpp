#include "RatingTreeFixture.hpp"

#include <utility>

namespace bridge_report::testing {
namespace {

std::string insert_id(const drogon::orm::DbClientPtr& client, const std::string& sql,
                      auto&&... args) {
    return client->execSqlSync(sql, std::forward<decltype(args)>(args)...)[0]["id"]
        .as<std::string>();
}

}  // namespace

RatingTreeFixture seed_rating_tree(
    const drogon::orm::DbClientPtr& client,
    const std::string& user_id,
    const std::string& tag) {
    RatingTreeFixture fixture;
    fixture.bridge_type_id = "h21.bridge_type.beam";
    fixture.component_category_id = "h21.component.deck.slab";
    fixture.other_component_category_id = "h21.component.beam.upper_bearing";

    fixture.technical_package_id = insert_id(client,
        "insert into standard_packages(standard_family,standard_id,standard_code,"
        "standard_name,official_edition,package_version,contract_version,algorithm_id,"
        "effective_date,content_checksum) values('technical_condition',"
        "$1||'-tech-'||gen_random_uuid()::text,'TEST H21','测试技术标准','2026','1.0.0',1,"
        "'test-h21','2026-01-01','sha256:'||md5(gen_random_uuid()::text)"
        "||md5(gen_random_uuid()::text)) returning id::text as id", tag);

    fixture.maintenance_package_id = insert_id(client,
        "insert into standard_packages(standard_family,standard_id,standard_code,"
        "standard_name,official_edition,package_version,contract_version,algorithm_id,"
        "effective_date,content_checksum) values('maintenance',"
        "$1||'-maint-'||gen_random_uuid()::text,'TEST 5120','测试养护规范','2026','1.0.0',1,"
        "'test-maintenance','2026-01-01','sha256:'||md5(gen_random_uuid()::text)"
        "||md5(gen_random_uuid()::text)) returning id::text as id", tag);

    // 校验和列唯一，因此当场生成；published 要求 published_at 非空，分两步写。
    fixture.tree_version_id = insert_id(client,
        "insert into rating_tree_versions("
        "tree_code,tree_name,package_version,contract_version,"
        "technical_condition_package_id,technical_condition_standard_id,"
        "technical_condition_package_version,technical_condition_content_checksum,"
        "maintenance_package_id,maintenance_standard_id,"
        "maintenance_package_version,maintenance_content_checksum,"
        "organization_tree_code,organization_package_version,"
        "organization_content_checksum,tree_content_checksum,status"
        ") select $1||'-tree-'||gen_random_uuid()::text,'测试评定树','1.0.0',1,"
        "       $2::uuid,t.standard_id,t.package_version,t.content_checksum,"
        "       $3::uuid,m.standard_id,m.package_version,m.content_checksum,"
        "       $1||'-org','1.0.0',"
        "       'sha256:'||md5(gen_random_uuid()::text)||md5(gen_random_uuid()::text),"
        "       'sha256:'||md5(gen_random_uuid()::text)||md5(gen_random_uuid()::text),'draft' "
        "from standard_packages t, standard_packages m "
        "where t.id=$2::uuid and m.id=$3::uuid returning id::text as id",
        tag, fixture.technical_package_id, fixture.maintenance_package_id);

    client->execSqlSync(
        "insert into rating_tree_nodes("
        "rating_tree_version_id,node_key,display_name,node_type,scoring_mode,is_selectable"
        ") values ($1::uuid,'root','桥梁评定','root','non_scoring',false)",
        fixture.tree_version_id);

    // 病害节点声明自己适用的桥型与构件类别；命令层按这两项判定「节点适用于该构件」。
    fixture.node_id = insert_id(client,
        "insert into rating_tree_nodes("
        "rating_tree_version_id,node_key,display_name,node_type,scoring_mode,"
        "is_selectable,bridge_type_ids,component_category_ids,allowed_scales"
        ") values ($1::uuid,'defect.crack','裂缝','defect','non_scoring',true,"
        "array[$2],array[$3],array[1,2,3]) returning id::text as id",
        fixture.tree_version_id, fixture.bridge_type_id, fixture.component_category_id);

    fixture.inapplicable_node_id = insert_id(client,
        "insert into rating_tree_nodes("
        "rating_tree_version_id,node_key,display_name,node_type,scoring_mode,"
        "is_selectable,bridge_type_ids,component_category_ids,allowed_scales"
        ") values ($1::uuid,'defect.other','其他部位病害','defect','non_scoring',true,"
        "array[$2],array[$3],array[1,2]) returning id::text as id",
        fixture.tree_version_id, fixture.bridge_type_id,
        fixture.other_component_category_id);

    client->execSqlSync(
        "update rating_tree_versions set status='published',published_at=now() "
        "where id=$1::uuid", fixture.tree_version_id);

    fixture.profile_id = insert_id(client,
        "insert into project_standard_profiles(technical_condition_package_id,"
        "maintenance_package_id,rating_tree_version_id,created_by_user_id,change_reason) "
        "values($1::uuid,$2::uuid,$3::uuid,$4::uuid,'测试夹具') returning id::text as id",
        fixture.technical_package_id, fixture.maintenance_package_id,
        fixture.tree_version_id, user_id);

    return fixture;
}

void drop_rating_tree(
    const drogon::orm::DbClientPtr& client,
    const RatingTreeFixture& fixture) {
    if (fixture.profile_id.empty()) return;
    client->execSqlSync("delete from project_standard_profiles where id=$1::uuid",
                        fixture.profile_id);
    // 已发布的树不可变（018 的 protect_published_rating_tree_version），清场时暂停它；
    // 而树对规范包是 RESTRICT 外键，所以树必须排在规范包之前删。
    client->execSqlSync(
        "alter table rating_tree_versions disable trigger "
        "trg_rating_tree_versions_published_immutable");
    client->execSqlSync("delete from rating_tree_versions where id=$1::uuid",
                        fixture.tree_version_id);
    client->execSqlSync(
        "alter table rating_tree_versions enable trigger "
        "trg_rating_tree_versions_published_immutable");
    // 切换评定树时，bind_rating_tree 会为新规范包继承一份台账映射；那条外键是
    // RESTRICT，映射必须排在规范包之前删。而已确认台账的映射又是不可变的
    // （011 的 protect_confirmed_component_standard_mapping），清场时暂停它。
    client->execSqlSync(
        "alter table bridge_component_standard_mappings disable trigger "
        "trg_component_standard_mappings_confirmed_immutable");
    client->execSqlSync(
        "delete from bridge_component_standard_mappings "
        "where standard_package_id in ($1::uuid,$2::uuid)",
        fixture.technical_package_id, fixture.maintenance_package_id);
    client->execSqlSync(
        "alter table bridge_component_standard_mappings enable trigger "
        "trg_component_standard_mappings_confirmed_immutable");
    client->execSqlSync("delete from standard_packages where id in ($1::uuid,$2::uuid)",
                        fixture.technical_package_id, fixture.maintenance_package_id);
}

}  // namespace bridge_report::testing
