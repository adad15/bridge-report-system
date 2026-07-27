# “需要处理”提示精确去重设计

## 背景

校对工作台会把草稿派生出的 warning 与系统评定返回的问题合并到“需要处理”列表。当前记录中，同一病害的无效标度会同时显示：

- `defect_scale_invalid`：Word 解析阶段产生的 warning；
- `assessment_defect_scale_required`：系统评定阶段产生的问题，前端统一按 error 展示。

构件范围拆分后的病害还会同时显示专用的拆分核对提示和通用的联合确认提示，信息存在部分重复。

## 目标

减少同一处理动作对应的重复提示，同时保留不同字段、不同原因和不同处理动作的问题。

## 方案比较

### 方案一：按病害和字段通用去重

同一病害、同一字段同时存在 error 与 warning 时只保留 error。

优点是规则简洁；缺点是未来同一字段可能出现多个原因不同的问题，通用去重可能误吞有效提示。

### 方案二：按已知代码组合精确去重（采用）

只处理已经确认语义相同的代码组合，并为拆分病害应用一条明确的提示优先规则。

优点是边界清晰、不会影响未知提示；缺点是新增重复组合时需要显式补充规则。

### 方案三：修改各生成端，不再产生重复提示

分别调整 Word 解析、系统评定和拆分逻辑。

该方案会改变持久化数据和后端职责，影响范围大于当前展示问题，不适合本次修复。

## 详细规则

### 标度提示

当且仅当以下条件同时满足时，隐藏 warning：

1. warning 代码为 `defect_scale_invalid`；
2. error 代码为 `assessment_defect_scale_required`；
3. 两者指向同一个病害候选。

保留 `assessment_defect_scale_required` error。其他同病害或同字段提示均不受影响。

系统评定结果尚未加载、重新试算后不再包含该 error，或者 error 指向其他病害时，原 warning 继续显示。

### 拆分提示

当同一病害包含 `component_range_split_review_required` warning 且其 `group_review_status` 为 `待确认` 时：

- 保留 `component_range_split_review_required`；
- 不再派生通用的 `defect_group_pending`。

未拆分病害仍按原规则显示 `defect_group_pending`。拆分核对完成并清除专用 warning 后，若联合确认状态仍为 `待确认`，通用提示恢复显示。

## 实现位置

- `frontend/src/review/grouping.ts`：负责草稿 warning 与派生提示，实施拆分提示优先规则。
- `frontend/src/pages/ReviewWorkspacePage.tsx`：在合并草稿提示和系统评定问题时实施标度代码对精确去重。
- 不修改 Python Word 解析结果、不修改 C++ 系统评定结果、不回写数据库。

## 测试与验收

增加回归测试覆盖：

1. 同一病害同时命中两个标度代码时，只展示 assessment error；
2. 两个代码指向不同病害时均保留；
3. 只有解析 warning、尚无 assessment error 时保留 warning；
4. 拆分病害只显示拆分核对提示；
5. 非拆分病害仍显示联合确认提示；
6. 拆分专用 warning 清除后，待确认病害恢复通用联合确认提示。

现有“需要处理”分类、系统评定计算、Word 解析和持久化数据保持不变。
