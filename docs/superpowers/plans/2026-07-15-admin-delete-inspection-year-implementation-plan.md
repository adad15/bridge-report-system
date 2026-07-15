# 管理员永久删除年度检测实施计划

> 日期：2026-07-15
>
> 设计真源：`docs/superpowers/specs/modules/06-5-admin-delete-inspection-year.md`
>
> 实施分支：`codex/06-5-interaction-redesign`
>
> 基线提交：`87b0337`

## 1. 目标与边界

为模块 06.5 增加管理员专属的 C1 永久删除能力：从任意年度版本定位“桥梁 + 年度”，预览并永久删除该年度 V1、V2 等全部版本及其年度业务数据；保留桥梁构件和其他年度的跨年病害线索；数据库提交后安全清理独占归档文件，并永久保留删除审计。

本计划不实现回收站、版本回滚、删除审批、普通用户申请或审计查询页面。

## 2. 全局约束

1. 不提交或删除 `.claude/`、`test-inputs/`、`test-output/`、`backend-cpp/archive/`、`tools-python/archive/`。
2. 只有管理员可预览和执行删除，前后端都要校验，后端为最终边界。
3. API 不返回绝对路径或 `storage_relative_path`。
4. 数据库事务提交前不得删除磁盘文件。
5. 删除接口不得隐式强制解除编辑锁。
6. 删除范围改变时旧影响令牌必须失效。

## 3. 实施任务

### Task 1：数据库迁移与 smoke test

**文件**：新增 `database/migrations/006_inspection_year_deletion.sql`，修改数据库 smoke test。

新增：

- `inspection_year_deletion_audits`：桥梁/管理员快照、年度、原因、影响与删除数量 JSON、文件清理状态和时间；
- `archived_file_deletion_queue`：审计 ID、安全相对路径、状态、尝试次数、错误和完成时间；
- 必要索引和状态检查约束；
- 审计表不引用即将删除的年度或导入记录。

**测试**：迁移可重复应用；约束、默认值、外键删除行为和索引存在。

**建议提交**：`feat(database): add annual deletion audit tables`

### Task 2：规范删除计划与影响令牌

**文件**：新增 `InspectionYearDeletionModels.hpp/.cpp`、纯函数测试。

实现稳定模型：桥梁快照、版本列表、各类计数、活动锁摘要、候选文件 ID、受影响线索与对比 ID。按固定字段和排序生成规范 JSON，并计算 SHA-256 影响令牌。API JSON 只输出数量和锁摘要，不输出文件 ID 或路径。

**测试**：相同集合不同查询顺序产生相同令牌；新增/删除目标记录令牌变化；响应脱敏；确认文字为 `永久删除 <年度>`。

**建议提交**：`feat(deletion): model annual deletion impact`

### Task 3：影响预览仓储查询

**文件**：新增 `InspectionYearDeletionRepository.hpp/.cpp`、PostgreSQL 集成测试。

从任意年度版本 ID 定位桥梁和年度，聚合全部版本及：

- 导入记录、活动编辑锁；
- 病害观测、尺寸、正式照片、评分；
- 对比结论和受影响线索；
- 与目标年度或导入关联的候选归档文件；
- 删除后仍有外部引用的共享文件。

预览查询不写数据库，不取得编辑锁。

**测试**：V1/V2 聚合、未知年度、活动/过期锁、共享文件、跨年线索和对比计数。

**建议提交**：`feat(deletion): preview annual deletion impact`

### Task 4：单事务永久删除

**文件**：扩展删除仓储与集成测试。

事务内重新构造并锁定删除计划，复核影响令牌和活动锁，然后：

1. 写入删除审计和文件清理队列；
2. 删除目标年度相关对比；
3. 删除目标导入记录及锁、文件关联；
4. 删除目标年度观测（级联尺寸和正式照片）及评分；
5. 清理修订引用并删除同年度所有版本；
6. 重算保留线索首末年度，删除空线索；
7. 删除已无数据库引用的归档元数据；
8. 保留桥梁构件、其他年度观测和共享文件；
9. 返回删除数量与剩余最新年度 ID。

**测试**：V1/V2 同删、事实和导入删除、跨年线索重算、空线索删除、构件保留、共享文件保留、旧令牌 409 结果、事务故障回滚。

**建议提交**：`feat(deletion): permanently delete annual facts`

### Task 5：文件清理队列

**文件**：新增 `ArchivedFileDeletionQueue.hpp/.cpp`、测试，修改 `main.cpp`。

事务提交后立即处理本次队列；所有路径经 `resolve_path_under_root` 验证。不存在视为成功，失败记录次数与错误。后端启动时只处理有界批次的未完成项。清理结果更新审计状态。

**测试**：成功删除、幂等、越界拒绝、文件占用失败入队、后续重试成功、数据库回滚时没有文件清理。

**建议提交**：`feat(archive): clean files after annual deletion`

### Task 6：管理员删除 API

**文件**：新增 `InspectionYearDeletionRoutes.hpp/.cpp`、路由测试，修改 CMake 与 `main.cpp`。

接口：

```text
GET    /api/inspection-years/{id}/deletion-impact
DELETE /api/inspection-years/{id}
```

实现认证、管理员角色、UUID、JSON、原因、确认文字、稳定错误码、锁冲突和令牌冲突。DELETE 成功提交后调用文件清理器。响应只返回删除数量、待清理数量和下一年度 ID。

**测试**：401、403、404、400、活动锁 409、令牌变化 409、成功删除、响应不泄漏路径。

**建议提交**：`feat(api): expose admin annual deletion`

### Task 7：前端 API 与状态

**文件**：修改 `workspaceApi.ts` 及测试，新增删除影响类型和错误映射。

新增 `fetchInspectionYearDeletionImpact`、`deleteInspectionYear`。DELETE 请求严格携带原因、确认文字和影响令牌；所有动态段 URL 编码。稳定错误码映射为可操作中文提示。

**测试**：请求路径/方法/JSON、锁摘要、409 映射、未知错误保留后端信息。

**建议提交**：`feat(frontend): add annual deletion contracts`

### Task 8：管理员菜单与危险确认弹窗

**文件**：新增 `DeleteInspectionYearDialog.tsx` 及测试，修改 `InspectionWorkspacePage.tsx`、样式。

年度标题右上角增加管理员专属“更多 → 永久删除年度”。弹窗打开即重新预览，显示桥梁、版本、计数、共享文件和锁；原因与精确确认文字合法后才启用红色按钮。提交中禁止关闭与重复提交。

删除成功后刷新概览和年份，按 `next_inspection_year_id` 跳转；无剩余年度进入空状态。409 数据变化保留弹窗并要求重新预览；活动锁显示编辑人并禁用提交。

**测试**：普通用户不显示、管理员显示、加载/失败/锁定、表单门槛、防重复、成功导航、空年度和令牌变化。

**建议提交**：`feat(ui): let admins permanently delete annual inspections`

### Task 9：回归、文档与保护目录审计

更新 README、PROJECT_CONTEXT 和设计/计划状态。执行：

```powershell
Set-Location tools-python
uv run pytest -q
$env:BRIDGE_REPORT_REAL_WORD_PATH='D:\vs2022 code\bridge-report-system\test-inputs\word-import\绕阳河二号桥报告b8e246e8-cd4d-4202-8d4d-49c56dd34389.docx'
uv run pytest -q tests/importers/test_real_word_regression.py

Set-Location ..\backend-cpp
cmake --build --preset vs2022-x64-debug
$env:BRIDGE_REPORT_TEST_DATABASE_URL='postgresql://bridge_report:bridge_report_dev@127.0.0.1:5432/bridge_report_system'
.\build\vs-debug\Debug\bridge_report_backend_tests.exe --gtest_brief=1

Set-Location ..\frontend
npm run test -- --run
npm run build

Set-Location ..
git diff --check
git status --short
```

手工验证：管理员分别删除待校对年度、含 V1/V2 的已确认年度和含跨年线索的年度；普通用户无入口；活动编辑锁阻断；剩余年度和空状态导航正确。

**建议提交**：`docs: complete admin annual deletion`

## 4. 依赖顺序

```text
Task 1
  └─ Task 2 ─ Task 3 ─ Task 4 ─ Task 5 ─ Task 6
                                  └────────── Task 7 ─ Task 8
Task 6 + Task 8 ─ Task 9
```

## 5. 完成标准

- 管理员可以在警告弹窗中永久删除同桥同年的全部版本；
- 普通用户无法发现或调用删除能力；
- 活动编辑锁和陈旧影响令牌能阻断删除；
- 年度事实、导入、对比和独占文件按设计清理；
- 其他年度线索与桥梁构件保持正确；
- 审计与失败文件队列可追踪；
- API 不泄漏路径；
- 全量测试、构建、真实 Word 回归和保护目录审计通过。

