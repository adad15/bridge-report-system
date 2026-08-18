# 构件绑定改用按需检索

- 日期：2026-08-17（2026-08-18 两轮评审后修订）
- 状态：已按复审修订，待复审确认
- 相关模块：校对工作台构件绑定（`ComponentBindingWorkspace`）、构件台账检索端点
- 评审记录：`…-design-review.txt`（初审）、`…-design-rereview.txt`（复审）

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

## 修订说明：前两稿改掉了什么

保留此节，供日后回看时知道为什么这样设计。

**初稿两处错误**（初审指出）：

- 称候选 id"本来就是服务端在同一次查询里算出来的，就地 join"。实际候选 id 来自
  `parsed_result_json`（`ImportBindingRepository.cpp:80-82`），而 `overview()` 另外把
  整份台账加载一遍再丢掉——只用了 `revision.has_value()`。按初稿实现只会减少浏览器
  下载，后端照样读五千多条。
- 缺了版本 id 主线：搜索端点按 `revision_id` 寻址，而首屏台账请求一删，前端手里就
  没有这个 id 了。

**二稿四处问题**（复审指出）：

- 精简 DTO 会让批量替换预览**全部判成 `not_in_inventory`**：`replacePreview.ts:55`
  有 `if (!entry.is_active) continue;`，而精简对象没有这个字段，`!undefined` 为真，
  于是每条都被跳过。不报错、不崩溃，安静地全错。
- 补了 `inventory_revision_id` 并不足以消除版本漂移：年度是在**写操作时**才锁版本的
  （`attach_revision_to_pending_year` 只在 `:315/:413/:851` 调用），进面板时可能尚未
  锁定。
- `resolve_confirmed_revision()` 有 5 个调用点，其中 4 个在写路径
  （`:314/:412/:567/:850`）需要完整 `InventoryRevision` 做 `validate_target()`，
  不能直接改返回类型。
- 批量端点与候选展示对象都漏了"存在生效映射"这半个过滤条件。

## 目标与非目标

**目标**

1. 绑定面板首屏不再拉取整份台账，概览到达即可渲染。
2. 后端概览也不再完整加载台账，改为按需要的 id 定向查询。
3. 行内搜索改走服务端按需检索，且只返回启用且有生效映射的构件。
4. 准确的台账版本贯穿概览、搜索、批量替换与写操作，并在版本变化时**明确报错**而不是
   静默改用新版本。
5. 顺带修正三处既有缺陷（见下）。
6. 删除面板内因此作废的代码。

**非目标**

- **不改造 `DefectsSection` 的手动添加病害表单**，`/latest` 端点因此本轮不删。
- **不改绑定的业务判定规则**。`binding_eligible` 不承担部件名与规范类别的兼容性
  校验，那仍由 `validate_target()` 在正式绑定时执行（见契约 ②）。
- 不改绑定、标记缺失、批量替换的业务语义；批量替换的归一化、存在性与歧义判定不变。

## 顺带修正的三处既有缺陷

都不是本次引入的，但新代码会把它们固化，所以必须一起处理。

### 缺陷一：确认版本解析用了草稿优先的接口

`resolve_confirmed_revision()` 在年度未锁定版本时调用 `get_latest_revision(bridge_id)`，
后者排序是 `order by (status='草稿') desc, revision_number desc`——**草稿优先**。取到
草稿后再判 `status == '已确认'` 必然不成立。

**后果**：桥上同时存在已确认版本和一个新草稿时，绑定面板误报"该桥构件台账尚未确认"，
整块绑定 UI 被挡住。

**修正**：未锁定时直接查该桥最新的**已确认**版本
（`where bridge_id=$1 and status='已确认' order by revision_number desc limit 1`），
不经过草稿优先的接口。

### 缺陷二：批量替换读的是桥级 `/latest`

同一个草稿优先排序，会让批量替换预览读到与绑定校验不同的版本：年度锁定 R2 →
有人改台账派生出草稿 R3 → `/latest` 返回 R3 → 预览按 R3 算而绑定按 R2 校验 →
前端显示"可以绑定"，后端却拒绝。

**修正**：改用按导入记录取数的端点（契约 ③），前端不参与选版本。

### 缺陷三：年度未锁版本时的跨请求漂移

年度是在写操作时才锁版本的。若进面板时未锁定：

1. 概览解析出最新已确认版本 R2，但**没有把 R2 固定下来**；
2. 行内搜索按 R2；
3. 其他人确认了 R3；
4. 批量取数重新解析 → R3；
5. 正式绑定再解析一次，并可能把 R3 写进年度；
6. 用户看到的候选来自 R2，校验却按 R3。

**修正**：见下节"版本一致性"。

## 版本一致性：`expected_revision_id`

仅仅返回 `inventory_revision_id` 不够——响应到达时版本可能又变了。改为**乐观校验**：

1. 行内绑定、批量绑定、批量替换取数，请求都携带概览返回的
   `expected_revision_id`。前端不是在"选版本"，而是在**声明本次操作所依据的版本**。
2. 后端在事务里校验：
   - 年度已锁定版本：必须等于预期版本；
   - 年度未锁定：确认预期版本仍是有效的最新已确认版本，然后**锁定它**；
   - 不一致：不继续操作，返回 409 `component_inventory_revision_changed`。
3. 前端收到该错误码：重新加载概览，提示"台账版本已变化，已为你刷新"，并清除搜索
   结果与批量缓存。

这条规则的价值在于**把静默的错误变成显式的冲突**。没有它，用户会看到"可以绑定"
然后被拒绝，且不知道为什么。

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

新增 `?binding_eligible=true`，SQL 至少满足：

```sql
e.is_active
and exists (
  select 1 from bridge_component_standard_mappings m
  where m.inventory_entry_id = e.id and m.is_active
)
```

**这个过滤必须在服务端、在 `limit` 之前生效。** 先返回前 20 条再由前端过滤的话，
这 20 条可能全是停用构件，真正可绑的结果被截断在后面。

**语义边界**：`binding_eligible` 只表示"台账层面可供选择"，**不保证对当前行可绑**——
正式绑定时 `validate_target()` 还会校验部件名与规范类别的兼容性，用户仍可能被拒。
要真正保证，搜索得额外携带 `part_name` 并复刻 `validate_target()` 的规则，那是改动
绑定判定语义，不在本轮范围。文档中一律表述为"启用且有生效映射"，不写"可绑定"。

台账管理页不传 `binding_eligible`，仍能看到停用与未映射构件。

### ③ 批量替换取数端点

```
GET /api/import-records/{import_id}/component-binding/inventory?expected_revision_id={id}
```

挂在现有 `component-binding` 基路径下。后端按 `inspection_years` 解析该导入实际使用的
版本，并按上节规则校验 `expected_revision_id`。

**过滤条件与 ② 的 `binding_eligible` 完全一致**（启用 + 有生效映射），以复现旧代码
`usableEntries()` 的范围。只筛启用会让预览把"无生效映射的构件"判成 `will_bind`，
用户点应用后 `validate_target()` 找不到映射，整批冲突——与改造前不一致。

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
| 导入记录不是"待校对" | 409 `component_binding_conflict` |
| 没有可用的已确认台账 | 409 `component_binding_inventory_unavailable` |
| 预期版本不一致 | 409 `component_inventory_revision_changed` |
| 数据库异常 | 503 |
| 前端主动取消 | 静默，不显示加载失败 |

### ④ 写操作

行内绑定、批量绑定的请求体新增 `expected_inventory_revision_id`，后端按"版本一致性"
一节校验，不一致返回 409 `component_inventory_revision_changed`。

## 后端数据流

### 确认版本解析拆成两层

`resolve_confirmed_revision()` 现有 5 个调用点，其中 4 个在写路径需要完整
`InventoryRevision` 做 `validate_target()`。直接改返回类型会让那 4 处编译不过，
或逼各调用者各自重写加载逻辑。拆成：

```cpp
// 只解析版本 id 与桥梁 id，不加载构件。供概览与批量取数端点使用。
std::optional<ConfirmedRevisionRef> resolve_confirmed_revision_ref(...);

// 先调上者，再按准确版本 id 加载完整修订版。供需要 validate_target() 的写操作使用。
std::optional<inventory::InventoryRevision> load_confirmed_revision(...);
```

```cpp
struct ConfirmedRevisionRef { std::string id; std::string bridge_id; };
```

**两者必须共享同一套版本解析规则**（锁定优先、否则最新已确认），不能各写一份 SQL——
两份实现迟早漂移，而这正是缺陷一那类问题的来源。

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
因此响应必须携带按新版本生成的完整概览（含新的 `inventory_revision_id` 与展示对象）。

前端删掉那次额外的 `/latest`，**理由不是"绑评定树不改台账"（那是错的）**，而是
操作响应已经带回按新版本生成的完整概览，不必再问一次。

## 前端数据流

### 首屏

`Promise.all` 塌成单个 `fetchComponentBinding`，概览到达即渲染。
`:426` 绑定评定树后的整份台账重取**删除**。

面板另有一个**非阻塞**的 `fetchRatingTreeVersions()`（`:269`），它是辅助选择数据，
保留不动。因此不能笼统说"只发一次请求"，见测试一节的精确表述。

### 行内搜索

- 用 `overview.inventory_revision_id` 调
  `?keyword=…&binding_eligible=true&limit=20`；
- 250 ms 防抖；`AbortController` 防乱序；
- `limit` 取 20，与现有 `MAX_SEARCH_RESULTS` 一致。改成别的值是行为变更，本轮不改。

### 合并与显示

| 规则 | 取值 |
| --- | --- |
| 候选项是否始终显示 | 是，与当前关键词是否匹配无关 |
| 顺序 | 候选在前（按 `candidate_components` 原序），搜索结果追加在后 |
| 去重键 | `bridge_component_id` |
| `limit` 是否包含候选项 | 否，只约束服务端搜索结果 |
| 搜索失败 | 保留概览候选项，就地显示行内错误 |
| 清空关键词 | 立即清除服务端搜索结果，只留候选项 |
| 绑定完成 | 清除该行的搜索词与搜索结果 |
| 版本变化 | 取消在途搜索、清空搜索结果、清除批量缓存 |

**"候选 · "前缀保留**（`:190`）。它现在靠 `new Set(row.candidate_component_ids)` 判定，
改后从 `candidate_components` 的 `bridge_component_id` 建同样的集合。这个前缀是用户区分
"系统猜的"与"自己搜的"的唯一标记，不能随字段一起删掉。

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
  **不能按 `bridgeId` 缓存**——那正是缺陷二那类错误的温床。

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

**后端**

- `keyword` 分别命中编号、构件类别、现场名称；通配符 `%`、`_`、`\` 转义依然有效；
  空或纯空白返回 400；
- `binding_eligible=true` 不返回停用构件、不返回无生效映射的构件；
- **过滤先于 `limit`**：构造前 20 条全为停用、第 21 条才有效的数据，断言能搜到它；
- `binding_eligible` **只**过滤启用与生效映射，不做类别兼容性过滤（语义边界用例）；
- 概览返回准确的 `inventory_revision_id`；`inventory_confirmed` 与它始终满足不变量；
- 概览返回 `bound_component` 与 `candidate_components`，JSON 中不再有
  `candidate_component_ids`；
- `candidate_components` 不含停用或无生效映射的构件；
- `candidate_components` 的顺序与 `candidate_component_ids` 一致；
- 候选展示对象缺失时行状态仍为 `ambiguous`；
- 已绑构件在该版本中不存在或不可用时 `bound_component` 为 `null`；
- 年度锁定版本优先于桥梁最新版本；
- **同时存在已确认版本与新草稿时，概览仍判定为已确认**（缺陷一回归）；
- 概览只查候选与已绑 id，不完整加载整份台账；
- ③ 端点只含启用且至少有一个生效映射的构件，返回精简 DTO 且带 `is_active`；
- ③ 端点取的是年度锁定版本，不是桥级 `/latest`（缺陷二回归）；
- 年度未锁定且最新已确认版本已变化时，`expected_revision_id` 校验返回 409；
  年度已锁定且预期版本不符时同样 409（缺陷三回归）；
- 行内绑定与批量绑定按预期版本执行，不静默切换版本；
- ③ 端点的 401 / 404 / 409 / 503 映射。

**前端**

- 进面板**只发一次构件绑定首屏数据请求**：`fetchComponentBinding` 恰好 1 次、
  `fetchLatestComponentInventory` 恰好 0 次；`fetchRatingTreeVersions` 允许 1 次且
  不阻塞概览渲染——**不能笼统断言"只发一次请求"**；
- 概览里的候选项在搜索前即可见；不可用的候选不出现在选项里；
- 行内输入 250 ms 后调 `keyword` 搜索，带 `binding_eligible`，且用
  `overview.inventory_revision_id`；
- 新搜索取消旧请求，旧响应不覆盖新响应；
- 搜索失败保留候选项并显示行内错误；
- 候选与搜索结果按 `bridge_component_id` 去重，候选在前且保持原序；
- 候选项带「候选 · 」前缀，搜索结果不带；
- 候选不占用 `limit` 名额：候选 2 项 + 服务端返回 20 项时下拉共 22 项；
- 写操作携带当前 `overview.inventory_revision_id`；
- 收到 `component_inventory_revision_changed` 时刷新概览并给出明确提示；
- 绑定评定树后不再重取整份台账；
- **精简批量条目不会被 `buildReplacePreview` 全部跳过**（复审第 1 条的回归）；
- 批量响应版本与当前概览不一致时不生成预览；
- 对话框未打开时不取数；打开后才取；取数期间不会把编号判成 `not_in_inventory`；
- 取数失败显示错误与重试；**主动取消不显示错误**；
- 同版本重开复用缓存；版本变化时缓存失效。

**必须同步修改的位置**：`importBindingApi.ts` 的接口定义，以及 5 处测试夹具——
`importBindingApi.test.ts:28`、`ReviewWorkspacePage.test.tsx:197`、
`BulkReplaceDialog.test.tsx:15`、`replacePreview.test.ts:14` 四处是空数组，
`ComponentBindingWorkspace.test.tsx:94` 是 `status === "unmatched" ? ["c1"] : []`，
改新字段时要把 `"c1"` 换成完整的候选对象，否则那条用例覆盖不到候选项的显示。

## 验收

1. 打开绑定面板不再请求 `/component-inventories/latest`；
2. 首屏只等概览（评定树版本列表非阻塞）；
3. 后端概览不再完整加载五千多条构件及映射；
4. 概览返回准确的 `inventory_revision_id`，且与 `inventory_confirmed` 满足不变量；
5. 搜索、批量取数、写操作都针对同一版本；版本变化时明确报 409，不静默切换；
6. 行内搜索与批量取数都只返回启用且有生效映射的构件；
7. 搜索能匹配编号、构件类别、现场名称；
8. 绑定评定树后不再额外获取整份台账；
9. 批量替换只在打开对话框时取数，取的是年度锁定版本，失败不生成预览；
10. 绑定、标记缺失、批量替换的业务语义不变，预览结果与改造前逐项一致；
    唯一有意的可见行为变化是候选项不再占用搜索结果名额（见前端一节）。

性能须以 **Release 构建**复测：Debug 关优化且开 `_ITERATOR_DEBUG_LEVEL=2`，
数千条构件的 JSON 序列化约慢 4 倍，Debug 下的数字无参考价值。

## 实施规模提示

本设计已远超最初"换成现成端点"的设想。后端要拆分确认版本解析、重写概览查询、
新增一个端点与两个查询参数、引入一个错误码并贯通四条写路径；前端要重写面板数据流、
改 `buildReplacePreview` 的输入类型、补批量替换的加载态与版本缓存。

实施计划应据此分批，**缺陷一（草稿优先解析）可以独立先行**——它是一个能让整块绑定
UI 失效的既有 bug，与按需检索改造无依赖关系。

## 遗留

`DefectsSection` 的手动添加病害表单仍拉整份台账，且用 `<select>` 承载五千多个选项。
它是 `/latest` 最后一个调用者；改造它之后，那条全量路径才可能真正退场。
