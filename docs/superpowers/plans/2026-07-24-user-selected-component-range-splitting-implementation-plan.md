# 构件绑定 · 用户选择式范围拆分 实施计划

> **For agentic workers:** 逐任务实施；每步 `- [ ]` 勾选。先写失败测试 → 跑到失败 → 最小实现 → 跑到通过 → 只提交本任务相关文件。工作区已有其他未提交修改，任何任务都不得覆盖、清理或顺带提交它们。

**Goal:** 实现 spec [2026-07-24-user-selected-component-range-splitting-design.md](../specs/2026-07-24-user-selected-component-range-splitting-design.md)：Word 解析保持不变；用户在构件绑定界面多选安全范围行，经过后端预览和影响令牌确认后，原子地把范围病害拆成独立构件病害、复制照片候选、自动绑定唯一台账构件，并保留永久来源和临时人工检查标记。

**Architecture:** 新增纯 C++ 范围解析器和纯 JSON 拆分规划器，把语法、计数、ID 重写与数据库事务分离。新增独立 `ComponentRangeSplitRepository` 负责预览、影响令牌和原子应用；现有 `ImportBindingRepository` 仅扩展 overview 行的拆分资格，不承载拆分细节。前端不解析范围，只渲染后端资格和预览。候选合同新增可选 `range_split_origin`；正式入库把该元数据写入既有 `source_raw_cells_json`，不新增数据库列。

**Tech Stack:** Python 3 / Pydantic / pytest；C++20 / Drogon / JsonCpp / GoogleTest / PostgreSQL；React 18 / TypeScript / Vitest；现有 H21 系统评定服务。

**真值来源：** spec §4–§15。范围语法以 C++ 后端实现为唯一真值；前端不得镜像解析器。

**实施前工作区约束：**

当前工作区已有与本功能无关的未提交修改，至少涉及：

- `backend-cpp/include/bridge_report/db/ComponentInventoryRepository.hpp`
- `backend-cpp/include/bridge_report/inventory/ComponentInventoryGenerator.hpp`
- `backend-cpp/src/review/ConfirmPlan.cpp`
- `contracts/bridge_annual_inspection_data.schema.json`
- `frontend/src/pages/ReviewWorkspacePage.tsx`
- `frontend/src/pages/ReviewWorkspacePage.test.tsx`
- `frontend/src/styles.css`

实施时允许在确属本功能需要的重叠文件中做最小合并，但每次编辑前后必须检查 diff，保留原修改。禁止 `git reset --hard`、`git checkout --` 或整文件覆盖。

---

## 文件结构

### 新增

- `backend-cpp/include/bridge_report/inventory/ComponentRangeParser.hpp`
- `backend-cpp/src/inventory/ComponentRangeParser.cpp`
- `backend-cpp/include/bridge_report/review/ComponentRangeSplitPlanner.hpp`
- `backend-cpp/src/review/ComponentRangeSplitPlanner.cpp`
- `backend-cpp/include/bridge_report/db/ComponentRangeSplitRepository.hpp`
- `backend-cpp/src/db/ComponentRangeSplitRepository.cpp`
- `backend-cpp/tests/test_component_range_parser.cpp`
- `backend-cpp/tests/test_component_range_split_planner.cpp`
- `backend-cpp/tests/test_component_range_split_repository.cpp`
- `frontend/src/review/binding/ComponentRangeSplitDialog.tsx`
- `frontend/src/review/binding/ComponentRangeSplitDialog.test.tsx`

### 修改

- `tools-python/bridge_report_tools/contracts/annual_inspection.py`
- `tools-python/tests/test_annual_inspection_contract.py`
- `contracts/bridge_annual_inspection_data.schema.json`（由 Python 导出器生成；保留现有未提交合同修改）
- `backend-cpp/src/contracts/AnnualInspectionContract.cpp`
- `backend-cpp/tests/test_annual_inspection_contract.cpp`
- `frontend/src/contracts/annualInspection.ts`
- `frontend/src/contracts/annualInspection.test.ts`
- `backend-cpp/include/bridge_report/db/ImportBindingRepository.hpp`
- `backend-cpp/src/db/ImportBindingRepository.cpp`
- `backend-cpp/src/http/ImportBindingRoutes.cpp`
- `backend-cpp/CMakeLists.txt`
- `frontend/src/api/importBindingApi.ts`
- `frontend/src/api/importBindingApi.test.ts`
- `frontend/src/review/binding/ComponentBindingWorkspace.tsx`
- `frontend/src/review/binding/ComponentBindingWorkspace.test.tsx`
- `frontend/src/review/reviewDraft.ts`
- `frontend/src/review/reviewDraft.test.ts`
- `frontend/src/review/components/DefectPhotoGroup.tsx`
- `frontend/src/review/components/DefectPhotoGroup.test.tsx`
- `frontend/src/styles.css`（保留现有布局修复）
- `backend-cpp/include/bridge_report/review/ConfirmPlan.hpp`
- `backend-cpp/src/review/ConfirmPlan.cpp`（保留现有未提交修改）
- `backend-cpp/src/db/ReviewRepository.cpp`
- `backend-cpp/src/db/ComponentArchiveRepository.cpp`
- `frontend/src/api/componentArchiveApi.ts`
- `frontend/src/archive/ObservationYearRow.tsx`

---

## 决策点（实施时不得改变）

1. Word 解析不拆分、不增加警告、不改变真实 Word 回归数量。
2. 只有 `unmatched` / `ambiguous` 行可拆；`bound` / `missing` 行不显示选择框且后端再次拒绝。
3. 支持 `~`、`～`；两端必须同固定前缀、同固定后缀，只展开末位整数。
4. 单范围最多 500 个构件；一次应用最多生成 2000 条病害。
5. 一条绑定行引用的所有病害一起拆分。
6. 每条新病害复制当前候选中的文字、测量、标度、备注、来源和原警告；不读取 Word 扣分。
7. 照片候选按新病害复制，归档路径复用，不复制物理文件。
8. 唯一同类别台账编号自动绑定；零命中为未匹配；多命中为歧义。
9. 预览由后端计算；应用必须用影响令牌重新计算并原子提交。
10. 临时警告在联合确认时移除；永久 `range_split_origin` 不删除，并进入正式事实来源证据。

---

## Task 0：基线与重叠文件保护

- [ ] 记录 `git status --short` 和本功能将触及的重叠文件 diff：
  - `contracts/bridge_annual_inspection_data.schema.json`
  - `backend-cpp/src/review/ConfirmPlan.cpp`
  - `frontend/src/styles.css`
- [ ] 运行当前相关基线：
  - `uv run pytest tools-python/tests/test_annual_inspection_contract.py tools-python/tests/importers/test_real_word_regression.py`
  - `npm run test -- --run src/contracts/annualInspection.test.ts src/review/binding/ComponentBindingWorkspace.test.tsx src/review/reviewDraft.test.ts`
  - 现有后端 Debug 测试中与合同、绑定、确认计划相关的测试
- [ ] 将现有失败与本功能引入的失败区分记录；不得用修改测试期望掩盖基线失败
- [ ] 本任务不改代码、不提交

## Task 1：C++ 安全范围解析器

### 测试

- [ ] 新增 `test_component_range_parser.cpp`，先写失败测试：
  - `1-1#板~1-25#板` → 25 个编号
  - `2-1#板～2-25#板` → 25 个编号
  - `3-2#铰缝 ~ 3-5#铰缝` → 4 个编号
  - 输出沿用起点模板格式
  - 前缀不同拒绝：`1-1#板~2-25#板`
  - 后缀不同拒绝：`1-1#板~1-25#梁`
  - 倒序拒绝
  - 多个分隔符拒绝
  - 端点没有末位整数拒绝
  - 起止相同、展开仅 1 个编号拒绝
  - 501 个编号拒绝并返回 `limit_exceeded`
  - 全角编号符号、破折号和空白比较沿用 `normalize_component_number`
- [ ] 将测试加入 `backend-cpp/CMakeLists.txt`
- [ ] 编译并跑到失败

### 实现

- [ ] 定义纯类型：
  - `ComponentRangeParseStatus`
  - `ComponentRangeExpansion { source, first, last, numbers }`
  - `parse_component_range(text, max_count=500)`
- [ ] 解析器不访问数据库、不抛业务异常；失败返回稳定状态和可显示原因
- [ ] 输出编号不得调用会抹掉展示格式的归一化函数；归一化只用于两端同构校验
- [ ] 跑单测通过
- [ ] 提交：`feat(binding): parse safe component number ranges`

## Task 2：四端候选合同加入永久拆分来源

### Python / JSON Schema

- [ ] 在 `annual_inspection.py` 新增严格模型 `RangeSplitOrigin`：
  - `operation_id`
  - `source_candidate_id`
  - `source_component_number`
  - `expanded_component_number`
  - `split_index`（从 1 开始）
  - `split_count`
  - `operated_by_user_id`
  - `operated_at`
- [ ] `DefectCandidate.range_split_origin` 为可选字段，默认 `None`
- [ ] 失败测试：
  - 完整对象可解析
  - 历史对象缺字段仍可解析
  - `split_index < 1`、`split_count < split_index`、空编号、额外字段拒绝
- [ ] 用 `python -m bridge_report_tools.contracts.export_schema` 重新生成 JSON Schema；生成前保存现有 schema diff，生成后人工合并，确保不丢失用户已有合同修改

### C++ / TypeScript

- [ ] C++ 合同校验失败测试：合法、缺省、字段类型错误、索引越界、额外字段
- [ ] 实现 C++ 可选对象校验
- [ ] TypeScript 增加 `RangeSplitOrigin` 和可选字段
- [ ] TypeScript 合同测试覆盖有/无来源
- [ ] 四端字段名和可空性逐项比对
- [ ] Python、C++、前端合同测试通过
- [ ] 提交：`feat(contract): describe component range split origin`

## Task 3：纯 JSON 拆分规划器

### 边界

`ComponentRangeSplitPlanner` 只接收：

- 当前候选 JSON；
- 已确认台账修订版；
- 规范化后的选择集合；
- 安全上限。

它不提交数据库、不认证用户、不生成操作时间。预览与应用都调用同一规划器，避免两套计数逻辑。

### 测试

- [ ] 新增失败测试：
  - 一条范围引用 1 条病害，展开 25 条
  - 一条范围引用 3 条病害，展开 75 条
  - 多个选择按 `(part_name, normalized_component_number)` 稳定排序
  - 原病害字段完整复制，`component_number`、绑定字段、审核状态按 spec 改写
  - 原对象警告保留且目标 ID 改写
  - 顶层定向 warnings/errors 为每个新病害复制，不留旧 ID
  - 多照片候选按每条新病害复制
  - 归档路径复用
  - 缺照片候选时只保留 `photo_numbers`
  - 候选 ID 在本次 JSON 内唯一且稳定生成
  - 唯一台账匹配自动绑定
  - 零命中未匹配
  - 多命中写候选 ID 并判歧义
  - 类别不符不得自动绑定
  - 原行已 bound/missing 拒绝
  - 结果病害超过 2000 拒绝
- [ ] 明确预览计数按病害候选而非构件编号计

### 实现

- [ ] 定义：
  - `ComponentRangeSplitTarget`
  - `ComponentRangeSplitItem`
  - `ComponentRangeSplitTotals`
  - `ComponentRangeSplitPlan`
  - 稳定错误状态
- [ ] 规划阶段生成确定性的“新候选占位 ID 映射”；应用阶段只替换操作 ID、用户和时间，不重新选择范围或匹配结果
- [ ] 新病害增加 `component_range_split_review_required`
- [ ] `range_split_origin` 写入所有必需字段
- [ ] 照片与顶层警告引用重写在一个纯函数中完成
- [ ] 单测通过
- [ ] 提交：`feat(binding): plan component range splits`

## Task 4：后端预览仓储与影响令牌

### 仓储隔离

- [ ] 新增 `ComponentRangeSplitRepository`，不要继续扩大 `ImportBindingRepository.cpp`
- [ ] 复用现有删除预览的 canonical JSON + `auth::sha256_hex` 模式生成影响令牌
- [ ] 令牌输入必须包含：
  - import ID
  - `parsed_result_json` 稳定摘要
  - 台账修订版 ID
  - 排序后的选择
  - 规划结果稳定摘要

### 测试

- [ ] DB 测试建立待校对导入和已确认台账
- [ ] `preview` 返回逐项计数、汇总和 `sha256:` 影响令牌
- [ ] 相同输入令牌稳定
- [ ] 候选 JSON、选择或台账修订版任一变化使令牌变化
- [ ] 无导入返回 NotFound
- [ ] 非待校对、无已确认台账返回 Conflict
- [ ] 非法范围、已处理行和超限返回具体状态及范围

### 实现

- [ ] `preview(import_id, targets)` 读取当前 JSON 和台账，调用纯规划器
- [ ] preview 不写数据库
- [ ] overview 的 `BindingRow` 增加后端计算字段：
  - `split_eligible`
  - 可选 `split_expanded_count`
- [ ] 只有 pending 状态且解析成功、未超限时 eligible
- [ ] 单测通过
- [ ] 提交：`feat(binding): preview component range splits`

## Task 5：后端原子应用与路由

### 事务测试

- [ ] `apply` 用 `for update` 锁定导入记录
- [ ] 正确令牌：一次更新完整 JSON并返回 overview + summary
- [ ] 错误令牌：`component_range_split_stale`，零写入
- [ ] 预览后原病害变化：零写入
- [ ] 预览后台账修订变化：零写入
- [ ] 多目标中任一失效：整批零写入
- [ ] 操作人、操作时间和 operation ID 写入每条新病害
- [ ] 重放同一请求：第二次因原范围不存在而拒绝，不重复拆分
- [ ] 提交失败/CommitLatch 失败返回 Failed

### 路由

- [ ] 注册 OPTIONS：
  - `.../split-preview`
  - `.../split-apply`
- [ ] 两个接口都要求认证
- [ ] 路由只解析 DTO；范围、状态和令牌校验留在仓储/规划器
- [ ] preview 请求：非空 targets
- [ ] apply 请求：非空 targets + 非空 impact_token
- [ ] 把稳定状态映射为 spec §5.5 错误码，并在 details 写首个失败范围
- [ ] apply 从认证结果取得用户 ID；时间由后端 UTC 时钟产生
- [ ] 成功日志记录操作 ID、计数和选择，不记录令牌或文件内容
- [ ] 路由与仓储测试通过
- [ ] 提交：`feat(binding): apply component range splits atomically`

## Task 6：前端 API、选择状态与预览对话框

### API

- [ ] `importBindingApi.ts` 类型增加：
  - overview 行拆分资格
  - preview item/totals/response
  - apply response
- [ ] 新增 `previewComponentRangeSplit`、`applyComponentRangeSplit`
- [ ] API 测试断言 URL、方法、请求体和错误解析

### 工作区

- [ ] 失败测试：
  - eligible pending 行显示复选框
  - 非 eligible、bound、missing 不显示复选框
  - 可多选不同分组行
  - 页头按钮显示选择数量
  - overview 更新后清理失效选择
  - 筛选隐藏选择时按 spec 清理
  - 点击按钮用准确 targets 请求后端预览
- [ ] 用稳定 key `(part_name, component_number)` 保存选择
- [ ] 不在前端解析范围或计算拆分数量

### 对话框

- [ ] 新增失败测试：
  - 逐项展示原范围、展开数量、病害数、照片数和三态匹配计数
  - 汇总准确
  - 固定展示评分影响警告
  - busy 时禁用取消与确认
  - apply 发送原 targets + preview impact token
  - stale/冲突显示具体范围且保留对话框
  - 成功关闭并上报新 overview
- [ ] 对话框复用 `.dialog-backdrop` / `.workspace-dialog` / `.data-table`
- [ ] 表格限高滚动；大量预览不撑穿视口
- [ ] 组件和 API 测试通过
- [ ] 提交：`feat(binding): select and preview component range splits`

## Task 7：临时人工检查与永久校对标识

### 联合确认

- [ ] `reviewDraft.test.ts` 失败测试：
  - `confirm_defect_group` 只移除 `component_range_split_review_required`
  - 其他 warning 不被误删
  - `range_split_origin` 保留
  - 未满足照片联合确认条件时不得清除警告
- [ ] 修改 reducer：确认成功时过滤临时拆分警告
- [ ] 后端 DraftValidation 允许该预期 warning 变化，但不得允许客户端修改 `range_split_origin`

### UI

- [ ] 病害卡片待确认时显示“范围拆分 · 待核对”
- [ ] 联合确认后显示“由范围拆分”
- [ ] 展示原范围编号，提供 title/辅助文本，不只靠颜色表达
- [ ] “需要处理”继续复用 warning 自动入列，不新增第二套派生逻辑
- [ ] 相关前端和后端草稿校验测试通过
- [ ] 提交：`feat(review): mark range-split defects for confirmation`

## Task 8：正式入库与档案追溯

### ConfirmPlan / ReviewRepository

- [ ] `DefectPlan` 增加可选 `range_split_origin`
- [ ] ConfirmPlan 测试：
  - 有来源时完整映射
  - 无来源时保持现状
  - 临时拆分警告未清除或 group 未确认时 preflight 仍阻止入库
- [ ] 扩展 `build_raw_cells_json`，同时写：
  - 现有 raw row text
  - 可选 `range_split_origin`
- [ ] 注意合并 `ConfirmPlan.cpp` 现有用户修改，禁止整文件重写

### 档案

- [ ] ComponentArchiveRepository 证据响应保留 `source_raw_cells`
- [ ] 前端为 `source_raw_cells.range_split_origin` 增加结构化展示：
  - `由 {source_component_number} 拆分`
  - 操作时间
  - 不显示内部用户 UUID 为主文案；若现有接口没有显示名，只展示范围和时间
- [ ] 保留原始 JSON 证据展开能力
- [ ] 正式入库与档案测试通过
- [ ] 提交：`feat(archive): preserve component range split provenance`

## Task 9：评分语义回归

- [ ] AssessmentService 测试：
  - 两条拆分病害绑定不同 component ID、同 indicator、同标度 → 两个构件分别有扣分
  - 同 component ID、同 indicator 的多条病害仍只取最高标度
  - `range_split_origin` 不进入评分输入摘要
  - Word `defect_deduction` 仍被合同拒绝
- [ ] H21 evaluator 既有 component/part/bridge 测试不改算法、不改期望
- [ ] 真实 Word 未执行拆分时解析与评定输入完全不变
- [ ] 提交：`test(assessment): cover range-split defect scoring`

## Task 10：全量验证与真实数据验收

### 自动验证

- [ ] Python：
  - `uv run pytest`
- [ ] 前端：
  - `npm run test -- --run`
  - `npm run build`
- [ ] C++：
  - `cmake --build --preset vs-debug`
  - 运行 Debug 后端全量测试（使用项目现有 DB 门控脚本/环境）
- [ ] `git diff --check`
- [ ] 检查 `git status --short`，确认用户原有未提交修改仍在且未被误提交

### 本地服务

- [ ] 重新编译并重启 C++ 后端；不能只依赖源文件存在
- [ ] OPTIONS 探测两个新接口必须返回 CORS 允许，不得 404
- [ ] 前端 dev server 加载最新代码

### 真实数据

- [ ] 使用现有百股大桥待校对导入，选择：
  - `1-1#板~1-25#板`
  - `2-1#板~2-25#板`
- [ ] 核对预览计数与数据库只读查询一致
- [ ] 应用前记录候选 JSON 摘要；应用后确认只有选中范围被拆
- [ ] 核对：
  - 未选范围保持原样
  - 每条文字、测量、标度不变
  - 照片候选数量与预览一致
  - 唯一构件自动绑定
  - “需要处理”出现拆分警告
  - 联合确认后临时警告消失
  - 试算中各实际构件分别参与评分
- [ ] 如真实数据操作会改变用户业务数据，执行前必须再次获得用户对该具体导入记录的授权；无授权时仅用测试夹具验收

### 收尾

- [ ] 更新模块 03/05 规格的实施状态与变更记录
- [ ] 更新 `PROJECT_CONTEXT.md` 的当前进度
- [ ] 最终提交仅包含本功能文件；不夹带工作区其他修改
- [ ] 提交：`feat(binding): split selected component ranges`

---

## 完成定义

只有同时满足以下条件才算完成：

1. Word 解析基线未变化。
2. 只有用户选择的 eligible 范围被拆。
3. 后端预览与原子应用使用同一规划器。
4. 病害、照片、警告和引用不存在悬空 ID。
5. 唯一台账构件自动绑定，零/多命中状态正确。
6. 临时人工检查和永久来源追溯均可见。
7. 正式入库后档案仍可追溯原范围。
8. 评分器对不同实际构件分别评定，既有聚合规则未变。
9. 单范围 500、单次 2000 的限制前后端一致。
10. Python、C++、前端全量测试和生产构建通过。
11. 新路由在实际运行的后端中生效。
12. 用户原有未提交修改完整保留。
