# 构件台账聚合接口设计

- 日期：2026-08-16
- 状态：已评审，待实现
- 相关模块：构件台账（`ComponentInventoryEditor` / `ComponentInventoryRoutes` / `ComponentInventoryRepository`）

## 背景

百股大桥（QL-000002）的台账有 5174 条构件、5174 条规范映射，分 18 个类别。
页面上"分组核对"表只有 18 行，但为了画出这 18 行，前端要把全部构件连同各自的
映射整份下载下来，在客户端汇总。

实测（2026-08-16，真实数据）：

| 环节 | 耗时 / 体积 |
| --- | --- |
| SQL 取构件（含逐行 `is_referenced` 相关子查询） | 32.75 ms |
| SQL 取映射 | 8.84 ms |
| 响应体 | 约 3.4 MB（构件 1.6 MB + 映射 1.9 MB） |
| 前端 `JSON.parse` | 8.4 ms |
| 前端三处 `O(n)` 汇总 | 22.9 ms |

数据库和前端合计约 73 ms，其余时间花在把 3.4 MB 拼出来并传输。

更要紧的是这 3.4 MB 不只首屏来一次。`ComponentInventoryRoutes.cpp` 的
`respond_inventory_outcome` 是所有写操作共用的响应器，**8 个写端点全部回传整份修订版**
（生成、新增、修改、删除、停用、设置映射、批量确认映射、确认台账）。改一条构件的
备注，请求约 200 字节，响应 3.4 MB，放大约 17000 倍。

## 目标与非目标

**目标**

1. 首屏从 3.4 MB 降到 KB 级。
2. 写操作的响应从 3.4 MB 降到 KB 级。
3. 服务端补齐重复编号校验，使"确认前还需处理 N 项"与 confirm 的实际拦截口径一致。

**非目标**

- 不引入台账写操作的编辑锁（现状无锁，后写覆盖先写，本次保持）。
- 不改 `is_referenced` 的语义。
- 不改向导（`InventoryPlanPanel`）与生成逻辑。
- 不做响应压缩。聚合之后载荷已是 KB 级，压缩省的是带宽而序列化 CPU 一分不少，
  收益不再显著。

## 设计中发现的既有问题

记录在此，因为它们决定了实现细节，不是实现时可以自行取舍的。

> 校准：第 1、4、5、6 条在**当前真实数据上不可见**（该桥每构件仅 1 个生效映射、
> `sort_order` 5174 个值全不重复、无停用构件）。它们是 schema 与代码允许、
> 而现有数据恰好没触发的情形。因此真实数据比对不能作为唯一验收手段，
> 见"测试与验收"。

### 1. 编号范围不是字典序的 min/max

`inventoryGroupSummaries` 的 `firstNumber` / `lastNumber` 是按 `sort_order` 遍历时的
第一条和最后一条，不是 `min()` / `max()`。编号是字符串，字典序下 `'9' > '3'`，
33 孔的桥用 min/max 会取到第 9 孔：

| 类别 | 遍历口径（界面现值） | `min/max` 口径 |
| --- | --- | --- |
| 支座 | `1-1-1#支座 … 33-2-50#支座` | `1-1-1#支座 … 9-2-9#支座` |
| 梁 | `1-1#梁 … 33-25#梁` | `1-1#梁 … 9-9#梁` |
| 湿接缝 | `1-1#湿接缝 … 33-24#湿接缝` | `1-1#湿接缝 … 9-9#湿接缝` |
| 墩柱 | `1-1#墩柱 … 32-4#墩柱` | `1-1#墩柱 … 9-4#墩柱` |
| 盖梁 | `1#墩盖梁 … 32#墩盖梁` | `1#墩盖梁 … 9#墩盖梁` |
| 伸缩缝 | `1#伸缩缝 … 9#伸缩缝` | 相同（孔号均为个位） |

SQL 里用 `array_agg(... order by sort_order, id)[1]` 取两头，不得用 `min/max`。

### 1b. 分组排序需要 tie-breaker

现有分组顺序来自前端 `Map` 的插入序，而 entries 由后端按 `order by sort_order, id`
返回——即"首次出现的 (sort_order, id)"。

`sort_order` 定义是 `integer not null default 0`，**没有唯一约束**。手工新增构件
若不传 `sort_order` 即为 0，两个分组的最小值就会撞在 0 上。因此不能写
`order by min(sort_order)`，要按首个构件的 `(sort_order, id)` 排：

```sql
order by (array_agg(e.sort_order order by e.sort_order, e.id))[1],
         (array_agg(e.id::text order by e.sort_order, e.id))[1]
```

### 2. 停用构件算进编号范围但不算进数量（本次修正）

```js
group.lastNumber = entry.component_number;   // 先赋值
if (!entry.is_active) continue;              // 后判断停用
group.activeCount += 1;
```

停用构件会出现在编号范围里却不计入数量。当前该桥无停用构件，问题不可见。

**本次决定修正**：`first_number` / `last_number` 只统计启用构件，与 `active_count`
口径一致。这是本次**第一处**有意的行为变更（第二处见问题 5）。

边界：某组构件被全部停用时 `active_count` 为 0、编号范围为空，该组**仍需返回**，
界面显示 `—`。用 `array_agg(...) filter (where is_active)` 天然得到 NULL，无需分支。

### 3. 前端与后端的确认校验口径不一致

| 规则 | 前端 | 后端 |
| --- | --- | --- |
| `inventory_empty` | 有 | 有 |
| `component_mapping_required` | 有 | 有 |
| `duplicate_component_number` | 有 | **无** |

绕过前端调用 API 可以确认掉带重复编号的台账。本次把校验统一到服务端，由
`compute_blockers()` 一份实现同时服务汇总端点与 confirm 路径。

另需注意：前端 `inventoryConfirmationBlockers` 的重复检查遍历全部构件，
**不按 `is_active` 过滤**，服务端实现保持一致。

### 4. 一个构件可以有多个生效映射

`011_component_inventory_revisions.sql` 的唯一索引是

```sql
create unique index ux_component_standard_mappings_active
  on bridge_component_standard_mappings (inventory_entry_id, standard_package_id)
  where is_active;
```

唯一性按 **(构件, 规范包)**，不是按构件。**一个构件可以同时挂多个规范包的生效映射**，
`test_component_inventory_repository.cpp:148` 就明确创建了两个。

后果：汇总若直接 `left join ... and m.is_active`，这类构件会展开成多行，
`count(*) filter (where e.is_active)` 把它**重复计数**，`active_count`、
`confirmed_count`、`pending_count` 全部偏大。必须先把每个构件收敛成一行再 join。

### 5. 多映射下前端两个函数互相矛盾（本次统一）

| 函数 | 判定方式 |
| --- | --- |
| `inventoryGroupSummaries` | `mappings.find(m => m.is_active)` —— 只看**首个**生效映射 |
| `inventoryConfirmationBlockers` | `mappings.some(m => m.is_active && 已确认)` —— **任意一个**已确认即放行 |
| 后端 confirm | `count(*) where is_active and 已确认 >= 1` —— 同 `some` |

一个构件挂 [待确认(包 A)、已确认(包 B)]：分组汇总记进"待确认"，而 blocker 判定放行。
两个数字对不上，用户会问。

**本次统一到 `some`**（与后端 confirm 现状一致）。分组计数改为按构件三分：

- `confirmed_count`：该构件**存在**已确认的生效映射
- `pending_count`：有生效映射，但**全部**为待确认
- `unmapped_count`：没有任何生效映射

三者互斥且覆盖全部启用构件，每个构件只计一次。这是本次第二处有意的行为变更
（第一处见问题 2）。

`structure_part` 与 `standard_component_category_id` 是展示字段，**不套用 `some`**，
规则单列在"接口契约 ⑦"。

## 方案选择

考虑过三种：

- **A 汇总 + 按需明细**（采纳）：汇总、分组分页、编号搜索三个读端点各司其职，
  前端不再持有全量列表。
- **B 汇总先行、明细整份懒加载**：首屏取汇总，用户一旦点开任意分组就拉整份 3.4 MB。
  前端改动小，但该页的主要用途正是点开分组核对，多数会话里 3.4 MB 照样发生，
  只是从进页面挪到第一次点击。
- **C 单端点 + 投影参数**：给 `/latest` 加 `?view=`，一个 handler 内部分支。
  新增路由最少，但同端点按参数返回不同形状，前端要写联合类型收窄，
  后端 handler 分支化，测试按 view 分别覆盖，得不偿失。

选 A。理由：B 只推迟问题；C 用清晰度换路由数量；而 A 的额外代价（前端状态模型重写）
在"写端点不再回传全量"这一决定下本来就躲不掉。

## 接口契约

### ① 首屏汇总

`GET /api/bridges/{bridge_id}/component-inventories/latest/summary`

```json
{
  "revision": {
    "id": "9d52a613-…", "bridge_id": "…", "revision_number": 1, "status": "草稿",
    "baseline_revision_id": null, "confirmed_at": null, "active_entry_count": 5174
  },
  "groups": [
    {
      "site_component_type": "支座", "structure_part": "superstructure",
      "active_count": 3300,
      "first_number": "1-1-1#支座", "last_number": "33-2-50#支座",
      "confirmed_count": 3300, "pending_count": 0, "unmapped_count": 0,
      "standard_package_id": "6c66162b-…",
      "standard_component_category_id": "h21.component.upper.bearing"
    }
  ],
  "blockers": {
    "total": 0,
    "by_code": { "component_mapping_required": 0, "duplicate_component_number": 0 },
    "samples": [
      { "code": "duplicate_component_number", "entity_type": "inventory_entry",
        "entity_id": "…", "field_path": "component_number", "message": "…",
        "site_component_type": "梁", "position": 54 }
    ]
  }
}
```

按字段估算约 4.5 KB（未实测）。`groups` 按首个构件的 `(sort_order, id)` 排序
（见问题 1b），与向导顺序一致。

**可空字段**：`first_number`、`last_number`、`structure_part`、
`standard_package_id`、`standard_component_category_id` 类型均为 `string | null`。
整组构件被全部停用时前两者为 `null`；该组无任何生效映射时后三者为 `null`。
新增的前端类型必须显式可空——现有 `InventoryGroupSummary` 的
`firstNumber: string` 是必填，不能照搬。

`revision.active_entry_count` **只计启用构件**（界面"共 N 个启用构件"用它），
与 ② 中的 `total`（该组构件总数，**含停用**，用于翻页）语义不同，勿混。

`confirmed_count` / `pending_count` / `unmapped_count` 按问题 5 的三分定义，
三者之和等于 `active_count`。

返回 `standard_component_category_id` 而非解析后的标签：规范目录由前端另一条
请求获取，目录未到达时显示 `—` 的现有逻辑保持不变。

`blockers.samples` 的取值规则见 ⑤。

### ② 分组明细

`GET /api/component-inventories/{revision_id}/entries?group=<类别>&page=0&size=100`

```json
{ "total": 3300, "page": 0, "size": 100, "entries": [ /* 完整构件对象，含 mappings 与 is_referenced */ ] }
```

`total` 是该组的构件总数（含停用），用于翻页。100 条约 70 KB。

`page` 默认 0，`size` 默认 100（与现有 `kEntriesPageSize` 一致），上限 200；
超出上限按上限处理，负值返回 400。

### ③ 编号搜索

`GET /api/component-inventories/{revision_id}/entries?number=<编号片段>&limit=50`

```json
{ "total": 5, "entries": [ { "…": "…", "site_component_type": "梁", "position": 54 } ] }
```

`total` 是**未截断**的命中数，用 `count(*) over ()` 在同一次查询里得出——界面上
"匹配 N 个构件，显示前 50 个"依赖真实命中数。

`limit` 默认 50（与现有 `kMaxSearchResults` 一致），上限 100。

②③ 复用同一路由与同一响应形状，仅过滤条件不同。`group` 与 `number` 二选一，
同时缺失或同时提供时返回 400。

### ④ 全部写端点的响应

统一改为：

```json
{ "revision": {…}, "groups": […], "blockers": {…}, "entry": { /* 可选 */ } }
```

前三段与 ① 完全一致（约 4.5 KB）。`entry` 仅在单条构件被改动时出现
（新增 / 修改 / 停用 / 设置映射）；删除、批量确认映射、确认台账、生成台账不带。

不做"只回传受影响的分组"：批量确认映射天然影响所有分组，为它单开路径不值；
而整份汇总仅 4.5 KB，统一回传省去一套增量失效簿记，前端每次拿到的汇总
永远完整自洽。

### ⑤ `position` 而非 `page`

页码取决于客户端的 `size`，服务端不作假定。blocker 样本与搜索结果返回
`position`（组内序号，从 0 起），前端计算 `Math.floor(position / size)`。

**分页序号只有一个权威**：`row_number() over (partition by site_component_type
order by sort_order, id) - 1`，**不按 `is_active` 过滤**（与现有弹窗一致，停用构件
仍在列表中可见并占位）。②、③、blocker 样本三处必须共用同一个 CTE，
不得各写一遍，否则"定位"会跳到错误的页。

注意这与"编号范围只算启用构件"是两套不同的过滤，不可混用：
范围是**展示口径**（本次修正过），分页是**定位口径**（保持原样）。

### ⑥ blocker 样本的两级拆分

现有界面把 blocker 分两类显示：有待确认映射的构件汇总成一行"N 个构件的规范映射
待确认"加一键确认按钮；其余逐条列出并带"定位"按钮。服务端照此拆分：

- `samples` **只含**重复编号、以及完全没有生效映射的构件；
- 有待确认映射的那批**不进 `samples`**，界面上那一行的数字取自
  `groups[].pending_count` 之和（现有 `pendingMappingCount` 就是这么算的，
  数据源不变，只是从客户端汇总改为读汇总响应）。

计数与样本的关系需明确：`by_code.component_mapping_required` 统计
**全部缺少已确认生效映射的启用构件**，按问题 5 统一后的 `some` 口径判定，
等于 `pending_count + unmapped_count` 之和；而 `samples` 只收其中
`unmapped_count` 那部分。两者不相等是有意的。

（统一到 `some` 之前这条等式不成立——`pending_count` 按首个映射算、blocker 按任意
映射算，多映射时两边会各说各话。这正是问题 5 要消除的。）

`samples` 上限取 30（现有 `kMaxIndividualBlockers` 的值），"……其余 N 项"由
`total` 减样本数得出。

### ⑦ `structure_part` 与规范类别的取值规则

这两个是展示字段，不套用 `some`，规则分两级，复刻现有行为：

**构件级**：取该构件**首个**生效映射（按 `created_at, id` 排序，与现有映射查询
一致）的 `structure_part` / `standard_component_category_id` / `standard_package_id`。

**分组级**：`structure_part` 取该组内**首个（按 `sort_order, id`）取值非 `other`
的启用构件**；全组都是 `other` 或都无映射时为 `other`。现有代码
`if (group.structurePart === "other" && mapping.structure_part)` 起始值就是
`"other"`，赋 `"other"` 等于没赋，循环会继续往后找——所以它不是"首个生效映射"，
而是"首个非 `other`"。

`standard_package_id` / `standard_component_category_id` 取该组内**首个有生效映射
的启用构件**的值（不筛 `other`）。现有实现里它取自"首个能解析出标签的构件"，
与 `structure_part` 可能来自不同构件；本次规范化为"首个有生效映射的构件"，
在单映射数据上无差异。

## 后端实现

### SQL

四条查询已在真实数据上验证（2026-08-16，5174 条）：

| 查询 | 实测 |
| --- | --- |
| 汇总（18 组） | 76.0 ms |
| 组内序号 | 9.9 ms |
| 重复编号 | 3.7 ms |
| 编号搜索 | 8.4 ms |

汇总查询。`entry_mapping` 这层 CTE 是必需的：直接
`left join ... and m.is_active` 会让多生效映射的构件展开成多行，把 `count(*)`
放大（见问题 4）。先按构件收敛成一行，再 join：

```sql
with entry_mapping as (
  select m.inventory_entry_id,
         bool_or(m.confirmation_status = '已确认')                               as has_confirmed,
         (array_agg(m.structure_part order by m.created_at, m.id))[1]           as structure_part,
         (array_agg(m.standard_package_id::text order by m.created_at, m.id))[1] as package_id,
         (array_agg(m.standard_component_category_id
                    order by m.created_at, m.id))[1]                            as category_id
  from bridge_component_standard_mappings m
  join bridge_component_inventory_entries e on e.id = m.inventory_entry_id
  where e.inventory_revision_id = $1::uuid and m.is_active
  group by m.inventory_entry_id
)
select e.site_component_type,
       count(*) filter (where e.is_active)                                   as active_count,
       (array_agg(e.component_number order by e.sort_order, e.id)
          filter (where e.is_active))[1]                                     as first_number,
       (array_agg(e.component_number order by e.sort_order desc, e.id desc)
          filter (where e.is_active))[1]                                     as last_number,
       coalesce((array_agg(em.structure_part order by e.sort_order, e.id)
          filter (where e.is_active and em.structure_part is not null
                    and em.structure_part <> 'other'))[1], 'other')          as structure_part,
       (array_agg(em.category_id order by e.sort_order, e.id)
          filter (where e.is_active and em.inventory_entry_id is not null))[1] as category_id,
       count(*) filter (where e.is_active and em.has_confirmed)              as confirmed_count,
       count(*) filter (where e.is_active and em.inventory_entry_id is not null
                          and not em.has_confirmed)                          as pending_count,
       count(*) filter (where e.is_active and em.inventory_entry_id is null) as unmapped_count
from bridge_component_inventory_entries e
left join entry_mapping em on em.inventory_entry_id = e.id
where e.inventory_revision_id = $1::uuid
group by e.site_component_type
order by (array_agg(e.sort_order order by e.sort_order, e.id))[1],
         (array_agg(e.id::text order by e.sort_order, e.id))[1];
```

`bool_or(... = '已确认')` 即问题 5 定下的 `some` 口径；三个计数用
`em.inventory_entry_id is null / not null` 加 `has_confirmed` 三分，互斥且
覆盖全部启用构件。`package_id` 同 `category_id` 取法，为节省篇幅未列出。

### 快照一致性

`load_summary()` 由修订信息、`groups`、`blockers` 多条查询组成。契约声称
"每次拿到的汇总完整自洽"，因此这几条**必须在同一个只读事务里执行**，取同一个
快照；否则并发写入时 `groups` 与 `blockers` 可能来自不同时点，出现
"blocker 说有未映射构件，但分组里 `unmapped_count` 全为 0"这类自相矛盾。

写端点的响应同理：写入与随后的汇总重算要在同一事务内完成。

搜索使用 `like '%' || 转义(输入) || '%'`，保留现有 `.includes()` 的子串语义
（搜 `3-5` 会同时命中 `13-5#梁`）。用户输入中的 `%`、`_`、`\` 必须转义。

### 代码落点

| 文件 | 变化 |
| --- | --- |
| `ComponentInventoryRepository` | 新增 `load_summary()` / `load_group_entries()` / `search_entries()` / `compute_blockers()` |
| `ComponentInventoryModels` | 新增 `inventory_summary_json()`、`inventory_group_json()`；现有 `inventory_blockers_json()` 改造为计数加样本形状 |
| `ComponentInventoryRoutes` | 新增 2 条路由；`respond_inventory_outcome` 改为回传汇总形状 |

`compute_blockers()` 由汇总端点与 confirm 路径共用，是三号问题的机制性修复：
不依赖"两边都记得改"，而是只存在一份实现。

### 随之删除

- `GET /api/component-inventories/{revision_id}`（按 id 取全量）——前端无调用者
- `inventory_revision_json()` 及其调用的全量装配路径——失去调用者
- 前端 `fetchComponentInventory()` 及相关类型

`load_revision()` 中逐行的 `is_referenced` 相关子查询不删除，移入 ②，
只在实际翻到的一页（100 行）上执行。

## 前端实现

### 状态模型

| 现在 | 之后 |
| --- | --- |
| `revision`（含 5174 条 entries） | `summary`：`{ revision, groups[18], blockers }` |
| — | `groupEntries`：`{ group, page, total, entries[] }` |
| — | `searchResults`：`{ total, entries[] }` |
| `drafts`：全部 5174 条 | `drafts`：仅覆盖已加载的构件 |
| `search` / `expandedGroup` / `groupPage` / `editingEntryId` / `deactivatingId` / `pendingFocusId` / `busy` / `error` / `notCreated` / `plan` | 不变 |

作废的 `useMemo`：`entriesById`、`searchMatches`、`expandedGroupEntries`、
`pageEntries`、`pageCount`、`blockers`、`groupSummaries`。

保留并改数据源：

- `groupSections`：按 `structurePartOrder` 分段，改读 `summary.groups`
- `pendingMappingCount`：改读 `summary.groups` 的 `pending_count` 之和
  （现有实现就是从分组汇总累加，数据源性质不变）
- `individualBlockers`：改读 `summary.blockers.samples`。原先那段"过滤掉有待确认
  映射的构件"的客户端逻辑随之删除——服务端已按 ⑥ 拆好，样本里不含这批

### 取数时机

| 触发 | 请求 | 量级 |
| --- | --- | --- |
| 进页面 | `GET …/latest/summary` | 4.5 KB |
| 点"查看构件" | `GET …/entries?group=X&page=0&size=100` | 约 70 KB |
| 翻页 | 同上，换 `page` | 约 70 KB |
| 搜索框输入（防抖 250 ms） | `GET …/entries?number=Q&limit=50` | 约 35 KB |
| 任意写操作 | 写端点响应自带新汇总 | 4.5 KB |
| blocker 面板"定位" | 由 `position` 算页码后取该页 | 约 70 KB |

搜索防抖沿用评定树页的 250 ms `setTimeout` 写法。搜索与分组弹窗各自需要
独立的 loading 态——现为纯客户端过滤，尚无此概念。

### 组件拆分

`ComponentInventoryEditor.tsx` 现有 819 行，同时承担向导、汇总表、搜索、分组弹窗、
单行编辑、确认流程。按端点切分后边界自然浮现：

| 组件 | 职责 | 数据来源 |
| --- | --- | --- |
| `ComponentInventoryEditor` | 编排：拉汇总、分发、confirm 流程 | `summary` |
| `InventoryGroupTable` | 分组核对表 | `summary.groups` |
| `InventoryGroupDialog` | 分组弹窗：分页取数与翻页 | `groupEntries` |
| `InventoryEntrySearch` | 搜索框与结果表 | `searchResults` |
| `InventoryBlockersPanel` | "确认前还需处理 N 项"与定位 | `summary.blockers` |
| `InventoryEntryRow` | 单条构件行（编辑 / 停用 / 映射） | 传入 |
| `InventoryPlanPanel` | 向导 | 已存在，不动 |

`InventoryEntryRow` 必须抽出：现有 `renderEntryRow` 被搜索结果与分组弹窗共用，
拆组件后不能各留一份。

### 缓存

`resourceCache` 的 `inventory:{bridgeId}` 改为缓存 `summary`（4.5 KB），
stale-while-revalidate 语义不变。分组页与搜索结果不进缓存：它们是瞬时的，
缓存会在翻回时显示改动前的旧行。

## 错误处理与边界

| 情况 | 处理 |
| --- | --- |
| 客户端持有的是**已确认**版本的 id | **不是错误**。现有 `ComponentInventoryRepository.cpp:153` 起的逻辑：写入已确认版本会自动派生（或复用）一个以它为 baseline 的草稿，把写入落到草稿上。本次保留该行为，见下方专条 |
| revision id 确实不存在（台账被删） | ②③与写端点返回 404 `component_inventory_not_found`；前端重新拉汇总，不弹错 |
| 页码越界（删构件后该组变短） | 返回空 `entries` 与真实 `total`，前端据此夹取页码并重取 |
| 分组已空（最后一条被删） | `total` 为 0，前端关闭弹窗回到汇总表 |
| 搜索词含 `%` `_` `\` | 服务端转义后按字面匹配；搜 `%` 得到"匹配 0 个" |
| 台账为空 | `groups: []`，`blockers` 含 `inventory_empty`；前端显示向导 |
| 台账不存在 | 汇总端点返回 `component_inventory_not_found`，前端 `notCreated` 分支不变 |
| 规范目录未到达 | 汇总返回 category id 而非标签，前端仍显示 `—`，目录到达后重算 |
| confirm 被拦截 | 返回与汇总同形状的 `blockers`；`mutate()` 中 `details.blockers[0].entity_id` 改为 `details.blockers.samples[0]`，并用 `position` 算页码后跳转 |
| `group` 与 `number` 同时提供或同时缺失 | 400 |

### 写入已确认版本会派生草稿——响应里的 revision id 可能变

现有行为（本次保留）：对已确认版本发起写入时，仓储会查找以它为 `baseline_revision_id`
的草稿；没有就新建一个，并把全部构件与映射复制过去，然后把这次写入落到草稿上。

因此 ④ 的响应里 **`revision.id` 可能与请求 URL 里的不同**。契约要求：

- 客户端**必须采纳响应中的 `revision.id`**，后续 ②③ 与写操作都用它，
  不能继续沿用自己原先持有的 id；
- ②③ 对已确认版本是**正常读取**（该版本存在且有数据），不得返回 404；
- 前端 `mutate()` 在写回 `summary` 时一并更新持有的 revision id。

把"陈旧 id 一律 404"写进契约会破坏这条既有流程——已确认台账将再也无法通过
界面修改。

**并发写维持现状**：台账写端点不使用 `X-Edit-Lock-Token`（仅校对工作台使用），
两人同时改同一条构件仍为后写覆盖先写。此处明确记录，以免被误认为搬迁疏漏。

## 测试与验收

### 后端口径测试（连库，置于 `test_component_inventory_repository.cpp`）

| 用例 | 钉住 | 真实数据能覆盖吗 |
| --- | --- | --- |
| 33 孔桥的编号范围 | `last_number` 为 `33-2-50#支座`，非 `9-2-9#支座` | 能 |
| 含停用构件的分组 | 范围与 `active_count` 口径一致（有意变更之一） | **不能**，需构造 |
| 整组全部停用 | 范围为 `null`，该组仍返回 | **不能**，需构造 |
| **构件挂两个包的生效映射** | `active_count` 不被 join 放大；三个计数之和等于 `active_count` | **不能**，需构造 |
| **多映射下的 `some` 口径** | [待确认(A)、已确认(B)] 记入 `confirmed_count`，且不产生 blocker（有意变更之二） | **不能**，需构造 |
| `structure_part` 两级取值 | 首个映射为 `other` 时继续往后找；全为 `other` 则 `other` | **不能**，需构造 |
| **两组 `sort_order` 撞 0** | 分组顺序按 `(sort_order, id)` 稳定，不因平局抖动 | **不能**，需构造 |
| 重复编号 | 被检出 | **不能**（当前 0 组重复），需构造 |
| `position` 与分页 | 同一 window，不按 `is_active` 过滤，停用构件占位 | 部分 |
| 搜索转义 | 搜 `%` 命中 0 条；搜 `3-5` 同时命中 `13-5#梁` | 能 |
| blocker 两级拆分 | 待确认映射不进 `samples`，完全无映射进 `samples` | **不能**，需构造 |
| 写入已确认版本 | 派生草稿，响应 `revision.id` 与请求不同 | **不能**，需构造 |

第三列是这次评审补上的：**11 条里有 8 条在当前真实数据上不可见**。
`test_component_inventory_repository.cpp:148` 已经会创建两个生效映射，
多映射那几条在现有测试里就会触发。

### 校验一致性回归测试

构造"存在重复编号但映射均已确认"的台账：汇总端点必须报
`duplicate_component_number`，confirm 必须拒绝。此用例是 `compute_blockers()`
单一实现的守门人。

### 路由参数测试（不连库，置于 `test_component_inventory_routes.cpp`）

`page` / `size` / `limit` 的边界与默认值；`group` 与 `number` 的互斥校验。

### 搬迁验收：真实数据逐字段比对

该桥当前 18 组、5174 条，且**0 条停用构件、每构件仅 1 个生效映射、
`sort_order` 5174 个值全不重复、0 组重复编号**。因此：

> 以该桥真实数据分别运行现有前端的 `inventoryGroupSummaries` 与新 SQL，
> 18 组 × 9 字段必须逐字段完全相同。任一处不符即为搬迁错误。

**但这条验收是必要不充分的。** 上表第三列显示，本设计要处理的 11 种情形里有 8 种
在这份数据上根本不出现——包括多生效映射导致的计数放大这个最严重的问题。
换句话说：**一个把 `count(*)` 写错的实现，也能在这份真实数据上全绿通过。**

所以两类验收缺一不可：

- **真实数据比对**证明"常规路径没搬错"（规模真实、口径真实）；
- **构造数据单测**证明"边界改对了"（多映射、停用、平局、重复编号、
  已确认版本派生）。

一次性比对脚本置于 `scripts/dev/`，不进 CI；构造数据单测进 CI。

### 前端测试改造

`ComponentInventoryEditor.test.tsx` 中直接调用两个导出函数的 5 处用例
（第 106 / 109 / 117 / 264 / 290 行）连同 fixture 迁至后端口径测试。
组件层用例改为 mock 三个新端点，断言：进页面只发一次汇总请求；点"查看构件"
才发明细请求；写操作后汇总被替换且不再触发全量拉取。

后端口径测试通过后，删除前端的 `inventoryGroupSummaries` 与
`inventoryConfirmationBlockers`。

### 性能验收门槛

须以 **Release 构建**复测（Debug 关优化且开 `_ITERATOR_DEBUG_LEVEL=2`，
数千条构件的 JSON 序列化约慢 4 倍，Debug 下的数字无参考价值）：

| 指标 | 现在 | 门槛 |
| --- | --- | --- |
| 首屏响应体 | 3.4 MB | ≤ 10 KB |
| 单条构件改动的响应体 | 3.4 MB | ≤ 10 KB |
| 汇总 SQL | ——（原为 42 ms 取全量） | ≤ 100 ms（已实测 52 ms） |
| 打开一组（100 条） | 0（本地过滤） | ≤ 100 KB |

## 实施顺序建议

1. 后端：`compute_blockers()` 与重复编号规则，按问题 5 的 `some` 口径实现，
   接入 confirm，补一致性回归测试。此步单独可发布，先修好口径分歧。
2. 后端：汇总端点。先写**构造数据**的口径单测（多映射、停用、平局、
   全组停用、`structure_part` 两级取值），再跑真实数据比对脚本。
   顺序不能倒过来——真实数据全绿并不能说明多映射写对了。
3. 后端：②③明细端点，`is_referenced` 随之移入；`position` 与②共用同一 CTE。
4. 后端：写端点响应改形状；写入与汇总重算同事务；补"写入已确认版本派生草稿、
   响应 revision.id 变化"的用例。
5. 前端：状态模型与组件拆分，改用新端点；`mutate()` 采纳响应里的 revision id。
6. 删除失去调用者的全量路径与前端两个导出函数。
7. Release 构建下复测性能门槛。
