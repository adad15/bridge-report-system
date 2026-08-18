# 已确认台账版本解析的一致性

- 日期：2026-08-18（当日两轮评审后修订）
- 状态：已按复审修订，待复审确认
- 相关模块：校对保存、入库前检查、年度确认、评定树自动匹配、Word 导入、系统评定
- 前序：`2026-08-17-component-binding-on-demand-lookup-design.md`（缺陷一修的是同源问题的另外两处）
- 评审记录：`…-design-review.txt`（初审）、`…-design-rereview.txt`（复审）

## 背景

绑定链路已经统一到一条版本解析规则：**检测年度锁定的版本优先，年度未锁定时取该桥
最新的已确认版本**（`ComponentInventoryRepository::resolve_confirmed_revision_ref()`）。
绑定写入病害时，写进 `component_inventory_revision_id` 的就是这条规则解析出来的版本。

清理 `/latest` 死代码时把 `get_latest_revision()` 的调用方全部过了一遍，发现**还有
六处**用了草稿优先的 `get_latest_revision()`：

```sql
order by (status='草稿') desc, revision_number desc limit 1
```

两轮评审下来，事情比"六处换调用"大得多：真正的写入—校验—评定闭环还牵涉到**第七个
数据源**（系统评定服务自己重读年度版本），以及三处事务边界问题。

## 修订说明：前两稿错在哪

保留此节，供日后回看时知道为什么这样设计。

**初稿三处事实错误**（初审指出，均已核实）：

- 年度确认的事务查询**不需要"补选一列"**——`ReviewRepository.cpp:722` 已经选了
  `iy.component_inventory_revision_id`，`:803-804` 也已经读进 preflight 上下文。
- `get_latest_revision()` 迁移后**不是"只剩台账管理页一个调用者"，而是零调用者**。
  管理页用的是 `find_latest_revision_id()`（`ComponentInventoryRoutes.cpp:449`）；
  同文件 `:436` 那句 `get_latest_revision` 是**注释**，写的恰恰是"绝不走它"。
- ④ **不是"为病害挑构件"**。`DefectRatingTreeMatching.cpp:73` 读的是病害上**已绑**的
  `bridge_component_id`，`:85` 拿它去台账里找映射，用于挑评定树节点。它从不写构件。

初稿还把 ③ 说成"在确认事务内重复一遍同样的解析"——那条事务的 preflight 上下文**已经
用年度锁定版本**，只有评定树校验另起了一条草稿优先解析。③ 只坏了一半。

**二稿的关键遗漏**（复审指出，已核实）：

- 漏了**系统评定服务**这个数据源。修好六处也不闭环，见下节。
- Word 导入的版本冲突会掉进"删除失败导入"的通用路径。
- 校对保存的 `bool` 返回值传不出具体原因。

## 问题的本质：写入方与校验方对"哪个版本"意见不一致

单看"草稿优先"本身并不必然是错的——问题在于**同一份数据，写的时候按 A 版本，校验的
时候按 B 版本**。

- **写**：绑定把已确认版本的 id 写进每条病害（`write_binding()` 的第五个参数）。
- **校验**：保存草稿与入库前检查**整体**拿这个 id 去跟 `get_latest_revision()` 的结果比；
  年度确认的 preflight 用的是锁定版本，但它的评定树校验另行读了草稿优先版本。

桥上没有草稿时两者恰好相等，所以一直没暴露。**一旦存在草稿，两边必然不等。**

## 触发条件是日常操作

草稿不是异常状态：`ensure_editable_target()` 被每一个台账写操作调用
（`update_entry` / `add_entry` / `delete_entry` / `deactivate_entry` / `set_mapping`），
**在已确认台账上改一个构件编号、加一条构件、设一次映射，都会派生出草稿**。

2026-08-18 对本地开发库抽样时未发现草稿版本（1 座有台账的桥、0 个草稿）。该结果只说明
缺陷尚未在这份样本上触发，不构成"现网无影响"的依据。

## 六处清单

| # | 位置 | 现在取的版本 | 有草稿时的后果 | 严重度 |
| --- | --- | --- | --- | --- |
| ① | `ReviewRoutes.cpp:271` 保存校对草稿 | 桥梁最新（草稿优先） | **每条已绑定病害报错，草稿存不了盘** | 阻断 |
| ② | `ImportConfirmRoutes.cpp:71` 入库前检查 | 同上 | **判定"台账尚未确认"，确认不了** | 阻断 |
| ③ | `ReviewRepository.cpp:837` 确认事务内的评定树校验 | 同上 | 评定树校验按错版本（preflight 部分本身是对的） | 阻断 |
| ④ | `DefectMatchingRoutes.cpp:188` 评定树自动匹配 | 同上 | 按草稿映射挑节点；草稿里停用/改号的构件查不到 | 污染 |
| ⑤ | `WordImportRepository.cpp:67` 导入构件匹配 | 年度锁定优先，否则桥梁最新 | 按草稿匹配；年度不锁版本、病害不带版本 id | 降级 |
| ⑥ | `WordImportRepository.cpp:164` 导入评定树匹配 | 同上 | 同 ⑤，且与 ⑤ 各解析一次 | 降级 |

### ① 保存校对草稿

`DraftValidation.cpp:194` 把病害上的版本 id 与草稿优先解析的结果比，不等即落 issue。
有草稿时**每条已绑定病害都会进 issues**，接口返回 400，整份草稿存不下去。用户看到一串
"请重新选择"，而重新绑定并不能解决——绑定写回的仍然是已确认版本。

### ② 入库前检查

`PreflightReport.cpp:139-147` 在标志为假时直接落阻塞项；`:151-155` 还会逐条比对版本 id。
② 传进来的标志是 `inventory->status == "已确认"`，取到草稿时为假。

②**还需要完整台账**：`ImportConfirmRoutes.cpp:102-107` 把同一个 `inventory` 传给
`validate_defect_rating_tree_for_confirmation()`，那个函数要遍历构件与映射。

### ③ 年度确认事务——只坏了一半

preflight 那半是对的（`:803-813` 用年度锁定版本并单独查它的真实状态）。**错的是 `:837`
另起一条草稿优先解析**，结果只喂给 `validate_defect_rating_tree_for_confirmation()`。
修法不是补取数，而是**删掉那条多余的解析**。

### ④ 评定树自动匹配

`DefectRatingTreeMatching.cpp:73` 读病害上**已绑**的构件 id，`:85` 按它在台账里找生效
映射，用映射的规范类别挑评定树节点。**它不选构件。** 草稿优先的真实后果：用草稿的新映射
挑出错误节点；已确认版本里有效、但在草稿中被停用或改了映射的构件，会被判成无法匹配。

### ⑤ / ⑥ Word 导入

`WordImportRepository.cpp:65-67` 的形状与修正前的 `resolve_confirmed_revision()` 一样；
`:81` 只在已确认时才置 `confirmed_revision_id`，取到草稿时留空，于是 `:353-360` 那段
"把版本锁进待校对年度"不会执行。

## 隐藏的第七个数据源：系统评定服务

**这是二稿最大的遗漏。** `AssessmentConfirmationService::calculate()` 不接受调用方解析好的
版本，而是自己从库里重读，且用的是**内连接**：

```sql
join bridge_component_inventory_revisions r
  on r.id = iy.component_inventory_revision_id and r.bridge_id = iy.bridge_id
...
where iy.id = $1::uuid limit 1 for update of iy,p,sp,r
```

年度未锁定版本时该列为 NULL，连接无行，服务返回 `assessment_context_incomplete`
（"检测年度尚未锁定规范组合和已确认构件台账。"）。

`ImportConfirmRoutes.cpp:122-124` 在 preflight 通过后就调它，`ReviewRepository.cpp:886-891`
在确认事务里也调它。**因此：六处全部改对，年度未锁定时入库前检查依然过不去。**

这条同时推翻了二稿的一个说法。系统本来就要求**年度在确认前已锁定版本**——今天这个锁定
发生在绑定写操作或 Word 导入里。所以 ②③ 的"年度未锁定"分支，正确处置不是"放行并用最新
已确认版本"：

- **②（只读预检）**：给评定服务传显式版本，只算不写；
- **③（写事务）**：调评定服务**之前**先把版本锁进年度。

### 评定服务需要的能力

给它一个显式版本上下文（二选一，推荐前者）：

```cpp
AssessmentContextSnapshot load_context(
    const std::string& inspection_year_id,
    const std::optional<std::string>& inventory_revision_override) const;
```

规则：

1. 无 override 时维持现状，读年度锁定版本；
2. 有 override 时**必须校验**它属于同一桥梁且状态为已确认——override 不是绕过校验的后门；
3. 用 override 构建评定上下文；
4. **只读预检不得把 override 写进 `inspection_years`。**

## 阻塞项一：Word 导入一次事务里解析两次

当前顺序是：`match_imported_defects()` 解析一次 → `match_imported_defect_rating_tree_nodes()`
另查年度再解析一次 → 两次匹配都完成后才写年度锁定。

即便两处都换成正确的解析器，**仍不保证取到同一版本**：PostgreSQL 默认 READ COMMITTED 下
每条语句取新快照，同一事务里的两次查询可以看到不同的已提交数据（构件按 R2、评定树按 R3、
年度锁 R2）。

**修正：把版本在匹配开始前就固定下来**，顺序改为

1. 锁 `import_records` 行；
2. 有关联年度时**立即锁 `inspection_years` 行**；
3. 在年度行锁内读当前锁定版本；
4. 未锁定则解析最新已确认版本，并**立即**调 `lock_pending_year_revision()` 锁上；
5. 版本稳定之后再做构件匹配与评定树匹配，两者共用**同一个** `InventoryRevision`；
6. 最后写 `parsed_result_json` 与照片关系。

这样绝大多数冲突在匹配之前就暴露，而不是做完全部匹配才发现。
`match_imported_defect_rating_tree_nodes_unguarded()` 不再自行解析版本。

## 阻塞项二：Word 版本冲突会被当成解析失败删掉

`WordImportRoutes.cpp:376-383` 把 `persist_parse_result()` 的**任何**失败交给 `fail_parse`，
后者调 `discard_failed_import_safely()`（`:329-335`），它会删除导入记录与相关文件。

于是一个可恢复的并发版本冲突会导致：Python 解析已成功、照片已归档，只因年度被别人先锁到
另一个版本，**整条导入记录、临时源文件与归档一起被删**。

**修正**：

1. 按阻塞项一的新顺序，冲突通常在匹配前就暴露，代价小；
2. 仍可能冲突时用专用错误码 `component_inventory_revision_changed`；
3. **`WordImportRoutes` 遇到该错误码不得调用 `discard_failed_import_safely()`**，
   而是保留导入记录与原始 Word，标成可重试，或在事务内按当前已锁版本重做匹配。

真正的契约解析失败仍走原有清理流程——这条不能一起改掉。

## 阻塞项三：确认事务的加锁顺序，不是缺锁

复审建议"事务开始后单独 SELECT `inspection_years` FOR UPDATE"。核实后要修正这个提法：
**年度行锁已经有了**——评定服务那句上下文查询自带 `for update of iy,p,sp,r`。

问题是顺序：

| 步骤 | 位置 | 是否持有年度行锁 |
| --- | --- | --- |
| 读年度锁定版本 | `:722` → `:803` | **否** |
| 锁年度行 | `:890`（评定服务内） | 是 |
| 写年度（含版本列） | `:944-952` | 是 |

`:944` 写的 `$4` 是 `:803` 读到的值。`:722 → :890` 之间别人把年度锁到 R3 时，这里会用 R2
**无条件覆盖**（该 UPDATE 只有 `where id=$1`，没有版本条件）。而 `import_records.inspection_year_id`
**没有唯一约束**，两条导入记录可以关联同一个年度，这个并发是真实可达的。

**修正**：把年度行锁提到读之前（事务开始后立刻 `select ... from inspection_years where id=$1 for update`），
而不是新增第二把锁——两处加锁且顺序不一致会引入死锁风险。锁内读到的版本贯穿整个事务。

## 阻塞项四：保存校对草稿的事务与返回值

### 事务边界

当前：路由读 `ImportRecordDetail` → 路由解析版本 → 路由做关联校验 → 调 `save_review_draft()`，
而后者的 UPDATE 条件只有 `import_status='待校对'` 与编辑锁，**不含台账版本**。

**方案 A（推荐）：把解析、校验、写入放进同一个仓储事务**，事务内锁 `import_records` 与
关联的 `inspection_years`。不改对外契约，纯服务端改动。

（方案 B 是给 `save_review_draft()` 加预期版本参数并在 UPDATE 里加版本条件。不推荐：这里
**用户并没有在选版本**，他只是在存自己的校对结果，服务端自洽即可，不必新增一个前端必须
填对的字段。上一轮给绑定链路加 `expected_inventory_revision_id` 是另一回事——那里用户面前
摆着来自某个版本的候选列表。）

### 成功保存时是否锁定年度

**要锁**，条件是这份草稿确实含有版本相关数据：至少一条病害带 `bridge_component_id`，
或带依赖构件映射的评定树关联。理由是绑定与 Word 导入在写版本化病害数据时都会锁年度，
草稿保存写的是同一类数据，不跟上就会出现：

1. 草稿按 R2 校验并保存；
2. 事务提交后年度仍未锁定；
3. 别人确认了 R3；
4. 下次加载按统一规则解析成 R3；
5. 刚存的草稿立刻变成旧版本数据。

规则：年度已锁定则严格用已有版本；未锁定则锁定本次解析出的版本；锁定失败返回 409；
保存或后续校验失败时锁定与草稿写入一起回滚。

空草稿或不含任何构件绑定的草稿**不锁**——没有理由让一次纯文本编辑给年度定版本。

### 返回值要结构化

`save_review_draft()` 现在返回 `bool`（`ReviewRepository.hpp:99`），路由把 false 统一映射成
409 `import_record_not_editable`（`ReviewRoutes.cpp:369`）。新事务内至少会出现七类失败，
必须能分辨：

```cpp
struct SaveReviewDraftOutcome {
    bool success{false};
    std::string error_code;
    std::string error_message;
    review::DraftValidationResult validation;   // 逐项问题，仅校验类失败时有值
};
```

| 错误码 | HTTP |
| --- | --- |
| `component_inventory_revision_changed` | 409（整体提示一次，不逐条） |
| `edit_lock_invalid` | 按项目现有约定 |
| `import_record_not_editable` | 409 |
| `defect_component_assignment_invalid` / `rating_tree_assignment_invalid` | 400，带逐项 details |
| `database_commit_failed` | 503 |

**版本变化只报一条整体提示**（"本检测年度使用的台账版本已变化，请刷新后重试"），而不是逐条
"请重新选择"——病害自己带着版本 id，服务端分得清"版本整体变了"与"这一条绑错了"。

## 修正方案

六处统一改用现成的规则，不新增解析逻辑：

| 需要 | 用 |
| --- | --- |
| 只要版本 id / 是否已确认 | `resolve_confirmed_revision_ref(bridge_id, locked_revision_id)` |
| 需要遍历构件或映射 | `resolve_confirmed_revision(bridge_id, locked_revision_id)` |

**本轮六处大多需要完整版本**：① `validate_defect_component_associations()` 要遍历；
②③ preflight 标志 + 评定树校验；④ 要按构件 id 查映射；⑤ 构件匹配要遍历；⑥ 与 ⑤ **共用
同一个对象**。②③ 的"是否已确认"标志随之简化为 `inventory.has_value()`。
**不要先调 ref 再调完整版**：两次查询，非事务场景下还可能落在两个快照上。

### 两个解析 API 的去留

- **`get_latest_revision()`**：六处迁完后**零生产调用方**。管理页用的是
  `find_latest_revision_id()`（同样草稿优先），不受影响。建议删除。
- **`get_latest_confirmed_revision()`**：**现在就已经是死代码**——修缺陷一时加的，
  批次三把 `resolve_confirmed_revision_ref()` 改成直接写 SQL 之后就孤立了。一并删除。

### 实施约束：上下文里缺锁定版本 id

| 处 | 现有上下文 | 办法 |
| --- | --- | --- |
| ①②④ | `ImportRecordDetail`（有 `inspection_year_id`，无版本 id，`ReviewModels.hpp:71-112`） | 给 `get_import_record_detail()` 的 SELECT 补 `iy.component_inventory_revision_id`，结构体加一个字段——一处改动，三处受益 |
| ③ | 事务内的 `record_row` | **无需改取数**：`ReviewRepository.cpp:722` 已经选了该列 |
| ⑤⑥ | 已有 `locked_revision_id` / `inventory_revision_id` | 直接换调用 |

**新字段只作服务端内部上下文，不进对外 JSON。** `ImportRecordDetail` 同时用于
`build_review_response()`，那是手写的 JSON 拼装，加字段不会自动序列化；实施时要有一条测试
钉住 review 接口响应形状没变。

## 命名与提示要同步改

统一规则是"年度锁定优先"，所以年度锁着 R1、桥上已有更新的已确认 R2 时，**正确行为是继续
用 R1**。以下叫法与提示因此不准确：

| 现在 | 改为 |
| --- | --- |
| `latest_revision` / `latest_inventory` | `resolved_revision` / `resolved_inventory` |
| "病害关联的实际构件不属于当前桥梁最新台账。" | "……不属于本检测年度使用的构件台账。" |
| "桥梁最新构件台账尚未确认……" | "本检测年度没有可用的已确认构件台账……" |

要求是**阻塞行为与错误码不变**，允许修正误导性的"最新"表述。

## 实施批次

复审给的顺序是对的，采纳：

**批次一：评定服务的版本上下文**
给 `AssessmentConfirmationService` 增加显式版本能力（含"属于同桥且已确认"的校验）；
② 只读预检用 override 且不写年度；③ 确认事务改为在年度行锁内先锁版本再调服务。
**没有这一批，后面几批都不闭环。**

**批次二：Word 导入的事务顺序（⑤⑥）**
先锁 import 与 year、稳定版本、两处匹配共用同一对象、锁定失败即回滚；并把版本冲突从
"删除失败导入"路径里摘出来，改成可重试。

**批次三：校对草稿事务（①）**
定义 `SaveReviewDraftOutcome`；把解析、校验、年度锁定、草稿 UPDATE 放进同一事务；
路由按结构化错误映射 HTTP 与提示。

**批次四：其余替换与清理**
④ 与确认链路剩余部分的替换；删除 `get_latest_revision()` 与 `get_latest_confirmed_revision()`；
改 `latest_*` 命名与两条提示；补齐历史锁定版本与并发测试。

## 测试

已有的 `OverviewStaysConfirmedWhileADraftExists`（`test_import_binding_repository.cpp`）
是现成模板：夹具用 `add_revision(2, /*confirmed=*/false)` 造草稿即可。

**版本选择（六处共用）**

- 年度锁定 R1，桥上另有已确认 R2 与草稿 R3 → 六处都必须用 **R1**；
- 年度未锁定，存在已确认 R1、R2 与草稿 R3 → 必须用 **R2**；
- 年度锁定的版本属于别的桥、是草稿、或已不存在 → 解析失败；
- 桥上没有任何已确认版本 → ②③ 仍然阻塞（**行为与错误码不变，提示文案可改**）。

最后一条容易在改动中丢失：把 `status` 判断换成 `has_value()` 之后，"没有已确认版本"与
"取到草稿"归进了同一分支，必须确认前者的阻塞行为没变。

**评定服务**

- 年度未锁定但桥上有已确认台账时，只读预检用显式版本算出结果；
- 只读预检结束后 `inspection_years.component_inventory_revision_id` **仍为 null**；
- 显式版本属于别的桥、是草稿、或不存在时，上下文构建失败；
- 年度已锁定时优先用锁定版本，override 不得覆盖它。

**年度确认事务**

- 调评定服务前版本已在年度行锁内锁定（或以同一版本传入）；
- 两条导入记录共享同一年度时不会互相覆盖年度版本；
- 最终 UPDATE 不覆盖并发锁定的不同版本；
- 评定或确认失败时，事务内的版本锁定一并回滚。

**Word 导入**

- 匹配开始前版本已稳定；构件匹配与评定树匹配用**同一个** revision id；
- 年度锁定更新 0 行时不得继续提交；
- **版本冲突不删除导入记录与原始 Word，且可重试**；
- 版本冲突不进入 `discard_failed_import_safely()`；
- 真正的解析失败仍按原流程清理。

**校对草稿保存**

- 桥上有草稿时，保存带已绑定病害的草稿成功；
- 年度锁着历史 R1 时不改用更新的已确认 R2；
- 含构件绑定的草稿保存成功后年度被锁定；空草稿不锁；
- 校验完成后版本变化 → 不写入旧版本数据，返回 `component_inventory_revision_changed`；
- `SaveReviewDraftOutcome` 能区分版本冲突、校验失败、编辑锁失效与状态变化；
- 版本冲突只报整体提示；单病害数据错误仍返回逐项 details；
- 事务失败时版本锁定与草稿写入一起回滚。

**死代码与管理页**

- 迁移完成后生产代码不再调用 `get_latest_revision()`；
- `find_latest_revision_id()` 仍保持草稿优先，管理页仍能看见草稿。

## 验收

1. 在已确认台账上编辑派生出草稿后：校对草稿可保存、入库前检查可通过、年度可确认、
   ④ 给出的评定树节点来自已确认版本；
2. **年度未锁定且桥上有已确认台账时，入库前检查可以通过，且检查本身不写年度版本**；
3. Word 导入到带草稿的桥：匹配前版本已稳定，两次匹配同版本，年度锁到该版本；
   版本冲突不删除导入记录；
4. 年度锁定历史版本时，六处都用锁定版本；
5. 桥上没有已确认版本时，②③ 的阻塞行为与错误码不变；
6. 生产代码不再调用 `get_latest_revision()`；
7. review 接口的响应 JSON 形状未变。

## 非目标

- 不改绑定链路：它已经在用正确的规则。
- 不改 `find_latest_revision_id()` 的草稿优先排序：台账管理页依赖它。
- 不引入新的版本解析规则；本轮是把各处接到既有规则上，不是设计新契约。
- 不给 review 接口新增对外字段。
