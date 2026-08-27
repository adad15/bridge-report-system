# 导入构件解析与评分树解析状态分离设计

> 日期：2026-08-27
>
> 状态：设计已确认，尚未实施
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

## 3. 不在本次范围内

1. 不接入大模型、向量检索或外部推理服务。
2. 不重做构件绑定工作区的整体交互布局。
3. 不把病害位置、尺寸、照片关系和一般校对状态全部关系化。
4. 不改变构件台账、评分树版本和正式病害事实的业务定义。
5. 不允许自动跨桥、跨台账版本或绕过构件类别约束绑定。
6. 不提供现有测试导入记录的迁移或回滚脚本。
7. 不取消导入记录级编辑锁，也不实现构件组级多人协作。

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

## 5. 术语

| 术语 | 含义 |
| --- | --- |
| 来源病害 | Python 导入器或人工新增产生的一条病害候选，以 `candidate_id` 标识 |
| 来源构件组 | 同一导入中来源构件名称和规范化编号相同的一组病害 |
| 构件解析 | 来源构件组到零个、一个或多个当前台账构件的受控映射 |
| 解析目标 | 构件解析选中的一个台账构件 |
| 病害解析实例 | 一条来源病害在一个解析目标上的展开结果 |
| 评分树解析 | 病害解析实例到一个当前评分树节点的受控映射 |
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

## 8. 数据模型

### 8.1 `import_component_resolution_groups`

保存来源构件组身份和当前构件解析状态。

| 字段 | 含义 |
| --- | --- |
| `id` | UUID 主键 |
| `import_record_id` | 导入记录，删除导入时级联删除 |
| `source_component_name` | 来源构件名称原文 |
| `source_component_number` | 来源构件编号原文，可空 |
| `normalized_component_number` | C++ 权威归一化结果 |
| `resolution_mode` | `single`、`multi` 或 `range` |
| `status` | `unresolved`、`bound` 或 `missing` |
| `match_method` | `exact`、`confirmed_alias`、`manual`、`side_pair`、`range` 或空 |
| `inventory_revision_id` | 本次解析所依据的构件台账版本 |
| `version` | 乐观并发版本，成功状态变更后递增 |
| `resolved_by_user_id` | 最近一次人工解析者，可空 |
| `resolved_at` | 最近一次人工解析时间，可空 |

唯一约束：

```text
(import_record_id, source_component_name, normalized_component_number)
```

状态不变量：

- `bound` 至少有一个当前解析目标；
- `unresolved` 和 `missing` 没有解析目标；
- `missing` 只能由人工明确设置；
- `inventory_revision_id` 必须是导入年度当前绑定的已确认台账版本。

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

当一条来源病害展开为多个实例时，仅 `instance_order=1` 的活动实例设置 `is_photo_owner=true`。

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
| `match_input_hash` | 本次匹配输入的规范 SHA-256 |
| `version` | 评分树解析乐观并发版本 |
| `resolved_by_user_id` | 人工选择者，可空 |
| `resolved_at` | 人工选择时间，可空 |

`matched` 必须同时具备有效评分树版本和节点。`unresolved` 不得保留节点或标准指标。

`match_input_hash` 至少包含：

```text
source_candidate_id
target bridge_component_id
target component category
rating_tree_version_id
source_defect_indicator_id / number
defect_type
defect_location
defect_description
```

只要当前规范输入计算出的哈希与记录不一致，该评分树结果即视为失效，不得进入预检或正式确认。

### 8.6 `import_resolution_operation_plans`

保存需要显式预览的短期执行计划。

| 字段 | 含义 |
| --- | --- |
| `id` | UUID，同时作为不透明 plan token |
| `import_record_id` | 所属导入记录 |
| `actor_user_id` | 创建计划的用户 |
| `operation_type` | `bulk_replace`、`range_expand` 或其他受控批量操作 |
| `request_json` | 规范化后的用户意图 |
| `plan_json` | 冻结的目标、影响行、警告和阻断原因 |
| `preconditions_json` | 台账版本、评分树版本及构件组版本集合 |
| `status` | `ready`、`applied`、`expired` 或 `invalidated` |
| `expires_at` | 短期过期时间 |
| `applied_at` | 成功执行时间，可空 |
| `apply_result_json` | 首次执行结果，用于幂等重放 |

计划只允许创建者在同一导入记录编辑锁下执行。计划有效期取“创建后 15 分钟”和“当前编辑锁到期时间”中的较早者；编辑锁续租不延长已生成计划。重复应用已成功计划时返回原 `apply_result_json`，不重复创建目标或实例。

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
3. 按来源成员和新目标集合重建病害解析实例；
4. 删除不再存在实例的评分树解析；
5. 对保留或新增实例重新计算 `match_input_hash`；
6. 旧哈希或旧构件版本的评分树结果失效；
7. 运行评分树自动匹配，无法唯一确定的保持 `unresolved`；
8. 写入审计事件。

若请求没有造成任何实际状态变化，后端返回幂等成功，不递增版本。

### 9.2 评分树解析

```text
unresolved -> matched      （规则唯一匹配或人工选择）
matched    -> matched      （人工重新选择）
matched    -> unresolved   （构件或匹配输入变化）
```

忽略的来源病害或解析实例不要求评分树节点。所有活动实例在正式确认前必须满足现有评分树绑定规则。

## 10. 导入初始化

Python 只生成 5.0 来源事实。C++ 在持久化解析结果的同一业务流程中初始化解析状态：

1. 校验并保存 `parsed_result_json`；
2. 按来源构件名称和权威编号归一化规则建立构件组；
3. 建立 `candidate_id` 到构件组的成员关系；
4. 使用当前已确认台账版本执行确定性构件匹配；
5. 唯一匹配时写入目标并生成病害解析实例；
6. 对活动实例执行评分树匹配；
7. 未匹配或歧义组保持 `unresolved`，候选按需生成；
8. 整个初始化失败时，导入记录不得进入可校对状态。

候选列表不是权威事实，第一阶段不持久化。需要性能优化时只能增加带台账版本、评分树版本和输入哈希的可丢弃缓存。

## 11. 草稿保存与解析同步

### 11.1 普通保存请求

`saveReviewDraft` 只接收 BridgeAnnualInspectionData 5.0。请求中出现构件解析、评分树解析或旧区间拆分字段时返回稳定合同错误，不得静默忽略。

保存事务比较旧草稿与新草稿：

- 新增 `candidate_id`：创建或复用来源构件组并增加成员；组已绑定时生成实例并执行评分树匹配；
- 删除 `candidate_id`：删除成员，级联删除实例和评分树解析；空构件组随之删除；
- 修改病害类型、位置、描述或来源指标身份：重算相关实例的 `match_input_hash` 并重新匹配评分树；
- 仅修改尺寸、数量、标度、备注或照片关系：不重新选择评分树节点；
- 修改来源构件名称或编号：普通保存拒绝，必须使用专门的“移动来源病害成员”命令。

保存与解析同步在同一个 PostgreSQL 事务中完成，不能出现 JSON 已保存而成员/评分树状态未同步的中间状态。

### 11.2 实例级修正

区间或多目标展开后，用户若需针对单个目标修改位置、尺寸、标度等事实，调用病害解析实例专用接口，写入受控 `fact_overrides_json`。

该接口不得修改来源病害、目标构件或评分树节点。涉及匹配输入的实例级字段变化后，后端重新计算评分树解析。

## 12. 后端服务边界

新增统一的 `ImportResolutionService`，它是构件解析和评分树解析业务规则的唯一编排位置。仓储只负责数据库读写，不自行决定业务结果。

服务内部按职责拆分：

| 单元 | 职责 |
| --- | --- |
| `ResolutionWorkspaceQuery` | 生成前端读模型和进度统计 |
| `ComponentCandidateFinder` | 构件候选检索、归一化、类别和版本约束 |
| `ComponentResolutionCommand` | 单个、多目标、缺失、清除和重新绑定 |
| `ResolutionPlanBuilder` | 批量替换、区间展开预览 |
| `ResolutionPlanExecutor` | 校验并原子执行预览计划 |
| `ResolvedDefectInstanceBuilder` | 按来源成员和目标生成稳定实例 |
| `RatingResolutionEngine` | 评分树自动匹配、人工选择和失效处理 |
| `DraftResolutionSynchronizer` | 普通草稿增删改与解析关系同步 |
| `ConfirmResolutionReader` | 为预检和正式确认提供权威解析视图 |

现有 `ComponentMatcher`、构件分类词典、区间编号解析器和评分树匹配器可作为纯规则单元复用，但不得继续直接读写 `parsed_result_json`。

## 13. API 设计

### 13.1 查询

```http
GET /api/imports/{import_id}/resolution-workspace
GET /api/imports/{import_id}/component-groups/{group_id}/candidates?q=
GET /api/imports/{import_id}/defect-instances/{instance_id}/rating-candidates
```

工作区响应直接包含：

- 构件组和当前解析目标；
- 组成员和病害解析实例；
- 评分树解析状态；
- 后端计算的进度、过滤标签和允许动作；
- 当前台账版本、评分树版本和各对象版本。

前端不得根据原始病害数组自行聚合绑定组或推导状态。

### 13.2 简单写操作

```http
PUT /api/imports/{import_id}/component-groups/{group_id}/resolution
PUT /api/imports/{import_id}/defect-instances/{instance_id}/rating-resolution
PUT /api/imports/{import_id}/defect-instances/{instance_id}/fact-overrides
```

单构件、多目标、标记缺失、清除、人工评分树选择和实例级修正提交：

- 编辑锁 token；
- `expected_version`；
- 用户选择的目标或动作；
- 当前客户端看到的台账或评分树版本。

后端重新验证所有业务约束，不能因为候选来自后端就信任客户端目标。

### 13.3 必须预览的写操作

```http
POST /api/imports/{import_id}/resolution-plans
POST /api/imports/{import_id}/resolution-plans/{plan_token}/apply
```

第一阶段下列操作必须先生成预览计划：

- 批量替换；
- 区间展开；
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

应用计划时，在同一事务内依次校验：

1. 计划存在、未过期且属于当前用户和导入记录；
2. 当前用户仍持有有效导入记录编辑锁；
3. 计划尚未被其他请求应用或作废；
4. 台账版本和评分树版本仍与计划一致；
5. 每个受影响构件组的 `version` 与计划一致；
6. 所有目标仍属于当前桥梁、当前台账版本和允许的构件类别；
7. 所有来源病害成员仍存在且仍属于计划中的构件组。

任一检查失败则整批不写入。成功时原子更新解析状态、实例、评分树结果、计划状态和审计事件。

重复提交已经成功应用的 token 时返回首次 `apply_result_json`。同一 token 不得再次执行状态变更。

## 15. 编辑锁和并发

第一阶段继续使用 `import_record_edit_locks`：同一导入记录同一时间只有一名编辑者。

关系表仍使用对象版本防止：

- 浏览器重复提交；
- 同一用户多个标签页覆盖；
- 预览后对象发生变化；
- 后台台账或评分树版本切换。

编辑锁解决业务所有权，`version` 解决陈旧写入，两者不能互相替代。

## 16. 前端职责

### 16.1 保留

- 工作区加载、搜索输入和过滤选择；
- 候选展示；
- 用户目标选择；
- 预览对话框；
- 加载、空状态和错误提示；
- 后端执行结果的局部状态替换。

### 16.2 删除

- TypeScript 构件编号归一化镜像；
- 前端替换表达式展开；
- 前端批量替换有效性判断；
- 前端区间拆分合法性和数量推演；
- 前端左右侧目标组合判断；
- 前端评分树失效判断；
- 绑定写操作后的父草稿整体失效和整页重载。

现有 `normalizeComponentNumber.ts`、`replacePattern.ts`、`replacePreview.ts` 及仅验证这些业务规则的前端测试应被删除。对应规则测试迁移到 C++ 后端。

### 16.3 组件边界

建议把当前大型工作区拆成：

| 组件 | 职责 |
| --- | --- |
| `ResolutionWorkspaceController` | 请求、局部缓存和命令编排 |
| `ResolutionProgressSummary` | 展示后端统计 |
| `ComponentGroupQueue` | 筛选和展示来源构件组 |
| `ComponentResolutionAction` | 单个、多目标、缺失和清除操作 |
| `RatingResolutionPanel` | 病害实例评分树候选与人工选择 |
| `ResolutionPlanDialog` | 展示批量或区间计划并执行 token |

组件不复制后端领域规则；允许动作和阻断原因由 DTO 明确提供。

## 17. 预检与正式确认

`PreflightReport` 和 `ConfirmPlan` 不再从病害 JSON 读取构件或评分树解析字段。

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
- `match_input_hash` 与当前输入一致。

`missing` 和 `unresolved` 的阻断或提示口径保持现有正式确认规则，不在本设计中降低约束。

## 18. 错误处理

| 错误码 | 含义 | 前端处理 |
| --- | --- | --- |
| `edit_lock_required` | 编辑锁不存在、过期或不属于当前用户 | 切换只读，提示重新获取编辑权 |
| `resolution_version_conflict` | 构件组或评分树解析版本过期 | 只刷新冲突对象 |
| `resolution_plan_expired` | 预览计划已过期 | 保留用户输入并重新生成预览 |
| `resolution_plan_invalidated` | 计划前提已变化 | 展示变化原因并重新预览 |
| `inventory_revision_changed` | 当前台账版本已变化 | 刷新工作区并重新选择 |
| `rating_tree_version_changed` | 当前评分树版本已变化 | 重新生成评分树匹配 |
| `target_not_allowed` | 目标不属于当前桥梁、版本或允许类别 | 展示后端目标级原因 |
| `resolution_has_blockers` | 批量计划包含阻断项 | 整批不应用，定位阻断行 |
| `draft_contains_resolution_fields` | 普通草稿夹带解析字段 | 提示客户端合同不兼容 |
| `database_unavailable` | 数据库操作失败 | 保留当前页面，不伪造成功状态 |

后端异常不能以数据库原始文本直接返回。所有批量写操作失败时必须完整回滚。

## 19. 测试策略

### 19.1 合同测试

- Python、JSON Schema、C++、TypeScript 均只接受合同 5.0；
- 5.0 拒绝所有已移出的构件和评分树字段；
- 来源指标身份字段继续存在且不被评分树重算修改；
- 4.0 被标记为需要重新解析。

### 19.2 数据库约束测试

- 来源病害在同一导入内只能属于一个构件组；
- 解析目标不能跨桥、跨台账版本；
- 同一组不能重复选择目标构件；
- 同一来源成员和目标只能生成一个实例；
- 每个实例最多一个当前评分树解析；
- 导入删除级联删除全部解析状态、计划和重开快照；
- 重开、放弃和重新确认能同时恢复或清理来源草稿与解析状态。

### 19.3 服务测试

- 精确匹配、确认别名、未匹配和歧义；
- 单构件、左右侧、多目标和区间展开；
- 标记缺失、清除和重新绑定；
- 构件版本变化使旧评分树结果失效；
- 病害类型、位置或描述变化使旧输入哈希失效；
- 尺寸和标度变化不重新选择评分树节点；
- 预览计划版本冻结、过期、作废和幂等重放；
- 批量计划任一阻断时零写入；
- 区间展开只让首个实例继承照片。

### 19.4 API 集成测试

- 工作区响应无需前端聚合即可展示；
- 候选查询只返回当前桥梁和版本允许的目标；
- 所有写接口验证编辑锁和对象版本；
- 写操作只返回受影响对象和最新统计；
- 绑定后不要求重新加载 `parsed_result`；
- 普通草稿保存不能修改解析表中的权威状态；
- 预检和确认从关系表读取构件与评分树解析。

### 19.5 前端测试

- 展示后端构件组、候选、状态和允许动作；
- 单个命令成功后只替换响应涉及的对象；
- 批量和区间计划按后端结果展示，不在前端重新计算；
- 版本冲突只刷新冲突对象；
- 计划过期可以保留输入并重新预览；
- 删除前端归一化、替换和区间业务规则测试。

### 19.6 端到端测试

对接口同步真实测试任务重新导入并执行：

```text
解析
-> 自动构件匹配
-> 人工构件解析
-> 区间/多目标展开
-> 评分树解析
-> 保存病害草稿
-> 预检
-> 正式确认
-> 构件病害档案查询
```

绑定和评分树操作前后计算 `parsed_result_json` 规范校验和，必须保持一致。普通病害事实编辑除外。

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
15. 全部数据库、C++、Python 和前端测试通过，接口同步真实测试任务完整走通。

## 21. 实施顺序约束

后续实施计划应按以下依赖顺序展开：

1. 合同 5.0 与数据库结构；
2. 导入初始化和关系表仓储；
3. `ImportResolutionService` 与工作区查询；
4. 单个构件/评分树命令；
5. 批量及区间预览计划；
6. 草稿同步、预检和正式确认改造；
7. 前端迁移并删除重复规则；
8. 重开快照、审计和端到端验收；
9. 经明确授权后重置测试数据并重新导入。

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

## 23. 变更记录

| 日期 | 变更 | 原因 |
| --- | --- | --- |
| 2026-08-27 | 创建设计 | 将构件解析和评分树解析从大 JSON 中拆出，并把匹配与预览统一收回后端 |
