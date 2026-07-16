# 管理员新增与批量永久删除桥梁档案实施计划

> 实施状态：待实施
>
> 日期：2026-07-16
>
> 设计真源：`docs/superpowers/specs/2026-07-16-bridge-administration-design.md`
>
> 实施分支：`codex/06-5-interaction-redesign`
>
> 基线提交：`975d194`

## 1. 目标与边界

为模块 06.5 的桥梁档案页增加管理员专属的两项维护能力：使用精简表单新增桥梁；通过复选框选择一座或多座桥梁，预览实时影响后逐座永久删除整桥业务档案。批量删除采用单桥独立事务，允许部分成功；活动编辑锁和陈旧影响令牌只阻止对应桥梁。

本计划同时把现有年度删除文件队列升级为可并发领取、可恢复、带退避且每 5 分钟持续调度的统一清理机制。数据库删除提交后才处理物理文件；文件待清理不改变桥梁业务删除已经完成的事实。

本计划不实现回收站、恢复、删除审批、删除审计页面、桥梁完整信息编辑页或普通用户删除申请。

## 2. 全局实施规则

1. 严格按任务顺序推进，每个任务先写失败测试，再写最小实现，再运行该任务的定向测试。
2. 不提交或删除 `.claude/`、`test-inputs/`、`test-output/`、`backend-cpp/archive/`、`tools-python/archive/`。
3. 后端是权限最终边界；普通用户即使手工调用接口也必须得到 403。
4. API、日志和前端错误不得暴露归档文件 ID、相对路径、绝对路径、SQL 或服务器异常正文。
5. 单桥事务提交前不得删除物理文件；事务失败时不得产生成功审计或可执行的文件清理项。
6. 既有 `GET /api/bridges` 继续由 `ReviewRoutes` 提供；同路径新增 POST/DELETE 时只保留一个 OPTIONS 注册，避免重复路由。
7. 新增表和约束必须进入数据库 smoke test；新 C++ 文件必须显式加入 `backend-cpp/CMakeLists.txt`。
8. 每次提交前执行 `git diff --check` 和保护目录审计，只暂存当前任务明确列出的文件。

## 3. 实施任务

### Task 1：迁移整桥删除审计并升级两类文件清理队列

**新增文件**：

- `database/migrations/007_bridge_administration.sql`
- `database/tests/007_bridge_administration_smoke.sql`

**修改文件**：

- `scripts/dev/check-module06-db.ps1`

**先写测试**：

1. 新迁移可在已有 001—006 数据库上执行并可重复运行；
2. `bridge_deletion_audits.bridge_id` 是可空外键且 `ON DELETE SET NULL`；
3. 桥梁删除审计保存 `batch_id`、桥梁和操作者快照、原因、影响 JSON、删除数量 JSON、文件状态和时间；
4. 新增 `bridge_archived_file_deletion_queue` 并通过外键关联桥梁删除审计；
5. 新队列和既有 `archived_file_deletion_queue` 都具有 `待清理 / 清理中 / 失败待重试 / 已完成` 四态、`attempt_count`、`last_error`、`next_attempt_at`、`processing_started_at`、`completed_at`；
6. “已完成”必须有完成时间，“清理中”必须有领取时间，其他状态清空领取时间；
7. 待领取部分索引覆盖 `status + next_attempt_at`；陈旧领取恢复查询有相应索引；
8. 006 遗留的待清理、失败和已完成记录迁移后语义不变。

**实现要点**：

- 新建 `bridge_deletion_audit_system_number_seq`，审计编号使用独立稳定前缀；
- `batch_id` 使用 UUID，同批成功桥梁共享；失败桥不写成功审计；
- 审计只保存公共快照和数量，不保存病害正文、评分明细、Word、照片、草稿或原始 JSON；
- 修改既有队列 check constraint 时先删除旧约束再按新四态重建，保持迁移幂等；
- 为年度审计的文件状态补齐“待清理 / 已完成 / 部分失败”兼容语义，不把历史已完成项重新入队；
- 两张队列表仍只保存受数据库约束的安全相对路径，路径不通过任何公共 API 返回。

**验证命令**：扩展并运行 `scripts/dev/check-module06-db.ps1`，依次应用 002—007 迁移并执行 002、003、006、007 smoke test；再对已经应用 006 且含未完成队列记录的测试库单独执行 007，验证升级路径。

**建议提交**：`feat(database): add bridge deletion audit and cleanup claims`

### Task 2：抽取安全文件删除核心并实现统一持续清理协调器

**新增文件**：

- `backend-cpp/include/bridge_report/deletion/ArchiveFileDeletionCore.hpp`
- `backend-cpp/src/deletion/ArchiveFileDeletionCore.cpp`
- `backend-cpp/include/bridge_report/deletion/ArchiveFileCleanupCoordinator.hpp`
- `backend-cpp/src/deletion/ArchiveFileCleanupCoordinator.cpp`
- `backend-cpp/tests/test_archive_file_deletion_core.cpp`
- `backend-cpp/tests/test_archive_file_cleanup_coordinator.cpp`

**修改文件**：

- `backend-cpp/include/bridge_report/deletion/ArchivedFileDeletionQueue.hpp`
- `backend-cpp/src/deletion/ArchivedFileDeletionQueue.cpp`
- `backend-cpp/tests/test_archived_file_deletion_queue.cpp`
- `backend-cpp/CMakeLists.txt`

**先写测试**：

1. 安全相对路径可删除，文件不存在按幂等成功处理，越界路径被拒绝；
2. 原子领取使用 `FOR UPDATE SKIP LOCKED`，两个协调器不能领取同一项目；
3. 领取后状态变为“清理中”并写 `processing_started_at`；
4. 成功转“已完成”，失败转“失败待重试”，保存截断后的错误、次数和 `next_attempt_at`；
5. 退避增长但不超过配置上限；未到 `next_attempt_at` 的失败项不领取；
6. 超过恢复时限的“清理中”项目可重新领取；未超时的项目保持占用；
7. 年度队列和桥梁队列都能通过同一协调器处理并更新各自审计；
8. 一个队列项目失败不阻断同批其他项目；批次领取数量受上限控制。

**实现要点**：

- `ArchiveFileDeletionCore` 只负责 `resolve_path_under_root` 和单文件幂等删除，不访问业务表；
- `ArchiveFileCleanupCoordinator` 分别领取年度队列和桥梁队列，但复用相同的领取、删除、状态迁移和退避策略；
- 保留 `ArchivedFileDeletionQueue` 作为短期兼容适配器，内部委托协调器，待年度删除路由完成切换后再删除无调用的旧实现；不得并存两套物理删除算法；
- 审计状态按所属队列剩余数量更新：全部完成才写完成时间，否则保持待清理或部分失败；
- 数据库断开、文件权限错误等异常在单项边界截获，不允许定时回调异常逃逸。

**定向验证**：构建并运行 `ArchiveFileDeletionCoreTest.*`、`ArchiveFileCleanupCoordinatorTest.*`、`ArchivedFileDeletionQueueTest.*`。

**建议提交**：`refactor(archive): unify durable file cleanup`

### Task 3：增加清理配置并接入启动、立即与五分钟调度

**修改文件**：

- `backend-cpp/include/bridge_report/config/AppConfig.hpp`
- `backend-cpp/src/config/AppConfig.cpp`
- `backend-cpp/tests/test_app_config.cpp`
- `config/local.example.json`
- `backend-cpp/src/main.cpp`
- `backend-cpp/src/http/InspectionYearDeletionRoutes.cpp`
- `backend-cpp/tests/test_inspection_year_deletion_routes.cpp`

**先写测试**：

1. 默认调度周期为 300 秒、每类队列每批 25 项、领取恢复时限 900 秒、基础退避 300 秒、最大退避 86400 秒；
2. JSON 配置可覆盖这些值，非法或非正数配置回退默认值；
3. 年度删除成功后仍立即尝试所属审计文件；清理失败时 DELETE 仍报告数据库删除成功和文件待清理；
4. 协调器启动处理和定时处理调用同一有界入口。

**实现要点**：

- 配置放在 `archive` 节点：`cleanup_interval_seconds`、`cleanup_batch_size`、`cleanup_claim_timeout_seconds`、`cleanup_retry_base_seconds`、`cleanup_retry_max_seconds`；
- `main.cpp` 持有共享协调器：后端启动时执行一次有界恢复，随后用 Drogon event loop 每 300 秒调度；
- 定时器捕获对象必须覆盖应用生命周期，不得捕获已经销毁的局部引用；
- 一个调度尚未完成时跳过下一次触发，禁止同进程重入；跨进程并发仍由数据库领取锁保证；
- 更新年度删除路由，提交后调用协调器的年度审计立即处理方法；响应继续区分业务删除与文件清理；
- 启动或定时清理失败只记录中性日志，不阻断后端启动和 HTTP 服务。

**定向验证**：`AppConfigTest.*`、`InspectionYearDeletionRoutesTest.*` 和全部清理器测试。

**建议提交**：`feat(archive): schedule persistent cleanup retries`

### Task 4：建立整桥影响计划、规范确认文字与令牌

**新增文件**：

- `backend-cpp/include/bridge_report/deletion/BridgeDeletionModels.hpp`
- `backend-cpp/src/deletion/BridgeDeletionModels.cpp`
- `backend-cpp/tests/test_bridge_deletion_models.cpp`

**修改文件**：

- `backend-cpp/CMakeLists.txt`

**先写测试**：

1. 同一影响集合以不同数据库返回顺序输入时产生相同规范 JSON 和 SHA-256；
2. 桥梁 `updated_at`、受影响记录集合增删、活动锁状态或文件分类变化都会改变令牌；只修改不影响删除范围的展示正文不强制改变令牌；
3. 单桥确认文字为 `永久删除 QL-001287`；多桥按系统编号升序生成并使用中文顿号；
4. 输入桥梁 ID 去重且最多 100 座；重复 ID、空集合和超限返回稳定校验错误；
5. 公共 JSON 只包含桥梁快照、计数、锁摘要、独占/共享文件数量和令牌，不包含文件 ID 或路径；
6. 批量合计等于逐桥计数求和，不因排序或锁定状态改变。

**实现要点**：

- 模型至少包含桥梁、年度及版本、导入、构件、别名、观测、尺寸、照片、评分、线索、对比、候选文件分类和活动锁的稳定 ID 集合；
- 明确分开“公共预览 JSON”和“内部规范令牌 JSON”；内部可含稳定 ID，公共响应不能含文件标识；
- 所有 ID 数组排序，数值字段和空值采用固定表示；不把数据库非确定顺序直接序列化；
- 批次确认文字只由后端根据已预览桥梁系统编号生成。

**定向验证**：`BridgeDeletionModelsTest.*`。

**建议提交**：`feat(deletion): model whole bridge deletion impact`

### Task 5：实现并发安全的管理员新增桥梁仓储

**新增文件**：

- `backend-cpp/include/bridge_report/db/BridgeAdministrationRepository.hpp`
- `backend-cpp/src/db/BridgeAdministrationRepository.cpp`
- `backend-cpp/tests/test_bridge_administration_repository.cpp`

**修改文件**：

- `backend-cpp/CMakeLists.txt`

**先写测试**：

1. 必填桥名和默认“在用”创建成功，系统编号由数据库生成；
2. 选填空白统一写 `NULL`，所有文本去首尾空白；
3. 状态只允许“在用 / 停用 / 拆除”；
4. 比较忽略首尾空白和英文字母大小写，`NULL` 与空字符串等价；
5. 同桥名、路线编号、桩号返回已有桥摘要；同名但路线或桩号不同允许创建；
6. 两个并发相同请求只能插入一条，另一个稳定返回重复结果。

**实现要点**：

- 仓储只负责创建和重复身份，不承载删除逻辑；
- 在事务内用规范身份键计算 PostgreSQL transaction advisory lock，再查询并插入；
- 锁键使用稳定数据库算法，不使用进程相关的 `std::hash`；
- 重复响应只返回进入已有桥梁所需的 ID、系统编号和桥名。

**定向验证**：`BridgeAdministrationRepositoryTest.*`，包含真实 PostgreSQL 并发测试。

**建议提交**：`feat(bridges): create bridges safely`

### Task 6：实现整桥影响预览查询和共享文件分类

**新增文件**：

- `backend-cpp/include/bridge_report/db/BridgeDeletionRepository.hpp`
- `backend-cpp/src/db/BridgeDeletionRepository.cpp`
- `backend-cpp/tests/test_bridge_deletion_repository.cpp`

**修改文件**：

- `backend-cpp/CMakeLists.txt`

**先写测试**：

1. 单桥和多桥预览按系统编号排序，逐桥与总计准确；
2. 同一年 V1/V2 都计入年度版本，年度数按自然年度去重；
3. 导入、草稿 JSON、编辑锁及锁事件、构件、两类别名、线索、观测、尺寸、照片、评分和对比全部纳入；
4. 活动锁返回编辑人、年度和到期时间；过期锁不阻断；
5. 归档文件引用扫描覆盖 `bridge_aliases.source_file_id`、`import_records.main_file_id`、`import_record_files.archived_file_id`、`component_aliases.source_file_id`、`defect_observations.source_file_id`、`defect_photos.archived_file_id/source_file_id`、`condition_ratings.source_file_id`；
6. 仍被桥外数据引用的文件分类为共享保留，只有全部引用均在目标桥内的文件分类为独占删除；
7. 任一选择已不存在时整批预览返回 `bridge_selection_changed`，不返回部分预览；
8. 公共响应无文件 ID 和路径。

**实现要点**：

- 预览阶段只读，不取得编辑锁或桥梁行锁；
- 数据库查询结果组装为 Task 4 的规范模型，文件分类规则集中在仓储私有查询或独立纯函数，不散落在路由；
- 以前的 `inspection_year_deletion_audits` 只计入“保留审计”说明，不进入删除数量；
- 对当前已知的全部桥级外键做显式覆盖，集成测试用外键阻止遗漏静默通过。

**定向验证**：`BridgeDeletionRepositoryTest.Preview*`。

**建议提交**：`feat(deletion): preview whole bridge impact`

### Task 7：实现单桥原子永久删除事务

**修改文件**：

- `backend-cpp/include/bridge_report/db/BridgeDeletionRepository.hpp`
- `backend-cpp/src/db/BridgeDeletionRepository.cpp`
- `backend-cpp/tests/test_bridge_deletion_repository.cpp`

**先写测试**：

1. 单桥全部年度版本、导入记录、锁及事件、构件、别名、线索、观测、尺寸、照片、评分和对比完整删除；
2. 独占文件元数据删除并入桥梁清理队列，共享文件及桥外关系保留；
3. 既有年度删除审计保留，桥梁外键按 `ON DELETE SET NULL` 置空且桥梁快照不变；
4. 活动锁返回 `locked`，不写审计、不入队且目标桥零变化；
5. 预览后新增或修改任一受影响数据返回 `impact_changed`；
6. 桥梁已经不存在返回 `not_found`；
7. 注入任一 SQL 故障时整座桥完整回滚；
8. 成功时写一条桥梁删除审计，`bridge_id` 在删除主表后置空，快照、原因、操作者、批次和数量正确；
9. 同批两座成功、一座锁定时成功桥提交、锁定桥保留，两个成功审计共享 `batch_id`；
10. 一个桥数据库失败不阻断后续桥处理，失败结果不含 SQL 文本。

**实现要点**：

- 删除批次循环在服务层逐座调用；`BridgeDeletionRepository::delete_one` 每次创建独立事务；
- 事务顺序固定为：锁桥梁 → 锁目标年度、导入、事实和候选文件 → 重建影响计划 → 比令牌 → 复核活动锁 → 写审计/队列 → 显式删除依赖 → 删除独占文件元数据 → 删除桥梁主记录；
- 先删除同时引用“当前/对比年度”的 `defect_comparisons`，再处理年度和观测，避免 restrict 外键；
- 修订来源引用、导入文件 restrict 引用、线索到构件 restrict 引用必须在删除主记录前显式解除或删除；
- 让未知的新桥级 restrict 外键导致事务失败，不能使用绕过约束的级联脚本；
- 返回 `deleted / locked / impact_changed / not_found / failed`，文件清理不是单桥业务状态。

**定向验证**：`BridgeDeletionRepositoryTest.Delete*` 和部分成功批量场景。

**建议提交**：`feat(deletion): atomically delete whole bridges`

### Task 8：暴露管理员桥梁新增、预览和批量删除 API

**新增文件**：

- `backend-cpp/include/bridge_report/http/BridgeAdministrationRoutes.hpp`
- `backend-cpp/src/http/BridgeAdministrationRoutes.cpp`
- `backend-cpp/tests/test_bridge_administration_routes.cpp`

**修改文件**：

- `backend-cpp/src/http/ReviewRoutes.cpp`
- `backend-cpp/src/main.cpp`
- `backend-cpp/CMakeLists.txt`

**接口**：

```text
POST   /api/bridges
POST   /api/bridges/deletion-impact
DELETE /api/bridges
```

**先写测试**：

1. 三个接口未登录 401、普通用户 403；
2. POST 创建字段、长度、状态、空白和 JSON 类型错误返回稳定 400；桥名上限 200 字符，路线编号和桩号各 100 字符，路线名称和行政区划各 200 字符；重复返回 409 及已有桥摘要；成功返回 201；
3. 预览对空集合、非法 UUID、重复 ID 和超过 100 座返回 400；桥梁消失返回 409 `bridge_selection_changed`；
4. DELETE 严格验证非空且不超过 4000 字符的原因、精确确认文字、`items[]` 和逐桥令牌，批次级失败时一座也不处理；
5. 进入处理后始终逐桥返回 `deleted / locked / impact_changed / not_found / failed`；
6. `deleted` 项单独返回 `file_cleanup_status: completed|pending` 和待清理数量；
7. 删除提交后立即调用协调器；清理异常仍返回 `deleted + pending`；
8. 响应不泄漏文件标识、路径、SQL 和服务端异常；
9. GET `/api/bridges`、CORS OPTIONS 和已有年度删除路由不回归。

**实现要点**：

- `BridgeAdministrationRoutes` 只做鉴权、解析、批次级校验、调用仓储和响应脱敏；
- POST/DELETE 与既有 GET 共用 `/api/bridges`，把该路径 OPTIONS 的唯一注册位置写清并用路由测试锁定；
- 后端对 items 去重后按最新桥梁系统编号重排并重算确认文字，不信任前端顺序；
- 批次开始前生成一个 `batch_id`，每座桥调用 Task 7 的独立事务；
- 每个成功结果在事务提交后尝试对应桥梁审计的立即文件清理。

**定向验证**：`BridgeAdministrationRoutesTest.*`、`InspectionYearDeletionRoutesTest.*`；随后运行后端全量测试，覆盖既有 `GET /api/bridges` 和 OPTIONS 注册回归。

**建议提交**：`feat(api): expose bridge administration`

### Task 9：增加独立前端桥梁管理 API 合同

**新增文件**：

- `frontend/src/api/bridgeAdministrationApi.ts`
- `frontend/src/api/bridgeAdministrationApi.test.ts`

**先写测试**：

1. 创建请求使用 POST、正确 JSON，201 响应映射为桥梁摘要；
2. 影响预览使用 POST，桥梁 ID 原样传输，响应逐桥/合计/锁摘要类型完整；
3. 删除使用 DELETE，携带统一原因、后端确认文字和逐桥令牌；
4. `bridge_already_exists`、`bridge_selection_changed`、`locked`、`impact_changed` 和未知错误映射为可操作中文信息；
5. 类型中不出现文件 ID、文件路径字段；
6. `deleted + pending` 与未删除状态可被 UI 无歧义区分。

**实现要点**：

- 不把管理员写操作继续塞入只读 `navigationApi.ts` 或年度 `workspaceApi.ts`；
- 复用通用 `request` 和 `ApiError`，不复制认证与 JSON 错误处理；
- API 类型名称区分批次级错误、逐桥业务结果和文件清理子状态。

**定向验证**：`npm run test -- --run src/api/bridgeAdministrationApi.test.ts`。

**建议提交**：`feat(frontend): add bridge administration contracts`

### Task 10：实现新增桥梁弹窗

**新增文件**：

- `frontend/src/bridges/CreateBridgeDialog.tsx`
- `frontend/src/bridges/CreateBridgeDialog.test.tsx`

**修改文件**：

- `frontend/src/styles.css`

**先写测试**：

1. 桥名必填，状态默认“在用”，选填字段可以留空；
2. 提交中禁止重复提交和关闭；
3. 后端字段错误显示在弹窗内且保留输入；
4. 重复桥提示已有编号、桥名并提供“进入已有桥梁”；
5. 创建成功调用完成回调，由页面进入新桥概览；
6. 键盘焦点、label、dialog 标题和按钮具有可访问名称。

**实现要点**：

- 字段严格限定为桥名、路线编号、路线名称、行政区划、桩号和状态；
- 不在前端生成系统编号或自行判断最终重复；
- 关闭只在空闲态生效，成功回调返回新桥 ID。

**定向验证**：`npm run test -- --run src/bridges/CreateBridgeDialog.test.tsx`。

**建议提交**：`feat(ui): add bridge creation dialog`

### Task 11：实现批量永久删除弹窗

**新增文件**：

- `frontend/src/bridges/DeleteBridgesDialog.tsx`
- `frontend/src/bridges/DeleteBridgesDialog.test.tsx`

**修改文件**：

- `frontend/src/styles.css`

**先写测试**：

1. 打开后先加载实时预览，加载完成前不显示可提交状态；
2. 逐桥计数、总计、共享保留文件和活动编辑锁正确显示；
3. 原因非空且确认文字逐字一致时才可提交；
4. 锁定桥标记“本次不能删除”，但不阻止同批其他桥提交；
5. 提交中防关闭、防重复；
6. 结果分“已删除 / 未删除”，成功桥文件 pending 使用“业务档案已完整删除；N 个孤立归档文件等待后台清理”；
7. `locked / impact_changed / not_found / failed` 显示不同说明；
8. `bridge_selection_changed` 触发清空选择和列表刷新，不展示不完整预览；
9. 关闭结果后通知页面刷新并清空本批选择。

**实现要点**：

- 弹窗保存本次预览返回的确认文字和逐桥令牌，提交时不重新拼接；
- 前端不把锁定桥从 items 中偷偷移除，后端逐桥报告其未删除；
- 成功结果不得使用“删除了一半”等模糊文案；文件 pending 只是成功桥的清理子状态。

**定向验证**：`npm run test -- --run src/bridges/DeleteBridgesDialog.test.tsx`。

**建议提交**：`feat(ui): add batch bridge deletion dialog`

### Task 12：集成桥梁列表管理员操作与批量选择

**修改文件**：

- `frontend/src/pages/BridgesPage.tsx`
- `frontend/src/pages/BridgesPage.test.tsx`
- `frontend/src/styles.css`

**先写测试**：

1. 普通用户看不到新增、删除按钮和复选框；管理员看到相邻按钮；
2. 未选中时删除按钮禁用，选中后数量正确；
3. 行复选框点击不导航，点击行其他区域仍进入概览；
4. 表头复选框只选择当前搜索结果，再次点击取消当前可见项；
5. 搜索文本变化立即清空全部选择；
6. 列表刷新移除已经不存在的 ID；
7. 新增成功进入新桥概览；
8. 批量删除部分成功后重新加载列表，成功桥消失、失败桥保留；
9. 无数据、无搜索结果、加载和错误状态仍正常。

**实现要点**：

- 使用 `useAuth()` 判断 `user.role === "admin"`，后端仍独立鉴权；
- 顶部相邻渲染“＋ 添加桥梁”和“删除选中桥梁（N）”；
- 选择状态使用桥梁 ID 集合；搜索 `onChange` 同步清空，避免不可见选中项；
- 复选框事件 `stopPropagation`，表头选中状态正确表达全选/部分选中；
- 抽取可复用的 `reloadBridges`，统一供初次加载、创建后和删除后调用。

**定向验证**：`npm run test -- --run src/pages/BridgesPage.test.tsx`，随后运行前端全量测试与构建。

**建议提交**：`feat(ui): manage bridges from archive list`

### Task 13：端到端回归、文档和完成审计

**修改文件**：

- `README.md`
- `PROJECT_CONTEXT.md`
- `docs/superpowers/specs/2026-07-16-bridge-administration-design.md`
- `docs/superpowers/plans/2026-07-16-bridge-administration-implementation-plan.md`

**自动验证**：

```powershell
# 数据库：从空库执行全部迁移和 smoke tests，并验证 006 -> 007 升级路径

Set-Location 'D:\vs2022 code\bridge-report-system\backend-cpp'
cmake --build --preset vs2022-x64-debug
$env:BRIDGE_REPORT_TEST_DATABASE_URL='postgresql://bridge_report:bridge_report_dev@127.0.0.1:5432/bridge_report_system'
.\build\vs-debug\Debug\bridge_report_backend_tests.exe --gtest_brief=1

Set-Location 'D:\vs2022 code\bridge-report-system\frontend'
npm run test -- --run
npm run build

Set-Location 'D:\vs2022 code\bridge-report-system\tools-python'
uv run pytest -q
$env:BRIDGE_REPORT_REAL_WORD_PATH='D:\vs2022 code\bridge-report-system\test-inputs\word-import\绕阳河二号桥报告b8e246e8-cd4d-4202-8d4d-49c56dd34389.docx'
uv run pytest -q tests/importers/test_real_word_regression.py

Set-Location 'D:\vs2022 code\bridge-report-system'
git diff --check
git status --short
```

**手工验收**：

1. 普通用户登录：无新增/删除入口，手工请求三个管理员接口均为 403；
2. 管理员新增空年度桥梁并自动进入桥梁概览；相同身份重复新增跳转已有桥；
3. 单选删除一座无年度测试桥，确认审计保留且列表刷新；
4. 批量选择三座桥，其中一座有张工活动锁：两座删除、一座保留，结果逐座准确；
5. 在预览后修改目标桥数据，旧令牌只阻止该桥；
6. 删除含 V1/V2、导入草稿、病害档案、评分和照片的桥，数据库业务记录无残留；
7. 构造跨桥共享归档文件，删除目标桥后共享文件仍可被另一桥读取；
8. 占用一个独占文件使立即清理失败：UI 显示业务已删除和文件 pending；解除占用后不重启后端，等待下一次调度完成；
9. 模拟进程在“清理中”退出，重启后超过恢复时限的项目继续处理；
10. 既有年度删除功能仍能删除年度，并由同一五分钟协调器重试失败文件。

**完成记录**：

- 把设计和计划状态改为已完成，记录数据库、C++、前端、Python、构建和真实 Word 回归的实际数量；
- 核对 `git status --short`，确保保护目录仍未提交；
- 只提交任务相关文档和代码，不自动推送，除非用户明确要求。

**建议提交**：`docs: complete bridge administration`

## 4. 依赖顺序

```text
Task 1 ─ Task 2 ─ Task 3
   │
   └─ Task 4 ─ Task 6 ─ Task 7 ─ Task 8
             \
Task 5 ───────┘

Task 8 ─ Task 9 ─ Task 10 ─ Task 11 ─ Task 12 ─ Task 13
```

Task 5 可在 Task 4—6 期间独立完成，但合入前必须保证共享工作区没有并行修改同一 CMake 文件。实际由单一执行者按编号顺序实施时，直接依次执行即可。

## 5. 完成标准

- 管理员可从桥梁档案页新增桥梁，重复身份在并发下也只创建一条；
- 管理员可通过复选框批量预览并永久删除整桥档案，普通用户无 UI 且 API 为 403；
- 批量删除逐座独立提交，锁定、影响变化、消失或失败只影响对应桥；
- 每座成功桥的全部业务数据按设计删除，永久审计保留且不可恢复业务正文；
- 共享文件保留、独占文件入持久队列，数据库提交前不碰磁盘；
- 文件清理支持立即、启动和每 5 分钟处理，并具备并发领取、退避和陈旧领取恢复；
- 前端明确区分“桥梁业务已删除”和“孤立文件等待清理”；
- 既有年度删除、桥梁概览、年度检测、校对、病害档案和真实 Word 导入全部回归通过；
- 所有测试、Debug 构建、前端生产构建及保护目录审计通过。
