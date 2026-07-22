# 导入绑定界面 + 进入校对前置 + 校对页构件只读 实施计划

> **For agentic workers:** 逐任务实施；每步 `- [ ]` 勾选。先写失败测试 → 跑到失败 → 最小实现 → 跑到通过 → 提交。

**Goal:** 在导入解析（`import_status='待校对'`）之后、正式确认入库之前，新增 spec §4-D 的**绑定界面**：按部件类别分组核对、行=报告里出现的每个不同构件编号、绑一次挂上所有引用它的病害、支持"标记缺失"、进度与"进入校对"；把"标记缺失"纳入确认入库门槛；并按 §4-E 把校对页"实际构件"改为只读展示。绑定状态直接改写导入记录的 `parsed_result_json.defects[]`（用户已定：绑定是导入记录上的独立步骤，与校对解耦）。

**Architecture:** 绑定不新增表：复用每条病害已有的绑定字段（`bridge_component_id` / `component_match_method` / `component_match_candidate_ids` / `standard_component_category_id` / `resolved_structure_part` / `component_inventory_revision_id`）。新增后端**绑定仓储 + 路由**在 `待校对` 相内读/改 `parsed_result_json`：分组绑定视图查询、按（类别 + 归一化编号）批量绑定/改绑、标记缺失、取消标记。**行单位 = 报告里不同的 (类别, 归一化编号)**；绑定/标记作用于所有该组病害。`component_match_method` 扩为 `exact|confirmed_alias|manual|missing|none`；`missing` 表示台账确无此构件（`bridge_component_id=null` 但视为已处理）。确认入库门槛（现有 preflight/ConfirmPlan）扩为"每条病害必须已绑定或已标记缺失"。前置：绑定与确认都要求台账已确认（沿用现状）。

**Tech Stack:** 后端 C++20 / Drogon / GoogleTest（`backend-cpp/src/db/WordImportRepository.*` 或新增 `ImportBindingRepository.*`、`src/http/*Routes.cpp`）；前端 React + TS + Vitest（`frontend/src/review/*`、新增绑定页/区）。

**真值来源：** spec §4-D/§4-E/§6.1；导入状态 `import_records.import_status ∈ ('已上传','解析中','待校对','已确认','解析失败','已取消')`；现有匹配写入 `parsed_result_json.defects[]`（`WordImportRepository.match_imported_defects`）。

**决策点（部分已定，其余待复核）：**
1. **绑定位置**（已定）：导入记录上的独立步骤，绑定端点改写 `parsed_result_json`。
2. **标记缺失表示**：`component_match_method="missing"` + `bridge_component_id=null`（不新增列/表）。是否需要"缺失原因"文本？初版**不要**，只记状态。
3. **行分组键**：`(标准类别 id, 归一化编号)`。同一编号跨类别极少，用类别+编号更稳。**待确认**是否只按编号（若报告同编号必同类别）。
4. **进入校对 vs 确认门槛**："进入校对"是前端软跳转（允许"稍后再绑"）；硬门槛在确认入库：未绑定且未标记缺失 → 阻塞（复用现有 blocker，新增 `missing` 视为已处理）。
5. **§4-E 只读**：校对页 `ComponentMatchField` 改为只读展示 + 一个"去绑定界面"入口；本计划最后一个任务做。

---

## 文件结构

- 新增 `backend-cpp/include/bridge_report/db/ImportBindingRepository.hpp` / `src/db/ImportBindingRepository.cpp`：`binding_overview(import_id)`、`bind_component(import_id, category, number, bridge_component_id)`、`mark_missing(...)`、`clear_binding(...)`——均在 `待校对` 相读改 `parsed_result_json`。
- 新增 `backend-cpp/src/http/ImportBindingRoutes.cpp` / `include/.../ImportBindingRoutes.hpp`：`GET /api/import-records/{id}/component-binding`、`POST …/bind`、`POST …/mark-missing`、`POST …/clear`。
- 修改确认门槛：`src/review/PreflightReport.cpp` / `ConfirmPlan.cpp`（`missing` 视为已处理；未处理项阻塞）。
- 前端：新增 `frontend/src/review/binding/*`（绑定页 + 分组核对表 + 行级搜索选择器，复用 `ComponentMatchField` 选择逻辑）；`frontend/src/api/importBindingApi.ts`；`review/components/ComponentMatchField.tsx` 改只读（§4-E）。
- 测试：`backend-cpp/tests/test_import_binding_repository.cpp`、`test_import_binding_routes.cpp`、既有 `test_preflight_report.cpp`；前端 `importBindingApi.test.ts`、绑定页组件测试、`ComponentMatchField.test.tsx`。
- `backend-cpp/CMakeLists.txt`、Drogon 路由注册处登记新增。

约定：新增 `.cpp` 必须加入 `CMakeLists.txt` 的 core 与 tests；新路由在 `main.cpp`/路由注册聚合处登记。

---

## Task 1: 绑定仓储（读改 parsed_result_json）

**Files:** Create `ImportBindingRepository.{hpp,cpp}`；Test `test_import_binding_repository.cpp`；`CMakeLists.txt`。

数据结构（返回给路由/前端）：
```cpp
struct BindingRow { std::string category_id, structure_part, component_number;   // 报告原文编号
                    int defect_count; std::string status;                        // bound|ambiguous|unmatched|missing
                    std::optional<std::string> bridge_component_id;
                    std::vector<std::string> candidate_component_ids; };
struct BindingGroup { std::string category_id, category_label; int total, bound, unmatched, ambiguous, missing;
                      std::vector<BindingRow> rows; };
struct BindingOverview { bool inventory_confirmed; std::vector<BindingGroup> groups; };
```

- [ ] **Step 1: 失败测试**（隔离 schema）：造一条 `待校对` 导入记录，`parsed_result_json.defects` 含同一 (类别,编号) 被 3 条病害引用 + 一条未匹配。断言 `binding_overview` 分组、`defect_count`、`status` 正确；`bind_component` 后该组 3 条病害全部 `bridge_component_id` 落定且 `method="manual"`；`mark_missing` 后 `status="missing"`；`clear_binding` 复位。非 `待校对` 记录改写返回冲突。
- [ ] **Step 2: 跑到失败**。
- [ ] **Step 3: 最小实现** — 读 `import_records.parsed_result_json`（要求 `import_status='待校对'`），聚合分组；写操作对匹配 (category, normalize_number) 的所有 defect 就地改 `bridge_component_id`/`component_match_method`，回写 `parsed_result_json`（事务 + `for update`）。`bind_component` 需校验 `bridge_component_id` 属于该桥已确认台账且其活动映射类别 ∈ 该行类别候选。
- [ ] **Step 4: 跑到通过**。
- [ ] **Step 5: 提交** `feat(import): component binding repository over parsed defects`

---

## Task 2: 绑定路由

**Files:** Create `ImportBindingRoutes.{hpp,cpp}`；注册；Test `test_import_binding_routes.cpp`。

- [ ] **Step 1: 失败测试** — 解析请求体（`component_number`/`category_id`/`bridge_component_id` 必填校验）；鉴权；台账未确认 → 明确错误（引导先确认台账）。
- [ ] **Step 2/3:** `GET …/component-binding` → overview；`POST …/bind`、`…/mark-missing`、`…/clear` → 调仓储后回 overview。UUID 校验、`respond_*` 复用。
- [ ] **Step 4/5:** 通过 → 提交 `feat(import): component binding endpoints`

---

## Task 3: 确认入库门槛纳入"标记缺失"

**Files:** Modify `PreflightReport.cpp` / `ConfirmPlan.cpp`；Test `test_preflight_report.cpp`。

- [ ] **Step 1: 失败测试** — 一条病害未绑定且未标记缺失 → 确认被阻塞（现有行为）；标记缺失后 → 不再阻塞、且不会给评定造缺构件。
- [ ] **Step 2/3:** 未匹配阻塞判定改为"`bridge_component_id` 为空 **且** `method != 'missing'`"；`missing` 的病害在入库时不落实际构件（按现状"未关联"处理，不计入需评定构件）。
- [ ] **Step 4/5:** 通过 → 提交 `feat(review): treat marked-missing defects as resolved at confirm`

---

## Task 4: 前端绑定 API + 绑定页

**Files:** Create `frontend/src/api/importBindingApi.ts`（+test）、`frontend/src/review/binding/ComponentBindingWorkspace.tsx`（+test）。

- [ ] **Step 1: 失败测试** — api：GET overview / POST bind / mark-missing / clear 的 URL 与请求体；组件：分组核对表渲染（每组一行：类别｜数量｜绑定情况徽章｜处理）；点"处理"展开行；行显示 报告编号｜引用N条｜台账构件｜状态徽章｜操作；未匹配/歧义点"选择构件"就地搜索选（复用 `ComponentMatchField` 选择逻辑，按同类别预筛）；"标记缺失"；底部进度 + "全部绑定完成，进入校对"/"稍后再绑"。
- [ ] **Step 2/3:** 实现（复用现有分组核对表样式与搜索式选择器；绑定/标记调 api 后刷新 overview）。
- [ ] **Step 4/5:** `vitest` + `npm run build` → 提交 `feat(review): component binding workspace`

---

## Task 5: 绑定页接入路由 + 入口

**Files:** Modify 导入/校对相关页面（`ReviewWorkspacePage`/`InspectionWorkspacePage` 或导入详情），加"绑定"步骤入口与路由。

- [ ] 在 `待校对` 导入记录上暴露"构件绑定"入口；"进入校对"跳到现有校对工作台。测试 + 构建 → 提交 `feat(review): route to component binding step`

---

## Task 6: 校对页实际构件只读（§4-E）

**Files:** Modify `review/components/ComponentMatchField.tsx`（+test）。

- [ ] `ComponentMatchField` 改为**只读展示**已绑定构件（编号/名称/状态徽章）+ "去绑定界面调整"链接；移除逐条搜索选择。更新 `ComponentMatchField.test.tsx` 与依赖它的 `DefectsSection` 测试。测试 + 构建 → 提交 `feat(review): read-only component display on review page`

---

## Task 7: 全量回归

- [ ] `scripts/dev/check-backend-tests.ps1`（隔离 schema 全绿；`PGCLIENTENCODING=UTF8`）。
- [ ] `cmake --build --preset vs2022-x64-debug`（exe 先停；MSB3073/discovery flake 忽略）。
- [ ] 前端 `npm run test` + `npm run build`。
- [ ] `git diff --check`；保护目录不提交。提交（如有清理）。

---

## Self-Review 结论

- **Spec 覆盖**：实现 §4-D 绑定（分组核对、行=编号、绑一次挂全部、手动搜索绑、标记缺失、进入校对、不做记忆持久化）、§4-E 校对只读、§7 前置（台账已确认、确认前必须绑定或标记缺失）。
- **不新增表**：绑定复用病害现有字段，写入导入记录 `parsed_result_json`，与用户所选"导入记录上的独立步骤"一致。
- **门槛安全**：`missing` 显式记录，不静默放过；确认入库阻塞逻辑扩展而非削弱。
- **待复核**：标记缺失是否需原因文本（初版不要）；行分组键仅编号还是类别+编号；绑定页是独立页还是导入详情内区块。

## 后续（不在本计划）

非梁桥型上部编号形状真实报告校准（只改目录数据 + `provisional`）；绑定记忆/跨年套用明确为**非目标**（每次导入各绑各的）。
