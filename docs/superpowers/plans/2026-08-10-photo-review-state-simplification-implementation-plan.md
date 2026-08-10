# 实施计划：病害照片确认状态简化

设计文档：[2026-08-10-photo-review-state-simplification-design.md](../specs/2026-08-10-photo-review-state-simplification-design.md)

日期：2026-08-10

## 实施目标

删除照片级“确认 / 撤销确认”流程，将病害组确认作为构件、病害、标度和照片关系的唯一人工确认
入口；从照片候选、正式照片表和跨语言代码中删除冗余的 `match_status` 与照片级
`review_status`；将 `BridgeAnnualInspectionData` 一次性升级到 4.0，同时保持现有照片添加、移除、
上传、缺图确认和文件清理行为不变。

## 实施约束

- 当前数据均为测试数据，不迁移 3.0 草稿，不提供 3.0 / 4.0 双版本兼容。
- 不修改 `BridgeCheck.match_status`、病害 `review_status`、病害 `group_review_status`、
  `component_match_method` 或 `rating_tree_match_method`。
- 保留 `photo_references[].resolution`、照片 `confidence`、来源和警告。
- 不新增或重写照片添加、移除、上传、确认缺图和文件删除接口。
- 不在本计划中实现构件范围拆分关系的批量核对。
- 历史迁移 `002_core_schema_and_archive.sql` 不回写；通过新迁移删除正式表字段。
- 工作区已有大量用户改动和已暂存删除。每次提交必须按路径限定，禁止重置、还原或夹带无关
  变更。

## 关键实现决定

### 唯一确认入口

照片本身不再拥有人工确认状态。当前关联由 `linked_defect_candidate_id` 表达，正式归属由
`defect_photos.defect_observation_id` 表达，最终人工确认由病害 `group_review_status` 表达。

### 合同 4.0

4.0 的 `PhotoCandidate` 不含 `match_status`、`review_status`。所有生产者和消费者一次性切换；
遗留字段因合同禁止额外属性而被拒绝。

### 确认计划

照片进入正式表只要求：关联目标属于本次确认计划，且归档路径有效。未归属照片不入库；Word
明确引用但仍为 `pending` 的照片继续阻断。

### 正式数据库

`defect_photos` 的非空病害外键已经表达正式归属，因此删除 `match_status` 和依赖它的索引，改建
`(defect_observation_id, photo_number)` 索引。

---

## Task 0：记录基线并保护工作区

**修改：无。**

步骤：

- [ ] 记录 `git status --short` 和 `git diff --cached --name-status`，特别保留用户已暂存的
      `tools-python/.../standards` 删除，不改变其暂存状态。
- [ ] 记录当前合同版本、照片状态字段引用清单和最新数据库迁移编号。
- [ ] 运行前端、Python、C++ 和数据库基线，区分本任务前已经存在的失败。
- [ ] 后续每次提交使用显式路径；提交前检查 `git diff --cached --name-status`。

验证：

```powershell
git status --short
git diff --cached --name-status
rg -n "PhotoMatchStatus|confirm_photo|photo_not_confirmed|match_status" frontend backend-cpp tools-python contracts database samples
git diff --check
```

提交：无。

---

## Task 1：升级 Python 合同和所有照片候选生产者

**修改：**

- `tools-python/bridge_report_tools/contracts/annual_inspection.py`
- `tools-python/bridge_report_tools/importers/photo_extractor.py`
- `tools-python/bridge_report_tools/importers/source_db/photos.py`
- `tools-python/tests/test_annual_inspection_contract.py`
- `tools-python/tests/importers/test_word_importer.py`
- `tools-python/tests/importers/source_db/test_photos.py`
- `tools-python/tests/importers/source_db/test_endpoint.py`
- `contracts/bridge_annual_inspection_data.schema.json`
- 所有直接构造 3.0 合同或照片候选的 Python 固定数据

**重命名并修改：**

- `samples/contracts/bridge_annual_inspection_data.v3.valid.json`
  → `samples/contracts/bridge_annual_inspection_data.v4.valid.json`
- `samples/contracts/bridge_annual_inspection_data.v3.with-comparison.json`
  → `samples/contracts/bridge_annual_inspection_data.v4.with-comparison.json`

步骤：

- [ ] 先写失败合同测试：4.0 照片不需要两个状态字段；遗留 `match_status` 或 `review_status`
      因额外字段被拒绝；桥梁和病害状态仍被接受。
- [ ] 将合同版本字面量改为 `4.0`，删除 `PhotoMatchStatus` 和照片候选的两个字段。
- [ ] Word 照片抽取器只写目标 `linked_defect_candidate_id`、`confidence` 和警告；可确定目标时
      关联，不能确定时保持 `null`。
- [ ] 源数据库照片和人工上传照片候选停止写两个状态字段。
- [ ] 更新导入器测试，分别覆盖已关联和未归属照片，不再断言“高置信候选 / 已确认 / 未关联”。
- [ ] 从 Pydantic 模型重新生成 JSON Schema，禁止手工维护两份不同定义。
- [ ] 将样例文件升级并重命名为 v4，修正仓库内对旧样例路径的引用。

验证：

```powershell
cd tools-python
uv run python -m bridge_report_tools.contracts.export_schema
uv run pytest tests/test_annual_inspection_contract.py tests/importers/test_word_importer.py tests/importers/source_db/test_photos.py tests/importers/source_db/test_endpoint.py
```

提交建议：

```text
feat(contract): remove photo review statuses in version 4
```

---

## Task 2：简化前端照片模型和交互

**修改：**

- `frontend/src/contracts/annualInspection.ts`
- `frontend/src/contracts/annualInspection.test.ts`
- `frontend/src/review/defectPhotoCards.ts`
- `frontend/src/review/defectPhotoCards.test.ts`
- `frontend/src/review/defectPhotoReviewModel.ts`
- `frontend/src/review/defectPhotoReviewModel.test.ts`
- `frontend/src/review/reviewDraft.ts`
- `frontend/src/review/reviewDraft.test.ts`
- `frontend/src/review/components/DefectPhotoPanel.tsx`
- `frontend/src/review/components/DefectPhotoPanel.test.tsx`
- `frontend/src/review/components/UnlinkedPhotosPanel.tsx`
- `frontend/src/review/components/UnlinkedPhotosPanel.test.tsx`
- `frontend/src/review/components/DefectsSection.test.tsx`
- `frontend/src/review/testFixtures.ts`
- `frontend/src/api/defectPhotoApi.test.ts`
- 其他直接构造 `PhotoCandidate` 或 3.0 合同的前端测试夹具

步骤：

- [ ] 先写失败组件测试：照片卡片不显示“确认照片、撤销确认、已确认、待校对”；添加、移除、
      上传和缺图动作仍存在。
- [ ] 将 TypeScript 合同切换到 4.0，删除 `PhotoMatchStatus` 和照片候选状态字段，收紧类型守卫。
- [ ] 从照片卡片派生模型删除 `confirmed`；卡片是否实际存在仍由关联和归档文件派生。
- [ ] 删除 `confirm_photo` action、reducer 分支和所有调用。
- [ ] `link_photo_to_defect`、`unlink_photo_from_defect`、`add_photo`、`remove_photo` 只维护关联、
      Word 引用和病害组失效，不再写照片状态。
- [ ] `confirm_defect_groups` 不再扫描照片并批量改成“已确认”；仍按编号把可确定的 pending 引用
      更新为 `matched`。
- [ ] 从问题模型删除 `photo_not_confirmed` 和基于“高置信候选”的自动确认分支；保留引用 pending、
      归档缺失和无效关联问题。
- [ ] 照片面板删除确认/撤销按钮和状态标签；现有添加、移除、上传、查看、确认缺图功能不改。
- [ ] 未归属面板删除“系统判断 / 我的处理”，改为来源、编号、警告和未归属事实。
- [ ] 更新所有前端夹具，确保没有用固定“已确认”掩盖问题。

关键测试：

```text
自动关联且文件存在 → 不产生 photo_not_confirmed，可随本组确认
Word 引用 pending    → 仍阻断
无引用且无照片       → 不阻断
未归属且未被引用照片 → 不入库语义，不阻断病害
添加/移除照片         → 关系变化且病害组回到待确认
```

验证：

```powershell
cd frontend
npm run test -- --run src/contracts/annualInspection.test.ts src/review/defectPhotoCards.test.ts src/review/defectPhotoReviewModel.test.ts src/review/reviewDraft.test.ts src/review/components/DefectPhotoPanel.test.tsx src/review/components/UnlinkedPhotosPanel.test.tsx src/review/components/DefectsSection.test.tsx
npm run build
```

提交建议：

```text
refactor(review): confirm photos with their defect group
```

---

## Task 3：切换 C++ 合同、预检和确认计划

**修改：**

- `backend-cpp/src/contracts/AnnualInspectionContract.cpp`
- `backend-cpp/src/review/PreflightReport.cpp`
- `backend-cpp/src/review/ConfirmPlan.cpp`
- `backend-cpp/src/http/DefectPhotoRoutes.cpp`
- `backend-cpp/tests/support/review_fixtures.hpp`
- `backend-cpp/tests/test_annual_inspection_contract.cpp`
- `backend-cpp/tests/test_preflight_report.cpp`
- `backend-cpp/tests/test_confirm_plan.cpp`
- `backend-cpp/tests/test_defect_photo_routes.cpp`
- 其他直接构造照片候选的 C++ 测试

步骤：

- [ ] 先写失败 C++ 合同测试：只接受 4.0，照片无需两个状态字段，遗留字段被拒绝。
- [ ] 删除照片 `match_status` 和 `review_status` 的必填、枚举及结算校验；桥梁和病害的同名字段
      保持原规则。
- [ ] 重写 `PreflightReport` 的照片判断：检查关联目标、归档路径和 Word 引用，不再检查照片状态。
- [ ] 删除 `ConfirmPlan::append_photos` 中照片已结算且 `match_status == 已确认` 的门槛。
- [ ] 仅当 `linked_defect_candidate_id` 指向确认计划内病害且归档路径有效时生成 `PhotoPlan`。
- [ ] 人工上传端点返回的 4.0 照片候选不再写状态字段；上传、删除、编辑锁和文件事务保持不变。
- [ ] 更新共享测试夹具，禁止默认补“已确认”来制造可入库照片。

关键测试：

```text
已关联 + 有归档文件 + 病害入计划 → 照片入计划
未关联                           → 跳过
关联病害未入计划                 → 跳过
归档路径缺失                     → 预检问题且不入计划
Word 引用 pending                → 继续阻断
```

验证：

```powershell
cmake --build --preset vs-debug
.\build\vs-debug\Debug\bridge_report_backend_tests.exe --gtest_filter=*AnnualInspectionContract*:*PreflightReport*:*ConfirmPlan*:*DefectPhotoRoutes*
```

提交建议：

```text
refactor(review): derive photo persistence from linkage
```

---

## Task 4：删除正式照片状态列并更新持久化

**新增：**

- `database/migrations/025_drop_defect_photo_match_status.sql`
- `database/tests/025_drop_defect_photo_match_status_smoke.sql`

**修改：**

- `backend-cpp/src/db/ReviewRepository.cpp`
- `backend-cpp/tests/test_review_repository.cpp`
- `backend-cpp/tests/test_component_archive_repository.cpp`
- `database/tests/002_core_schema_smoke.sql`
- 其他直接向 `defect_photos.match_status` 插入或查询的数据库测试

步骤：

- [ ] 先写失败仓储测试，期望正式照片只凭病害归属和归档文件写入，不读取或返回状态列。
- [ ] 新迁移删除 `ix_defect_photos_observation_number_status`，再删除 `match_status`。
- [ ] 新建 `(defect_observation_id, photo_number)` 普通索引，保留现有查询性能。
- [ ] `ReviewRepository` 的照片插入 SQL 删除列和值，其他来源和归档字段不变。
- [ ] 更新仓储、构件档案和数据库 smoke，不再插入、查询或断言正式照片状态。
- [ ] smoke 断言列已不存在、新索引存在、级联删除和归档引用行为不变。
- [ ] 连续应用全部迁移两次，验证迁移脚本和检查脚本的幂等约定。

验证：

```powershell
.\scripts\dev\check-database.ps1
cmake --build --preset vs-debug
$env:BRIDGE_REPORT_TEST_DATABASE_URL='postgresql://bridge_report:bridge_report_dev@127.0.0.1:5432/bridge_report_system'
.\build\vs-debug\Debug\bridge_report_backend_tests.exe --gtest_filter=*ReviewRepository*:*ComponentArchiveRepository*
```

提交建议：

```text
refactor(database): drop defect photo match status
```

---

## Task 5：更新活动文档并做静态清扫

**修改：**

- `PROJECT_CONTEXT.md`
- `docs/superpowers/specs/modules/02-postgresql-schema-and-file-archive.md`
- `docs/superpowers/specs/modules/03-bridge-annual-inspection-data-contract.md`
- `docs/superpowers/specs/modules/05-review-workspace.md`
- 其他明确描述当前照片逐张确认行为的活动文档

步骤：

- [ ] 将活动合同文档升级到 4.0，删除照片状态字段示例。
- [ ] 将校对工作台描述改为“照片关系随病害组一次确认”。
- [ ] 将正式表说明中的 `match_status` 和含状态索引改为新结构。
- [ ] 更新 `PROJECT_CONTEXT.md` 当前能力摘要；历史设计和历史实施计划保留原文，由新设计声明替代关系。
- [ ] 全仓静态搜索，逐条分类剩余 `match_status`：桥梁匹配应保留；历史迁移和历史文档可保留；活动
      照片代码、样例和测试不得残留。
- [ ] 确认 `confirm_photo`、`photo_not_confirmed`、`PhotoMatchStatus` 在活动代码中为零引用。

验证：

```powershell
rg -n "PhotoMatchStatus|confirm_photo|photo_not_confirmed" frontend backend-cpp tools-python contracts samples
rg -n "match_status" frontend backend-cpp tools-python contracts samples database
rg -n '"version"\s*:\s*"3\.0"|Literal\["3\.0"\]' frontend backend-cpp tools-python contracts samples
git diff --check
```

提交建议：

```text
docs: align photo workflow with contract version 4
```

---

## Task 6：全量验证和人工验收

**修改：仅修复本任务引入的验证问题。**

步骤：

- [ ] 运行 Python 全量测试和真实 Word 环境门控回归；确认病害、照片文件和关联数量符合现有基线。
- [ ] 运行前端全量测试、TypeScript 编译和生产构建。
- [ ] 构建 C++ Debug，运行后端全量测试和 PostgreSQL 集成测试。
- [ ] 从空测试库连续应用全部迁移，运行所有数据库 smoke。
- [ ] 使用 Word 导入验证：自动关联照片没有确认按钮；移除后回到未归属区；重新添加后可随本组
      确认入库。
- [ ] 使用源数据库导入验证：已有明确关系的照片直接显示在对应病害下，没有“已确认”状态文字。
- [ ] 验证 Word 引用缺图和归档文件丢失仍出现在待处理问题中。
- [ ] 验证构件范围拆分警告仍存在，本任务没有自动清除它。
- [ ] 查询 `information_schema.columns`，确认正式库 `defect_photos.match_status` 不存在。
- [ ] 最终检查提交范围和工作区，确保用户原有改动未被覆盖或带入本任务提交。

全量验证：

```powershell
cd tools-python
uv run pytest

cd ../frontend
npm run test -- --run
npm run build

cd ..
cmake --build --preset vs-debug
$env:BRIDGE_REPORT_TEST_DATABASE_URL='postgresql://bridge_report:bridge_report_dev@127.0.0.1:5432/bridge_report_system'
.\build\vs-debug\Debug\bridge_report_backend_tests.exe --gtest_brief=1
.\scripts\dev\check-database.ps1
git diff --check
git status --short
```

提交：只有修复验证问题时才创建，并按修复范围命名；纯验证不提交。

---

## 完成定义

- [ ] `BridgeAnnualInspectionData 4.0` 是唯一活动合同版本。
- [ ] 照片候选和正式照片表均不含照片级确认状态。
- [ ] 界面不存在照片确认、撤销确认或照片确认状态标签。
- [ ] 现有照片添加、移除、上传、缺图确认和文件清理行为保持不变。
- [ ] 病害组确认能够把当前有效照片关系写入正式表。
- [ ] 未归属照片不入库，真实照片异常仍被预检发现。
- [ ] 全量 Python、前端、C++、PostgreSQL 和数据库迁移测试通过。
- [ ] 活动文档与新模型一致，历史文档通过新设计的替代声明保留。
