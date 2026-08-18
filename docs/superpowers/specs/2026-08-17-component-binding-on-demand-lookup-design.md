# 构件绑定改用按需检索

- 日期：2026-08-17
- 状态：已评审，待实现
- 相关模块：校对工作台构件绑定（`ComponentBindingWorkspace`）、构件台账检索端点

## 背景

打开校对工作台的构件绑定面板要等很久。首屏用 `Promise.all` 并排发两个请求，
**必须两个都回来才渲染**（`ComponentBindingWorkspace.tsx:251`）：

| 请求 | 规模 |
| --- | --- |
| `fetchComponentBinding` | 按导入记录分组，几百项 |
| `fetchLatestComponentInventory` | 5174 条构件 + 5174 条映射，约 3.4 MB |

第二个是 2026-08-16 那轮聚合改造中，因校对工作台仍在使用而**刻意保留**的全量路径。
台账页当时已改走汇总端点，绑定面板没有跟着改。

绑定评定树成功后还会再取一次整份台账（`:426`）——绑评定树不改台账，那次纯属白花。

这 3.4 MB 在面板里只做两件事：

1. 建 `bridge_component_id → entry` 的 Map，把行上的候选 id 与已绑 id 换成可读信息；
2. 行内搜索时遍历全部构件做子串匹配，最多取 50 条。

为了一个搜索框和几个候选项，先把五千多条构件搬进浏览器。

## 目标与非目标

**目标**

1. 绑定面板首屏不再拉取整份台账，概览到达即可渲染。
2. 行内搜索改走服务端按需检索。
3. 删除面板内因此作废的代码。

**非目标**

- **不改造 `DefectsSection` 的手动添加病害表单**。它同样拉整份台账（`:525`），但
  已是懒加载（点"手动添加病害"才拉），且它的问题是另一个——往 `<select>` 里塞
  5174 个选项，需要换成可搜索的选择器，是独立的一块 UI 工作。
- **因此本轮后端一行都不删**。`/latest`、`load_revision()`、
  `inventory_revision_json()`、`fetchLatestComponentInventory()` 这条全量路径仍有
  两个调用者（`DefectsSection` 与本面板的批量替换），删不掉。"删除冗余代码"这半个
  目标只在面板内部兑现。
- 不改绑定、标记缺失、批量替换的业务语义。

## 设计中查明的三件事

### 1. 面板需要的是"按 id 反查"，而现有端点是"按编号检索"

`BindingRow` 给的是 id 不是编号：

```ts
bridge_component_id: string | null;   // 已绑目标
candidate_component_ids: string[];    // 候选
```

2026-08-16 那轮建的检索端点是 `?number=` 按编号子串匹配，**没有按 id 反查的能力**。
"直接换成现成端点"缺这一环。

### 2. 面板只用到构件的四个字段

- 已绑定行（`:119-129`）：仅在"改绑到了别的编号"时显示 `编号 / 类别`；
- 未绑定行（`:187-191`）：下拉项显示 `编号 / 类别`，`value` 用 `bridge_component_id`，
  React key 用 `id`。

`site_name` **只参与搜索匹配，不参与显示**。因此概览每行需要多带的字段很少。

### 3. 批量替换要的是第三种能力

`BulkReplaceDialog` 把找/替换模式套到每行编号上得到一批目标编号，再按**归一化编号**
建索引判断台账里有 0 个 / 1 个 / 多个（`replacePreview.ts:52-58`）。这是"整批存在性
与歧义判定"，不是搜索，`?keyword=` 端点给不了。

## 方案选择

候选 id 与已绑 id 如何变成可读信息：

- **A（采纳）**：概览直接带上展示信息。这些 id 本就是服务端在同一次查询里算出来的，
  就地 join 一次比让客户端再问一轮便宜；且一次消掉两处全量拉取。
- **B**：新增按 id 批量反查端点。概览不动，但多一个端点、多一轮往返，要的数据服务端
  刚才就在手里。
- **C**：只改搜索，候选仍靠全量台账。等于没优化。

批量替换如何处理：

- **采纳**：`buildReplacePreview` 不动，把整份台账的拉取**挪到对话框打开时**。
- **否决**：把归一化与存在性判定搬到服务端。那要求 `normalizeComponentNumber` 有两份
  永远一致的实现，否则预览说"会绑上"而实际绑不上。不该为一个偶发的批量操作冒这个险。

**因此"彻底换成按需接口"要打个折**：首屏路径彻底换掉，批量替换仍走全量，只是不再
挡首屏。

## 接口契约

### ① 概览的 row

```jsonc
{
  "component_number": "1-1#梁",
  "defect_count": 3,
  "status": "unmatched",
  "bridge_component_id": null,
  // 已绑构件的展示信息；未绑定、或绑了但台账里查无此构件时为 null
  "bound_component": null,
  // 取代 candidate_component_ids：id 已在对象里，不再单列一份
  "candidate_components": [
    {
      "entry_id": "…",
      "bridge_component_id": "…",
      "component_number": "1-1#梁",
      "site_component_type": "梁"
    }
  ]
}
```

`candidate_component_ids` 从 **JSON 输出**中移除，但 **C++ 结构体字段保留**——
`ImportBindingRepository.cpp:90` 用它判定 `ambiguous` 状态，那是内部逻辑。

`bound_component` 为 `null` 有实义：对应"绑了但台账里查不到那条构件"（构件被删），
沿用现有的「已绑定构件」文案分支。

### ② 检索端点

`?number=` 改名为 `?keyword=`，匹配 `component_number`、`site_component_type`、
`site_name` 三个字段。

改名不是洁癖：语义已不是"编号"，名字留着会误导。该参数只有一个前端在用。
SQL 由单列 `like` 改为三列 `or`，转义逻辑不变。

绑定面板与构件台账页**共用同一参数、同一语义**。

## 前端数据流

### 绑定面板首屏

`Promise.all` 塌成单个 `fetchComponentBinding`，概览到达即渲染。
`:426` 绑定评定树后的整份台账重取**删除**。

### 行内搜索

输入后调 `?keyword=`，250 ms 防抖，`AbortController` 防乱序（快速改词时先发的可能
后回）。写法与台账页、评定树页一致。

候选项与已绑构件不再查 Map，直接读 `row.candidate_components` /
`row.bound_component`。

### 批量替换

`buildReplacePreview` 不动；`fetchLatestComponentInventory` 保留，但移到对话框打开时
触发。3.4 MB 从"每次进面板"变成"只有真要批量替换时"——那是用户主动发起的重操作。

### 删除清单（均在面板内部）

| 删除 | 原因 |
| --- | --- |
| `usableEntries()` | 概览已带展示信息 |
| `entries` memo、`byId` Map | 同上 |
| `RowAction` 的 `entries` / `byId` 两个 prop | 改读 `row.*` |
| `:426` 绑定评定树后的整份台账重取 | 纯白花 |
| 搜索框与下拉的 `entries.length === 0` 门禁 | 冗余：整块 UI 已在 `inventory_confirmed` 之后，而 confirm 的前置条件（`inventory_empty` 与 `component_mapping_required` 两条 blocker）保证"已确认 ⇒ 至少一条可用构件" |

## 构件台账页的连带改动

检索语义统一后，台账页的搜索也变成三字段匹配，文案必须跟上：

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
| 行内搜索失败 | 就地提示，不清空该行已有候选项——候选来自概览，与搜索无关 |
| 搜索无结果 | 沿用现有的「没有匹配的构件」 |
| 快速改词导致乱序 | `AbortController`；旧响应一律丢弃 |
| 已绑构件在台账里被删 | `bound_component` 为 `null`，沿用「已绑定构件」文案 |
| 台账未确认 | 整块 UI 仍由 `inventory_confirmed` 挡住，不变 |

## 测试

**后端**

- `?keyword=` 分别命中编号、构件类别、现场名称三个字段；
- 通配符转义在三字段下依然有效（搜 `%` 不命中全表）——现有用例需确认仍成立；
- 概览带回候选与已绑构件的展示信息；
- 已绑构件被删时 `bound_component` 为 `null`。

**前端**

- 进面板**只发一次**请求（概览），不再拉台账——本轮的核心承诺，必须钉住；
- 行内输入触发 `?keyword=`；概览里的候选项在搜索前就已可见；
- 绑定评定树后**不再**重取台账；
- 批量替换对话框**打开时**才拉台账，不打开就不拉。

**必须一并修改的位置**：`importBindingApi.ts` 的接口定义，以及 5 处测试夹具——
`importBindingApi.test.ts:28`、`ReviewWorkspacePage.test.tsx:197`、
`BulkReplaceDialog.test.tsx:15`、`replacePreview.test.ts:14` 四处是空数组，
`ComponentBindingWorkspace.test.tsx:94` 是
`status === "unmatched" ? ["c1"] : []`，改新字段时要把 `"c1"` 换成完整的候选对象，
否则那条用例覆盖不到候选项的显示。

## 验收

- 打开绑定面板不再发出 `/component-inventories/latest` 请求；
- 概览响应体较改造前增大，但仍是 KB 量级；
- 行内搜索能搜到编号、构件类别、现场名称；
- 批量替换预览结果与改造前逐项一致。

性能须以 **Release 构建**复测：Debug 关优化且开 `_ITERATOR_DEBUG_LEVEL=2`，
数千条构件的 JSON 序列化约慢 4 倍，Debug 下的数字无参考价值。

## 遗留

`DefectsSection` 的手动添加病害表单仍拉整份台账，且用 `<select>` 承载五千多个选项。
它是全量路径最后一个"非批量操作"的调用者；改造它之后，`/latest` 才可能真正退场。
