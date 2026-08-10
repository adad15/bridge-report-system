# 病害照片确认状态简化设计

## 1. 背景

病害详情中的照片当前同时具有两层确认状态：

1. 照片候选通过 `match_status` 和 `review_status` 表示待校对、已确认、未关联等状态；
2. 病害通过 `group_review_status` 确认构件、病害、标度和照片关系。

一张已经挂在病害下的照片仍需单独点击“确认照片”，随后病害还要再点击“确认本组”。构件范围
拆分后，同一来源病害可能生成几十条病害记录，这套双重确认会把一次业务判断放大成几十次重复
点击。

正式表 `defect_photos` 也保存了 `match_status`，但该表的 `defect_observation_id` 非空。正式照片行
一旦存在，就已经表达“这张照片属于这条病害”；再保存“已确认”是重复事实，“未关联”则与表结构
本身矛盾。

本设计取消照片级人工确认状态，让病害组确认成为构件、病害、标度和照片关系的唯一确认入口。

## 2. 与既有设计的关系

本设计替代 `2026-07-31-defect-photo-actions-redesign-design.md` 中以下内容：

- “确认照片 / 撤销确认”动作；
- `PhotoCandidate.match_status` 与照片级 `review_status` 状态机；
- 正式照片 `defect_photos.match_status`；
- 照片必须先变为“已确认”才能随病害入库的规则。

既有设计中的添加照片、移除照片、人工上传、确认缺图、未归属照片清单和文件归档规则保持不变。
Word/源软件照片从病害移除后仍退回未归属区，人工上传照片仍按现有接口删除；本设计不新增也不
改变这两类操作。

## 3. 目标

1. 删除照片级确认与撤销确认，消除重复操作；
2. 以照片是否关联、文件是否存在和病害组是否确认表达完整业务状态；
3. 删除数据库、跨语言合同和代码中的冗余照片状态字段；
4. 保留 Word 照片引用的来源证据和真实异常校验；
5. 不改变现有照片添加、移除、上传和文件清理行为；
6. 在正式数据产生前完成破坏性清理，不留下兼容层和长期废字段。

## 4. 非目标

本期不做：

- 新增照片上传或删除能力；
- 改变 Word/源软件照片与人工上传照片现有的移除语义；
- 修改照片匹配算法或置信度阈值；
- 删除 `photo_references[].resolution`；
- 删除桥梁、构件或评定树的匹配状态与匹配方式；
- 实现构件范围拆分关系的批量核对；
- 为合同 3.0 或现有测试数据提供兼容读取和数据回填。

构件范围拆分的批量核对属于后续独立设计。本设计只移除其中重复的照片逐张确认环节。

## 5. 状态真源

照片关系由三个事实共同表达：

```text
照片文件是否存在
照片当前关联到哪条病害
该病害组是否已经确认
```

具体真源如下：

| 业务问题 | 真源 |
| --- | --- |
| 照片是否有关联 | `PhotoCandidate.linked_defect_candidate_id` |
| 照片属于哪条正式病害 | `defect_photos.defect_observation_id` |
| 照片文件是否可用 | `extracted_file.archive_relative_path` / `archived_file_id` |
| 当前病害关系是否通过人工确认 | `DefectCandidate.group_review_status` |
| Word 原文引用是否已找到、确认缺失或无关 | `photo_references[].resolution` |
| 自动关联的可信程度 | `PhotoCandidate.confidence` |

不再为照片保存独立的“待校对、已确认、已修改、已忽略”生命周期。

## 6. 合同模型

### 6.1 合同版本

删除必填字段属于破坏性修改，`BridgeAnnualInspectionData` 从 `3.0` 升级为 `4.0`。所有生产者、
消费者、JSON Schema、样例和测试一次性切换到 4.0，不同时支持两个版本。

### 6.2 照片候选

`PhotoCandidate` 删除：

```text
match_status
review_status
```

简化后的结构为：

```ts
interface PhotoCandidate {
  candidate_id: string;
  photo_number: string;
  linked_defect_candidate_id?: string | null;
  extracted_file: ExtractedPhotoFile;
  source_ref: SourceRef;
  confidence: number;
  warnings: WarningItem[];
}
```

同时删除仅服务于照片的 `PhotoMatchStatus` 类型。共享的 `ReviewStatus` 类型仍供病害使用，不能
整体删除。

### 6.3 保留的来源引用状态

`PhotoReference.resolution` 继续保留以下值：

```text
pending / matched / relinked / missing / unrelated
```

它描述 Word 原文中的照片编号如何被处理，属于来源证据和缺图完整性检查，不是照片人工确认
状态。`photo_candidate_id`、`resolved_defect_candidate_id` 和 `review_note` 的现有契约保持不变。

### 6.4 不受影响的同名或相邻字段

以下字段与照片级确认不是同一概念，必须保留：

- `BridgeCheck.match_status`；
- `DefectCandidate.review_status`；
- `DefectCandidate.group_review_status`；
- `component_match_method`；
- `rating_tree_match_method`；
- 照片 `confidence`、`source_ref` 和 `warnings`。

## 7. 导入与编辑数据流

### 7.1 导入

导入器沿用现有匹配算法。能够确定目标病害时写入 `linked_defect_candidate_id`；不能确定时写
`null`，照片进入未归属区。算法仍可写 `confidence` 和警告，但不再把判断转换为“高置信候选、
待校对、已确认、未关联”等枚举状态。

```text
确定目标病害   -> linked_defect_candidate_id = 目标病害
不能确定目标   -> linked_defect_candidate_id = null
```

源数据库导入、Word 导入和人工上传均输出相同的简化照片结构。

### 7.2 添加和移除

现有行为保持不变：

- 从未归属区选择照片或人工上传后，写入目标 `linked_defect_candidate_id`；
- Word/源软件照片从病害移除后清空关联并回到未归属区；
- 人工上传照片继续通过既有删除端点清理候选、数据库归档关系和文件；
- 添加或移除照片后，所属病害组回到待确认。

这些操作不再写 `match_status` 或照片级 `review_status`。

### 7.3 病害组确认

病害组确认一次性接受当前的构件、病害、标度和照片关系。前端不再先把照片批量改成“已确认”。

确认计划纳入照片的条件为：

```text
linked_defect_candidate_id 指向本次确认计划中的病害
+ 归档文件路径存在
= 写入正式 defect_photos
```

未归属照片不写入正式表，也不因为缺少照片级处理状态而阻断病害确认。Word 明确引用但尚未处理
的照片仍由 `photo_references[].resolution = pending` 阻断。

## 8. 界面设计

照片卡片删除：

- “确认照片”；
- “撤销确认”；
- “已确认、待校对”等照片状态文字。

照片卡片继续提供现有功能：

- 缩略图和大图查看；
- 添加照片；
- 删除或移除照片；
- Word 引用缺图的确认与撤销。

未归属照片面板不再拼接显示“系统判断：match_status / 我的处理：review_status”。它只展示照片
来源、编号、缩略图、警告及当前未归属事实。

页面不新增替代确认按钮。用户通过“确认本组”完成唯一一次确认。

## 9. 预检与错误处理

预检和问题列表删除以下规则：

- 照片 `match_status` 不是“已确认”；
- 照片 `review_status` 尚未结算；
- 自动关联照片需要逐张人工确认。

以下真实异常继续阻断确认：

1. Word 引用仍为 `pending`；
2. 引用标记为已匹配，但目标照片或目标病害不一致；
3. 已关联照片指向不存在的病害候选；
4. 已关联照片没有归档路径，或归档文件不存在；
5. 候选编号、照片编号或引用关系违反合同唯一性约束。

以下情况不阻断：

- 没有任何照片且 Word 也没有照片引用；
- 导入中存在未归属、且未被任何 Word 引用要求命中的照片；
- `confidence` 较低但照片已经关联；最终关系由病害组确认承担。

## 10. 正式数据库

`defect_photos` 删除：

```text
match_status
```

同时删除包含该列的索引 `ix_defect_photos_observation_number_status`。如仍需按病害和照片编号检索，
迁移中改建不含状态列的索引：

```sql
create index ... on defect_photos (defect_observation_id, photo_number);
```

正式照片表最终只保存照片事实：

- 主键和系统编号；
- 所属病害观测；
- 归档文件及来源文件/导入记录；
- 照片编号、标题和说明；
- 创建、修改时间。

由于当前均为测试数据，迁移直接删除列和旧索引，不回填、不保留兼容视图、不增加触发器。

## 11. 代码清理边界

实现需同步清理以下位置，避免形成只写不读的死字段：

- JSON Schema、TypeScript 合同、Pydantic 合同和 C++ 合同校验；
- Word、源数据库和人工上传的照片候选构造；
- 前端 `confirm_photo` action、reducer 分支、按钮和状态标签；
- 照片卡片派生模型中的 `confirmed` 状态；
- 问题列表中的 `photo_not_confirmed`；
- `ConfirmPlan` 和 `PreflightReport` 的照片状态门槛；
- `ReviewRepository` 的正式照片插入 SQL；
- `defect_photos.match_status` 数据库约束和索引；
- 样例 JSON、固定测试数据和历史断言。

不得借此删除或重写现有照片添加、移除、上传、缺图确认和文件清理代码。

## 12. 测试策略

### 12.1 合同与导入器

- 4.0 合同接受不含照片状态字段的候选；
- 4.0 合同拒绝遗留的照片 `match_status`、`review_status` 额外字段；
- Word 自动关联照片仍写正确的 `linked_defect_candidate_id`；
- 无法关联的 Word 照片进入未归属区；
- 源数据库和人工上传照片输出简化结构；
- `photo_references[].resolution` 的目标约束保持通过。

### 12.2 前端

- 照片卡片不出现确认、撤销确认和照片确认状态；
- 添加、移除、上传、缺图确认及撤销行为保持不变；
- 已关联照片不再产生 `photo_not_confirmed`；
- 未归属照片面板不依赖已删除字段；
- 确认本组不再批量改写照片状态；
- 全量 Vitest、TypeScript 编译和 Vite 构建通过。

### 12.3 C++ 与数据库

- 预检不因照片缺少确认状态报错；
- 已关联且文件存在的照片进入确认计划；
- 未归属、文件缺失或关联目标无效的照片按新规则处理；
- 正式照片插入不再写 `match_status`；
- 数据库迁移可连续应用，schema smoke 不再要求该列；
- 全量 C++、PostgreSQL 集成测试通过。

## 13. 验收标准

1. 页面不再显示或操作照片级确认状态；
2. 现有添加和删除/移除照片功能行为不变；
3. 病害组确认成为照片关系的唯一人工确认入口；
4. 已关联且文件存在的照片能随病害一次确认入库；
5. 未归属照片不写入正式照片表；
6. Word 引用待处理、引用缺图和归档文件缺失仍能被准确识别；
7. `PhotoCandidate` 不再含 `match_status` 和 `review_status`；
8. PostgreSQL `defect_photos` 不再含 `match_status`；
9. 所有合同生产者和消费者统一使用 `BridgeAnnualInspectionData 4.0`；
10. 前端、Python、C++、数据库迁移和完整测试全部通过。

## 14. 已知取舍

1. 自动关联后不再要求逐张确认。错误关系通过病害组整体检查以及添加/移除照片修正，这是减少
   重复操作的核心取舍；
2. `confidence` 保留为来源证据，但不决定正式照片是否入库；
3. 未被 Word 引用的未归属照片可以留在导入记录中而不阻断确认；
4. 合同 4.0 不兼容 3.0 草稿。当前只有测试数据，因此接受直接清理和重新导入；
5. 构件范围拆分仍需单独的批量核对设计，本期不会自动清除
   `component_range_split_review_required`。
