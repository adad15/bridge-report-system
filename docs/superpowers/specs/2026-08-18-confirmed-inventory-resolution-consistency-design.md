# 已确认台账版本解析的一致性

- 日期：2026-08-18
- 状态：待评审
- 相关模块：校对保存、入库前检查、年度确认、病害自动匹配、Word 导入
- 前序：`2026-08-17-component-binding-on-demand-lookup-design.md`（缺陷一修的是同源问题的另外两处）

## 背景

绑定链路已经统一到一条版本解析规则：**检测年度锁定的版本优先，年度未锁定时取该桥
最新的已确认版本**（`ComponentInventoryRepository::resolve_confirmed_revision_ref()`）。
绑定写入病害时，写进 `component_inventory_revision_id` 的就是这条规则解析出来的版本。

清理 `/latest` 死代码时把 `get_latest_revision()` 的调用方全部过了一遍，发现**还有
六处**没有走这条规则，而是用了草稿优先的 `get_latest_revision()`：

```sql
order by (status='草稿') desc, revision_number desc limit 1
```

## 问题的本质：写入方与校验方对"哪个版本"意见不一致

单看"草稿优先"本身并不必然是错的——问题在于**同一份数据，写的时候按 A 版本，校验的
时候按 B 版本**。

- **写**：绑定把已确认版本的 id 写进每条病害（`ImportBindingRepository.cpp`
  `write_binding()` 的第五个参数）。
- **校验**：保存校对草稿、入库前检查、年度确认都拿这个 id 去跟 `get_latest_revision()`
  的结果比。

桥上没有草稿时两者恰好相等，所以一直没暴露。**一旦存在草稿，两边必然不等。**

## 触发条件是日常操作

草稿不是异常状态：`ensure_editable_target()` 被每一个台账写操作调用
（`update_entry` / `add_entry` / `delete_entry` / `deactivate_entry` / `set_mapping`），
**在已确认台账上改一个构件编号、加一条构件、设一次映射，都会派生出草稿**。

现网当前尚未触发：开发库里 1 座有台账的桥、0 个草稿。也就是说这组缺陷是**潜伏**的，
第一次有人编辑已确认台账就会同时点着六处。

## 六处清单

| # | 位置 | 现在取的版本 | 有草稿时的后果 | 严重度 |
| --- | --- | --- | --- | --- |
| ① | `ReviewRoutes.cpp:271` 保存校对草稿 | 桥梁最新（草稿优先） | **每条已绑定病害报错，草稿存不了盘** | 阻断 |
| ② | `ImportConfirmRoutes.cpp:71` 入库前检查 | 同上 | **判定"台账尚未确认"，确认不了** | 阻断 |
| ③ | `ReviewRepository.cpp:837` 确认事务内复核 | 同上 | 同 ②，且评定树校验按错版本 | 阻断 |
| ④ | `DefectMatchingRoutes.cpp:188` 病害自动匹配 | 同上 | 匹配到草稿里的构件，落进草稿后被 ①②③ 判为不一致 | 污染 |
| ⑤ | `WordImportRepository.cpp:67` 导入构件匹配 | 年度锁定优先，否则桥梁最新 | 按草稿匹配；年度不锁版本、病害不带版本 id | 降级 |
| ⑥ | `WordImportRepository.cpp:164` 导入评定树匹配 | 同上 | 同 ⑤ | 降级 |

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

### ② / ③ 入库前检查与年度确认

`PreflightReport.cpp:139-147`：

```cpp
if (!*context.component_inventory_confirmed ||
    !context.component_inventory_revision_id.has_value()) {
    add_issue(blocking, "component_inventory_unconfirmed",
              "桥梁最新构件台账尚未确认，不能正式确认年度病害事实。");
    return;
}
```

② 传进来的标志是 `inventory->status == "已确认"`。取到草稿时它是 `false`，直接落一条
阻塞项。即便绕过这一条，`:151-155` 还会逐条比对
`component_inventory_revision_id == context.component_inventory_revision_id`，同样全不通过。

③ 在确认事务内重复一遍同样的解析，并把结果喂给
`validate_defect_rating_tree_for_confirmation()`。

### ④ 病害自动匹配

只读计算，不写 `parsed_result_json`（`DefectMatchingRoutes.cpp:189` 的注释说明了这点），
但结果由页面落进本地草稿再保存。匹配依据草稿台账，写回的构件 id 可能根本不在已确认
版本里，于是在 ①②③ 处被判为不一致。**它不直接坏，但会持续制造出让前三处报错的数据。**

### ⑤ / ⑥ Word 导入

`WordImportRepository.cpp:65-67` 的形状与修正前的 `resolve_confirmed_revision()`
一模一样：

```cpp
const auto revision = locked_revision_id.has_value()
    ? inventories.get_revision(*locked_revision_id)
    : inventories.get_latest_revision(bridge_id);
```

随后 `:81` 只在 `status == "已确认"` 时才置 `confirmed_revision_id`。取到草稿时该值留空，
于是 `:353-360` 那段"把版本锁进待校对年度"不会执行——年度始终不锁版本，病害也拿不到
`component_inventory_revision_id`，全部退回人工绑定。

## 修正方案

六处统一改用现成的规则，不新增任何解析逻辑：

| 需要 | 用 |
| --- | --- |
| 只要版本 id / 是否已确认 | `resolve_confirmed_revision_ref(bridge_id, locked_revision_id)` |
| 需要遍历构件 | `resolve_confirmed_revision(bridge_id, locked_revision_id)` |

②③ 的"是否已确认"标志随之简化：`ref.has_value()` 即为已确认——该函数按定义只返回已确认
版本，不必再单独判 `status`。

**`get_latest_revision()` 本身保留**：台账管理页要看见草稿，那是它唯一正当的用途。
可以考虑给它改名成 `get_latest_revision_including_draft()`，让下一个人在调用点就看出
语义，避免第七次踩同一个坑。

### 实施约束：上下文里缺锁定版本 id

规则的第一个参数是"年度锁定的版本"，而 ①②④ 手上都只有 `ImportRecordDetail`，
它有 `inspection_year_id` 但**没有** `component_inventory_revision_id`
（`ReviewModels.hpp:71-112`）。三处的取数各不相同：

| 处 | 现有上下文 | 取锁定版本的办法 |
| --- | --- | --- |
| ①②④ | `ImportRecordDetail` | 给 `get_import_record_detail()` 的查询补 `iy.component_inventory_revision_id`，结构体加一个字段——一处改动，三处受益 |
| ③ | 事务内的 `record_row` | 该查询已 `left join inspection_years`（`ReviewRepository.cpp:724`），补选一列即可 |
| ⑤⑥ | 已有 `locked_revision_id` / `inventory_revision_id` | 直接换调用，无需取数改动 |

给 `ImportRecordDetail` 加字段是本方案唯一的结构改动，且它本来就该有：这个结构体已经
带着年度的评定树版本与规范包 id，唯独漏了台账版本。

## 实施批次

**批次一：⑤⑥（导入路径）** —— 上下文齐全，纯换调用，无取数改动。可独立验证。

**批次二：①②③④（校对与确认路径）** —— 先给 `ImportRecordDetail` 补字段，再改四处。
它们互相耦合：①②③ 校验同一份数据，④ 制造那份数据，分开改会出现"匹配按新规则、校验按
旧规则"的中间态。

顺序可换，但**批次二内部不可再拆**。

## 测试

每一处都要有"桥上存在草稿时仍按已确认版本工作"的回归用例。已有的
`OverviewStaysConfirmedWhileADraftExists`（`test_import_binding_repository.cpp`）
是现成的模板：夹具用 `add_revision(2, /*confirmed=*/false)` 造一个草稿即可。

- ① 桥上有草稿时，保存带已绑定病害的校对草稿成功，不再出现"关联所依据的构件台账已变化"；
- ② 桥上有草稿时，入库前检查不报 `component_inventory_unconfirmed`；
- ③ 桥上有草稿时，年度确认不因台账版本被拒；
- ④ 自动匹配返回的构件属于已确认版本，不属于草稿；
- ⑤ 导入时年度未锁版本、桥上有草稿 → 仍按已确认版本匹配，并把该版本锁进待校对年度；
- ⑥ 同上，评定树匹配依据已确认版本；
- 反向用例：桥上**没有**已确认版本时，②③ 仍应报"台账尚未确认"——这一条不能被改掉。

最后一条容易在改动中丢失：把 `status` 判断换成 `has_value()` 之后，"没有任何已确认
版本"和"取到草稿"都归到同一个分支，必须确认前者的提示与阻塞行为没有变。

## 验收

1. 在已确认台账上做一次编辑派生出草稿，随后：校对草稿可保存、入库前检查可通过、
   年度可确认、自动匹配给出的构件在已确认版本内；
2. Word 导入到一座带草稿的桥，年度被锁定到已确认版本，病害带上该版本 id；
3. 桥上没有已确认版本时，②③ 的阻塞提示与改动前逐字一致；
4. 全仓 `get_latest_revision()` 的调用方只剩台账管理页那一处。

## 非目标

- 不改绑定链路：它已经在用正确的规则。
- 不改 `get_latest_revision()` 的排序：台账管理页依赖草稿优先。
- 不引入新的版本解析规则或新的错误码——这轮是把六处接到既有规则上，不是设计新契约。
