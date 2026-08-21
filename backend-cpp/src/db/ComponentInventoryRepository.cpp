#include "bridge_report/db/ComponentInventoryRepository.hpp"
#include "bridge_report/inventory/ComponentPartCatalog.hpp"
#include "bridge_report/inventory/ComponentMatcher.hpp"

#include <trantor/utils/Logger.h>

#include <cctype>

#include <memory>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

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

    // 一次取回本修订版全部映射，在内存里按构件归组。逐条查会让往返次数随构件数线性增长：
    // 4941 条的台账实测 3004ms，批量后 155ms。排序与逐条版一致，故各构件内映射顺序不变。
    std::unordered_map<std::string, std::vector<inventory::InventoryMapping>> mappings_by_entry;
    const auto mapping_rows = client->execSqlSync(
        "select m.id::text,m.inventory_entry_id::text,m.standard_package_id::text,"
        "m.standard_bridge_type_id,m.standard_component_category_id,m.structure_part,"
        "m.mapping_source,m.confirmation_status,m.is_active "
        "from bridge_component_standard_mappings m "
        "join bridge_component_inventory_entries e on e.id=m.inventory_entry_id "
        "where e.inventory_revision_id=$1::uuid "
        "order by m.inventory_entry_id,m.is_active desc,m.created_at,m.id",
        revision_id);
    for (const auto& mapping_row : mapping_rows) {
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
        mappings_by_entry[mapping_row["inventory_entry_id"].as<std::string>()]
            .push_back(std::move(mapping));
    }

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
        if (auto found = mappings_by_entry.find(entry.id); found != mappings_by_entry.end())
            entry.mappings = std::move(found->second);
        revision.entries.push_back(std::move(entry));
    }
    return revision;
}

struct EditableTarget {
    bool found{false};
    std::string revision_id;
    std::string entry_id;
    std::string component_id;
    // 桥上已有基于另一个版本的草稿，本次请求携带的已确认版本不能再派生。
    // 放在末尾是为了不打断既有的位置初始化。
    bool superseded{false};
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
    // 锁到桥上，不是锁到修订版上。两个并发请求若从不同的已确认版本派生，
    // 上面那句 for update 锁的是两行不同的修订版，谁也挡不住谁，结果各建一条草稿。
    tx->execSqlSync("select 1 from bridges where id=$1::uuid for update", bridge_id);

    // 只有最新的已确认版本能派生草稿。从更早的版本派生，会把它之后确认的改动丢在
    // 一边，而派生出的草稿一旦确认就成了最新版——等于静默回滚。
    const auto latest_confirmed = tx->execSqlSync(
        "select id::text from bridge_component_inventory_revisions "
        "where bridge_id=$1::uuid and status='已确认' order by revision_number desc limit 1",
        bridge_id);
    if (!latest_confirmed.empty() &&
        latest_confirmed[0]["id"].as<std::string>() != source_revision_id) {
        EditableTarget stale;
        stale.superseded = true;
        return stale;
    }

    // 按 baseline 找草稿是不够的：桥上已有基于 R2 的草稿时，拿 R1 进来会找不到匹配，
    // 于是又建一条以 R1 为 baseline 的草稿，一桥两条分支，而草稿优先的"最新版本"
    // 排序只挑得中其中一条。这里改成先看"有没有草稿"，baseline 不符直接判 superseded。
    auto draft = tx->execSqlSync(
        "select id::text,baseline_revision_id::text from bridge_component_inventory_revisions "
        "where bridge_id=$1::uuid and status='草稿' limit 1 for update",
        bridge_id);
    std::string draft_id;
    if (!draft.empty()) {
        const bool same_baseline = !draft[0]["baseline_revision_id"].isNull() &&
            draft[0]["baseline_revision_id"].as<std::string>() == source_revision_id;
        if (!same_baseline) {
            EditableTarget stale;
            stale.superseded = true;
            return stale;
        }
        draft_id = draft[0]["id"].as<std::string>();
    } else {
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

// 组内序号的唯一定义。分组分页、编号搜索、汇总里的 blocker 样本三处都用它——
// 各写一遍必然漂移，而"定位"按钮就是拿这个序号算页码的，漂了就会跳到错的页。
// 刻意不按 is_active 过滤：停用构件在分组弹窗里仍然可见并占位。
std::string entry_position_window_sql() {
    return "row_number() over (partition by e.site_component_type "
           "order by e.sort_order,e.id)-1";
}

// 用户输入按字面匹配。不转义的话搜一个 % 就会命中全表——那正是聚合要消灭的响应。
std::string escaped_like_expr(const std::string& parameter) {
    return "replace(replace(replace(" + parameter + ",'\\','\\\\'),'%','\\%'),'_','\\_')";
}

// 确认前置校验的唯一规则来源：只产出具名 CTE 的 SQL 文本，不执行查询。
// confirm 语句和汇总语句各自把它嵌进自己那条 SQL——共用的是文本而不是一次查询执行，
// 这样两处永远是同一套判定，又都各自处在单语句快照里。
// 约定 $1 = inventory_revision_id。
//
// 判定用 not exists 而不是 join + count：唯一索引是
// (inventory_entry_id, standard_package_id) where is_active，一个构件可以按规范包
// 挂多个生效映射，join 会把它展开成多行，count 随之偏大。
std::string blocker_cte_sql() {
    return
        "inventory_active_entries as ("
        "select count(*)::int as value from bridge_component_inventory_entries "
        "where inventory_revision_id=$1::uuid and is_active"
        "),"
        "inventory_unconfirmed_entries as ("
        "select e.id::text as entry_id,e.component_number,e.site_component_type,e.sort_order,"
        "exists(select 1 from bridge_component_standard_mappings m "
        "where m.inventory_entry_id=e.id and m.is_active) as has_mapping "
        "from bridge_component_inventory_entries e "
        "where e.inventory_revision_id=$1::uuid and e.is_active "
        "and not exists(select 1 from bridge_component_standard_mappings m "
        "where m.inventory_entry_id=e.id and m.is_active "
        "and m.confirmation_status='已确认')"
        ")";
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
    // 这里刻意不再塞一个只有 id 的空壳 revision。除 generate_draft 外的写方法不再
    // 装配全量，留个 entries 为空的壳只会让调用方拿到"看起来有、其实是空"的数据；
    // 留成 nullopt，误用会当场失败。修订版 id 由 entry_id 之外的 summary 带回。
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

std::optional<std::string> ComponentInventoryRepository::find_latest_revision_id(
    const std::string& bridge_id) const {
    // 草稿优先于已确认；同类之间按 revision_number 倒序。管理页依赖这条排序看见草稿。
    const auto rows = db_client_->execSqlSync(
        "select id::text from bridge_component_inventory_revisions where bridge_id=$1::uuid "
        "order by (status='草稿') desc,revision_number desc limit 1",
        bridge_id);
    if (rows.empty()) return std::nullopt;
    return rows[0]["id"].as<std::string>();
}

std::optional<Json::Value> ComponentInventoryRepository::load_summary(
    const std::string& revision_id) const {
    return load_summary_with(db_client_, revision_id);
}

std::optional<Json::Value> ComponentInventoryRepository::load_summary_with(
    const drogon::orm::DbClientPtr& executor,
    const std::string& revision_id) {
    // blocker 规则用 blocker_cte_sql() 的文本，与 confirm 嵌的是同一段。
    const std::string sql =
        "with " + blocker_cte_sql() + ","
        "target as ("
        "select id,bridge_id,revision_number,status,baseline_revision_id,confirmed_at "
        "from bridge_component_inventory_revisions where id=$1::uuid"
        "),"
        // 先按构件把生效映射收敛成一行。直接 left join 映射表的话，一个挂了多个规范包
        // 生效映射的构件会展开成多行，下面的 count(*) 就把它数了好几遍。
        "entry_mapping as ("
        "select m.inventory_entry_id,"
        "bool_or(m.confirmation_status='已确认') as has_confirmed,"
        "(array_agg(m.structure_part order by m.created_at,m.id))[1] as structure_part,"
        "(array_agg(m.standard_package_id::text order by m.created_at,m.id))[1] as package_id,"
        "(array_agg(m.standard_component_category_id order by m.created_at,m.id))[1] as category_id,"
        "(array_agg(m.standard_bridge_type_id order by m.created_at,m.id))[1] as bridge_type_id "
        "from bridge_component_standard_mappings m "
        "join bridge_component_inventory_entries e on e.id=m.inventory_entry_id "
        "where e.inventory_revision_id=$1::uuid and m.is_active "
        "group by m.inventory_entry_id"
        "),"
        // 组内序号：分页与"定位"共用的唯一权威，不按 is_active 过滤——停用构件在
        // 分组弹窗里仍然可见并占位，过滤掉会让页码对不上。
        "numbered as ("
        "select e.id,e.component_number,e.site_component_type,e.sort_order,e.is_active,"
        "em.has_confirmed,em.structure_part as em_part,em.package_id as em_package,"
        "em.category_id as em_category,em.bridge_type_id as em_bridge_type,"
        "em.inventory_entry_id is not null as has_mapping,"
        + entry_position_window_sql() + " as position "
        "from bridge_component_inventory_entries e "
        "left join entry_mapping em on em.inventory_entry_id=e.id "
        "where e.inventory_revision_id=$1::uuid"
        "),"
        "grouped as ("
        "select n.site_component_type,"
        "count(*) filter (where n.is_active) as active_count,"
        // 编号范围只统计启用构件，与 active_count 口径一致。取的是遍历首尾而不是
        // min/max：编号是字符串，字典序下 '9-2-9#支座' > '33-2-50#支座'，33 孔的桥
        // 用 min/max 会把范围末端取到第 9 孔。
        "(array_agg(n.component_number order by n.sort_order,n.id) "
        "filter (where n.is_active))[1] as first_number,"
        "(array_agg(n.component_number order by n.sort_order desc,n.id desc) "
        "filter (where n.is_active))[1] as last_number,"
        // 首个取值非 other 的启用构件；全是 other 或都没映射时留 other。
        "coalesce((array_agg(n.em_part order by n.sort_order,n.id) "
        "filter (where n.is_active and n.em_part is not null and n.em_part<>'other'))[1],"
        "'other') as structure_part,"
        "(array_agg(n.em_category order by n.sort_order,n.id) "
        "filter (where n.is_active and n.has_mapping))[1] as category_id,"
        "(array_agg(n.em_package order by n.sort_order,n.id) "
        "filter (where n.is_active and n.has_mapping))[1] as package_id,"
        // 规范类别要配上桥型才能定位评定树节点；少了它，调用方只能靠下载整份台账
        // 从任意一条映射里把桥型翻出来。
        "(array_agg(n.em_bridge_type order by n.sort_order,n.id) "
        "filter (where n.is_active and n.has_mapping))[1] as bridge_type_id,"
        // 三分互斥且覆盖全部启用构件，三者之和等于 active_count。
        "count(*) filter (where n.is_active and n.has_confirmed) as confirmed_count,"
        "count(*) filter (where n.is_active and n.has_mapping and not n.has_confirmed) "
        "as pending_count,"
        "count(*) filter (where n.is_active and not n.has_mapping) as unmapped_count,"
        // 分组顺序按首个构件的 (sort_order,id)。sort_order 没有唯一约束且默认 0，
        // 手工新增的构件会撞在一起，只按 min(sort_order) 排会不稳定。
        "(array_agg(n.sort_order order by n.sort_order,n.id))[1] as ord_so,"
        "(array_agg(n.id::text order by n.sort_order,n.id))[1] as ord_id "
        "from numbered n group by n.site_component_type"
        "),"
        "totals as ("
        "select coalesce(sum(pending_count),0)::bigint as pending_total,"
        "coalesce(sum(unmapped_count),0)::bigint as unmapped_total from grouped"
        "),"
        "empty_flag as ("
        "select case when (select value from inventory_active_entries)=0 then 1 else 0 end as v"
        "),"
        // 样本只收空台账和"完全没有生效映射"的构件；有待确认映射的那批由界面上
        // "N 个构件的规范映射待确认"那一行代表，进样本会被数两遍。
        "sample_rows as ("
        "select 0 as ord_group,'inventory_empty' as code,'inventory_revision' as entity_type,"
        "$1::text as entity_id,'entries' as field_path,"
        "'构件台账至少需要一个启用构件。' as message,"
        "null::text as site_component_type,null::bigint as position,"
        "0 as sort_order,'' as tie "
        "where (select value from inventory_active_entries)=0 "
        "union all "
        "select 1,'component_mapping_required','inventory_entry',n.id::text,'mappings',"
        "'构件 '||n.component_number||' 至少需要一个已确认的有效规范映射。',"
        "n.site_component_type,n.position,n.sort_order,n.id::text "
        "from numbered n where n.is_active and not n.has_mapping"
        "),"
        "sample_limited as ("
        "select * from sample_rows order by ord_group,sort_order,tie limit 30"
        ") "
        "select json_build_object("
        "'revision',(select json_build_object("
        "'id',id::text,'bridge_id',bridge_id::text,'revision_number',revision_number,"
        "'status',status,'baseline_revision_id',baseline_revision_id::text,"
        "'confirmed_at',confirmed_at::text,"
        "'active_entry_count',(select value from inventory_active_entries)) from target),"
        "'groups',coalesce((select json_agg(json_build_object("
        "'site_component_type',site_component_type,'structure_part',structure_part,"
        "'active_count',active_count,'first_number',first_number,'last_number',last_number,"
        "'confirmed_count',confirmed_count,'pending_count',pending_count,"
        "'unmapped_count',unmapped_count,'standard_package_id',package_id,"
        "'standard_component_category_id',category_id,"
        "'standard_bridge_type_id',bridge_type_id) order by ord_so,ord_id) "
        "from grouped),'[]'::json),"
        "'blockers',json_build_object("
        // total = 待确认 + individual_total；"其余 N 项"用 individual_total 算，
        // 用 total 会把待确认那批数两遍。
        "'total',(select pending_total+unmapped_total from totals)+(select v from empty_flag),"
        "'individual_total',(select unmapped_total from totals)+(select v from empty_flag),"
        "'by_code',json_build_object("
        "'inventory_empty',(select v from empty_flag),"
        "'component_mapping_required',(select pending_total+unmapped_total from totals)),"
        "'samples',coalesce((select json_agg(json_build_object("
        "'code',code,'entity_type',entity_type,'entity_id',entity_id,"
        "'field_path',field_path,'message',message,"
        "'site_component_type',site_component_type,'position',position) "
        "order by ord_group,sort_order,tie) from sample_limited),'[]'::json))"
        ") as summary";

    const auto rows = executor->execSqlSync(sql, revision_id);
    if (rows.empty()) return std::nullopt;
    Json::Value summary;
    Json::CharReaderBuilder builder;
    std::string errors;
    const auto text = rows[0]["summary"].as<std::string>();
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader->parse(text.data(), text.data() + text.size(), &summary, &errors)) {
        return std::nullopt;
    }
    if (summary["revision"].isNull()) return std::nullopt;
    return summary;
}

namespace {

// is_referenced 的判定与 load_revision() 里那段一致：被病害、病害线索、技术状况评定
// 或年度检测引用过的构件只能停用、不能删除。区别只在于这里只对翻到的那一页跑，
// 而不是对整份台账的每一行跑。
constexpr const char* kIsReferencedSql =
    "exists(select 1 from defect_observations o "
    "where o.bridge_component_id=s.bridge_component_id "
    "union all select 1 from defect_threads t "
    "where t.bridge_component_id=s.bridge_component_id "
    "union all select 1 from condition_ratings cr "
    "where cr.bridge_component_id=s.bridge_component_id "
    "union all select 1 from inspection_years iy "
    "join bridge_component_inventory_entries ie "
    "on ie.inventory_revision_id=iy.component_inventory_revision_id "
    "where ie.bridge_component_id=s.bridge_component_id limit 1)";

// 启用且至少有一个生效映射。CTE 里先算好，谓词就只是引用一列，不必把带子查询的
// 表达式重复拼进三条语句。
constexpr const char* kHasActiveMappingSql =
    "exists(select 1 from bridge_component_standard_mappings m "
    "where m.inventory_entry_id=e.id and m.is_active)";

// 拼 Postgres 数组字面量，作为参数绑定（不进 SQL 文本，故无注入面）。
// 但候选 id 来自 parsed_result_json，是历史遗留数据；一个畸形值会让 ::uuid[] 转换
// 抛错，把整个绑定概览拖成 503。这里直接跳过不合法的，宁可少显示一个候选。
bool looks_like_uuid(const std::string& value) {
    if (value.size() != 36) return false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return false;
        } else if (!std::isxdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

std::string pg_uuid_array(const std::vector<std::string>& ids) {
    std::string joined = "{";
    bool first = true;
    for (const auto& id : ids) {
        if (!looks_like_uuid(id)) continue;
        if (!first) joined += ',';
        joined += id;
        first = false;
    }
    return joined + "}";
}

// text[] 字面量：把每个元素用双引号包起来，内部的引号和反斜杠转义。
// 类别 id 目前都来自代码里的常量，但拼字面量这件事本身就该有个正确的实现。
std::string pg_text_array(const std::vector<std::string>& values) {
    std::string joined = "{";
    bool first = true;
    for (const auto& value : values) {
        if (!first) joined += ',';
        joined += '"';
        for (const char ch : value) {
            if (ch == '"' || ch == '\\') joined += '\\';
            joined += ch;
        }
        joined += '"';
        first = false;
    }
    return joined + "}";
}

}  // namespace

// 两个查询共用的装配：先取一页构件，再一次性把这页的生效映射取回来按构件归组。
// 逐条取映射会让往返次数随页大小线性增长，那正是本次要消灭的形态。
namespace {

// 谓词与 CTE 的 select 列表在这里一并决定，保证"谓词引用到的列，CTE 一定 select 了"。
// $2 是取值槽：分组是类别名，检索是关键词；两者都不用时传空串，谓词也不引用它。
struct ScopedSql {
    std::string predicate;   // 已按别名 s 限定
    std::string extra_columns;  // 谓词需要、而基础列表里没有的列
};

ScopedSql scoped_sql_for(const std::string& entry_id,
                         const std::string& site_component_type,
                         const std::string& keyword, bool binding_eligible) {
    std::vector<std::string> clauses;
    std::string extra;
    if (!entry_id.empty()) {
        clauses.emplace_back("s.id=$2::uuid");
    } else if (!site_component_type.empty()) {
        clauses.emplace_back("s.site_component_type=$2");
    } else if (!keyword.empty()) {
        // 三个字段任一命中即可；子串语义与前端原来的 String.includes 一致。
        const auto like = "'%'||" + escaped_like_expr("$2") + "||'%' escape '\\'";
        clauses.emplace_back(
            "(s.component_number like " + like +
            " or s.site_component_type like " + like +
            " or s.site_name like " + like + ")");
    }
    if (binding_eligible) {
        clauses.emplace_back("s.is_active and s.has_active_mapping");
        extra = std::string(kHasActiveMappingSql) + " as has_active_mapping,";
    }
    std::string predicate = "true";
    for (const auto& clause : clauses) predicate += " and " + clause;
    return {predicate, extra};
}

}  // namespace

ComponentInventoryRepository::EntryLookup
ComponentInventoryRepository::load_entry_page(
    const drogon::orm::DbClientPtr& executor,
    const std::string& revision_id,
    const EntryQuery& query,
    std::int64_t offset,
    std::int64_t limit) {
    EntryLookup lookup;
    const auto scoped = scoped_sql_for(query.entry_id, query.site_component_type,
                                      query.keyword, query.binding_eligible);
    const std::string scope_value = !query.entry_id.empty() ? query.entry_id
        : !query.site_component_type.empty() ? query.site_component_type : query.keyword;

    // count 的 CTE 不必算位置窗口与 is_referenced，只需谓词用到的列。
    const std::string count_sql =
        "with scoped as (select e.is_active,e.component_number,e.site_component_type,"
        "e.site_name," + scoped.extra_columns +
        "e.id from bridge_component_inventory_entries e "
        "where e.inventory_revision_id=$1::uuid) "
        "select count(*)::bigint as value from scoped s where " + scoped.predicate;
    // total 单独查一次。放在分页语句里用 count(*) over () 的话，页码越界时返回零行，
    // 就没有任何一行能把总数带回来，前端也就无从夹取页码。
    lookup.total = executor->execSqlSync(count_sql, revision_id, scope_value)[0]["value"]
                       .as<std::int64_t>();

    const std::string page_sql =
        "with scoped as ("
        "select e.id,e.bridge_component_id,e.component_number,e.site_name,"
        "e.site_component_type,e.span_or_location,e.is_active,e.deactivated_at,"
        "e.deactivation_reason,e.sort_order,e.remarks," + scoped.extra_columns +
        entry_position_window_sql() + " as position "
        "from bridge_component_inventory_entries e "
        "where e.inventory_revision_id=$1::uuid"
        ") "
        "select s.id::text,s.bridge_component_id::text,s.component_number,s.site_name,"
        "s.site_component_type,s.span_or_location,s.is_active,s.deactivated_at::text,"
        "s.deactivation_reason,s.sort_order,s.remarks,s.position," +
        std::string(kIsReferencedSql) + " as is_referenced "
        "from scoped s where " + scoped.predicate +
        " order by s.sort_order,s.id offset $3 limit $4";

    const auto rows = executor->execSqlSync(page_sql, revision_id, scope_value, offset, limit);
    std::vector<std::string> page_ids;
    std::unordered_map<std::string, std::size_t> index_by_id;
    for (const auto& row : rows) {
        inventory::LocatedInventoryEntry located;
        auto& entry = located.entry;
        entry.id = row["id"].as<std::string>();
        entry.bridge_component_id = row["bridge_component_id"].as<std::string>();
        entry.component_number = row["component_number"].as<std::string>();
        entry.site_name = row["site_name"].as<std::string>();
        entry.site_component_type = row["site_component_type"].as<std::string>();
        if (!row["span_or_location"].isNull())
            entry.span_or_location = row["span_or_location"].as<std::string>();
        entry.is_active = row["is_active"].as<bool>();
        if (!row["deactivated_at"].isNull())
            entry.deactivated_at = row["deactivated_at"].as<std::string>();
        if (!row["deactivation_reason"].isNull())
            entry.deactivation_reason = row["deactivation_reason"].as<std::string>();
        entry.sort_order = row["sort_order"].as<int>();
        if (!row["remarks"].isNull()) entry.remarks = row["remarks"].as<std::string>();
        entry.is_referenced = row["is_referenced"].as<bool>();
        located.position = row["position"].as<std::int64_t>();
        index_by_id.emplace(entry.id, lookup.entries.size());
        page_ids.push_back(entry.id);
        lookup.entries.push_back(std::move(located));
    }
    if (lookup.entries.empty()) return lookup;

    // 只返回生效映射。现有的全量装配不过滤，会把历次改动积累的失效映射一并带出，
    // 单页体积随之不可控；而前端所有消费点都只读生效映射。
    // 这段 CTE 的 select 列表此前漏了 is_active，谓词一旦引用它就必然出事。
    // 三处共用同一个 scoped.extra_columns，正是为了不再各写各的。
    const std::string mapping_sql =
        "with scoped as ("
        "select e.id,e.component_number,e.site_component_type,e.site_name,"
        "e.is_active,e.sort_order," + scoped.extra_columns +
        entry_position_window_sql() + " as position "
        "from bridge_component_inventory_entries e "
        "where e.inventory_revision_id=$1::uuid"
        "),page_rows as ("
        "select s.id from scoped s where " + scoped.predicate +
        " order by s.sort_order,s.id offset $3 limit $4"
        ") "
        "select m.inventory_entry_id::text,m.id::text,m.standard_package_id::text,"
        "m.standard_bridge_type_id,m.standard_component_category_id,m.structure_part,"
        "m.mapping_source,m.confirmation_status,m.is_active "
        "from bridge_component_standard_mappings m "
        "join page_rows pr on pr.id=m.inventory_entry_id "
        "where m.is_active order by m.inventory_entry_id,m.created_at,m.id";
    const auto mapping_rows =
        executor->execSqlSync(mapping_sql, revision_id, scope_value, offset, limit);
    for (const auto& row : mapping_rows) {
        const auto entry_id = row["inventory_entry_id"].as<std::string>();
        const auto found = index_by_id.find(entry_id);
        if (found == index_by_id.end()) continue;
        inventory::InventoryMapping mapping;
        mapping.id = row["id"].as<std::string>();
        mapping.standard_package_id = row["standard_package_id"].as<std::string>();
        mapping.standard_bridge_type_id = row["standard_bridge_type_id"].as<std::string>();
        mapping.standard_component_category_id =
            row["standard_component_category_id"].as<std::string>();
        mapping.structure_part = row["structure_part"].as<std::string>();
        mapping.mapping_source = row["mapping_source"].as<std::string>();
        mapping.confirmation_status = row["confirmation_status"].as<std::string>();
        mapping.is_active = row["is_active"].as<bool>();
        lookup.entries[found->second].entry.mappings.push_back(std::move(mapping));
    }
    return lookup;
}

std::optional<inventory::LocatedInventoryEntry> ComponentInventoryRepository::load_entry_with(
    const drogon::orm::DbClientPtr& executor,
    const std::string& revision_id,
    const std::string& entry_id) {
    auto lookup = load_entry_page(executor, revision_id, EntryQuery{entry_id}, 0, 1);
    if (lookup.entries.empty()) return std::nullopt;
    return std::move(lookup.entries.front());
}

ComponentInventoryRepository::EntryLookup ComponentInventoryRepository::load_group_entries(
    const std::string& revision_id,
    const std::string& site_component_type,
    std::int64_t offset,
    std::int64_t limit) const {
    return load_entry_page(db_client_, revision_id,
                           EntryQuery{"", site_component_type}, offset, limit);
}

ComponentInventoryRepository::EntryLookup ComponentInventoryRepository::search_entries(
    const std::string& revision_id,
    const std::string& keyword,
    bool binding_eligible,
    std::int64_t limit) const {
    return load_entry_page(db_client_, revision_id,
                           EntryQuery{"", "", keyword, binding_eligible}, 0, limit);
}

// 绑定概览只要这几十个 id 的展示信息。装配整份修订版再从里面挑，正是这次要去掉的形态。
std::vector<inventory::InventoryEntry>
ComponentInventoryRepository::load_bindable_entries_by_component_ids(
    const std::string& revision_id,
    const std::vector<std::string>& bridge_component_ids) const {
    std::vector<inventory::InventoryEntry> entries;
    if (bridge_component_ids.empty()) return entries;
    // 过滤条件与 binding_eligible 一致：启用且至少有一个生效映射。
    const auto rows = db_client_->execSqlSync(
        "select e.id::text,e.bridge_component_id::text,e.component_number,"
        "e.site_name,e.site_component_type "
        "from bridge_component_inventory_entries e "
        "where e.inventory_revision_id=$1::uuid "
        "and e.bridge_component_id = any($2::uuid[]) and e.is_active "
        "and exists(select 1 from bridge_component_standard_mappings m "
        "where m.inventory_entry_id=e.id and m.is_active)",
        revision_id, pg_uuid_array(bridge_component_ids));
    for (const auto& row : rows) {
        inventory::InventoryEntry entry;
        entry.id = row["id"].as<std::string>();
        entry.bridge_component_id = row["bridge_component_id"].as<std::string>();
        entry.component_number = row["component_number"].as<std::string>();
        entry.site_name = row["site_name"].as<std::string>();
        entry.site_component_type = row["site_component_type"].as<std::string>();
        entries.push_back(std::move(entry));
    }
    return entries;
}

std::vector<inventory::InventoryEntry>
ComponentInventoryRepository::load_bindable_entries_by_categories(
    const std::string& revision_id,
    const std::vector<std::string>& standard_component_category_ids) const {
    std::vector<inventory::InventoryEntry> entries;
    if (standard_component_category_ids.empty()) return entries;
    // 过滤条件与 binding_eligible 一致：启用 + 有生效映射；再按类别收窄。
    const auto rows = db_client_->execSqlSync(
        "select e.id::text,e.bridge_component_id::text,e.component_number,"
        "e.site_name,e.site_component_type,m.id::text as mapping_id,"
        "m.standard_component_category_id,m.structure_part "
        "from bridge_component_inventory_entries e "
        "join bridge_component_standard_mappings m on m.inventory_entry_id=e.id and m.is_active "
        "where e.inventory_revision_id=$1::uuid and e.is_active "
        "and m.standard_component_category_id = any($2::text[]) "
        "order by e.sort_order,e.id",
        revision_id, pg_text_array(standard_component_category_ids));
    for (const auto& row : rows) {
        inventory::InventoryMapping mapping;
        mapping.id = row["mapping_id"].as<std::string>();
        mapping.standard_component_category_id =
            row["standard_component_category_id"].as<std::string>();
        mapping.structure_part = row["structure_part"].as<std::string>();
        mapping.is_active = true;
        inventory::InventoryEntry entry;
        entry.id = row["id"].as<std::string>();
        entry.bridge_component_id = row["bridge_component_id"].as<std::string>();
        entry.component_number = row["component_number"].as<std::string>();
        entry.site_name = row["site_name"].as<std::string>();
        entry.site_component_type = row["site_component_type"].as<std::string>();
        entry.mappings.push_back(std::move(mapping));
        entries.push_back(std::move(entry));
    }
    return entries;
}

std::vector<ComponentInventoryRepository::ReplaceEntry>
ComponentInventoryRepository::load_bindable_replace_entries(
    const std::string& revision_id) const {
    // is_active 恒为 true（下面已按它过滤），仍然要返回：前端的预览算法里有一句
    // if (!entry.is_active) continue，字段缺失时 !undefined 为真，会把每一条都跳过，
    // 预览安静地全判成"台账里没有"。保留字段比让两边互相假设对方过滤过更稳。
    const auto rows = db_client_->execSqlSync(
        "select e.bridge_component_id::text,e.component_number "
        "from bridge_component_inventory_entries e "
        "where e.inventory_revision_id=$1::uuid and e.is_active "
        "and exists(select 1 from bridge_component_standard_mappings m "
        "where m.inventory_entry_id=e.id and m.is_active) "
        "order by e.sort_order,e.id",
        revision_id);
    std::vector<ReplaceEntry> entries;
    entries.reserve(rows.size());
    for (const auto& row : rows) {
        entries.push_back({row["bridge_component_id"].as<std::string>(),
                           row["component_number"].as<std::string>(), true});
    }
    return entries;
}

std::optional<ComponentInventoryRepository::ConfirmedRevisionRef>
ComponentInventoryRepository::resolve_confirmed_revision_ref(
    const std::string& bridge_id,
    const std::optional<std::string>& locked_revision_id) const {
    // 锁定版本是外部传进来的，所以这里连同"属于本桥"和"确实已确认"一起判掉；
    // 未锁定时才轮到"该桥最新已确认"这条规则。
    const auto rows = locked_revision_id.has_value()
        ? db_client_->execSqlSync(
              "select id::text,bridge_id::text from bridge_component_inventory_revisions "
              "where id=$1::uuid and bridge_id=$2::uuid "
              "and status in ('已确认','confirmed')",
              *locked_revision_id, bridge_id)
        : db_client_->execSqlSync(
              "select id::text,bridge_id::text from bridge_component_inventory_revisions "
              "where bridge_id=$1::uuid and status in ('已确认','confirmed') "
              "order by revision_number desc limit 1",
              bridge_id);
    if (rows.empty()) return std::nullopt;
    return ConfirmedRevisionRef{rows[0]["id"].as<std::string>(),
                                rows[0]["bridge_id"].as<std::string>()};
}

std::vector<ReviewOrderedComponent> ComponentInventoryRepository::order_components_for_review(
    const std::string& bridge_id,
    const std::optional<std::string>& locked_revision_id,
    const std::vector<std::string>& bridge_component_ids) const {
    // 精简装配本来就带着类别、结构部位和 sort_order，排序不必另取一次数。
    const auto revision = resolve_confirmed_revision_for_components(
        bridge_id, locked_revision_id, bridge_component_ids);
    if (!revision.has_value()) return {};

    struct Ranked {
        int rank{0};
        int sort_order{0};
        ReviewOrderedComponent component;
    };
    std::vector<Ranked> ranked;
    ranked.reserve(revision->entries.size());
    for (const auto& entry : revision->entries) {
        const auto* mapping = inventory::active_inventory_mapping(entry);
        if (mapping == nullptr) continue;  // 没有生效映射就定不了部件，排不进来
        ranked.push_back({
            inventory::component_review_rank(
                mapping->structure_part, mapping->standard_component_category_id),
            entry.sort_order,
            {entry.bridge_component_id, entry.site_component_type},
        });
    }
    std::sort(ranked.begin(), ranked.end(), [](const Ranked& left, const Ranked& right) {
        // component_id 兜底只为让结果稳定：同部件同 sort_order 时次序不该随查询漂。
        return std::tie(left.rank, left.sort_order, left.component.bridge_component_id)
             < std::tie(right.rank, right.sort_order, right.component.bridge_component_id);
    });

    std::vector<ReviewOrderedComponent> ordered;
    ordered.reserve(ranked.size());
    for (auto& item : ranked) ordered.push_back(std::move(item.component));
    return ordered;
}

std::optional<inventory::InventoryRevision>
ComponentInventoryRepository::resolve_confirmed_revision_for_components(
    const std::string& bridge_id,
    const std::optional<std::string>& locked_revision_id,
    const std::vector<std::string>& bridge_component_ids) const {
    // 版本解析规则与完整装配共用同一份，不另开一条。
    const auto ref = resolve_confirmed_revision_ref(bridge_id, locked_revision_id);
    if (!ref.has_value()) return std::nullopt;

    const auto revisions = db_client_->execSqlSync(
        "select id::text,bridge_id::text,revision_number,status,baseline_revision_id::text,"
        "confirmed_at::text from bridge_component_inventory_revisions where id=$1::uuid",
        ref->id);
    if (revisions.empty()) return std::nullopt;
    inventory::InventoryRevision revision;
    revision.id = revisions[0]["id"].as<std::string>();
    revision.bridge_id = revisions[0]["bridge_id"].as<std::string>();
    revision.revision_number = revisions[0]["revision_number"].as<int>();
    revision.status = revisions[0]["status"].as<std::string>();
    revision.baseline_revision_id = optional_text(revisions[0]["baseline_revision_id"]);
    revision.confirmed_at = optional_text(revisions[0]["confirmed_at"]);
    if (bridge_component_ids.empty()) return revision;

    const auto id_array = pg_uuid_array(bridge_component_ids);
    // 与完整装配的差别就在这条 SQL：没有 is_referenced 那四个 exists 子查询，
    // 也只取点名的构件。
    const auto entries = db_client_->execSqlSync(
        "select e.id::text,e.bridge_component_id::text,e.component_number,e.site_name,"
        "e.site_component_type,e.span_or_location,e.is_active,e.deactivated_at::text,"
        "e.deactivation_reason,e.sort_order,e.remarks "
        "from bridge_component_inventory_entries e "
        "where e.inventory_revision_id=$1::uuid "
        "and e.bridge_component_id = any($2::uuid[]) "
        "order by e.sort_order,e.id",
        ref->id, id_array);

    std::unordered_map<std::string, std::vector<inventory::InventoryMapping>> mappings_by_entry;
    const auto mapping_rows = db_client_->execSqlSync(
        "select m.id::text,m.inventory_entry_id::text,m.standard_package_id::text,"
        "m.standard_bridge_type_id,m.standard_component_category_id,m.structure_part,"
        "m.mapping_source,m.confirmation_status,m.is_active "
        "from bridge_component_standard_mappings m "
        "join bridge_component_inventory_entries e on e.id=m.inventory_entry_id "
        "where e.inventory_revision_id=$1::uuid "
        "and e.bridge_component_id = any($2::uuid[]) "
        // 排序与完整装配一致，各构件内的映射顺序因此不变。
        "order by m.inventory_entry_id,m.is_active desc,m.created_at,m.id",
        ref->id, id_array);
    for (const auto& mapping_row : mapping_rows) {
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
        mappings_by_entry[mapping_row["inventory_entry_id"].as<std::string>()]
            .push_back(std::move(mapping));
    }

    revision.entries.reserve(entries.size());
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
        // is_referenced 不在这条路径上计算，保持默认的 false（见头文件说明）。
        if (const auto found = mappings_by_entry.find(entry.id);
            found != mappings_by_entry.end()) {
            entry.mappings = std::move(found->second);
        }
        revision.entries.push_back(std::move(entry));
    }
    return revision;
}

std::optional<inventory::InventoryRevision>
ComponentInventoryRepository::resolve_confirmed_revision(
    const std::string& bridge_id,
    const std::optional<std::string>& locked_revision_id) const {
    // 规则只有上面那一份；这里只负责把解析出来的版本完整装配出来。
    const auto ref = resolve_confirmed_revision_ref(bridge_id, locked_revision_id);
    if (!ref.has_value()) return std::nullopt;
    return get_revision(ref->id);
}

bool ComponentInventoryRepository::lock_pending_year_revision(
    const std::optional<std::string>& year_id,
    const std::string& bridge_id,
    const std::optional<std::string>& locked_revision_id,
    const std::string& revision_id) const {
    if (locked_revision_id.has_value()) return *locked_revision_id == revision_id;
    if (!year_id.has_value()) return true;
    const auto updated = db_client_->execSqlSync(
        "update inspection_years "
        "set component_inventory_revision_id=$2::uuid,updated_at=now() "
        "where id=$1::uuid and bridge_id=$3::uuid and status='待校对' "
        "and component_inventory_revision_id is null returning id",
        *year_id, revision_id, bridge_id);
    if (!updated.empty()) return true;
    // 没更新到：要么并发抢先锁了，要么年度不在待校对。只有抢到的正好是同一个版本才放行。
    const auto current = db_client_->execSqlSync(
        "select component_inventory_revision_id::text as revision_id "
        "from inspection_years where id=$1::uuid and bridge_id=$2::uuid",
        *year_id, bridge_id);
    return !current.empty() && !current[0]["revision_id"].isNull()
        && current[0]["revision_id"].as<std::string>() == revision_id;
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
        // 目录路径没有版本化模板，用哨兵标记生成方式（DB 溯源列要求非空）。
        const std::string template_marker =
            input.template_id.empty() ? std::string("part-catalog") : input.template_id;
        const auto batch = tx->execSqlSync(
            "insert into bridge_component_generation_batches "
            "(bridge_id,template_standard_package_id,template_id,bridge_type_code,"
            "input_quantities,generated_by_user_id) "
            "values($1::uuid,$2::uuid,$3,$4,$5::jsonb,$6::uuid) returning id::text",
            bridge_id, input.standard_package_id, template_marker, input.bridge_type_id,
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
        // 刚往三张表里灌了几千行，而计划器的统计信息还停在"接近空表"。下面那句汇总
        // 会据此选出灾难性的计划：实测 5126 个构件时它要 18.6 秒，ANALYZE 之后同一条
        // 查询只要 42 毫秒（443 倍）。而 DbClient 的单语句超时是 10 秒，于是整个生成
        // 以 SQL execution timeout 失败——界面上表现为"构件台账写入失败"，重试永远无解，
        // 因为每次重试都从同样的空统计开始。
        //
        // autovacuum 也会做这件事，但要等到下一轮；汇总就在同一个事务里、紧接着跑，
        // 等不到。ANALYZE 可以在事务内执行，其结果对本事务后续语句立即可见。
        if (generated.size() >= 500) {
            tx->execSqlSync("analyze bridge_components");
            tx->execSqlSync("analyze bridge_component_inventory_entries");
            tx->execSqlSync("analyze bridge_component_standard_mappings");
        }
        auto summary = load_summary_with(tx, revision_id);
        auto outcome = finish(tx, latch, revision_id);
        if (outcome.status == ComponentInventoryStatus::Ok) outcome.summary = std::move(summary);
        return outcome;
    } catch (const std::exception& error) {
        // 原来是 catch (...) 且一个字都不记，失败最终收口成 HTTP 503"数据库暂不可用"——
        // 而真实原因往往是约束冲突。定位只能靠在库里手工重放，代价极高。
        if (tx) { try { tx->rollback(); } catch (...) {} }
        LOG_ERROR << "component inventory write failed detail=" << error.what();
        return {ComponentInventoryStatus::Failed};
    } catch (...) {
        if (tx) { try { tx->rollback(); } catch (...) {} }
        LOG_ERROR << "component inventory write failed detail=<non-standard exception>";
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
        if (target.superseded) { tx->rollback(); return {ComponentInventoryStatus::Superseded}; }
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
        // 汇总在提交前的同一个事务里算出，提交确认后才返回。放到事务外重读的话，
        // 提交与重读之间的并发写会混进来，响应就不再是这次写入的结果。
        auto summary = load_summary_with(tx, target.revision_id);
        auto changed = load_entry_with(tx, target.revision_id, target.entry_id);
        auto outcome = finish(tx, latch, target.revision_id, target.entry_id);
        if (outcome.status == ComponentInventoryStatus::Ok) {
            outcome.summary = std::move(summary);
            outcome.entry = std::move(changed);
        }
        return outcome;
    } catch (const std::exception& error) {
        // 原来是 catch (...) 且一个字都不记，失败最终收口成 HTTP 503"数据库暂不可用"——
        // 而真实原因往往是约束冲突。定位只能靠在库里手工重放，代价极高。
        if (tx) { try { tx->rollback(); } catch (...) {} }
        LOG_ERROR << "component inventory write failed detail=" << error.what();
        return {ComponentInventoryStatus::Failed};
    } catch (...) {
        if (tx) { try { tx->rollback(); } catch (...) {} }
        LOG_ERROR << "component inventory write failed detail=<non-standard exception>";
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
        if (target.superseded) { tx->rollback(); return {ComponentInventoryStatus::Superseded}; }
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
        // 汇总在提交前的同一个事务里算出，提交确认后才返回。放到事务外重读的话，
        // 提交与重读之间的并发写会混进来，响应就不再是这次写入的结果。
        auto summary = load_summary_with(tx, target.revision_id);
        auto changed = load_entry_with(tx, target.revision_id, new_entry_id);
        auto outcome = finish(tx, latch, target.revision_id, new_entry_id);
        if (outcome.status == ComponentInventoryStatus::Ok) {
            outcome.summary = std::move(summary);
            outcome.entry = std::move(changed);
        }
        return outcome;
    } catch (const std::exception& error) {
        // 原来是 catch (...) 且一个字都不记，失败最终收口成 HTTP 503"数据库暂不可用"——
        // 而真实原因往往是约束冲突。定位只能靠在库里手工重放，代价极高。
        if (tx) { try { tx->rollback(); } catch (...) {} }
        LOG_ERROR << "component inventory write failed detail=" << error.what();
        return {ComponentInventoryStatus::Failed};
    } catch (...) {
        if (tx) { try { tx->rollback(); } catch (...) {} }
        LOG_ERROR << "component inventory write failed detail=<non-standard exception>";
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
        if (target.superseded) { tx->rollback(); return {ComponentInventoryStatus::Superseded}; }
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
        // 汇总在提交前的同一个事务里算出，提交确认后才返回。放到事务外重读的话，
        // 提交与重读之间的并发写会混进来，响应就不再是这次写入的结果。
        auto summary = load_summary_with(tx, target.revision_id);
        auto outcome = finish(tx, latch, target.revision_id);
        if (outcome.status == ComponentInventoryStatus::Ok)
            outcome.summary = std::move(summary);
        return outcome;
    } catch (const std::exception& error) {
        // 原来是 catch (...) 且一个字都不记，失败最终收口成 HTTP 503"数据库暂不可用"——
        // 而真实原因往往是约束冲突。定位只能靠在库里手工重放，代价极高。
        if (tx) { try { tx->rollback(); } catch (...) {} }
        LOG_ERROR << "component inventory write failed detail=" << error.what();
        return {ComponentInventoryStatus::Failed};
    } catch (...) {
        if (tx) { try { tx->rollback(); } catch (...) {} }
        LOG_ERROR << "component inventory write failed detail=<non-standard exception>";
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
        if (target.superseded) { tx->rollback(); return {ComponentInventoryStatus::Superseded}; }
        if (!target.found) { tx->rollback(); return {ComponentInventoryStatus::NotFound}; }
        tx->execSqlSync(
            "update bridge_component_inventory_entries set is_active=false,deactivated_at=now(),"
            "deactivation_reason=$1,updated_at=now() where id=$2::uuid",
            reason, target.entry_id);
        // 汇总在提交前的同一个事务里算出，提交确认后才返回。放到事务外重读的话，
        // 提交与重读之间的并发写会混进来，响应就不再是这次写入的结果。
        auto summary = load_summary_with(tx, target.revision_id);
        auto changed = load_entry_with(tx, target.revision_id, target.entry_id);
        auto outcome = finish(tx, latch, target.revision_id, target.entry_id);
        if (outcome.status == ComponentInventoryStatus::Ok) {
            outcome.summary = std::move(summary);
            outcome.entry = std::move(changed);
        }
        return outcome;
    } catch (const std::exception& error) {
        // 原来是 catch (...) 且一个字都不记，失败最终收口成 HTTP 503"数据库暂不可用"——
        // 而真实原因往往是约束冲突。定位只能靠在库里手工重放，代价极高。
        if (tx) { try { tx->rollback(); } catch (...) {} }
        LOG_ERROR << "component inventory write failed detail=" << error.what();
        return {ComponentInventoryStatus::Failed};
    } catch (...) {
        if (tx) { try { tx->rollback(); } catch (...) {} }
        LOG_ERROR << "component inventory write failed detail=<non-standard exception>";
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
        if (target.superseded) { tx->rollback(); return {ComponentInventoryStatus::Superseded}; }
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
        // 汇总在提交前的同一个事务里算出，提交确认后才返回。放到事务外重读的话，
        // 提交与重读之间的并发写会混进来，响应就不再是这次写入的结果。
        auto summary = load_summary_with(tx, target.revision_id);
        auto changed = load_entry_with(tx, target.revision_id, target.entry_id);
        auto outcome = finish(tx, latch, target.revision_id, target.entry_id);
        if (outcome.status == ComponentInventoryStatus::Ok) {
            outcome.summary = std::move(summary);
            outcome.entry = std::move(changed);
        }
        return outcome;
    } catch (const std::exception& error) {
        // 原来是 catch (...) 且一个字都不记，失败最终收口成 HTTP 503"数据库暂不可用"——
        // 而真实原因往往是约束冲突。定位只能靠在库里手工重放，代价极高。
        if (tx) { try { tx->rollback(); } catch (...) {} }
        LOG_ERROR << "component inventory write failed detail=" << error.what();
        return {ComponentInventoryStatus::Failed};
    } catch (...) {
        if (tx) { try { tx->rollback(); } catch (...) {} }
        LOG_ERROR << "component inventory write failed detail=<non-standard exception>";
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
        // 汇总在提交前的同一个事务里算出，提交确认后才返回。放到事务外重读的话，
        // 提交与重读之间的并发写会混进来，响应就不再是这次写入的结果。
        auto summary = load_summary_with(tx, revision_id);
        auto outcome = finish(tx, latch, revision_id);
        if (outcome.status == ComponentInventoryStatus::Ok)
            outcome.summary = std::move(summary);
        return outcome;
    } catch (const std::exception& error) {
        // 原来是 catch (...) 且一个字都不记，失败最终收口成 HTTP 503"数据库暂不可用"——
        // 而真实原因往往是约束冲突。定位只能靠在库里手工重放，代价极高。
        if (tx) { try { tx->rollback(); } catch (...) {} }
        LOG_ERROR << "component inventory write failed detail=" << error.what();
        return {ComponentInventoryStatus::Failed};
    } catch (...) {
        if (tx) { try { tx->rollback(); } catch (...) {} }
        LOG_ERROR << "component inventory write failed detail=<non-standard exception>";
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
        // 左连接一个恒真条件，是为了"没有任何未确认构件"时也能拿到 active_count 那一行；
        // 否则空结果集里读不出启用构件数，判不了 inventory_empty。
        const auto rows = tx->execSqlSync(
            "with " + blocker_cte_sql() + " "
            "select a.value as active_count,u.entry_id,u.component_number "
            "from inventory_active_entries a "
            "left join inventory_unconfirmed_entries u on true "
            "order by u.sort_order nulls first,u.entry_id",
            revision_id);
        if (rows[0]["active_count"].as<int>() == 0) {
            blockers.push_back({"inventory_empty", "inventory_revision", revision_id,
                                "entries", "构件台账至少需要一个启用构件。"});
        }
        for (const auto& row : rows) {
            if (row["entry_id"].isNull()) continue;
            blockers.push_back({
                "component_mapping_required", "inventory_entry",
                row["entry_id"].as<std::string>(), "mappings",
                "构件 " + row["component_number"].as<std::string>() +
                    " 至少需要一个已确认的有效规范映射。"});
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
        // 汇总在提交前的同一个事务里算出，提交确认后才返回。放到事务外重读的话，
        // 提交与重读之间的并发写会混进来，响应就不再是这次写入的结果。
        auto summary = load_summary_with(tx, revision_id);
        auto outcome = finish(tx, latch, revision_id);
        if (outcome.status == ComponentInventoryStatus::Ok)
            outcome.summary = std::move(summary);
        return outcome;
    } catch (const std::exception& error) {
        // 原来是 catch (...) 且一个字都不记，失败最终收口成 HTTP 503"数据库暂不可用"——
        // 而真实原因往往是约束冲突。定位只能靠在库里手工重放，代价极高。
        if (tx) { try { tx->rollback(); } catch (...) {} }
        LOG_ERROR << "component inventory write failed detail=" << error.what();
        return {ComponentInventoryStatus::Failed};
    } catch (...) {
        if (tx) { try { tx->rollback(); } catch (...) {} }
        LOG_ERROR << "component inventory write failed detail=<non-standard exception>";
        return {ComponentInventoryStatus::Failed};
    }
}

}  // namespace bridge_report::db
