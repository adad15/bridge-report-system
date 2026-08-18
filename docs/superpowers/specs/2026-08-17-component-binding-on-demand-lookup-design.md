# 构件绑定改用按需检索

- 日期：2026-08-17（2026-08-18 三轮评审后修订）
- 状态：已按第三轮复审修订，待复审确认
- 相关模块：校对工作台构件绑定（`ComponentBindingWorkspace`）、构件范围拆分、构件台账检索端点
- 评审记录：`…-design-review.txt`、`…-design-rereview.txt`、`…-design-review-3.txt`

## 背景

打开校对工作台的构件绑定面板要等很久。首屏用 `Promise.all` 并排发两个请求，
**必须两个都回来才渲染**（`ComponentBindingWorkspace.tsx:251`）：

| 请求 | 规模 |
| --- | --- |
| `fetchComponentBinding` | 按导入记录分组，几百项 |
| `fetchLatestComponentInventory` | 5174 条构件 + 5174 条映射，约 3.4 MB |

第二个是 2026-08-16 那轮聚合改造中，因校对工作台仍在使用而**刻意保留**的全量路径。

这 3.4 MB 在面板里只做两件事：把行上的候选 id 与已绑 id 换成可读信息；行内搜索时
遍历全部构件做子串匹配（上限 `MAX_SEARCH_RESULTS = 20`）。

三轮评审下来，事情比"换个端点"大：改造过程中暴露出**两个正在生效的缺陷**，以及
**整条绑定链路缺少版本一致性约定**。这份文档现在装着三件互相独立的事，因此先给
批次划分。

## 实施批次

三批可以分别落地、分别验收。**顺序不能颠倒**：契约必须建立在正确的版本解析之上，
按需检索又依赖契约提供的版本 id。

### 批次一：修两个既有缺陷（无接口变化，建议立即做）

不改任何 API，不动前端，改动量最小，但眼下就在坏。见"既有缺陷"一节的缺陷一、缺陷二。

### 批次二：版本一致性契约

`expected_inventory_revision_id` 贯通 8 个端点，读写分离的校验规则，
`component_inventory_revision_changed` 错误码通道，参数非法的 400。
概览开始返回 `inventory_revision_id`。

### 批次三：按需检索

概览改定向查询，搜索端点扩关键词与 `binding_eligible`，新增批量取数端点，
前端数据流重写与冗余代码删除。

## 既有缺陷

都不是本次引入的，但新代码会把它们固化。缺陷一、二属批次一，缺陷三、四属批次二。

### 缺陷一：草稿优先排序，两处受害

`get_latest_revision(bridge_id)` 的排序是
`order by (status='草稿') desc, revision_number desc`——**草稿优先**。两处代码取到草稿后
再判"是否已确认"，必然不成立。

**受害点 A：绑定面板**（`ImportBindingRepository.cpp:145` `resolve_confirmed_revision`）
年度未锁定版本时走 `get_latest_revision()`。桥上同时存在已确认版本和新草稿时，
面板误报"该桥构件台账尚未确认"，整块绑定 UI 被挡住。

**受害点 B：构件范围拆分**（`ComponentRangeSplitRepository.cpp:197` 预览、`:254` 应用）
两处都是：

```cpp
const auto revision = ComponentInventoryRepository(...).get_latest_revision(bridge_id);
if (!revision || !(revision->status == "已确认" || revision->status == "confirmed")) {
    return {ComponentRangeSplitStatus::Conflict};
}
```

**桥上只要存在一个草稿，范围拆分预览和应用就全部返回 Conflict**，功能整个不可用。
这不是"可能取到不一致的版本"，是直接挡死。

**修正**：两处都改为直接查该桥最新的**已确认**版本
（`where bridge_id=$1 and status='已确认' order by revision_number desc limit 1`），
不经过草稿优先的接口。这一步不需要任何接口改动。

### 缺陷二：范围拆分完全不看检测年度

`ComponentRangeSplitRepository.cpp` 全文**没有一次** `inspection_year` 引用，取数只有：

```sql
select bridge_id::text, import_status, coalesce(parsed_result_json::text,'{}')
from import_records where id=$1::uuid
```

因此拆分用的台账版本与绑定校验用的版本可以不同：年度锁定 R2，而拆分按桥级最新版本
分析。现有 `impact_token` 只能发现"预览与应用之间数据变了"，发现不了"一开始就选错了
版本"。

**修正（批次一部分）**：改为先读 `inspection_years.component_inventory_revision_id`，
锁定优先；未锁定则按缺陷一的规则取最新已确认版本。
**批次二**再补 `expected_inventory_revision_id` 校验。

### 缺陷三：批量替换读的是桥级 `/latest`

同一个草稿优先排序，会让批量替换预览读到与绑定校验不同的版本。
**修正**：改用按导入记录取数的端点（契约 ③），前端不参与选版本。

### 缺陷四：写操作静默切换并锁定版本

年度是在**写操作时**才锁版本的。若进面板时未锁定，`mark_missing` 与 `clear`
（共用 `mutate_group`，`:850-851`）同样会调 `attach_revision_to_pending_year`：

1. 用户打开面板看到 R2；
2. 他人确认了 R3；
3. 用户点"标记缺失"；
4. `mutate_group` 重新解析得到 R3 并写进 `inspection_years`；
5. 操作完成后概览突然变成 R3。

用户没有做任何选择版本的动作，版本却被换掉并固化了。**修正**：见下节。

## 版本一致性契约（批次二）

### 字段名统一

全链路一律用 **`expected_inventory_revision_id`**，POST 放请求体根节点，
GET 放查询参数。不要出现第二个名字——名字一分叉，前后端、测试夹具和错误提示就会
各说各话。

批量绑定的该字段在**请求根节点**，不重复放进每个 `target`。

### 读操作：只校验，不锁定

| 端点 | 说明 |
| --- | --- |
| 获取概览 | 解析有效版本引用并返回，不锁定 |
| 行内搜索 | 按概览给的版本 id 查不可变版本 |
| 批量替换取数 | 携带预期版本，只校验 |
| 拆分预览 | 携带预期版本，只校验 |

读操作的校验规则：年度已锁定版本则必须相等；未锁定则必须仍是最新已确认版本。
**任何情况下都不写 `inspection_years.component_inventory_revision_id`。**

批量取数因此可以安全地保持 GET。反过来，若把"未锁定就锁它"这条规则套到 GET 上，
用户只是打开一次批量替换对话框就会写库，浏览器预取、刷新或客户端重试都会改状态，
读端点的幂等语义也就没有了。要锁，就得改成显式 POST——但业务上没这个必要，
正式应用时重新校验即可。

### 写操作：事务内校验并锁定

| 端点 | 说明 |
| --- | --- |
| 单条绑定 | 校验后锁定 |
| 批量绑定 | 校验后锁定 |
| 标记缺失 | 校验后锁定（修缺陷四） |
| 清除绑定 | 校验后锁定（修缺陷四） |
| 拆分应用 | 校验后锁定；`impact_token` 继续负责数据变化检测 |
| 绑定/切换评定树 | 校验**源**版本，再按目标规范选择或迁移目标版本 |

写操作在同一事务里：校验预期版本 → 未锁定则锁定它 → 执行业务写入。
不一致返回 409 `component_inventory_revision_changed`，**不静默切换**。

`impact_token` 与版本校验各管一段，互不替代：前者管"预览之后 `parsed_result_json`、
拆分目标或分析结果变了"，后者管"台账版本变了"。两者都要保留，且要能分别触发。

### 请求示例

```jsonc
// 单条绑定
{ "part_name": "梁", "component_number": "1-1#梁",
  "bridge_component_id": "…", "expected_inventory_revision_id": "…" }

// 标记缺失 / 清除绑定
{ "part_name": "梁", "component_number": "1-1#梁",
  "expected_inventory_revision_id": "…" }

// 批量绑定（字段在根节点）
{ "expected_inventory_revision_id": "…", "targets": [ … ] }

// 拆分预览
{ "expected_inventory_revision_id": "…", "targets": [ … ] }

// 拆分应用
{ "expected_inventory_revision_id": "…", "targets": [ … ], "impact_token": "…" }

// 绑定评定树
{ "rating_tree_version_id": "…", "expected_inventory_revision_id": "…" }
```

### 错误码必须有传递通道

`BindingOutcome` 现在只有三个字段（`status` / `overview` / `rejected_component_number`），
而 `respond_binding()`（`ImportBindingRoutes.cpp:163`）把**所有** `BindingStatus::Conflict`
压成 `component_binding_conflict`。仓储层就算判出版本不一致，也没有渠道把
`component_inventory_revision_changed` 送到前端。

**采用方案 B**：给 `BindingOutcome` 加 `error_code` / `error_message`，路由优先使用
结果自带的错误码。理由是 `ComponentRangeSplitOutcome` **已经是这个形状**——
`ComponentRangeSplitRepository.cpp:274` 就在设
`error_code = "component_range_split_stale"`。加一个 `BindingStatus` 枚举值会让两套
出参结构继续分叉，而加字段能让绑定、范围拆分、评定树三条路径用同一个机制。

错误码不贯通，前端那五个动作（重取概览、清行内搜索、取消在途请求、清批量缓存、
提示"台账版本已变化，已为你刷新"）就没有可靠触发条件。

### 预期版本参数非法

参数缺失、空串、非 UUID、`null`、层级放错——这些是输入错误，不是版本冲突，
返回 **400 `invalid_component_binding`**（该错误码已存在，见
`ImportBindingRoutes.cpp:231`），不要返回 `component_inventory_revision_changed`。

批量取数端点用更具体的 `invalid_component_binding_inventory_request`。

## 确认版本解析拆两层（批次二）

`resolve_confirmed_revision()` 有 5 个调用点。**订正上一版的说法**：并不是四个写路径
都需要完整 `InventoryRevision`。逐个查过之后：

| 需要完整台账 | 用途 |
| --- | --- |
| `bind`（`:314`） | `validate_target(*revision, …)` |
| `bind_batch`（`:412`） | `validate_target(*revision, …)` |
| 范围拆分预览/应用 | `analysis_outcome(parsed, *revision, targets)` 要遍历 entries |

| 只需要版本引用 | 说明 |
| --- | --- |
| `overview`（`:211`） | 只用了 `.has_value()` |
| `mark_missing` / `clear`（`:850`） | 只用 `revision->id` 做锁定 |
| `bind_rating_tree`（`:567`） | `source_revision` 出现 9 次**全是 `->id`**；映射兼容性是一句 SQL `not exists` 在库里算的 |
| 批量替换取数（新增） | 只需版本 id |

所以拆成：

```cpp
struct ConfirmedRevisionRef { std::string id; std::string bridge_id; };

// 只解析版本 id 与桥梁 id，不加载构件。
std::optional<ConfirmedRevisionRef> resolve_confirmed_revision_ref(...);

// 先调上者，再按准确版本 id 加载完整修订版。
std::optional<inventory::InventoryRevision> load_confirmed_revision(...);
```

**两者必须共享同一套版本解析规则**（锁定优先、否则最新已确认），不能各写一份 SQL——
两份实现迟早漂移，缺陷一就是这么来的。

按上表，`mark_missing`、`clear`、`bind_rating_tree` 改用轻量层之后就不再无谓地加载
整份台账了。

## 检索仓储改造（批次三）

### 现状：谓词被拼进三处，别名各不相同

`load_entry_page()` 收一个 `scope_predicate` 字符串，拼进**三**个地方：

| 位置 | 别名 | 可用列 |
| --- | --- | --- |
| `:514` count 查询 | `e`（实表） | 全部列 |
| `:531` 分页查询的 `numbered` CTE | `p` | `id,bridge_component_id,component_number,site_name,site_component_type,span_or_location,is_active,deactivated_at,deactivation_reason,sort_order,remarks,position` |
| `:571` 映射查询的 `numbered` CTE | `p` | **只有** `id,component_number,site_component_type,sort_order,position` |

现有谓词都是不带别名的裸列（`"id=$2::uuid"`、`"site_component_type=$2"`、
`"component_number like …"`），恰好在三处都能解析，所以一直没出事。

把文档原先给的条件原样拼进去会有两种失败：

**显式报错**：`e.is_active` 在两个 CTE 里没有别名 `e`，SQL 直接失败。

**静默失败**（更危险）：改成裸列写法后，

```sql
exists (select 1 from bridge_component_standard_mappings m
        where m.inventory_entry_id = id and m.is_active)
```

里的裸 `id` 会**就近解析成 `m.id`**，条件变成 `m.inventory_entry_id = m.id` 恒假，
于是搜索永远返回零行——不报错，只是什么都搜不到。

再加上第三处 CTE 根本没 select `is_active`，裸列写法在那里也过不去。

### 改法：一个别名、一段谓词、一条不变量

改成结构化查询参数：

```cpp
struct EntryQuery {
  std::string keyword;            // 空表示不按关键词过滤
  bool binding_eligible{false};
};

EntryLookup load_entry_page(const std::string& revision_id, const EntryQuery& query,
                            std::int64_t offset, std::int64_t limit);
```

三条语句都改成从同名 CTE `scoped`（别名统一为 `s`）取数，谓词按 `s.` 限定生成**一次**，
三处复用。CTE 里预先算好 `has_active_mapping`：

```sql
exists (select 1 from bridge_component_standard_mappings m
        where m.inventory_entry_id = e.id and m.is_active) as has_active_mapping
```

`binding_eligible` 于是就是 `s.is_active and s.has_active_mapping`，
关键词是 `s.component_number / s.site_component_type / s.site_name` 三者 OR。

三条语句的 CTE **select 列表可以不同**（count 不必算位置窗口和 `is_referenced`，
省掉没必要的开销），但要守住一条不变量：

> **谓词引用到的每一列，三处 CTE 都必须 select 出来。**

这正是现在被破坏的地方——第三处漏了 `is_active`。实现时值得加一条注释把这句写上。

### 必须保证的四点

1. 关键词匹配规则三处一致；
2. `binding_eligible` 过滤三处一致；
3. `total` 与 `entries` 用同一数据范围——现有结构已经把谓词同时拼进 count 和 page，
   这条是"别改坏"，不是新增工作；
4. 过滤发生在 `limit` 之前。否则前 20 条可能全是停用构件，真正可绑的被截断在后面。

## 接口契约

### 统一的展示对象类型

```ts
interface BindingComponentSummary {
  entry_id: string;
  bridge_component_id: string;
  component_number: string;
  site_component_type: string;
  site_name: string;
}
```

`site_name` 一并返回：它参与搜索匹配，结果里不显示的话用户看不出为什么命中。
`entry_id` 也必须返回——下拉 `<option>` 的 `key` 用的就是它（`:189`），
`bridge_component_id` 是 `value`，两者不能合并。

### ① 绑定概览

```jsonc
{
  "inventory_confirmed": true,
  // 接口不变量：inventory_confirmed === (inventory_revision_id !== null)
  "inventory_revision_id": "9d52a613-…",
  "rating_tree": { "…": "…" },
  "groups": [
    {
      "part_name": "梁",
      "rows": [
        {
          "component_number": "1-1#梁",
          "defect_count": 3,
          "status": "ambiguous",
          "bridge_component_id": null,
          "bound_component": null,
          "candidate_components": [ /* BindingComponentSummary[] */ ]
        }
      ]
    }
  ]
}
```

**不变量**：`inventory_confirmed === true` 当且仅当 `inventory_revision_id !== null`。
前端仍用 `inventory_confirmed` 控制整块 UI，但搜索、缓存与写操作一律用
`inventory_revision_id`。两者不得出现矛盾组合。

**展示对象的过滤**：`candidate_components` 与 `bound_component` 都只填充
**启用且至少存在一个生效映射**的构件。这是为了复现旧行为——旧代码的 `byId` 是从
`usableEntries()` 建的，不可用的候选根本不会出现在下拉里；绑了但已停用的构件也查不到，
走的是「已绑定构件」那条文案分支。

**候选顺序**：定向 SQL 的返回顺序不保证与 `candidate_component_ids` 一致。回填时
先建 `bridge_component_id → BindingComponentSummary` 的 Map，再**按原 id 顺序**遍历
生成数组，查不到的跳过。否则候选显示顺序会漂，相关测试也不稳定。

**`candidate_component_ids` 从 JSON 移除，但 C++ 结构体字段必须保留**——
`ImportBindingRepository.cpp:90` 用它判定 `ambiguous`，那是业务逻辑。

### ② 检索端点

`?number=` 改名 `?keyword=`，匹配 `component_number`、`site_component_type`、
`site_name` 三字段。**本契约覆盖 2026-08-16 设计文档中的 `?number=` 契约**，
那份文档需同步更新。

新增 `?binding_eligible=true`，语义即上一节的 `s.is_active and s.has_active_mapping`。

**语义边界**：`binding_eligible` 只表示"台账层面可供选择"，**不保证对当前行可绑**——
正式绑定时 `validate_target()` 还会校验部件名与规范类别的兼容性，用户仍可能被拒。
要真正保证，搜索得额外携带 `part_name` 并复刻 `validate_target()` 的规则，那是改动
绑定判定语义，不在本轮范围。文档中一律表述为"启用且有生效映射"，不写"可绑定"。

台账管理页不传 `binding_eligible`，仍能看到停用与未映射构件。

### ③ 批量替换取数端点

```
GET /api/import-records/{import_id}/component-binding/inventory
      ?expected_inventory_revision_id={id}
```

挂在现有 `component-binding` 基路径下。按 `inspection_years` 解析该导入实际使用的版本，
**只校验预期版本，不锁定**（见"读操作"一节）。

**过滤条件与 ② 的 `binding_eligible` 完全一致**，以复现旧代码 `usableEntries()` 的范围。
只筛启用会让预览把"无生效映射的构件"判成 `will_bind`，用户点应用后 `validate_target()`
找不到映射，整批冲突——与改造前不一致。

**响应只含预览真正需要的字段**：

```jsonc
{
  "inventory_revision_id": "9d52a613-…",
  "entries": [
    { "bridge_component_id": "…", "component_number": "1-1#梁", "is_active": true }
  ]
}
```

`buildReplacePreview` 只用这三样：`is_active` 做过滤、`component_number` 做归一化索引
键、`bridge_component_id` 做绑定目标（`replacePreview.ts:54-84`）。不含映射、类别与
现场名。5174 条约 400 KB，而整份台账是 3.4 MB。

**`is_active` 必须返回，即使服务端已经过滤掉停用构件。** `replacePreview.ts:55` 的
`if (!entry.is_active) continue;` 是防御性判断；字段缺失时 `!undefined` 为真，会把
**每一条**都跳过，预览安静地全判成 `not_in_inventory`。保留字段与保留那行判断，
比互相依赖对方"已经过滤过"更稳。

**错误契约**：

| 情况 | 响应 |
| --- | --- |
| 未登录 | 401 |
| `import_id` 非法或记录不存在 | 404 |
| `expected_inventory_revision_id` 缺失或格式非法 | 400 `invalid_component_binding_inventory_request` |
| 导入记录不是"待校对" | 409 `component_binding_conflict` |
| 没有可用的已确认台账 | 409 `component_binding_inventory_unavailable` |
| 预期版本不一致 | 409 `component_inventory_revision_changed` |
| 数据库异常 | 503 |
| 前端主动取消 | 静默，不显示加载失败 |

## 后端数据流

### 概览

1. `resolve_confirmed_revision_ref()` 拿到版本引用，**不加载构件**；
2. 从 `parsed_result_json` 聚合 `BindingRow`，行状态照旧由内部
   `candidate_component_ids` 与现有业务逻辑计算；
3. 收集所有 `bridge_component_id` 与候选 id，**去重**；
4. 在该版本下对这批 id 做**一次**定向查询，带上"启用 + 有生效映射"过滤，
   只取 `BindingComponentSummary` 的五个字段；
5. 按原 id 顺序回填 `candidate_components`，并回填 `bound_component`。

第 4 步是后端优化的关键：查的是"这几十个 id"，不是"这个版本的全部构件"。

### 行状态与展示对象是两回事

内部 id 是业务判断数据，展示对象是显示数据。可能出现内部有两个候选 id、而展示对象
只有一个（构件被删、或已停用、或没有生效映射）：

```
candidate_component_ids.length = 2
candidate_components.length   = 1
```

此时行**仍然是 `ambiguous`**，不能按展示对象的数量重算成 `unmatched` 或单候选。
展示对象缺失只影响显示，不改变行状态。

### 评定树绑定/切换

评定树流程**会**改变年度使用的台账版本——`ImportBindingRepository.cpp:802` 就在
`update inspection_years set standard_profile_id=…, component_inventory_revision_id=…`。
因此它要校验**源**版本仍是用户看到的那个，再按目标规范选择或迁移目标版本，
最后响应携带按新版本生成的完整概览（含新的 `inventory_revision_id` 与展示对象）。

前端删掉那次额外的 `/latest`，**理由不是"绑评定树不改台账"（那是错的）**，而是
操作响应已经带回按新版本生成的完整概览，不必再问一次。

## 前端数据流

### 首屏

`Promise.all` 塌成单个 `fetchComponentBinding`，概览到达即渲染。
`:426` 绑定评定树后的整份台账重取**删除**。

面板另有一个**非阻塞**的 `fetchRatingTreeVersions()`（`:269`），它是辅助选择数据，
保留不动。因此不能笼统说"只发一次请求"，见测试一节的精确表述。

### 统一的选项模型

概览候选用 `entry_id`，而搜索端点返回的 `ComponentInventoryEntry` 用 `id`
（`componentInventoryApi.ts:17`）。两种对象要合进同一个下拉，直接混用会让
`<option key>` 取到 `undefined`，React 出现重复 key 与列表复用错误。

因此前端只处理一种模型，两个来源各自转换后入表：

```ts
interface BindingComponentOption {
  entryId: string;
  bridgeComponentId: string;
  componentNumber: string;
  siteComponentType: string;
  siteName: string;
  candidate: boolean;   // 决定「候选 · 」前缀
}
```

- `candidate_components[].entry_id` → `entryId`，`candidate: true`；
- 搜索结果 `entry.id` → `entryId`，`candidate: false`。

合并、去重、排序与渲染只认 `BindingComponentOption`。

### 行内搜索

- 用 `overview.inventory_revision_id` 调
  `?keyword=…&binding_eligible=true&limit=20`；
- 250 ms 防抖；`AbortController` 防乱序；
- `limit` 取 20，与现有 `MAX_SEARCH_RESULTS` 一致。

### 合并与显示

| 规则 | 取值 |
| --- | --- |
| 候选项是否始终显示 | 是，与当前关键词是否匹配无关 |
| 顺序 | 候选在前（按 `candidate_components` 原序），搜索结果追加在后 |
| 去重键 | `bridgeComponentId` |
| `limit` 是否包含候选项 | 否，只约束服务端搜索结果 |
| 搜索失败 | 保留概览候选项，就地显示行内错误 |
| 清空关键词 | 立即清除服务端搜索结果，只留候选项 |
| 绑定完成 | 清除该行的搜索词与搜索结果 |
| 版本变化 | 取消在途搜索、清空搜索结果、清除批量缓存 |

**「候选 · 」前缀保留**（`:190`）。它现在靠 `new Set(row.candidate_component_ids)` 判定，
改后由 `BindingComponentOption.candidate` 承担。这个前缀是用户区分"系统猜的"与
"自己搜的"的唯一标记，不能随字段一起删掉。

**`limit` 语义有一处刻意的行为变化**：现在的 `MAX_SEARCH_RESULTS` 卡的是**含候选在内
的选项总数**（`:157` 判的是 `options.size`），因此候选多时会挤掉搜索结果。改后 `limit`
只约束服务端返回条数，候选另计，合并后可能是 20~22 项。这是有意为之——候选被搜索结果
挤掉是当前实现的缺陷，而不是要保留的语义。除此之外行内搜索的可见行为不变。

下拉项显示 `编号 / 类别 / 现场名`。原先只显示前两项（`:191`），但 `keyword` 会匹配
现场名——不显示的话，用户按现场名搜出来会看不懂为什么命中。

### 批量替换

打开对话框时才调 ③。必须补齐加载态：

- `loading` / `error` 两个状态；加载完成前**禁用**查找、替换、应用；
- **不能用空数组表示"尚未加载"**——那会让预览把所有编号判成 `not_in_inventory`；
- 响应到达后先比对 `inventory_revision_id` 与当前 `overview.inventory_revision_id`，
  不一致则不生成预览，走版本变化流程；
- 关闭对话框取消未完成请求；取消是静默的，不显示加载失败；
- 失败给重试入口；
- 缓存**按 `inventory_revision_id`**，同版本重开可复用，版本变化即失效。
  **不能按 `bridgeId` 缓存**——那正是缺陷三那类错误的温床。

### `buildReplacePreview` 的输入类型

归一化、存在性与歧义判定**语义不变**，但**输入类型要适配精简响应**：

```ts
interface BindingReplaceInventoryEntry {
  bridge_component_id: string;
  component_number: string;
  is_active: boolean;
}
```

签名从 `entries: ComponentInventoryEntry[]` 改为
`entries: BindingReplaceInventoryEntry[]`，`if (!entry.is_active) continue;` **保留**。
不要把精简对象伪装成完整的 `ComponentInventoryEntry`。

`replacePreview.test.ts` 中"停用构件不参与预览"的用例必须保留。

`replacePreview.ts` 顶部"绑定页已把台账 entries 加载在手"的注释已过期，需更新。

### 删除清单（均在面板内部）

| 删除 | 原因 |
| --- | --- |
| `usableEntries()` | 概览已带展示信息，搜索由服务端过滤 |
| `entries` memo、`byId` Map、`inventory` state | 同上 |
| `RowAction` 的 `entries` / `byId` 两个 prop | 改读 `row.*` |
| `:426` 绑定评定树后的整份台账重取 | 响应已带回新概览 |
| 搜索框与下拉的 `entries.length === 0` 门禁 | 冗余：整块 UI 已在 `inventory_confirmed` 之后 |

## 构件台账页的连带改动

台账页不传 `binding_eligible`，仍能看到停用与未映射构件；但搜索变成三字段匹配，
文案必须跟上：

| 现在 | 改后 |
| --- | --- |
| 标签「按编号搜索构件」 | 「搜索构件」 |
| 占位符「如 3-5#」 | 「编号、类别或现场名，如 3-5# 或 支座」 |
| 提示「输入构件编号可直接定位单个构件」 | 补一句：整组构件请用分组表的"查看构件"，那里能分页看全 |

最后一句是必要的：搜"支座"会得到「匹配 3300 个，显示前 50 个」，而分组表点进去能
完整分页浏览。不点破，用户会以为这座桥只有 50 个支座。

## 测试

### 批次一

- 同时存在已确认版本与新草稿时，绑定概览仍判定为已确认；
- 同时存在已确认版本与新草稿时，范围拆分预览与应用**不再返回 Conflict**；
- 范围拆分读取 `inspection_years.component_inventory_revision_id`，年度锁定版本优先；
- 范围拆分不再调用草稿优先的 `get_latest_revision()`。

### 批次二

- 年度未锁定且最新已确认版本已变化时，预期版本校验返回 409；
  年度已锁定且预期版本不符时同样 409；
- `mark_missing` / `clear` 携带并校验预期版本，版本变化时不再静默切换；
- 拆分预览携带预期版本，版本变化时返回 `component_inventory_revision_changed`；
- 拆分应用在事务中重新校验，版本变化时返回同一错误码；
- `impact_token` 与版本校验**分别**生效：只改 `parsed_result_json` 触发 stale，
  只改版本触发 revision_changed；
- 评定树绑定校验源台账预期版本；
- 批量取数 GET **不写入** `inspection_years.component_inventory_revision_id`；
  拆分预览同样不写入；
- 只有正式写操作才锁定年度版本；
- 预期版本缺失、空串、非 UUID 时返回 400，不是 409；
- `BindingOutcome` 能把版本变化映射成独立错误码；
- `ComponentRangeSplitOutcome` 映射同一个错误码；
- `mark_missing` / `clear` / `bind_rating_tree` 改用轻量解析后不再加载整份台账。

### 批次三（后端）

- `keyword` 分别命中编号、构件类别、现场名称；通配符 `%`、`_`、`\` 转义依然有效；
  空或纯空白返回 400；
- `binding_eligible=true` 不返回停用构件、不返回无生效映射的构件；
- **过滤先于 `limit`**：构造前 20 条全为停用、第 21 条才有效的数据，断言能搜到它；
- **`total` 与 `entries` 过滤范围一致**：`binding_eligible` 下的 `total` 不含被过滤掉的构件；
- **三条语句的谓词一致**：分页结果与映射装配结果针对同一批构件（谓词漏拼或别名写错
  会让映射查询返回零行，构件全部丢失映射）；
- `binding_eligible` **只**过滤启用与生效映射，不做类别兼容性过滤（语义边界用例）；
- 概览返回准确的 `inventory_revision_id`；`inventory_confirmed` 与它始终满足不变量；
- 概览返回 `bound_component` 与 `candidate_components`，JSON 中不再有
  `candidate_component_ids`；
- `candidate_components` 不含停用或无生效映射的构件；
- `candidate_components` 的顺序与 `candidate_component_ids` 一致；
- 候选展示对象缺失时行状态仍为 `ambiguous`；
- 已绑构件在该版本中不存在或不可用时 `bound_component` 为 `null`；
- 概览只查候选与已绑 id，不完整加载整份台账；
- ③ 端点只含启用且至少有一个生效映射的构件，返回精简 DTO 且带 `is_active`；
- ③ 端点取的是年度锁定版本，不是桥级 `/latest`；
- ③ 端点的 400 / 401 / 404 / 409 / 503 映射。

### 批次三（前端）

- 进面板**只发一次构件绑定首屏数据请求**：`fetchComponentBinding` 恰好 1 次、
  `fetchLatestComponentInventory` 恰好 0 次；`fetchRatingTreeVersions` 允许 1 次且
  不阻塞概览渲染——**不能笼统断言"只发一次请求"**；
- 概览里的候选项在搜索前即可见；不可用的候选不出现在选项里；
- 行内输入 250 ms 后调 `keyword` 搜索，带 `binding_eligible`，且用
  `overview.inventory_revision_id`；
- 新搜索取消旧请求，旧响应不覆盖新响应；
- 搜索失败保留候选项并显示行内错误；
- 候选与搜索结果按 `bridgeComponentId` 去重，候选在前且保持原序；
- 候选项带「候选 · 」前缀，搜索结果不带；
- 候选不占用 `limit` 名额：候选 2 项 + 服务端返回 20 项时下拉共 22 项；
- **候选的 `entry_id` 与搜索结果的 `id` 都转成 `entryId`**，option key 稳定、唯一、
  不为 `undefined`；
- 五个写操作 API（绑定、批量绑定、标记缺失、清除、评定树）都携带
  `expected_inventory_revision_id`，且批量绑定的该字段在**请求根节点**；
- 拆分预览与拆分应用同样携带该字段；
- 参数名全链路统一，无 `expected_revision_id` 残留；
- 收到 `component_inventory_revision_changed` 时刷新概览、清搜索结果、清批量缓存
  并给出明确提示；
- 绑定评定树后不再重取整份台账；
- **精简批量条目不会被 `buildReplacePreview` 全部跳过**；
- 批量响应版本与当前概览不一致时不生成预览；
- 对话框未打开时不取数；打开后才取；取数期间不会把编号判成 `not_in_inventory`；
- 取数失败显示错误与重试；**主动取消不显示错误**；
- 同版本重开复用缓存；版本变化时缓存失效。

**必须同步修改的位置**：`importBindingApi.ts` 的接口定义，以及 5 处测试夹具——
`importBindingApi.test.ts:28`、`ReviewWorkspacePage.test.tsx:197`、
`BulkReplaceDialog.test.tsx:15`、`replacePreview.test.ts:14` 四处是空数组，
`ComponentBindingWorkspace.test.tsx:94` 是 `status === "unmatched" ? ["c1"] : []`，
改新字段时要把 `"c1"` 换成完整的候选对象，否则那条用例覆盖不到候选项的显示。

## 涉及文件

**批次一**：`ComponentRangeSplitRepository.{cpp,hpp}`、`ImportBindingRepository.cpp`
及对应后端测试。

**批次二**：上述文件加 `ImportBindingRoutes.cpp`、`ImportBindingRepository.hpp`
（`BindingOutcome` 加错误码字段）、`importBindingApi.ts`、
`ComponentBindingWorkspace.tsx` 及前后端测试。

**批次三**：`ComponentInventoryRepository.{cpp,hpp}`、`ComponentInventoryRoutes.cpp`、
`ImportBindingRepository.cpp`、`ImportBindingRoutes.cpp`、`importBindingApi.ts`、
`ComponentBindingWorkspace.tsx`、`BulkReplaceDialog.tsx`、`replacePreview.ts`、
`ComponentInventoryEditor.tsx` 及测试。

## 验收

1. **批次一**：桥上存在草稿时，绑定面板正常显示、范围拆分正常可用；
2. 范围拆分使用年度锁定的台账版本；
3. **批次二**：8 个端点统一使用 `expected_inventory_revision_id`；
4. 读操作只校验不锁定，写操作事务内校验并锁定；
5. 版本变化时三条路径都返回 `component_inventory_revision_changed`，不静默切换；
6. 参数缺失或非法返回 400；
7. **批次三**：打开绑定面板不再请求 `/component-inventories/latest`；
8. 首屏只等概览（评定树版本列表非阻塞）；
9. 后端概览不再完整加载五千多条构件及映射；
10. 行内搜索与批量取数都只返回启用且有生效映射的构件，且 `total` 与 `entries` 同范围；
11. 搜索能匹配编号、构件类别、现场名称；
12. 绑定评定树后不再额外获取整份台账；
13. 批量替换只在打开对话框时取数，失败不生成预览；
14. 绑定、标记缺失、批量替换的业务语义不变，预览结果与改造前逐项一致；
    唯一有意的可见行为变化是候选项不再占用搜索结果名额。

性能须以 **Release 构建**复测：Debug 关优化且开 `_ITERATOR_DEBUG_LEVEL=2`，
数千条构件的 JSON 序列化约慢 4 倍，Debug 下的数字无参考价值。

## 非目标

- **不改造 `DefectsSection` 的手动添加病害表单**，`/latest` 端点因此本轮不删。
- **不改绑定的业务判定规则**。`binding_eligible` 不承担部件名与规范类别的兼容性
  校验，那仍由 `validate_target()` 在正式绑定时执行。
- 不改绑定、标记缺失、批量替换的业务语义。

## 遗留（2026-08-18 已完成）

`DefectsSection` 已改造：手动添加病害的构件选择器换成按需检索，其余所需信息改从分组
汇总与草稿自身取。过程中发现真正的大头不在这个表单——校对页响应 `/review` 本身就内嵌
整份台账（约 3.6 MB），每次打开都传，且同样踩了草稿优先解析。两处一并去掉后，
`/latest` 端点与 `inventory_revision_json()` 已无任何调用者，随之删除。

**仍未处理**：`get_latest_revision()`（草稿优先）在校对保存、入库前检查、病害自动匹配
与 Word 导入四条路径上还有 6 个调用点，与本文档"缺陷一"同源，详见下节。

## 草稿优先解析的剩余调用点（2026-08-18 审查发现，尚未修）

缺陷一修的是绑定面板与范围拆分两处。清理死代码时把 `get_latest_revision()` 的调用方
全部过了一遍，**同一个坑还有 6 处**，都在"应当使用已确认台账"的语义下用了草稿优先的
接口：

| 位置 | 用途 | 桥上存在草稿时的后果 |
| --- | --- | --- |
| `ReviewRoutes.cpp:271` | 保存校对草稿前校验病害与构件的关联 | **最严重**，见下 |
| `ImportConfirmRoutes.cpp:71` | 入库前检查的上下文 | 预检按草稿版本判定 |
| `ReviewRepository.cpp:837` | 确认事务内的预检复核 | 同上 |
| `DefectMatchingRoutes.cpp:188` | 病害→构件自动匹配 | 匹配到不在已确认台账里的构件 |
| `WordImportRepository.cpp:67` | 导入时的构件匹配 | 同上 |
| `WordImportRepository.cpp:164` | 导入时的评定树匹配 | 同上 |

前三处的判定链条最清楚。`DraftValidation.cpp:194`：

```cpp
if (!latest_revision.has_value() || revision_id != latest_revision->id) {
    result.issues.push_back({path, "关联所依据的构件台账已变化，请重新选择。"});
```

左边 `revision_id` 是绑定时写进病害的**已确认**版本（`write_binding()` 的第五个参数），
右边却是草稿优先解析出来的版本。两者在有草稿时必然不等，于是**每一条已绑定病害都会
报错，整个校对草稿保存不下去**。

这几处都不在本轮改动范围内，且修法已经现成（`resolve_confirmed_revision()` /
`get_latest_confirmed_revision()`），但每一处都要先确认它到底该用"年度锁定版本"还是
"最新已确认版本"——`WordImportRepository` 那两处已经带着 `locked_revision_id` 分支，
形状与修正前的 `resolve_confirmed_revision()` 一模一样。
