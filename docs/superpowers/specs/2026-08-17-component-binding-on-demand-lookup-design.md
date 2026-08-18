# 构件绑定改用按需检索

- 日期：2026-08-17（2026-08-18 按评审意见修订）
- 状态：已按评审修订，待复审
- 相关模块：校对工作台构件绑定（`ComponentBindingWorkspace`）、构件台账检索端点
- 评审记录：`2026-08-17-component-binding-on-demand-lookup-design-review.txt`

## 背景

打开校对工作台的构件绑定面板要等很久。首屏用 `Promise.all` 并排发两个请求，
**必须两个都回来才渲染**（`ComponentBindingWorkspace.tsx:251`）：

| 请求 | 规模 |
| --- | --- |
| `fetchComponentBinding` | 按导入记录分组，几百项 |
| `fetchLatestComponentInventory` | 5174 条构件 + 5174 条映射，约 3.4 MB |

第二个是 2026-08-16 那轮聚合改造中，因校对工作台仍在使用而**刻意保留**的全量路径。

绑定评定树成功后还会再取一次整份台账（`:426`）——绑评定树不改台账，那次纯属白花。

这 3.4 MB 在面板里只做两件事：把行上的候选 id 与已绑 id 换成可读信息；行内搜索时
遍历全部构件做子串匹配（上限 `MAX_SEARCH_RESULTS = 20`）。

## 修订说明：原稿的两处错误

本节保留，供日后回看时知道为什么这样设计。

**其一，"就地 join"的说法不成立。** 原稿称候选 id"本来就是服务端在同一次查询里
算出来的，就地 join 一次比让客户端再问一轮便宜"。实际上候选 id 来自
`parsed_result_json`（`ImportBindingRepository.cpp:80-82`），与台账查询无关；而
`overview()` 另外通过 `resolve_confirmed_revision()` 把**整份台账加载了一遍再丢掉**
——它只用了 `revision.has_value()`。

按原稿实现，会**只减少浏览器下载，后端照样读五千多条构件和映射**。正确的做法是：
聚合出 id → 去重 → 在准确版本下做一次定向批量查询。

**其二，缺了版本 id 这条主线。** 原稿要求行内搜索调
`/component-inventories/{revision_id}/entries`，却没说 `revision_id` 从哪来。把首屏
的台账请求删掉之后，前端手里根本没有这个 id。方案跑不起来。

## 目标与非目标

**目标**

1. 绑定面板首屏不再拉取整份台账，概览到达即可渲染。
2. **后端概览也不再完整加载台账**，改为按需要的 id 定向查询。
3. 行内搜索改走服务端按需检索，且只返回**可绑定**的构件。
4. 准确的台账版本 id 贯穿概览、搜索、批量替换三处。
5. 顺带修正两处既有缺陷（见下）。
6. 删除面板内因此作废的代码。

**非目标**

- **不改造 `DefectsSection` 的手动添加病害表单**。它同样拉整份台账（`:525`），但
  已是懒加载，且它的问题是另一个——往 `<select>` 里塞 5174 个选项，需要换成可搜索的
  选择器，是独立的一块 UI 工作。
- **因此 `/latest` 端点本轮不删**，它仍有 `DefectsSection` 这个调用者。
- 不改绑定、标记缺失、批量替换的业务语义。

## 顺带修正的两处既有缺陷

这两处是评审查出来的，都不是本次改造引入的，但本次改造必须处理——否则新代码会把
它们固化下来。

### 缺陷一：确认版本解析用了草稿优先的接口

`resolve_confirmed_revision()` 在年度没有锁定版本时调用
`get_latest_revision(bridge_id)`，而后者的排序是
`order by (status='草稿') desc, revision_number desc`——**草稿优先**。取到草稿后再判
`status == '已确认'` 必然不成立，于是 `inventory_confirmed` 被置为 `false`。

**后果**：桥上同时存在已确认版本和一个新草稿时，绑定面板会误报"该桥构件台账尚未
确认"，整块绑定 UI 被挡住。

**修正**：

- 年度锁定了版本（`inspection_years.component_inventory_revision_id` 有值）：严格读取
  该版本，校验桥梁 id 与确认状态；
- 未锁定：直接查该桥**最新的已确认版本**
  （`where bridge_id=$1 and status='已确认' order by revision_number desc limit 1`），
  不经过草稿优先的接口。

### 缺陷二：批量替换读的是桥级 `/latest`

同一个草稿优先排序，会让批量替换预览读到与绑定校验不同的版本：

1. 年度锁定已确认版本 R2；
2. 有人改台账，派生出草稿 R3；
3. `/latest` 草稿优先，返回 R3；
4. 预览按 R3 算，绑定时后端按 R2 校验；
5. 前端显示"可以绑定"，后端却拒绝；或编号、类别与正式版本对不上。

**修正**：批量替换改用按导入记录取数的端点（见契约 ③），由后端解析出该导入实际
使用的版本，不再走桥级 `/latest`。

## 主线：准确的台账版本 id

这是本次修订的核心。三处都必须针对**同一个**版本：

| 用途 | 版本来源 |
| --- | --- |
| 概览填充展示对象 | 后端解析出的确认版本 |
| 行内搜索 | `overview.inventory_revision_id` |
| 批量替换取数 | 后端按导入记录解析，前端不参与选版本 |

前端**不再**根据 `bridgeId` 去问"最新版本"——那正是缺陷二的来源。

## 接口契约

### ① 绑定概览

新增 `inventory_revision_id`；行上新增两个展示对象。

```jsonc
{
  "inventory_confirmed": true,
  // 未确认时为 null。行内搜索与缓存键都用它。
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
          // 已绑构件的展示信息；未绑定、或绑了但该版本里查无此构件时为 null
          "bound_component": null,
          // 取代 candidate_component_ids 出现在 JSON 里
          "candidate_components": [
            {
              "entry_id": "…",
              "bridge_component_id": "…",
              "component_number": "1-1#梁",
              "site_component_type": "梁",
              "site_name": "梁"
            }
          ]
        }
      ]
    }
  ]
}
```

`site_name` 一并返回：它参与搜索匹配，结果里不显示的话用户看不出为什么命中
（见"前端数据流·合并与显示"）。

**`candidate_component_ids` 从 JSON 移除，但 C++ 结构体字段必须保留**——
`ImportBindingRepository.cpp:90` 用它判定 `ambiguous` 状态，那是业务逻辑。

### ② 检索端点

`?number=` 改名 `?keyword=`，匹配 `component_number`、`site_component_type`、
`site_name` 三字段。改名不是洁癖：语义已不是"编号"，名字留着会误导。

新增 `?binding_eligible=true`：只返回可绑定的构件，SQL 至少满足

```sql
e.is_active
and exists (
  select 1 from bridge_component_standard_mappings m
  where m.inventory_entry_id = e.id and m.is_active
)
```

**这个过滤必须在服务端、在 `limit` 之前生效。** 先返回前 20 条再由前端过滤的话，
这 20 条可能全是停用构件，真正可绑的结果被截断在后面。

台账管理页需要看到停用与未映射构件，绑定页不能让用户选中它们——所以两处共享
`keyword` 的三字段匹配规则，但**数据范围由 `binding_eligible` 区分**。

**本契约覆盖 2026-08-16 设计文档中的 `?number=` 契约**，那份文档的相应段落需同步
更新，避免两份设计冲突。

### ③ 批量替换取数端点

```
GET /api/import-records/{import_id}/component-binding/inventory
```

挂在现有 `component-binding` 基路径下，由后端按 `inspection_years` 解析出该导入实际
使用的版本，前端不传版本。

**只返回预览真正需要的字段**。`buildReplacePreview` 只用三样：`is_active` 做过滤、
`component_number` 做归一化索引键、`bridge_component_id` 做绑定目标
（`replacePreview.ts:54-84`）。因此响应只需：

```jsonc
{
  "inventory_revision_id": "9d52a613-…",
  "entries": [
    { "bridge_component_id": "…", "component_number": "1-1#梁" }
  ]
}
```

只含启用构件，不含映射、不含现场名与类别。5174 条约 400 KB，而整份台账是 3.4 MB
——**八分之一**。评审建议"恢复按版本读取完整台账"，这里做得更省：预览用不到的字段
就不发。

归一化仍在前端（`normalizeComponentNumber`），`buildReplacePreview` 算法不变。把
归一化搬到服务端要求两份实现永远一致，否则预览说"会绑上"而实际绑不上——不该为一个
偶发的批量操作冒这个险。

## 后端数据流

### 概览

1. 解析该导入对应的**确认版本引用**——只要 id 与桥梁 id，不加载构件：

   ```cpp
   struct ConfirmedRevisionRef { std::string id; std::string bridge_id; };
   ```

   解析规则见"缺陷一"。`resolve_confirmed_revision()` 返回完整
   `InventoryRevision` 的现状要改掉：概览只需要判断"确认与否"和"哪个版本"。

2. 从 `parsed_result_json` 聚合出 `BindingRow`，行状态照旧由内部
   `candidate_component_ids` 与现有业务逻辑计算。
3. 收集所有 `bridge_component_id` 与候选 id，**去重**。
4. 在步骤 1 得到的版本下，对这批 id 做**一次**定向查询，只取
   `entry_id / bridge_component_id / component_number / site_component_type / site_name`。
5. 回填 `bound_component` 与 `candidate_components`。

第 4 步是本次后端优化的关键：查的是"这几十个 id"，不是"这个版本的全部构件"。

### 行状态与展示对象是两回事

内部 id 是业务判断数据，展示对象是显示数据。可能出现内部有两个候选 id、但在准确
版本里只查到一个展示对象（构件被删）：

```
candidate_component_ids.length = 2
candidate_components.length   = 1
```

此时行**仍然是 `ambiguous`**，不能按 `candidate_components` 的数量重新算成
`unmatched` 或单候选。展示对象缺失只影响显示，不改变行状态。

### 评定树绑定/切换

返回的新概览必须携带**新的** `inventory_revision_id`（若版本随之变化）。前端据此
判断是否要作废批量替换的缓存。

## 前端数据流

### 首屏

`Promise.all` 塌成单个 `fetchComponentBinding`，概览到达即渲染。
`:426` 绑定评定树后的整份台账重取**删除**。

### 行内搜索

- 用 `overview.inventory_revision_id` 调
  `?keyword=…&binding_eligible=true&limit=20`；
- 250 ms 防抖；`AbortController` 防乱序（快速改词时先发的可能后回）；
- `limit` 取 20，与现有 `MAX_SEARCH_RESULTS` 一致。**改成别的值就是行为变更**，
  本次不改。

### 合并与显示

保持现有语义，并把原先隐含的规则写明：

| 规则 | 取值 |
| --- | --- |
| 候选项是否始终显示 | 是，与当前关键词是否匹配无关 |
| 候选项与搜索结果的顺序 | 候选在前，搜索结果追加在后 |
| 去重键 | `bridge_component_id` |
| `limit` 是否包含候选项 | 否，`limit=20` 只约束服务端搜索结果 |
| 搜索失败 | 保留概览候选项，就地显示行内错误 |
| 清空关键词 | 立即清除服务端搜索结果，只留候选项 |
| 绑定完成 | 清除该行的搜索词与搜索结果 |

下拉项显示 `编号 / 类别 / 现场名`。原先只显示前两项，但 `keyword` 会匹配现场名——
不显示的话，用户按现场名搜出来会看不懂为什么命中，也分不清编号与类别相同、
仅现场名不同的构件。

### 批量替换

打开对话框时才调 ③，`buildReplacePreview` 不动。必须补齐加载态：

- `loading` / `error` 两个状态；加载完成前**禁用**查找、替换、应用；
- **不能用空数组表示"尚未加载"**——那会让预览把所有编号判成 `not_in_inventory`；
- 关闭对话框取消未完成请求；失败时给重试入口；
- 缓存**按 `inventory_revision_id`**，同一版本重复打开可复用；版本变化（如评定树
  切换）即失效。**不能按 `bridgeId` 缓存**——那正是缺陷二那类错误的温床。

### 删除清单（均在面板内部）

| 删除 | 原因 |
| --- | --- |
| `usableEntries()` | 概览已带展示信息，搜索由服务端过滤 |
| `entries` memo、`byId` Map、`inventory` state | 同上 |
| `RowAction` 的 `entries` / `byId` 两个 prop | 改读 `row.*` |
| `:426` 绑定评定树后的整份台账重取 | 纯白花 |
| 搜索框与下拉的 `entries.length === 0` 门禁 | 冗余：整块 UI 已在 `inventory_confirmed` 之后，而 confirm 的前置条件保证"已确认 ⇒ 至少一条可用构件" |

## 构件台账页的连带改动

检索语义统一后，台账页的搜索也变成三字段匹配（不传 `binding_eligible`，仍能看到
停用与未映射构件），文案必须跟上：

| 现在 | 改后 |
| --- | --- |
| 标签「按编号搜索构件」 | 「搜索构件」 |
| 占位符「如 3-5#」 | 「编号、类别或现场名，如 3-5# 或 支座」 |
| 提示「输入构件编号可直接定位单个构件」 | 补一句：整组构件请用分组表的"查看构件"，那里能分页看全 |

最后一句是必要的：搜"支座"会得到「匹配 3300 个，显示前 50 个」，而分组表点进去能
完整分页浏览。不点破，用户会以为这座桥只有 50 个支座。

## 错误处理与边界

| 情况 | 处理 |
| --- | --- |
| 概览未确认（`inventory_revision_id` 为 null） | 整块 UI 仍由 `inventory_confirmed` 挡住，不发搜索请求 |
| 行内搜索失败 | 就地提示，保留概览候选项 |
| 搜索无结果 | 沿用现有的「没有匹配的构件」 |
| 快速改词导致乱序 | `AbortController`；版本 id 变化时旧响应一律丢弃 |
| 已绑构件在该版本里被删 | `bound_component` 为 `null`，沿用「已绑定构件」文案；行状态不变 |
| 批量替换数据未到 | 禁用操作，不计算预览 |
| 批量替换取数失败 | 显示错误与重试，不生成预览 |

## 测试

**后端**

- `keyword` 分别命中编号、构件类别、现场名称；
- 通配符 `%`、`_`、`\` 转义在三字段下依然有效；
- `keyword` 为空或纯空白返回 400；
- `binding_eligible=true` 不返回停用构件、不返回无生效映射的构件；
- **过滤发生在 `limit` 之前**：构造前 20 条全为停用、第 21 条才有效的数据，
  断言能搜到它；
- 概览返回准确的 `inventory_revision_id`；
- 概览返回 `bound_component` 与 `candidate_components`，JSON 中不再有
  `candidate_component_ids`；
- 年度锁定版本优先于桥梁最新版本；
- **同时存在已确认版本与新草稿时，概览仍判定为已确认**（缺陷一的回归用例）；
- 概览只查候选与已绑 id，不完整加载整份台账；
- 已绑构件在目标版本中不存在时 `bound_component` 为 `null`；
- 候选展示对象缺失时行状态仍为 `ambiguous`；
- ③ 端点返回年度锁定的版本，且只含启用构件与两个字段（缺陷二的回归用例）。

**前端**

- 进面板**只发一次**请求，不请求 `/component-inventories/latest`；
- 概览里的候选项在搜索前即可见；
- 行内输入 250 ms 后调 `keyword` 搜索，且带 `binding_eligible`；
- 搜索请求使用 `overview.inventory_revision_id`；
- 新搜索取消旧请求，旧响应不覆盖新响应；
- 搜索失败保留候选项并显示行内错误；
- 候选与搜索结果按 `bridge_component_id` 去重，候选在前；
- 绑定评定树后不再重取整份台账；
- 批量替换对话框未打开时不取数；打开后才取；
- 取数期间不会把编号判成 `not_in_inventory`；
- 取数失败显示错误与重试；关闭对话框取消未完成请求；
- 同版本重开复用缓存；`inventory_revision_id` 变化时缓存失效。

**必须同步修改的位置**：`importBindingApi.ts` 的接口定义，以及 5 处测试夹具——
`importBindingApi.test.ts:28`、`ReviewWorkspacePage.test.tsx:197`、
`BulkReplaceDialog.test.tsx:15`、`replacePreview.test.ts:14` 四处是空数组，
`ComponentBindingWorkspace.test.tsx:94` 是 `status === "unmatched" ? ["c1"] : []`，
改新字段时要把 `"c1"` 换成完整的候选对象，否则那条用例覆盖不到候选项的显示。

`replacePreview.ts` 顶部"绑定页已把台账 entries 加载在手"的注释已过期，需更新。

## 验收

1. 打开绑定面板不再请求 `/component-inventories/latest`；
2. 首屏只等概览；
3. **后端概览不再完整加载五千多条构件及映射**；
4. 概览返回准确的 `inventory_revision_id`，搜索与批量替换都针对它；
5. 行内搜索不返回停用或无生效映射的构件；
6. 搜索能匹配编号、构件类别、现场名称；
7. 绑定评定树后不再额外获取整份台账；
8. 批量替换只在打开对话框时取数，且取的是年度锁定版本；
9. 批量替换取数失败不会生成错误预览；
10. 绑定、标记缺失、批量替换的业务语义不变，预览结果与改造前逐项一致。

性能须以 **Release 构建**复测：Debug 关优化且开 `_ITERATOR_DEBUG_LEVEL=2`，
数千条构件的 JSON 序列化约慢 4 倍，Debug 下的数字无参考价值。

## 遗留

`DefectsSection` 的手动添加病害表单仍拉整份台账，且用 `<select>` 承载五千多个选项。
它是 `/latest` 最后一个调用者；改造它之后，那条全量路径才可能真正退场。
