# Review Workspace Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build module 05 review workspace so users can review BridgeAnnualInspectionData candidates, save draft edits, and confirm reviewed annual facts into PostgreSQL formal tables.

**Architecture:** C++ Drogon remains the only formal fact write path. React reads and edits the candidate JSON through C++ APIs. PostgreSQL formal tables are written only after backend preflight validation passes.

**Tech Stack:** C++ Drogon, JsonCpp, PostgreSQL SQL migrations/schema from database/migrations, React + TypeScript + Vite, Vitest, module 03 BridgeAnnualInspectionData contract.

## Global Constraints

1. 模块 03 契约是唯一数据契约。不新增契约字段：照片候选不加 `review_note`、不加可编辑题注字段（题注只读展示 `extracted_file.original_caption`）；评分候选不加 `review_note`。
2. 模块 05 不调用 Python 工具服务，不生成 `comparison_candidates`，不做 Word 解析优化。
3. 保存草稿只写 `import_records.parsed_result_json`，不写任何正式事实表。
4. 确认入库在单个数据库事务内完成；任何一步失败必须回滚，`import_status` 保持 `待校对`。
5. 同桥同年已有当前有效事实（`inspection_years.is_current = true` 且 `status = '已确认'`）时，必须显式 `confirm_revision: true` 才能入库。
6. 前端页面按“需要处理 / 普通病害 / 病害照片 / 技术状况评定 / 来源证据 / 原始 JSON”组织。
7. C++ 集成测试依赖环境变量 `BRIDGE_REPORT_TEST_DATABASE_URL`（drogon 连接串格式 `host=... port=... dbname=... user=... password=...`）；未设置时用 `GTEST_SKIP()` 跳过，不算失败。
8. `test-inputs/`、`test-output/` 不提交。
9. 后端错误响应统一为 `{"code": "...", "message": "..."}`；契约校验失败附加 `"issues": [{"path": "...", "message": "..."}]`。

## 现状梳理（已完成的调查结论）

后端 `backend-cpp` 现状：

- 只有 health 路由（`src/main.cpp` 内联 lambda 注册），无控制器目录、无数据库访问层。
- `vcpkg.json` 依赖仅 `drogon` + `gtest`，未启用 drogon 的 `postgres` 特性，当前二进制不能连 PostgreSQL。
- `AppConfig` 已含 `PostgresConfig`（host/port/database/user/password），但没有任何代码使用它。
- `bridge_report::contracts::validate_bridge_annual_inspection_data(const Json::Value&)` 轻量契约校验器已存在（检查必备对象/数组/confidence 等），可直接复用。
- CORS 模式：`bridge_report::http::apply_local_dev_cors_headers(response)` + 每个路径注册一个 OPTIONS handler。
- 构建/测试：CMake preset `vs2022-x64-debug`；`ctest --preset vs2022-x64-debug`。

前端 `frontend` 现状：

- 只有健康页 `App.tsx`、`api/health.ts`、契约类型 `contracts/annualInspection.ts`（含 `isBridgeAnnualInspectionData` 运行时守卫）。
- 无路由库。需要新增 `react-router-dom`。
- 测试：Vitest（`npm run test -- --run`）；构建含类型检查（`npm run build` = `tsc -b && vite build`）。

数据库（`database/migrations/002_core_schema_and_archive.sql`）关键事实：

- `import_records.parsed_result_json jsonb`；`import_status in ('已上传','解析中','待校对','已确认','解析失败','已取消')`。
- `inspection_years` 有 `version_number`、`is_current`、`revision_source_inspection_id`、`status in ('待校对','已确认','已被修订','已归档')`、`overall_score`、`overall_grade`；部分唯一索引 `ux_inspection_years_current_bridge_year (bridge_id, inspection_year) where is_current`——修订时必须先降级旧行再插入新行。
- `defect_observations` 用 `defect_description_raw`（非 defect_description）、`scale`（对应候选 `severity`）、`part_name`、`component_type`、`business_component_code`、`source_raw_cells_json`、`extraction_confidence`，`review_status in ('待校对','已确认','已修改','已驳回')`。
- `defect_measurements.measurement_type in ('数量','长度','宽度','最大宽度','面积','总面积','间距','尺寸组合','未识别尺寸')`。
- `defect_photos.defect_observation_id` 非空——未关联病害的照片不能入库；`match_status in ('高置信候选','待校对','已确认','未关联')`。
- `condition_ratings.rating_level in ('全桥','结构分部','部件','构件','项目')`；无 jsonb 列，`score_rows` 序列化进 `remarks`。
- `bridge_components` 无 `component_name` 列；用 `component_type` + `business_component_code` + `normalized_component_key`（桥内唯一）表达；别名进 `component_aliases`。

规格冲突的落地决策（以模块 03 契约和 002 表结构为准）：

| 规格表述 | 落地决策 |
| --- | --- |
| 模块 05 §11.3 “按 component_name + component_alias 查构件”“source_type=导入识别、is_manually_confirmed=true” | `bridge_components` 无这些列。映射为：`business_component_code = component_name`，`component_type = component_alias`（为空时用 `component_name`），`normalized_component_key = structure_part + "|" + component_type + "|" + business_component_code`（去空白）；`creation_source='导入沉淀'`，`current_status='已确认'`；`component_alias` 另写入 `component_aliases(alias_text, is_manually_confirmed=true)` |
| 模块 05 §11.4 字段映射列出 `defect_description`、`severity`、`raw_text` | 实际列为 `defect_description_raw`、`scale`、`source_raw_cells_json`（存 `{"raw_row_text": ...}`） |
| 模块 05 §7.1 顶部显示“解析规则 profile” | 契约无 `rule_profile` 字段，不扩展契约；顶部改显示 `import_records.importer_name` / `importer_version` 与 `source_type` |
| 模块 05 §7.5 有“取消导入”按钮但 §10 无对应 API | 新增 `POST /api/import-records/{id}/cancel`（见 Task 4），与 §12.1 “取消导入 -> 已取消”一致 |
| 模块 05 §8.1 “系统根据尺寸原文重新结构化 measurements[]”且不许调 Python | 在前端 TypeScript 实现与 `tools-python/bridge_report_tools/importers/measurements.py` 同规则的解析器（L=长度、W=宽度、S=/A=面积、D=间距、N处/N条=数量），随草稿一起保存（见 Task 12） |
| 契约 `quantity_text` 无对应 `defect_observations` 列 | 入库时若 `quantity_text` 非空且 `measurements[]` 无 `数量` 项，写一条 `defect_measurements(measurement_type='数量', raw_text=quantity_text, numeric_value=前导整数或 null, is_auto_parsed=false, is_manually_confirmed=true)` |

## File Structure

后端新增（均挂到 `bridge_report_backend_core` 与测试目标；CMakeLists 同步修改）：

- `backend-cpp/include/bridge_report/db/DbClientFactory.hpp` + `src/db/DbClientFactory.cpp` — 由 `PostgresConfig` 构造 drogon `orm::DbClient`（连接串拼装单独成纯函数便于测试）
- `backend-cpp/include/bridge_report/review/ReviewModels.hpp` + `src/review/ReviewModels.cpp` — 导航/校对响应的结构体与 `to_json` 纯函数
- `backend-cpp/include/bridge_report/review/ReviewStatistics.hpp` + `src/review/ReviewStatistics.cpp` — 候选 JSON 统计纯函数
- `backend-cpp/include/bridge_report/review/DraftValidation.hpp` + `src/review/DraftValidation.cpp` — 草稿保存校验纯函数
- `backend-cpp/include/bridge_report/review/PreflightReport.hpp` + `src/review/PreflightReport.cpp` — 入库前检查纯函数
- `backend-cpp/include/bridge_report/review/ConfirmPlan.hpp` + `src/review/ConfirmPlan.cpp` — 契约 JSON → 入库行计划纯函数
- `backend-cpp/include/bridge_report/db/ReviewRepository.hpp` + `src/db/ReviewRepository.cpp` — 导航查询、导入记录读写、确认事务执行
- `backend-cpp/include/bridge_report/http/ReviewRoutes.hpp` + `src/http/ReviewRoutes.cpp` — 全部 `/api/...` 路由注册（含 OPTIONS/CORS）
- `backend-cpp/tests/test_db_client_factory.cpp`、`tests/test_review_models.cpp`、`tests/test_review_statistics.cpp`、`tests/test_draft_validation.cpp`、`tests/test_preflight_report.cpp`、`tests/test_confirm_plan.cpp`、`tests/test_review_repository.cpp`（集成，可跳过）

后端修改：

- `backend-cpp/vcpkg.json` — drogon 加 `postgres` 特性
- `backend-cpp/CMakeLists.txt` — 新源文件与测试
- `backend-cpp/src/main.cpp` — 创建 DbClient、注册 review 路由、`/health/db`

数据库与脚本新增：

- `database/dev/seed_module05_review_sample.sql` — 样例桥/年度/导入记录种子
- `scripts/dev/seed-module05-review-sample.ps1` — 应用种子并输出 import_record_id

前端新增：

- `frontend/src/api/navigationApi.ts` + `navigationApi.test.ts`
- `frontend/src/api/reviewApi.ts` + `reviewApi.test.ts`
- `frontend/src/review/measurementParser.ts` + `measurementParser.test.ts`
- `frontend/src/review/grouping.ts` + `grouping.test.ts`
- `frontend/src/review/reviewDraft.ts` + `reviewDraft.test.ts` — 草稿编辑 reducer
- `frontend/src/pages/BridgesPage.tsx`、`pages/BridgeDetailPage.tsx`、`pages/ReviewWorkspacePage.tsx`
- `frontend/src/review/components/OverviewHeader.tsx`、`ReviewSidebar.tsx`、`NeedsAttentionSection.tsx`、`DefectsSection.tsx`、`PhotosSection.tsx`、`RatingsSection.tsx`、`EvidencePanel.tsx`、`RawJsonSection.tsx`、`ReviewActionBar.tsx`

前端修改：

- `frontend/package.json` — 新增 `react-router-dom`
- `frontend/src/App.tsx` — 改为路由壳，健康页保留在 `/`

---

### Task 1: 后端 PostgreSQL 访问基座

**Files:**
- Modify: `backend-cpp/vcpkg.json`
- Create: `backend-cpp/include/bridge_report/db/DbClientFactory.hpp`
- Create: `backend-cpp/src/db/DbClientFactory.cpp`
- Create: `backend-cpp/tests/test_db_client_factory.cpp`
- Modify: `backend-cpp/CMakeLists.txt`
- Modify: `backend-cpp/src/main.cpp`

**Interfaces:**
- `std::string bridge_report::db::build_pg_connection_string(const config::PostgresConfig&)` — 纯函数，输出 `host=... port=... dbname=... user=... password=...`
- `drogon::orm::DbClientPtr bridge_report::db::create_db_client(const config::PostgresConfig&, size_t connection_count = 2)`
- 新路由 `GET /health/db` — 执行 `select 1`，成功返回 `{"status":"ok","database":"reachable"}`；失败返回 503 `{"status":"degraded","database":"unavailable"}`

**Steps:**

- [ ] Step 1: `vcpkg.json` 中 `"drogon"` 改为 `{ "name": "drogon", "features": ["postgres"] }`
- [ ] Step 2: 写失败测试 `test_db_client_factory.cpp`：
  - `test build_pg_connection_string_formats_all_fields`：给定默认 `PostgresConfig`，断言输出串等于 `host=127.0.0.1 port=5432 dbname=bridge_report_system user=bridge_report password=bridge_report_dev`
  - `test create_db_client_connects_and_selects_one`：读取 `BRIDGE_REPORT_TEST_DATABASE_URL`，未设置则 `GTEST_SKIP()`；设置则用该连接串建 client，`execSqlSync("select 1")` 返回一行
- [ ] Step 3: 重新配置构建，确认测试编译失败（符号未定义）：`cmake --preset vs2022-x64-debug`（vcpkg 会安装 postgres 特性，耗时正常）→ `cmake --build --preset vs2022-x64-debug` 预期编译错误
- [ ] Step 4: 实现 `DbClientFactory`；`main.cpp` 启动时 `create_db_client(config.postgres)` 并注册 `/health/db`（沿用现有 CORS + OPTIONS 模式）
- [ ] Step 5: `cmake --build --preset vs2022-x64-debug` 通过；`ctest --preset vs2022-x64-debug` 全绿（无 env 时 DB 测试 skip）
- [ ] Step 6: 手工验证：启动后端，`curl http://127.0.0.1:18080/health/db` 返回 ok（本地 PostgreSQL 已按模块 02 迁移）

**Test:** 上述 gtest 两条 + curl 手工验证。
**Commit:** `feat(backend): add postgres db client foundation`

---

### Task 2: 导航只读 API（桥梁 → 年度 → 导入记录）

**Files:**
- Create: `backend-cpp/include/bridge_report/review/ReviewModels.hpp`
- Create: `backend-cpp/src/review/ReviewModels.cpp`
- Create: `backend-cpp/include/bridge_report/db/ReviewRepository.hpp`
- Create: `backend-cpp/src/db/ReviewRepository.cpp`
- Create: `backend-cpp/include/bridge_report/http/ReviewRoutes.hpp`
- Create: `backend-cpp/src/http/ReviewRoutes.cpp`
- Create: `backend-cpp/tests/test_review_models.cpp`
- Create: `backend-cpp/tests/test_review_repository.cpp`
- Modify: `backend-cpp/CMakeLists.txt`、`backend-cpp/src/main.cpp`

**Interfaces:**
- 结构体（`ReviewModels.hpp`）：`BridgeSummary{id, system_number, bridge_name, route_name, status}`、`InspectionYearSummary{id, system_number, inspection_year, status, version_number, is_current}`、`ImportRecordSummary{id, system_number, import_name, source_type, import_status, inspection_year_id, importer_name, created_at}`；每个都有 `Json::Value to_json() const`
- `ReviewRepository`（构造注入 `drogon::orm::DbClientPtr`）：
  - `std::vector<BridgeSummary> list_bridges()`
  - `std::vector<InspectionYearSummary> list_inspection_years(bridge_id)`
  - `std::vector<ImportRecordSummary> list_import_records(bridge_id)`
- 路由（`ReviewRoutes.cpp` 的 `register_review_routes(DbClientPtr)`）：
  - `GET /api/bridges` → `{"bridges": [...]}`
  - `GET /api/bridges/{bridge_id}/inspection-years` → `{"inspection_years": [...]}`
  - `GET /api/bridges/{bridge_id}/import-records` → `{"import_records": [...]}`
  - bridge_id 非法/不存在返回 404 `{"code":"bridge_not_found","message":"..."}`

**Steps:**

- [ ] Step 1: 写失败单元测试 `test_review_models.cpp`：各 `to_json` 输出的 key 与值（含中文枚举值原样、`inspection_year_id` 为空时输出 `null`）
- [ ] Step 2: 写集成测试 `test_review_repository.cpp`（无 env 则 skip）：fixture 在事务里插入一座桥 + 一个年度 + 一条导入记录（系统编号用 `gen_random_uuid()` 默认值，人工唯一 `bridge_name`），断言三个 list 方法返回预期行；fixture 结束回滚
- [ ] Step 3: 构建确认失败 → 实现 ReviewModels、ReviewRepository（`execSqlSync` 参数化 SQL）、ReviewRoutes（CORS + OPTIONS 与 main.cpp 现有模式一致），main.cpp 注册
- [ ] Step 4: `ctest --preset vs2022-x64-debug` 全绿；设置 `BRIDGE_REPORT_TEST_DATABASE_URL` 后再跑一次，集成测试也绿
- [ ] Step 5: 手工验证：`curl http://127.0.0.1:18080/api/bridges`

**Test:** `to_json` 单元测试 + 仓库集成测试（可跳过）+ curl。
**Commit:** `feat(backend): add bridge navigation read APIs`

---

### Task 3: GET review 读取校对数据

**Files:**
- Create: `backend-cpp/include/bridge_report/review/ReviewStatistics.hpp`
- Create: `backend-cpp/src/review/ReviewStatistics.cpp`
- Create: `backend-cpp/tests/test_review_statistics.cpp`
- Modify: `backend-cpp/include/bridge_report/db/ReviewRepository.hpp`、`src/db/ReviewRepository.cpp`
- Modify: `backend-cpp/src/http/ReviewRoutes.cpp`
- Modify: `backend-cpp/tests/test_review_repository.cpp`
- Modify: `backend-cpp/CMakeLists.txt`

**Interfaces:**
- `ReviewStatistics bridge_report::review::build_review_statistics(const Json::Value& parsed_result)` — 纯函数。字段：`defect_count`、`photo_count`、`rating_item_count`（= overall 1 + structure_parts + evaluation_parts）、`pending_count`、`confirmed_count`、`modified_count`、`ignored_count`（对 defects+photos+ratings 三层的 `review_status` 汇总）、`object_warning_count`（对象级 warnings 非空的候选数）
- `ReviewRepository` 新增：
  - `std::optional<ImportRecordDetail> get_import_record_detail(import_record_id)` — 联查 import_records + bridges + inspection_years，含 `parsed_result_json` 文本
  - `bool has_current_annual_facts(bridge_id, inspection_year)` — `exists(select 1 from inspection_years where bridge_id=$1 and inspection_year=$2 and is_current and status='已确认')`
- 路由 `GET /api/import-records/{import_record_id}/review`，200 响应：

```json
{
  "import_record": {"id","system_number","import_name","source_type","import_status","importer_name","importer_version","created_at","updated_at"},
  "bridge": {"id","system_number","bridge_name","route_name"},
  "inspection_year": {"id","system_number","inspection_year","status","version_number","is_current"},
  "parsed_result": {},
  "statistics": {"defect_count":0,"photo_count":0,"rating_item_count":0,"pending_count":0,"confirmed_count":0,"modified_count":0,"ignored_count":0,"object_warning_count":0},
  "has_current_annual_facts": false
}
```

  `inspection_year` 在 `inspection_year_id` 为空时为 `null`，此时 `has_current_annual_facts` 用 `parsed_result.inspection.inspection_year` 查询。找不到记录返回 404 `import_record_not_found`。

**Steps:**

- [ ] Step 1: 写失败单元测试 `test_review_statistics.cpp`：用手工构造的候选 JSON 覆盖——三层计数、各 review_status 汇总、对象 warnings 计数、defects/photos 为空数组时全为 0
- [ ] Step 2: 集成测试追加：插入带 `parsed_result_json`（取自 `samples/contracts/bridge_annual_inspection_data.valid.json` 文本）的导入记录，`get_import_record_detail` 返回桥名/年度/JSON；`has_current_annual_facts` 在插入 `已确认+is_current` 年度行前后分别为 false/true
- [ ] Step 3: 构建失败 → 实现统计纯函数、仓库方法、路由拼装 → 测试全绿
- [ ] Step 4: 手工验证：对种子数据（Task 9 之前可手工 insert）curl review 路由

**Test:** 统计纯函数单元测试 + 仓库集成测试 + curl。
**Commit:** `feat(backend): add review detail endpoint`

---

### Task 4: PUT review-draft 保存草稿 + POST cancel 取消导入

**Files:**
- Create: `backend-cpp/include/bridge_report/review/DraftValidation.hpp`
- Create: `backend-cpp/src/review/DraftValidation.cpp`
- Create: `backend-cpp/tests/test_draft_validation.cpp`
- Modify: `backend-cpp/include/bridge_report/db/ReviewRepository.hpp`、`src/db/ReviewRepository.cpp`
- Modify: `backend-cpp/src/http/ReviewRoutes.cpp`
- Modify: `backend-cpp/tests/test_review_repository.cpp`
- Modify: `backend-cpp/CMakeLists.txt`

**Interfaces:**
- `DraftValidationResult bridge_report::review::validate_review_draft(const Json::Value& body, const std::string& record_system_number, const std::string& record_import_status)` — 纯函数，按顺序检查并返回第一类错误：
  1. `import_record_not_editable`（`import_status` 不是 `待校对`）
  2. `contract_validation_failed`（复用 `validate_bridge_annual_inspection_data`，附 `issues[]`）
  3. `import_context_mismatch`（`body.import_context.import_record_system_number != record_system_number`）
- `ReviewRepository::save_review_draft(import_record_id, const std::string& parsed_json_text)` — `update import_records set parsed_result_json=$2::jsonb, updated_at=now() where id=$1`，`import_status` 保持不变
- `ReviewRepository::cancel_import_record(import_record_id)` — 状态 `已上传/解析中/待校对/解析失败` → `已取消`；已是 `已确认/已取消` 返回 false
- 路由：
  - `PUT /api/import-records/{id}/review-draft`：请求体为完整 `BridgeAnnualInspectionData`；成功 200 `{"saved":true,"import_status":"待校对"}`；错误码同上（400/409），JSON 非法为 400 `invalid_json_body`；记录不存在 404 `import_record_not_found`
  - `POST /api/import-records/{id}/cancel`：成功 200 `{"cancelled":true}`；不可取消 409 `import_record_not_editable`

**Steps:**

- [ ] Step 1: 写失败单元测试 `test_draft_validation.cpp`：
  - 合法样例 JSON（读 `samples/contracts/bridge_annual_inspection_data.valid.json`，`import_record_system_number` 对齐）→ ok
  - 状态 `已确认` → `import_record_not_editable`
  - 删掉 `ratings` → `contract_validation_failed` 且 `issues` 非空
  - 系统编号不一致 → `import_context_mismatch`
- [ ] Step 2: 集成测试追加：`save_review_draft` 后重新读取 `parsed_result_json` 已更新且 `import_status` 仍为 `待校对`；`cancel_import_record` 各状态分支
- [ ] Step 3: 构建失败 → 实现 → 全绿
- [ ] Step 4: 手工验证：curl PUT 一份修改过 `review_note` 的样例 JSON，再 GET review 确认已保存

**Test:** 草稿校验单元测试（4 条分支）+ 仓库集成测试 + curl 往返。
**Commit:** `feat(backend): add review draft save and cancel endpoints`

---

### Task 5: 入库前检查纯逻辑 PreflightReport

**Files:**
- Create: `backend-cpp/include/bridge_report/review/PreflightReport.hpp`
- Create: `backend-cpp/src/review/PreflightReport.cpp`
- Create: `backend-cpp/tests/test_preflight_report.cpp`
- Modify: `backend-cpp/CMakeLists.txt`

**Interfaces:**

```text
struct PreflightContext {
    std::string import_status;            // import_records.import_status
    std::string record_system_number;     // import_records.system_number
    std::string bridge_system_number;     // bridges.system_number
    std::optional<int> inspection_year;   // inspection_years.inspection_year（记录已挂年度时）
    bool has_current_annual_facts;
};

struct PreflightIssue { std::string code; std::string message; std::string target_candidate_id; /* 可空 */ };

struct PreflightReport {
    bool can_confirm;
    bool requires_revision_confirmation;
    std::vector<PreflightIssue> blocking_errors;
    std::vector<PreflightIssue> warnings;
    Json::Value to_json() const;
};

PreflightReport build_preflight_report(const Json::Value& data, const PreflightContext& context);
```

阻断错误（全部实现，逐条测试）：

| code | 触发条件 |
| --- | --- |
| `import_record_wrong_status` | `import_status != "待校对"` |
| `contract_validation_failed` | `validate_bridge_annual_inspection_data` 不通过 |
| `import_context_mismatch` | `import_context.import_record_system_number` 或 `bridge_check.selected_bridge_system_number` 或（记录已挂年度时）`inspection.inspection_year` 与上下文不一致 |
| `candidate_pending_review` | defects/photos/ratings（overall + structure_parts + evaluation_parts）任一 `review_status == "待确认"`；每个对象一条，带 `target_candidate_id`（评分项用 `ratings.overall` / `ratings.structure_parts[i]` 形式定位） |
| `defect_missing_required_field` | `已确认/已修改` 病害的 `structure_part`、`component_name`、`defect_type`、`defect_description` 任一为空 |
| `photo_link_unresolved` | `已确认/已修改` 照片 `match_status in (高置信候选, 已确认)` 但 `linked_defect_candidate_id` 为空，或指向不存在/`已忽略`/`待确认` 的病害 |
| `rating_overall_missing` | `ratings.overall.total_score` 非数字或 `overall_grade` 为空字符串 |

非阻断警告：

| code | 触发条件 |
| --- | --- |
| `defect_without_photo` | 已确认/已修改病害 `photo_numbers` 为空，或其中某编号没有任何已确认/已修改照片候选指向该病害 |
| `unreferenced_photo_ignored` | 照片 `已忽略`，或 `已确认/已修改` 且 `match_status == "未关联"`（不会入库） |
| `measurement_unstructured_kept` | 已确认/已修改病害 `measurement_text` 非空且 `measurements` 为空 |
| `rating_parts_incomplete` | `structure_parts` 少于 3 条或 `evaluation_parts` 为空 |

`requires_revision_confirmation = context.has_current_annual_facts`（独立于 can_confirm；阻断清空时 `can_confirm=true`，前端据此弹修订确认）。

**Steps:**

- [ ] Step 1: 写失败测试 `test_preflight_report.cpp`：以合法样例 JSON 为基底，逐条构造上表 11 种场景 + 全绿场景（`can_confirm=true`、无阻断）+ `has_current_annual_facts=true` 时 `requires_revision_confirmation=true`
- [ ] Step 2: 构建失败 → 实现 → 全绿（纯函数，无 DB）
- [ ] Step 3: 补 `to_json` 断言：输出结构与模块 05 规格 §10.3 示例一致（`blocking_errors[].target_candidate_id` 为空时输出 `null`）

**Test:** 13+ 条纯函数单元测试。
**Commit:** `feat(backend): add confirm preflight logic`

---

### Task 6: POST preflight-confirm 端点

**Files:**
- Modify: `backend-cpp/src/http/ReviewRoutes.cpp`
- Modify: `backend-cpp/tests/test_review_repository.cpp`

**Interfaces:**
- `POST /api/import-records/{import_record_id}/preflight-confirm`：无请求体；读取记录 → 组装 `PreflightContext`（含 `has_current_annual_facts` 查询）→ `build_preflight_report` → 200 返回 `report.to_json()`；记录不存在 404 `import_record_not_found`

**Steps:**

- [ ] Step 1: 集成测试追加：种子记录（待校对 + 合法 JSON，但把一条病害改为 `待确认`）调用仓库层组装逻辑，断言报告含 `candidate_pending_review`
- [ ] Step 2: 实现路由 → 测试全绿
- [ ] Step 3: 手工验证：curl 返回体与规格 §10.3 形状一致

**Test:** 集成测试 + curl。
**Commit:** `feat(backend): add preflight-confirm endpoint`

---

### Task 7: 入库计划纯映射 ConfirmPlan

**Files:**
- Create: `backend-cpp/include/bridge_report/review/ConfirmPlan.hpp`
- Create: `backend-cpp/src/review/ConfirmPlan.cpp`
- Create: `backend-cpp/tests/test_confirm_plan.cpp`
- Modify: `backend-cpp/CMakeLists.txt`

**Interfaces:**

```text
struct ComponentPlan { structure_part; component_type; business_component_code; normalized_component_key; alias_text /* 可空 */; };
struct MeasurementPlan { measurement_type; numeric_value /* 可空 */; unit /* 可空 */; raw_text; is_auto_parsed; };
struct DefectPlan {
    candidate_id; component_key /* 指向 ComponentPlan */;
    structure_part; part_name; defect_location; defect_type; defect_description_raw;
    scale /* ← severity，可空 */; raw_row_text; source_table_title; source_table_index; source_row_number;
    extraction_confidence; review_status; review_note;
    std::vector<MeasurementPlan> measurements;
};
struct PhotoPlan { candidate_id; defect_candidate_id; photo_number; photo_title /* ← original_caption */; };
struct RatingPlan { rating_level; structure_part; rating_item_name; score; grade /* 可空 */; weight /* 可空 */; remarks /* 可空，score_rows JSON */; review_status; };
struct ConfirmPlan {
    std::vector<ComponentPlan> components;   // 去重后
    std::vector<DefectPlan> defects;
    std::vector<PhotoPlan> photos;
    std::vector<RatingPlan> ratings;
    std::optional<double> overall_score; std::string overall_grade;
};
ConfirmPlan build_confirm_plan(const Json::Value& data);
```

映射规则（全部逐条测试）：

1. 只取 `review_status in (已确认, 已修改)` 的病害；`已忽略/待确认` 不进计划（preflight 已挡待确认，这里再兜底跳过）。
2. 构件：`business_component_code = component_name`；`component_type = component_alias 非空 ? component_alias : component_name`；`normalized_component_key = structure_part|component_type|business_component_code`（各段去首尾与内部连续空白）；同 key 只产生一个 `ComponentPlan`；`alias_text = component_alias`（非空时）。
3. 病害：`part_name = component_alias`；`defect_description_raw = defect_description`；`scale = severity`；`raw_row_text = source_ref.raw_row_text`；`extraction_confidence = confidence`；`review_status` 原样（已确认/已修改）。
4. 尺寸：`measurements[]` 逐条 → `MeasurementPlan(dimension_type→measurement_type, value, unit, source_text→raw_text, is_auto_parsed=true)`；`measurements` 为空且 `measurement_text` 非空 → 一条 `('未识别尺寸', null, null, measurement_text, is_auto_parsed=false)`；`quantity_text` 非空且 `measurements` 无 `数量` 项 → 一条 `('数量', 前导整数或 null, null, quantity_text, is_auto_parsed=false)`。
5. 照片：只取 `review_status in (已确认, 已修改)` 且 `linked_defect_candidate_id` 指向已进计划病害的照片；`photo_title = extracted_file.original_caption`；`match_status` 入库时固定写 `已确认`；`未关联/已忽略` 不入库。
6. 评分：`overall` → `('全桥','全桥','全桥', total_score, overall_grade, weight=null, remarks=null)`；`structure_parts[i]` → `('结构分部', structure_part, structure_part, structure_score, grade, weight, remarks=null)`；`evaluation_parts[i]` → `('部件', structure_part, evaluation_part, part_score, grade=null, weight=null, remarks=score_rows 的紧凑 JSON 文本，空数组时为 null)`；三层 `review_status` 原样带出。
7. `overall_score/overall_grade` 同步放顶层，供 `inspection_years` 更新。

**Steps:**

- [ ] Step 1: 写失败测试 `test_confirm_plan.cpp`：覆盖上面 7 条规则各自的正反例（同构件两条病害只产出一个 ComponentPlan；已忽略病害的照片即使已确认也不入库；`component_alias` 为空时 `component_type` 回退等），共 ≥12 条
- [ ] Step 2: 构建失败 → 实现 → 全绿

**Test:** ≥12 条纯函数单元测试。
**Commit:** `feat(backend): add confirm plan mapping`

---

### Task 8: 确认入库事务 + POST confirm 端点

**Files:**
- Modify: `backend-cpp/include/bridge_report/db/ReviewRepository.hpp`、`src/db/ReviewRepository.cpp`
- Modify: `backend-cpp/src/http/ReviewRoutes.cpp`
- Modify: `backend-cpp/tests/test_review_repository.cpp`

**Interfaces:**
- `ConfirmOutcome ReviewRepository::confirm_annual_facts(import_record_id, const ConfirmPlan&, bool confirm_revision, const std::string& confirmation_note)`，在 `drogon::orm::Transaction` 内按序执行：
  1. `select ... for update` 重读导入记录，状态非 `待校对` → 抛 `import_record_wrong_status`
  2. 目标年度：记录已挂 `inspection_year_id` 用之；否则按 `bridge_id + data.inspection.inspection_year` 建新行（`version_number=1`）
  3. 修订规则：存在 `is_current && status='已确认'` 的同桥同年行时——`confirm_revision=false` 抛 `revision_confirmation_required`；`true` 则旧行 `is_current=false, status='已被修订'`，插入新行 `version_number=旧+1, revision_source_inspection_id=旧id, is_current=true`，并把导入记录 `inspection_year_id` 指向新行
  4. 年度行更新：`status='已确认', is_current=true, overall_score, overall_grade, updated_at=now()`
  5. 构件 upsert：按 `(bridge_id, normalized_component_key)` 查，无则插（`creation_source='导入沉淀'`, `current_status='已确认'`）；`alias_text` 非空时 `insert ... on conflict (bridge_component_id, alias_text) do nothing`，`is_manually_confirmed=true`
  6. 病害/尺寸/照片/评分按 ConfirmPlan 逐行插入（列映射见 Task 7；`source_import_record_id`、`inspection_year_id`、`bridge_id`、`source_raw_cells_json={"raw_row_text":...}` 由仓库补齐；`defect_photos.archived_file_id=null`）
  7. 导入记录：`import_status='已确认', finished_at=now()`，`validation_result_json` 写入 `{"confirmed_at":..., "confirmation_note":..., "written":{...}}`
- `POST /api/import-records/{import_record_id}/confirm`，请求体 `{"confirm_revision": false, "confirmation_note": "..."}`：
  - 先在同一事务外执行 preflight，`can_confirm=false` → 409 返回完整 preflight 报告
  - `requires_revision_confirmation && !confirm_revision` → 409 `{"code":"revision_confirmation_required","message":"同桥同年已有当前有效事实，需显式确认修订版。"}`
  - 成功 200：`{"confirmed":true,"inspection_year_id":"...","version_number":1,"written":{"defect_observations":N,"defect_measurements":N,"defect_photos":N,"condition_ratings":N}}`
  - 事务失败 500 `{"code":"db_write_failed","message":"<异常摘要>"}`，状态保持 `待校对`

**Steps:**

- [ ] Step 1: 写失败集成测试（无 env 则 skip）：
  - `confirm_happy_path_writes_all_fact_tables`：种子桥+待校对年度+全部候选已确认的合法 JSON → 确认后 `defect_observations/defect_measurements/defect_photos/condition_ratings` 行数与内容抽查（`scale`、`defect_description_raw`、`未识别尺寸` 行、`remarks` 的 score_rows JSON、构件 upsert 复用）、年度行 `已确认+is_current`、导入记录 `已确认`
  - `confirm_blocks_when_pending_candidate`：一条待确认 → 端点层 409，正式表零行
  - `confirm_requires_revision_when_current_facts_exist`：预置同桥同年 `已确认+is_current` 行 → `confirm_revision=false` 抛/409；`true` → 旧行 `已被修订+is_current=false`，新行 `version_number=2` 且 `revision_source_inspection_id=旧id`
  - `confirm_rolls_back_on_failure`：在 ConfirmPlan 里人为注入非法 `structure_part`（绕过 preflight 直接调仓库方法）触发 check 约束失败 → 断言无任何正式表行残留、导入记录状态未变
- [ ] Step 2: 实现事务方法与路由 → 集成测试全绿（需 env）
- [ ] Step 3: `ctest --preset vs2022-x64-debug` 无 env 时其余测试不受影响

**Test:** 4 条集成测试覆盖规格 §14 的 4/5/6/7/8/9/10/11/12/13 项。
**Commit:** `feat(backend): add annual fact confirm transaction`

---

### Task 9: 模块 05 样例种子脚本

**Files:**
- Create: `database/dev/seed_module05_review_sample.sql`
- Create: `scripts/dev/seed-module05-review-sample.ps1`

**Interfaces:**
- SQL 幂等（`on conflict do nothing` / 先删同名样例桥）：插入样例桥 `绕阳河二号桥（模块05样例）`、2026 年度行（`status='待校对'`, `is_current=true`）、导入记录（`import_status='待校对'`, `source_type='软件导出Word'`, `importer_name='word_importer'`），`parsed_result_json` 用 `\set content` 读入 `samples/contracts/bridge_annual_inspection_data.valid.json`
- PS1 沿用 `check-module02-db.ps1` 的 `BRIDGE_REPORT_DATABASE_URL`/`PSQL_EXE` 约定，结束时 `select id, system_number from import_records ...` 输出可直接用于 curl/前端的 `import_record_id`

**Steps:**

- [ ] Step 1: 编写 SQL 与 PS1
- [ ] Step 2: 运行 `powershell -ExecutionPolicy Bypass -File scripts/dev/seed-module05-review-sample.ps1`，输出 import_record_id
- [ ] Step 3: `curl http://127.0.0.1:18080/api/import-records/{id}/review` 返回 200 且 `statistics.defect_count == 1`
- [ ] Step 4: 重复运行脚本确认幂等

**Test:** 脚本运行 + curl 验证 + 幂等重跑。
**Commit:** `feat(db): add module05 review sample seed`

---

### Task 10: 前端 review/navigation API client

**Files:**
- Create: `frontend/src/api/navigationApi.ts`、`frontend/src/api/navigationApi.test.ts`
- Create: `frontend/src/api/reviewApi.ts`、`frontend/src/api/reviewApi.test.ts`

**Interfaces:**

```text
// navigationApi.ts
fetchBridges(baseUrl): Promise<BridgeSummary[]>
fetchInspectionYears(baseUrl, bridgeId): Promise<InspectionYearSummary[]>
fetchImportRecords(baseUrl, bridgeId): Promise<ImportRecordSummary[]>

// reviewApi.ts
fetchReview(baseUrl, importRecordId): Promise<ReviewResponse>          // parsed_result 用 isBridgeAnnualInspectionData 守卫校验，失败抛错
saveReviewDraft(baseUrl, importRecordId, data: BridgeAnnualInspectionData): Promise<{saved: boolean}>
runPreflight(baseUrl, importRecordId): Promise<PreflightResponse>
confirmImport(baseUrl, importRecordId, body: {confirm_revision: boolean; confirmation_note: string}): Promise<ConfirmResponse>
cancelImport(baseUrl, importRecordId): Promise<{cancelled: boolean}>
// 非 2xx 响应统一抛 ApiError{code, message, issues?}（从响应体解析）
```

类型 `ReviewResponse/PreflightResponse/ConfirmResponse/BridgeSummary/...` 与 Task 3/5/8 的响应 JSON 一一对应，定义在各 api 文件内，候选数据复用 `contracts/annualInspection.ts` 类型。

**Steps:**

- [ ] Step 1: 写失败 Vitest（mock `fetch`）：每个函数一条成功用例（URL、method、body 断言）+ `fetchReview` 对非法 parsed_result 抛错 + 非 2xx 抛 `ApiError` 且 `code` 来自响应体
- [ ] Step 2: `npm run test -- --run` 失败 → 实现 → 全绿

**Test:** Vitest ≥10 条。
**Commit:** `feat(frontend): add review api client`

---

### Task 11: 前端路由与导航页

**Files:**
- Modify: `frontend/package.json`（新增 `react-router-dom@^6`）
- Modify: `frontend/src/App.tsx`
- Create: `frontend/src/pages/BridgesPage.tsx`
- Create: `frontend/src/pages/BridgeDetailPage.tsx`

**Interfaces:**
- 路由：`/`（现有健康页内容）、`/bridges`（桥梁表格：系统编号/桥名/路线/状态，行点击进详情）、`/bridges/:bridgeId`（上：年度检测任务表；下：导入记录表，行内“进入校对”链接）
- 校对路由路径：`/bridges/:bridgeId/inspections/:inspectionYearId/imports/:importRecordId/review`；导入记录无年度时 `inspectionYearId` 段用字面量 `unassigned`（页面只消费 `importRecordId`）
- 本任务先注册校对路由到占位组件（显示 importRecordId），Task 13 替换

**Steps:**

- [ ] Step 1: `npm install react-router-dom`
- [ ] Step 2: 改造 App.tsx 为 `BrowserRouter` + 顶部导航（首页 / 桥梁列表）；两个页面用 Task 10 的 client 拉数据，加载中/出错状态各有文案
- [ ] Step 3: `npm run build` 通过（tsc 校验）；`npm run test -- --run` 既有测试不回归
- [ ] Step 4: 手工验证：种子数据下 `/bridges` 能看到样例桥并逐级点到校对占位页

**Test:** 构建 + 既有 Vitest + 手工点击链路。
**Commit:** `feat(frontend): add bridge navigation pages`

---

### Task 12: 前端校对领域逻辑（分组 / 批量确认 / 尺寸解析 / 草稿 reducer）

**Files:**
- Create: `frontend/src/review/measurementParser.ts`、`measurementParser.test.ts`
- Create: `frontend/src/review/grouping.ts`、`grouping.test.ts`
- Create: `frontend/src/review/reviewDraft.ts`、`reviewDraft.test.ts`

**Interfaces:**

```text
// measurementParser.ts —— 与 tools-python/bridge_report_tools/importers/measurements.py 同规则
parseMeasurements(text: string | null): Measurement[]
// L=0.8m→长度 / W=0.12mm→宽度 / S=、A=→面积 / D=→间距 / 3处、2条→数量；无法稳定解析返回 []

// grouping.ts
needsAttention(data): AttentionItem[]        // {kind: 'defect'|'photo'|'rating'|'import', candidateId, message, severity}
// 规则（模块 05 §9.1）：对象级 warnings 非空；顶层 warnings/errors 有 target_candidate_id 指向它；
// 病害 measurement_text 非空且 measurements 为空且原文含数字+单位线索（/\d+(\.\d+)?\s*(m|mm|cm|m2|m²|处|条)/）；
// 病害 photo_numbers 中存在无照片候选关联的编号；照片 match_status=未关联 或 linked 为空且未忽略
isNormalDefect(defect, data): boolean        // §9.2：待确认 + 无 warnings + 无顶层 error 指向 + structure_part/component_name/defect_type/defect_description 非空 + photo_numbers 每个编号都有照片候选 linked 到本病害（或 photo_numbers 为空）
isNormalPhoto(photo): boolean                // 待确认 + 无 warnings + match_status=高置信候选 + linked 非空
isNormalRating(item): boolean                // 待确认 + score 非空（overall 还需 grade 非空）
buildStatistics(data): ReviewCounts          // 与后端 statistics 同口径 + needs_attention_count

// reviewDraft.ts —— useReducer 纯 reducer
reviewDraftReducer(state: BridgeAnnualInspectionData, action): BridgeAnnualInspectionData
// actions：
//  {type:'edit_defect_field', candidateId, field, value}   // 8.1 白名单字段；改动后该病害 review_status 自动置 '已修改'（原为已确认/待确认时）
//  {type:'edit_measurement_text', candidateId, text}       // 同时用 parseMeasurements 重算 measurements
//  {type:'set_defect_status', candidateId, status}
//  {type:'photo_confirm_match'|'photo_unlink'|'photo_mark_unrelated'|'photo_ignore', candidateId}
//  {type:'edit_photo_number', candidateId, photoNumber}
//  {type:'edit_photo_link', candidateId, defectCandidateId}
//  {type:'edit_rating_field', target: 'overall'|{part}|{evaluation}, field, value}
//  {type:'set_rating_status', target, status}
//  {type:'batch_confirm_normal'}                           // 用 isNormal* 把普通候选置 '已确认'
// 只读字段（candidate_id/source_ref/confidence/warnings/original_caption）无 action 可改
```

照片 action 语义（模块 05 §8.2）：`photo_confirm_match` → `match_status='已确认'`（要求 linked 非空）；`photo_unlink` → `linked=null, match_status='待校对'`；`photo_mark_unrelated` → `linked=null, match_status='未关联'`；`photo_ignore` → `review_status='已忽略'`。

**Steps:**

- [ ] Step 1: 写失败 Vitest：
  - measurementParser ≥8 条（镜像 Python 测试用例：L/W/S/A/D/N处/组合/不可解析返回空）
  - grouping ≥10 条（§9.1 每条规则一正一反；isNormal* 各字段缺失反例；buildStatistics 计数）
  - reviewDraft ≥12 条（每类 action 一条；编辑后自动 `已修改`；batch_confirm_normal 不动带 warning 候选；reducer 不可变性——原 state 未被修改）
- [ ] Step 2: `npm run test -- --run` 失败 → 实现 → 全绿

**Test:** Vitest ≥30 条，全部纯函数。
**Commit:** `feat(frontend): add review domain logic`

---

### Task 13: 校对工作台页面 UI

**Files:**
- Create: `frontend/src/pages/ReviewWorkspacePage.tsx`
- Create: `frontend/src/review/components/OverviewHeader.tsx`、`ReviewSidebar.tsx`、`NeedsAttentionSection.tsx`、`DefectsSection.tsx`、`PhotosSection.tsx`、`RatingsSection.tsx`、`EvidencePanel.tsx`、`RawJsonSection.tsx`、`ReviewActionBar.tsx`
- Modify: `frontend/src/App.tsx`（占位路由替换）
- Modify: `frontend/src/styles.css`

**Interfaces:**
- 页面加载 `fetchReview` → `useReducer(reviewDraftReducer, parsed_result)`；三栏布局：左 `ReviewSidebar`（六个分组 + 计数，来自 `buildStatistics`/`needsAttention`），中间按当前分组渲染对应 Section，右 `EvidencePanel`（选中候选的 candidate_id、raw_row_text、章节/表名/行号、confidence、warnings、题注 `original_caption` 只读）
- `OverviewHeader`：桥名、年度、导入记录编号、`source_type`、`importer_name`、导入状态、六项计数、顶层 warnings/errors 列表（红/黄区分）
- `DefectsSection`：表格列 = 模块 05 §7.3（结构部位下拉、构件/位置/病害类型/数量/尺寸原文/照片编号文本输入、校对状态下拉、备注输入）；行点击设为选中候选
- `PhotosSection`：表格列 = 照片编号（可编辑）、题注（只读）、关联病害（下拉，选项为病害候选）、匹配状态、校对状态 + 四个操作按钮（确认匹配/取消匹配/标记未关联/忽略）
- `RatingsSection`：全桥总分/等级输入、结构分部表（评分/权重只读展示、等级输入、状态下拉）、评价部件表（评分输入、状态下拉）
- `RawJsonSection`：`<pre>{JSON.stringify(draft, null, 2)}</pre>` 只读
- `ReviewActionBar`：保存草稿 / 批量确认普通候选 / 入库前检查 / 确认年度事实入库 / 取消导入，按钮行为在 Task 14 接线，本任务先渲染并禁用未接线按钮

**Steps:**

- [ ] Step 1: 写失败 Vitest（对纯展示辅助函数）：`formatAttentionItem`（AttentionItem → 列表行文案）、`ratingRows`（ratings → 表格行数组）两个辅助函数先行测试
- [ ] Step 2: 实现组件与页面；`npm run build` 通过；`npm run test -- --run` 全绿
- [ ] Step 3: 手工验证：种子数据打开校对页，六个分组切换正常，编辑病害字段后状态自动变 `已修改`，右侧证据面板随选中变化

**Test:** 辅助函数 Vitest + 构建 + 手工走查。
**Commit:** `feat(frontend): add review workspace page`

---

### Task 14: 按钮流接线 + 端到端验证 + 文档

**Files:**
- Modify: `frontend/src/review/components/ReviewActionBar.tsx`、`frontend/src/pages/ReviewWorkspacePage.tsx`
- Modify: `README.md`
- Modify: `docs/superpowers/specs/modules/05-review-workspace.md`（变更记录）
- Modify: `PROJECT_CONTEXT.md`（当前进度）

**Interfaces:**
- 保存草稿 → `saveReviewDraft`，成功显示“已保存”，失败显示 `ApiError.message`（`contract_validation_failed` 时逐条列 `issues`）
- 批量确认 → dispatch `batch_confirm_normal` 后自动触发一次保存草稿
- 入库前检查 → `runPreflight`，结果面板列出 blocking_errors（红）与 warnings（黄），`can_confirm=true` 才解锁确认按钮
- 确认入库 → 若最近一次 preflight `requires_revision_confirmation=true`，弹确认框要求勾选“作为修订版确认”并填 `confirmation_note`；调 `confirmImport`；409 `revision_confirmation_required` 时回到弹框；成功后显示 written 计数并把页面切为只读（记录已 `已确认`）
- 取消导入 → 二次确认后调 `cancelImport`，成功后返回桥梁详情页

**Steps:**

- [ ] Step 1: 写失败 Vitest：确认按钮解锁逻辑纯函数 `canPressConfirm(preflight)`、修订弹框必填校验 `validateRevisionForm(checked, note)` 两条
- [ ] Step 2: 实现接线 → `npm run test -- --run`、`npm run build` 全绿
- [ ] Step 3: 端到端手工验证（记录到 README 模块 05 小节）：
  1. `scripts/dev/seed-module05-review-sample.ps1` 种子
  2. 启动后端/前端，走完：改一个病害字段 → 保存草稿 → psql 查 `parsed_result_json` 已变 → 批量确认 → 入库前检查（无阻断）→ 确认入库 → psql 查四张正式表行数与 `inspection_years.status='已确认'`
  3. 再次对同桥同年种子第二条导入记录 → 入库前检查 `requires_revision_confirmation=true` → 不勾修订被 409 拒 → 勾选后成功，旧年度行 `已被修订`、新行 `version_number=2`
- [ ] Step 4: 全量回归：`ctest --preset vs2022-x64-debug`（含 env 集成测试）、`npm run test -- --run`、`npm run build`、`cd tools-python && uv run pytest -q`（应不受影响）
- [ ] Step 5: 文档：README 增加模块 05 使用说明（API 清单 + 种子脚本 + 页面入口）；模块 05 规格变更记录加一行实施完成；PROJECT_CONTEXT 当前进度更新
- [ ] Step 6: `git status --short` 确认无 `test-inputs/`、`test-output/` 被暂存

**Test:** 两条 Vitest + 三端全量回归 + 端到端手工清单。
**Commit:** `feat(frontend): wire review confirm flow`（代码）+ `docs: document module05 review workspace`（文档）

---

## Self-Review

**规格覆盖对照（模块 05 §15 验收标准）：**

| 验收项 | 任务 |
| --- | --- |
| 1 桥梁年度导入记录进入校对页 | Task 2、11 |
| 2 读取并展示 parsed_result_json | Task 3、13 |
| 3 导入级与对象级 warning/error 展示 | Task 12（分组）、13（OverviewHeader/NeedsAttention） |
| 4 编辑病害核心字段 | Task 12（reducer 白名单）、13（DefectsSection） |
| 5 编辑照片匹配状态 | Task 12（四个照片 action）、13（PhotosSection） |
| 6 编辑评分核心字段 | Task 12、13（RatingsSection） |
| 7 批量确认普通候选 | Task 12（isNormal* + batch_confirm_normal） |
| 8 保存校对草稿 | Task 4（后端）、14（前端接线） |
| 9 入库前检查与可读错误 | Task 5、6、14 |
| 10 确认入库写四张正式表 | Task 7、8 |
| 11 修订版显式确认 | Task 5（requires_revision_confirmation）、8（事务规则）、14（弹框） |
| 12 不生成对比候选 | 全程未触碰 comparison_candidates（Global Constraints 2） |

**测试建议对照（§14）：** 1→Task 4/5；2→Task 4；3→Task 12；4→Task 5/8；5→Task 7；6/7/8/9/10→Task 7/8；11/12→Task 8；13→Task 8（回滚测试）；14→Task 12/13。

**占位符扫描：** 全文无 TODO/TBD/“以后补”；每个错误码都有触发条件；每个映射都有具体列名。

**接口一致性检查：**
- `build_preflight_report` 的 `PreflightContext` 字段与 Task 6 端点组装一致。
- `ConfirmPlan` 结构在 Task 7 定义、Task 8 消费，字段名一致（`defect_description_raw`、`scale`、`photo_title`）。
- 前端 `ReviewResponse` 等类型对应 Task 3/5/8 响应 JSON；`reviewDraftReducer` 在 Task 12 定义、Task 13/14 消费。
- 错误码全集：`bridge_not_found`、`import_record_not_found`、`import_record_not_editable`、`invalid_json_body`、`contract_validation_failed`、`import_context_mismatch`、`import_record_wrong_status`、`candidate_pending_review`、`defect_missing_required_field`、`photo_link_unresolved`、`rating_overall_missing`、`revision_confirmation_required`、`db_write_failed`、`db_unavailable`（503，数据库不可达/查询超时）；前后端引用同一份清单。
- 契约零扩展：照片无 review_note/无可编辑题注；评分无 review_note；`rule_profile` 不进契约，顶部用 `importer_name` 展示。
