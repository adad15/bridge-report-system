# 2026-07-23 变更记录：支座编号规则落地、可选下部构件复选、台账加载性能

> 日期：2026-07-23
>
> 分支：`codex/06-5-interaction-redesign`
>
> 涉及提交：`b2aa2cb`、`512b3a2`、`9aa0959`
>
> 触发背景：用户按真实桥录入台账——31 跨、每跨 25 块空心板、每块板四角各一个支座，生成 4941 条构件（支座 3168、板 759、铰缝 759）。此前构件数只有几十到几百，三处问题都是"真实规模 + 真实编号规则"同时到位后才暴露的。

## 变更一：支座编号改按《构件编号规则》第 10 条

提交：`b2aa2cb`（feat(inventory): bearing numbering per rule article 10）

### 现象

向导要求用户填「每孔墩数」，但这个量在物理上无法确定：一孔两端都有墩，且相邻两孔共用中墩，用户不知道该填一边还是两边。追问下暴露出更根本的问题——该字段表达不了任何真实布置。

### 根因

规则第 10 条原文：

> 支座编号：每个支座为单独构件，在桥孔和桥墩编号的基础上从右至左依次进行编号，例如 N-A-B#支座表示第 N 孔的第 A 号墩第 B 个支座。

即 A 是「一孔的两个支承」，由几何恒定为 2、自右至左记 1/2，**不是自由量**。设计文档 §3.2 也只列了「每墩支座数」一个数量输入。实现时却把 A 做成了用户输入的 `piers_per_span`（每孔墩数），多出一个规则里不存在的维度。

设计文档 §3.2 的 `[注]` 当时把支座形状标为"待真实报告校准"，本次据规则第 10 条定案，该注已同步更新。

### 修复

- `NumberingTemplate` 新增 `Placeholder::SpanSupport`（`{sup}`）：一孔恒两个支承，token 为 `1`/`2`，位置文案「第N号墩」，由几何派生、不消费用户数量。
- 支座模板 `{span}-{c1}-{c2}#{name}` → `{span}-{sup}-{c1}#{name}`，`count_inputs` 从两个减为一个，删除 `piers_per_span`。
- 前端编号预览器 `inventoryNumbering.ts` 同步镜像该占位符（前端只做预览，权威生成仍在后端，两者必须一致）。

### 验证

真实算例写成测试：33 孔 × 每孔每墩 50 个支座 = 3300，与「825 块板 × 每板 4 个支座」吻合；编号自 `1-1-1#支座` 至 `33-2-50#支座`。

### 已知不覆盖的情形

规则按孔编号，**不建模「墩是否被共用」**。对全简支桥（含本例的空心板梁桥）计数正确；对连续梁桥，中墩上梁不断开、实际只有一排支座，按每孔两端算会**多算**。分联（如 `1+3`）参数未引入，此类桥需在台账编辑器手工删除多出的支座。

### 涉及文件

- `backend-cpp/include/bridge_report/inventory/NumberingTemplate.hpp`、`backend-cpp/src/inventory/NumberingTemplate.cpp`
- `backend-cpp/src/inventory/ComponentPartCatalog.cpp`
- `frontend/src/bridges/inventoryNumbering.ts`
- `backend-cpp/tests/test_component_part_catalog.cpp`
- `docs/superpowers/specs/2026-07-21-component-inventory-standard-numbering-and-binding-design.md`（§3.2 `[注]` 与 §3.3 形状 2b）

## 变更二：支座数量口径消歧 + 翼墙/锥坡/护坡逐个可选

提交：`512b3a2`（feat(inventory): disambiguate bearing count and make optional substructure parts selectable）

### 现象

1. 标签「每墩支座数」有歧义：一个墩上落着相邻两孔的支座，用户无法判断该填一孔的还是两孔的。
2. 翼墙固定生成 4 个（2 台 × 2 侧）、锥坡 4 个、护坡 2 个，但真实桥不一定都有这 10 处。

### 根因

1. 按规则第 10 条，B 是「第 N 孔第 A 号墩」上的支座数，即**单孔单端**；原标签未体现"单孔"这一限定。
2. 这三个部件的位置由 `{ab}`/`{side}` 几何展开，目录里没有"某个位置可能不存在"的表达方式，只能全有或全无。

### 修复

- `PartCountInput` 新增 `hint` 字段（口径说明，空则不渲染）。支座标签改为「每孔每墩支座数」，提示："只数一个孔落在这个墩上的支座，不含相邻孔。例：每孔 25 块板、每板每端 2 个角 → 填 50。"
- `CatalogPart` 新增 `instance_selectable`，翼墙/锥坡/护坡置真。向导对展开出的每个位置渲染一个复选框（默认全选），实时显示"共 N 个"。
- `PartSelection` 新增 `excluded_numbers`，生成器据此过滤。排除项若不在展开结果中，报 `unknown_excluded_component_number` —— 前后端编号器不一致时当场暴露，不静默少生成构件。
- 向导内部按**展开下标**记录取舍而非编号：用户改现场名或跨数后编号会变，下标仍指向同一位置（如「0#台左侧」），取舍不丢；提交时才转成编号。

### 未纳入

`人行道`、`栏杆`（左/右各一）同样可能只有单侧，本次未改——用户只点名了上述 10 处。

### 涉及文件

- `backend-cpp/include/bridge_report/inventory/ComponentPartCatalog.hpp`、`ComponentInventoryModels.hpp`
- `backend-cpp/src/inventory/ComponentPartCatalog.cpp`、`ComponentInventoryModels.cpp`、`ComponentInventoryGenerator.cpp`
- `backend-cpp/src/http/ComponentInventoryRoutes.cpp`（端点暴露 `hint`、`instance_selectable`）
- `frontend/src/api/componentInventoryApi.ts`、`frontend/src/bridges/BridgeInventoryWizard.tsx`、`frontend/src/styles.css`
- 测试：`test_component_part_catalog.cpp`、`test_component_inventory_generator.cpp`、`BridgeInventoryWizard.test.tsx`

## 变更三：台账加载的 N+1 查询

提交：`9aa0959`（perf(inventory): load standard mappings in one query instead of per entry）

### 现象

打开实际构件台账页长时间停在"加载中…"。

### 根因

`revision_from_client` 用一条 SQL 取出全部构件后，**对每条构件再单发一次 SQL 查它的规范映射**。往返次数随构件数线性增长：4941 条构件 = 1 + 4941 次同步查询，逐次串行往返。

数据库侧实测（该桥真实数据，psql 计时）：

| 查询 | 耗时 |
| --- | --- |
| 逐条映射查询 × 4941 | **3004 ms** |
| 批量一次取全部映射 | **155 ms** |
| 主查询（含 `is_referenced` 的 EXISTS） | 217 ms |

`is_referenced` 那个跨四表的 `exists` 子查询虽然每行都算，但它在**同一条 SQL 内部**，217 ms、仅占修复前总耗时的 7%，不是瓶颈。

### 修复

映射改为按 `inventory_revision_id` 一次取回，在内存里用 `unordered_map` 按 `inventory_entry_id` 归组后挂到各构件。排序沿用逐条版的 `is_active desc, created_at, id`（批量查询加 `inventory_entry_id` 作为首排序键），各构件内映射顺序不变。**接口形状不变，前端无需改动。**

数据库总耗时：约 **3.2 s → 0.37 s**（约 1/9）。

### 明确的局限

本次只减少往返次数，**不减少数据量**。该台账原始数据即 1142 KB 构件 + 1241 KB 映射 ≈ 2.4 MB，序列化成 JSON 后更大，仍需传输、解析并建立约 4941 条前端状态。因此结果是"明显变快但仍有可感知等待"，不是瞬开。

要进一步优化需改为**分组按需加载**（列表只取分组摘要，点开某组才拉该组构件）。未做，因为主要成本不在接口本身，而在于确认门槛 `inventoryConfirmationBlockers` 目前在前端遍历全量构件查重号、查映射确认状态，分页后必须改为后端聚合——那是独立的设计题，不宜与本次性能修复混做。

修复后 `is_referenced` 将占剩余耗时的约 58%（217/372），是下一个可考虑的优化点，但 217 ms 当前可接受。

### 涉及文件

- `backend-cpp/src/db/ComponentInventoryRepository.cpp`

## 回归

- 后端：隔离 schema 全量通过（15 个迁移跑两遍、10 个冒烟文件、`EXIT=0`），测试数 385 → 389（新增支座规则 2 条、逐实例排除 2 条）。
- 前端：242 → 244 项通过，`tsc -b && vite build` 通过。

## 经验

1. **领域规则要按原文落地，不要按实现方便重新发明。** 变更一里多出的「每孔墩数」正是实现阶段自造的维度，规则里没有，用户因此反复追问却得不到答案。
2. **几何派生量不该问用户。** 桥台恒 2 个、一孔恒两个支承、墩数 = 跨数 − 1，这些填了跨数就已确定；把它们做成输入框只会制造歧义。
3. **界面文案若来自后端目录，改完必须重启后端才可见。** 本次改完前端页面无变化，原因是后端 exe 仍是旧的——部件名、数量维标签、`hint`、`instance_selectable` 全部由 `/api/component-inventories/part-catalog` 下发，前端不写死。
4. **N+1 在小数据下完全无感。** 同一段代码在几十条构件时无人察觉，4941 条时变成 3 秒。真实规模数据是唯一可靠的验收条件。
