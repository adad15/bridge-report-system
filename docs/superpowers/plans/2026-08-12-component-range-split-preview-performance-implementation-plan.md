# 实施计划：构件范围拆分预览性能优化

设计文档：[2026-08-12-component-range-split-preview-performance-design.md](../specs/2026-08-12-component-range-split-preview-performance-design.md)

日期：2026-08-12

## 实施目标

将构件范围拆分拆成“轻量分析”和“正式物化”两个阶段。点击拆分按钮后立即打开加载态弹框；预览接口只验证范围、计算摘要和影响令牌，不再复制完整病害、照片和警告 JSON；确认时在事务内重新分析、校验令牌，再生成并保存完整结果。

## 实施约束

- 不修改范围语法、拆分资格、数量上限、评分语义或错误码。
- 不改变正式拆分生成的病害、照片、警告、来源和绑定结果。
- 前端仍只提交原始目标和不透明影响令牌。
- 预览和应用必须共享同一个分析器，避免统计与正式结果分叉。
- 本轮不增加跨请求缓存或后台任务。
- 已确认台账修订版按现有数据库保护规则视为不可原地修改。

## Task 1：轻量分析模型与测试

**修改：**

- `backend-cpp/include/bridge_report/review/ComponentRangeSplitPlanner.hpp`
- `backend-cpp/src/review/ComponentRangeSplitPlanner.cpp`
- `backend-cpp/tests/test_component_range_split_planner.cpp`

步骤：

- [x] 新增 `ComponentRangeSplitAnalysis`，保存摘要及物化所需的最小内部信息，不含 `result_json`。
- [x] 新增 `analyze_component_range_splits()`，完成目标验证、范围展开、来源病害定位、照片计数和构件匹配统计。
- [x] 新增 `materialize_component_range_splits()`，消费已验证分析结果生成完整结果。
- [x] 保留 `plan_component_range_splits()` 作为兼容组合入口，内部调用分析和物化。
- [x] 测试轻量分析统计、错误语义及正式物化结果与现有行为一致。

## Task 2：预览仓储与轻量影响令牌

**修改：**

- `backend-cpp/include/bridge_report/db/ComponentRangeSplitRepository.hpp`
- `backend-cpp/src/db/ComponentRangeSplitRepository.cpp`
- `backend-cpp/src/http/ImportBindingRoutes.cpp`
- 相关后端测试

步骤：

- [x] 预览仓储只调用轻量分析器，不生成完整计划。
- [x] 响应序列化改为读取分析摘要，保持外部 JSON 形状兼容。
- [x] 令牌改为覆盖导入 ID、候选 JSON 摘要、台账修订版 ID、规范目标和分析摘要。
- [x] 应用在事务内重新分析和比较令牌，通过后才物化完整结果。
- [x] 增加读取、分析、令牌、物化、持久化和总耗时日志。

## Task 3：前端即时弹框与异步保护

**修改：**

- `frontend/src/review/binding/ComponentBindingWorkspace.tsx`
- `frontend/src/review/binding/ComponentRangeSplitDialog.tsx`
- 对应测试

步骤：

- [x] 点击后立即保存目标快照并打开 `loading` 弹框，再请求预览。
- [x] 弹框支持 `loading`、`ready` 和 `error`，计算时禁用确认。
- [x] 预览失败留在弹框内，支持重新计算和关闭。
- [x] 使用请求序号或取消标志忽略关闭后、重试后及切换导入记录后的迟到响应。
- [x] 正式确认使用弹框打开时的目标快照，不受后台选择变化影响。

## Task 4：验证与性能检查

步骤：

- [x] 运行拆分规划器和构件绑定聚焦测试。
- [x] 运行完整前端测试和生产构建。
- [x] 运行完整后端隔离数据库测试。
- [x] 检查预览路径没有物化 `result_json`。
- [x] 用可用真实记录观察分段日志；若没有稳定可复用的认证测试数据，则记录为人工验收项，不伪造性能结论。

实测记录（Debug 集成测试数据，75 条结果病害）：预览总耗时约 `35ms`，其中台账读取 `21ms`、轻量分析 `11ms`、令牌生成 `1ms`；正式应用总耗时约 `68ms`，其中完整物化 `9ms`、持久化 `47ms`。该数据用于验证优化方向和分段日志，不替代生产环境验收。

验证命令：

```powershell
npm run test -- --run src/review/binding/ComponentRangeSplitDialog.test.tsx src/review/binding/ComponentBindingWorkspace.test.tsx
npm run test -- --run
npm run build
cmake --build --preset vs2022-x64-debug
.\scripts\dev\check-backend-tests.ps1
```

## 验收

- 点击拆分按钮后无需等待接口即可看到计算状态。
- 预览响应形状保持兼容，预览代码不生成完整拆分 JSON。
- 预览期间数据变化会使确认返回过期错误。
- 正式拆分结果与优化前一致，任一失败仍整批零写入。
- 自动测试不使用不稳定的毫秒硬阈值；真实 Release 环境以常见 25～50 个展开构件预览 `500ms` 为目标。
