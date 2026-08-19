# 已确认台账版本解析的一致性

- 日期：2026-08-18（当日三轮评审后修订）
- 状态：已按第三轮复审修订；④ 与 ⑤⑥ 已实施（`434b21e`、`3b8c43b`），①②③ 待实施
- 相关模块：校对保存、入库前检查、年度确认、评定树自动匹配、Word 导入、系统评定
- 前序：`2026-08-17-component-binding-on-demand-lookup-design.md`（缺陷一修的是同源问题的另外两处）
- 评审记录：`…-design-review.txt`、`…-design-rereview.txt`、`…-design-third-review.txt`

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

**三稿的错误与遗漏**（第三轮复审指出，已核实）：

- **锁定时机定错了位置**。三稿写"调评定服务之前锁版本"，但确认事务 `:816` 先建
  preflight、`:817-821` 直接返回，`:873-884` 再挡一次——**两处都在评定服务（`:886`）
  之前**。版本解析与锁定必须提到 `build_preflight_report()` **之前**。
- **评定服务接口没写完整**。三稿写"（二选一，推荐前者）"却只给了一个签名；而且只加
  `load_context()` 并不能让 `calculate()` 用上调用方给的版本。
- **override 优先级前后矛盾**：能力一节说"有 override 就用 override"，测试一节又说
  "年度已锁定时 override 不得覆盖"。
- **错误码写错**：实际是 `defect_rating_tree_assignment_invalid`（`DraftValidation.cpp:311`），
  不是 `rating_tree_assignment_invalid`；现有约定 `database_commit_failed` 与
  `db_write_failed` **都映射 HTTP 500**（`ImportConfirmRoutes.cpp:209-217`），三稿写 503
  等于悄悄改约定。
- **漏了两个分支**：Word 导入"桥上没有已确认台账"时现在是降级而非阻断；版本冲突后光
  不删除还不够重试。
- **锁顺序表述不准**：事务开始时只有 `import_record_id`，得先锁 `import_records` 才拿得到
  年度 id。

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
- **③（写事务）**：把版本解析与锁定提到 **`build_preflight_report()` 之前**——不是
  "调评定服务之前"。确认事务 `:816` 先建 preflight、`:817-821` 就直接返回了，
  `:873-884` 还会再以"缺 `component_inventory_revision_id`"为由挡一次，根本走不到
  评定服务。

### 评定服务需要的能力

改的必须是 `calculate()` 本身——只加一个 `load_context()` 而 `calculate()` 仍自行重读
`inspection_years`，原问题原样还在：

```cpp
AssessmentConfirmationOutcome calculate(
    const std::string& inspection_year_id,
    const Json::Value& draft,
    const std::optional<std::string>& inventory_revision_override = std::nullopt) const;
```

（若改成让 `calculate()` 接收已加载好的 `AssessmentContextSnapshot`，必须一并说明
evaluator 与 `StandardPackage` 怎么随之传入，不能留一个没人消费的 `load_context()`。）

**override 与年度锁定版本的优先级**（三稿此处自相矛盾，这里定死）：

| 年度是否已锁定 | override | 行为 |
| --- | --- | --- |
| 未锁定 | 未传 | 上下文不完整，维持现状 |
| 未锁定 | 传入且合法 | 用 override；**只读预检不得写年度** |
| 已锁定 | 未传 | 用年度锁定版本 |
| 已锁定 | 与锁定版本相同 | 正常使用 |
| 已锁定 | 与锁定版本**不同** | 返回 `component_inventory_revision_changed` |

override 不得静默覆盖年度锁定版本，也不得静默忽略一个冲突的 override。传入 override 时
**必须校验**它属于同一桥梁且状态为已确认——它不是绕过校验的后门。

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

**三个分支必须分清**，其中第三个是现有行为，不能改掉：

| 情形 | 处置 |
| --- | --- |
| 年度未锁定，桥上有已确认版本 | 锁定该版本，两次匹配共用同一个 `InventoryRevision` |
| 解析不出可用的已确认版本（桥上没有，**或年度锁着草稿**） | **不锁年度，两次匹配共享 `nullopt`，保留现有降级**：清空候选、置空版本 id、加 `defect_component_match_required` 警告，解析结果照常进待校对 |
| `lock_pending_year_revision()` 返回 false（并发抢锁） | 唯一的硬失败：回滚并返回 `component_inventory_revision_changed` |

**实施时订正了本文档原先的三分支表**：原表把"年度锁定到非法版本"单列为失败。但
`validate_inspection_year_inventory_revision()`（`011:366-371`）只在年度状态为
已确认/已被修订/已归档时才要求版本已确认——**待校对年度可以合法地锁在草稿上**。
把这种情况判成失败会让这类年度的导入直接挂掉，而今天它是降级的。因此两种"解析不出"
合并到同一分支，只有并发抢锁才失败。

## 阻塞项二：Word 版本冲突会被当成解析失败删掉

`WordImportRoutes.cpp:376-383` 把 `persist_parse_result()` 的**任何**失败交给 `fail_parse`，
后者调 `discard_failed_import_safely()`（`:329-335`），它会删除导入记录与相关文件。

于是一个可恢复的并发版本冲突会导致：Python 解析已成功、照片已归档，只因年度被别人先锁到
另一个版本，**整条导入记录、临时源文件与归档一起被删**。

**修正**：

1. 按阻塞项一的新顺序，冲突通常在匹配前就暴露，代价小；
2. 仍可能冲突时用专用错误码 `component_inventory_revision_changed`；
3. **`WordImportRoutes` 遇到该错误码不得调用 `discard_failed_import_safely()`**。

**但"不删除"本身不足以可重试**：`mark_parsing()` 只接受 `('已上传','解析失败')`
（`WordImportRepository.cpp:265`），跳过删除会把记录**卡在"解析中"**，永远重试不了。
冲突分支至少要做完：

1. 清理本次尚未提交的照片批次与 staging 目录；
2. 清空 `active_parse_work_relative_path`；
3. 把 `import_records` 与 `import_source_files` 转成"解析失败"——现成的
   `mark_parse_failed()` 正好做这件事，且**必须在不执行删除的前提下调用**；
4. 保留原始 Word；
5. HTTP 返回 409 `component_inventory_revision_changed`；
6. 确认之后 `mark_parsing()` 能重新进入解析。

真正的契约解析失败仍走原有清理流程——这条不能一起改掉。

### 实施笔记：仓库对象不能留在外层作用域

`ComponentInventoryRepository` 的构造函数**按值收下 `DbClientPtr` 并一直持有**。在
`persist_parse_result()` 里留一个具名变量（`ComponentInventoryRepository inventories(tx);`）
会让事务的 shared_ptr 活过 `tx.reset()`，提交回调永远不来，30 秒后以 `db_commit_failed`
超时——而且报出来的是"提交失败"，看不出真正原因。一律用临时量：
`ComponentInventoryRepository(tx).resolve_confirmed_revision(...)`。

①③ 改事务时会遇到同一个坑。

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

**修正**：把年度行锁提到读之前，而不是新增第二把锁——两处加锁且顺序不一致会引入死锁
风险。事务开始时只有 `import_record_id`，所以顺序是：

1. `select ... from import_records ... for update`，锁并读导入记录；
2. 从中取 `inspection_year_id`；
3. `select ... from inspection_years ... for update`，同时读年度状态、锁定版本与规范组合；
4. 之后全部使用**年度行锁内**读到的数据。

**统一锁顺序 `import_records → inspection_years`**，与现有绑定事务一致。不要再用
"联查 `inspection_years` 字段但只 `for update of ir`"的写法——那样年度字段仍是在没有
年度行锁的情况下读的。

## 阻塞项四：保存校对草稿的事务与返回值

### 事务边界

当前：路由读 `ImportRecordDetail` → 路由解析版本 → 路由做关联校验 → 调 `save_review_draft()`，
而后者的 UPDATE 条件只有 `import_status='待校对'` 与编辑锁，**不含台账版本**。

**方案 A（推荐）：把解析、校验、写入放进同一个仓储事务**，事务内锁 `import_records` 与
关联的 `inspection_years`。不改对外契约，纯服务端改动。

**事务内必须重新读取**下列内容——只把台账解析与 UPDATE 搬进去、却仍用事务外读到的数据，
旧快照照样能覆盖新数据：

1. `import_status`、编辑锁、当前 `parsed_result_json`；
2. `inspection_year_id`、年度状态、锁定台账版本；
3. `standard_profile_id`、`rating_tree_version_id`、技术规范包；
4. 当前已发布的评定树；
5. 统一解析出的完整 `InventoryRevision`；
6. 依赖 `stored_draft` 的证据校验、评定树规范化、重开范围校验与审计基线。

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
| `defect_component_assignment_invalid` / `defect_rating_tree_assignment_invalid` | 400，带逐项 details |
| `db_write_failed`（事务内 SQL/约束异常） | 500 |
| `database_commit_failed`（提交回调失败） | 500 |

后两个错误码**沿用 `confirm_annual_facts()` 的现有约定**（`ReviewRepository.cpp:1103`、
`:1110-1112`，路由映射见 `ImportConfirmRoutes.cpp:209-217`），两者都是 500 而不是 503，
且必须能分别返回——三稿只列了 `database_commit_failed` 且写成 503，是把现有约定改掉了。
评定树关联的错误码带 `defect_` 前缀（`DraftValidation.cpp:311`）。

**版本变化只报一条整体提示**（"本检测年度使用的台账版本已变化，请刷新后重试"），而不是逐条
"请重新选择"。要兑现这句承诺，仓储必须**先做整请求级的版本判定，再做逐项关联校验**——
`DraftValidation.cpp:194-196` 现在把版本不一致**全部**变成逐项问题。判定规则：

| 情形 | 处置 |
| --- | --- |
| 全部版本化病害一致地引用同一个旧版本，而服务端解析出的是新版本 | 409 `component_inventory_revision_changed`，整体一条 |
| 请求内部**混用**多个版本 | 400，视为草稿数据非法，返回逐项问题 |
| 版本相同，但构件已停用 / 类别或结构部位对不上 | 400，逐项 details |
| 年度锁定版本自身非法（别的桥 / 草稿 / 不存在） | 返回年度上下文或版本错误，**不要伪装成单病害错误** |

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
- 年度已锁定且 override **相同** → 正常算出结果；
- 年度已锁定且 override **不同** → 返回 `component_inventory_revision_changed`
  （既不静默用锁定版本，也不静默用 override）。

**年度确认事务**

- **年度未锁定且桥上有已确认台账时，版本在 `build_preflight_report()` 之前就已锁定**——
  锁在评定服务之前是不够的，preflight 会先把事务挡回去；
- preflight、评定树校验与评定服务用的是**同一个** revision id；
- 锁顺序为 `import_records → inspection_years`；
- 两条导入记录共享同一年度时不会互相覆盖年度版本；
- 最终 UPDATE 不覆盖并发锁定的不同版本；
- 评定或确认失败时，事务内的版本锁定一并回滚。

**Word 导入**

- 匹配开始前版本已稳定；构件匹配与评定树匹配用**同一个** revision id；
- 年度锁定更新 0 行时不得继续提交；
- **版本冲突不删除导入记录与原始 Word，且可重试**；
- 版本冲突不进入 `discard_failed_import_safely()`；
- **年度未锁定且桥上没有已确认版本 → 解析结果照常入库并带 `defect_component_match_required`
  警告**（现有降级行为不得变成导入失败）；
- 版本冲突后 `import_records` 与来源文件**不滞留在"解析中"**，下一次 `mark_parsing()`
  能成功；冲突分支清理本次照片批次与 staging 目录；
- 年度锁定到非法版本时不被当成普通契约解析失败；
- 真正的解析失败仍按原流程清理。

**校对草稿保存**

- 桥上有草稿时，保存带已绑定病害的草稿成功；
- 年度锁着历史 R1 时不改用更新的已确认 R2；
- 含构件绑定的草稿保存成功后年度被锁定；空草稿不锁；
- 校验完成后版本变化 → 不写入旧版本数据，返回 `component_inventory_revision_changed`；
- `SaveReviewDraftOutcome` 能区分版本冲突、校验失败、编辑锁失效与状态变化；
- 版本冲突只报整体提示；单病害数据错误仍返回逐项 details；
- **请求内部混用多个版本 → 400 逐项问题**，不是整体 409；
- 版本相同但构件已停用 / 类别或结构部位对不上 → 400 逐项 details；
- `db_write_failed` 与 `database_commit_failed` 能**分别**返回并各自映射（都为 500）；
- 评定树关联的错误码用 `defect_rating_tree_assignment_invalid`，与现有代码一致；
- 事务内重新读取 `stored_draft`、年度版本、规范组合与已发布评定树；
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
