# 两侧构件绑定（人行道、护栏）

- 日期：2026-08-20
- 状态：**设计完成，待实施**
- 相关模块：构件绑定、范围拆分、H21 评分
- 缺陷来源：`../2026-08-20-baigu-score-discrepancy-investigation-report.md` 的 BG-2024-02
- 适用范围：**人行道、栏杆（护栏）**。翼墙、锥坡本次不放开，理由见"配对判定"。

## 背景

百股大桥 2024 年度的一条病害，报告里写的是：

```
评价部件  栏杆、护栏     构件编号  两侧护栏
病害      基座破损露筋   病害量    总面积 80.0 ㎡    标度 3
```

台账里这个部件只有两件：**左侧栏杆**、**右侧栏杆**。当前系统把这条病害绑到了左侧
一件，右侧当作无病害保留 100 分，部件分算出 76；报告是 56。全桥因此高 0.40 分。

这不是操作员点错，是系统对"两侧"这类语义范围**没有概念，而且不告警**——它和普通
的未匹配行长得完全一样。链路四步：

1. 台账按 `{side}侧{name}` 生成"左侧栏杆/右侧栏杆"，病害编号"两侧护栏"对不上，
   行状态 `unmatched`（匹配规则是"部件类别 + 归一化编号精确相等"，
   `ComponentMatcher.cpp:113`）。
2. `ImportBindingRepository.cpp:113` 对未匹配行试算能否拆分。
3. `parse_component_range("两侧护栏")` 只认波浪号，无分隔符即返回 `NotRange`
   （`ComponentRangeParser.cpp:95`），于是 `split_eligible = false`。
4. 界面只给单构件绑定，绑完行变绿，右侧从头到尾无人提及。

## 否决的两条路

**教解析器认"两侧"。** 改动看似最小，但把语义猜测放回解析器；而且展开出的
"左侧护栏"仍匹配不上台账的"左侧栏杆"（类型字不同），还得再加一层忽略类型字的
兜底比对——那正是本项目刻意定死的规则（编号列含类型字、归一化禁剥类型字）。
更糟的是类型字有时**是**关键：锥坡与护坡同属一个 H21 类别，正是靠类型字分开的。

**让一条病害绑多个构件。** 数据模型动到根：契约校验、评分输入、校对页、确认流
全线波及，而评分侧本就要求"一条病害 = 一个构件"。风险不成比例。

## 设计

### 关键前提：materialize 早已与范围解析解耦

`materialize_component_range_splits(current, analysis)` 只消费
`analysis.work_items[].matches`，**一次都没碰 `parse_component_range`**
（`ComponentRangeSplitPlanner.cpp:304`）。它按 matches 逐个复制病害、改编号、
清绑定、盖溯源、复制照片、改写警告指向。

所以本功能不需要新的 materialize，只需要换一种方式**构造 Analysis**：matches 不来自
范围展开，而来自人工在下拉里选定的构件。

### 配对判定：不读"两侧"这两个字

在 `overview()` 里，对每个 `unmatched` / `ambiguous` 行，除现有 `split_eligible`
外再算一组侧别配对：

> 该行所属部件类别下的启用构件**恰好是两件**，且编号仅"左↔右"不同。

满足则产出一条选项，标明两个成员。判定只看台账结构，不解析病害编号里的文字。

这条规则在真实桥上正好命中且只命中人行道与栏杆：

| 部件 | 编号模板 | 类别下构件数 | 是否出选项 |
| --- | --- | ---: | --- |
| 栏杆 | `{side}侧{name}` | 2 | 是 |
| 人行道 | `{side}侧{name}` | 2 | 是 |
| 翼墙 | `{ab}#台{side}侧{name}` | 4 | 否 |
| 锥坡、护坡 | `{ab}#台{side}侧{name}` + `{ab}#台{name}` | 6 | 否 |
| 板、铰缝等 | 无侧别 | — | 否 |

翼墙和锥坡被排除不是因为写死了部件名单，而是因为它们的类别下不止一对，
"两侧"到底指哪个台是含糊的。等有明确的按台配对需求时再单独设计。

> 副作用（可接受）：若某桥只勾选了 0# 台的左右翼墙，该类别恰好剩两件成对，
> 选项也会出现——此时它指向的正是那两件，行为正确。

### 后端

**构造器**（`review` 层纯函数，无 IO，可直接单测）

```
analyze_component_multi_bind(current, target, bridge_component_ids[], revision)
    → ComponentRangeSplitAnalysis        // 与范围拆分同型
```

对每个选定构件从台账取条目，构造
`ComponentRangeSplitMatch{component_number = 台账真实编号, bridge_component_id,
standard_component_category_id, resolved_structure_part, match_method = "manual"}`。
复用现成的资格校验：该行病害若已绑定或已标缺失则 `IneligibleTarget`；每个构件的
类别必须与 `part_name` 相符（与 `bind` 同一条规则）。

`match_method` 取 `"manual"`，与既有 `bind` 路径一致（`ImportBindingRepository.cpp:395`）。

**写操作** `ImportBindingRepository::bind_multi(...)`

既有三道闸门一道不减：

- 编辑锁：路由先拦一道拿到具体失效原因，仓储在事务内复查；
- `expected_revision_id`：事务内比对，不符返回 `component_inventory_revision_changed`，
  不静默改用新版本；
- 年度尚未锁定版本时，校验通过后才锁定。

事务内：读 `parsed_result_json` → 构造 analysis → materialize → 写回 → 返回新 overview。

**不做 preview / impact_token。** 范围拆分需要它是因为可能产出几千条病害；这里是
1→2，下拉选项本身已经写明将绑给谁。

接口收的是**数组**而非固定两个，将来"全幅""三跨"只是不同预设，不必再改接口。

**路由** `POST /api/import-records/{id}/component-binding/bind-multi`，错误码沿用
现有一套。

### 产物

选中后，该行的每条病害变成 N 条（本期 N=2），各自：

| 字段 | 值 |
| --- | --- |
| `candidate_id` | `<源 id>__range_1` / `__range_2`（沿用既有命名） |
| `component_number` | 台账真实编号（左侧栏杆 / 右侧栏杆） |
| `bridge_component_id` | 已绑定 |
| `component_match_method` | `manual` |
| `review_status` / `group_review_status` | 待确认 |
| `range_split_origin` | 记来源候选 id、原编号"两侧护栏"、序号 1/2 |
| 照片 | 整套复制并改指向新候选 |
| 标度、病害量 | **原样复制** |

标度与病害量原样复制是沿用现有范围拆分的既有约定（25 块板每块都记源病害的量），
并靠 `append_split_warning` 提示人工核对。评分只取标度，量值分摊不在本次范围。

### 前端

`BindingRow` 增 `side_pair_options: { label, bridge_component_ids[] }[]`，下拉在候选
之上插入，value 用哨兵前缀分流到新接口。

写入后**必须调 `onDraftInvalidated()`**：病害有增删，父页面草稿是首屏独立持有的
reducer 状态，不重取会一直显示拆分前的旧数据
（`ComponentBindingWorkspace.tsx:319` 的注释已记下这个坑）。

## 分数影响（验算）

指标 `h21.defect.10_4_1_2`，扣分表 `h21.deduction.scale_table.max_4` = `{1:0, 2:25,
3:40, 4:50}`，标度 3 扣 40 → 构件分 60。构件数量系数取自规范表 4.1.2：
**n=1 → t=∞，n=2 → t=10**。

| | 左侧 | 右侧 | 均值 | 最低 | t | 部件分 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 改前 | 60 | 100 | 80 | 60 | 10 | 76 |
| 改后 | 60 | 60 | 60 | 60 | 10 | **56** |

传导：栏杆在桥面系权重 0.10 → 桥面系 71.5118 → 69.5118；桥面系在全桥权重 0.20 →
全桥 84.4181 → **84.0181（84.02）**，正好落 0.40，与调查报告第 5.4 节的影响链吻合。

> 顺带记下一条反直觉的事实，供日后有人提"把两侧合成一件构件"时参考：单构件时
> `t=∞`，部件分公式退化为"部件分 = 构件分"（`H21Evaluator.cpp:698`），最低分惩罚项
> 直接消失。合成一件会得到 60 而不是 56，**不是等价变换**，且该部件此后永远吃不到
> 最低分惩罚。

## 测试

**构造器（纯函数）**

- 一行 1 条病害 + 2 个构件 → 产出 2 条，编号 / 绑定 / `manual` / 待确认 / 溯源均正确
- 照片整套复制，`linked_defect_candidate_id` 指向新候选
- 该行已绑定或已标缺失 → `IneligibleTarget`
- 选定构件的类别与 `part_name` 不符 → 拒绝，整批不写

**配对判定**

- 栏杆类别（2 件成对）→ 出 1 条选项，成员正确
- 人行道类别（2 件成对）→ 出 1 条选项
- 锥坡类别（6 件）、翼墙类别（4 件）→ 0 条
- 无侧别部件 → 0 条
- 类别下 2 件但不成对（如一左一右分属不同台）→ 0 条

**评分回归**（调查报告 13.2 节点名的两条）

- 栏杆构件分 `[60, 100]` → 部件分 76
- 栏杆构件分 `[60, 60]` → 部件分 **56**

**并发与闸门**

- 编辑锁在事务内失效 → `EditLockInvalid`，一条不写
- `expected_revision_id` 与事务内解析不符 → `component_inventory_revision_changed`
- 年度未锁定时成功路径锁定年度版本；失败路径不得留下锁定

每条新测试都必须验证"改回旧行为就变红"，再改回来。

## 不在本次范围

- BG-2024-01「其它病害」是否参与评分（业务政策，需单独决策）
- BG-2024-03 防排水标度 1/2（需现场照片复核）
- BG-2024-05 锥坡、护坡数量口径 4 vs 6（台账定义问题）
- BG-2024-06 试算页展示非评分病害数与跳过原因（数据层已有
  `rating_tree_skips`，前端从未读取；独立改动）
- 按台配对的"两侧"（翼墙、锥坡）
- 拆分时的病害量分摊
