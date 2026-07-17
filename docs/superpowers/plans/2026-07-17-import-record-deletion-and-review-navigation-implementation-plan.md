# 导入记录删除、无照片规则与校对定位实施计划

> 实施状态：待实施
>
> 日期：2026-07-17
>
> 设计真源：`docs/superpowers/specs/2026-07-16-import-record-deletion-and-review-navigation-design.md`
>
> 实施分支：`codex/06-5-interaction-redesign`
>
> 基线提交：`f99f186`

## 1. 目标与边界

本计划完成模块 06.5 的三个补充能力：管理员受控永久删除尚未形成正式事实的导入记录；把“病害没有照片编号”改为正常业务情况；为病害增加展示序号，并让“需要处理”中的病害、照片和评分问题能够精确跳转。

本计划不迁移已有导入 JSON，不改变 `BridgeAnnualInspectionData` 1.2，不允许单独删除已确认记录，不删除正式年度事实，不改变评分算法、编辑锁租约、草稿保存和确认入库规则。

## 2. 全局实施规则

1. 按任务顺序实施；每个任务先增加失败测试，再写最小实现，再运行定向测试。
2. 不提交或删除 `.claude/`、`test-inputs/`、`test-output/`、`backend-cpp/archive/`、`tools-python/archive/`，也不提交 `runtime/` 和本地截图。
3. 不为旧 `photo_number_missing` 编写迁移或运行时兼容层；测试数据需要新语义时重新解析。
4. 病害序号只是前端派生视图，禁止进入合同、保存草稿请求或正式事实表。
5. 后端是删除权限、状态、活动锁、正式事实引用和影响令牌的最终边界。
6. 删除数据库事务提交前不得删除磁盘文件；文件失败进入持久清理队列，不能恢复业务记录。
7. API、日志和前端错误不返回文件路径、归档文件 ID、SQL 或服务器异常正文。
8. 新迁移进入数据库 smoke test；新增 C++ 文件显式加入 `backend-cpp/CMakeLists.txt`。
9. 每个任务提交前执行 `git diff --check`，只暂存该任务明确列出的文件。

## 3. 实施任务

### Task 1：修正 Python 无照片病害语义

**修改文件**：

- `tools-python/bridge_report_tools/importers/defect_tables.py`
- `tools-python/tests/importers/test_defect_tables.py`
- `tools-python/tests/importers/test_word_importer.py`

**先写测试**：

1. 照片编号单元格为空时，病害仍被解析，`photo_numbers == []`；
2. 病害对象和导入级警告中均不存在 `photo_number_missing`；
3. 同一行的构件、位置、病害类型、标度和扣分解析不受影响；
4. 有照片编号但没有提取图片时，照片匹配阶段仍生成 `photo_number_unmatched`；
5. 多照片编号只对没有匹配的编号产生警告。

**实现要点**：删除 `defect_tables.py` 中空 `photo_numbers` 自动追加 `photo_number_missing` 的分支，不改变 `photo_extractor.py` 对实际引用未匹配图片的判断。更新仍断言旧警告的测试，不改合同模型。

**定向验证**：

```powershell
Set-Location 'D:\vs2022 code\bridge-report-system\tools-python'
uv run pytest -q tests/importers/test_defect_tables.py tests/importers/test_word_importer.py
```

**建议提交**：`fix(import): treat blank photo numbers as normal`

### Task 2：建立可定位的待处理项模型

**新增文件**：

- `frontend/src/review/reviewNavigation.ts`
- `frontend/src/review/reviewNavigation.test.ts`

**修改文件**：

- `frontend/src/review/grouping.ts`
- `frontend/src/review/grouping.test.ts`
- `frontend/src/review/components/displayHelpers.ts`
- `frontend/src/review/components/displayHelpers.test.ts`

**先写测试**：

1. 病害警告携带病害候选 ID、警告代码和可选字段目标；
2. 照片警告能区分已关联照片和未关联照片；
3. `ratings.overall`、结构分部、评价部件和 `component_rating_*` 都能解析为评分目标；
4. 已知代码映射到对应字段，例如标度、扣分、尺寸原文和照片编号；未知代码降级到整个卡片；
5. 病害显示名按当前 `defects` 顺序生成“病害 1…N”；
6. 已关联照片显示“病害 N · 照片 x”，未关联照片显示“未关联照片 · x”，评分显示业务名称；
7. 照片编号为空不会由前端派生出缺图待处理项，且不增加 `needs_attention_count`；
8. 没有具体候选的导入级问题保持不可定位。

**实现要点**：

- 扩展前端专属 `AttentionItem`，保存 `warningCode`、业务标签和结构化 `navigationTarget`；
- 结构化目标只存在前端视图层，不修改 `WarningItem` 合同；
- `needsAttention` 在生成派生问题时同时指定目标字段或目标区域；
- `reviewNavigation.ts` 负责序号查找、照片所属病害解析、评分标签和锚点 ID 规范化；
- 未识别警告代码只定位到候选卡片，不能靠中文消息字符串猜字段。

**定向验证**：

```powershell
Set-Location 'D:\vs2022 code\bridge-report-system\frontend'
npm run test -- --run src/review/grouping.test.ts src/review/reviewNavigation.test.ts src/review/components/displayHelpers.test.ts
```

**建议提交**：`feat(review): model precise attention targets`

### Task 3：增加病害序号和稳定页面锚点

**修改文件**：

- `frontend/src/review/components/DefectsSection.tsx`
- `frontend/src/review/components/DefectsSection.test.tsx`
- `frontend/src/review/components/DefectPhotoGroup.tsx`
- `frontend/src/review/components/DefectPhotoGroup.test.tsx`
- `frontend/src/review/components/UnlinkedPhotosPanel.tsx`
- `frontend/src/review/components/UnlinkedPhotosPanel.test.tsx`
- `frontend/src/review/components/RatingsSection.tsx`
- `frontend/src/review/components/RatingsSection.test.tsx`
- `frontend/src/styles.css`

**先写测试**：

1. 三条病害按数组顺序显示“病害 1”“病害 2”“病害 3”；
2. 序号不可编辑，编辑其他字段后顺序不变；
3. 病害卡片和病害字段具有由候选 ID 派生的稳定锚点；
4. 照片缩略图、未关联照片和评分项具有稳定锚点；
5. 组件只使用规范化锚点，不把任意来源文本直接写入 DOM ID；
6. 只读终态仍显示序号和锚点，不因此开放编辑能力。

**实现要点**：`DefectsSection` 在 `map` 时把 `index + 1` 传给 `DefectPhotoGroup`；病害卡片左上角增加只读序号标签。锚点和 `data-review-target` 由 Task 2 的纯函数统一生成。评分表的总评、结构分部、评价部件和构件评分均建立可定位容器。

**定向验证**：运行上述四个组件测试文件。

**建议提交**：`feat(review): number and anchor review candidates`

### Task 4：实现待处理列表的跨栏目跳转

**修改文件**：

- `frontend/src/review/components/NeedsAttentionSection.tsx`
- `frontend/src/review/components/NeedsAttentionSection.test.tsx`
- `frontend/src/pages/ReviewWorkspacePage.tsx`
- `frontend/src/styles.css`

**新增文件**：

- `frontend/src/pages/ReviewWorkspacePage.test.tsx`

**先写测试**：

1. 待处理表格显示业务标签而不是 `defect_0001` 等内部编号；
2. 点击病害字段警告后切换“病害与照片”、展开病害、滚动、高亮并聚焦字段；
3. 点击病害级警告时高亮整张病害卡片；
4. 点击已关联照片时展开所属病害并激活对应照片；
5. 点击未关联照片时滚动到未关联照片区域并选中照片；
6. 点击评分警告时切换“技术状况评定”并定位正确评分项；
7. 重复点击同一条问题会重新执行定位；
8. 目标已不存在时显示“目标数据已变化，请刷新待处理列表”，页面不崩溃；
9. 导入级无目标警告不显示虚假的定位行为；
10. `prefers-reduced-motion` 下不使用平滑动画，但保留静态高亮。

**实现要点**：

- `NeedsAttentionSection` 把完整 `AttentionItem` 交给页面，不再只传 `kind + candidateId`；
- 页面保存 `pendingNavigationTarget`，先切换栏目、展开病害或选择照片；
- 目标组件渲染后，由 effect 查找锚点并调用 `scrollIntoView`、`focus` 和短暂高亮；
- 新导航请求带递增序号，保证重复点击同一目标也会触发；
- 清理高亮定时器，组件卸载后不更新状态；跳转不派发草稿修改动作。

**定向验证**：运行 NeedsAttention、ReviewWorkspacePage 和 Task 2—3 的相关前端测试。

**建议提交**：`feat(review): navigate from warnings to candidates`

### Task 5：迁移导入删除审计和持久文件清理队列

**新增文件**：

- `database/migrations/009_import_record_deletion.sql`
- `database/tests/009_import_record_deletion_smoke.sql`

**修改文件**：

- `scripts/dev/check-module06-db.ps1`

**先写测试**：

1. 009 可在 001—008 基线上执行并可重复运行；
2. `import_record_deletion_audits` 保存原导入 UUID/编号/状态、桥梁和年度快照、操作者快照、原因、影响/删除数量、文件状态和时间；
3. 审计不通过外键依赖将被删除的导入记录；
4. `import_record_file_deletion_queue` 关联审计，支持“待清理 / 清理中 / 失败待重试 / 已完成”；
5. 队列包含受控存储类型、相对路径、文件或解析工作目录类型、次数、错误、下次重试、领取和完成时间；
6. 临时 Word 路径只能是 UUID `.docx`；解析工作目录只能位于 `work/word-import/`；归档路径继续通过后端受控根校验；
7. `import_source_files` 增加可空的活动解析工作相对路径，并带严格路径约束；
8. 待领取、陈旧领取恢复和按审计统计均有索引；
9. smoke 脚本明确先执行 008 再执行 009，修正当前模块数据库检查遗漏 008 的问题。

**实现要点**：审计保留必要快照而不保存完整解析 JSON。队列的 `storage_kind` 至少区分归档根和临时 Word 根，`artifact_kind` 区分文件和受限解析工作目录。所有 check constraint 采用可幂等重建方式。

**验证命令**：运行 `scripts/dev/check-module06-db.ps1`，并验证已有 008 数据库升级到 009 时未完成临时 Word 状态不变。

**建议提交**：`feat(database): add import deletion audit queue`

### Task 6：建立导入删除影响模型和令牌

**新增文件**：

- `backend-cpp/include/bridge_report/deletion/ImportRecordDeletionModels.hpp`
- `backend-cpp/src/deletion/ImportRecordDeletionModels.cpp`
- `backend-cpp/tests/test_import_record_deletion_models.cpp`

**修改文件**：

- `backend-cpp/CMakeLists.txt`

**先写测试**：

1. 同一影响集合不同查询顺序产生相同规范 JSON 和 SHA-256 令牌；
2. 导入状态、`updated_at`、候选计数、归档文件、临时 Word、活动解析目录、锁或正式事实引用变化会改变令牌；
3. 允许状态为已上传、解析中、解析失败、待校对、已取消；
4. 已确认、正式事实引用和活动锁分别产生明确阻断原因；
5. 确认文字严格为 `永久删除 DRJL-xxxxxx`；
6. 公共预览 JSON 只有业务快照、数量、锁摘要、阻断信息和令牌，不含文件 ID 或路径。

**实现要点**：内部模型保存稳定 ID 集合和文件分类供事务复核；公共模型只输出用户需要的数量。状态判定、确认文字和规范令牌集中在纯模型，不散落到路由或 SQL。

**定向验证**：`ImportRecordDeletionModelsTest.*`。

**建议提交**：`feat(deletion): model import record deletion impact`

### Task 7：实现影响预览和正式事实保护

**新增文件**：

- `backend-cpp/include/bridge_report/db/ImportRecordDeletionRepository.hpp`
- `backend-cpp/src/db/ImportRecordDeletionRepository.cpp`
- `backend-cpp/tests/test_import_record_deletion_repository.cpp`

**修改文件**：

- `backend-cpp/CMakeLists.txt`

**先写测试**：

1. 五种允许状态返回可删除预览，已确认返回不可删除；
2. 未知记录返回 not found；
3. 预览包含桥梁、年度、版本、病害/照片/评分/解析图片、归档照片、临时 Word 和活动解析目录数量；
4. 有效编辑锁返回编辑人并阻断，过期锁不阻断；
5. `defect_observations`、`defect_photos` 或 `condition_ratings` 任一 `source_import_record_id` 引用都阻断；
6. 仅由目标导入引用的归档文件分类为可删除，其他业务记录仍引用的文件分类为共享保留；
7. 临时 Word 和解析工作目录只进入内部影响计划，公共响应不暴露路径；
8. 预览只读，不取得编辑锁，也不修改导入状态。

**实现要点**：候选数量从受校验的 `parsed_result_json` 数组聚合；评分数量沿用工作台统计口径。文件引用扫描覆盖现有所有 `archived_files` 外键，不能假设附件一定独占。

**定向验证**：`ImportRecordDeletionRepositoryTest.Preview*`。

**建议提交**：`feat(deletion): preview import record deletion impact`

### Task 8：实现原子删除和解析迟到结果保护

**修改文件**：

- `backend-cpp/include/bridge_report/db/ImportRecordDeletionRepository.hpp`
- `backend-cpp/src/db/ImportRecordDeletionRepository.cpp`
- `backend-cpp/tests/test_import_record_deletion_repository.cpp`
- `backend-cpp/include/bridge_report/db/WordImportRepository.hpp`
- `backend-cpp/src/db/WordImportRepository.cpp`
- `backend-cpp/tests/test_word_import_repository.cpp`
- `backend-cpp/src/http/WordImportRoutes.cpp`
- `backend-cpp/tests/test_word_import_routes.cpp`

**先写测试**：

1. 删除事务重新锁定记录并复算影响，旧令牌返回 `impact_changed` 且零修改；
2. 活动锁、已确认状态或新增正式事实引用在事务内阻断；
3. 成功删除导入记录、附件关系、编辑锁和锁事件，并写一条不可恢复审计；
4. 独占归档照片、临时 Word 和活动解析目录先入清理队列，再删除对应元数据；共享归档文件保留；
5. 删除原因和操作者快照正确，完整解析 JSON 不进入审计；
6. 任一 SQL 故障使审计、队列和业务删除完整回滚；
7. 解析中的记录删除后，`persist_parse_result` 返回稳定的 `import_record_deleted`，不重建记录；
8. 迟到结果已复制到归档根的照片批次被回收，解析工作目录被移除；
9. 正常解析开始时登记活动工作目录，正常结束或失败后清理登记；
10. 删除与解析并发时，无论先后顺序都只得到“解析成功保留记录”或“删除成功无记录”之一，不产生半写状态。

**实现要点**：

- 固定事务顺序：锁导入 → 重建影响 → 比令牌 → 复核状态/锁/事实 → 写审计和队列 → 删除附件/独占文件元数据/临时来源行 → 删除导入；
- `import_records` 删除本身清除 `parsed_result_json` 草稿，不能再虚构独立草稿表；
- Word 解析开始时把相对工作目录登记到 `import_source_files`，只允许受控前缀；
- 迟到结果沿用现有 `cleanup_archived_photo_batch` 和 staging 清理路径，并把“记录已删除”与普通数据库失败区分；
- 删除接口不等待 Python 主动取消，数据库记录不存在即是最终边界。

**定向验证**：`ImportRecordDeletionRepositoryTest.Delete*`、`WordImportRepositoryTest.*`、`WordImportRoutesTest.*`。

**建议提交**：`feat(deletion): atomically delete import records`

### Task 9：扩展持续文件清理协调器

**修改文件**：

- `backend-cpp/include/bridge_report/deletion/ArchiveFileDeletionCore.hpp`
- `backend-cpp/src/deletion/ArchiveFileDeletionCore.cpp`
- `backend-cpp/tests/test_archive_file_deletion_core.cpp`
- `backend-cpp/include/bridge_report/deletion/ArchiveFileCleanupCoordinator.hpp`
- `backend-cpp/src/deletion/ArchiveFileCleanupCoordinator.cpp`
- `backend-cpp/tests/test_archive_file_cleanup_coordinator.cpp`
- `backend-cpp/src/main.cpp`

**先写测试**：

1. 协调器除年度、桥梁队列外还能领取导入删除队列；
2. 归档照片使用归档根，临时 Word 使用临时根，根类型不能被队列路径绕过；
3. 解析工作目录只允许在 `work/word-import/` 下递归删除，其他目录拒绝；
4. 文件或目录不存在按幂等成功；
5. 成功、失败退避、未到重试时间、陈旧领取恢复和并发 `SKIP LOCKED` 行为与现有两类队列一致；
6. `process_pending` 处理三类删除队列，`process_import_audit` 只处理指定审计；
7. 全部导入清理项完成后更新导入删除审计，部分失败保持 pending；
8. 年度删除、桥梁删除和正常临时 Word 清理回归不变。

**实现要点**：把当前二值 `QueueKind` 扩展为显式三类元数据；协调器构造函数同时接收归档根和临时 Word 根。递归删除只能由 `artifact_kind=parse_work_directory` 调用严格受限的核心函数。启动和五分钟调度继续调用同一个 `process_pending`，不新增第二套定时器。

**定向验证**：归档删除核心、协调器、年度删除路由、桥梁删除路由和临时 Word 清理器测试。

**建议提交**：`feat(archive): clean files after import deletion`

### Task 10：暴露管理员导入删除 API

**新增文件**：

- `backend-cpp/include/bridge_report/http/ImportRecordDeletionRoutes.hpp`
- `backend-cpp/src/http/ImportRecordDeletionRoutes.cpp`
- `backend-cpp/tests/test_import_record_deletion_routes.cpp`

**修改文件**：

- `backend-cpp/src/main.cpp`
- `backend-cpp/CMakeLists.txt`

**接口**：

```text
GET    /api/import-records/{id}/deletion-impact
DELETE /api/import-records/{id}
```

**先写测试**：

1. 未登录 401、普通用户 403、管理员可预览和删除；
2. 非法 UUID、空/超过 1000 字符原因、错误确认文字和缺失令牌返回稳定 400；
3. not found 返回 404；活动锁、不可删除状态、正式事实引用和令牌变化返回各自稳定 409；
4. 成功响应包含审计 ID、删除数量、文件清理 completed/pending，不含路径；
5. DELETE 提交后立即调用 `process_import_audit`；清理失败仍返回业务删除成功；
6. 与现有 `/api/import-records/{id}/parse-word`、校对、取消和 OPTIONS 路由不冲突；
7. 数据库异常使用中性错误，不向前端传 SQL 或文件系统细节。

**实现要点**：路由只负责认证、管理员鉴权、输入校验、调用仓储和响应脱敏；状态和影响判断全部来自 Task 6—8。若同一路径需要 OPTIONS，只保留一个注册位置。

**定向验证**：`ImportRecordDeletionRoutesTest.*`，随后运行相关 Word、Review 和删除路由测试。

**建议提交**：`feat(api): expose admin import deletion`

### Task 11：增加前端删除合同和危险确认弹窗

**新增文件**：

- `frontend/src/workspace/DeleteImportRecordDialog.tsx`
- `frontend/src/workspace/DeleteImportRecordDialog.test.tsx`

**修改文件**：

- `frontend/src/api/workspaceApi.ts`
- `frontend/src/api/workspaceApi.test.ts`
- `frontend/src/styles.css`

**先写测试**：

1. 预览使用 GET，删除使用 DELETE 并携带原因、确认文字和影响令牌；
2. 类型完整表达候选计数、文件计数、锁、事实引用、可删除性和清理子状态；
3. 稳定错误码映射为可操作中文提示；
4. 弹窗打开即加载最新预览，加载或失败期间不能提交；
5. 显示导入编号、状态、桥梁年度、病害/照片/评分、归档照片和临时 Word 影响；
6. 活动锁显示编辑人，正式事实引用和不可删除状态显示阻断原因；
7. 原因非空且不超过 1000 字符、确认文字逐字一致时才启用危险按钮；
8. 提交中禁止关闭和重复点击；
9. 令牌变化保留弹窗并要求重新预览；
10. 删除成功区分“文件已清理”和“孤立文件等待后台清理”。

**实现要点**：弹窗使用后端返回的确认文字，不在前端自行拼接最终真值。API 类型不包含文件路径字段。成功回调只传必要结果，由年度页面负责刷新。

**定向验证**：workspace API 和 DeleteImportRecordDialog 测试。

**建议提交**：`feat(ui): add import deletion confirmation`

### Task 12：集成年度导入资料卡片

**修改文件**：

- `frontend/src/pages/InspectionWorkspacePage.tsx`
- `frontend/src/pages/InspectionWorkspacePage.test.tsx`
- `frontend/src/styles.css`

**先写测试**：

1. 普通用户的导入卡片没有删除入口；
2. 管理员在可预览记录上看到“删除”，并把正确导入 ID 传给弹窗；
3. 删除入口与“继续校对 / 查看结果 / 重新解析”不互相触发；
4. 已确认记录即使显示删除入口预览也明确阻断；实现可按后端状态提前隐藏或禁用，但后端仍复核；
5. 活动锁信息与删除弹窗一致；
6. 删除成功关闭弹窗并刷新当前年度、年份摘要和桥梁概览；
7. 删除最后一条记录后显示正常空状态；
8. 刷新失败提示“记录已删除，可重新加载”，不允许重复提交同一删除；
9. 原有导入、解析、重试、校对和年度删除流程回归。

**实现要点**：页面保存当前待删除的 `WorkspaceImport`，将管理员动作放在每条导入卡片自身操作区。删除后使用现有 `refreshAll`，不复制工作台加载逻辑。

**定向验证**：`InspectionWorkspacePage.test.tsx` 和前端全量测试。

**建议提交**：`feat(ui): let admins delete import records`

### Task 13：全量回归、文档与完成审计

**修改文件**：

- `README.md`
- `PROJECT_CONTEXT.md`
- `docs/superpowers/specs/2026-07-16-import-record-deletion-and-review-navigation-design.md`
- `docs/superpowers/plans/2026-07-17-import-record-deletion-and-review-navigation-implementation-plan.md`

**自动验证**：

```powershell
Set-Location 'D:\vs2022 code\bridge-report-system'
.\scripts\dev\check-module06-db.ps1

Set-Location 'D:\vs2022 code\bridge-report-system\tools-python'
uv run pytest -q
$env:BRIDGE_REPORT_REAL_WORD_PATH='D:\vs2022 code\bridge-report-system\test-inputs\word-import\绕阳河二号桥报告b8e246e8-cd4d-4202-8d4d-49c56dd34389.docx'
uv run pytest -q tests/importers/test_real_word_regression.py

Set-Location 'D:\vs2022 code\bridge-report-system\backend-cpp'
cmake --build --preset vs2022-x64-debug
$env:BRIDGE_REPORT_TEST_DATABASE_URL='postgresql://bridge_report:bridge_report_dev@127.0.0.1:5432/bridge_report_system'
.\build\vs-debug\Debug\bridge_report_backend_tests.exe --gtest_brief=1

Set-Location 'D:\vs2022 code\bridge-report-system\frontend'
npm run test -- --run
npm run build

Set-Location 'D:\vs2022 code\bridge-report-system'
git diff --check
git status --short
```

**手工验收**：

1. 重新上传一个含无照片病害的 Word：该病害显示“查看照片（0）”，不因无照片进入需要处理；
2. 构造填写照片编号但缺图的病害：仍显示具体缺图警告并要求确认；
3. 大型报告中从需要处理点击病害、已关联照片、未关联照片和评分警告，均准确滚动、高亮和聚焦；
4. 普通用户无删除入口，手工调用接口为 403；
5. 管理员删除待校对、解析失败、已取消记录，原因和影响快照保存在审计；
6. 已确认、有正式事实引用或有张工活动锁的记录不能删除；
7. 删除解析中记录，Python 迟到结果不会使记录重新出现；
8. 占用归档照片或临时 Word使立即清理失败：业务记录仍删除，解除占用后定时器或后端重启继续完成；
9. 年度删除、桥梁删除、临时 Word 24 小时清理、校对保存和确认入库回归正常。

**完成记录**：把设计与计划状态改为已完成，记录数据库、Python、前端、C++、构建和真实 Word 回归的实际结果；确认保护目录仍未提交。除非用户明确要求，不自动推送。

**建议提交**：`docs: complete import deletion and review navigation`

## 4. 依赖顺序

```text
Task 1

Task 2 ─ Task 3 ─ Task 4

Task 5 ─ Task 6 ─ Task 7 ─ Task 8 ─ Task 9 ─ Task 10
                                             └─ Task 11 ─ Task 12

Task 1 + Task 4 + Task 10 + Task 12 ─ Task 13
```

前端定位任务和后端删除任务在逻辑上可以独立验证，但当前共享工作区由单一执行者按编号推进，避免同时修改 `frontend/src/styles.css`、`backend-cpp/CMakeLists.txt` 和 `backend-cpp/src/main.cpp`。

## 5. 完成标准

- 无照片编号的病害不告警、不需要缺图确认；实际引用未匹配图片仍告警；
- 每条病害显示当前导入内的顺序号，序号不进入合同或数据库；
- 所有可定位待处理项使用业务标签并能跳到正确病害、照片或评分项；
- 只有管理员能永久删除尚未形成正式事实的导入记录；
- 已确认、正式事实引用、活动锁和陈旧令牌均能阻断删除；
- 删除原因、操作者、影响和清理状态永久审计，业务正文不可恢复；
- 独占归档照片、临时 Word 和解析工作目录在数据库提交后受控清理，失败可持续重试；
- 解析中删除不会被迟到结果复活；
- 年度删除、整桥删除、临时 Word 生命周期、校对、确认入库和真实 Word 回归全部通过；
- 全量测试、Debug 构建、前端生产构建、数据库 smoke test 和保护目录审计通过。
