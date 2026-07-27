# 病害与照片两阶段校对实施计划

> **For agentic workers:** 逐任务实施；每步 `- [ ]` 勾选。严格执行“先写失败测试 → 跑到失败 → 最小实现 → 跑到通过 → 只提交本任务相关文件”。工作区已有其他未提交修改，任何任务都不得覆盖、清理或顺带提交它们。

**Goal:** 实现 [2026-07-27-defect-photo-two-stage-review-design.md](../specs/2026-07-27-defect-photo-two-stage-review-design.md)：把“病害与照片”改造成“快速校对视觉列表 + 详情维护”的两阶段工作区，安全项可自动预选并批量确认，异常项进入规范病害、正式字段和照片关系的精细维护。

**Architecture:** 候选 JSON 继续作为待校对真值，但增加不可编辑的 Word 来源快照、稳定规范病害指标 ID 和逐条照片引用处理结果。后端锁定的技术评定规范包与 C++ 指标解析器是规范适用性和标度校验的权威；前端只使用后端返回的规范上下文和目录做交互预判。前端以纯派生模型统一计算问题分类、批量资格和确认原因，reducer 只执行明确业务动作，不再接受任意状态赋值。正式入库把规范指标 ID写入病害观测列，并把来源快照与照片引用处理结果写入来源证据。

**Tech Stack:** Python 3 / Pydantic / pytest；JSON Schema；C++20 / Drogon / JsonCpp / GoogleTest / PostgreSQL；React 18 / TypeScript / Vitest。

**真值来源：**

- 页面和状态语义：设计规格 §5–§9；
- 规范病害适用性、允许标度：检测年度锁定的技术评定规范包；
- 正式照片归属：`photos[].linked_defect_candidate_id`；
- Word 照片引用：`defects[].source_snapshot.photo_numbers`；
- 引用处理结论：`defects[].photo_reference_reviews`；
- 正式系统评分：C++ `AssessmentService`。

## 实施前工作区约束

当前工作区已有与本功能无关的未提交修改，至少涉及：

- `backend-cpp/include/bridge_report/db/ComponentInventoryRepository.hpp`
- `backend-cpp/include/bridge_report/inventory/ComponentInventoryGenerator.hpp`
- `frontend/src/pages/ReviewWorkspacePage.tsx`
- `frontend/src/pages/ReviewWorkspacePage.test.tsx`
- `frontend/src/styles.css`
- 多份模块 03 图和规格
- `samples/scoring/component_score_cases.json`
- `.claude/`
- `scripts/dev/start-all.ps1`

其中 `ReviewWorkspacePage.tsx`、对应测试和 `styles.css` 与本功能存在必要重叠。实施每个相关任务前后必须记录局部 diff，做最小合并；禁止整文件覆盖，禁止 `git reset --hard`、`git checkout --` 或清理用户文件。

## 合同新增字段

`DefectCandidate` 增加下列可选字段，保持合同版本 `2.0`，避免把已解析的 2.0 草稿整体强制重解析：

```json
{
  "source_snapshot": {
    "snapshot_origin": "word_import",
    "structure_part": "superstructure",
    "component_name": "上部承重构件",
    "component_number": "1-1#板",
    "defect_type": "/",
    "defect_location": "板底",
    "defect_description": "板底/",
    "defect_scale": 2,
    "quantity_text": null,
    "measurement_text": "存在条形裂缝",
    "photo_numbers": ["2.1-1"]
  },
  "standard_defect_indicator_id": "h21.defect.5_1_1_11",
  "photo_reference_reviews": [
    {
      "photo_number": "2.1-1",
      "resolution": "matched",
      "photo_candidate_id": "photo_0012",
      "resolved_defect_candidate_id": "defect_0001",
      "review_note": null
    }
  ]
}
```

约束：

1. `source_snapshot` 对 Word 导入病害存在，对人工新增病害为 `null`；`snapshot_origin` 为
   `word_import` 或 `compatibility_snapshot`；保存草稿时后端禁止客户端修改；
2. `standard_defect_indicator_id` 可为 `null`，但非忽略病害确认前必须存在且适用于实际构件类别；
3. `photo_reference_reviews` 只处理 `source_snapshot.photo_numbers` 中的编号，同一编号只能有一个结论；
4. `matched` 必须指向当前病害且实际照片关系一致；
5. `relinked` 必须指向其他未忽略病害且实际照片关系一致；
6. `missing` 不得带照片候选；
7. `unrelated` 必须指向已确认未关联/已忽略的照片候选；
8. 现有 `photo_numbers`、`confirmed_missing_photo_numbers` 在 2.0 兼容期保留为只读镜像，界面不再编辑，后端保存前从新字段同步，防止双真值。

## 文件结构

### 新增

- `frontend/src/review/defectPhotoReviewModel.ts`
- `frontend/src/review/defectPhotoReviewModel.test.ts`
- `frontend/src/review/components/DefectReviewToolbar.tsx`
- `frontend/src/review/components/DefectReviewToolbar.test.tsx`
- `frontend/src/review/components/DefectQuickReviewList.tsx`
- `frontend/src/review/components/DefectQuickReviewList.test.tsx`
- `frontend/src/review/components/DefectBatchConfirmDialog.tsx`
- `frontend/src/review/components/DefectBatchConfirmDialog.test.tsx`
- `frontend/src/review/components/DefectDetailEditor.tsx`
- `frontend/src/review/components/DefectDetailEditor.test.tsx`
- `frontend/src/review/components/PhotoRelationEditor.tsx`
- `frontend/src/review/components/PhotoRelationEditor.test.tsx`
- `backend-cpp/include/bridge_report/standards/DefectIndicatorResolver.hpp`
- `backend-cpp/src/standards/DefectIndicatorResolver.cpp`
- `backend-cpp/tests/test_defect_indicator_resolver.cpp`
- `database/migrations/017_defect_standard_indicator.sql`
- `database/tests/017_defect_standard_indicator_smoke.sql`

### 修改

- `tools-python/bridge_report_tools/contracts/annual_inspection.py`
- `tools-python/bridge_report_tools/importers/defect_tables.py`
- `tools-python/tests/test_annual_inspection_contract.py`
- `tools-python/tests/importers/test_defect_tables.py`
- `tools-python/tests/importers/test_real_word_regression.py`
- `contracts/bridge_annual_inspection_data.schema.json`
- `samples/contracts/bridge_annual_inspection_data.v2.valid.json`
- `backend-cpp/src/contracts/AnnualInspectionContract.cpp`
- `backend-cpp/tests/test_annual_inspection_contract.cpp`
- `backend-cpp/src/review/ContractCompatibility.cpp`
- `backend-cpp/include/bridge_report/review/ContractCompatibility.hpp`
- `backend-cpp/tests/test_contract_compatibility.cpp`
- `backend-cpp/src/review/ComponentRangeSplitPlanner.cpp`
- `backend-cpp/tests/test_component_range_split_planner.cpp`
- `backend-cpp/src/review/DraftValidation.cpp`
- `backend-cpp/include/bridge_report/review/DraftValidation.hpp`
- `backend-cpp/tests/test_draft_validation.cpp`
- `frontend/src/contracts/annualInspection.ts`
- `frontend/src/contracts/annualInspection.test.ts`
- `frontend/src/api/standardsApi.ts`
- `frontend/src/api/standardsApi.test.ts`
- `backend-cpp/include/bridge_report/review/ReviewModels.hpp`
- `backend-cpp/src/review/ReviewModels.cpp`
- `backend-cpp/tests/test_review_models.cpp`
- `backend-cpp/src/db/ReviewRepository.cpp`
- `backend-cpp/src/http/ReviewRoutes.cpp`
- `backend-cpp/include/bridge_report/http/ReviewRoutes.hpp`
- `backend-cpp/src/main.cpp`
- `frontend/src/api/reviewApi.ts`
- `frontend/src/api/reviewApi.test.ts`
- `backend-cpp/src/assessment/AssessmentService.cpp`
- `backend-cpp/tests/test_assessment_service.cpp`
- `backend-cpp/CMakeLists.txt`
- `frontend/src/review/reviewDraft.ts`
- `frontend/src/review/reviewDraft.test.ts`
- `frontend/src/review/defectPhotoGroups.ts`
- `frontend/src/review/defectPhotoGroups.test.ts`
- `frontend/src/review/components/DefectsSection.tsx`
- `frontend/src/review/components/DefectsSection.test.tsx`
- `frontend/src/review/components/DefectPhotoGroup.tsx`
- `frontend/src/review/components/DefectPhotoGroup.test.tsx`
- `frontend/src/review/components/UnlinkedPhotosPanel.tsx`
- `frontend/src/pages/ReviewWorkspacePage.tsx`
- `frontend/src/pages/ReviewWorkspacePage.test.tsx`
- `frontend/src/styles.css`
- `backend-cpp/include/bridge_report/review/ConfirmPlan.hpp`
- `backend-cpp/src/review/ConfirmPlan.cpp`
- `backend-cpp/tests/test_confirm_plan.cpp`
- `backend-cpp/src/db/ComponentArchiveRepository.cpp`
- `backend-cpp/tests/test_component_archive_repository.cpp`
- `frontend/src/api/componentArchiveApi.ts`
- `frontend/src/archive/ObservationYearRow.tsx`
- `scripts/dev/check-database.ps1`（脚本已按文件名自动发现迁移和冒烟，仅在验证发现需要说明时修改）
- `docs/superpowers/specs/modules/05-review-workspace.md`
- `PROJECT_CONTEXT.md`

## 不得改变的决策

1. Word 解析表格规则、病害数量和照片提取规则不变。
2. Word 来源字段只读；正式字段可编辑。
3. 规范病害使用稳定指标 ID，名称只作显示与兼容匹配。
4. 前端不得自行复制 C++ 评分算法。
5. 手工“校对状态”下拉框和可编辑“照片编号”输入框删除。
6. 编辑、确认、忽略动作自动产生状态。
7. 批量确认只处理当前明确选择且重新校验后仍安全的病害。
8. `warning`、`error`、构件未绑定、规范指标未确定、标度越界、照片引用未解决的病害不能进入批量确认。
9. 正式照片关系只认 `linked_defect_candidate_id`；来源编号不决定归属。
10. 旧草稿无法唯一推导的字段保持待处理，不自动确认。
11. 已修改病害确认后仍保留 `review_status="已修改"`，只把组状态改为已确认。
12. 页面分区切换继续保持已挂载状态；筛选、选择、当前病害、分页和折叠状态不得丢失。

---

## Task 0：记录基线并保护重叠修改

- [ ] 记录 `git status --short`。
- [ ] 保存下列重叠文件当前 diff：
  - `frontend/src/pages/ReviewWorkspacePage.tsx`
  - `frontend/src/pages/ReviewWorkspacePage.test.tsx`
  - `frontend/src/styles.css`
- [ ] 运行当前相关基线：
  - `uv run pytest tools-python/tests/test_annual_inspection_contract.py tools-python/tests/importers/test_defect_tables.py`
  - `npm run test -- --run src/contracts/annualInspection.test.ts src/review/reviewDraft.test.ts src/review/defectPhotoGroups.test.ts src/review/components/DefectsSection.test.tsx`
  - Debug 后端测试中合同、草稿、评定和确认计划相关用例
- [ ] 记录既有失败，不能通过修改无关期望掩盖。
- [ ] 本任务不改代码、不提交。

## Task 1：四端合同加入来源快照和照片引用结论

### Python 与 JSON Schema

- [ ] 先写失败测试：
  - Word 来源快照完整对象可解析；
  - 人工病害 `source_snapshot=null` 可解析；
  - 规范指标 ID 可空或非空字符串；
  - 四种照片引用结论合法；
  - 重复 `photo_number` 拒绝；
  - 结论引用不在来源编号中的照片拒绝；
  - `matched/relinked/missing/unrelated` 的关联字段组合不合法时拒绝；
  - 额外字段继续拒绝。
- [ ] 新增严格模型 `DefectSourceSnapshot`、`PhotoReferenceReview`。
- [ ] `DefectSourceSnapshot.snapshot_origin` 为必填枚举
  `word_import | compatibility_snapshot`。
- [ ] `DefectCandidate` 增加：
  - `source_snapshot: DefectSourceSnapshot | None = None`
  - `standard_defect_indicator_id: str | None = None`
  - `photo_reference_reviews: list[PhotoReferenceReview] = []`
- [ ] 保留 `photo_numbers` 和 `confirmed_missing_photo_numbers`，标注兼容镜像语义。
- [ ] 从 Pydantic 导出器重新生成 JSON Schema；生成前保存 schema 当前 diff，生成后人工核对，不能覆盖用户修改。

### TypeScript 与 C++

- [ ] TypeScript 增加等价接口和运行时守卫。
- [ ] C++ 合同校验增加对象成员白名单、枚举、唯一性和字段组合校验。
- [ ] 现有 2.0 fixture 补充一个 Word 病害和一个人工病害样例。
- [ ] 四端字段名、可空性、默认值和枚举逐项对照。
- [ ] Python、TypeScript、C++ 合同测试通过。
- [ ] 提交：`feat(contract): describe defect source and photo references`

## Task 2：解析器生成不可变 Word 来源快照

- [ ] `test_defect_tables.py` 先写失败测试，断言 `source_snapshot` 精确保留：
  - 结构分部；
  - 构件类别与编号；
  - 原始病害类型、位置、描述和标度；
  - 数量、尺寸原文；
  - Word 照片编号顺序。
- [ ] 修改 `defect_tables.py`：创建病害时只生成来源快照，不改变现有正式字段、病害数、警告和照片匹配。
- [ ] 新解析快照写 `snapshot_origin="word_import"`。
- [ ] 初始 `standard_defect_indicator_id=null`，初始 `photo_reference_reviews=[]`，不得在 Python 端猜规范指标。
- [ ] 真实 Word 回归断言原有病害/照片数量不变，并抽样核对来源快照。
- [ ] 提交：`feat(import): preserve defect source snapshot`

## Task 3：2.0 旧草稿兼容归一化与来源防篡改

### 兼容归一化

- [ ] `test_contract_compatibility.cpp` 先写失败测试：
  - 缺 `source_snapshot` 时从当前 2.0 字段生成一次兼容快照，并写
    `snapshot_origin="compatibility_snapshot"`；
  - 缺新数组时补空数组；
  - 已有来源快照不覆盖；
  - 已确认照片且唯一关联当前病害时可推导 `matched`；
  - 已人工确认缺图时可推导 `missing`；
  - 高置信但尚未确认、重复编号、跨病害冲突不推导人工结论；
  - 归一化不改变 `contract.version=2.0`。
- [ ] 扩展 `normalize_review_contract`，只做可证明的兼容补全。
- [ ] `photo_numbers`、`confirmed_missing_photo_numbers` 从新结构同步为兼容镜像；旧数据首次仍允许作为推导输入。

### 服务端不可变证据

- [ ] `test_draft_validation.cpp` 先写失败测试：
  - 客户端修改 `source_snapshot` 被拒绝；
  - 客户端修改 `source_ref` 被拒绝；
  - 客户端修改 `range_split_origin` 被拒绝；
  - 人工病害保持无来源快照；
  - 规范指标和照片引用结论允许按业务规则修改；
  - `warnings_only` 重开仍不能伪造来源证据。
- [ ] 新增候选 ID 对齐的来源证据校验函数；保存草稿时比较数据库内原草稿与待保存草稿。
- [ ] 保存前先归一化数据库内旧草稿，再做来源比较、重开范围校验和审计；不能把
  GET 响应自动补出的来源快照误判为客户端篡改。
- [ ] 对新字段执行跨对象一致性校验：候选 ID 存在、来源编号存在、实际照片关系一致。
- [ ] 范围拆分规划器回归：
  - 每条拆分病害保留同一来源快照与规范指标 ID；
  - 克隆照片后重写 `photo_candidate_id`；
  - 重写 `resolved_defect_candidate_id` 到对应拆分病害；
  - 任何旧候选 ID 都不能残留为悬空引用。
- [ ] 保持现有编辑锁和服务端 `component_match_confirmed_by` 所有权。
- [ ] 提交：`fix(review): protect imported defect evidence`

## Task 4：把锁定规范包上下文暴露给校对页

### ReviewResponse

- [ ] 后端模型测试先失败：挂载年度有规范组合时返回
  `technical_condition_standard { package_id, standard_code, standard_name, official_edition, package_version }`；
  无配置时返回 `null`。
- [ ] `ReviewRepository::get_import_record_detail` 查询检测年度锁定的技术评定规范包。
- [ ] `ReviewModels` 只组装公开身份，不返回服务器路径或规则文件内容。
- [ ] TypeScript `ReviewResponse` 和 API 测试同步。

### 规范目录类型

- [ ] 将 `standardsApi.ts` 的 `defect_catalogs: unknown[]` 替换为明确类型：
  - `StandardDefectCatalog`
  - `StandardDefectIndicator`
  - `allowed_scales`
  - `applicable_component_ids`
- [ ] API 测试覆盖多目录、同名指标、允许标度和空目录。
- [ ] 页面只通过 `technical_condition_standard.package_id` 调用现有
  `GET /api/standards/{package_id}/catalog`，不得从构件列表猜包 ID。
- [ ] 提交：`feat(review): expose locked defect standard catalog`

## Task 5：C++ 规范指标解析成为评定唯一真值

- [ ] 新建纯模块 `DefectIndicatorResolver`，加入 CMake，测试覆盖：
  - 指标 ID 存在且适用于构件；
  - 指标 ID 不存在；
  - 指标 ID 存在但不适用；
  - 标度不在 `allowed_scales`；
  - 缺 ID 的兼容病害按“适用类别 + 名称完全一致”唯一匹配；
  - 同名多个指标保持歧义；
  - `defect_type` 与指标当前名称不一致时以 ID 为真值，并在输入摘要使用目录名称。
- [ ] `calculate_assessment_preview` 优先读取 `standard_defect_indicator_id`，不再对新数据只靠名称匹配。
- [ ] `AssessmentService` 与后续草稿保存校验必须复用该解析器，不得各写一套适用性/标度判断。
- [ ] 新稳定错误码：
  - `assessment_defect_indicator_required`
  - `assessment_defect_indicator_unknown`
  - `assessment_defect_indicator_not_applicable`
  - `assessment_defect_scale_not_allowed`
- [ ] 缺 ID 的兼容唯一匹配只用于打开旧草稿和提示；正式确认前由归一化结果写回 ID。
- [ ] 保持同一实际构件、同一指标取最高标度的现有聚合规则。
- [ ] 提交：`feat(assessment): score defects by standard indicator id`

## Task 6：前端纯派生模型统一问题、筛选和批量资格

- [ ] 新建 `defectPhotoReviewModel.test.ts`，先覆盖：
  - 汇总：全部、可批量确认、需处理、已确认；
  - 问题分类：构件、病害类型、标度、照片；
  - 搜索：构件编号、位置、类型、照片编号；
  - 规范指标按实际构件类别过滤；
  - 自动唯一匹配只选完全一致名称；
  - 缺指标、指标不适用、标度越界不可确认；
  - 同一照片编号被多个病害引用不可安全批量确认；
  - 高置信候选必须唯一、归档存在、关系指向当前病害；
  - 任一 parser warning、assessment issue 或 error 使病害不可批量确认；
  - 已忽略不进入确认；
  - 编辑后资格立即失效；
  - 筛选不清除隐藏选择，只移除已经失去资格的选择；
  - 下一条需处理病害按 Word 顺序定位。
- [ ] 模型输入显式包含：
  - 当前草稿；
  - 规范目录；
  - 当前且非过期的 assessment issues；
  - 当前选择集合。
- [ ] 模型输出显式包含：
  - 每条病害的状态、问题类别、原因文本；
  - 可安全批量确认 ID；
  - 行照片缩略图（最多三张）和总数；
  - 筛选后的稳定顺序；
  - 批量影响统计。
  - 可自动补全的唯一规范指标建议。
- [ ] 不在组件中复制资格判断。
- [ ] 提交：`feat(review): derive defect photo review eligibility`

## Task 7：重构 reducer 为明确动作和自动状态

- [ ] `reviewDraft.test.ts` 先写失败测试。
- [ ] 删除 action：
  - `set_defect_status`
  - `edit_defect_field(field="review_status")`
  - `edit_defect_field(field="photo_numbers")`
  - `edit_photo_number`
- [ ] 新增明确动作：
  - `select_standard_defect_indicator`
  - `apply_unique_standard_indicator_matches`
  - `ignore_defect`
  - `restore_ignored_defect`
  - `confirm_photo_reference_match`
  - `relink_photo_reference`
  - `confirm_missing_photo_reference`
  - `confirm_unrelated_photo_reference`
  - `reset_photo_reference_review`
  - `confirm_defect_groups`
- [ ] 规范指标选择同时更新 ID、显示名称和组待确认状态；不得修改 `source_snapshot.defect_type`。
- [ ] 自动唯一匹配只填补当前为 `null`、类别适用且名称完全一致的指标 ID，不把病害标记为人工修改，也不确认该组。
- [ ] 照片动作原子更新实际照片关系与对应引用结论，不能出现一半成功的草稿状态。
- [ ] `confirm_defect_groups`：
  - 只处理调用方给出的当前安全集合；
  - 将唯一高置信照片转为已确认；
  - 写入对应 `matched` 结论；
  - 将待确认病害变为已确认；
  - 保留已修改状态；
  - 将组状态变为已确认；
  - 只移除已解决的临时拆分核对警告。
- [ ] 任意正式字段、规范指标、照片关系或引用结论变化都使组状态回到待确认。
- [ ] 已解决警告精准收口：
  - 标度改为允许值后只移除该病害的 `defect_scale_invalid`；
  - 照片引用处理完成后只移除同一病害、同一编号对应的缺图警告；
  - 无关照片处理完成后只移除该照片的未引用警告；
  - 不删除其他字段、其他候选或拆分核对警告。
- [ ] 忽略使用独立动作，二次确认留在 UI；恢复后回到待确认。
- [ ] 兼容镜像字段由 reducer helper 同步，不允许 UI 任意写。
- [ ] 提交：`refactor(review): derive statuses from defect actions`

## Task 8：详情确认规则改用规范指标和引用结论

- [ ] `defectPhotoGroups.test.ts` 先覆盖：
  - 正式核心字段完整；
  - 指标 ID 与实际构件类别适用；
  - 标度允许；
  - 每个 Word 引用都有处理结论；
  - `matched/relinked/unrelated/missing` 与实际关系一致；
  - 已关联照片归档存在且已确认；
  - 其他病害引用同编号不误判当前组；
  - 具体中文原因稳定且去重；
  - 忽略病害不走普通确认。
- [ ] `canConfirmDefectPhotoGroup` 接收规范目录与当前 assessment issues，返回结构化原因：
  `{ code, category, message, target }`，不再只返回内部字符串。
- [ ] 批量资格复用详情确认的底层检查，再额外要求“可自动确认的高置信关系”；两套规则不得漂移。
- [ ] 提交：`refactor(review): validate defect photo references`

## Task 9：快速校对工具栏、视觉列表和批量确认

### 工具栏

- [ ] `DefectReviewToolbar.test.tsx`：
  - 四个汇总数量；
  - 默认“需处理”；
  - 四类问题筛选；
  - 搜索；
  - 清除筛选；
  - 选择数量与批量按钮。

### 视觉列表

- [ ] `DefectQuickReviewList.test.tsx`：
  - 行显示构件编号/位置、规范病害、标度、状态；
  - 最多三张缩略图和总数；
  - 点击行打开详情；
  - 点击缩略图打开详情并选中照片；
  - 只有可批量确认行可勾选；
  - 自动预选可由用户取消；
  - 页面切换保持选择；
  - 每页 50 条，筛选后分页重新夹紧；
  - 跳转指定病害时自动翻页。
- [ ] 继续使用受控 `photoContentUrl`，图片 `loading="lazy"`，缩略图失败显示占位。

### 批量确认对话框

- [ ] `DefectBatchConfirmDialog.test.tsx`：
  - 展示病害数、照片数、异常未处理数；
  - 打开时和确认时各重新计算一次资格；
  - 资格变化时显示被移除数量，不处理失效项；
  - 无有效选择时不 dispatch；
  - 确认只修改当前草稿，不自动保存或入库。
- [ ] 提交：`feat(review): batch confirm safe defect photo groups`

## Task 10：正式病害详情维护区

- [ ] `DefectDetailEditor.test.tsx` 先覆盖：
  - 左侧核心字段：实际构件、规范病害、位置、描述、标度；
  - 规范病害按当前实际构件类别过滤；
  - 标度选项来自指标 `allowed_scales`；
  - 更换实际构件后旧指标不适用则清空并提示；
  - 修改字段显示“已修改”和原值；
  - “检测量与补充信息”默认折叠；
  - “导入原文”默认折叠、只读；
  - 人工病害显示“人工新增，无 Word 来源”；
  - 无法确认时按钮禁用并显示具体原因；
  - “确认并查看下一条”确认后选中下一条需处理；
  - “仅保存修改”不改变组确认状态；
  - “忽略病害”二次确认；
  - 只读/无编辑锁时仍可展开和看照片。
- [ ] 实际构件继续复用 `ComponentMatchField` 和已加载台账，不新增第二套构件选择逻辑。
- [ ] 新增病害表单也改用规范病害选择器；禁止自由文本直接成为正式规范类型。
- [ ] 删除详情中的校对状态下拉框和照片编号输入框。
- [ ] 旧 `DefectPhotoGroup.tsx` 缩减为兼容包装或删除其表单职责；迁移完成后不能存在两套可编辑卡片。
- [ ] 提交：`feat(review): maintain formal defect details`

## Task 11：照片关系与原文引用精细维护

- [ ] `PhotoRelationEditor.test.tsx` 先覆盖：
  - 当前大图、缩略图和加载错误；
  - Word 原始照片引用逐项状态；
  - “照片正确”；
  - “重新关联”到其他病害；
  - “确认缺图”；
  - “确认无关”；
  - “原文引用有误”通过 `relinked` 记录目标；
  - 撤销处理回到待确认；
  - 同编号多照片时强制人工选择具体照片；
  - 关系变化同时使来源组和目标组回到待确认；
  - 未解决引用不允许确认当前组。
- [ ] `UnlinkedPhotosPanel` 与详情照片编辑器共享照片动作和状态文案，不保留另一套“忽略/无关”语义。
- [ ] 照片编号只读展示；正式归属只由关系动作改变。
- [ ] 提交：`feat(review): resolve imported photo references`

## Task 12：整合两阶段页面并保持交互状态

- [ ] `DefectsSection.test.tsx` 和 `ReviewWorkspacePage.test.tsx` 先覆盖：
  - 页面结构为工具栏 → 快速列表 → 当前详情 → 未关联照片；
  - 首次进入允许加载规范目录和缩略图；
  - 切到其他工作区再返回时不卸载；
  - 筛选、选择、页码、当前病害、当前照片和 details 展开状态保持；
  - assessment 响应落后于草稿 revision 时不得用于安全资格；
  - assessment 更新后自动重新计算且取消失效预选；
  - “需要处理”侧栏跳转能定位列表行和详情字段/照片；
  - `warnings_only` 重开与只读查看权限仍正确；
  - 新增/删除病害后列表、汇总和选择集合一致。
- [ ] `ReviewWorkspacePage` 传入当前 assessment issues、响应 revision、规范目录加载状态和锁状态。
- [ ] 规范目录首次加载完成后应用一次唯一指标建议；歧义或无法匹配项保持空值并进入需处理。
- [ ] 规范目录按 package ID 缓存；同一次工作区分区切换不重复请求。
- [ ] 首屏不挂载 50 张大图；详情大图只在选中时加载。
- [ ] `styles.css` 最小合并现有用户样式，支持窄屏退化为上下布局。
- [ ] 提交：`feat(review): integrate two-stage defect photo workspace`

## Task 13：后端保存与预检验证规范指标和照片引用

- [ ] `test_draft_validation.cpp` 增加失败测试：
  - 指标字段类型错误；
  - 引用候选不存在；
  - `matched` 实际关联不是当前病害；
  - `relinked` 目标等于当前病害或目标不存在；
  - `missing` 仍指向照片；
  - `unrelated` 照片仍绑定病害；
  - 同一来源编号重复结论；
  - 兼容镜像与新结构不一致时由服务端规范化而不是相信客户端；
  - 已确认组仍有未解决引用时拒绝保存。
- [ ] 保存路由取得当前锁定技术评定规范包和最新确认台账，服务端校验：
  - 指标存在；
  - 指标适用于实际构件类别；
  - 标度在允许集合中；
  - 照片引用结论与实际关系一致。
- [ ] `register_review_routes` 注入 `StandardRegistry`；保存校验复用 Task 5
  `DefectIndicatorResolver`，并同步修改路由声明和 `main.cpp` 注册调用。
- [ ] 组处于待确认时允许字段尚未补齐；`group_review_status="已确认"` 或批量确认结果必须全部通过。
- [ ] preflight 继续通过 AssessmentService 做最终规范校验；错误码映射到对应病害字段。
- [ ] 提交：`feat(review): validate confirmed defect photo groups`

## Task 14：正式入库保存规范指标与完整来源证据

### 数据库迁移

- [ ] 新增幂等迁移 `017_defect_standard_indicator.sql`：
  - `defect_observations.standard_defect_indicator_id text null`
  - 新索引 `(inspection_year_id, bridge_component_id, standard_defect_indicator_id)`
- [ ] 不回填历史行；旧档案 `null` 表示历史数据没有稳定指标 ID。
- [ ] 冒烟脚本验证列、索引、重复执行和事务回滚。

### ConfirmPlan 与写入

- [ ] `test_confirm_plan.cpp` 先覆盖：
  - 规范指标 ID 映射；
  - 来源快照映射；
  - 照片引用结论映射；
  - 人工病害无来源快照；
  - 忽略病害不进入计划；
  - 临时拆分警告未解决仍阻断。
- [ ] `DefectPlan` 增加指标 ID、来源快照和照片引用结论。
- [ ] `build_raw_cells_json` 合并：
  - `raw_row_text`
  - `source_snapshot`
  - `photo_reference_reviews`
  - `range_split_origin`
- [ ] `insert_defect_observation` 写入规范指标列。
- [ ] `defect_photos` 仍只从已确认实际照片关系生成，不读取 `source_snapshot.photo_numbers`。
- [ ] 提交：`feat(archive): persist standard defect identity`

## Task 15：病害档案显示规范身份和来源处理记录

- [ ] `ComponentArchiveRepository` 查询和响应增加 `standard_defect_indicator_id`。
- [ ] 历史空值正常返回 `null`，不得按文本猜回填。
- [ ] 来源证据面板结构化展示：
  - Word 原始病害类型；
  - Word 原始照片编号；
  - 照片引用处理结果；
  - 既有范围拆分来源。
- [ ] 仍保留原始 JSON 展开能力。
- [ ] 后端仓储和前端 `ObservationYearRow` 测试通过。
- [ ] 提交：`feat(archive): show reviewed defect provenance`

## Task 16：全量验证与真实数据验收

### 自动验证

- [ ] Python：
  - `uv run pytest`
- [ ] 前端：
  - `npm run test -- --run`
  - `npm run build`
- [ ] C++：
  - `cmake --build --preset vs-debug`
  - 运行 Debug 后端全量测试；配置测试数据库时 DB 门控用例全部通过
- [ ] 数据库：
  - `.\scripts\dev\check-database.ps1`
  - 017 迁移连续执行两次无失败
- [ ] `git diff --check`
- [ ] `git status --short` 确认用户原有修改仍完整且未误提交。

### 性能夹具

- [ ] 建立至少 361 条病害、279 张照片的前端夹具。
- [ ] 验证：
  - 首次进入页面可操作；
  - 默认需处理筛选响应流畅；
  - 切换筛选和搜索不挂载全部大图；
  - 切换工作区再返回立即显示；
  - 批量确认 100 条以上安全病害不丢选择、不误确认异常项。

### 真实 Word 回归

- [ ] 解析现有真实报告，确认病害数、照片数和既有警告基线不变。
- [ ] 抽样核对：
  - 唯一规范病害可自动选中；
  - `/` 病害进入病害类型问题；
  - 高置信唯一照片进入可批量确认；
  - 缺图、重复编号、跨病害冲突不自动确认；
  - 批量确认后已修改病害仍显示已修改；
  - 详情修改指标或标度后系统评定立即更新；
  - 正式入库后档案包含指标 ID、来源快照和照片处理记录。
- [ ] 对真实业务记录执行批量确认或入库前，必须再次取得用户对该具体记录的授权；无授权时只用复制夹具验收。

### 文档收口

- [ ] 更新 `docs/superpowers/specs/modules/05-review-workspace.md` 的页面、状态和合同字段。
- [ ] 更新 `PROJECT_CONTEXT.md` 当前进度。
- [ ] 删除已经无调用的旧卡片代码和样式；不能保留隐藏的第二套编辑入口。
- [ ] 最终提交不夹带工作区既有修改。
- [ ] 提交：`feat(review): redesign defect photo review workflow`

---

## 依赖关系与实施批次

```text
批次 1（合同与来源）：T0 → T1 → T2 → T3
批次 2（规范真值）：T4 → T5
批次 3（前端领域层）：T6 → T7 → T8
批次 4（两阶段界面）：T9 → T10 ∥ T11 → T12
批次 5（后端正式化）：T13 → T14 → T15
批次 6（收口）：T16
```

- T3 必须在前端可编辑新字段之前完成，防止来源证据可被客户端篡改。
- T5 必须在 T6 批量资格前完成，确保前后端采用同一规范语义。
- T10 和 T11 可在 T8 结构化确认规则完成后并行，但 T12 必须等两者均完成。
- T14 必须在正式入库真实数据验收之前应用迁移。

## 主要风险与防护

1. **合同双真值：** 2.0 兼容期保留旧照片字段。服务端统一从 `source_snapshot + photo_reference_reviews` 生成兼容镜像，界面不提供旧字段写入口。
2. **错误批量确认：** 自动预选要求当前 assessment revision、唯一照片引用、归档存在、无任何 warning/error；确认对话框再次计算，后端保存和 preflight 再兜底。
3. **规范名称漂移：** 正式评分和档案保存指标 ID；名称仅作显示。规则包升级不改写历史指标 ID。
4. **旧草稿无法恢复原值：** 缺来源快照的 2.0 草稿只能把首次兼容时的当前字段固化为来源，不能伪造已丢失的 Word 原值；界面标注“兼容快照”。
5. **照片编号重复：** Python 现有匹配取首项，快速资格层必须检测跨病害重复引用并禁止自动确认；人工详情明确选择照片和目标病害。
6. **重开范围：** `warnings_only` 当前不允许修改顶层照片数组。本功能保持该安全边界；若未来要在已确认记录中单独维护照片，需要另立变更，不在本次扩权。
7. **性能回退：** 保留每页 50 条、缩略图懒加载、详情大图按需加载；不得把所有详情卡片同时挂载。
8. **工作区脏改重叠：** `ReviewWorkspacePage` 和 `styles.css` 逐块合并，每个提交前检查 staged 文件清单。

## 完成定义

只有同时满足以下条件才算完成：

1. Word 解析规则和真实报告病害/照片数量不变。
2. 用户进入页面即可看到汇总、问题分类和照片缩略图。
3. 安全病害能批量确认，异常病害绝不被批量动作修改。
4. 筛选、选择、当前详情和展开状态在工作区切换后保留。
5. 手工校对状态和照片编号输入框已移除。
6. 正式病害类型由稳定规范指标 ID 表达，标度按指标允许集合校验。
7. Word 来源快照只读且后端防篡改。
8. 每个 Word 照片引用都有可追溯处理结论，正式照片关系只有一个真值。
9. 修改、确认、忽略由明确动作自动产生状态。
10. 已修改病害确认后仍保留人工修改痕迹。
11. 正式入库和档案保留规范指标、来源快照、照片引用处理结果及范围拆分来源。
12. 361 条病害规模下页面保持可用，未一次加载全部大图。
13. Python、前端、C++、数据库全量测试和生产构建通过。
14. 用户原有未提交修改完整保留。

## 自审清单

- [x] 设计规格的快速判断与精细维护两个阶段均映射到独立任务。
- [x] 自动预选、批量影响汇总、二次资格校验和后端兜底均有明确归属。
- [x] 规范指标 ID 从目录、草稿、评定到正式档案全链路闭合。
- [x] Word 来源证据有结构化快照且后端防篡改。
- [x] 照片来源引用与正式照片关系分离，并有一致性校验。
- [x] 手工状态下拉和照片编号输入删除，替换为明确动作。
- [x] 旧 2.0 草稿兼容策略不伪造人工结论。
- [x] 每项包含文件、失败测试、实现和提交边界。
- [x] 数据库迁移为新增列且不回填历史事实。
- [x] 性能、工作区缓存、真实 Word 和脏工作区均有验收。
