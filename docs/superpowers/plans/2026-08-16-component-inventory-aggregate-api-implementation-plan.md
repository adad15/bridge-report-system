# 实施计划：构件台账聚合接口

设计文档：[2026-08-16-component-inventory-aggregate-api-design.md](../specs/2026-08-16-component-inventory-aggregate-api-design.md)

日期：2026-08-16

## 实施目标

把构件台账页从"下载全部构件在客户端汇总"改成"服务端聚合、按需取明细"。
首屏与单次写入的响应体从约 3.4 MB 降到 KB 级；顺带修复一桥可派生两条草稿分支的
既有缺陷，并把确认校验统一到一份服务端规则。

## 实施约束

- 不改造校对工作台。`/latest`、`load_revision()`、`inventory_revision_json()`
  因它在用而全部保留，那条路径的 3.4 MB 问题本轮不动。
- 不改 `generate_draft()` 的生成算法，不改向导，不改 `is_referenced` 语义。
- 不引入台账写操作的编辑锁，不做响应压缩。
- 两处有意的行为变更，其余一律要求与现状逐字段一致：
  1. 编号范围只统计启用构件；
  2. 分组计数按"任意生效映射"（`some`）而非"首个生效映射"。
- 已确认台账修订版按现有数据库保护规则视为不可原地修改。

## 工作区注意

当前分支工作区有约 55 个与本任务无关的未提交改动，其中
`frontend/src/styles.css` 含约 600 行他人在途工作。**每次提交只暂存本计划涉及的
文件，禁止 `git add -A` 或整目录暂存。** 若需修改 `styles.css`，先快照再按 hunk
取差集暂存。

## Task 0：记录基线

**修改：** 无（只记录）

步骤：

- [ ] 记录前端基线：`npm run test -- --run` 的文件数与用例数。
- [ ] 记录后端基线：`.\scripts\dev\check-backend-tests.ps1` 的通过情况。
- [ ] 记录当前 `git status --short | wc -l`，供后续核对未误伤他人改动。

验证命令：

```powershell
npm run test -- --run
```

## Task 1：单草稿不变量与 Superseded 错误码

与聚合改造互不依赖，可独立发布。修的是既有缺陷。

**修改：**

- `database/migrations/`（新增一份 migration）
- `backend-cpp/include/bridge_report/db/ComponentInventoryRepository.hpp`
- `backend-cpp/src/db/ComponentInventoryRepository.cpp`
- `backend-cpp/src/http/ComponentInventoryRoutes.cpp`
- `backend-cpp/tests/test_component_inventory_repository.cpp`
- `frontend/src/api/componentInventoryApi.ts`
- `frontend/src/bridges/ComponentInventoryEditor.tsx`

步骤：

- [ ] 新增 migration：
      `create unique index ux_component_inventory_single_draft on bridge_component_inventory_revisions (bridge_id) where status = '草稿';`
      （现网全库仅 1 个修订版，不冲突；上线前仍需核对目标库无一桥多草稿）。
- [ ] 派生草稿路径改为：在桥级锁内检查该桥是否已存在 baseline 不同的草稿，
      存在则返回新状态而不是新建第二条。
- [ ] `ComponentInventoryStatus` 新增 `Superseded`。
- [ ] `respond_inventory_outcome()` 增加分支：409 +
      `inventory_revision_superseded`。现有 `Conflict` 分支固定输出
      `component_inventory_conflict`，不要复用。
- [ ] 前端 `componentInventoryErrorMessage` 码表补 `inventory_revision_superseded`。
- [ ] 前端 `mutate()` 增加分支：收到该码时重新拉取台账并采纳新的 revision id，
      不是只弹错误——用户手里的 id 已不可写，只提示会让他反复点同一个按钮。
- [ ] 守门测试三条：同桥插入第二条草稿被索引拒绝；已有 R2 草稿时写 R1 返回
      `inventory_revision_superseded`；两个并发请求从不同已确认版本派生，
      最终至多一条草稿。

验证命令：

```powershell
.\scripts\dev\check-backend-tests.ps1
```

## Task 2：blocker 规则收敛为单一 SQL 片段

**修改：**

- `backend-cpp/include/bridge_report/db/ComponentInventoryRepository.hpp`
- `backend-cpp/src/db/ComponentInventoryRepository.cpp`
- `backend-cpp/tests/test_component_inventory_repository.cpp`
- `frontend/src/bridges/ComponentInventoryEditor.tsx`
- `frontend/src/bridges/ComponentInventoryEditor.test.tsx`

步骤：

- [ ] 新增 `blocker_cte_sql()`：**只产出具名 CTE 的 SQL 文本，不执行查询**。
      判定两条规则——"启用且无已确认生效映射的构件"与"启用构件数为 0"。
      命名不用 `compute_blockers()`，避免读成"执行并返回结果"。
- [ ] confirm 路径改为嵌入该片段，删除原地重写的那段聚合 SQL。
- [ ] 判定改为 `some` 口径（存在任一已确认生效映射即通过），与现有
      `count(...) >= 1` 语义一致，但要覆盖多生效映射的情形。
- [ ] 前端删除 `duplicate_component_number` 检查（数据库唯一约束已保证）。
- [ ] 删除 `ComponentInventoryEditor.test.tsx:290`
      `reports duplicate numbers before confirmation` 整个用例——规则已删，
      无可迁移去处。
- [ ] 现有 `renderEntryRow` / `confirmExistingMapping` 的状态判定改为
      `hasConfirmed = mappings.some(m => m.is_active && m.confirmation_status === '已确认')`；
      "确认映射"按钮仅在 `!hasConfirmed` 时出现，且确认一条明确选定的待确认生效
      映射，不再把"首个生效映射"原样重交。
- [ ] 一致性回归测试：构造"部分构件无生效映射"与"全部构件停用"两种台账，
      汇总侧与 confirm 侧报出的 code 与构件集合必须一致。

验证命令：

```powershell
npm run test -- --run src/bridges/ComponentInventoryEditor.test.tsx
.\scripts\dev\check-backend-tests.ps1
```

## Task 3：领域模型与序列化抽取

**修改：**

- `backend-cpp/include/bridge_report/inventory/ComponentInventoryModels.hpp`
- `backend-cpp/src/inventory/ComponentInventoryModels.cpp`
- `backend-cpp/include/bridge_report/db/ComponentInventoryRepository.hpp`

步骤：

- [ ] 新增模型：`InventoryRevisionSummary`、`InventoryGroupSummary`、
      `InventoryBlockerSummary`、`InventorySummary`、`InventoryEntryPage`、
      `InventorySearchResult`、`LocatedInventoryEntry`
      （`InventoryEntry` 已有 `site_component_type`，只需再加 `position`）。
- [ ] 约定：`total` / `page` / `size` / `position` 用 `int64_t`；可空字段用
      `std::optional`；`by_code` 是固定字段的 struct 而非 map；
      `samples` 截断前的总数记在 `InventoryBlockerSummary` 上。
- [ ] **不要复用 `InventoryRevision` 并把 `entries` 留空**——`/latest` 仍返回
      全量版，两者会被混用。
- [ ] 把 entry 与 mapping 的序列化**从 `inventory_revision_json()` 内部抽出**为
      `inventory_entry_json()` / `located_entry_json()`，供分页、搜索、写响应复用。
      `inventory_revision_json()` 本身保留（`/latest` 在用），改为调用抽出的函数。
- [ ] `ComponentInventoryOutcome` 改为
      `{ status, std::optional<InventorySummary> summary, std::optional<InventoryEntry> entry }`。
      **不能只带 `entry_id`**——事务外再按 id 加载会破坏 Task 6 的同快照要求。

## Task 4：汇总端点

**修改：**

- `backend-cpp/src/db/ComponentInventoryRepository.cpp`
- `backend-cpp/src/http/ComponentInventoryRoutes.cpp`
- `backend-cpp/tests/test_component_inventory_repository.cpp`
- `scripts/dev/`（新增一次性比对脚本）

步骤：

- [ ] 新增 `find_latest_revision_id(bridge_id)`：只做
      `order by (status='草稿') desc, revision_number desc limit 1` 取 id。
      **禁止用 `get_latest_revision()` 解析 latest**——它内部调 `get_revision()`
      做全量装配，复用它会让响应体虽小、后端仍跑一遍 3.4 MB，优化只做一半。
- [ ] 实现 `load_summary(executor, revision_id)`：**一条语句**，五个 CTE
      （`target` / `entry_mapping` / `numbered` / `grouped` / `blocker_entries`）
      加 `json_build_object` 组装。`blocker_entries` 用 Task 2 的
      `blocker_cte_sql()` 文本，不另写一份。
- [ ] `entry_mapping` 必须先按构件收敛成一行再 join——直接
      `left join ... and m.is_active` 会让多生效映射的构件展开成多行，
      `count(*)` 重复计数。
- [ ] 路由：`GET /api/component-inventories/{revision_id}/summary`，
      以及 `GET /api/bridges/{bridge_id}/component-inventories/latest/summary`
      （后者先解析 id 再转调前者）。
- [ ] 口径单测（**先写，早于比对脚本**）：33 孔编号范围不取字典序、含停用构件的
      分组、整组全部停用返回 `null` 范围且该组仍在、构件挂两个包的生效映射、
      多映射下的 `some` 口径、`structure_part` 两级取值、两组 `sort_order` 撞 0、
      `inventory_empty` 两种成因、`/latest/summary` 不经 `get_revision()`。
- [ ] 一次性比对脚本：真实数据上跑旧 `inventoryGroupSummaries` 与新汇总，
      18 组逐字段比对。**先定义投影**——旧 `mappingLabel` 与新
      `standard_package_id` + `standard_component_category_id` 不是一一对应，
      须用同一份 catalog 解析后再比；`null` 编号范围按界面口径折算成 `—`。
      脚本放 `scripts/dev/`，不进 CI。

验证命令：

```powershell
.\scripts\dev\check-backend-tests.ps1
```

## Task 5：明细与搜索端点

**修改：**

- `backend-cpp/src/db/ComponentInventoryRepository.cpp`
- `backend-cpp/src/http/ComponentInventoryRoutes.cpp`
- `backend-cpp/tests/test_component_inventory_repository.cpp`
- `backend-cpp/tests/test_component_inventory_routes.cpp`

步骤：

- [ ] `load_group_entries()` 与 `search_entries()` 共用同一个 `position` 窗口定义
      （`partition by site_component_type order by sort_order, id`，
      **不按 `is_active` 过滤**）。落法三选一：产出该 CTE 文本的函数、数据库 view、
      或 repository 私有查询构造器。三处各写一遍必然漂移。
- [ ] 三处（②、③、blocker 样本）各加断言 `position` 一致的测试。
- [ ] `is_referenced` 的逐行相关子查询移入 ②，只在实际翻到的一页上执行。
- [ ] `entries[].mappings` **只返回 `is_active = true` 的映射**，并按
      `created_at, id` 稳定排序。
- [ ] `total` 不能只靠 `count(*) over ()`：越界页返回零行，窗口函数便没有行可携带
      总数——用独立 count 或 CTE 左连接。
- [ ] 搜索零命中时显式规定 `rows.empty() ⇒ total = 0`。
- [ ] 搜索用 `like '%' || 转义(输入) || '%'`，转义 `%`、`_`、`\`。
- [ ] 路由参数测试逐项覆盖：`size=0`、`limit=0`、负数、`page=abc`、`size=1.5`、
      超 `int32` 的 `page`、`size=999` 截为 200、`limit=999` 截为 100、
      空与纯空白的 `group` / `number`、二者同时提供或同时缺失。
      `page * size` 用 64 位并检查溢出。

验证命令：

```powershell
.\scripts\dev\check-backend-tests.ps1
```

## Task 6：写端点响应改形状

**修改：**

- `backend-cpp/src/db/ComponentInventoryRepository.cpp`（所有写方法）
- `backend-cpp/src/http/ComponentInventoryRoutes.cpp`
- `backend-cpp/tests/test_component_inventory_repository.cpp`

步骤：

- [ ] 所有写方法从"执行写入 → `finish()` 提交 → 事务外 `get_revision()` 重读 → 返回"
      改为"执行写入 → 同事务算 summary → 提交 → 返回"。这不是改
      `respond_inventory_outcome()` 就能做到的。
- [ ] `load_summary()` 接受 executor 参数：只读路径传连接，写路径传当前事务，
      复用同一段 SQL 且不产生嵌套事务。
- [ ] summary 在提交**前**计算，但必须等提交确认后才返回；提交失败不得返回已算好的
      成功响应；confirm 被 blocker 拒绝时返回事务内算出的结果；派生草稿时 summary
      必须基于**派生后**的 revision id。
- [ ] `respond_inventory_outcome()` 输出
      `{ revision, groups, blockers, entry? }`。`entry` 仅在单条构件被改动时出现
      （新增 / 修改 / 停用 / 设映射），删除、批量确认、confirm、生成不带。
- [ ] 并发测试：汇总查询进行期间并发更新映射，`groups` 与 `blockers` 必须同快照；
      写事务算 summary 期间另一写请求到达时等待或冲突；提交失败不返回成功 summary。

验证命令：

```powershell
.\scripts\dev\check-backend-tests.ps1
```

## Task 7：前端状态模型与组件拆分

**修改：**

- `frontend/src/api/componentInventoryApi.ts`
- `frontend/src/api/resourceCache.ts`
- `frontend/src/bridges/ComponentInventoryEditor.tsx`
- `frontend/src/bridges/InventoryGroupTable.tsx`（新增）
- `frontend/src/bridges/InventoryGroupDialog.tsx`（新增）
- `frontend/src/bridges/InventoryEntrySearch.tsx`（新增）
- `frontend/src/bridges/InventoryBlockersPanel.tsx`（新增）
- `frontend/src/bridges/InventoryEntryRow.tsx`（新增）
- `frontend/src/bridges/ComponentInventoryEditor.test.tsx`

步骤：

- [ ] 状态改为 `summary` / `groupEntries` / `searchResults`；`drafts` 只覆盖已加载的
      构件。作废 `entriesById`、`searchMatches`、`expandedGroupEntries`、
      `pageEntries`、`pageCount`、`blockers`、`groupSummaries`。
- [ ] `groupSections` 改读 `summary.groups`；`pendingMappingCount` 改读
      `summary.groups` 的 `pending_count` 之和（**不是 blockers**）；
      `individualBlockers` 改读 `summary.blockers.samples`，
      删除原先"过滤掉有待确认映射的构件"的客户端逻辑（服务端已拆好）。
- [ ] `InventoryEntryRow` 必须抽出——现有 `renderEntryRow` 被搜索结果与分组弹窗
      共用，拆组件后不能各留一份。
- [ ] 搜索防抖 250 ms，沿用评定树页写法；搜索与分组弹窗各自独立 loading 态。
- [ ] 写成功后的刷新协议，**顺序不能颠倒**：替换 summary → 更新 revision id →
      丢弃旧 revision 的 drafts 并取消在途请求 → 重取当前分组页 → 重执行搜索 →
      按新 `total` 夹取页码 → 该组 `total` 归零则关弹窗。
- [ ] 乱序保护：`AbortController` 或请求序号或响应落地前比对
      `(revisionId, group/number, page, size)`。revision id 变化时旧响应一律丢弃。
- [ ] blocker 定位：先刷新汇总，再
      `samples.find(s => s.entity_type === 'inventory_entry' && s.position != null)`；
      **挑不到就只展示错误，不算页码不跳转**。按 `position` 取页后找不到
      `entity_id` 则重拉一次汇总再试一次，仍不存在则提示问题已变化，不无限重试。
- [ ] `resourceCache` 的 `inventory:{bridgeId}` 改存 summary；分组页与搜索结果
      **不进缓存**。
- [ ] 空修订版渲染编辑器（可手工新增），**不是向导**——向导只在
      `component_inventory_not_found` 时出现。不能用 `groups.length === 0` 判断空台账，
      "有构件但全部停用"时 `groups` 非空。
- [ ] 组件测试：进页面只发一次汇总请求；点"查看构件"才发明细请求；写后汇总被替换
      且不再触发全量拉取；删除后重取当前分组页；改类别后原分组重取；批量确认后
      分组与搜索都重取；页码越界夹取并二次请求；分组 `total` 归零关弹窗；
      旧搜索响应不覆盖新结果。

验证命令：

```powershell
npm run test -- --run src/bridges/ComponentInventoryEditor.test.tsx
npm run test -- --run
npm run build
```

## Task 8：清理

**修改：**

- `backend-cpp/src/http/ComponentInventoryRoutes.cpp`
- `frontend/src/api/componentInventoryApi.ts`
- `frontend/src/bridges/ComponentInventoryEditor.tsx`
- `frontend/src/bridges/ComponentInventoryEditor.test.tsx`

步骤：

- [ ] 删除 `GET /api/component-inventories/{revision_id}`（全仓无调用者）。
      属 **breaking change**，在发布说明标注；本系统前后端同仓库同发布，
      不做弃用观察期。
- [ ] 删除前端 `fetchComponentInventory()` 及相关类型。
- [ ] 后端口径测试通过后，删除 `inventoryGroupSummaries` 与
      `inventoryConfirmationBlockers`；其 106 / 109 / 117 / 264 四处用例连同
      fixture 已在 Task 4 迁至后端。
- [ ] **确认保留**：`GET .../latest`、`load_revision()`、
      `inventory_revision_json()`——校对工作台
      （`ComponentBindingWorkspace.tsx:253`、`:426`、`DefectsSection.tsx`）仍在用。
      删掉会直接打断病害绑定流程。

## Task 9：验收

步骤：

- [ ] 全量前端测试与生产构建。
- [ ] 全量后端隔离数据库测试。
- [ ] 跑一次性比对脚本，18 组逐字段一致（按 Task 4 定义的投影）。
- [ ] **用 Release 构建复测性能**——Debug 关优化且开 `_ITERATOR_DEBUG_LEVEL=2`，
      数千条构件的 JSON 序列化约慢 4 倍，Debug 下的数字无参考价值。
- [ ] 核对 `git status` 未误伤工作区里那约 55 个无关改动。

验证命令：

```powershell
npm run test -- --run
npm run build
.\scripts\dev\check-backend-tests.ps1
pwsh -ExecutionPolicy Bypass -File scripts/dev/start-cpp-backend.ps1 -Configuration Release
```

## 验收

响应体按 UTF-8 原始 JSON 字节数计，不含 HTTP 头，不启用压缩：

| 指标 | 现在 | 门槛 |
| --- | --- | --- |
| 首屏响应体（`samples` 取满 30 条） | 3.4 MB | ≤ 12 KB |
| 写响应，不带 `entry` | 3.4 MB | ≤ 12 KB |
| 写响应，带一条 `entry` | 3.4 MB | ≤ 20 KB |
| 汇总合并语句（18 组，5174 条） | ——（原为 42 ms 取全量） | ≤ 150 ms |
| 打开一组（100 条，仅生效映射） | 0（本地过滤） | ≤ 100 KB |

行为验收：

- 分组核对表的 18 行数字与改造前完全一致（两处有意变更除外）。
- 停用一个构件后，编号范围与数量口径一致。
- 删光构件后页面仍是编辑器，可手工新增，不出现"点生成得 409"的死路。
- 已确认台账写入仍能自动派生草稿，且一桥始终至多一条草稿。
- "确认前还需处理 N 项"不再出现"N 个待确认"与"其余 N 项"重复计数。
- 空台账时 blocker 面板能列出具体内容，而非只显示"还需处理 1 项"。

## 实施顺序摘要

Task 1 与 Task 2 各自可独立发布，修的是既有缺陷。Task 3 起才进入聚合改造，
Task 4 的口径单测必须早于比对脚本——真实数据全绿并不能说明多映射写对了
（19 项待验情形里有 14 项在现网数据上根本不出现）。
