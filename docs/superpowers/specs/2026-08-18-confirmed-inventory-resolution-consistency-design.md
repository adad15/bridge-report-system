# 已确认台账版本解析的一致性

- 日期：2026-08-18（当日按评审修订）
- 状态：已按评审修订，待复审
- 相关模块：校对保存、入库前检查、年度确认、评定树自动匹配、Word 导入
- 前序：`2026-08-17-component-binding-on-demand-lookup-design.md`（缺陷一修的是同源问题的另外两处）
- 评审记录：`…-design-review.txt`

## 背景

绑定链路已经统一到一条版本解析规则：**检测年度锁定的版本优先，年度未锁定时取该桥
最新的已确认版本**（`ComponentInventoryRepository::resolve_confirmed_revision_ref()`）。
绑定写入病害时，写进 `component_inventory_revision_id` 的就是这条规则解析出来的版本。

清理 `/latest` 死代码时把 `get_latest_revision()` 的调用方全部过了一遍，发现**还有
六处**没有走这条规则，而是用了草稿优先的 `get_latest_revision()`：

```sql
order by (status='草稿') desc, revision_number desc limit 1
```

## 修订说明：初稿错在哪

保留此节，供日后回看时知道为什么这样设计。评审指出初稿三处事实错误，都已核实：

- **年度确认的事务查询并不需要"补选一列"**——`ReviewRepository.cpp:722` 已经选了
  `iy.component_inventory_revision_id`，`:803-804` 也已经读进 preflight 上下文。
- **`get_latest_revision()` 迁移后不是"只剩台账管理页一个调用者"，而是零调用者**。
  台账管理页用的是 `find_latest_revision_id()`（`ComponentInventoryRoutes.cpp:449`）；
  同文件 `:436` 那句 `get_latest_revision` 是**注释**，写的恰恰是"绝不走它"。
- **④ 不是"为病害挑构件"**。`DefectRatingTreeMatching.cpp:73` 读的是病害上**已绑**的
  `bridge_component_id`，`:85` 拿它去台账里找映射，用于挑评定树节点。它从不写构件。

此外初稿把 ③ 说成"在确认事务内重复一遍同样的解析"，也不对：那条事务的 preflight
上下文**已经用年度锁定版本**并单独查它的真实状态（`:805-813`，还带 `for update`），
草稿优先的 `:837` 只喂给评定树校验。③ 只坏了一半。

## 问题的本质：写入方与校验方对"哪个版本"意见不一致

单看"草稿优先"本身并不必然是错的——问题在于**同一份数据，写的时候按 A 版本，校验的
时候按 B 版本**。

- **写**：绑定把已确认版本的 id 写进每条病害（`ImportBindingRepository.cpp`
  `write_binding()` 的第五个参数）。
- **校验**：保存校对草稿、入库前检查、年度确认拿这个 id 去跟 `get_latest_revision()`
  的结果比。

桥上没有草稿时两者恰好相等，所以一直没暴露。**一旦存在草稿，两边必然不等。**

## 触发条件是日常操作

草稿不是异常状态：`ensure_editable_target()` 被每一个台账写操作调用
（`update_entry` / `add_entry` / `delete_entry` / `deactivate_entry` / `set_mapping`），
**在已确认台账上改一个构件编号、加一条构件、设一次映射，都会派生出草稿**。

现网当前尚未触发：开发库里 1 座有台账的桥、0 个草稿。这组缺陷是**潜伏**的，第一次有人
编辑已确认台账就会同时点着六处。

## 六处清单

| # | 位置 | 现在取的版本 | 有草稿时的后果 | 严重度 |
| --- | --- | --- | --- | --- |
| ① | `ReviewRoutes.cpp:271` 保存校对草稿 | 桥梁最新（草稿优先） | **每条已绑定病害报错，草稿存不了盘** | 阻断 |
| ② | `ImportConfirmRoutes.cpp:71` 入库前检查 | 同上 | **判定"台账尚未确认"，确认不了** | 阻断 |
| ③ | `ReviewRepository.cpp:837` 确认事务内的评定树校验 | 同上 | 评定树校验按错版本（preflight 部分本身是对的） | 阻断 |
| ④ | `DefectMatchingRoutes.cpp:188` 评定树自动匹配 | 同上 | 按草稿映射挑节点；草稿里停用/改号的构件查不到 | 污染 |
| ⑤ | `WordImportRepository.cpp:67` 导入构件匹配 | 年度锁定优先，否则桥梁最新 | 按草稿匹配；年度不锁版本、病害不带版本 id | 降级 |
| ⑥ | `WordImportRepository.cpp:164` 导入评定树匹配 | 同上 | 同 ⑤，且与 ⑤ 各解析一次 | 降级 |

### ① 保存校对草稿——最硬的一处

`DraftValidation.cpp:194`：

```cpp
if (!latest_revision.has_value() || revision_id != latest_revision->id) {
    result.issues.push_back({path, "关联所依据的构件台账已变化，请重新选择。"});
    continue;
}
```

左边是绑定时写入的已确认版本，右边是草稿优先解析的结果。有草稿时**每一条已绑定病害
都会进 issues**，接口返回 400，整份校对草稿保存不下去。用户看到的是一串"请重新选择"，
而重新绑定并不能解决——绑定写回的仍然是已确认版本。

### ② 入库前检查

`PreflightReport.cpp:139-147` 在标志为假时直接落阻塞项；`:151-155` 还会逐条比对
`component_inventory_revision_id`。② 传进来的标志是 `inventory->status == "已确认"`，
取到草稿时为假。

②**还需要完整台账**：`ImportConfirmRoutes.cpp:102-107` 把同一个 `inventory` 传给
`validate_defect_rating_tree_for_confirmation()`，那个函数要遍历构件与映射。

### ③ 年度确认事务——只坏了一半

preflight 上下文那半是对的：`:803-804` 用年度锁定版本，`:805-813` 单独查它的真实状态
（带 `for update`）。**错的是 `:837` 另起一条草稿优先解析**，结果只喂给
`validate_defect_rating_tree_for_confirmation()`。

因此 ③ 的修法不是补取数，而是**删掉那条多余的解析**，改用查询里已有的
`inventory_revision_id` 走统一规则，并把同一个完整版本同时用于 preflight 与评定树校验。

### ④ 评定树自动匹配

`DefectRatingTreeMatching.cpp:73` 读病害上**已绑**的 `bridge_component_id`，`:85` 在台账
里按它找生效映射，用映射的规范类别去挑评定树节点。**它不选构件。**

草稿优先的真实后果：

1. 用草稿里的新映射挑出错误的规范类别与评定树节点；
2. 已确认版本里有效的构件，在草稿中被停用或改了映射时，查不到而被判成无法匹配。

结果由页面落进本地草稿再保存，于是持续制造出让 ①②③ 报错的数据。

### ⑤ / ⑥ Word 导入

`WordImportRepository.cpp:65-67` 的形状与修正前的 `resolve_confirmed_revision()`
一模一样；`:81` 只在 `status == "已确认"` 时才置 `confirmed_revision_id`，取到草稿时留空，
于是 `:353-360` 那段"把版本锁进待校对年度"不会执行。

**但 ⑤⑥ 的问题不止于取错版本**，见下节两个阻塞项。

## 阻塞项一：Word 导入一次事务里解析两次

当前顺序是：`match_imported_defects()` 解析一次 → `match_imported_defect_rating_tree_nodes()`
另查年度再解析一次 → 两次匹配都完成后才写年度锁定。

即便两处都换成正确的解析器，**仍不保证取到同一版本**。PostgreSQL 默认 READ COMMITTED
下每条语句取新快照，同一事务里的两次查询可以看到不同的已提交数据：

1. 构件匹配解析到 R2；
2. 其他事务确认了 R3；
3. 评定树匹配解析到 R3；
4. 年度最后锁到 R2；
5. 构件关联基于 R2，评定树关联基于 R3。

**修正**：在 `persist_parse_result()` 里解析**一次**完整版本，把同一个对象（或 const 引用）
同时传给构件匹配与评定树匹配；`match_imported_defect_rating_tree_nodes_unguarded()`
不再自行解析。

## 阻塞项二：年度锁定的 UPDATE 没有检查结果

`WordImportRepository.cpp:353-360`：

```sql
update inspection_years set component_inventory_revision_id=$2::uuid,updated_at=now()
where id=$1::uuid and bridge_id=$3::uuid and status='待校对'
  and component_inventory_revision_id is null
```

没有 `RETURNING`，也没查受影响行数。并发下另一个事务先把年度锁到 R3 时，这条更新 0 行，
代码毫无察觉地继续提交——**病害写着 R2，年度锁着 R3**。

**修正**：改用 `ComponentInventoryRepository::lock_pending_year_revision()` 并检查返回值。
返回 false 时不能继续保存混合版本的数据，只能整体回滚（推荐）或重读年度版本后重做全部
匹配。

另需考虑读取关联年度时对 `inspection_years` 行加锁：当前只有 `for update of ir`，
锁住导入记录并不能阻止别人先锁年度版本。

## 阻塞项三：保存校对草稿的 TOCTOU

当前顺序是：路由读 `ImportRecordDetail` → 路由解析版本 → 路由做构件与评定树关联校验 →
调 `save_review_draft()`。而 `save_review_draft()` 的 UPDATE 条件只有
`import_status = '待校对'` 与编辑锁，**不含台账版本**。

给 `ImportRecordDetail` 加字段只能让正常情况下选对版本，挡不住"校验后、写入前版本变了"。

**两个方案**：

**方案 A（推荐）：把解析、校验、写入放进同一个仓储事务**，事务内锁 `import_records`
与关联的 `inspection_years`。不改任何对外契约，纯服务端改动。

**方案 B：给 `save_review_draft()` 加预期版本参数**，在最终 UPDATE 里加版本条件，
不一致返回独立的冲突码（复用 `component_inventory_revision_changed` 与 409），
而不是普通的 `import_record_not_editable`。

推荐 A，理由是这里**用户并没有在选版本**——他只是在存自己的校对结果，服务端自洽即可，
不必新增一个前端必须正确填写的字段。批次二那轮给绑定链路加 `expected_inventory_revision_id`
是另一回事：那里用户面前摆着来自某个版本的候选列表，版本变了必须让他知道。

采用 A 时**错误提示要一并改好**：版本确实变了的时候，应当整体报一次"本检测年度使用的
台账版本已变化，请刷新后重试"，而不是逐条报"请重新选择"——病害自己带着
`component_inventory_revision_id`，服务端有足够信息分辨这两种情况。

## 修正方案

六处统一改用现成的规则，不新增解析逻辑：

| 需要 | 用 |
| --- | --- |
| 只要版本 id / 是否已确认 | `resolve_confirmed_revision_ref(bridge_id, locked_revision_id)` |
| 需要遍历构件或映射 | `resolve_confirmed_revision(bridge_id, locked_revision_id)` |

**订正初稿的判断**：本轮六处**大多需要完整版本**，不是只要 ref。

| 处 | 需要 | 原因 |
| --- | --- | --- |
| ① | 完整 | `validate_defect_component_associations()` 要遍历 entries 与 mappings |
| ② | 完整 | preflight 标志 + `validate_defect_rating_tree_for_confirmation()` |
| ③ | 完整 | 同 ②，且要与 preflight 用同一个对象 |
| ④ | 完整 | `match_defect_rating_tree_nodes()` 要按构件 id 查映射 |
| ⑤ | 完整 | 构件匹配要遍历 entries |
| ⑥ | 完整 | 与 ⑤ **共用同一个对象**，不得自行解析 |

②③ 的"是否已确认"标志随之简化为 `inventory.has_value()`——该函数按定义只返回已确认
版本。**不要先调 ref 再调完整版**：那是两次查询，非事务场景下还可能落在两个快照上。

### 两个解析 API 的去留

- **`get_latest_revision()`**：六处迁完后**零生产调用方**。台账管理页用的是
  `find_latest_revision_id()`（同样草稿优先），不受影响。建议删除；若暂留，注释必须写明
  "已无生产调用者"，不能再声称管理页依赖它。
- **`get_latest_confirmed_revision()`**：**现在就已经是死代码**。它是修缺陷一时加的，
  批次三把 `resolve_confirmed_revision_ref()` 改成直接写 SQL 之后就孤立了。一并删除，
  避免三个相近 API 继续诱发误用。

### 实施约束：上下文里缺锁定版本 id

规则的第一个参数是"年度锁定的版本"，而 ①②④ 手上只有 `ImportRecordDetail`，它有
`inspection_year_id` 但**没有** `component_inventory_revision_id`（`ReviewModels.hpp:71-112`）。

| 处 | 现有上下文 | 办法 |
| --- | --- | --- |
| ①②④ | `ImportRecordDetail` | 给 `get_import_record_detail()` 的 SELECT 补 `iy.component_inventory_revision_id`，结构体加一个字段——一处改动，三处受益 |
| ③ | 事务内的 `record_row` | **无需改取数**：`ReviewRepository.cpp:722` 已经选了该列 |
| ⑤⑥ | 已有 `locked_revision_id` / `inventory_revision_id` | 直接换调用 |

**新字段只作服务端内部上下文，不进对外 JSON。** `ImportRecordDetail` 同时用于
`build_review_response()`，但那是手写的 JSON 拼装，加结构体字段不会自动序列化；实施时
要有一条测试钉住 review 接口的响应形状没变。

### 快照边界的说明

①②④ 会先 `get_import_record_detail()` 再解析版本，两步不在同一快照里：第一步读到年度
未锁定、第二步之前别人锁了另一个版本时，调用方仍会按"最新已确认"回退。

- ①（写操作）由方案 A 的事务解决：事务内重读并锁年度。
- ②（只读预检）允许短暂过期——最终确认事务会复核，这是它本来的定位。
- ④（只读计算）结果保存时还要再过一遍 ① 的校验，同样由 ① 兜住。

## 命名与提示要同步改

统一规则是"年度锁定优先"，所以当年度锁着 R1、桥上已有更新的已确认 R2 时，**正确行为是
继续用 R1**。以下叫法与提示因此变得不准确：

| 现在 | 改为 |
| --- | --- |
| `latest_revision` / `latest_inventory` | `resolved_revision` / `resolved_inventory` |
| "病害关联的实际构件不属于当前桥梁最新台账。" | "……不属于本检测年度使用的构件台账。" |
| "桥梁最新构件台账尚未确认，不能正式确认年度病害事实。" | "本检测年度没有可用的已确认构件台账，不能正式确认年度病害事实。" |

**初稿"提示逐字不变"那条验收作废**：要求改为**阻塞行为与错误码不变**，但允许修正
误导性的"最新"表述。

## 实施批次

**批次一：清理与准备**
删除已死的 `get_latest_confirmed_revision()`；给 `ImportRecordDetail` 加内部字段并补
`get_import_record_detail()` 的 SELECT 与赋值；钉住 review 接口响应形状不变。

**批次二：Word 导入原子化（⑤⑥）**
`persist_parse_result()` 内解析一次完整版本，构件匹配与评定树匹配共用；改用
`lock_pending_year_revision()` 并检查返回值，失败即回滚。上下文自足，可独立验证。

**批次三：确认链路（②③④）**
三处各解析一次完整版本并全程复用。②③ 校验同一份数据，④ 制造那份数据，**内部不可再拆**
——分开改会出现"匹配按新规则、校验按旧规则"的中间态。

**批次四：保存校对草稿（①）**
按方案 A 建立解析—校验—写入的事务一致性，并改好版本变化时的整体提示。最难的一批，
放最后。

**批次五：收尾**
删除 `get_latest_revision()`；改 `latest_*` 命名与两条用户提示；确认台账管理页仍走
`find_latest_revision_id()` 且仍能看见草稿。

## 测试

已有的 `OverviewStaysConfirmedWhileADraftExists`（`test_import_binding_repository.cpp`）
是现成模板：夹具用 `add_revision(2, /*confirmed=*/false)` 造草稿即可。

**版本选择（六处共用）**

- 年度锁定 R1，桥上另有已确认 R2 与草稿 R3 → 六处都必须用 **R1**；
- 年度未锁定，存在已确认 R1、R2 与草稿 R3 → 必须用 **R2**；
- 年度锁定的版本属于别的桥、是草稿、或已不存在 → 解析失败；
- 桥上没有任何已确认版本 → ②③ 仍然阻塞（**行为与错误码不变，提示文案可改**）。

最后一条容易在改动中丢失：把 `status` 判断换成 `has_value()` 之后，"没有已确认版本"
与"取到草稿"归进了同一分支，必须确认前者的阻塞行为没有变。

**Word 导入**

- 构件匹配与评定树匹配用的是**同一个** revision id；
- 年度锁定更新 0 行时不得继续提交；
- 并发把年度先锁到别的版本时，导入回滚（或重做全部匹配）；
- 病害上的 `component_inventory_revision_id` 与年度锁定版本一致；
- 评定树匹配所用的映射与构件匹配来自同一版本。

**校对保存**

- 桥上有草稿时，保存带已绑定病害的草稿成功；
- 年度锁着历史 R1 时不改用更新的已确认 R2；
- 校验完成后版本变化 → 不得写入旧版本数据，返回明确的版本冲突（不是普通的编辑状态错误）；
- 版本确实变化时报的是整体提示，不是逐条"请重新选择"。

**确认与自动匹配**

- 入库前检查与事务内确认用同一套解析规则，结论一致；
- ④ 返回的评定树节点与规范映射**来自已确认/年度锁定版本**，不是草稿——断言要落在节点与
  映射上，不是"构件属于已确认版本"（它本来就不选构件）；
- 草稿里的停用、改号、改映射不影响已锁定年度的匹配结果。

**死代码与管理页**

- 迁移完成后生产代码不再调用 `get_latest_revision()`；
- `find_latest_revision_id()` 仍保持草稿优先，台账管理页仍能看见草稿。

## 验收

1. 在已确认台账上编辑派生出草稿后：校对草稿可保存、入库前检查可通过、年度可确认、
   ④ 给出的评定树节点来自已确认版本；
2. Word 导入到带草稿的桥：年度锁到已确认版本，病害带上该版本 id，两次匹配同版本；
3. 年度锁定历史版本时，六处都用锁定版本而非更新的已确认版本；
4. 桥上没有已确认版本时，②③ 的阻塞行为与错误码不变；
5. **生产代码不再调用 `get_latest_revision()`**；台账管理页的草稿优先行为由
   `find_latest_revision_id()` 保证；
6. review 接口的响应 JSON 形状未变。

## 非目标

- 不改绑定链路：它已经在用正确的规则。
- 不改 `find_latest_revision_id()` 的草稿优先排序：台账管理页依赖它。
- 不引入新的版本解析规则；本轮是把六处接到既有规则上，不是设计新契约。
- 不给 review 接口新增对外字段。
