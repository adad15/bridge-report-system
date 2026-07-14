# 模块 06 实施计划：合同 1.2 跨模块升级 + 构件病害档案

> 日期：2026-07-13
>
> 状态：计划已确认，待实施
>
> 依据：`docs/superpowers/specs/changes/2026-07-13-change-001-component-rating-and-defect-location.md`、`docs/superpowers/specs/modules/03/04/05/06`
>
> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task.
>
> 实施期间不得提交、删除或修改本地目录：`.claude/`、`test-inputs/`、`test-output/`、`backend-cpp/archive/`、`tools-python/archive/`。

---

## Context

模块 05 已完成"候选 JSON → 人工校对 → C++ 事务入库"闭环，但合同 1.1 未表达表 2.x-1 的病害标度、病害扣分和构件评分；且 `ConfirmPlan.cpp:213` 把校对提示级别 `severity` 错误写入 `defect_observations.scale`（规范标度），语义混用。模块 06（只读构件病害档案 + 人工病害线索整理）必须读取可信构件年度事实，因此按变更提案 001 方案 A：先完成合同 1.2 跨模块升级（03→04→05），再建设模块 06。构件评分依据 JTG/T H21-2011 第 4.1.1 条：

```text
将同一构件病害扣分 DP 降序排列：
U1 = DP1
Ux = DPx / (100 × sqrt(x)) × (100 - ΣUj)   (x ≥ 2)
P  = 100 - ΣUx
全程不提前舍入；任一 DP=100 时 P=0。
```

## 已核实现状（git + 代码勘察结论）

- 起点 HEAD `295680f`（`feature/06-component-defect-archive`），工作区干净（仅上述禁动的未跟踪本地目录）。
- **数据库 002 迁移**：`defect_observations` 已有 `defect_location`、`scale text`、`defect_deduction numeric(8,2)`、`component_score numeric(8,2)`、`defect_thread_id`（set null）——病害侧无需 DDL，只差 C++ 写入。`condition_ratings` 的 `rating_level` CHECK 已含 `'构件'`、已有 `bridge_component_id`，缺 `source_score / calculated_score / score_validation_status / score_resolution_reason / calculation_details_json` 5 列和"同检测版本同构件唯一"约束。`defect_threads` 表结构齐全（`first/latest_seen_inspection_id` 指向 `inspection_years`），当前无任何代码写它。迁移无框架，按 `NNN_*.sql` 用 psql 应用（参照 `scripts/dev/check-module02-db.ps1`），下一个编号 003。
- **C++**：合同版本钉死 `"1.1"`（`AnnualInspectionContract.cpp:118`）；severity→scale 在 `ConfirmPlan.cpp:213`；确认 INSERT 不写 `defect_deduction`，评分 INSERT 不写 `bridge_component_id`；`ContractCompatibility.cpp` 是版本兼容接缝（现处理 1.0→1.1 内存升级）；纯函数惯例：`src/review/*.cpp` + `include/bridge_report/review/*.hpp`，命名空间 `bridge_report::review`；测试单可执行文件 `bridge_report_backend_tests`，夹具经 `BRIDGE_REPORT_REPOSITORY_ROOT` 读 `samples/contracts/`；确认事务用 `newTransaction + CommitLatch`。
- **Python**：`annual_inspection.py` 全模型 `extra="forbid"`，`version: Literal["1.1"]`；`DefectCandidate` 已有 `defect_location` 与 `severity`，缺 `defect_scale`、`defect_deduction`；`Ratings` 无 `component_ratings`；Schema 由 `uv run python -m bridge_report_tools.contracts.export_schema` 生成，不手改；`defect_tables.py` 中 标度/病害扣分/构件评分 目前只是排除词，完全未解析，且无构件组传播逻辑；`rating_tables.py` 的表 4.1-2 `构件评分` 是按数量聚合的 `score_rows`，与第二章 per-构件评分粒度不同、互不干扰；真实回归 `tools-python/tests/importers/test_real_word_regression.py` 门控 `BRIDGE_REPORT_REAL_WORD_PATH`，断言 25/31/36/31 与 15 评分项（模块 05 的真实 Word 验收即此测试 + 手工 E2E，backend-cpp 无真实 Word E2E 测试文件）。
- **前端**：手写类型守卫（无 zod）；`App.tsx` 4 条路由，`:22` 处 `/\/review$/` 正则决定工作台全屏壳（会误匹配 `/defect-threads/review`，需收紧）；`reviewApi.ts:46` 的 `ContractCompatibility` 标签需随 C++ 改；`reviewDraft.ts` 判别联合 action + 穷尽 switch；`RatingsSection.tsx` 无构件评分行；数据加载 useEffect+fetch；样式单一 `styles.css`；测试 Vitest 2 + Testing Library。
- **跨语言夹具机制已存在**：`samples/contracts/*.json` 由 Python 与 C++ 共同消费；前端用 `review/testFixtures.ts` 同构对象。评分纯函数一致性测试沿用该机制，新增 `samples/scoring/`。

## Global Constraints（实施全程必守）

- 主页面只读优先；病害线索是一级单位，年度观测是二级单位。
- `defect_threads.defect_location` = 线索标准位置；`defect_observations.defect_location` = 年度实际位置原文；两层并存，不互相覆盖。
- 系统只能建议"同一病害"候选，绝不自动写 `defect_thread_id`；绑定/重绑只由人工发起。
- 模块 06 只修改线索与绑定关系，不修改年度病害事实；不生成发展/减轻/修复/新增结论（属模块 07）。
- 默认只读各年度当前有效版本（`inspection_years.is_current AND status='已确认'`）；旧修订版独立只读展示、不进统计；新修订版不自动继承旧版 `defect_thread_id`。
- `severity` 只表示 info/warning/error 提示级别，绝不写入 `scale`；`defect_scale` 只来自合同 1.2。
- 第一版只使用 Word 已给出的 DP 复算，不从标度反推 DP，不重算部件/结构分部/全桥评分。
- `score_validation_status ∈ {不一致, 无法复算}` 时 `confirmed_score` 必须为 null；只有用户显式选择"接受 Word 值 / 采用复算值"并填写 `score_resolution_reason` 后才能有值；自动`一致`时可预填来源值、原因为 null。
- 已确认旧 1.1 数据保持可读、不猜测回填；缺明细的年度显示"历史数据缺少评分校验明细"。
- 待校对的 1.0/1.1 旧草稿不做静默字段补造：标记 `legacy_pending_reparse` 只读，须经既有 `POST /api/import-records/{id}/parse-word` 重新解析为 1.2 后再校对（依据变更提案 001 §7"尚未确认的 1.1 草稿应重新解析为 1.2"）。
- 评分计算全程 double 不提前舍入；展示/比较用统一自实现 `round2(x) = floor(x*100 + 0.5) / 100`（半数远离零），三语言一致，禁用各语言内建 round 的默认平/半舍规则。
- 照片与证据只经 C++ 受控接口读取，不暴露归档绝对路径。

## 固定验收数值

- 纯函数：`[35] → 65`；`[35,20] → 55.8076118446…`（round2=55.81）；输入顺序不影响结果；任一 `DP=100 → 0`；过程不提前舍入。
- 真实 Word 原基线保持：25 条病害、31 个照片候选、36 个 Word 图片、31 张归档照片、15 个原评分项（1 全桥 + 3 结构分部 + 11 评价部件，`component_ratings` 不计入该 15）。
- 新增精确断言：`1-1#板` 标度 2、扣分 35、构件评分 65；`1-2#板` 同；`2-1#板` 扣分 35、20，构件评分 55.81；上部承重构件 86.62；全桥 85.61。

---

## File Map（关键新增/修改文件）

**合同（四端）**
- 改 `tools-python/bridge_report_tools/contracts/annual_inspection.py`（1.2 真源）
- 再生成 `contracts/bridge_annual_inspection_data.schema.json`；改 `contracts/README.md`
- 改 `samples/contracts/*.json`（3 个升级）+ 新 `samples/contracts/bridge_annual_inspection_data.invalid-component-rating-status.json`
- 改 `frontend/src/contracts/annualInspection.ts`、`frontend/src/review/testFixtures.ts`
- 改 `backend-cpp/src/contracts/AnnualInspectionContract.cpp`、`backend-cpp/src/review/ContractCompatibility.cpp`

**评分纯函数（三语言 + 共享夹具）**
- 新 `samples/scoring/component_score_cases.json`
- 新 `tools-python/bridge_report_tools/scoring/component_score.py`
- 新 `backend-cpp/include/bridge_report/review/ComponentScore.hpp` + `backend-cpp/src/review/ComponentScore.cpp`
- 新 `frontend/src/review/componentScore.ts`

**Python 解析**
- 改 `tools-python/bridge_report_tools/importers/defect_tables.py`、`word_importer.py`、`tests/importers/docx_fixtures.py` 及相关测试

**模块 05 / 入库**
- 改 `frontend/src/review/reviewDraft.ts`、`grouping.ts`、`components/RatingsSection.tsx`、`components/DefectPhotoGroup.tsx`、`reviewSession.ts`、`api/reviewApi.ts`
- 改 `backend-cpp/src/review/ConfirmPlan.cpp`、`PreflightReport.cpp`、`ReviewStatistics.cpp`、`backend-cpp/src/db/ReviewRepository.cpp`

**数据库**
- 新 `database/migrations/003_component_rating_validation_and_thread_binding.sql`
- 新 `database/tests/003_component_rating_archive_smoke.sql`、新 `scripts/dev/check-module06-db.ps1`

**模块 06 后端**
- 新 `backend-cpp/{include/bridge_report,src}/db/ComponentArchiveRepository.{hpp,cpp}`
- 新 `backend-cpp/{include/bridge_report,src}/http/ComponentArchiveRoutes.{hpp,cpp}`
- 新 `backend-cpp/{include/bridge_report,src}/review/ThreadSuggestions.{hpp,cpp}`
- 新 `backend-cpp/{include/bridge_report,src}/db/DefectThreadRepository.{hpp,cpp}`
- 新 `backend-cpp/{include/bridge_report,src}/http/DefectThreadRoutes.{hpp,cpp}`
- 改 `backend-cpp/src/main.cpp`、`backend-cpp/CMakeLists.txt`

**模块 06 前端**
- 新 `frontend/src/api/componentArchiveApi.ts`
- 新 `frontend/src/pages/ComponentArchivePage.tsx`、`frontend/src/pages/DefectThreadReviewPage.tsx`
- 新 `frontend/src/archive/`（ComponentListPanel / ComponentDetailPanel / DefectThreadCard / ObservationYearRow / ComponentRatingSummary / RevisionHistoryPanel / ThreadBindingCard 及测试）
- 改 `frontend/src/App.tsx`、`frontend/src/pages/BridgeDetailPage.tsx`、`frontend/src/styles.css`

---

## 任务清单（15 个任务）

### Task 1：合同 1.2 —— Python Pydantic 真源 + Schema 再生成 + 共享夹具

**修改文件**：`tools-python/bridge_report_tools/contracts/annual_inspection.py`；`tools-python/bridge_report_tools/importers/word_importer.py`（version="1.2"、`PARSER_VERSION="0.2.0"`、`ratings.component_ratings=[]` 占位）；`tools-python/tests/test_annual_inspection_contract.py`；再生成 `contracts/bridge_annual_inspection_data.schema.json`；`contracts/README.md`；`samples/contracts/` 3 个夹具升级 + 新增 `bridge_annual_inspection_data.invalid-component-rating-status.json`。

**核心逻辑**：
- `ContractInfo.version: Literal["1.2"]`。
- `DefectCandidate` 增加 `defect_scale: int | None`（`gt=0`）、`defect_deduction: float | None`（`ge=0, le=100`）；`severity` 语义不变。
- 新增：
  - `ScoreValidationStatus = Literal["一致","不一致","无法复算","人工接受Word值","人工采用复算值"]`
  - `ComponentRef(structure_part: StructurePart, component_name: str, component_alias: str | None)`
  - `ComponentScoreCalculationDetails(standard: Literal["JTG/T H21-2011 4.1.1"], ordered_deductions: list[float], rounding_scale: Literal[2])`
  - `ComponentRatingCandidate(candidate_id: str, component_ref, source_score/calculated_score/confirmed_score: float | None（各 0–100）, score_validation_status, score_resolution_reason: str | None, deduction_defect_candidate_ids: list[str], calculation_details: ComponentScoreCalculationDetails | None, review_status: ReviewStatus, warnings: list[WarningItem])`
  - `Ratings.component_ratings: list[ComponentRatingCandidate]`（必填数组）。
- `candidate_id` 为规格"每项至少包含"之外的补充字段（前端编辑与 warning 定位需要，命名 `component_rating_%04d`），在 Task 15 同步进模块 03 文档变更记录。
- `model_validator` 固化状态不变量（三端一致）：
  - 状态 ∈ {不一致, 无法复算} ⇒ `confirmed_score is None` 且 `score_resolution_reason is None`；
  - 状态 ∈ {人工接受Word值, 人工采用复算值} ⇒ `confirmed_score` 非空且 `score_resolution_reason` 非空字符串；
  - 状态 = 一致 ⇒ `score_resolution_reason is None`。
- 夹具升级：3 个既有夹具补 `defect_scale/defect_deduction`（可为 null）与 `component_ratings`（valid 夹具含一条"一致"记录）；新增非法夹具：状态"不一致"却带 `confirmed_score`。

**测试**：version=1.1 拒绝；缺新字段拒绝（extra="forbid" + 必填）；三条不变量各自违反时拒绝；valid 夹具通过；`parse_word_import` 输出 version=1.2 且 `component_ratings==[]`（本任务阶段）。

**验收命令与预期**：
```powershell
Set-Location tools-python
uv run python -m bridge_report_tools.contracts.export_schema
uv run pytest -q
```
预期：全部通过；`git status` 显示 schema 文件已再生成且含 `component_ratings`、`"const": "1.2"`。

---

### Task 2：TypeScript 合同与会话状态同步

**修改文件**：`frontend/src/contracts/annualInspection.ts`（+`.test.ts`）；`frontend/src/review/testFixtures.ts`；`frontend/src/api/reviewApi.ts`（+`.test.ts`）；`frontend/src/review/reviewSession.ts`（+`.test.ts`）。

**核心逻辑**：
- 类型：`ContractInfo.version: "1.2"`；`DefectCandidate` 增 `defect_scale?: number | null`、`defect_deduction?: number | null`；新增 `ScoreValidationStatus`、`ComponentRef`、`ComponentScoreCalculationDetails`、`ComponentRatingCandidate` 接口；`Ratings.component_ratings: ComponentRatingCandidate[]`。
- 守卫：复用 `isRecord/hasOwn/getRequiredArray` 等辅助；版本判定改 `"1.2"`；新增 `isValidComponentRating`，检查数值范围、枚举与 Task 1 的三条状态不变量。
- `reviewApi.ts` 的 `ContractCompatibility` 改为 `"native_1_2" | "legacy_pending_reparse" | "legacy_read_only"`。
- `reviewSession.ts`：仅 `importStatus==="待校对" && compatibility==="native_1_2"` 可编辑；`legacy_pending_reparse` 返回横幅文案"该草稿为旧版合同（1.0/1.1），请重新解析为 1.2 后再校对"（只读，重解析走既有 parse-word 接口，本模块不新增前端上传/解析 UI）。

**测试**：接受合法 1.2 数据；拒绝 1.1；拒绝各不变量违反；`reviewSession` 对三种兼容标签的只读/横幅断言；`testFixtures.ts` 升级后全部既有组件测试仍通过。

**验收命令与预期**：
```powershell
Set-Location frontend
npm run test -- --run
npm run build
```
预期：全部通过、构建成功。

---

### Task 3：C++ 合同校验 1.2 + 兼容策略改造

**修改文件**：`backend-cpp/src/contracts/AnnualInspectionContract.cpp`（+头文件、`tests/test_annual_inspection_contract.cpp`）；`backend-cpp/src/review/ContractCompatibility.cpp`（+头文件、`tests/test_contract_compatibility.cpp`）；`backend-cpp/src/http/ReviewRoutes.cpp`（标签透传）；`backend-cpp/tests/support/review_fixtures.hpp`（如需）。

**核心逻辑**：
- 校验器：`contract.version` 必须为 `"1.2"`；`defects[]` 增查 `defect_scale`（缺省或正整数）、`defect_deduction`（缺省或 0–100 数值）；`ratings.component_ratings` 必填数组，逐项校验：`candidate_id` 唯一非空、`component_ref` 对象、三个分值范围、状态枚举、Task 1 三条不变量、`deduction_defect_candidate_ids` 每项必须指向存在的 `defects[].candidate_id`（模式同 `validate_photo_relations`）、`calculation_details.ordered_deductions` 为降序数值数组（存在时）。
- `ContractCompatibility` 枚举改为 `{ Native12, LegacyPendingReparse, LegacyReadOnly }`：
  - version=="1.2" → `Native12`；
  - version ∈ {"1.0","1.1"} 且 `import_status=="待校对"` → `LegacyPendingReparse`（数据原样返回，**不做任何内存补造**；替代原 Upgraded10 行为，依据变更提案 001"重新解析而非补造"）；
  - 其余旧版本终态 → `LegacyReadOnly`。
  - 名称：`"native_1_2" / "legacy_pending_reparse" / "legacy_read_only"`。
- `ReviewRoutes.cpp` GET review：兼容标签进响应；`ReviewStatistics` 对 1.0/1.1 旧形状容错（缺 `component_ratings` 时按空数组统计）。保存草稿与确认路径经严格 1.2 校验，旧草稿自然被 `contract_validation_failed` 阻断，配合前端只读横幅。

**测试**：拒绝 1.1 版本；拒绝悬空 `deduction_defect_candidate_ids`；拒绝不变量违反；1.1 待校对 → `LegacyPendingReparse` 且 data 原样；1.1 已确认 → `LegacyReadOnly`；1.2 → `Native12`。

**验收命令与预期**：
```powershell
Set-Location backend-cpp
cmake --preset vs2022-x64-debug
cmake --build --preset vs2022-x64-debug
ctest --preset vs2022-x64-debug -R "AnnualInspectionContract|ContractCompatibility|ReviewModels|ReviewStatistics"
```
预期：全部通过。

---

### Task 4：Python 从表 2.x-1 抽取详细位置、标度、扣分与构件评分来源值

**修改文件**：`tools-python/bridge_report_tools/importers/defect_tables.py`；`word_importer.py`（组装 `component_ratings` 来源值）；`tools-python/tests/importers/docx_fixtures.py`（夹具表增加 标度/病害扣分/构件评分 列）；`tests/importers/test_defect_tables.py`、`test_word_importer.py`、`test_real_word_regression.py`。

**核心逻辑**：
- `defect_tables.py` 新增列定位：`scale_index = header_index(headers, ["标度"])`、`deduction_index = header_index(headers, ["病害扣分"])`、`component_score_index = header_index(headers, ["构件评分"])`；解析为 `int/float`，空单元格 → None；有内容但非数值 → 保留 None 并写对象级 warning（code `defect_scale_invalid` / `defect_deduction_invalid` / `component_score_source_invalid`）。
- 构件组传播：同一表内按连续相同 `(component_name, component_alias)` 行段分组；`构件评分` 在组内向下传播首个非空值（兼容"合并单元格被 python-docx 复制到每行"与"仅组首行有值"两种形态）；**每组只产出一条** `ComponentRatingCandidate`，`source_score`=组值，`deduction_defect_candidate_ids`=组内全部病害候选 id，`calculated_score=None`、`confirmed_score=None`、`score_validation_status="无法复算"`、`calculation_details=None`、`review_status="待确认"`（复算在 Task 5 接线后填充）。绝不把组评分复制成多条评分候选。
- `defect_location` 继续取 病害位置 列原文（已有逻辑，确认不动）；`severity` 保持 None，与标度完全隔离。

**测试**：夹具表含三新列时逐字段断言；组内两行病害只产出一条构件评分候选且引用两个病害 id；仅组首行有评分时传播正确；非数值标度写 warning 不中断；无三列的旧表仍可解析（字段为 None、`component_ratings` 仅在有构件评分列时产出）；真实回归（门控）：25/31/36/31 保持，`1-1#板` `defect_scale==2`、`defect_deduction==35`，`2-1#板` 两条病害扣分 {35,20}，`component_ratings` 中 `1-1#板/1-2#板 source_score==65`、`2-1#板 source_score==55.81`。

**验收命令与预期**：
```powershell
Set-Location tools-python
uv run pytest -q
$env:BRIDGE_REPORT_REAL_WORD_PATH="D:\vs2022 code\bridge-report-system\test-inputs\word-import\绕阳河二号桥报告b8e246e8-cd4d-4202-8d4d-49c56dd34389.docx"
uv run pytest tests/importers/test_real_word_regression.py -q
```
预期：全部通过，原基线数量不变。

---

### Task 5：JTG/T H21-2011 4.1.1 构件评分纯函数（三语言）+ 共享夹具一致性测试 + Python 复算接线

**修改文件**：新 `samples/scoring/component_score_cases.json`；新 `tools-python/bridge_report_tools/scoring/__init__.py`、`component_score.py`、`tests/test_component_score.py`；新 `backend-cpp/include/bridge_report/review/ComponentScore.hpp`、`src/review/ComponentScore.cpp`、`tests/test_component_score.cpp`、改 `backend-cpp/CMakeLists.txt`（core 库与测试各加一文件）；新 `frontend/src/review/componentScore.ts`、`componentScore.test.ts`；改 `tools-python/bridge_report_tools/importers/word_importer.py`（复算接线）及 `test_word_importer.py`、`test_real_word_regression.py`。

**核心逻辑**：
- 三语言同名语义 API：
  - `compute_component_score(deductions) -> { score: double(未舍入), ordered_deductions: 降序 }`；输入为空 → 无结果（Python 返回 None / C++ `std::optional` 空 / TS null）；任一 DP 不在 (0,100] → 无结果并给原因；任一 DP==100 → score=0。
  - `round2(x) = floor(x*100 + 0.5) / 100`（统一自实现，半数远离零）。
  - `classify_score_validation(source, calculated)`：二者皆有且 `round2` 相等 → `一致`；皆有不等 → `不一致`；calculated 缺 → `无法复算`。
- 计算顺序固定：降序排序后逐项累计，禁止中间 round；实现三端逐行同构，杜绝漂移。
- 共享夹具 `samples/scoring/component_score_cases.json`：字段 `{name, deductions(乱序输入), expected_unrounded, expected_rounded}`；用 Python 实现生成、人工复核 `[35,20]` 一例（`100 − 35 − 20/(100·√2)×65 = 55.8076118445748…`，round2=55.81）。用例至少含：`[35]`、`[35,20]`、`[20,35]`（与前者结果一致）、`[100]`、`[100,50]`、`[90,90]`、`[25,20,15]`、单条小扣分、空数组（预期无结果）。断言：`|computed − expected_unrounded| < 1e-9` 且 `round2(computed) == expected_rounded`。
- Python 消费：`Path(__file__)` 上溯到仓库根；C++ 消费：`BRIDGE_REPORT_REPOSITORY_ROOT / "samples" / "scoring"`；TS 消费：测试内 `node:fs` `readFileSync` 相对仓库根读取。
- `word_importer.py` 接线：对每条构件评分候选，取组内病害的非空 `defect_deduction` 复算；全部病害均有扣分且计算成功 → 填 `calculated_score`（未舍入）、`calculation_details{standard, ordered_deductions, rounding_scale:2}`、按 `classify` 定状态；`一致` 时预填 `confirmed_score=source_score`；`不一致/无法复算` 时 `confirmed_score=None`；任一病害缺扣分 → `无法复算`（保留来源分，不编造扣分）。

**测试**：三语言各自跑同一夹具全用例；Python 追加接线测试（组内缺扣分 → 无法复算；扣分齐全 → 一致/不一致分支）；真实回归（门控）新增：`1-1#板/1-2#板` `round2(calculated)==65` 且状态`一致`且 `confirmed_score==65`；`2-1#板` `ordered_deductions==[35,20]`、`round2(calculated)==55.81`、状态`一致`、`confirmed==55.81`。

**验收命令与预期**：
```powershell
Set-Location tools-python; uv run pytest -q
Set-Location ..\backend-cpp; cmake --build --preset vs2022-x64-debug; ctest --preset vs2022-x64-debug -R "ComponentScore"
Set-Location ..\frontend; npm run test -- --run src/review/componentScore.test.ts
```
预期：三端同夹具全部通过；真实回归含新断言通过。

---

### Task 6：模块 05 前端 —— 1.2 字段校对与评分差异处理 UI

**修改文件**：`frontend/src/review/reviewDraft.ts`（+`.test.ts`）；`grouping.ts`（+`.test.ts`）；`components/displayHelpers.ts`（+`.test.ts`）；`components/RatingsSection.tsx`；`components/DefectPhotoGroup.tsx`（+`.test.tsx`）；`components/DefectsSection.test.tsx`；`pages/ReviewWorkspacePage.tsx`（横幅接入，如 Task 2 未覆盖）。

**核心逻辑**：
- `reviewDraft.ts` 新 action：
  - `edit_defect_field` 增 `defect_scale`（`number|null`）、`defect_deduction`（`number|null`）两个变体（穷尽 switch 强制处理）。
  - `resolve_component_score { candidateId, choice: "accept_source" | "adopt_calculated", reason }`：写 `confirmed_score`（取来源分或 `round2(复算分)`）、状态置 `人工接受Word值/人工采用复算值`、存原因、`review_status` 置 `已修改`。
  - `reset_component_score_resolution { candidateId }`、`edit_component_rating_review_status`。
- 联动重算：任何影响某构件的 `defect_deduction` 编辑、病害 `review_status` 变化（如置 `已忽略`），reducer 定位 `component_ref` 匹配（structure_part+component_name+component_alias）的构件评分候选，用 `componentScore.ts` 重算 `calculated_score/ordered_deductions/deduction_defect_candidate_ids`（重算集合 = 该构件当前未忽略病害的非空扣分），重分类状态；除新状态为`一致`（预填来源分）外清空 `confirmed_score/score_resolution_reason`，`review_status` 退回 `待确认`。
- `DefectPhotoGroup.tsx` 的 defect-fact-grid 增加 `标度`、`病害扣分` 输入（编辑触发既有"组失效"规则）。
- `RatingsSection.tsx` 新增"构件评分"子表：构件（alias）/来源分/复算分/差值/校验状态徽章/最终确认分/原因/校对状态/操作（"接受 Word 值""采用复算值"带必填原因的弹窗；`一致` 时只读显示预填值）。旧 1.1 只读记录无该数组时不渲染（兼容标签已是只读）。
- `grouping.ts`：`needsAttention` 增加构件评分项——状态 `不一致` 或 `无法复算` 且未解决 → kind `"rating"`（`classifyCandidateId` 识别 `component_rating_` 前缀）；`isNormalRating` 扩展：构件评分仅当状态为 `一致` 或人工已解决且有原因时可入批量确认；`buildStatistics` 把 `component_ratings` 计入评分总数/待确认数（与 Task 7 的 `ReviewStatistics.cpp` 同步口径）。

**测试**：reducer——编辑扣分触发重算与状态迁移（一致→预填；不一致→确认清空）；忽略病害后扣分集合收缩重算；`resolve_component_score` 两分支与原因必填；grouping——不一致进"需要处理"、已解决不进；RatingsSection 渲染与操作交互测试；只读态全禁用。

**验收命令与预期**：
```powershell
Set-Location frontend
npm run test -- --run
npm run build
```
预期：全部通过、构建成功。

---

### Task 7：C++ 预检与确认映射修正 + 入库前独立复算（severity 永别 scale）

**修改文件**：`backend-cpp/include/bridge_report/review/ConfirmPlan.hpp`、`src/review/ConfirmPlan.cpp`（+`tests/test_confirm_plan.cpp`）；`src/review/PreflightReport.cpp`（+`tests/test_preflight_report.cpp`）；`src/review/ReviewStatistics.cpp`（+测试）；`src/db/ReviewRepository.cpp`（`insert_defect_observation` 列扩展）；`tests/test_review_repository.cpp`；`tests/support/review_fixtures.hpp`。

**核心逻辑**：
- `ConfirmPlan.cpp`：
  - 删除 `ConfirmPlan.cpp:213` 的 `defect_plan.scale = optional_string_member(defect, "severity")`；改为 `defect_scale`（整数 → 十进制字符串存入 `scale text` 列）；新增 `DefectPlan.defect_deduction: std::optional<double>`。
  - 新 `ComponentRatingPlan { normalized_component_key, rating_item_name(=alias，缺省用 component_name), structure_part, score(=confirmed_score), source_score, calculated_score, score_validation_status, score_resolution_reason, calculation_details_json, review_status }`；仅收 `review_status ∈ {已确认,已修改}` 的构件评分；`component_ref` 也加入 `plan.components` 构件沉淀集合（与病害同 key 规则），保证入库时能解析 `bridge_component_id`。
  - `defect_observations.component_score` 旧列不再写入（历史兼容只读）。
- `PreflightReport.cpp` 新阻断码（全部在既有 `candidate_pending_review` 扩展到 `component_ratings` 的基础上）：
  - `component_score_pending_resolution`：状态 ∈ {不一致, 无法复算} 的已确认/已修改构件评分；
  - `component_score_resolution_reason_missing`：人工状态但原因为空；
  - `component_score_recalc_mismatch`：**独立复算**——按 `deduction_defect_candidate_ids` 收集未忽略病害的扣分，调 `review::compute_component_score`，要求 `|recalc − calculated_score| ≤ 1e-6` 且状态与 `round2` 对比结论吻合（`无法复算` 且确无可算扣分时跳过复算校验）；
  - `component_score_deduction_incomplete`：同构件（structure_part+component_name+component_alias 匹配）存在未被引用、带扣分且未忽略的已定病害；
  - `component_score_defect_reference_invalid`：引用不存在或已忽略病害；
  - `component_rating_duplicate_component`：同一 component_ref 出现两条。
- `ReviewStatistics.cpp`：`component_ratings` 计入评分统计（与前端 Task 6 同口径）。
- `ReviewRepository.cpp::insert_defect_observation`：INSERT 列表增加 `defect_deduction`（`scale` 参数来源改为计划中的标度值）。

**测试**：ConfirmPlan——severity 不再进 scale（夹具设 severity="warning"、defect_scale=2，断言 plan.scale=="2"）；扣分透传；构件评分计划收敛规则。Preflight——六个新码各一个触发用例 + 全部解决后 `can_confirm=true`。DB 门控——确认后 `defect_observations.scale='2'`、`defect_deduction=35.00`，且不存在 scale ∈ ('info','warning','error') 的新行。

**验收命令与预期**：
```powershell
Set-Location backend-cpp
cmake --build --preset vs2022-x64-debug
ctest --preset vs2022-x64-debug -R "ConfirmPlan|PreflightReport|ReviewStatistics"
$env:BRIDGE_REPORT_TEST_DATABASE_URL="postgresql://bridge_report:bridge_report_dev@127.0.0.1:5432/bridge_report_system"
ctest --preset vs2022-x64-debug -R "ReviewRepository"
```
预期：全部通过；DB 未配置时相关用例 SKIP。

---

### Task 8：数据库迁移 003 + 冒烟测试

**修改文件**：新 `database/migrations/003_component_rating_validation_and_thread_binding.sql`；新 `database/tests/003_component_rating_archive_smoke.sql`；新 `scripts/dev/check-module06-db.ps1`（镜像 `check-module02-db.ps1`：依次应用 002、003 与两个冒烟脚本）；改 `README.md` 数据库检查小节。

**核心逻辑（全部幂等）**：
```sql
alter table condition_ratings
  add column if not exists source_score numeric(8, 2),
  add column if not exists calculated_score numeric(18, 10),
  add column if not exists score_validation_status text,
  add column if not exists score_resolution_reason text,
  add column if not exists calculation_details_json jsonb not null default '{}'::jsonb;

-- 枚举与构件绑定约束（DO 块检查 pg_constraint 后添加）：
--   score_validation_status is null or in ('一致','不一致','无法复算','人工接受Word值','人工采用复算值')
--   rating_level <> '构件' or bridge_component_id is not null

create unique index if not exists ux_condition_ratings_component_per_inspection
  on condition_ratings (inspection_year_id, bridge_component_id)
  where rating_level = '构件';

create index if not exists ix_defect_observations_thread
  on defect_observations (defect_thread_id)
  where defect_thread_id is not null;

-- 历史纠错（非猜测回填：仅清除已知语义错误值，severity 曾被写入 scale）
update defect_observations set scale = null where scale in ('info', 'warning', 'error');
```
- 迁移顺序：003 依赖 002；应用方式与现状一致（psql + `ON_ERROR_STOP=1`）；必须先于 Task 9 的 C++ 入库改造部署。
- 1.1 历史数据兼容策略（在计划与迁移注释中固化）：新 5 列对旧行保持 NULL/'{}'，读取侧据 `score_validation_status is null` 显示"历史数据缺少评分校验明细"；不 UPDATE 旧评分行；旧待校对草稿走重新解析，已确认年度补齐走同桥同年显式修订导入。
- 冒烟脚本（事务内构造 + 全部回滚，模式同 002 冒烟）：插入构件级评分行成功；同检测同构件第二行违反唯一索引；`rating_level='构件'` 且 `bridge_component_id is null` 违反 CHECK；非法 `score_validation_status` 违反 CHECK。

**测试/验收命令与预期**：
```powershell
pwsh scripts/dev/check-module06-db.ps1
pwsh scripts/dev/check-module06-db.ps1   # 幂等复跑
```
预期：两次均成功；冒烟脚本内的预期失败被捕获并回滚，库内无残留。

---

### Task 9：C++ 构件级 condition_ratings 事务写入

**修改文件**：`backend-cpp/src/db/ReviewRepository.cpp`（`insert_condition_rating` 扩展 + 构件评分写入循环）；`tests/test_review_repository.cpp`。

**核心逻辑**：
- `insert_condition_rating` 增补参数与列：`bridge_component_id`、`source_score`、`calculated_score`、`score_validation_status`、`score_resolution_reason`、`calculation_details_json`（jsonb）。
- `confirm_annual_facts` 第 6c 步之后：按 `ComponentRatingPlan.normalized_component_key` 从本事务 upsert 的构件 map 取 `bridge_component_id`，写 `rating_level='构件'` 行，`score=confirmed_score`；全桥/结构分部/部件行为不变（新列传 NULL）。
- 唯一索引兜底：违反 `ux_condition_ratings_component_per_inspection` 时事务整体回滚，返回 `db_write_failed`（预检的 `component_rating_duplicate_component` 为第一道防线）。
- `validation_result_json` 写入计数增加 `component_ratings_written`。

**测试（DB 门控）**：确认后存在 `rating_level='构件'` 行且 6 个新值全部落库、绑定正确 `bridge_component_id`；修订确认后新检测版本另有一行（不与旧版本冲突）；人为构造重复构件评分 → 事务回滚、无半截事实、导入记录仍为 `待校对`。

**验收命令与预期**：
```powershell
Set-Location backend-cpp
cmake --build --preset vs2022-x64-debug
$env:BRIDGE_REPORT_TEST_DATABASE_URL="postgresql://bridge_report:bridge_report_dev@127.0.0.1:5432/bridge_report_system"
ctest --preset vs2022-x64-debug -R "ReviewRepository|ConfirmAnnualFacts"
```
预期：全部通过。

---

### Task 10：模块 06 只读档案查询 API（C++）

**修改文件**：新 `ComponentArchiveRepository.{hpp,cpp}`、`ComponentArchiveRoutes.{hpp,cpp}`；改 `main.cpp`（注册）、`CMakeLists.txt`；新 `tests/test_component_archive_repository.cpp`（DB 门控，含种子构造）、`tests/test_component_archive_routes.cpp`（纯响应组装函数）。

**核心逻辑**：
- 端点（全 GET，注册方式同 `register_review_routes`，含 OPTIONS/CORS）：
  - `/api/bridges/{bridge_id}/components`：曾在当前有效版本有正式病害的构件列表。当前有效口径（本模块所有查询统一）：`inspection_years.is_current AND status='已确认'`，观测 `review_status IN ('已确认','已修改')`。返回：构件标识 + `thread_count`（当前有效观测的去重非空 `defect_thread_id` 数）+ `unbound_count` + `first_seen_year/latest_seen_year` + 最新年度构件评分（如有）。
  - `/api/bridge-components/{component_id}/defect-archive`：按线索组织——`component`、`ratings[]`（各当前有效年度的构件级 `condition_ratings`：score/source/calculated/status/reason/calculation_details，`has_validation_details = status is not null`，无则前端显示"历史数据缺少评分校验明细"）、`threads[]`（线索标准类型/标准位置 + `observations[]` 年度倒序，每条含年度实际位置、scale、defect_deduction、类型、描述、尺寸、照片 id/编号、review_status、system_number）、`unbound_observations[]` 独立数组。后端完成分组，前端不重组。
  - `/api/bridge-components/{component_id}/defect-archive/revisions`：`is_current=false`（含 `已被修订`）版本的观测，按 `inspection_year + version_number` 分组，只读，并带 `superseded_by` 当前版本信息；不含绑定数据操作。
  - `/api/bridges/{bridge_id}/unbound-defect-observations`：全桥未绑定当前有效观测（线索整理页数据源）。
  - `/api/defect-observations/{observation_id}/evidence`：`source_raw_cells_json`、表名/表序/行号、导入记录与归档文件系统编号。
  - `/api/defect-photos/{defect_photo_id}/content`：`defect_photos.archived_file_id → archived_files.storage_relative_path`，复用 `resolve_photo_content_path` + `newFileResponse`；错误码对齐既有照片接口（404 `defect_photo_not_found` / 409 `photo_archive_missing` / 400 `unsafe_archive_path`）。
- 空态均返回空数组 + 200；错误响应统一 `{code, message}`。

**测试**：纯函数——响应组装（线索分组、年度倒序、legacy 标志）；DB 门控——种子两年度 + 一旧修订 + 一条 1.1 风格旧评分行（新列 NULL），断言：最新年度无病害的构件仍在列表；旧修订不进默认统计与线索观测；未绑定观测独立返回；legacy 年度 `has_validation_details=false`；照片路径越界拒绝。

**验收命令与预期**：
```powershell
Set-Location backend-cpp
cmake --build --preset vs2022-x64-debug
ctest --preset vs2022-x64-debug -R "ComponentArchive"
$env:BRIDGE_REPORT_TEST_DATABASE_URL="postgresql://bridge_report:bridge_report_dev@127.0.0.1:5432/bridge_report_system"
ctest --preset vs2022-x64-debug -R "ComponentArchive"
```
预期：纯函数用例直接通过；DB 用例配置后通过、未配置 SKIP。

---

### Task 11：病害线索建议（纯函数 + 端点）

**修改文件**：新 `backend-cpp/include/bridge_report/review/ThreadSuggestions.hpp`、`src/review/ThreadSuggestions.cpp`、`tests/test_thread_suggestions.cpp`；`ComponentArchiveRepository/Routes` 增 `GET /api/defect-observations/{observation_id}/thread-suggestions`；`CMakeLists.txt`。

**核心逻辑**：
- 候选集：与观测同 `bridge_component_id` 的全部 `defect_threads`（跨构件绝不建议）。
- 纯函数 `suggest_threads(observation, threads) -> ranked[]`：
  - `normalize_text`：去首尾与内部空白、全角标点转半角（固定映射表：（）：，。；、－～ → 半角）、ASCII 小写化。
  - 匹配依据 `match_basis { same_component: true, same_defect_type, location_exact, location_contains }`（contains 指规范化后一方包含另一方）。
  - 得分 = 类型匹配×2 + 位置全等×1 + 位置包含×0.5；仅返回得分 > 0 的线索；排序：得分降序 → `latest_seen_inspection_id` 对应年份降序 → system_number。
- 响应包含线索信息 + `match_basis` + 得分；**只读建议，绝不写 `defect_thread_id`**，不持久化建议。

**测试**：同类型同位置 > 同类型异位置 > 异类型同位置；跨构件不进入；空线索集返回空数组；规范化用例（全角/空白/大小写）。

**验收命令与预期**：
```powershell
Set-Location backend-cpp
cmake --build --preset vs2022-x64-debug
ctest --preset vs2022-x64-debug -R "ThreadSuggestions"
```
预期：全部通过。

---

### Task 12：线索创建、绑定与重新绑定事务（C++）

**修改文件**：新 `DefectThreadRepository.{hpp,cpp}`、`DefectThreadRoutes.{hpp,cpp}`；改 `main.cpp`、`CMakeLists.txt`；新 `tests/test_defect_thread_repository.cpp`（DB 门控）、`tests/test_defect_thread_routes.cpp`（请求体校验纯函数）。

**核心逻辑**：
- `POST /api/defect-threads` body：`{ bridge_component_id, defect_type, defect_location, first_observation_id, expected_observation_updated_at, thread_name? }`。单事务：锁定首观测 → 走下述绑定校验 → 插入 `defect_threads`（`thread_name` 缺省 `defect_type + "｜" + defect_location`，`confirmation_status='人工已确认'`，`current_status='不确定'`——变化结论留给模块 07）→ 绑定首观测 → 计算 `first/latest_seen_inspection_id` → 提交（`newTransaction + CommitLatch`，同确认事务模式）。`defect_type`、`defect_location` 空 → 400 `thread_required_field_missing`。
- `PUT /api/defect-observations/{observation_id}/defect-thread` body：`{ defect_thread_id: uuid|null, expected_observation_updated_at, confirm_rebind: bool }`。事务步骤（严格按模块 06 规格 §9）：
  1. `SELECT ... FOR UPDATE` 锁定观测；
  2. 校验来自当前有效版本（join `inspection_years` `is_current AND status='已确认'`）→ 否则 409 `observation_not_current`；状态 ∈ {已确认,已修改} → 否则 409 `observation_not_formal`；
  3. `updated_at` 与 `expected_observation_updated_at` 逐字符比对 → 否则 409 `observation_revision_conflict`；
  4. 目标线索存在且同 `bridge_id`、同 `bridge_component_id` → 否则 409 `thread_component_mismatch`；
  5. 已有非空绑定且发生变更（换线索或解绑）时要求 `confirm_rebind=true` → 否则 409 `rebind_confirmation_required`；
  6. 事务内查 `defect_comparisons`：`confirmation_status='人工已确认'` 且引用该观测（previous 或 current）→ 409 `observation_referenced_by_confirmed_comparison`（模块 07 撤销前不可重绑）；
  7. 更新 `defect_thread_id` 与 `updated_at=now()`；
  8. 按当前有效绑定重算受影响线索（新旧两条）的 `first/latest_seen_inspection_id`（无绑定时置 NULL）；
  9. 提交；返回新 `updated_at` 作为下次令牌。
- "暂不确定"不产生任何服务端调用；未绑定不是错误状态。
- 新修订版观测天然 `defect_thread_id=null`（确认事务从不写它），满足"不自动继承"。

**测试（DB 门控为主）**：同构件绑定成功且首末年份更新；跨构件、过期令牌、旧修订版观测、待校对观测、未 confirm 的重绑各自返回既定错误码；两次并发（旧令牌重放）仅第一次成功；解绑后线索年份重算；被"人工已确认"对比引用时拒绝；创建线索缺类型/位置拒绝。

**验收命令与预期**：
```powershell
Set-Location backend-cpp
cmake --build --preset vs2022-x64-debug
ctest --preset vs2022-x64-debug -R "DefectThread"
$env:BRIDGE_REPORT_TEST_DATABASE_URL="postgresql://bridge_report:bridge_report_dev@127.0.0.1:5432/bridge_report_system"
ctest --preset vs2022-x64-debug -R "DefectThread"
```
预期：全部通过。

---

### Task 13：前端构件病害档案 A1 主页面

**修改文件**：新 `frontend/src/api/componentArchiveApi.ts`（+`.test.ts`：全部端点封装 + 类型 + `defectPhotoContentUrl`）；新 `frontend/src/pages/ComponentArchivePage.tsx`；新 `frontend/src/archive/` 下 `ComponentListPanel.tsx`、`ComponentDetailPanel.tsx`、`DefectThreadCard.tsx`、`ObservationYearRow.tsx`、`ComponentRatingSummary.tsx`、`RevisionHistoryPanel.tsx`（关键组件配 `.test.tsx`）；改 `App.tsx`（新路由 `/bridges/:bridgeId/components` 与 `/bridges/:bridgeId/components/:componentId` 指向同页；`:22` 工作台壳正则收紧为 `/\/imports\/[^/]+\/review$/`）；改 `BridgeDetailPage.tsx`（入口链接"构件病害档案"）；`styles.css`。

**核心逻辑**：
- A1 布局：左列表——构件类别/部件名称/构件编号、线索数、未绑定数、首见/末见年份；搜索框 + 结构分部筛选 + "存在未绑定观测"筛选（全部前端过滤已加载列表）。选中构件写入路由 `:componentId`。
- 右详情：构件基本信息 + `ComponentRatingSummary`（各年度 来源分/复算分/最终分/状态徽章/原因，展开显示 `calculation_details` 的降序扣分序列；`has_validation_details=false` 年度显示"历史数据缺少评分校验明细"，绝不在页面端猜测）→ `DefectThreadCard` 列表（标题 `病害类型｜标准位置：…`；卡片内年度倒序 `ObservationYearRow`：`年份 标度 x｜扣分 x｜尺寸…｜照片…`，展开显示年度实际位置原文、尺寸原文与结构化值、照片大图（`GET /api/defect-photos/{id}/content`）、来源证据（evidence 端点弹窗，复用 EvidencePanel 模式））→ 未绑定观测区（提示非错误、链接到线索整理页）。构件类别颜色复用 `CATEGORY_COLORS`/`--defect-category-color`。
- 历史修订入口：详情头部按钮打开 `RevisionHistoryPanel`，按 `年份 + v版本号` 分组只读展示并标注"已被修订，当前有效版本为 vN"；无任何绑定操作。
- 缺照片/缺评分/缺明细均正常渲染并给中性提示；数据加载遵循 useEffect+fetch+ApiError 模式。

**测试**：api 封装 URL 与解析；列表筛选与选中；线索卡年度倒序渲染；legacy 提示语渲染；修订面板只读（无按钮）；含未绑定区的空态。

**验收命令与预期**：
```powershell
Set-Location frontend
npm run test -- --run
npm run build
```
预期：全部通过、构建成功。

---

### Task 14：前端线索整理页 + 绑定/重绑交互

**修改文件**：新 `frontend/src/pages/DefectThreadReviewPage.tsx`；新 `frontend/src/archive/ThreadBindingCard.tsx`（+`.test.tsx`）；`componentArchiveApi.ts` 增 `fetchThreadSuggestions/createDefectThread/bindObservationThread`；改 `App.tsx`（路由 `/bridges/:bridgeId/defect-threads/review`）；`ComponentDetailPanel.tsx`（年度观测行增"重新绑定"入口，弹同一绑定对话框并强制确认）；`styles.css`。

**核心逻辑**：
- 页面数据源 `GET /api/bridges/{bridge_id}/unbound-defect-observations`；每条 `ThreadBindingCard` 显示年份/构件/类型/详细位置/尺寸/照片缩略图 + 建议列表（badge 展示 match_basis：同构件/同类型/位置全等/位置相近），三个动作：
  - **绑定已有线索**：从建议或该构件全部线索下拉中选择 → `PUT` 携带 `expected_observation_updated_at`；
  - **创建新线索**：弹窗预填标准病害类型与标准详细位置（可编辑后提交，位置必填）→ `POST /api/defect-threads`；
  - **暂不确定**：仅本地折叠该卡片，不发请求，不视为错误。
- 重绑路径：已绑定观测的"重新绑定"必须先勾选确认弹窗（`confirm_rebind=true`）；
- 错误码 → 中文提示映射（`observation_revision_conflict` → "该观测已被其他操作更新，请刷新后重试" 等六个稳定码 + 通用兜底）；成功后本地刷新未绑定列表与令牌。

**测试**：建议渲染与排序展示；绑定成功回调移除卡片；409 冲突提示与刷新引导；创建弹窗必填校验；暂不确定不触发请求（spy）；重绑确认门。

**验收命令与预期**：
```powershell
Set-Location frontend
npm run test -- --run
npm run build
```
预期：全部通过、构建成功。

---

### Task 15：全量回归、生产构建、真实 Word 回归与文档收口

**修改文件**：`tools-python/tests/importers/test_real_word_regression.py`（新增精确断言，见 Task 4/5，含 `evaluation_parts` 中上部承重构件 `part_score==86.62`、`overall.total_score==85.61`、`overall_grade=="2类"` 显式断言）；`PROJECT_CONTEXT.md`（当前进度 + 模块 06 状态）；`docs/superpowers/specs/modules/03/04/05/06-*.md` 变更记录（实施完成条目 + `candidate_id` 补充字段说明）；`docs/superpowers/specs/changes/2026-07-13-change-001-*.md` 状态 → 已实施；`README.md`（模块 06 启动/验证步骤 + 003 迁移说明）。

**核心逻辑与验证序列**：
```powershell
# 1. Python 全量 + 真实 Word 回归
Set-Location tools-python
uv run pytest -q
$env:BRIDGE_REPORT_REAL_WORD_PATH="D:\vs2022 code\bridge-report-system\test-inputs\word-import\绕阳河二号桥报告b8e246e8-cd4d-4202-8d4d-49c56dd34389.docx"
uv run pytest tests/importers/test_real_word_regression.py -q

# 2. C++ 全量（无 DB / 有 DB）
Set-Location ..\backend-cpp
cmake --preset vs2022-x64-debug
cmake --build --preset vs2022-x64-debug
ctest --preset vs2022-x64-debug
$env:BRIDGE_REPORT_TEST_DATABASE_URL="postgresql://bridge_report:bridge_report_dev@127.0.0.1:5432/bridge_report_system"
ctest --preset vs2022-x64-debug --output-on-failure

# 3. 前端全量 + 生产构建
Set-Location ..\frontend
npm run test -- --run
npm run build

# 4. 迁移幂等复验
pwsh ..\scripts\dev\check-module06-db.ps1
```
- 手工 E2E（延续模块 05 验收方式：PostgreSQL → Python 18081 → C++ 18080 → 前端 5173）：真实 Word 走 parse-word → 校对工作台可编辑标度/扣分/详细位置，构件评分显示 65/65/55.81 且状态`一致`；人为改一处扣分制造`不一致`→ 预检阻断 → 显式选择+原因后放行；确认入库后查库验证 `defect_observations.scale/defect_deduction` 与 `condition_ratings` 构件行三值；进入 `/bridges/:id/components` 验证 A1 页面、照片、证据、历史修订入口；线索整理页完成一次创建 + 一次绑定 + 一次被拒的过期令牌重试。
- 预期：全部命令零失败；真实 Word 基线 25/31/36/31/15 不变，五个新精确断言通过。

---

## 迁移顺序与 1.1 历史数据兼容策略（汇总）

| 项 | 策略 |
|---|---|
| 迁移顺序 | 仅新增 `003_component_rating_validation_and_thread_binding.sql`，依赖 002，psql 手工应用（`check-module06-db.ps1`），幂等可复跑；必须先于 Task 9 及之后代码部署 |
| 已确认 1.1 事实（DB） | 保持可读；新 5 列为 NULL → 档案页显示"历史数据缺少评分校验明细"；不做任何回填 UPDATE |
| scale 历史纠错 | 仅 `update ... set scale=null where scale in ('info','warning','error')`——清除已知 severity 误写值，非猜测回填（若无误写行则为空操作） |
| 待校对 1.0/1.1 草稿 | `legacy_pending_reparse` 只读；经既有 `POST parse-word` 重新解析为 1.2（不静默补造字段） |
| 已确认 1.1 年度补齐 | 走同桥同年显式修订版重新导入（既有模块 05 流程），新修订不继承旧线索绑定 |
| 前端旧数据 | `contract_compatibility` 标签驱动横幅与只读；档案页按 `has_validation_details` 区分新旧 |

## 依赖关系与建议实施批次

```text
批次 1（合同，串行起步后并行）：T1 → T2 ∥ T3
批次 2（解析与评分）：T4 ∥ T5(纯函数+夹具)；T5(Python 接线) 依赖 T4
批次 3（模块 05 与库）：T6(依赖 T2,T5-TS) ∥ T7(依赖 T3,T5-C++) ∥ T8(独立)；T9 依赖 T7+T8
批次 4（模块 06 后端）：T10(依赖 T8，数据侧依赖 T9) → T11 ∥ T12
批次 5（模块 06 前端）：T13(依赖 T10) → T14(依赖 T11,T12,T13)
批次 6（收口）：T15 依赖全部
```
- 硬前置：T1 是一切合同工作的闸门（extra="forbid" 使四端必须同批升级）；T8 是 T9/T10 的数据前置；T15 永远最后。
- 可并行组：{T2,T3}、{T4,T5 前半}、{T6,T7,T8}、{T11,T12}。

## 主要风险

1. **构件评分组传播**：真实 Word 合并单元格在 python-docx 中呈"复制到每行"或"仅首行有值"两种形态，Task 4 双形态兼容 + 真实回归钉住；一旦样例出现第三形态，回归会精确失败。
2. **跨语言浮点/舍入漂移**：以共享夹具 + 统一自实现 `round2` + 1e-9 容差防御；排序与累计顺序三端固定。
3. **extra="forbid" 全链锁死**：1.2 四端必须同批合入（批次 1 原子），期间旧待校对草稿被判只读是预期行为，不是回归。
4. **前后端统计口径**：`component_ratings` 同时计入前端 `buildStatistics` 与 C++ `ReviewStatistics`（T6/T7 同批交付），否则"需要处理/待确认"数量错位。
5. **绑定并发**：真实并发依赖 `FOR UPDATE` + `updated_at` 令牌；自动化测试用过期令牌重放模拟，真并发路径靠事务原语保证。
6. **无迁移框架**：003 幂等性（`if not exists` + DO 块）必须严格，冒烟脚本双跑验证。

## 自审清单（对照设计要求）

- [x] 十个阶段全部映射到任务（阶段→任务：1→T1；2→T1/T2/T3；3→T4；4→T5；5→T6/T7；6→T8/T9；7→T10；8→T11/T12；9→T13/T14；10→T15）。
- [x] 每任务含修改文件、核心逻辑、测试、验收命令、预期结果；无 TODO/TBD/模糊后续处理。
- [x] 主页面只读优先；病害一级、年度二级（T10 响应结构 + T13 A1 布局）。
- [x] 两层位置语义（T10/T13/T14 均区分标准位置与年度实际位置）。
- [x] 只建议不自动确认（T11 纯只读；T12 仅人工触发）。
- [x] 模块 06 只改线索与绑定、不改年度事实、不出对比结论（T12 无观测字段更新；current_status 不写结论值）。
- [x] 当前有效口径 + 旧修订独立只读 + 新修订不继承绑定（T10 revisions 端点；确认事务不写 thread id）。
- [x] severity 永不写 scale（T7 删除映射 + 校验 + T8 历史纠错 + DB 门控断言）。
- [x] 只用 Word 已给 DP，不反推（T4 无扣分即"无法复算"，不编造）。
- [x] 不一致/无法复算 ⇒ confirmed_score 初始为空 + 显式选择 + 必填原因（T1 不变量、T5 解析、T6 UI、T7 预检四层）。
- [x] 1.1 可读不回填（迁移策略表 + T10 legacy 标志 + T2/T3 兼容标签）。
- [x] 评分测试五项数值要求全覆盖（T5 夹具）；真实 Word 基线与五个新增精确断言（T4/T5/T15）。
- [x] 绑定事务九步、并发、下游已确认对比引用校验（T12）。
- [x] 图片/证据只走受控接口（T10 content/evidence + T13 前端仅用 URL 构造器）。
- [x] 稳定错误码 + 用户可操作信息（T10/T12 错误码表 + T14 中文映射）。
- [x] 数据库迁移顺序与幂等、smoke、应用脚本（T8）。
- [x] 单元/集成/生产构建/真实 Word 全部收口（T15）。

## 变更记录

| 日期 | 变更 | 原因 | 影响模块 |
|---|---|---|---|
| 2026-07-13 | 创建模块 06 实施计划（含合同 1.2 跨模块升级） | 变更提案 001 与模块 06 设计均已确认，进入实施准备 | 03、04、05、06 |
