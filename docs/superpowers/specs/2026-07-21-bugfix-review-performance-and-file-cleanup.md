# 2026-07-21 缺陷修复记录：校对页性能与删除文件清理

> 日期：2026-07-21
>
> 分支：`codex/06-5-interaction-redesign`
>
> 涉及提交：`7743b33`、`8f5aa2d`、`53e91e5`
>
> 触发背景：真实规模数据首次进入系统——百股大桥构件台账 5173 个实际构件，单次 Word 导入 279 条病害、559 条待处理警告。此前所有页面只在小样例（约 25 条病害、31 张照片）下验证过，三个缺陷全部属于"数据量放大后才暴露"的一类。

## 缺陷一：桥梁删除后独占文件长期"删不掉"，且成功弹窗误报错误

提交：`7743b33`（feat(bridge-delete): show file-cleanup progress and drain on poll）

### 现象

- 管理员永久删除桥梁档案后，弹窗提示"141 个孤立归档文件等待后台清理"，之后多次删除桥梁都观察到文件迟迟不消失，被误认为"删除失败"。
- 删除成功的同一个弹窗里同时出现红色错误"删除影响加载失败。"。

### 根因

数据库实测：`bridge_archived_file_deletion_queue` 中 141 项全部 `attempt_count=0`（从未被尝试），25 项已完成且均一次成功——**清理没有失败过，是根本没被执行**。三个因素叠加：

1. 删除路由只在事务提交后调用一次 `process_bridge_audit`，而协调器单次只领取 `cleanup_batch_size=25` 项；
2. 其余项依赖每 `cleanup_interval_seconds=300`（5 分钟）一次的后台定时器，每轮同样只清 25 项——166 个文件需要约 30 分钟；
3. 开发期间后端进程被频繁重启（重编译需先停 exe），每次重启只在启动时清一批 25 项；重启间隔短于 5 分钟时定时器几乎没有机会运行，队列长期停留在"待清理"。

红色报错则是前端独立缺陷：`DeleteBridgesDialog` 的影响预览 effect 依赖 `bridgeIds`，删除成功后父组件重渲染触发它重新拉取已删除桥梁的删除影响预览，后端返回 404，前端把它显示成错误——实际上删除已成功。

### 修复

- 后端 `BridgeAdministrationRoutes.cpp`：
  - 删除结果新增 `total_file_count`（该桥独占文件总数，来自删除前预览计数）；
  - 新增管理员接口 `POST /api/bridge-deletion-audits/{audit_id}/cleanup/advance`：每次调用先 `process_bridge_audit` 推进一批清理，再返回 `{total, completed, failed, pending, done}`。审计不存在返回 404。
- 前端 `DeleteBridgesDialog.tsx`：
  - 删除成功后按约 800ms 间隔轮询 advance 接口，弹窗内显示确定性进度条"已清理 n / 总数"；清完显示"归档文件已全部清理"，某一轮无进展（剩余项进入退避重试）则收尾并提示"剩余 N 个由后台继续清理"；
  - 删除成功后不再重新拉取影响预览，消除"删除影响加载失败"误报。
- 效果：166 个文件按每轮 25 个、每秒左右一轮，约 7 秒内清完并有可视进度；不再依赖 5 分钟定时器和后端重启节奏。后台定时器仍保留，作为前端中途关闭弹窗时的兜底。

### 涉及文件

- `backend-cpp/src/http/BridgeAdministrationRoutes.cpp`
- `frontend/src/api/bridgeAdministrationApi.ts`（`advanceBridgeCleanup`、`BridgeCleanupProgress`）
- `frontend/src/bridges/DeleteBridgesDialog.tsx` 及测试、`frontend/src/api/bridgeAdministrationApi.test.ts`
- `frontend/src/styles.css`（进度条样式）

## 缺陷二：校对页"需要处理"列表在大数据量下卡顿

提交：`8f5aa2d`（perf(review): page and index the needs-attention list）

### 现象

559 条待处理警告时，校对页"需要处理"分组明显卡顿，每次编辑草稿（逐字输入）都出现可感知的停顿。

### 根因

1. `NeedsAttentionSection` 一次性渲染全部 559 行表格；
2. 每行渲染都调用 `attentionObjectLabel`（内部 `defects.findIndex`）和 `statusFor`（内部 `defects.find` / `photos.find`），559 行 × 279 条病害 × 2 ≈ 30 万次线性查找，且每次 draft 变动全量重算；
3. `needsAttention(draft)` 被计算两遍——一次在 `buildStatistics` 内部，一次在页面 `attentionItems` memo 中，而前者的结果随后又被 `displayedCounts` 覆盖，等于纯浪费。

### 修复

- `ReviewWorkspacePage`：`needsAttention(draft)` 只算一遍（`draftAttention` memo），`buildStatistics` 新增可选参数直接接收长度，列表与统计复用同一份结果；
- `NeedsAttentionSection`：预建 `候选ID → 序号 / 状态 / 照片` 三个 `Map`，行内查找降为 O(1)；错误、警告两张表各自按每页 100 行分页。

### 涉及文件

- `frontend/src/pages/ReviewWorkspacePage.tsx`
- `frontend/src/review/grouping.ts`（`buildStatistics` 增加 `precomputedAttentionCount`）
- `frontend/src/review/components/NeedsAttentionSection.tsx` 及测试
- `frontend/src/styles.css`

## 缺陷三：切换"病害与照片"整页冻结，全部按钮失灵

提交：`53e91e5`（perf(review): bound component options and page defect cards）

### 现象

台账 5173 构件 + 279 条病害时，切到"病害与照片"分组后页面完全冻结：分组无法切换、底部"返回 / 保存草稿"等按钮全部无响应。

### 根因

每张病害卡的"实际构件"字段（`ComponentMatchField`）把**全量可用台账条目渲染成 `<select>` 的 `<option>` 列表**，并对 5173 条做逐卡 filter + sort：

```text
279 张病害卡 × (5173 + 1) 个 option ≈ 1,443,000 个 DOM 节点
```

一次性构建百万级 DOM 使浏览器主线程长时间阻塞，表现为整页假死而非普通卡顿。即使不冻结，5173 项的下拉列表本身也无法供人使用。

### 修复

- `ComponentMatchField` 改为**受限渲染的搜索式选择器**：
  - 下拉只包含"当前已关联条目 + 系统匹配候选 + 搜索命中前 20 项"，每卡 option 数量有硬上限；
  - 新增"输入编号或名称搜索"输入框，按 `component_number` / `site_component_type` / `site_name` 包含匹配；
  - 无候选且搜索无命中时显示"没有匹配的构件"占位；台账未确认提示保留。
- `DefectsSection` 病害卡片**分页**：每页 50 张，底部翻页；从"需要处理"点击某条病害时，通过渲染期状态同步自动翻到该病害所在页，父层的滚动高亮在同一次提交内即可命中锚点。分页同时把每次输入触发的重渲染范围从全部病害缩小到当前页。

### 涉及文件

- `frontend/src/review/components/ComponentMatchField.tsx` 及测试
- `frontend/src/review/components/DefectsSection.tsx` 及测试
- `frontend/src/styles.css`

## 附：同日定位的数据配置问题（非代码缺陷）

559 条"未找到可唯一关联的实际构件"警告本身不是解析或匹配代码缺陷。数据库实测比对：

| | 构件台账 | Word 病害表 |
| --- | --- | --- |
| 构件名 | `空心板` | `上部承重构件` |
| 构件编号 | `1-1#` … `33-25#` | `33-25#板`（带"板"后缀） |

构件匹配要求"病害构件类别文本 == 台账构件名/别名"且编号规范化后相等；名称与编号两处都不一致（编号规范化只去尾部 `#`，去不掉"板"字），因此 279 条病害全部落入人工选择。另外该检测年度尚未锁定"规范组合 + 已确认台账"，系统评定试算同时报"检测年度尚未配置规范组合和构件台账"。

处理属于数据对齐决策，可选：

1. 将台账中承重构件组的构件名改为 `上部承重构件`、编号后缀改为 `#板`，重新解析导入；
2. 或为相关构件添加已确认别名并统一编号写法；
3. 或利用修复三提供的搜索式选择逐条人工关联（数量大时不推荐）。

## 回归验证

- 前端全量 237 项测试通过，生产构建通过（三次提交各自跑过全量）；
- C++ 全量隔离 schema 检查通过（14 个迁移连续应用两遍、10 个 smoke；涉及后端改动的提交）；
- 新增测试覆盖：清理轮询进度条推进至完成、删除成功后不重拉影响预览、待处理列表分页、构件下拉受限渲染与搜索、病害卡分页与选中跳页；
- `git diff --check` 通过；`.claude/`、`backend-cpp/archive/` 等保护目录未纳入提交。

## 经验教训

1. **列表类界面必须假设真实规模**：本项目单桥构件可达数千、单次导入病害数百，任何"全量渲染 + 行内线性查找"的写法都会在真实数据下失效。新列表一律带分页/按需渲染，行内查找预建索引。
2. **下拉框不是无界容器**：选项来源是数据库表的 `<select>` 必须限定渲染集合（候选 + 搜索），全量渲染既是性能炸弹也是可用性炸弹。
3. **后台队列要暴露进度**：仅靠"定时器慢慢清"的设计在开发期（频繁重启）会退化成"永远清不完"；把推进能力暴露给触发方（删除后轮询推进）既解决体验也解决吞吐。
4. **成功路径上的派生请求要考虑资源已消失**：删除成功后任何以"被删对象"为参数的自动刷新都必须跳过，否则 404 会被误报为操作失败。
