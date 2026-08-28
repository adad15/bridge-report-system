# 导入构件解析与评分树解析状态分离设计

> 日期：2026-08-27
>
> 状态：已按二轮评审修订，待最终确认（修订内容见 §23）
>
> 适用范围：年度检测导入校对、构件台账绑定、评分树匹配、区间构件展开

## 1. 背景

当前年度检测导入把两类性质不同的数据放在同一个 `import_records.parsed_result_json` 中：

1. 来源事实和人工校对事实，例如来源构件名称、构件编号、病害类型、位置、尺寸、照片引用和校对状态；
2. 依赖当前系统上下文的解析结果，例如台账构件 ID、构件匹配方式、评分树版本、评分树节点和匹配证据。

构件绑定写操作因此必须读取、遍历并覆盖整份 JSON。构件变化又会影响评分树节点，后端还要清空或重算同一 JSON 中的评分树字段。前端本地保存着另一份草稿，绑定后必须使草稿失效并重新加载，否则后续保存可能用旧 JSON 覆盖新绑定结果。

此外，构件编号归一化、批量替换预览、区间拆分判断等规则在 C++ 与 TypeScript 中重复实现。前端不仅展示结果，还承担了大量业务推演、版本一致性和缓存失效工作。

本设计将构件解析和评分树解析一起从 `parsed_result_json` 中拆出，以关系表作为唯一权威状态，并把候选生成、预览和执行校验统一收回 C++ 后端。

当前数据库中的导入记录均为测试数据。本设计不提供旧数据回填、双读或双写兼容；实施验收前由开发环境显式重置测试数据并重新导入。

## 2. 目标

1. 构件绑定、批量替换、区间展开和评分树选择不再改写 `parsed_result_json`。
2. 同一来源构件组只保存一次构件解析状态；组内每条病害分别保存评分树解析状态。
3. 多目标和区间构件通过解析实例表达，不再复制来源病害 JSON。
4. 前端不再实现构件编号归一化、替换展开、候选合法性或评分树失效规则。
5. 批量和展开操作由后端生成预览计划，执行时保证“用户看到的计划”与“实际执行的计划”一致。
6. 普通病害草稿保存不能覆盖、清除或伪造构件解析与评分树解析结果。
7. 预检和正式确认直接组合来源事实与关系表中的解析状态，不再要求把解析字段写回 JSON。
8. 保留现有导入记录级编辑租约锁，暂不引入多人同时编辑同一导入记录。
9. 拆分不得削弱现有可用路径：手工新增病害、台账未确认时的校对、实例级修正与忽略、台账版本更替
   后的恢复，都要有明确落点，不留“以后再补”。
10. 新增端点、请求头、参数名和错误码沿用现网既有约定，不与既有绑定链路并存两套。

## 3. 不在本次范围内

1. 不接入大模型、向量检索或外部推理服务。
2. 不重做构件绑定工作区的整体交互布局。
3. 不把病害位置、尺寸、照片关系和一般校对状态全部关系化。
4. 不改变构件台账、评分树版本和正式病害事实的业务定义。
5. 不允许自动跨桥、跨台账版本或绕过构件类别约束绑定。
6. 不提供现有测试导入记录的迁移或回滚脚本。
7. 不取消导入记录级编辑锁，也不实现构件组级多人协作。
8. 不支持在校对页修改来源构件名称与编号。校对页今天也只读展示这两个值
   （`DefectDetailEditor.tsx:134`），本设计因此不引入“移动来源病害成员”命令；
   普通草稿保存遇到这类改动直接按合同错误拒绝。

## 4. 核心决策

### 4.1 同时拆出构件解析和评分树解析

只拆构件绑定会留下一个循环依赖：构件改变后，仍需改写 JSON 中的评分树字段。为彻底停止绑定操作覆盖大 JSON，本设计同时拆出：

- 构件解析状态；
- 构件解析目标；
- 多目标产生的病害解析实例；
- 每个解析实例的评分树解析状态。

### 4.2 构件绑定按来源构件组建模

来源构件组沿用当前业务语义，由下列组合确定：

```text
import_record_id
+ source_component_name
+ normalized_source_component_number
```

例如同一导入中的：

```text
25-1#板｜裂缝
25-1#板｜渗水泛碱
25-1#板｜混凝土剥落
```

属于同一个来源构件组，只绑定一次目标台账构件。三条病害仍分别匹配评分树节点。

### 4.3 多目标和区间展开不复制来源 JSON

来源记录：

```text
1~3#伸缩缝｜止水带破损｜L=20m
```

保持为一条来源病害。构件解析后生成三个病害解析实例：

```text
来源病害 source_defect_0123
  -> 实例 1 -> 1#伸缩缝
  -> 实例 2 -> 2#伸缩缝
  -> 实例 3 -> 3#伸缩缝
```

正式确认时，三个实例分别生成年度病害观测。照片仍属于来源病害；多实例正式入库时只由约定的首个实例继承来源照片，保持当前“照片不随区间拆分重复”的语义。

### 4.4 存储和 API 同时分离

`parsed_result` 只承载来源事实和一般校对事实。构件解析与评分树解析通过独立 DTO 和接口读取、修改。

后端不得为了兼容旧前端而把解析字段重新混入可保存的 `parsed_result`。否则旧草稿仍可能把这些字段回传并覆盖权威状态。

### 4.5 保持现有功能，不同时重做页面

第一阶段保留：

- 单构件绑定；
- 左右侧或其他多构件绑定；
- 批量替换；
- 区间展开；
- 标记台账缺失；
- 清除绑定；
- 评分树候选和人工选择。

本次只改变数据边界和逻辑归属。异常项工作台等交互重设计另立设计。

### 4.6 手工新增病害是一次显式解析，不是一次草稿保存

手工新增病害今天由前端在草稿里直接造一条完整候选：用户在同一个对话框里选定**具体台账
构件**和**具体评分树节点**，`reviewDraft.ts` 的 `add_defect` 随即写入
`bridge_component_id`、`standard_component_category_id`、`resolved_structure_part`、
`component_inventory_revision_id`、`rating_tree_version_id`、`rating_tree_node_id`
六个字段（`DefectsSection.tsx:643`、`reviewDraft.ts:228`）。

这六个字段 5.0 全部移出合同，而 §11.1 又要求普通草稿保存拒绝解析字段。若不专门处理，
手工新增这条路会整条切断；退回“建组 + 自动匹配”也不行——那是把用户刚点选的目标降级成
一次猜测，评分树节点那一半基本必然丢失（自动匹配只在唯一命中时才写）。

因此手工新增走**专用命令**，在一个事务里完成来源事实与解析状态的写入：

1. 向 `parsed_result_json.defects` 追加来源病害；
2. 按来源构件名称和归一化编号创建或复用来源构件组；
3. 写入解析目标（`match_method = manual`）；
4. 生成病害解析实例；
5. 写入评分树解析（`match_method = manual`）。

来源构件名称与编号取自用户选中的台账条目本身，所以派生出的组键天然指向该构件。命中既有组时
按以下规则处理，不能因为“新增一条病害”暗中改变既有病害的解析结果：

| 既有组状态 | 处理 |
| --- | --- |
| `bound`，且目标就是用户所选构件 | 复用该组，只为新成员生成实例和人工评分树解析 |
| `bound`，但目标集合不同；或 `missing` | 按 `manual_defect_group_conflict` 拒绝，提示改用绑定工作区 |
| `unresolved`，且已有成员 | 按 `manual_defect_group_requires_resolution` 拒绝；用户先在绑定工作区解析该组，再重试新增 |

空组在成员删除后立即删除，因此不存在“`unresolved` 但没有成员的既有组”。专用新增命令不承担
批量绑定职责，也不需要在一个看似单条新增的接口里偷偷影响多条旧病害。

### 4.7 部件层级与歧义状态是派生读模型

现行绑定面板是两层：**部件组**（`component_name`）下挂**行**（归一化编号），行有
`bound` / `missing` / `ambiguous` / `unmatched` 四态（`ImportBindingRepository.cpp:78`、
`:110`）。批量替换的入口挂在部件组表头上，参与范围又恰好是 `unmatched` 与 `ambiguous`
两态（`2026-07-24-bulk-binding-replace-design.md` §4.1）。

关系表只建**行级**的来源构件组，状态只存三态（§8.1）。部件层级与歧义状态不入库，由
`ResolutionWorkspaceQuery` 在读模型里派生：

- 部件层级：按 `source_component_name` 聚合，给出该部件下的组数与各状态计数；
- `ambiguous`：`status = unresolved` 且候选数大于一，作为派生标签随组返回。

前端据此渲染分组表头、批量替换入口与“未匹配 26 · 歧义 0”这类计数，但不自己算这两件事。

## 5. 术语

| 术语 | 含义 |
| --- | --- |
| 来源病害 | Python 导入器或人工新增产生的一条病害候选，以 `candidate_id` 标识 |
| 来源构件组 | 同一导入中来源构件名称和规范化编号相同的一组病害 |
| 构件解析 | 来源构件组到零个、一个或多个当前台账构件的受控映射 |
| 解析目标 | 构件解析选中的一个台账构件 |
| 病害解析实例 | 一条来源病害在一个解析目标上的展开结果 |
| 有效病害事实 | 来源病害事实叠加该实例 `fact_overrides_json` 后的结果，见 §8.4 |
| 评分树解析 | 病害解析实例到一个当前评分树节点的受控映射 |
| 部件层级 | 按 `source_component_name` 对来源构件组做的读模型聚合，不入库，见 §4.7 |
| 预览计划 | 后端针对批量或展开操作生成的短期、一次性、带版本前提的执行计划 |

## 6. 目标架构

```text
BridgeAnnualInspectionData 5.0
  parsed_result_json
  （来源与校对事实）
          |
          v
来源构件组 -----> 构件解析目标
    |                  |
    v                  v
来源病害成员 -----> 病害解析实例 -----> 评分树解析
                                              |
                                              v
                                预检 / ConfirmPlan / 正式事实表
```

前端只调用后端解析 API：

```text
前端提交意图
  -> 后端生成候选或预览
  -> 用户确认
  -> 后端事务校验并执行
  -> 返回受影响的构件组和病害实例
  -> 前端局部更新
```

## 7. BridgeAnnualInspectionData 5.0

本次变更是破坏性合同升级，版本从 `4.0` 升为 `5.0`。

### 7.1 从 `DefectCandidate` 删除的字段

下列字段不再属于来源事实合同：

```text
bridge_component_id
standard_component_category_id
resolved_structure_part
component_inventory_revision_id
component_match_candidate_ids
component_match_method
component_match_confirmed_by
rating_tree_version_id
rating_tree_node_id
rating_tree_match_method
rating_tree_match_evidence
standard_defect_indicator_id
range_split_origin
```

这些字段分别由构件解析表、病害解析实例表和评分树解析表表达。

`range_split_origin` 需要单独交代：它**不只是校对期字段**。今天正式确认会把它写进
`defect_observations.source_raw_cells_json`（`ReviewRepository.cpp:41`、`:247`），
`ConfirmPlan.cpp:229` 读它，`DraftValidation.cpp:536` 拿它做防篡改比对，构件病害档案页
（`ObservationYearRow.tsx`）还在渲染它。把它移出合同后，**正式事实里的这块溯源必须由
`ConfirmResolutionReader` 从组、目标和实例重新合成**，口径见 §17.2；档案页不做改动。
草稿侧的防篡改比对随字段一并取消——溯源不再由客户端回传，无从篡改。

### 7.2 继续保留的来源身份

以下字段来自报告或来源软件，继续保留：

```text
source_structure_part
component_name
component_number
source_defect_group_id
source_defect_group_number
source_defect_indicator_id
source_defect_indicator_number
```

来源指标身份不是评分树解析结果。即使评分树重新绑定，也不得清空或覆盖这些来源字段。

### 7.3 校验边界

Python、JSON Schema、C++ 和 TypeScript 同步升级到 5.0。`extra="forbid"` 语义保持不变：新合同收到旧解析字段时必须报错，不能静默忽略。

C++ 的合同兼容检查把非 5.0 数据标记为需要重新解析。本设计不保留 4.0 编辑路径。

### 7.4 顺带修正的既有枚举漂移

`component_match_method` 今天三边不一致，5.0 正好是拉齐的时机：

| 位置 | 允许值 |
| --- | --- |
| Python 契约 | `exact` `confirmed_alias` `normalized_candidate` `manual` |
| C++ 契约校验（`AnnualInspectionContract.cpp:432`） | 同上 |
| 前端契约（`annualInspection.ts:393`） | 同上，**外加 `missing`** |

而标记台账缺失时后端写的正是 `component_match_method = "missing"`
（`ImportBindingRepository.cpp:1170`）。标记缺失后再存草稿会走到
`validate_bridge_annual_inspection_data`（`DraftValidation.cpp:118`）撞 enum 错。
这是既有缺陷，与本设计无关，但 5.0 把整个字段移出合同后它自然消失：缺失由
`import_component_resolution_groups.status = missing` 表达，`match_method` 置空（§8.1）。

`normalized_candidate` 同样不进入 §8.1 的 `match_method`。它在契约里挂着，但**现网没有任何
写入方**——归一化是匹配器的固定前置步骤（`ComponentMatcher.cpp:97`），归一化后命中记的就是
`exact`，这个值区分不出任何后续差异。5.0 直接不再声明它。

## 8. 数据模型

### 8.0 `import_records.draft_version`

`POST /manual-defects` 与普通 `PUT /review-draft` 都会修改 `parsed_result_json`。编辑锁只能保证当前
导入记录由谁编辑，不能阻止同一用户、同一会话的两个标签页拿着同一个锁 token 先后覆盖。因此在
`import_records` 增加：

| 字段 | 含义 |
| --- | --- |
| `draft_version` | 来源草稿乐观并发版本，初始为 1；每次实际修改 `parsed_result_json` 后递增 |

读取校对详情时返回 `draft_version`。所有校对期来源事实写操作——至少包括普通草稿保存、手工新增
病害，以及任何会改动 `parsed_result_json` 的照片或来源事实命令——都必须携带
`expected_draft_version`，统一使用标准 `If-Match: "draft-<version>"` 请求头表达，响应回传
`ETag: "draft-<new_version>"` 并在 DTO 中返回数值版本。在同一事务的条件更新中比对；不一致返回
`review_draft_version_conflict`，不得把“新版本里多出的候选”解释成用户主动删除。

成功响应返回新的 `draft_version`。仅修改关系表的构件解析、实例覆盖、实例状态和评分树解析命令
不递增它。

### 8.1 `import_component_resolution_groups`

保存来源构件组身份和当前构件解析状态。

| 字段 | 含义 |
| --- | --- |
| `id` | UUID 主键 |
| `import_record_id` | 导入记录，删除导入时级联删除 |
| `source_component_name` | 来源构件名称原文 |
| `source_component_number` | 来源构件编号原文，可空 |
| `normalized_component_number` | C++ 权威归一化结果，**非空**，编号缺失时为空串 |
| `resolution_mode` | `single`、`multi` 或 `range` |
| `status` | `unresolved`、`bound` 或 `missing` |
| `match_method` | `exact`、`confirmed_alias`、`manual`、`side_pair`、`range` 或空 |
| `inventory_revision_id` | 本次解析所依据的构件台账版本，**可空**，见下 |
| `version` | 乐观并发版本，成功状态变更后递增 |
| `resolved_by_user_id` | 最近一次人工解析者，可空 |
| `resolved_at` | 最近一次人工解析时间，可空 |

唯一约束：

```text
(import_record_id, source_component_name, normalized_component_number)
```

归一化列**不可为空**是这条约束能成立的前提。`source_component_number` 可空，若把归一化结果
也写成 NULL，PostgreSQL 默认 NULL 互不相等，唯一约束整条失效——每条无编号病害各成一组，
“同一构件只绑一次”当场垮掉。因此编号缺失时归一化结果落**空串**，同一导入内所有无编号且
部件名相同的病害归入同一组。（PG15+ 的 `nulls not distinct` 也能达到同样效果，但空串同时
让组键在 C++ 侧、审计事件和 API 里都是一个普通字符串，不必到处铺 NULL 分支。）

同一归一化值下可能出现多种原文（`1#` / `1＃` / `1`）。`source_component_number` 存**该组
第一条成员按 `source_order` 排序后的原文**，只用于界面展示与审计可读性，任何判定都以
`normalized_component_number` 为准。

`inventory_revision_id` 可空，因为**导入时该桥可能还没有已确认台账版本**。这是现网就在发生
的情况（`2026-08-17-component-binding-on-demand-lookup-design.md` 的“缺陷一·受害点 A”），
今天它只让绑定面板不可用，不影响其余校对。若把它写成必填、又按 §10 让初始化失败即整条导入
不可校对，就等于把一个局部限制升级成整条导入的阻断，是明确的功能倒退。

状态不变量：

- `bound` 至少有一个当前解析目标，且 `inventory_revision_id` 非空；
- `unresolved` 和 `missing` 没有解析目标；
- `unresolved` 允许 `inventory_revision_id` 为空（导入时该桥无已确认台账版本）；
- `missing` 只能由人工明确设置；
- `inventory_revision_id` 非空时，必须等于 `resolve_confirmed_revision_ref()` 当时解析出的
  版本——**检测年度锁定的版本优先，年度未锁定时取该桥最新的已确认版本**
  （`2026-08-18-confirmed-inventory-resolution-consistency-design.md`）。写“导入年度当前
  绑定的版本”只说了前半句，年度未锁定的导入会解析不出版本。

`ambiguous` 不是本表的状态。歧义组存 `unresolved`，由读模型按候选数派生标签（§4.7）。

### 8.2 `import_component_group_members`

把来源病害 `candidate_id` 归入一个来源构件组。

| 字段 | 含义 |
| --- | --- |
| `id` | UUID 主键 |
| `import_record_id` | 用于约束候选在导入内唯一 |
| `group_id` | 来源构件组 |
| `source_candidate_id` | `parsed_result_json.defects[].candidate_id` |
| `source_order` | 来源病害稳定顺序 |

约束：同一 `(import_record_id, source_candidate_id)` 只能属于一个来源构件组；成员和组必须属于同一导入记录。

### 8.3 `import_component_resolution_targets`

保存来源构件组当前选中的台账构件。

| 字段 | 含义 |
| --- | --- |
| `id` | UUID 主键 |
| `group_id` | 来源构件组 |
| `bridge_component_id` | 当前台账中的目标构件 |
| `target_order` | 多目标稳定顺序 |
| `target_role` | `primary`、`left`、`right` 或 `range_member` |

同一组不能重复选择同一构件。目标构件必须属于同一桥梁和组记录的台账版本。

### 8.4 `import_resolved_defect_instances`

表达一条来源病害在一个目标构件上的展开结果。

| 字段 | 含义 |
| --- | --- |
| `id` | UUID 主键 |
| `group_member_id` | 来源病害成员 |
| `target_id` | 构件解析目标 |
| `instance_order` | 展开后的稳定顺序 |
| `instance_status` | `active` 或 `ignored` |
| `is_photo_owner` | 是否继承来源病害照片 |
| `fact_overrides_json` | 仅保存展开后针对单个实例的病害事实修正，默认 `{}` |
| `component_resolution_version` | 生成实例时的构件解析版本 |
| `version` | 实例自身的乐观并发版本；覆盖或状态实际变化后递增 |

唯一约束：

```text
(group_member_id, target_id)
```

`fact_overrides_json` 只允许覆盖以下病害事实字段：

```text
defect_type
defect_location
defect_scale
defect_description
quantity_text
measurement_text
measurements
remark
```

忽略状态使用独立的 `instance_status`，不写入覆盖 JSON。覆盖 JSON 不允许出现构件解析、评分树解析、照片关系、来源证据、候选 ID 或任意未知字段。普通单目标实例不复制完整来源病害。

白名单不只约束字段名，也约束每个字段的类型、可空性和内部结构：

| 覆盖字段 | 可空 | 约束 |
| --- | --- | --- |
| `defect_type`、`defect_location`、`defect_description` | 否 | 字符串，规则与 5.0 来源事实一致 |
| `measurements` | 否 | `Measurement[]`，整段按 5.0 合同校验 |
| `defect_scale` | 是 | 正整数或 `null` |
| `quantity_text`、`measurement_text`、`remark` | 是 | 字符串或 `null` |

接口中的“清除覆盖”是删除对应键，不是把必填字段写成 `null`。合并完成后的有效病害事实必须再次
通过同一份 5.0 病害事实片段校验；不能让来源合同不接受的值借覆盖 JSON 进入预检或正式事实。

#### 有效病害事实

覆盖存在的那一刻起，“这条病害的类型是什么”就有了两个答案，必须先定死哪个算数：

```text
有效病害事实 = 来源病害事实  覆盖  该实例 fact_overrides_json 中出现的键
```

- **下游一律读有效事实**：`match_input_hash`（§8.5）、预检、正式确认、评定输入，全部按
  有效事实计算，不得直接取来源值；
- **键存在即生效**；仅上表可空字段允许显式 `null`。删除某个键即恢复来源值，这是唯一的
  “撤销覆盖”方式；
- **来源侧的修改到不了被覆盖的字段**。用户在校对页把某条病害的描述改对了，带该字段覆盖的
  实例不会跟着变。这不是缺陷，是覆盖的定义，但界面必须说清楚：工作区读模型对每个实例返回
  `overridden_fields`，实例面板据此标出“此字段已按本实例单独设定”，并提供“恢复来源值”。
  没有这条，用户会以为改了个错字全桥都改了，实际只改了三分之一。

#### 照片归属

照片始终属于来源病害，`is_photo_owner` 只决定正式入库时哪一条实例继承它。不变量：

- 一条来源病害在**存在至少一个活动实例**时，有且仅有一个活动实例
  `is_photo_owner = true`；
- 该实例是**活动实例中 `instance_order` 最小的那个**——不是“`instance_order = 1` 的活动
  实例”。两者在实例 1 被忽略时会分叉：按后者写，照片就没有归属者了，整组照片在正式入库时
  凭空消失；
- 实例集合变化、`instance_status` 变化之后，都在同一事务内重算这个标志；
- 单实例来源病害同样置 `true`，读取方无需分辨“单条”与“展开后第一条”。

#### 实例状态与校对状态

`instance_status` 只有 `active` 和 `ignored`，由 §13.2 的实例状态接口修改，普通草稿改不到它。

实例**不保存自己的 `review_status` / `group_review_status`**：这两个仍是来源病害的校对事实，
留在 `parsed_result_json` 里。今天范围拆分会给每条结果发一份独立的校对状态
（`ComponentRangeSplitPlanner.cpp:445`），拆完三条各自待确认；5.0 之后一条来源病害只有一份
校对状态，确认它就等于确认它展开出的全部活动实例。

这是本设计**有意接受的行为变化**：校对的对象回到“报告里的那一行”，而不是系统展开出来的中间
产物。代价是不能只确认三跨里的一跨——需要区分时，用 `instance_status = ignored` 把不要的那
条摘出去，而不是让它停在未确认。正式确认时每条实例写入的 `defect_observations.review_status`
取来源病害的校对状态（§17.2）。

### 8.5 `import_rating_resolutions`

每个活动病害解析实例最多有一个当前评分树解析结果。

| 字段 | 含义 |
| --- | --- |
| `resolved_defect_instance_id` | 主键及实例外键 |
| `rating_tree_version_id` | 评分树版本 |
| `rating_tree_node_id` | 当前节点，未解析时为空 |
| `standard_defect_indicator_id` | 由节点派生的标准病害指标，可空 |
| `status` | `unresolved` 或 `matched` |
| `match_method` | `exact`、`controlled_alias`、`controlled_keyword`、`fuzzy_candidate`、`source_indicator`、`manual` 或空 |
| `match_evidence_json` | 结构化匹配证据 |
| `component_resolution_version` | 结果所依据的构件解析版本 |
| `applicability_hash` | 评分树版本、目标构件、规范包、规范桥型和构件类别的规范 SHA-256 |
| `match_input_hash` | 本次匹配输入的规范 SHA-256 |
| `version` | 评分树解析乐观并发版本 |
| `resolved_by_user_id` | 人工选择者，可空 |
| `resolved_at` | 人工选择时间，可空 |

`matched` 必须同时具备有效评分树版本和节点。`unresolved` 不得保留节点或标准指标。

`match_input_hash` 至少包含：

```text
source_candidate_id
target bridge_component_id
technical_standard_package_id
target standard_bridge_type_id
target standard_component_category_id
rating_tree_version_id
source_defect_group_id / number
source_defect_indicator_id / number
defect_type          （有效值）
defect_location      （有效值）
defect_description   （有效值）
```

标“有效值”的三项取 §8.4 定义的**有效病害事实**，即来源值叠加本实例覆盖之后的结果，不是来源
值。三个实例把同一条来源病害改成了不同的病害类型时，它们本就该各自重算评分树；按来源值算
哈希会让三条实例的哈希恒等，覆盖再怎么改也触发不了失效。

`rating_tree_version_id` 已经隐含匹配规则包版本：规则包必须声明与本评定树版本一致的
`tree_code` 与 `package_version`，否则加载即报 `rating_tree_rule_pack_version_mismatch`
（`RatingTreePackageLoader.cpp:488`）。因此哈希不单列规则包版本。

`source_defect_group_id / number` 与桥型不能省略：现行 `RatingTreeResolver` 先按桥型和构件类别
收窄节点，再按“来源分组 + 来源指标”成对命中。少算其中任何一项，真实匹配输入变了而哈希仍可能
不变。

`defect_scale` 不进哈希：标度变化不重新选择评分树节点，与现行行为一致（§19.3）。

`applicability_hash` 与 `match_input_hash` 分开保存，避免为了保住人工选择而放弃检测构件适用性变化：

- `applicability_hash` 包含 `bridge_component_id`、`technical_standard_package_id`、
  `standard_bridge_type_id`、`standard_component_category_id`、`rating_tree_version_id`；
- `match_input_hash` 包含上述适用性输入，再加来源分组/指标身份和有效文字输入。

哈希失效规则按匹配方式区分：

- **自动匹配结果**：任一哈希不一致时失效并重新运行自动匹配；
- **人工选择结果**：普通文字、位置或来源描述变化不清除人工节点，也不让自动结果覆盖。只有评分树
  版本、目标构件、规范桥型、构件类别或节点适用性变化，即 `applicability_hash` 不一致时才失效；
  仅 `match_input_hash` 变化时读模型返回
  `content_changed_after_manual_resolution = true`，提示复核但保留人工选择。

这保持现网“人工选择只有用户自己能改”的语义。若以后决定让文字修改自动清除人工节点，必须作为
单独行为变更评审，不能借哈希实现悄悄改变。

### 8.6 `import_resolution_operation_plans`

保存需要显式预览的短期执行计划。

| 字段 | 含义 |
| --- | --- |
| `id` | UUID，同时作为不透明 plan token |
| `import_record_id` | 所属导入记录 |
| `actor_user_id` | 创建计划的用户 |
| `operation_type` | `bulk_replace`、`range_expand`、`inventory_repoint` 或其他受控批量操作 |
| `lock_token_hash` | 创建计划时所持编辑锁的 token 哈希 |
| `request_json` | 规范化后的用户意图 |
| `plan_json` | 冻结的目标、影响行、警告和阻断原因 |
| `preconditions_json` | 台账版本、评分树版本及构件组版本集合 |
| `status` | `ready`、`applied`、`expired` 或 `invalidated` |
| `expires_at` | 创建后 15 分钟 |
| `applied_at` | 成功执行时间，可空 |
| `apply_result_json` | 首次执行结果，用于幂等重放 |

计划只允许创建者在同一导入记录编辑锁下执行。

有效期是**创建后 15 分钟**，并要求应用时 `lock_token_hash` 与当前持有的编辑锁一致。这里不能
按“编辑锁到期时间”截断：编辑锁的 TTL 是 **2 分钟**，靠心跳每次续到 `now() + 2 minutes`
（`EditLockRepository.cpp:45`、`:206`）。若取“创建后 15 分钟与锁到期时刻的较早者”、又规定续租
不延长计划，那么任何计划活不过 2 分钟，15 分钟这个数永远不生效——而批量替换预览 26 行恰恰是
用户要逐行读的对话框，`resolution_plan_expired` 会从异常变成常态。

绑在 token 哈希上同样能挡住“锁掉了又被重新拿到”这种情况：那种情况下 token 已经换了一个，计划
按 `resolution_plan_invalidated` 拒绝。心跳续租不换 token，计划照常有效。

重复应用已成功计划时返回原 `apply_result_json`，不重复创建目标或实例。

### 8.7 `import_resolution_events`

构件解析和评分树解析使用独立的追加式审计表，不再把事件追加到 `validation_result_json`。

记录至少包含：

- 导入记录；
- 构件组或病害实例；
- 操作类型；
- 变更前后状态；
- 操作者；
- 关联计划；
- 发生时间。

当前状态表只表达当前结果，审计表只表达历史事件，两者不得混用。

### 8.8 重开校对快照

已确认导入记录重开时，现有 `reopen_backup_parsed_result_json` 只能还原来源草稿，不能还原关系表。

新增 `import_resolution_reopen_snapshots`：

- 每个处于重开态的导入记录最多一条；
- 重开事务中保存构件组、目标、实例和评分树解析的规范快照及校验和；
- 放弃修改时，在同一事务中还原来源 JSON 与解析快照；
- 重新确认成功后删除快照；
- 快照不是当前状态来源，不参与普通查询和确认。

重开、放弃修改恢复快照、重新确认成功这三个边界都会改变“当前状态世代”。事务内必须把该导入
记录所有 `ready` 计划更新为 `invalidated` 并记录原因；不能只靠恢复后的对象版本碰巧不同来挡旧
计划，因为快照恢复可能把版本号也恢复成计划创建时的值。放弃修改的恢复操作还要追加一条
`reopen_snapshot_restored` 审计事件，说明恢复的快照校验和与被作废计划数量。

## 9. 状态转换

### 9.1 构件解析

```text
unresolved -> bound
unresolved -> missing
bound      -> bound       （重新绑定）
bound      -> unresolved  （清除）
bound      -> missing
missing    -> unresolved
missing    -> bound
```

每次实际改变状态、目标集合、解析模式或台账版本时：

1. 构件组 `version` 递增；
2. 原子替换解析目标；
3. 按 `(group_member_id, target_id)` **差量对齐**病害解析实例，不是整组重建；
4. 删除不再存在实例的评分树解析；
5. 对保留或新增实例重新计算 `applicability_hash` 与 `match_input_hash`；
6. 按 §8.5 区分自动与人工结果处理失效：适用性变化使两者都失效，纯文字输入变化只让自动结果
   失效；
7. 对失效的自动结果和新增实例运行评分树自动匹配，无法唯一确定的保持 `unresolved`；
8. 重算 `is_photo_owner`（§8.4）；
9. 写入审计事件。

第 3 步必须是差量，不能是“删干净再按新目标集合生成”。唯一键就是 `(group_member_id,
target_id)`，因此可以精确判断：

| 该键在新目标集合中 | 该键已有实例行 | 动作 |
| --- | --- | --- |
| 在 | 有 | **保留该行**，只重刷 `instance_order` 与 `component_resolution_version` |
| 在 | 无 | 新建实例，`fact_overrides_json = {}`，`instance_status = active` |
| 不在 | 有 | 删除实例，级联删除其评分树解析 |

保留意味着 `fact_overrides_json` 和 `instance_status` 跟着这一行活下来。整组重建的话，三目标
里换掉一个，另外两条上人工逐条调过的位置、尺寸、标度和“这条不要”的判断会一起没掉——而用户
的操作意图只是换第三个目标。

若请求没有造成任何实际状态变化，后端返回幂等成功，不递增版本。

### 9.2 评分树解析

```text
unresolved -> matched      （规则唯一匹配或人工选择）
matched    -> matched      （人工重新选择）
matched    -> unresolved   （构件或匹配输入变化）
```

其中“匹配输入变化”只自动作用于自动匹配结果。`match_method = manual` 时，文字、位置和来源描述变化
保留原人工节点并标记待复核；评分树版本、目标构件、规范桥型、构件类别或节点适用性变化才转回
`unresolved`（§8.5）。

忽略的来源病害或解析实例不要求评分树节点。所有活动实例在正式确认前必须满足现有评分树绑定规则。

### 9.3 实例状态

```text
active  -> ignored   （人工判定该目标上不存在这条病害）
ignored -> active    （撤销）
```

两个方向都只由 §13.2 的实例状态接口触发，事务内重算 `is_photo_owner`；`ignored` 实例不参与
预检、正式确认和评定，也不要求评分树节点，但保留其 `fact_overrides_json`，撤销忽略后原样恢复。

### 9.4 台账版本更替后的出路

`inventory_revision_id` 钉在组上，而 §17 要求目标属于**当前**台账版本。检测年度锁定的版本一变
（或年度未锁定时该桥出现更新的已确认版本），全部已绑组同时变成不可确认。没有出路的话，用户
唯一的选择是几百个组逐个重绑——这不是可接受的收尾。

因此提供 `inventory_repoint` 预览计划（§8.6、§13.3）：把指定导入记录下所有钉在旧版本的组重指
到当前版本。逐组判定，结果在预览里逐行列出：

| 情形 | 计划中的标记 | 应用时 |
| --- | --- | --- |
| 目标 `bridge_component_id` 在新版本中仍启用、类别一致 | 可自动重指 | 改写组的 `inventory_revision_id`，目标不变 |
| 该构件在新版本中已停用或类别变了 | 需人工重绑 | 组回落 `unresolved`，清空目标 |
| 组本身是 `unresolved` / `missing` | 仅改版本号 | 改写 `inventory_revision_id` |

重指按 §9.1 的完整流程走：版本递增、实例差量对齐、评分树按新哈希失效重算。构件 id 不变的那些
组，实例和覆盖原样保留。

## 10. 导入初始化

Python 只生成 5.0 来源事实。C++ 在持久化解析结果的同一业务流程中初始化解析状态：

1. 校验并保存 `parsed_result_json`；
2. 按来源构件名称和权威编号归一化规则建立构件组；
3. 建立 `candidate_id` 到构件组的成员关系；
4. 按 `resolve_confirmed_revision_ref()` 解析台账版本；
5. **解析得到版本时**：执行确定性构件匹配，唯一匹配写入目标并生成病害解析实例，再对活动实例
   执行评分树匹配；
6. **解析不到版本时**（该桥尚无已确认台账版本）：全部组停在 `unresolved`、
   `inventory_revision_id` 为空，跳过第 5 步，初始化照常成功；
7. 未匹配或歧义组保持 `unresolved`，候选按需生成；
8. 步骤 1–3 或步骤 5 自身出错时，整个初始化失败，导入记录不得进入可校对状态。

第 6 步单列出来是因为它决定一次导入能不能开始校对。“该桥尚无已确认台账版本”是现网就会出现的
正常状态，今天它只让绑定面板挂出提示，病害校对、照片核对照常进行。若把它当作初始化失败，一个
局部限制就升级成整条导入不可校对——用户连报告里解析出了什么都看不到，只能等台账确认。台账确认
之后，用 §9.4 的 `inventory_repoint` 计划一次性把这批组带到当前版本。

工作区读模型在这种情况下明确返回“该桥构件台账尚未确认”，并把全部绑定类动作标为不可用，而不是
让用户点进去才发现候选永远是空的。

候选列表不是权威事实，第一阶段不持久化。需要性能优化时只能增加带台账版本、评分树版本和输入哈希的可丢弃缓存。

## 11. 草稿保存与解析同步

### 11.1 普通保存请求

`saveReviewDraft` 的请求体仍然只接收 BridgeAnnualInspectionData 5.0，`expected_draft_version` 通过
`If-Match` 请求头携带，不把并发元数据塞进 5.0 合同。请求中出现构件解析、评分树解析或旧区间
拆分字段时返回稳定合同错误，不得静默忽略。

保存事务比较旧草稿与新草稿：

- 新增 `candidate_id`：创建或复用来源构件组并增加成员；组已绑定时生成实例并执行评分树匹配。
  这条分支只兜住“没有明确目标的新增”；**用户在界面上手工新增病害走 §13.2 的专用命令**，不经
  普通保存，理由见 §4.6；
- 删除 `candidate_id`：删除成员，级联删除实例和评分树解析；空构件组随之删除；
- 修改病害类型、位置、描述或来源分组/指标身份：重算相关实例的 `match_input_hash`。自动结果重新
  匹配；人工结果保留节点并返回内容变化复核提示（§8.5）。
  重算按**有效值**进行（§8.4）：某实例覆盖了被改的那个字段时，它的有效值没变，哈希也不该变，
  这条实例的评分树结果保持不动；
- 仅修改尺寸、数量、标度、备注或照片关系：不重新选择评分树节点；
- 修改来源构件名称或编号：普通保存**拒绝**，返回 `source_component_identity_immutable`。
  校对页今天也只读展示这两个值（`DefectDetailEditor.tsx:134`），没有产生这种请求的正常路径；
  真要纠正报告里的构件编号，删掉该条重新手工新增。本设计不为此引入“移动来源病害成员”命令
  （§3.8）——那个命令要处理“搬走后旧组变空、新组可能已绑到别处、两边实例与评分树各自失效”，
  代价与它的使用频率不成比例。

保存与解析同步在同一个 PostgreSQL 事务中完成：先锁定导入记录并比对 `draft_version`，再完成 JSON、
成员与评分树同步，最后递增版本。不能出现 JSON 已保存而成员/评分树状态未同步的中间状态，也不能
让陈旧整份草稿覆盖手工新增命令刚写入的来源病害（§8.0）。

### 11.2 实例级修正

区间或多目标展开后，用户若需针对单个目标修改位置、尺寸、标度等事实，调用病害解析实例专用接口，写入受控 `fact_overrides_json`。

该接口不得修改来源病害、目标构件或评分树节点。涉及匹配输入的实例级字段变化后，后端按新的**有效值**
重新计算 `match_input_hash`：自动结果重新解析，人工结果保留并标记内容变化（§8.5）。

请求以字段为单位：给出值即写入覆盖，给出“清除”即删除该键、恢复来源值，并携带该实例的
`expected_version`。响应回带递增后的实例 `version` 和
`overridden_fields`，供界面标出哪些字段已脱离来源值（§8.4）。

`instance_status` 不走这个接口，它有自己的接口（§13.2）——把“这条不要了”和“这条的尺寸改一下”
混在一个写操作里，会让“清空全部覆盖”这类请求的语义变得没法讲清楚。

## 12. 后端服务边界

新增统一的 `ImportResolutionService`，它是构件解析和评分树解析业务规则的唯一编排位置。仓储只负责数据库读写，不自行决定业务结果。

服务内部按职责拆分：

| 单元 | 职责 |
| --- | --- |
| `ResolutionWorkspaceQuery` | 生成前端读模型和进度统计 |
| `ComponentCandidateFinder` | 构件候选检索、归一化、类别和版本约束 |
| `ComponentResolutionCommand` | 单个、多目标、缺失、清除和重新绑定 |
| `ManualDefectCommand` | 手工新增病害：来源事实追加与显式解析写入（§4.6） |
| `DefectInstanceCommand` | 实例状态切换与实例级覆盖的读写 |
| `EffectiveDefectFactResolver` | 来源事实与实例覆盖的合并，有效事实的唯一出处（§8.4） |
| `ResolutionPlanBuilder` | 批量替换、区间展开、台账版本重指的预览 |
| `ResolutionPlanExecutor` | 校验并原子执行预览计划 |
| `ResolvedDefectInstanceBuilder` | 按来源成员和目标**差量对齐**实例，并重算照片归属（§9.1） |
| `RatingResolutionEngine` | 评分树自动匹配、人工选择和失效处理 |
| `DraftResolutionSynchronizer` | 普通草稿增删改与解析关系同步 |
| `ConfirmResolutionReader` | 为预检和正式确认提供权威解析视图，并合成区间溯源（§17.2） |

`EffectiveDefectFactResolver` 是单独一格而不是散在各处的原因：有效事实有四个消费方——哈希计算、
预检、正式确认、评定输入。任何一处直接读来源值，症状都是“界面上改过的实例，算分/入库时用的还是
老值”，且只在带覆盖的实例上出现。

现有 `ComponentMatcher`、构件分类词典、区间编号解析器和评分树匹配器可作为纯规则单元复用，但不得继续直接读写 `parsed_result_json`。

## 13. API 设计

路由前缀沿用现网的 `/api/import-records/{import_id}/…`。现网 24 条导入相关路由全在这个前缀下
（`/component-binding`、`/review-draft`、`/preflight-confirm`、`/edit-lock` 等），本设计不另起
`/api/imports`——两套前缀并存只会让此后每个新端点都要先问一句“这个归哪边”。

### 13.1 查询

```http
GET /api/import-records/{import_id}/resolution-workspace
GET /api/import-records/{import_id}/component-groups/{group_id}/candidates?q=
GET /api/import-records/{import_id}/defect-instances/{instance_id}/rating-candidates
```

工作区响应直接包含：

- 构件组和当前解析目标；
- 组成员和病害解析实例，每个实例带 `overridden_fields` 与 `is_photo_owner`；
- 评分树解析状态；
- 后端计算的进度、过滤标签和允许动作；
- 派生的部件层级聚合与 `ambiguous` 标签（§4.7）；
- 当前台账版本、评分树版本和各对象版本；
- 台账尚未确认时的明确标识，以及据此置灰的动作集合（§10）。

前端不得根据原始病害数组自行聚合绑定组或推导状态。

### 13.2 简单写操作

```http
PUT  /api/import-records/{import_id}/component-groups/{group_id}/resolution
PUT  /api/import-records/{import_id}/defect-instances/{instance_id}/rating-resolution
PUT  /api/import-records/{import_id}/defect-instances/{instance_id}/fact-overrides
PUT  /api/import-records/{import_id}/defect-instances/{instance_id}/status
POST /api/import-records/{import_id}/manual-defects
```

单构件、多目标、标记缺失、清除、人工评分树选择、实例级修正与实例状态切换提交：

- 编辑锁 token，走 **`X-Edit-Lock-Token` 请求头**，与现行绑定接口一致
  （`importBindingApi.ts:138`），不放请求体；
- `expected_version`：构件解析用组版本，评分树选择用评分树解析版本，实例覆盖和状态切换用实例
  自身版本；三者不得混用 `component_resolution_version` 充当并发版本；
- 用户选择的目标或动作；
- `expected_inventory_revision_id`，沿用已贯通 8 个端点的既有字段名；评分树侧同理用
  `expected_rating_tree_version_id`。

`POST /manual-defects` 是 §4.6 的专用命令，通过 `If-Match` 请求头携带
`expected_draft_version`，请求体携带来源病害事实、选定的
`bridge_component_id` 与 `rating_tree_node_id`，响应返回新建的来源病害、构件组、实例与评分树
解析，以及新的 `draft_version`。它是唯一一个既写 `parsed_result_json` 又写解析表的接口，两者同一
事务。命中既有组时严格按 §4.6 处理，不能借新增命令解析一个已有成员的 `unresolved` 组。

浏览器跨域配置把 `If-Match` 加入 `Access-Control-Allow-Headers`，并通过
`Access-Control-Expose-Headers: ETag` 暴露新版本；不另造第二个私有草稿版本请求头。

`PUT .../status` 只切换 `active` / `ignored`（§9.3），事务内重算照片归属。

后端重新验证所有业务约束，不能因为候选来自后端就信任客户端目标。

### 13.3 必须预览的写操作

```http
POST /api/import-records/{import_id}/resolution-plans
POST /api/import-records/{import_id}/resolution-plans/{plan_token}/apply
```

第一阶段下列操作必须先生成预览计划：

- 批量替换（参与范围沿用现行规则：只含 `unresolved` 组，已绑定与已标记缺失的组不参与、不受
  影响，需要重绑的先清除绑定）；
- 区间展开；
- 台账版本重指（§9.4）；
- 影响多个来源构件组的批量清除或批量重算。

计划响应至少包含：

- 操作类型；
- 受影响构件组和病害实例；
- 目标构件；
- 展开前后数量；
- 评分树失效或重算数量；
- 行级警告与阻断原因；
- 台账、评分树和构件组版本前提；
- `plan_token` 和过期时间。

单条明确绑定不强制增加预览步骤。它使用 `expected_version` 和完整后端校验直接执行。预览操作和简单操作内部仍复用同一命令服务及校验器。

## 14. 预览与执行一致性

应用计划时，在同一事务内依次处理：

1. 按 token 查询计划并对计划行执行 `SELECT ... FOR UPDATE`；校验它属于当前用户和导入记录；
2. 若状态已经是 `applied`，立即返回首次 `apply_result_json`。这是结果重放，不再次执行写操作，
   不要求原编辑锁仍然存活；用于覆盖“服务端已成功、客户端响应丢失”后的重试；
3. 若状态为 `expired` 或 `invalidated`，返回对应稳定错误；
4. 对 `ready` 计划校验未过期（创建后 15 分钟内）；
5. 校验当前用户仍持有有效导入记录编辑锁，**且其 token 哈希与计划记录的
   `lock_token_hash` 相同**；心跳续租不换 token，因此正常编辑期间这条恒成立，而锁掉线后被
   重新拿到时 token 已换，计划按 `resolution_plan_invalidated` 拒绝；
6. 台账版本和评分树版本仍与计划一致；
7. 每个受影响构件组的 `version` 与计划一致；
8. 所有目标仍属于当前桥梁、当前台账版本和允许的构件类别；
9. 所有来源病害成员仍存在且仍属于计划中的构件组。

任一检查失败则整批不写入。成功时原子更新解析状态、实例、评分树结果、计划状态和审计事件。

计划行锁或等价的 `UPDATE ... WHERE status = 'ready'` 条件写是强制要求，防止两个并发应用请求都
观察到 `ready` 后重复执行。重复提交已经成功应用的 token 时返回首次 `apply_result_json`；同一
token 不得再次执行状态变更。

## 15. 编辑锁和并发

第一阶段继续使用 `import_record_edit_locks`：同一导入记录同一时间只有一名编辑者。锁的 TTL 是
**2 分钟**，由前端心跳续到 `now() + 2 minutes`（`EditLockRepository.cpp:45`、`:206`）。本设计不
改这个数值，但任何“以锁到期时刻为准”的推导都必须先看清它——2 分钟是心跳间隔量级，不是人的操作
时长量级，§8.6 的计划有效期就栽在这一点上。

来源草稿使用 `draft_version`，构件组、病害实例和评分树解析各使用自己的对象版本，防止：

- 浏览器重复提交；
- 同一用户多个标签页覆盖；
- 预览后对象发生变化；
- 后台台账或评分树版本切换。

编辑锁解决业务所有权，`draft_version` / 对象 `version` 解决陈旧写入，两者不能互相替代。
`component_resolution_version` 只说明实例和评分树结果依据的是哪一代构件解析，不是实例行的并发
版本，不能拿它代替实例 `version`。

## 16. 前端职责

### 16.1 保留

- 工作区加载、搜索输入和过滤选择；
- 候选展示；
- 用户目标选择；
- 预览对话框；
- 手工新增病害对话框的取数与表单校验（提交改走 §13.2 的命令）；
- 手工新增成功后，先把响应中的来源病害合并进本地草稿，再接受新的 `draft_version`；两步必须作为
  一次前端状态更新，不能只更新版本却保留缺少新候选的旧草稿；
- 按 `overridden_fields` 标出实例级覆盖，并提供“恢复来源值”；
- 加载、空状态和错误提示；
- 后端执行结果的局部状态替换。

### 16.2 删除

- TypeScript 构件编号归一化镜像；
- 前端替换表达式展开；
- 前端批量替换有效性判断；
- 前端区间拆分合法性和数量推演；
- 前端左右侧目标组合判断；
- 前端评分树失效判断；
- 绑定写操作后的父草稿整体失效和整页重载；
- 草稿 reducer 里直接写解析字段的动作：`add_defect` 改为调用 `POST /manual-defects`
  （`reviewDraft.ts:228`），`link_defect_component`（`reviewDraft.ts:282`）直接删除——它现网已
  无调用方，留着只会给 5.0 迁移平添一处要改的地方。

现有 `normalizeComponentNumber.ts`、`replacePattern.ts`、`replacePreview.ts` 及仅验证这些业务规则的前端测试应被删除。对应规则测试迁移到 C++ 后端。

### 16.3 组件边界

建议把当前大型工作区拆成：

| 组件 | 职责 |
| --- | --- |
| `ResolutionWorkspaceController` | 请求、局部缓存和命令编排 |
| `ResolutionProgressSummary` | 展示后端统计 |
| `ComponentGroupQueue` | 按部件层级聚合筛选并展示来源构件组 |
| `ComponentResolutionAction` | 单个、多目标、缺失和清除操作 |
| `RatingResolutionPanel` | 病害实例评分树候选与人工选择 |
| `DefectInstancePanel` | 实例列表、覆盖标识与恢复、忽略与撤销、照片归属提示 |
| `ResolutionPlanDialog` | 展示批量、区间或重指计划并执行 token |

组件不复制后端领域规则；允许动作和阻断原因由 DTO 明确提供。

## 17. 预检与正式确认

`PreflightReport` 和 `ConfirmPlan` 不再从病害 JSON 读取构件或评分树解析字段。

### 17.1 可确认病害视图

`ConfirmResolutionReader` 组合：

```text
来源病害事实
+ 构件组及解析目标
+ 病害解析实例及实例级覆盖
+ 评分树解析
= 可确认病害视图
```

每个活动实例进入正式确认前必须满足：

- 来源病害仍存在且未被忽略；
- 构件组状态为 `bound`；
- 目标属于当前桥梁及当前台账版本；
- 评分树解析状态为 `matched`；
- 评分树节点属于当前评分树版本并适用于目标构件类别；
- `component_resolution_version` 与当前构件组版本一致；
- `applicability_hash` 与当前适用性输入一致；
- 自动匹配结果的 `match_input_hash` 与当前输入一致；人工结果仅内容哈希变化时保留节点并产生复核
  提示，不把它当作解析缺失（§8.5）。

`missing` 和 `unresolved` 的阻断或提示口径保持现有正式确认规则，不在本设计中降低约束。

预检对来源病害的检查（联合确认状态、照片引用处理状态）仍按来源病害逐条进行，不按实例展开：
照片引用本来就只有来源病害这一份，`PreflightReport.cpp:225` 的遍历口径不变。

### 17.2 正式事实的字段来源

正式确认为每个活动实例写一条 `defect_observations`。字段来源逐项定死，避免实施时各取所需：

| 目标列 | 来源 |
| --- | --- |
| `bridge_component_id` | 解析目标 |
| `structure_part` / `part_name` / `component_type` / `business_component_code` | 由目标构件在组所钉台账版本中的条目与生效映射派生 |
| `defect_type` / `defect_location` / `defect_description_raw` / `scale` | **有效病害事实**（§8.4） |
| `defect_measurements` 子表 | 由有效事实中的 `measurements`、`measurement_text`、`quantity_text` 按现行 `ConfirmPlan::append_measurements` 口径生成 |
| `rating_tree_node_id` / `standard_defect_indicator_id` | 该实例的评分树解析 |
| `review_status` | 来源病害的校对状态（§8.4） |
| `source_raw_cells_json.raw_row_text` | 来源病害 |
| `source_raw_cells_json.photo_references` | `is_photo_owner = true` 的实例取来源病害的引用，其余实例写空数组 |
| `source_raw_cells_json.range_split_origin` | 展开为多个活动实例时合成，见下 |

前两行原先冻结在草稿 JSON 里（`bridge_component_id`、`standard_component_category_id`、
`resolved_structure_part`），现在改为按组所钉的台账版本当场派生。这是有意的：组已经钉死了版本
（§8.1），派生结果确定，且不再存在“JSON 里冻的类别与台账当前映射不一致”这类漂移。

照片一行是 2026-08-25 那次修复的语义，必须原样保住：同一个照片编号出现在 N 条观测上，报告里的
编号交叉引用就作废了。区别在于新模型不再“先复制 N 份再清掉 N-1 份”，而是一开始就只有一份。

尺寸子表不能漏过 `EffectiveDefectFactResolver`：实例覆盖了 `measurements`、`measurement_text` 或
`quantity_text` 时，正式写入的 `defect_measurements` 必须反映覆盖后的有效值。`remark` 目前没有
对应的正式病害观测列，第一阶段只保留在校对事实/实例覆盖中；若以后要进入档案，另行定义正式字段，
不能临时塞入 `source_raw_cells_json`。

**区间溯源的合成。** `range_split_origin` 移出合同后（§7.1），由 `ConfirmResolutionReader` 按
原字段形状合成，档案页与历史对比不做改动：

| 字段 | 取值 |
| --- | --- |
| `operation_id` | 产生当前实例集合的那次操作 id（计划 id 或构件解析审计事件 id） |
| `source_candidate_id` | 来源病害 `candidate_id` |
| `source_component_number` | 构件组的 `source_component_number` 原文 |
| `expanded_component_number` | 目标构件在台账中的编号 |
| `split_index` | 该实例在活动实例中按 `instance_order` 排出的序号，从 1 起 |
| `split_count` | 该来源病害的活动实例总数 |
| `operated_by_user_id` / `operated_at` | 构件组的 `resolved_by_user_id` / `resolved_at` |

仅在活动实例数大于 1 时写入，与现行“单构件绑定不带溯源”一致；`split_index ≤ split_count` 的
约束天然成立。合成发生在确认时，因此忽略掉某条实例后再确认，`split_count` 反映的是实际入库
条数，不是历史上曾经展开过的条数。

`operated_by_user_id` 与 `operated_at` 在原字段里都是必填非空。多实例只可能由人工操作产生
——自动匹配唯一命中时只写一个目标——所以组上这两个值必然有；实施时仍要断言它们非空，遇空
按内部错误阻断确认，而不是塞一个占位值进正式事实。

## 18. 错误处理

| 错误码 | 含义 | 前端处理 |
| --- | --- | --- |
| `edit_lock_invalid` | 编辑锁不存在、过期或不属于当前用户 | 切换只读，提示重新获取编辑权 |
| `review_draft_version_conflict` | 来源草稿版本过期 | 保留本地编辑，刷新差异后重新提交，不得自动覆盖 |
| `resolution_version_conflict` | 构件组、病害实例或评分树解析版本过期 | 只刷新冲突对象 |
| `resolution_plan_expired` | 预览计划已过期 | 保留用户输入并重新生成预览 |
| `resolution_plan_invalidated` | 计划前提已变化，含编辑锁 token 已更换 | 展示变化原因并重新预览 |
| `component_inventory_revision_changed` | 当前台账版本已变化 | 刷新工作区，提示走版本重指 |
| `component_inventory_not_confirmed` | 该桥尚无已确认台账版本 | 绑定类动作置灰，其余校对照常 |
| `rating_tree_version_changed` | 当前评分树版本已变化 | 重新生成评分树匹配 |
| `target_not_allowed` | 目标不属于当前桥梁、版本或允许类别 | 展示后端目标级原因 |
| `resolution_has_blockers` | 批量计划包含阻断项 | 整批不应用，定位阻断行 |
| `draft_contains_resolution_fields` | 普通草稿夹带解析字段 | 提示客户端合同不兼容 |
| `source_component_identity_immutable` | 普通草稿试图改来源构件名称或编号 | 提示删除后重新新增 |
| `manual_defect_group_conflict` | 手工新增命中的组已绑到别处或已标缺失 | 提示改用绑定工作区 |
| `manual_defect_group_requires_resolution` | 手工新增命中了已有成员的未解析组 | 提示先在绑定工作区解析该组 |
| `database_unavailable` | 数据库操作失败 | 保留当前页面，不伪造成功状态 |

前两处名字沿用现网既有通道，不另起新名：`edit_lock_invalid` 见 `ImportBindingRoutes.cpp:295`，
`component_inventory_revision_changed` 见 `importBindingApi.ts:158`。前端已有对这两个码的处理
分支，改名等于让同一件事在同一个页面上有两套处理。

后端异常不能以数据库原始文本直接返回。所有批量写操作失败时必须完整回滚。

## 19. 测试策略

### 19.1 合同测试

- Python、JSON Schema、C++、TypeScript 均只接受合同 5.0；
- 5.0 拒绝所有已移出的构件和评分树字段；
- 来源指标身份字段继续存在且不被评分树重算修改；
- 4.0 被标记为需要重新解析；
- 四边枚举逐项对齐：同一份枚举清单由一处生成或由测试逐值比对，不再出现
  `component_match_method` 那样“前端多一个值、后端写了这个值、契约不认”的三边漂移（§7.4）。

### 19.2 数据库约束测试

- 来源病害在同一导入内只能属于一个构件组；
- 解析目标不能跨桥、跨台账版本；
- 同一组不能重复选择目标构件；
- 同一来源成员和目标只能生成一个实例；
- 每个实例最多一个当前评分树解析；
- 实例覆盖与状态切换按实例 `version` 做条件更新，陈旧版本零写入；
- **无编号病害归一化落空串，同名部件下的多条无编号病害进同一个组**——这条要专门测，用 NULL
  写会让唯一约束静默失效，不测就发现不了；
- `bound` 组必须有非空 `inventory_revision_id`，`unresolved` 组允许为空；
- 导入删除级联删除全部解析状态、计划和重开快照；
- 重开、放弃和重新确认能同时恢复或清理来源草稿与解析状态。

### 19.3 服务测试

- 精确匹配、确认别名、未匹配和歧义；
- 单构件、左右侧、多目标和区间展开；
- 标记缺失、清除和重新绑定；
- 构件版本变化使旧评分树结果失效；
- 病害类型、位置或描述变化使旧输入哈希失效；
- 来源分组 ID/编号、规范桥型或技术规范包变化使自动匹配结果失效；
- 人工评分树节点在普通文字修改后继续保留并返回复核提示；构件类别、桥型或评分树版本变化时才
  失效；
- 尺寸和标度变化不重新选择评分树节点；
- 预览计划版本冻结、过期、作废和幂等重放；
- **心跳续租期间计划仍然有效**，锁掉线后重新获取（token 已换）则计划作废；
- 批量计划任一阻断时零写入；
- 区间展开只让首个活动实例继承照片；
- **忽略掉 `instance_order` 最小的实例后，照片归属移到下一个活动实例**；全部实例被忽略后无人
  持有照片；撤销忽略后归属回到最小序号；
- **换掉多目标中的一个目标，其余实例的 `fact_overrides_json` 与 `instance_status` 保留**；
- 有效事实：实例覆盖了病害类型时，该实例按覆盖值重算评分树；此时改来源值不影响该实例；
- 清除某个覆盖键后恢复来源值，并按来源值重算；
- 必填覆盖字段拒绝 `null`，`measurements` 按 5.0 的 `Measurement[]` 合同校验；
- 台账未确认的桥导入后仍可校对，组停在 `unresolved` 且版本为空；
- 台账确认后 `inventory_repoint` 能把可自动重指的组带到新版本，构件已停用的组回落
  `unresolved`；
- 手工新增病害写入显式目标与评分树节点；命中已绑到别处或已标缺失的组时按
  `manual_defect_group_conflict` 拒绝；命中已有成员的未解析组时按
  `manual_defect_group_requires_resolution` 拒绝，不暗中绑定旧成员。

### 19.4 API 集成测试

- 工作区响应无需前端聚合即可展示；
- 候选查询只返回当前桥梁和版本允许的目标；
- 所有写接口验证编辑锁和对象版本；
- 普通草稿保存和手工新增都用 `If-Match` 验证 `expected_draft_version`，响应暴露 `ETag`；先手工
  新增、后提交陈旧整份草稿时返回
  `review_draft_version_conflict`，新病害不能被当成删除；
- 写操作只返回受影响对象和最新统计；
- 绑定后不要求重新加载 `parsed_result`；
- 普通草稿保存不能修改解析表中的权威状态；
- 普通草稿携带来源构件名称或编号的改动时按 `source_component_identity_immutable` 拒绝；
- 编辑锁走 `X-Edit-Lock-Token` 头，缺失或失效时返回 `edit_lock_invalid`；
- 台账版本参数用 `expected_inventory_revision_id`，不一致时返回
  `component_inventory_revision_changed`；
- 预检和确认从关系表读取构件与评分树解析；
- 正式确认写出的 `source_raw_cells_json` 含合成的 `range_split_origin`，`split_count` 等于实际
  入库的活动实例数；非照片归属实例的 `photo_references` 为空数组；
- 正式确认按实例有效事实生成 `defect_measurements`；
- 已应用计划在原锁失效后重试仍返回首次结果；两个并发应用请求最多一个执行状态变更；
- 重开、放弃恢复和重新确认会作废该导入下全部 `ready` 计划。

### 19.5 前端测试

- 展示后端构件组、候选、状态和允许动作；
- 单个命令成功后只替换响应涉及的对象；
- 批量和区间计划按后端结果展示，不在前端重新计算；
- 版本冲突只刷新冲突对象；
- 草稿版本冲突保留本地编辑并进入差异恢复，不自动覆盖服务端草稿；
- 手工新增成功响应把新候选与新 `draft_version` 原子合并进前端状态，随后保存不会把它删除；
- 计划过期可以保留输入并重新预览；
- 部件层级聚合与歧义计数按后端返回渲染，不在前端统计；
- 覆盖过的字段标出“已按本实例单独设定”并能恢复来源值；
- 台账未确认时绑定类动作置灰，病害与照片校对仍可进行；
- 删除前端归一化、替换和区间业务规则测试。

### 19.6 端到端测试

对接口同步真实测试任务重新导入并执行：

```text
解析
-> 自动构件匹配
-> 人工构件解析
-> 区间/多目标展开
-> 实例级修正与忽略
-> 评分树解析
-> 手工新增一条病害
-> 保存病害草稿
-> 预检
-> 正式确认
-> 构件病害档案查询（核对区间溯源与照片归属）
```

绑定、实例修正和评分树操作前后计算 `parsed_result_json` 规范校验和，必须保持一致。两类操作除外：
普通病害事实编辑，以及手工新增病害——后者按定义要往 `parsed_result_json.defects` 追加一条来源
事实（§4.6），它改变校验和是正确行为，测试断言的是“只多了这一条，其余字节不变”。

## 20. 验收标准

1. 新导入使用 BridgeAnnualInspectionData 5.0，JSON 中不存在构件解析、评分树解析或区间展开派生字段。
2. 构件绑定、批量替换、区间展开、标记缺失、清除和评分树选择均不更新 `parsed_result_json`。
3. 同一来源构件组只存一份构件解析；组内多条病害分别拥有评分树解析。
4. 多目标和区间构件生成稳定解析实例，正式确认能生成正确数量的年度病害观测。
5. 多实例只由约定的首个活动实例继承来源照片。
6. 构件目标或台账版本变化后，旧评分树结果不能进入正式确认。
7. 病害匹配输入变化后，旧评分树结果按输入哈希失效。
8. 批量和区间预览由后端生成；前端执行时只提交 plan token，不提交自行计算的结果集合。
9. 过期或版本冲突计划原子拒绝；重复应用已成功计划不会重复写入。
10. 前端不再包含构件编号归一化、批量替换展开、区间合法性和评分树失效业务规则。
11. 解析写操作完成后前端只局部更新，不使父级病害草稿失效，也不整页重载。
12. `saveReviewDraft` 不能覆盖、清除或伪造构件和评分树解析状态。
13. 预检和正式确认直接读取解析关系表，未解析或失效数据按现有规则阻断或提示。
14. 已确认记录重开后，放弃修改能同时恢复来源草稿和解析状态。
15. 手工新增病害仍可用，且保住用户选定的目标构件与评分树节点，不退化成自动匹配。
16. 该桥尚无已确认台账版本时，导入仍可进入校对；台账确认后能一次性把这批组重指到当前版本。
17. 预览计划在正常编辑（心跳续租）期间不会过期；锁掉线后重新获取则计划作废。
18. 换掉多目标中的一个目标，其余实例的实例级修正与忽略状态不丢。
19. 实例级覆盖在界面上可见、可恢复；下游哈希、预检、确认和评定一律按有效事实计算。
20. 正式事实里的区间溯源与照片归属与 5.0 之前一致，构件病害档案页无需改动即可正确展示。
21. 全部数据库、C++、Python 和前端测试通过，接口同步真实测试任务完整走通。
22. 普通草稿保存与手工新增共享 `draft_version` 并发边界，陈旧整份草稿不能删除或覆盖更新事实。
23. 实例覆盖与忽略使用实例自身版本；必填事实不能通过覆盖 JSON 写成 `null`，正式尺寸按有效事实
    入库。
24. 人工评分树选择在普通文字修改后继续保留；只有适用性变化才失效，自动匹配仍按完整输入哈希
    失效。
25. 已应用计划可稳定重放；重开、恢复快照和重新确认后，旧 `ready` 计划全部作废。

## 21. 实施顺序约束

后续实施计划应按以下依赖顺序展开：

1. 合同 5.0 与数据库结构（含 `draft_version`、实例 `version` 和评分树双哈希）；
2. 导入初始化和关系表仓储（含台账未确认分支）；
3. `ImportResolutionService`、`EffectiveDefectFactResolver` 与工作区查询；
4. 单个构件/评分树命令，实例状态与实例级覆盖命令；
5. 手工新增病害命令；
6. 批量、区间及台账版本重指预览计划；
7. 草稿同步、预检和正式确认改造（含区间溯源合成）；
8. 前端迁移并删除重复规则；
9. 重开快照、计划作废、审计和端到端验收；
10. 经明确授权后重置测试数据并重新导入。

第 3 步的 `EffectiveDefectFactResolver` 必须先于第 4 步落地：实例级覆盖一旦可写，哈希、预检、
确认、评定四个消费方就都需要有效事实，此时才补合并规则会留下“某几处读来源值”的长尾。

第 5 步不能推到前端迁移之后。前端一旦按 5.0 停止在草稿里写解析字段，手工新增就没有落点；
命令必须先在后端可用。

不得先删除旧前端规则，再依赖尚未完成的后端 DTO；也不得让新关系表和旧 JSON 字段长期双写。

## 22. 风险与控制

### 22.1 范围扩张

拆出评分树会触及合同、预检、正式确认和重开流程。控制方式是只关系化构件和评分树解析，不顺带关系化全部病害草稿。

### 22.2 来源病害与解析实例身份混淆

`candidate_id` 始终表示来源病害，实例使用独立 UUID。所有 API 字段必须明确使用 `source_candidate_id` 或 `resolved_defect_instance_id`，不能使用含糊的 `defect_id`。

### 22.3 预览后数据变化

计划冻结台账、评分树和构件组版本。执行时重新验证，失败则整批拒绝，不尝试“尽可能执行”。

### 22.4 实例级覆盖重新形成大 JSON

`fact_overrides_json` 只保存允许字段的差异，不复制完整病害对象；后端使用白名单校验。若后续实例级编辑需求扩大，应另立完整关系化设计，不能无限扩张覆盖 JSON。

### 22.5 测试数据无迁移不等于可以自动删除

本设计只取消迁移兼容要求，不授权实施过程自动删除当前数据库记录。重置测试数据必须在实施验收阶段单独确认目标和操作。

### 22.6 校对粒度从“展开后每条”回到“报告里那一行”

实例不再各带一份校对状态（§8.4）。好处是校对对象与报告行一一对应，不必逐条确认系统展开出来的
中间产物；代价是“三跨里只认两跨”这类判断要用忽略实例表达，而不是让第三条停在未确认。

风险在于操作习惯：现行界面上那三条是三行待确认，用户逐行点过去。新界面必须把“一条来源病害 →
N 条实例”显式画出来，并让忽略动作就在实例旁边，否则用户会去找那两条“消失的待确认行”。这属于
交互实现要求，不是数据模型的可选项。

### 22.7 手工新增是唯一同时写两侧的命令

`POST /manual-defects` 同时写 `parsed_result_json` 和解析表，是唯一打破“来源事实归 JSON、解析
状态归关系表”这条分工的接口。它必须保持在一个事务内，并且**不得扩张**：任何“顺便也支持改点别的”
的需求都应该退回各自的接口。一旦它变成半个通用写入口，§4.4 划的边界就形同虚设。

## 23. 变更记录

| 日期 | 变更 | 原因 |
| --- | --- | --- |
| 2026-08-27 | 创建设计 | 将构件解析和评分树解析从大 JSON 中拆出，并把匹配与预览统一收回后端 |
| 2026-08-27 | 首轮评审后修订 | 见下 |
| 2026-08-27 | 二轮评审后修订 | 补齐草稿/实例并发、人工评分树保护、完整哈希、覆盖类型和计划幂等 |

首轮评审对照现网代码逐条核对了本文的断言，改动集中在四类：

**阻断项（不改则实施必坏）**

- §4.6、§13.2：补手工新增病害的专用命令。原稿删掉的六个字段正是该路径写入的，且 §11.1 会让它
  的保存直接失败——这条路会整条切断。
- §8.6、§14、§15：计划有效期改为“创建后 15 分钟 + 编辑锁 token 哈希一致”。原稿按锁到期时刻截断，
  而锁 TTL 只有 2 分钟且靠心跳续租，会让所有计划活不过两分钟。
- §8.1、§10：`inventory_revision_id` 改可空，台账未确认时初始化照常成功。原稿会把“该桥台账尚未
  确认”从局部限制升级成整条导入不可校对。
- §7.1、§17.2：补 `range_split_origin` 的确认期合成。它不是校对期字段，今天会落进
  `defect_observations.source_raw_cells_json`，档案页还在读。

**结构性空洞**

- §8.4：定义有效病害事实与合并规则；照片归属改为“活动实例中序号最小者”；明确实例不带独立校对状态。
- §9.1：实例改为按 `(group_member_id, target_id)` 差量对齐，保住覆盖与忽略状态。
- §9.3、§13.2：补实例状态接口。原稿定义了 `instance_status` 却没有改它的入口。
- §9.4：补台账版本重指计划。原稿下版本一变，全部已绑组同时不可确认且无出路。
- §3.8、§11.1：把“移动来源病害成员”从“必须使用的命令”改为明确不支持——原稿引用了它但从未设计，
  而校对页本就不允许改来源构件编号。

**建模口径**

- §4.7：部件层级与 `ambiguous` 明确为读模型派生物，原稿删掉了这两者却没说去哪了。
- §8.1：归一化编号非空、缺失时落空串，否则唯一约束遇 NULL 静默失效。

**与现网对齐**

- 路由前缀 `/api/import-records/`、编辑锁走 `X-Edit-Lock-Token` 头、错误码
  `edit_lock_invalid` 与 `component_inventory_revision_changed`、版本参数
  `expected_inventory_revision_id`、台账版本解析口径 `resolve_confirmed_revision_ref()`。
- §7.4：记录 `component_match_method` 的三边枚举漂移（现网标记缺失会写一个契约不认的值），
  并说明 5.0 如何顺带消除它。

二轮评审继续补齐了以下实施阻断点：

- §8.0、§11.1、§13.2：增加 `draft_version`。普通整份草稿保存不能覆盖手工新增命令刚写入的来源
  病害，同一用户多标签页也必须得到明确版本冲突；
- §8.4、§11.2、§13.2：实例增加自身 `version`，并为覆盖字段定义逐字段类型、可空性和合并后
  校验；
- §8.5、§9.2、§17.1：评分树结果拆分 `applicability_hash` 与 `match_input_hash`，补齐来源分组、
  规范桥型和技术规范包输入；人工选择在普通文字修改后继续保留；
- §17.2：正式确认的字段来源补上 `defect_measurements`，尺寸按实例有效事实生成；
- §14：已应用计划先返回首次结果，再校验 `ready` 计划的实时锁和版本；计划行必须加锁或条件更新；
- §4.6：手工新增命中已有成员的未解析组时拒绝并引导先解析，避免单条新增暗中绑定一批旧病害；
- §8.8：重开、恢复快照和重新确认在事务内作废全部 `ready` 计划，防止恢复后的旧版本号让历史计划
  再次变得可执行。
