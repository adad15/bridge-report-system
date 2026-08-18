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

1. **台账页**首屏从 3.4 MB 降到 KB 级。
2. 台账页写操作的响应从 3.4 MB 降到 KB 级。
3. 统一确认校验口径：前端"确认前还需处理 N 项"与 confirm 的实际拦截由同一份
   服务端实现给出（含多映射下的 `some` 口径，见问题 5）。

**非目标**

- 不引入台账写操作的编辑锁（现状无锁，后写覆盖先写，本次保持）。
- 不改 `is_referenced` 的语义。
- 不改向导（`InventoryPlanPanel`）与 `generate_draft()` 的生成算法——空修订版
  渲染编辑器而非向导，正是为了不触碰它"已有草稿即 Conflict"的前提。
  （**例外**：派生草稿那条路径与每桥单草稿约束要修，见"既有缺陷"一节。
  它修的是 `generate_draft()` 一直假设、却无人保证的不变量，不改生成算法本身。）
- **不改造校对工作台**。它的构件选择器仍通过 `/latest` 拉取全量台账，
  3.4 MB 的问题在那条路径上依然存在，另开后续。
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

### 3. 重复编号由数据库唯一约束保证，前端那条规则是冗余的

`011_component_inventory_revisions.sql:93`：

```sql
unique (inventory_revision_id, site_component_type, component_number)
```

重复编号在数据库层面就不可能存在，`add_entry()` / `update_entry()` 另有主动检查。
因此：

- 正常 API 造不出重复；直接插库也会被约束拒绝；
- confirm 缺少重复检查只是逻辑上不完整，**不是可绕过的漏洞**；
- 想为它构造连库测试 fixture 也做不到——唯一约束会先拒绝插入。

**处置：删除 `duplicate_component_number` blocker**，以数据库唯一约束为权威。
保留一条永远为 0 的规则只会误导后来人。前端 `inventoryConfirmationBlockers`
里的重复检查随之删除。

（本设计早期版本据此断言"绕过前端可以确认掉带重复编号的台账"，该断言不成立，
已更正。）

服务端校验仍需集中：`inventory_empty` 与 `component_mapping_required` 由
`blocker_cte_sql()` 一份 SQL 片段同时服务汇总端点与 confirm 路径。前端聚合后拿不到
构件级数据，"定位"按钮所需的 `entity_id` 与 `position` 只能由服务端给出——
这仍是校验必须落在服务端的理由，只是与重复编号无关。

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
    "individual_total": 0,
    "by_code": { "inventory_empty": 0, "component_mapping_required": 0 },
    "samples": [
      { "code": "component_mapping_required", "entity_type": "inventory_entry",
        "entity_id": "…", "field_path": "mappings", "message": "…",
        "site_component_type": "梁", "position": 54 }
    ]
  }
}
```

按字段估算约 4.5 KB（未实测）。`groups` 按首个构件的 `(sort_order, id)` 排序
（见问题 1b），与向导顺序一致。

**字段可空性**（早期版本此处与 ⑦、SQL 自相矛盾，以本表为准）：

| 字段 | 类型 | 何时为空 |
| --- | --- | --- |
| `first_number` / `last_number` | `string \| null` | 整组构件全部停用 |
| `structure_part` | `StructurePart`，**非空** | 无映射或全为 `other` 时取 `"other"` |
| `standard_package_id` / `standard_component_category_id` | `string \| null` | 该组无任何生效映射 |

新增的前端类型必须照此写。现有 `InventoryGroupSummary` 的 `firstNumber: string`
是必填，不能照搬。

**同时提供按 id 的汇总**：`GET /api/component-inventories/{revision_id}/summary`。
否则端点是不对称的——②③ 按 `{revision_id}` 寻址，汇总却只能取 latest，
而"写后刷新协议"第 4、5 步要求用响应里的新 revision id 重新请求；只有
`/latest/summary` 时，别人并发生成一个草稿就会让它悄悄换成另一个修订版。
`/latest/summary` 退化为"先解析 latest 再转调按 id 版本"。

`get_latest_revision()` 的排序是 `(status='草稿') desc, revision_number desc`
——**草稿优先于已确认**，与 `revision_number` 无关。

> **禁止用 `get_latest_revision()` 解析 latest。** 它内部调用 `get_revision()`，
> 会把全部 entries 与 mappings 装配一遍——`/latest/summary` 若图省事复用它，
> 响应体虽小，后端仍完整跑一次 3.4 MB 的装配，优化只做了一半。
> 应新增轻量的 `find_latest_revision_id(bridge_id)`（只做那条 `order by … limit 1`
> 取 id），或在汇总合并语句里把 `target` CTE 改成按 `bridge_id` 直接选出该行。
> 代码落点与性能测试都要显式挡住这条捷径。

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

类型名 `InventoryGroupEntriesResponse`。`total` 是该组的构件总数（含停用），
用于翻页。

`entries[].mappings` **只返回 `is_active = true` 的映射**。现有全量装配不过滤，
会把历次改动积累的失效映射一并带出；而前端所有消费点（台账页 5 处、校对工作台
`DefectsSection` / `ComponentBindingWorkspace` 共 7 处）无一读取失效映射。
不过滤则单页体积不可控，性能门槛也就无从谈起。映射历史如日后需要，另设端点。

**`total` 不能只靠 `count(*) over ()`**：越界页返回零行，窗口函数便没有任何一行
可携带总数。需用独立 count 查询，或 CTE 左连接使无分页行时仍返回 `total`。

### ③ 编号搜索

`GET /api/component-inventories/{revision_id}/entries?number=<编号片段>&limit=50`

```json
{ "total": 5, "entries": [ { "…": "…", "site_component_type": "梁", "position": 54 } ] }
```

类型名 `InventorySearchResponse`。`total` 是**未截断**的命中数，用
`count(*) over ()` 在同一次查询里得出——界面上"匹配 N 个构件，显示前 50 个"
依赖真实命中数。**零命中时查询返回零行，没有任何一行携带 `total`，实现必须
显式规定 `rows.empty() ⇒ total = 0`。**

`entries[].mappings` 同 ②，只返回生效映射。

②③ 复用同一路由与同一 entry 序列化，但**响应 envelope 不同**（②有 `page`/`size`，
③没有），因此定义为两个类型，不宣称形状相同。

### ②③ 的参数边界

| 参数 | 规则 | 违反时 |
| --- | --- | --- |
| `page` | 整数，`0..INT32_MAX` | 400 |
| `size` | 整数，`1..200`，默认 100（同 `kEntriesPageSize`） | 超上限截为 200；`0`、负数、非整数返回 400 |
| `limit` | 整数，`1..100`，默认 50（同 `kMaxSearchResults`） | 超上限截为 100；`0`、负数、非整数返回 400 |
| `group` | 非空字符串 | 空或仅空白返回 400 |
| `number` | trim 后非空 | 空或仅空白返回 400 |
| `group` / `number` | 二选一 | 同时提供或同时缺失返回 400 |

`number` 若允许为空，`like '%%'` 会命中全部构件——这正是聚合要消除的那种响应。
偏移量 `page * size` 用 64 位计算并检查溢出。

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
仍在列表中可见并占位）。

②、③、blocker 样本是三条独立请求，**运行时不可能共用同一个 CTE 实例**；需要共用的是
**窗口定义本身**。落法三选一（实施计划里定）：抽成生成该 CTE 文本的 C++ 函数、
建数据库 view、或 repository 私有查询构造器。三处各写一遍必然漂移，"定位"会跳错页，
因此三处都要有断言 `position` 一致的测试。

注意这与"编号范围只算启用构件"是两套不同的过滤，不可混用：
范围是**展示口径**（本次修正过），分页是**定位口径**（保持原样）。

### ⑥ blocker 样本的两级拆分

现有界面把 blocker 分两类显示：有待确认映射的构件汇总成一行"N 个构件的规范映射
待确认"加一键确认按钮；其余逐条列出并带"定位"按钮。服务端照此拆分：

- `samples` **只含**完全没有生效映射的构件，以及 `inventory_empty`；
- 有待确认映射的那批**不进 `samples`**，界面上那一行的数字取自
  `groups[].pending_count` 之和（现有 `pendingMappingCount` 就是这么算的，
  数据源不变，只是从客户端汇总改为读汇总响应）。

**计数关系必须写死，否则会双重计数**：

```
pending_total    = sum(groups[].pending_count)
unmapped_total   = sum(groups[].unmapped_count)
individual_total = (inventory_empty ? 1 : 0) + unmapped_total
total            = pending_total + individual_total
remaining        = individual_total - samples.length
```

`by_code.component_mapping_required` = `pending_total + unmapped_total`
（按问题 5 统一后的 `some` 口径：缺少已确认生效映射的启用构件）。

**"……其余 N 项"必须用 `individual_total` 减样本数，不能用 `total`。**
反例：100 个待确认、0 个无映射时 `total = 100`、`samples = []`，用 `total`
会让页面同时显示"100 个构件的规范映射待确认"和"……其余 100 项"，同一批构件数了
两遍。现有前端用的是 `individualBlockers.length`，本设计早期版本抄错了。

`samples` 上限取 30（现有 `kMaxIndividualBlockers` 的值）。

**`inventory_empty` 的展示载体**：它以 `entity_type: "inventory_revision"` 进入
`samples`。现有渲染逻辑对 `entity_type !== "inventory_entry"` 的条目不显示"定位"
按钮，天然适配，无需前端分支。否则空台账会出现"还需处理 1 项"却列不出任何内容。

**`by_code` 是固定字段**（`inventory_empty`、`component_mapping_required`），
不是动态键；无对应 blocker 时取 0，便于前端类型化。

**样本顺序**：`inventory_empty` 在前，其余构件按 `(sort_order, id)` 升序，
保证截断结果稳定可测。

### ⑦ `structure_part` 与规范类别的取值规则

这两个是展示字段，不套用 `some`，规则分两级，复刻现有行为：

**构件级**：取该构件**首个**生效映射（按 `created_at, id` 排序，与现有映射查询
一致）的 `structure_part` / `standard_component_category_id` / `standard_package_id`。

**分组级**：`structure_part` 取该组内**首个（按 `sort_order, id`）取值非 `other`
的启用构件**；全组都是 `other`、都无映射、或整组停用时均为 `other`（此字段非空，
见 ① 的可空性表）。现有代码
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
| **汇总合并语句**（revision + groups + blockers 一条出） | 首次 92.4 ms，随后 46.4 / 50.0 ms |
| 其中 groups 聚合单独跑 | 76.0 ms |
| 组内序号 | 9.9 ms |
| blocker 片段（无映射构件 + 空台账判定） | 3.7 ms |
| 编号搜索 | 8.4 ms |

性能门槛对的是**合并语句**那一行（这才是运行时真正执行的东西），其余几行是
拆开度量、用于定位瓶颈。合并语句的返回体实测 **6705 字节**（18 组、0 条 blocker
样本）。

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
       (array_agg(em.package_id order by e.sort_order, e.id)
          filter (where e.is_active and em.inventory_entry_id is not null))[1] as package_id,
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

`package_id` 与 `category_id` **用相同的过滤与排序**，保证两者来自同一个构件。

`bool_or(... = '已确认')` 即问题 5 定下的 `some` 口径；三个计数用
`em.inventory_entry_id is null / not null` 加 `has_confirmed` 三分，互斥且
覆盖全部启用构件。

### 快照一致性：汇总合成一条语句

契约声称"每次拿到的汇总完整自洽"。修订信息、`groups`、`blockers` 若拆成多条查询，
**仅仅放进同一个事务是不够的**：PostgreSQL 默认隔离级别是 READ COMMITTED，
该级别下同一事务内的每条 SELECT 各自取一个新快照，仍可能看到不同的已提交状态，
于是出现"blocker 说有未映射构件，但分组里 `unmapped_count` 全为 0"这类自相矛盾。

**决定：合成一条 SQL**，靠单语句快照保证一致，不使用 REPEATABLE READ——
只读路径因此不必引入序列化失败的错误映射与重试策略。

这与"blocker 规则只有一份实现"看似冲突：真把 blocker 逻辑写进汇总大 SQL，
它就在那里长出第二份，正好毁掉本设计声称要防的那件事。**解法是让共用的单位是
SQL 片段而不是查询执行**：

- 由 `blocker_cte_sql()` 产出具名 CTE 的 SQL **文本**（判定"启用且无已确认生效
  映射的构件"与"启用构件数为 0"）。命名刻意不叫 `compute_blockers()`——
  后者听起来像会执行查询并返回结果，而它只产出文本；
- 汇总语句把它作为 CTE 嵌进自己那条 SQL；
- confirm 语句嵌入同一段文本；
- 规则文本只有一处来源，两个调用点又各自处在单语句快照内。

"校验一致性回归测试"仍然保留，用来钉住两处确实嵌的是同一段文本。

**已验证可行**：按此形态写出的完整语句（`target` / `entry_mapping` / `numbered` /
`grouped` / `blocker_entries` 五个 CTE + `json_build_object` 组装）在真实数据上
一次返回完整汇总，见下方性能表。

写端点同理：写入与随后的汇总重算必须在同一事务内完成，且：

- summary 在提交**前**计算，但必须等提交确认后才返回；
- 提交失败不得返回已算好的成功响应；
- confirm 被 blocker 拒绝时，返回事务内算出的结果；
- 派生草稿时，summary 必须基于**派生后**的 revision id。

这不是改 `respond_inventory_outcome()` 就能做到的。现有仓储写方法的流程是
"执行写入 → `finish()` 提交 → 事务外 `get_revision()` 重读 → 返回"，
要改成"执行写入 → 同事务算 summary → 提交 → 返回"，**所有写方法都要重构**。

汇总构造函数须接受一个 **executor**（事务或连接）参数：只读路径传连接，
写路径传当前事务，从而复用同一段 SQL 而不产生嵌套事务，也不出现两套规则。

**`ComponentInventoryOutcome` 必须携带完整 entry，不能只带 `entry_id`**——
事务外再按 id 加载 entry，恰好破坏上面这条同快照要求。见"后端领域模型"。

搜索使用 `like '%' || 转义(输入) || '%'`，保留现有 `.includes()` 的子串语义
（搜 `3-5` 会同时命中 `13-5#梁`）。用户输入中的 `%`、`_`、`\` 必须转义。

### 代码落点

| 文件 | 变化 |
| --- | --- |
| `ComponentInventoryRepository` | 新增 `load_summary(executor, revision_id)` / `find_latest_revision_id()` / `load_group_entries()` / `search_entries()` / `blocker_cte_sql()`（产出 CTE 文本，不执行查询） |
| `ComponentInventoryModels` | 新增 `inventory_summary_json()`、`inventory_group_json()`、`inventory_entry_json()`、`located_entry_json()`；现有 `inventory_blockers_json()` 改造为计数加样本形状 |
| `ComponentInventoryRoutes` | 新增 3 条路由；`respond_inventory_outcome` 改为回传汇总形状 |
| migration | 新增每桥单草稿的部分唯一索引（见"既有缺陷"一节） |

### 后端领域模型

现有 `ComponentInventoryOutcome` 只有 `status` / `revision` / `entry_id` / `blockers`，
承载不了新响应，且 `entry_id` 会逼迫在事务外重新加载 entry、破坏同快照要求。改为：

```cpp
struct ComponentInventoryOutcome {
    ComponentInventoryStatus status;
    std::optional<inventory::InventorySummary> summary;
    std::optional<inventory::InventoryEntry> entry;   // 完整 entry，非 id
};
```

需新增的模型：`InventoryRevisionSummary`、`InventoryGroupSummary`、
`InventoryBlockerSummary`、`InventorySummary`、`InventoryEntryPage`、
`InventorySearchResult`、`LocatedInventoryEntry`（`InventoryEntry` 已有
`site_component_type`，只需再加 `position`）。

约定：`total` / `page` / `size` / `position` 用 `int64_t`；可空字段用
`std::optional`；`by_code` 是固定字段的 struct 而非 map（见 ⑥）；
`samples` 截断前的总数记在 `InventoryBlockerSummary` 上。

**不要复用 `InventoryRevision` 并把 `entries` 留空**——摘要版与全量版会被混用，
而 `/latest` 仍在返回全量版。

entry / mapping 的序列化目前嵌在 `inventory_revision_json()` 内部。因 `/latest`
保留，该函数不删，但需**把 entry 与 mapping 的序列化抽成独立函数**供分页、搜索、
写响应复用，不是重新实现一份。

`blocker_cte_sql()` 产出的 CTE 文本由汇总语句与 confirm 语句共同嵌入，
是三号问题的机制性修复：不依赖"两边都记得改"，而是只存在一份规则文本。

### 随之删除

| 端点 | 处置 | 理由 |
| --- | --- | --- |
| `GET /api/component-inventories/{revision_id}` | **删除** | 全仓无调用者 |
| `GET /api/bridges/{id}/component-inventories/latest` | ~~保留~~ → **已删除（2026-08-18）** | 见下 |

前端 `fetchComponentInventory()` 及相关类型随第一条一并删除。删除公开端点属
**breaking change**，需在发布说明中标注；本系统前后端同仓库同发布，不做弃用观察期。

**`/latest` 必须保留——它不止台账页在用。** 校对工作台有三处调用
`fetchLatestComponentInventory()`：

- `review/binding/ComponentBindingWorkspace.tsx:253`、`:426`
- `review/components/DefectsSection.tsx`

它们用全量 `entries` 构建构件选择器（筛选条件是
`entry.is_active && entry.mappings.some(m => m.is_active)`）。删掉 `/latest`
会直接打断病害绑定流程。

**因此 `inventory_revision_json()` 与 `load_revision()` 不能删除**，本设计早期
版本"失去调用者"的说法不成立。

**遗留问题（本次不处理，另开后续）**：校对工作台仍会为这两个选择器拉取
同一份 3.4 MB。它需要的是"按类别/编号检索构件"，与 ③ 的形状接近，
但改造涉及病害绑定流程，超出本次范围。本次结束后台账页不再触发全量装配，
`/latest` 的调用量随之下降，但单次体积不变。

`load_revision()` 中逐行的 `is_referenced` 相关子查询同样保留，同时在 ② 中
以相同语义实现，只在实际翻到的一页（100 行）上执行。

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

### 写成功后的刷新协议

"不进 resourceCache"只能防跨挂载的陈旧，**防不住当前 React 状态里的
`groupEntries` / `searchResults` / `drafts` / `editingEntryId`**。只把响应里的
`entry` 就地打补丁是不够的：删除与批量确认根本不带 `entry`，改类别还会让构件
换组、改变分页集合。会看到的现象包括：删除后汇总已减少而弹窗仍列着被删行；
批量确认后汇总显示已确认而已打开的行仍是待确认；改编号后当前搜索结果已不再匹配。

写成功后按固定顺序执行（**顺序不能颠倒**，否则会用旧 revision id 去请求）：

1. 用响应中的 `summary` 替换当前 `summary`；
2. 更新持有的 `revision.id`（可能因派生草稿而改变）；
3. 丢弃旧 revision 的 `drafts`，取消其所有在途请求；
4. 分组弹窗若开着，用新 revision id 重新请求当前分组与页码；
5. 搜索词若仍有效，用新 revision id 重新执行搜索；
6. 按新 `total` 夹取页码，越界则再请求一次；
7. 该组 `total` 归零则关闭弹窗。

### 异步响应乱序

250 ms 防抖只减少请求数，**不保证返回顺序**。搜 `3` 与搜 `33` 两个请求，
若后者先返回，前者的旧结果会把页面覆盖回去；分组快速翻页同理。

必须落实其一：`AbortController` 取消旧请求，或单调递增的请求序号，或响应落地前
比对 `(revisionId, group/number, page, size)` 是否仍与当前状态一致。
revision id 因写操作变化时，旧 revision 的在途响应一律丢弃。

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

### 单行的多映射口径必须跟汇总一致

汇总与 blocker 已按问题 5 改成 `some`，但 `renderEntryRow`（`:481`）和
`confirmExistingMapping` 仍是 `mappings.find(m => m.is_active)` ——只看首个。
[待确认(A)、已确认(B)] 的构件于是：汇总记入 `confirmed_count`，
**行上却显示"待确认映射"并允许再确认一次 A**。用户在一个"全部已确认"的分组里
看到一行待确认。

`InventoryEntryRow` 改为：

- 行状态用 `hasConfirmed = mappings.some(m => m.is_active && m.confirmation_status === '已确认')`；
- "确认映射"按钮**仅在 `!hasConfirmed` 时出现**，且确认的是一条明确选定的待确认
  生效映射（按稳定顺序取第一条），不再拿"首个生效映射"原样重交；
- 契约配合：②③ 与写响应返回的 `mappings` 按 `created_at, id` **稳定排序**，
  否则"第一条"在两次请求间可能不同。

**已知限制（本次不做）**：一个构件挂多个规范包的生效映射时，界面只展示代表映射，
没有编辑第二个包映射的入口。现网每构件均为 1 个映射，暂不构成实际问题；
需要时另开。

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
| 修订版存在但一条构件都没有 | `groups: []`，`blockers` 含 `inventory_empty`；**前端渲染编辑器（可手工新增），不是向导** |
| 有构件但全部停用 | `groups` **非空**、每组 `active_count` 为 0，`blockers` 同样含 `inventory_empty` |
| 台账不存在 | 汇总端点返回 `component_inventory_not_found`，前端 `notCreated` 显示向导——这是唯一显示向导的条件 |
| 规范目录未到达 | 汇总返回 category id 而非标签，前端仍显示 `—`，目录到达后重算 |
| confirm 被拦截 | 返回与汇总同形状的 `blockers`；定位逻辑见下方专条，**不得盲取 `samples[0]`** |
| `group` 与 `number` 同时提供或同时缺失 | 400 |

### `inventory_empty` 的两种成因不可混为一谈

后端 confirm 里 `inventory_empty` 的实际口径是**启用构件数为 0**，它覆盖两种情形：

- 修订版一条构件都没有 → `groups: []`
- 有构件但全部停用 → `groups` 非空，每组 `active_count` 为 0

**前端不能用 `groups.length === 0` 判断"空台账"**，否则第二种情形会走错分支。
本次不新增 error code（后端口径不变），但前端必须分别处理这两种渲染。

### 空修订版不能显示向导，否则用户走进死路

`generate_draft()` 只要发现该桥已有草稿就返回 Conflict
（`ComponentInventoryRepository.cpp:293`）。若空修订版显示向导，就会出现：
删光构件 → 页面显示向导 → 用户点生成 → 后端发现草稿仍在 → 409 —— 用户无法自救。

现有前端只在 `component_inventory_not_found` 时显示向导，本来没有这个问题；
本设计早期版本写的"台账为空显示向导"会**引入**它。已按上表更正：
修订版存在就渲染编辑器，允许手工新增构件。生成逻辑保持不动（见非目标）。

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

### 既有缺陷：一桥可以派生出两条草稿分支（本次修复）

派生逻辑按 baseline 查找现有草稿：

```sql
where bridge_id=$1::uuid and status='草稿' and baseline_revision_id=$2::uuid
```

而 `generate_draft()` 的检查是 `where bridge_id=$1 and status='草稿' limit 1`
——**有任何草稿就 409**。两条路径对"每桥至多一个草稿"这个不变量的假设相反，
而 `bridge_component_inventory_revisions` 只有 `unique (bridge_id, revision_number)`，
**数据库并不保证它**。

后果：桥上已有基于 R2 的草稿时，客户端拿着较旧的已确认版本 R1 写入，按 baseline
找不到匹配，于是**新建第二条以 R1 为 baseline 的草稿**。
`get_latest_revision()` 的 `order by (status='草稿') desc, revision_number desc limit 1`
只会挑中其中一条，另一条从此隐形。SWR 缓存（先渲染上次结果再后台校验）与直接
调用 API 都能触发。

这是既有缺陷，但本设计让它更易撞上，故一并修复：

1. **新增 migration**：
   ```sql
   create unique index ux_component_inventory_single_draft
     on bridge_component_inventory_revisions (bridge_id)
     where status = '草稿';
   ```
   让两条代码路径已经假设的不变量真正成立。现网数据全库仅 1 个修订版，不冲突。
2. **派生限定于最新已确认版本**：在桥级锁内（`generate_draft()` 已有的
   `select ... from bridges ... for update`）检查；若该桥已存在 baseline 不同的草稿，
   返回 409 `inventory_revision_superseded`，客户端重新拉取汇总后再操作。

因涉及 schema，本次范围包含**一条 migration**。

**新错误码需要贯通整条链路。** 现有 `respond_inventory_outcome()` 把
`ComponentInventoryStatus::Conflict` 固定序列化为
`component_inventory_conflict`／"构件台账已变化或编号重复。"，没有位置放新码。
因此要一并改：

- `ComponentInventoryStatus` 新增 `Superseded`（或给 outcome 加结构化的
  `error_code` / `message`，二选一，实施计划里定）；
- responder 增加对应分支，返回 409 + `inventory_revision_superseded`；
- 前端 `componentInventoryErrorMessage` 的码表补这一条；
- 前端 `mutate()` 增加分支：收到该码时**重新拉取 latest 汇总**并采纳新的
  revision id，而不是只弹一句错误——用户此时手里的 id 已经不可写，
  只提示会让他反复点同一个按钮。

**并发写维持现状**：台账写端点不使用 `X-Edit-Lock-Token`（仅校对工作台使用），
两人同时改同一条构件仍为后写覆盖先写。此处明确记录，以免被误认为搬迁疏漏。

### confirm 失败后的定位：不能取 `samples[0]`

`samples[0]` 未必可定位：按 ⑥ 的排序 `inventory_empty` 排在最前，而它的
`entity_type` 是 `inventory_revision`，没有 `site_component_type` 与 `position`；
若 blocker 全是待确认映射，`samples` 甚至为空。

正确流程：

1. 先刷新汇总（confirm 失败说明本地状态已过期）；
2. 用 `samples.find(s => s.entity_type === 'inventory_entry' && s.position != null)`
   挑可定位项；
3. **挑不到就只展示错误信息，不计算页码、不跳转**。

（早期版本写的"取 `samples[0]` 用 `position` 算页码"必然在空台账时取到没有
`position` 的那条——这是 ⑥ 的排序规则与定位逻辑没有对齐。）

### `position` 是尽力定位，不是保证

无锁并发下，blocker 生成之后、用户点"定位"之前，别人可能增删或移动构件，
使该 `position` 失效。契约明确：

- `position` 是**汇总生成时刻的快照位置**；
- 前端按它取页后若在返回结果里找不到 `entity_id`，重新拉一次汇总，
  读取新样本的 `site_component_type` / `position` 再试一次；
- 仍找不到则提示"该问题已被其他操作改变或解决"，**不无限重试**。

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
| `position` 与分页 | 同一 window，不按 `is_active` 过滤，停用构件占位 | 部分 |
| 搜索转义 | 搜 `%` 命中 0 条；搜 `3-5` 同时命中 `13-5#梁` | 能 |
| **搜索零命中** | `total = 0`（窗口函数无行可携带总数） | 能 |
| **分页越界** | 返回空 `entries` 但 `total` 仍为真实值 | 能 |
| blocker 计数关系 | `total = pending_total + individual_total`；100 待确认 0 无映射时 `remaining` 为 0 而非 100 | **不能**，需构造 |
| `inventory_empty` 两种成因 | 无条目（`groups: []`）与全部停用（`groups` 非空、`active_count` 全 0）都产生该 blocker | **不能**，需构造 |
| 明细只含生效映射 | 构件有失效历史映射时，②③ 不返回它们 | **不能**，需构造 |
| 写入已确认版本 | 派生草稿，响应 `revision.id` 与请求不同 | **不能**，需构造 |
| **同桥插入第二条草稿** | 被部分唯一索引拒绝 | **不能**，需构造 |
| **已有 R2 草稿时写 R1** | 返回 409 `inventory_revision_superseded` | **不能**，需构造 |
| **并发从不同已确认版本派生** | 桥级锁串行化，最终至多一条草稿 | **不能**，需构造 |
| **`/latest/summary` 不装配全量** | 不经 `get_revision()`；断言未加载 entries | **不能**，需构造 |

第三列是评审补上的：**19 项里有 14 项在当前真实数据上不可见**
（能覆盖 4 项、部分覆盖 1 项、需构造 14 项）。
`test_component_inventory_repository.cpp:148` 已经会创建两个生效映射，
多映射那几条在现有测试里就会触发。

**重复编号不再有测试项**：数据库唯一约束
`(inventory_revision_id, site_component_type, component_number)` 使其无法构造，
该 blocker 已按问题 3 删除。不得为构造该 fixture 而临时移除唯一约束。

### 校验一致性回归测试

构造"部分构件无生效映射"与"全部构件停用"两种台账：汇总端点报出的 blocker
与 confirm 的拒绝理由必须一致（同样的 code、同样的构件集合）。此用例是
`blocker_cte_sql()` 单一来源的守门人——将来谁把两边拆成两份规则，它就会红。

（早期版本用重复编号做这个用例，因唯一约束无法构造，已替换。）

### 快照一致性与并发测试

- 汇总查询进行期间并发更新映射：`groups` 与 `blockers` 必须来自同一快照，
  不得出现"blocker 报未映射构件但 `unmapped_count` 全为 0"；
- 写事务计算 summary 期间另一写请求到达：等待或冲突，不得读到中间态；
- 提交失败时不得返回已算好的成功 summary。

### 前端刷新与乱序测试

- 删除构件后重新拉取当前分组页，弹窗不再显示被删行；
- 改类别后构件换组，原分组重新拉取；
- 批量确认后分组与搜索结果都重新拉取；
- 写入已确认版本后，用响应里的**新** revision id 重新拉取；
- 页码越界时夹取并二次请求；
- 分组 `total` 归零时关闭弹窗；
- 旧搜索响应晚于新搜索返回时，不得覆盖新结果。

### 路由参数测试（不连库，置于 `test_component_inventory_routes.cpp`）

按 ②③ 的参数边界表逐项覆盖，不能只写"边界与默认值"：

- `size=0`、`limit=0`、负数 → 400
- `page=abc`、`size=1.5` 等非整数 → 400
- 超出 `int32` 范围的 `page` → 400；`page * size` 用 64 位计算并检查溢出
- `size=999` → 截为 200；`limit=999` → 截为 100
- `group=`（空）、`group=   `（仅空白）→ 400
- `number=`（空）、`number=   `（仅空白）→ 400，**不得退化成 `like '%%'`**
- `group` 与 `number` 同时提供、同时缺失 → 400
- 默认值：`page=0`、`size=100`、`limit=50`

### 搬迁验收：真实数据逐字段比对

该桥当前 18 组、5174 条，且**0 条停用构件、每构件仅 1 个生效映射、
`sort_order` 5174 个值全不重复、0 组重复编号**。因此：

> 以该桥真实数据分别运行现有前端的 `inventoryGroupSummaries` 与新 SQL，
> 18 组必须逐字段完全相同。任一处不符即为搬迁错误。

**比对前必须先定义投影**——新旧字段不是一一对应的：

| 旧（`inventoryGroupSummaries`） | 新（汇总端点） | 比对方式 |
| --- | --- | --- |
| `siteComponentType` / `activeCount` / `confirmedCount` / `pendingCount` / `unmappedCount` | 同名字段 | 直接比 |
| `structurePart` | `structure_part` | 直接比 |
| `firstNumber` / `lastNumber` | `first_number` / `last_number` | 直接比；新值可为 `null`，比对时按界面口径折算成 `—` |
| `mappingLabel`（由 catalog 解析得到的中文标签） | `standard_package_id` + `standard_component_category_id` | **不能直接比**。脚本须用同一份 catalog 把新接口的 package/category 解析成标签后再比，或改比旧逻辑内部选出的 package/category |

**这条验收是必要不充分的。** 上表第三列显示，本设计要处理的 19 项里有 14 项
在这份数据上根本不出现——包括多生效映射导致的计数放大这个最严重的问题。
换句话说：**一个把 `count(*)` 写错的实现，也能在这份真实数据上全绿通过。**

所以两类验收缺一不可：

- **真实数据比对**证明"常规路径没搬错"（规模真实、口径真实）；
- **构造数据单测**证明"边界改对了"（多映射、停用、平局、空台账两种成因、
  明细只含生效映射、已确认版本派生）。

一次性比对脚本置于 `scripts/dev/`，不进 CI；构造数据单测进 CI。

### 前端测试改造

`ComponentInventoryEditor.test.tsx` 中调用 `inventoryGroupSummaries` 的 4 处用例
（第 106 / 109 / 117 / 264 行）连同 fixture 迁至后端口径测试。

第 290 行 `reports duplicate numbers before confirmation` **直接删除**——它测的是
按问题 3 已删除的重复编号规则，没有可迁移的去处。
组件层用例改为 mock 三个新端点，断言：进页面只发一次汇总请求；点"查看构件"
才发明细请求；写操作后汇总被替换且不再触发全量拉取。

后端口径测试通过后，删除前端的 `inventoryGroupSummaries` 与
`inventoryConfirmationBlockers`。

### 性能验收门槛

须以 **Release 构建**复测（Debug 关优化且开 `_ITERATOR_DEBUG_LEVEL=2`，
数千条构件的 JSON 序列化约慢 4 倍，Debug 下的数字无参考价值）：

**测量口径**：响应体按 **UTF-8 原始 JSON 字节数**计，不含 HTTP 头，不启用压缩。
SQL 耗时为热缓存下的 `Execution Time`，不含事务与序列化开销。

| 指标 | 现在 | 门槛 |
| --- | --- | --- |
| 首屏响应体（汇总，`samples` 取满 30 条） | 3.4 MB | ≤ 12 KB（0 样本时已实测 6705 字节，30 条样本约再加 3 KB） |
| 写响应，不带 `entry`（删除 / 批量确认 / confirm） | 3.4 MB | ≤ 12 KB |
| 写响应，带一条 `entry` | 3.4 MB | ≤ 20 KB |
| 汇总合并语句（18 组，5174 条） | ——（原为 42 ms 取全量） | ≤ 150 ms（已实测热态 46–50 ms、首次 92 ms） |
| 打开一组（100 条，仅生效映射） | 0（本地过滤） | ≤ 100 KB |

写响应拆成两档，是因为带 `entry` 时多出一条完整构件；`entry` 只含生效映射
（见 ②），否则历史映射会让体积不可控。

## 实施顺序建议

0. migration：每桥单草稿的部分唯一索引；派生逻辑限定于最新已确认版本，
   baseline 不符返回 409；贯通 `Superseded` 状态、responder、前端码表与重拉分支；
   补三条守门测试（索引拒绝第二条草稿、R2 草稿在时写 R1 报
   `inventory_revision_superseded`、并发派生最终至多一条）。
   修既有缺陷，与聚合改造互不依赖，可先行发布。
1. 后端：`blocker_cte_sql()` 按问题 5 的 `some` 口径实现为 CTE 文本，接入 confirm，
   补一致性回归测试；前端删除 `duplicate_component_number` 检查，
   并把 `InventoryEntryRow` 的状态判定改成 `some`。此步单独可发布。
2. 后端：汇总端点。先写**构造数据**的口径单测（多映射、停用、平局、
   全组停用、`structure_part` 两级取值），再跑真实数据比对脚本。
   顺序不能倒过来——真实数据全绿并不能说明多映射写对了。
3. 后端：②③明细端点，`is_referenced` 随之移入；`position` 的窗口定义与②
   共用同一份 SQL 片段来源，三处各加断言 `position` 一致的测试。
4. 后端：写端点响应改形状；写入与汇总重算同事务；补"写入已确认版本派生草稿、
   响应 revision.id 变化"的用例。
5. 前端：状态模型与组件拆分，改用新端点；`mutate()` 采纳响应里的 revision id。
6. 删除按 revision id 的全量 GET 与前端两个导出函数。
   **`/latest` 与 `load_revision()` / `inventory_revision_json()` 保留**——
   校对工作台仍在用。
7. Release 构建下复测性能门槛。
