# 模块 06.5 实施计划：桥梁档案、年度工作台与导入入口重组

> 日期：2026-07-15
>
> 设计真源：`docs/superpowers/specs/modules/06-5-bridge-centric-interaction-redesign.md`
>
> 实施分支：`codex/06-5-interaction-redesign`
>
> 基线提交：`759c36d`（设计与现有 Word 导入输入已对齐）
>
> 状态：已实施（2026-07-15）

## 1. Context

模块 01～06 已完成：模块 05 提供经过真实 Word 回归验证的全屏校对工作台，模块 06 提供只读构件病害档案和病害线索整理。模块 06.5 只重组它们的入口、页面层级和返回路径，并补齐“桥梁内新建年度 → 上传 Word → 调用现有解析 → 进入校对”的受控业务入口。

本计划不重写模块 05、06，不改变 `BridgeAnnualInspectionData` 1.2，不建设第二种导入格式或多来源合并。

## 2. 已核实现状

1. `App.tsx` 当前把 `/` 指向健康检查页，把桥梁列表和桥梁详情作为独立普通页面。
2. `BridgeDetailPage.tsx` 分别拉取年度和全桥导入记录，并把两张表跨年度平铺。
3. `ReviewWorkspacePage.tsx` 已是独立全屏工作台；`App.tsx` 通过 `/imports/.../review` 正则启用 `app-shell-workbench`。
4. 校对页已拆分 `OverviewHeader`、`ReviewSidebar`、`DefectsSection`、`RatingsSection`、`ReviewActionBar` 等组件，应直接复用。
5. `ReviewRepository` 已有桥梁、年度、导入记录列表查询；导入摘要已带活动编辑锁。
6. `WordImportRoutes` 只支持对已经存在且已归档主 Word 的导入记录执行 `POST .../parse-word`，前端尚无创建导入记录和上传文件入口。
7. `ArchivePaths` 已有 `build_import_input_relative_path`，模块 06.5 应复用它归档原始 Word。
8. 迁移 002 已有：
   - `ux_inspection_years_current_bridge_year`，保证同桥同年只有一条当前记录；
   - `inspection_years`、`archived_files`、`import_records`、`import_record_files` 所需字段；
   - 安全相对路径约束和桥梁/年度/状态索引。
9. 因此本模块预计不新增数据库迁移；创建年度冲突由既有部分唯一索引兜底。
10. 模块 04 规定：检测年度、检查日期、报告编号和项目名称以系统输入为准；Word 只校验桥名，不自动决定年度。

## 3. Global Constraints

实施全程必须遵守：

1. 不提交或删除 `.claude/`、`test-inputs/`、`test-output/`、`backend-cpp/archive/`、`tools-python/archive/`。
2. `.superpowers/` 已由 `.gitignore` 忽略，不提交 brainstorming 线框稿会话文件。
3. 所有正式文件读取仍经 C++ 受控接口，前端和响应体不暴露绝对归档路径。
4. 年度工作台不得取得编辑锁；只有进入模块 05 校对工作台时才申请锁。
5. 未保存校对内容在异常关闭后继续按已确认方案直接丢失，不新增自动保存或恢复。
6. 已确认、已取消、旧版终态继续只读；重开、修订确认和 revision 竞态保护不变。
7. 新摘要接口只读取当前有效年度事实；旧修订版不得混入最新评分、历年趋势和病害计数。
8. 当前只接收 `.docx`；`.doc`、PDF、Excel、图片包、CSV、JSON 全部拒绝。
9. 前端只调用 C++ 主服务，不直接调用 Python。
10. 每个任务先补测试或失败断言，再完成最小实现；任务结束运行相关测试后再提交。

## 4. 固定回归基线

真实辽宁国省干线 Word：

- 25 条病害；
- 31 个照片候选；
- 36 个 Word 图片；
- 31 张归档照片；
- 15 个评分项。

实施前基线：

- Python：115 项通过、1 项跳过；
- 前端：222 项通过；
- C++ + PostgreSQL：271 项通过；
- 前端生产构建通过；
- 真实 Word 回归通过。

测试数量会因新增用例增加，验收只要求不减少既有覆盖且全部通过。

## 5. API 合同

### 5.1 扩展桥梁列表

```http
GET /api/bridges
```

在现有兼容字段后增加可空字段：

```json
{
  "id": "...",
  "system_number": "QL-000001",
  "bridge_name": "绕阳河二号桥",
  "route_name": "G305",
  "status": "在用",
  "latest_inspection_year": 2026,
  "latest_overall_score": 85.61,
  "latest_overall_grade": "2类",
  "pending_count": 3
}
```

没有正式年度时三个 `latest_*` 字段为 `null`，不是 0 或空字符串。

### 5.2 桥梁概览

```http
GET /api/bridges/{bridge_id}/overview
```

响应分为：`bridge`、`latest_inspection`、`recent_inspections`、`pending`、`defect_archive`。评分摘要使用数组返回已有结构层级，不把“上部/下部/桥面系”硬编码为必有字段。

### 5.3 年度工作台

```http
GET /api/inspection-years/{inspection_year_id}/workspace
```

响应包含：

- `bridge`；
- `inspection_year`；
- `imports[]`：导入摘要、`statistics`、活动锁和 `available_action`；
- `pending` 汇总。

`available_action` 只允许：`parse`、`continue_review`、`view_result`、`none`。它是后端根据现有状态给出的提示，前端仍按实际 API 错误处理并发变化。

### 5.4 创建年度

```http
POST /api/bridges/{bridge_id}/inspection-years
Content-Type: application/json

{"inspection_year": 2026}
```

成功返回 201 和新年度摘要。已存在同桥同年当前记录时返回 409：

```json
{
  "code": "inspection_year_already_exists",
  "message": "该桥梁已经存在 2026 年度检测。",
  "existing_inspection_year_id": "..."
}
```

前端收到该错误直接进入现有年度，不创建重复记录。

### 5.5 上传 Word

```http
POST /api/inspection-years/{inspection_year_id}/import-records/word
Content-Type: multipart/form-data
```

字段：

- `file`：唯一 `.docx` 文件；
- `source_type`：`软件导出Word` 或 `正式Word`。

成功返回 201：`import_record` 和 `archived_file` 的安全业务摘要，不返回 `storage_relative_path`。

上传只完成受控归档和导入记录创建，随后前端调用现有：

```http
POST /api/import-records/{import_record_id}/parse-word
```

解析请求固定：

```json
{
  "rule_profile": "辽宁国省干线",
  "import_mode": "已有桥年度导入",
  "file_role": "当前年度检测资料",
  "data_role": "当前年度",
  "inspection_date": "2026-05-18",
  "report_number": "Q202605001-JZ-024",
  "project_name": "绕阳河二号桥2026年度定期检测"
}
```

## 6. File Map

### 6.1 C++ 新增

- `backend-cpp/include/bridge_report/review/WorkspaceModels.hpp`
- `backend-cpp/src/review/WorkspaceModels.cpp`
- `backend-cpp/include/bridge_report/db/WorkspaceRepository.hpp`
- `backend-cpp/src/db/WorkspaceRepository.cpp`
- `backend-cpp/include/bridge_report/http/WorkspaceRoutes.hpp`
- `backend-cpp/src/http/WorkspaceRoutes.cpp`
- `backend-cpp/include/bridge_report/archive/WordInputArchive.hpp`
- `backend-cpp/src/archive/WordInputArchive.cpp`
- `backend-cpp/tests/test_workspace_models.cpp`
- `backend-cpp/tests/test_workspace_repository.cpp`
- `backend-cpp/tests/test_workspace_routes.cpp`
- `backend-cpp/tests/test_word_input_archive.cpp`

### 6.2 C++ 修改

- `backend-cpp/include/bridge_report/review/ReviewModels.hpp`
- `backend-cpp/src/review/ReviewModels.cpp`
- `backend-cpp/include/bridge_report/db/ReviewRepository.hpp`
- `backend-cpp/src/db/ReviewRepository.cpp`
- `backend-cpp/src/http/ReviewRoutes.cpp`
- `backend-cpp/src/main.cpp`
- `backend-cpp/CMakeLists.txt`
- `backend-cpp/tests/test_review_models.cpp`
- `backend-cpp/tests/test_review_repository.cpp`
- `backend-cpp/tests/test_word_import_routes.cpp`
- `config/local.example.json`、`backend-cpp/include/bridge_report/config/AppConfig.hpp`、`backend-cpp/src/config/AppConfig.cpp`、`backend-cpp/tests/test_app_config.cpp`（若把上传上限配置化）

### 6.3 前端新增

- `frontend/src/api/workspaceApi.ts`、`workspaceApi.test.ts`
- `frontend/src/workspace/workspaceState.ts`、`workspaceState.test.ts`
- `frontend/src/workspace/BridgeWorkspaceShell.tsx`、`BridgeWorkspaceShell.test.tsx`
- `frontend/src/workspace/InspectionYearRail.tsx`、`InspectionYearRail.test.tsx`
- `frontend/src/workspace/InspectionProgress.tsx`、`InspectionProgress.test.tsx`
- `frontend/src/workspace/ImportSourceCard.tsx`、`ImportSourceCard.test.tsx`
- `frontend/src/workspace/CreateInspectionDialog.tsx`、`CreateInspectionDialog.test.tsx`
- `frontend/src/workspace/ImportWordDialog.tsx`、`ImportWordDialog.test.tsx`
- `frontend/src/pages/BridgeOverviewPage.tsx`、`BridgeOverviewPage.test.tsx`
- `frontend/src/pages/InspectionWorkspacePage.tsx`、`InspectionWorkspacePage.test.tsx`
- `frontend/src/App.test.tsx`

### 6.4 前端修改

- `frontend/src/App.tsx`
- `frontend/src/pages/BridgesPage.tsx`（新增测试）
- `frontend/src/pages/BridgeDetailPage.tsx`（由新概览替代后删除或收缩为兼容转发，二选一；不得保留两套详情逻辑）
- `frontend/src/pages/ReviewWorkspacePage.tsx`
- `frontend/src/pages/ComponentArchivePage.tsx`
- `frontend/src/pages/DefectThreadReviewPage.tsx`
- `frontend/src/review/components/OverviewHeader.tsx`
- `frontend/src/review/components/ReviewActionBar.tsx`（及测试，如返回按钮文案改为可传入）
- `frontend/src/api/reviewApi.ts`、`reviewApi.test.ts`（新增 parse-word 客户端）
- `frontend/src/styles.css`

### 6.5 文档

- `README.md`
- `PROJECT_CONTEXT.md`
- 本设计文档状态和本实施计划变更记录

## 7. 任务清单（13 个任务）

### Task 1：工作区响应模型与纯组装函数

**文件**：新增 `WorkspaceModels.hpp/.cpp`、`test_workspace_models.cpp`；修改 `CMakeLists.txt`。

**核心逻辑**：

- 定义桥梁概览、年度工作台、待办摘要和导入统计的 C++ 数据结构。
- `to_json()` 明确 `null` 与空数组语义；没有最新年度时 `latest_inspection=null`。
- 导入统计直接接收现有 `ReviewStatistics`，不重新实现候选计数。
- 定义 `derive_available_action(import_status)` 纯函数：
  - `已上传`、`解析失败` → `parse`；
  - `待校对` → `continue_review`；
  - `已确认`、`已取消` → `view_result`；
  - `解析中` → `none`。
- 不在 C++ 模型中生成前端文案或颜色。

**测试**：完整/缺省 JSON；空年度；统计透传；每种导入状态到动作的映射。

**验收**：

```powershell
Set-Location backend-cpp
cmake --build --preset vs2022-x64-debug
.\build\vs-debug\Debug\bridge_report_backend_tests.exe --gtest_filter=WorkspaceModelsTest.*
```

**建议提交**：`test(workspace): define bridge and annual summary contracts`

---

### Task 2：桥梁列表扩展与桥梁概览查询

**文件**：修改 `ReviewModels.*`、`ReviewRepository.*`、`test_review_models.cpp`、`test_review_repository.cpp`；新增/修改 `WorkspaceRepository.*`、`test_workspace_repository.cpp`。

**核心逻辑**：

- 扩展 `BridgeSummary` 可空最新年度、综合评分、等级和待办数量，保持旧字段不变。
- `list_bridges()` 只取当前有效年度；最新年度优先最新年份，不混入 `已被修订`。
- 待办数量第一版口径：`待校对/已上传/解析中/解析失败` 导入记录数 + 当前有效正式未绑定观测数。
- `get_bridge_overview(bridge_id)` 返回：
  - 桥梁基本信息；
  - 最新当前有效年度；
  - 最新年度全桥评分和已有结构层级评分；
  - 最近当前有效年度；
  - 导入待办和未绑定观测计数；
  - 有正式病害事实的构件数、线索数、未绑定观测数。
- 无桥梁返回 `nullopt`，空档案返回合法空摘要。

**数据库测试**：当前版/旧修订版并存时只选当前版；无评分返回 null；活动与过期锁/待办口径；病害和线索计数不重复。

**验收**：

```powershell
$env:BRIDGE_REPORT_TEST_DATABASE_URL='postgresql://bridge_report:bridge_report_dev@127.0.0.1:5432/bridge_report_system'
.\build\vs-debug\Debug\bridge_report_backend_tests.exe '--gtest_filter=ReviewModelsTest.*:ReviewRepositoryTest.*:WorkspaceRepositoryTest.Bridge*'
```

**建议提交**：`feat(workspace): query bridge archive overview`

---

### Task 3：年度工作台查询

**文件**：`WorkspaceRepository.*`、`WorkspaceModels.*`、`test_workspace_repository.cpp`。

**核心逻辑**：

- `get_inspection_workspace(inspection_year_id)` 联查桥梁和年度，并验证年度存在。
- 导入记录只取该年度，不再在前端从全桥列表中过滤。
- 每条导入读取 `parsed_result_json` 并调用现有 `build_statistics`；空对象、旧合同和解析失败数据按现有容错规则返回零统计，不抛出页面级异常。
- 复用 `import_record_edit_locks` 的未过期锁口径。
- 返回所有状态导入记录；取消记录仍可查看，但不计入需要处理。
- 年度主状态不由导入记录静默改写；页面阶段由前端派生。

**测试**：年度归属；多年度隔离；统计正确；解析失败；活动锁；已取消不计待办；未知 UUID 返回空。

**验收**：运行 `WorkspaceRepositoryTest.Inspection*` 和 `WorkspaceModelsTest.*`。

**建议提交**：`feat(workspace): query annual inspection workspace`

---

### Task 4：工作区只读路由

**文件**：新增 `WorkspaceRoutes.hpp/.cpp`、`test_workspace_routes.cpp`；修改 `main.cpp`、`CMakeLists.txt`。

**路由**：

- `GET /api/bridges/{bridge_id}/overview`
- `GET /api/inspection-years/{inspection_year_id}/workspace`

**核心逻辑**：

- 注册 OPTIONS；UUID 非法与记录不存在均返回稳定 404，不泄漏数据库细节。
- 数据库不可用沿用 `respond_db_unavailable`。
- 纯响应组装与状态映射在模型层测试，路由只做参数校验、调用和状态码。
- `/api/bridges` 保留在现有 `ReviewRoutes`，只扩展响应字段，不重复注册。

**测试**：非法 UUID、404、正常响应形状、数据库异常映射；路由注册无冲突。

**验收**：`WorkspaceRoutesTest.*` + `ReviewModelsTest.*`。

**建议提交**：`feat(api): expose bridge and annual workspaces`

---

### Task 5：创建年度检测事务与路由

**文件**：`WorkspaceRepository.*`、`WorkspaceRoutes.*`、对应测试。

**核心逻辑**：

- 写路由首先 `authenticate_request`，未登录返回 401。
- 校验桥梁 UUID、JSON、`inspection_year` 范围 1900～2200。
- 单事务：验证桥梁存在 → 插入 `inspection_years`（状态 `待校对`、版本 1、当前）→ 返回摘要。
- 捕获 `ux_inspection_years_current_bridge_year` 冲突，查询现有年度 ID 并返回 409 `inspection_year_already_exists`。
- 并发双请求必须只创建一条当前年度；不能依赖“先查再插”的竞态窗口。
- 本任务不创建 revision；同桥同年修订仍由模块 05 确认流程处理。

**测试**：成功创建；桥梁不存在；非法年份；未登录；重复创建；两个连接竞争时只有一条当前年度。

**验收**：`WorkspaceRepositoryTest.CreateInspection*`、`WorkspaceRoutesTest.CreateInspection*`。

**建议提交**：`feat(workspace): create annual inspection tasks`

---

### Task 6：Word 原始文件受控归档与上传路由

**文件**：新增 `WordInputArchive.hpp/.cpp`、`test_word_input_archive.cpp`；修改 `WorkspaceRepository.*`、`WorkspaceRoutes.*`、配置文件（若配置化上传上限）、对应测试。

**核心逻辑**：

- 使用 Drogon multipart 解析器，只接受一个名为 `file` 的文件和一个合法 `source_type`。
- 文件名只用于展示和安全归档路径；拒绝空文件名、非 `.docx`（大小写不敏感）、空文件和超过上限的文件。
- 默认上限建议 256 MiB；若放入 `AppConfig`，配置项为 `archive.word_upload_max_bytes`，示例配置和默认值同步测试。
- 计算 SHA-256、文件大小和小写扩展名。
- 复用 `build_import_input_relative_path` 和 `resolve_path_under_root`，先写临时文件，再原子移动到最终路径；创建父目录。
- 数据库事务创建并关联：
  - `import_records`：桥梁和年度来自可信数据库上下文，状态 `已上传`；
  - `archived_files`：`Word文档`、原文件名、哈希和安全相对路径；
  - `import_record_files`：角色 `主报告`；
  - `import_records.main_file_id`。
- 文件系统与数据库采用明确补偿：任何 SQL/提交失败删除本次最终文件；文件移动失败则事务回滚；只能删除本次创建且已验证位于 archive 根内的路径。
- 不在响应中返回绝对路径或 `storage_relative_path`。
- 上传成功不直接写候选 JSON和正式事实。

**错误码**：`invalid_word_file`、`word_file_too_large`、`inspection_year_not_found`、`inspection_year_not_current`、`word_archive_failed`。

**测试**：安全路径、中文文件名、大小写扩展、路径字符清理、非 docx、过大、年度不存在/非当前、事务失败清理、文件移动失败回滚、成功三表关联、响应不泄漏路径。

**验收**：

```powershell
$env:BRIDGE_REPORT_TEST_DATABASE_URL='postgresql://bridge_report:bridge_report_dev@127.0.0.1:5432/bridge_report_system'
.\build\vs-debug\Debug\bridge_report_backend_tests.exe '--gtest_filter=WordInputArchiveTest.*:WorkspaceRepositoryTest.UploadWord*:WorkspaceRoutesTest.UploadWord*'
```

**建议提交**：`feat(import): upload and archive annual Word reports`

---

### Task 7：上传后解析闭环与重试语义

**文件**：`WordImportRoutes.cpp`、`WordImportRepository.*`（仅必要的小改动）、`test_word_import_routes.cpp`、`test_word_import_repository.cpp`；前端 API 接线在 Task 8。

**核心逻辑**：

- 确认新上传记录能被现有 `load_context()` 加载：主文件、桥梁、当前年度和来源类型完整。
- 现有 `POST .../parse-word` 继续是唯一 Python 调用入口，不把 Python 调用复制进上传路由。
- `已上传` 和 `解析失败` 可解析/重试；`解析中`、`待校对`、终态按现有规则拒绝重复解析。
- 解析请求仍要求检查日期、报告编号和项目名称；年度与桥梁来自数据库上下文。
- 上传成功而解析失败时保留导入记录和原 Word，状态 `解析失败`；用户可从年度工作台重试，不重复上传文件。
- Python 返回候选后继续走现有合同校验、图片归档和 `persist_parse_result`，回归数值不变。

**测试**：真实上传上下文可解析；解析失败保留原文件；重试从 `解析失败` 回到 `解析中/待校对`；错误状态拒绝；桥名未识别警告原样进入草稿。

**建议提交**：`test(import): close uploaded Word parse workflow`

---

### Task 8：前端工作区 API 与状态派生

**文件**：新增 `workspaceApi.ts`、`workspaceState.ts` 及测试；修改 `reviewApi.ts` 及测试。

**核心逻辑**：

- 定义桥梁列表扩展、概览、年度工作台、创建年度和上传响应类型。
- API：`fetchBridgeOverview`、`fetchInspectionWorkspace`、`createInspectionYear`、`uploadWordImport`。
- `uploadWordImport` 使用 `FormData`，不得手工设置 multipart `Content-Type` 边界；认证头继续由通用客户端添加。
- `reviewApi.ts` 新增 `parseWordImport`，请求体严格对应模块 04 现有字段。
- `deriveInspectionProgress` 纯函数只根据年度和导入状态生成展示阶段；不向后端写新状态。
- `inspectionWorkspacePath`、`reviewPath` 等路径生成函数统一 URL 编码，页面不手拼路由。
- 稳定错误码映射为中文可操作提示；保留未知错误后端 message。

**测试**：URL 编码、FormData、创建/冲突错误、解析请求字段、阶段映射、空年度/取消导入/解析失败/已归档。

**验收**：

```powershell
Set-Location frontend
npm run test -- --run src/api/workspaceApi.test.ts src/workspace/workspaceState.test.ts src/api/reviewApi.test.ts
```

**建议提交**：`feat(frontend): add bridge workspace API contracts`

---

### Task 9：全局导航、桥梁列表与桥梁工作区外壳

**文件**：`App.tsx`、新增 `App.test.tsx`、`BridgesPage.tsx` 及测试、`BridgeWorkspaceShell.tsx` 及测试、`styles.css`。

**核心逻辑**：

- `/` 使用 `<Navigate replace to="/bridges" />`；健康检查页不再是业务入口，后端健康端点和客户端代码保留供诊断。
- 全局导航只显示“桥梁档案”和用户信息，消除“首页/桥梁列表”重复。
- 桥梁列表增加即时搜索，字段：桥名、编号、路线；显示最新年度、等级和待办。
- `BridgeWorkspaceShell` 拉取桥梁概览，渲染面包屑、桥名、编号、路线、状态和三个可用一级导航：概览、年度检测、构件病害档案。
- 使用嵌套路由/`Outlet` 共享外壳；全屏校对路由保持外壳之外。
- 加载、404、请求失败、搜索无结果均显示明确页面和重试/返回动作，禁止空白页。

**测试**：根重定向；导航项；搜索；列表空/错；共享桥梁标题；当前 tab；未知桥梁。

**建议提交**：`feat(ui): establish bridge-centered navigation`

---

### Task 10：桥梁概览页面

**文件**：新增 `BridgeOverviewPage.tsx` 及测试；删除或收缩 `BridgeDetailPage.tsx`；修改样式。

**核心逻辑**：

- 依次显示桥梁身份、最新结论、待办、历年技术状况和病害概况。
- 最新结论只显示 API 返回的正式当前有效事实；缺项显示“—”。
- 待校对/解析失败导入链接到所属年度工作台；未绑定观测链接到构件档案内的线索整理入口。
- “新建年度检测”打开 Task 11 对话框。
- 最近年度点击后进入对应 URL；不在概览页展开完整导入记录。
- 不实现图表库，第一版使用可读数值、表格或简洁趋势标识。

**测试**：完整数据、无年度、部分评分缺失、待办链接、旧修订不由前端混入、按钮打开对话框。

**建议提交**：`feat(ui): add bridge archive overview`

---

### Task 11：年度工作台、年份栏与新建年度

**文件**：新增 `InspectionWorkspacePage`、`InspectionYearRail`、`InspectionProgress`、`ImportSourceCard`、`CreateInspectionDialog` 及测试；修改路由和样式。

**核心逻辑**：

- `/inspections` 拉取年度列表后 `replace` 到最新当前年度；无年度显示空状态和新建按钮。
- `/inspections/:yearId` 左侧列出 `is_current=true` 年度，倒序；旧修订版不混入年份主栏。
- 右侧加载指定年度工作台；验证响应桥梁 ID 与路由桥梁一致，不一致显示错误。
- 年度卡显示阶段、导入资料、统计、锁信息和主要动作。
- “继续校对/查看结果”进入现有全屏工作台；“解析/重试”进入 Task 12 解析表单。
- 新建年度对话框：默认当前年、范围校验、提交中禁用；成功进入新年度；409 使用 `existing_inspection_year_id` 进入已有年度并提示。
- 年份切换更新 URL；从校对返回后保持原年份。

**测试**：默认最新、无年度、年份切换、无效 yearId、桥梁不匹配、编辑锁文案、各状态动作、新建成功/重复/失败。

**建议提交**：`feat(ui): add annual inspection workspace`

---

### Task 12：Word 导入表单与全屏校对返回集成

**文件**：新增 `ImportWordDialog.tsx` 及测试；修改 `InspectionWorkspacePage.tsx`、`ReviewWorkspacePage.tsx`、`OverviewHeader.tsx`、`ReviewActionBar.tsx` 及测试、`reviewApi.ts`。

**导入表单**：

- 文件：`.docx`；
- 来源类型：默认 `软件导出Word`，可选 `正式Word`；
- 规则模板：显示“辽宁国省干线”，第一版不可切换；
- 检查日期：必填；
- 报告编号：必填；
- 项目名称：必填，默认“{桥名}{年度}年度定期检测”；
- 隐藏固定值：已有桥年度导入 / 当前年度检测资料 / 当前年度。

**调用编排**：

1. 上传 Word；
2. 获得导入记录 ID；
3. 调用 `parseWordImport`；
4. 成功后刷新年度工作台并进入全屏校对；
5. 解析失败则留在年度工作台，显示“解析失败”和“重新解析”；重试不重新上传。

上传中和解析中分别显示状态，按钮防双击；错误保留在对话框或记录卡，页面不能空白。

**校对集成**：

- 标题改为“{年度} 年度检测 · {来源类型}校对”；
- 返回按钮文案和行为改为“返回 {年度} 年度工作台”；
- 年度存在时导航到 `/bridges/:bridgeId/inspections/:yearId`；旧 `unassigned` 记录回退桥梁概览；
- 不改变校对 reducer、编辑锁、保存、预检、确认、重开和只读逻辑；
- 校对页继续使用 `app-shell-workbench` 全宽布局，不显示年度左栏。

**测试**：表单必填、非 docx、上传/解析顺序、防重复、上传失败、解析失败重试、成功导航；校对可编辑/只读页面返回正确年度；旧未挂年度回退；既有所有 review 测试通过。

**建议提交**：`feat(import): connect annual Word upload to review workspace`

---

### Task 13：构件档案入口、响应式、全量回归与文档收口

**文件**：`ComponentArchivePage.tsx`、`DefectThreadReviewPage.tsx`、相关组件测试、`styles.css`、`README.md`、`PROJECT_CONTEXT.md`、设计/计划状态。

**核心逻辑**：

- 构件档案顶部根据已有构件列表的 `unbound_count` 汇总显示“有 N 条观测待整理”；为 0 时不显示警告横幅。
- “进入线索整理”复用现有 `/defect-threads/review`；该路由继续可刷新，但不出现在一级 tab。
- 线索整理完成/返回进入 `/bridges/:bridgeId/components` 并刷新数量。
- 普通桥梁页统一视觉变量、间距、状态徽章和表格；不改校对页构件类别配色。
- ≥1100px：年度左栏约 240px；<1100px：左栏收窄；≤720px：年份改顶部横向选择，内容单列。
- 确认键盘焦点、对话框 `role="dialog" aria-modal="true"`、按钮可访问名称和错误提示关联。
- 更新 README 路由/API/启动说明；PROJECT_CONTEXT 记录模块 6.5 完成情况；设计和计划状态改为已实施（只有所有验收完成后）。

**全量验收**：

```powershell
# 1. Python 全量与真实 Word
Set-Location tools-python
uv run pytest -q
uv run pytest -q tests/importers/test_real_word_regression.py

# 2. C++ 构建与 PostgreSQL 全量
Set-Location ..\backend-cpp
cmake --build --preset vs2022-x64-debug
$env:BRIDGE_REPORT_TEST_DATABASE_URL='postgresql://bridge_report:bridge_report_dev@127.0.0.1:5432/bridge_report_system'
.\build\vs-debug\Debug\bridge_report_backend_tests.exe --gtest_brief=1

# 3. 前端全量与生产构建
Set-Location ..\frontend
npm run test
npm run build

# 4. 工作区和保护目录审计
Set-Location ..
git diff --check
git status --short
```

**手工真实流程**：

1. 登录后直接进入桥梁档案列表。
2. 进入绕阳河二号桥，默认看到桥梁概览。
3. 新建一个不冲突的测试年度或使用专用测试桥梁年度。
4. 从年度工作台上传真实 Word，完成解析。
5. 验证 25/31/36/31/15 基线。
6. 点击继续校对，确认截图所示全屏工作台仍存在且宽度不变。
7. 另一个账号打开同一记录，年度卡和校对页都显示编辑人；不得抢锁。
8. 退出校对返回原年度工作台。
9. 已确认记录从年度工作台点击“查看结果”，进入只读校对页。
10. 构件档案可进入线索整理，一级导航没有独立“病害线索校对”。
11. 重启前后所有深链接正常，无空白页。

**建议提交**：`docs: complete module 06.5 bridge workspace`

## 8. 依赖关系与建议批次

```text
Task 1
  ├─ Task 2 ─ Task 3 ─ Task 4
  └─ Task 5 ─ Task 6 ─ Task 7

Task 4 + Task 7
  └─ Task 8
      ├─ Task 9 ─ Task 10
      └─ Task 11 ─ Task 12

Task 10 + Task 12
  └─ Task 13
```

建议分五批提交：

1. 后端只读摘要（Tasks 1～4）；
2. 创建年度与 Word 上传（Tasks 5～7）；
3. 前端 API、导航和桥梁概览（Tasks 8～10）；
4. 年度工作台、导入和校对集成（Tasks 11～12）；
5. 构件入口、响应式、全量验收和文档（Task 13）。

## 9. 主要风险与控制

### 9.1 文件系统与数据库无法天然同事务

控制：临时文件 + 安全最终路径 + 明确补偿清理；所有删除都先通过 `resolve_path_under_root`；测试 SQL 失败和文件移动失败两个方向。

### 9.2 桥梁概览查询过重

控制：只查摘要；候选统计在后端复用纯函数；避免前端为卡片拉完整候选 JSON；必要时用单桥少量 CTE，而不是逐构件 N+1。

### 9.3 把年度工作台变成第二套校对器

控制：年度页只显示摘要和入口，不引入 reducer、编辑锁或可编辑病害字段；所有详细校对仍进入 `ReviewWorkspacePage`。

### 9.4 路由重组破坏旧深链接

控制：保留现有 review、components 和 defect-thread 路由；为根路由做 replace；新增 App 路由测试和手工刷新测试。

### 9.5 状态映射出现“双真源”

控制：数据库状态仍为唯一业务真源；进度条只是纯派生展示，不写回新的阶段字段。

### 9.6 上传成功但 Python 服务不可用

控制：保留导入记录和原文件，标记 `解析失败`，年度工作台提供重新解析；不要求用户重复上传。

## 10. 自审清单

- [x] 系统默认页是桥梁档案列表。
- [x] 进入桥梁默认页是桥梁概览。
- [x] 年度检测采用左侧年份、右侧工作台。
- [x] 导入只在选定桥梁和年度后进行。
- [x] 当前只有 Word 格式；界面使用中性“导入资料”命名。
- [x] 多来源合并和冲突处理没有进入代码或数据库。
- [x] 截图所示全屏校对工作台完整保留。
- [x] 年度工作台不取得编辑锁。
- [x] 校对退出返回原年度。
- [x] 病害线索整理功能保留，但不占一级导航。
- [x] Word 年度来自系统选择，不从文档自动改挂。
- [x] 上传路径受控，响应不泄漏绝对路径或相对存储路径。
- [x] 既有同桥同年唯一约束被复用，没有无必要迁移。
- [x] 五个受保护本地目录未提交、未删除。
- [x] Python、前端、C++/PostgreSQL、生产构建和真实 Word 回归全部通过。

## 11. 变更记录

| 日期 | 变更 | 原因 |
| --- | --- | --- |
| 2026-07-15 | 创建模块 06.5 实施计划 | 将已确认的桥梁档案模式、年度工作台、Word 导入入口和现有校对工作区复用拆成可执行任务 |
| 2026-07-15 | 完成模块 06.5 实施与回归 | 桥梁概览、年度工作台、受控 Word 上传/解析、全屏校对返回和构件档案入口均已落地 |
