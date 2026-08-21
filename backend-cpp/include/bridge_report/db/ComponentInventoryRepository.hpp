#pragma once

#include <optional>
#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>

#include "bridge_report/inventory/ComponentInventoryGenerator.hpp"

namespace bridge_report::db {

enum class ComponentInventoryStatus {
    Ok,
    NotFound,
    Invalid,
    Conflict,
    Referenced,
    Blocked,
    // 客户端拿着的已确认版本已被别的草稿取代：桥上存在基于另一版本的草稿，
    // 再派生一条会让一桥出现两条草稿分支。与 Conflict 分开，是因为前端要据此
    // 重新拉取台账并采纳新的修订版 id，而不是只弹一句"已变化"。
    Superseded,
    Failed,
};

struct InventoryEntryUpdate {
    std::string component_number;
    std::string site_name;
    std::string site_component_type;
    std::optional<std::string> span_or_location;
    std::optional<std::string> remarks;
};

struct InventoryNewEntry : InventoryEntryUpdate {
    int sort_order{0};
};

struct InventoryMappingUpdate {
    std::string standard_package_id;
    std::string standard_bridge_type_id;
    std::string standard_component_category_id;
    std::string structure_part;
    std::string mapping_source{"人工选择"};
};

struct ComponentInventoryOutcome {
    ComponentInventoryStatus status{ComponentInventoryStatus::Failed};
    // 写入后的分组汇总，在提交前的同一个事务里算出，提交确认后才返回。
    std::optional<Json::Value> summary;
    // 被改动的那一条构件（新增 / 修改 / 停用 / 设映射时有；删除、批量确认、
    // 确认台账、生成台账没有）。同样在事务内取，事务外按 id 重读会破坏同快照。
    std::optional<inventory::LocatedInventoryEntry> entry;
    std::optional<std::string> entry_id;
    std::vector<inventory::InventoryBlocker> blockers;
};

// 走查顺序里的一个构件：id + 它属于哪个部件。
//
// 部件名取台账的 site_component_type（"板""铰缝""支座"…）。它是向导里的现场名，
// 同一个部件在别的桥可能叫别的名字——所以只拿它做**显示与分组**，排序仍由后端按
// 规范类别与目录次序定，不靠这个字符串。
struct ReviewOrderedComponent {
    std::string bridge_component_id;
    std::string part_name;
};

class ComponentInventoryRepository {
public:
    explicit ComponentInventoryRepository(drogon::orm::DbClientPtr db_client);

    std::optional<inventory::InventoryRevision> get_revision(const std::string& revision_id) const;

    // 只解析出最新修订版的 id，不装配构件与映射。台账管理页专用：这里的"最新"是
    // **草稿优先**，管理页必须能看见自己刚派生的草稿。任何要判断"是否已确认"的
    // 调用方都不能用它，走下面的 resolve_confirmed_revision*()。
    //
    // 汇总端点必须走这条而不是先取整份修订版：get_revision() 会把全部构件连同映射
    // 装配一遍；复用它的话响应体虽小，后端仍完整跑一次全量装配，优化只做了一半，
    // 而且从响应上完全看不出来。
    std::optional<std::string> find_latest_revision_id(const std::string& bridge_id) const;

    // 版本解析的结果引用：只有 id 与所属桥梁，不含构件。
    struct ConfirmedRevisionRef {
        std::string id;
        std::string bridge_id;
    };

    // 解析某次操作应当使用的已确认台账版本：检测年度锁定的版本优先，年度未锁定时取该桥
    // 最新的已确认版本。绑定与范围拆分都必须走这里——各自实现一份的话规则迟早漂移，
    // 范围拆分此前就是这么绕开年度锁定版本、又踩上草稿优先排序的。
    //
    // 只需要版本 id 的调用方（概览、批量取数、标记缺失/清除、评定树绑定）用这一条，
    // 装配整份台账再从里面取一个 id 是纯粹的浪费。
    std::optional<ConfirmedRevisionRef> resolve_confirmed_revision_ref(
        const std::string& bridge_id,
        const std::optional<std::string>& locked_revision_id) const;

    // 需要完整台账做 validate_target() 或范围分析的调用方用这一条。
    // 内部先走 resolve_confirmed_revision_ref()，两者共用同一套版本解析规则。
    /**
     * @brief 把点名的构件按病害校对的走查顺序排好，返回排序后的构件 id。
     *
     * 顺序 = 结构部位（上部 → 下部 → 桥面系）→ 部件在目录中的位置 → 台账 sort_order。
     * 最后那一级不能省：墩柱与盖梁、台与台帽、锥坡与护坡各自共用一个 H21 类别，
     * 只有台账的生成次序分得开它们。
     *
     * 规则整体住在后端，前端只按返回的次序摆行——同一份顺序将来若要给别的视图用，
     * 不必在两处各维护一份。传入的构件若不在台账里，直接不出现在结果中。
     */
    [[nodiscard]] std::vector<ReviewOrderedComponent> order_components_for_review(
        const std::string& bridge_id,
        const std::optional<std::string>& locked_revision_id,
        const std::vector<std::string>& bridge_component_ids) const;

    /**
     * @brief 按同一条解析规则取已确认版本，但**只装配指定构件**。
     *
     * 专供"按 bridge_component_id 查规范类别"这一种用途（病害评定树匹配）。与完整的
     * resolve_confirmed_revision 有两处刻意的减法，都是为了不给一次匹配付整份台账的钱：
     *
     *   1. 只取传入的那些构件。匹配是逐条病害按 id 找条目，用不到其余构件；
     *      5174 条的桥上，362 条病害最多只涉及几百条。
     *   2. **不计算 is_referenced**。那个字段每条构件要跑 4 个 exists 子查询，是完整
     *      装配里最贵的一块，而匹配从不读它。返回的条目上它一律是默认值 false。
     *
     * 正因为如此，返回的版本**不是完整的台账**：条目被筛过、is_referenced 不可信。
     * 需要完整台账的调用方必须用 resolve_confirmed_revision。
     */
    [[nodiscard]] std::optional<inventory::InventoryRevision>
    resolve_confirmed_revision_for_components(
        const std::string& bridge_id,
        const std::optional<std::string>& locked_revision_id,
        const std::vector<std::string>& bridge_component_ids) const;

    std::optional<inventory::InventoryRevision> resolve_confirmed_revision(
        const std::string& bridge_id,
        const std::optional<std::string>& locked_revision_id) const;

    // 把版本锁进待校对年度。写操作专用：读操作绝不能调它，否则光是打开一个对话框
    // 就会改掉年度状态。年度已锁定时只做一致性确认，不覆盖。
    // 返回 false 表示年度已经锁在别的版本上，调用方应当整体回滚。
    bool lock_pending_year_revision(
        const std::optional<std::string>& year_id,
        const std::string& bridge_id,
        const std::optional<std::string>& locked_revision_id,
        const std::string& revision_id) const;

    // 分组汇总。整份结果由一条语句产出，靠单语句快照保证 revision / groups / blockers
    // 三段来自同一时点——拆成多条查询时，默认的 READ COMMITTED 会让每条 SELECT 各取
    // 一个新快照，出现"blocker 报未映射构件但 unmapped_count 全为 0"这类自相矛盾。
    //
    // 返回的是最终响应形状的 JSON 而不是结构体：既然快照要求逼出了单语句，
    // 异构的三段就只能在 SQL 里用 json_build_object 拼；再解析回结构体又序列化回去
    // 是纯粹的往返开销。代价是响应形状落在了 SQL 里，改形状要改 SQL。
    std::optional<Json::Value> load_summary(const std::string& revision_id) const;

    // 分组分页与编号搜索共用的返回形状。page / size 由路由按请求回显，不在这里存。
    struct EntryLookup {
        std::int64_t total{0};
        std::vector<inventory::LocatedInventoryEntry> entries;
    };

    // 某一类别的构件，按 (sort_order, id) 分页。含停用构件——它们在分组弹窗里
    // 仍然可见并占位，过滤掉会让页码与"定位"对不上。
    EntryLookup load_group_entries(
        const std::string& revision_id,
        const std::string& site_component_type,
        std::int64_t offset,
        std::int64_t limit) const;

    // 关键词子串检索，语义与前端原来的 String.includes 一致（搜 3-5 也会命中 13-5#梁），
    // 匹配编号、构件类别与现场名称三个字段。
    // total 是未截断的命中数，界面上"匹配 N 个构件，显示前 M 个"依赖它。
    //
    // binding_eligible：只返回启用且至少有一个生效映射的构件。这是"台账层面可供选择"，
    // **不等于**对某一行可绑——部件名与规范类别的兼容性仍由 validate_target() 在正式
    // 绑定时判定。台账管理页不传它，仍能看见停用与未映射构件。
    EntryLookup search_entries(
        const std::string& revision_id,
        const std::string& keyword,
        bool binding_eligible,
        std::int64_t limit) const;

    // 供绑定概览按 id 定向取展示信息：查的是这几十个 id，不是整份台账。
    // 过滤条件与 binding_eligible 完全一致，以复现旧前端 usableEntries() 的可见范围。
    std::vector<inventory::InventoryEntry> load_bindable_entries_by_component_ids(
        const std::string& revision_id,
        const std::vector<std::string>& bridge_component_ids) const;

    // 供侧别配对按部件类别定向取构件：放行名单只有两个类别，一座桥至多几件，
    // 不会退化成加载整份台账（大桥有五千多件）。
    // 与上面不同，这里**会填上生效映射**——配对判定要按类别过滤并核对件数，
    // 缺了映射就无法与匹配共用同一条"可用构件"口径。
    std::vector<inventory::InventoryEntry> load_bindable_entries_by_categories(
        const std::string& revision_id,
        const std::vector<std::string>& standard_component_category_ids) const;

    // 批量替换预览用的精简条目：只有 bridge_component_id / component_number / is_active。
    struct ReplaceEntry {
        std::string bridge_component_id;
        std::string component_number;
        bool is_active{true};
    };
    std::vector<ReplaceEntry> load_bindable_replace_entries(
        const std::string& revision_id) const;

private:
    // executor 传连接就是普通只读路径，传当前事务就是写路径——同一段 SQL 复用，
    // 不产生嵌套事务，也不会出现两套规则。
    static std::optional<Json::Value> load_summary_with(
        const drogon::orm::DbClientPtr& executor,
        const std::string& revision_id);

    // 分组分页与关键词检索的共同取数条件。
    struct EntryQuery {
        // 三选一：按 id 定位、按类别分组、或按关键词检索。都为空表示取该版本全部构件。
        std::string entry_id;
        std::string site_component_type;
        std::string keyword;
        bool binding_eligible{false};
    };

    // 分组分页与关键词检索的共同实现。
    //
    // 三条语句（count / 分页 / 映射装配）都从同名 CTE scoped 取数、统一别名 s，
    // 过滤条件只生成一次。此前是把一段谓词文本分别拼进三处，而三处别名并不相同
    // （实表 e、两个 CTE 各自的 p），且映射查询那个 CTE 连 is_active 都没 select
    // 出来；带别名的谓词在那里直接报错，不带别名的裸 id 更糟——它会就近解析成
    // 子查询里的 m.id，条件恒假，搜索静默返回零行。
    //
    // 不变量：**谓词引用到的每一列，三处 CTE 都必须 select 出来。**
    static EntryLookup load_entry_page(
        const drogon::orm::DbClientPtr& executor,
        const std::string& revision_id,
        const EntryQuery& query,
        std::int64_t offset,
        std::int64_t limit);

    // 单条构件，连同它的组内序号。写响应用它，取自写入所在的那个事务。
    static std::optional<inventory::LocatedInventoryEntry> load_entry_with(
        const drogon::orm::DbClientPtr& executor,
        const std::string& revision_id,
        const std::string& entry_id);

public:

    ComponentInventoryOutcome generate_draft(
        const std::string& bridge_id,
        const std::string& user_id,
        const inventory::GenerateInventoryInput& input,
        const std::vector<inventory::GeneratedInventoryEntry>& generated);
    ComponentInventoryOutcome update_entry(
        const std::string& revision_id,
        const std::string& entry_id,
        const std::string& user_id,
        const InventoryEntryUpdate& update);
    ComponentInventoryOutcome add_entry(
        const std::string& revision_id,
        const std::string& user_id,
        const InventoryNewEntry& entry);
    ComponentInventoryOutcome delete_entry(
        const std::string& revision_id,
        const std::string& entry_id,
        const std::string& user_id);
    ComponentInventoryOutcome deactivate_entry(
        const std::string& revision_id,
        const std::string& entry_id,
        const std::string& user_id,
        const std::string& reason);
    ComponentInventoryOutcome set_mapping(
        const std::string& revision_id,
        const std::string& entry_id,
        const std::string& user_id,
        const InventoryMappingUpdate& mapping);
    ComponentInventoryOutcome confirm_pending_mappings(
        const std::string& revision_id,
        const std::string& user_id,
        const std::string& site_component_type);
    ComponentInventoryOutcome confirm_revision(
        const std::string& revision_id,
        const std::string& user_id,
        const std::string& note);

private:
    drogon::orm::DbClientPtr db_client_;
};

}  // namespace bridge_report::db
