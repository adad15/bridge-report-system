# 版本化桥梁规范模块与系统自主评定实施计划

> 实施状态：待实施
>
> 日期：2026-07-17
>
> 设计真源：`docs/superpowers/specs/2026-07-17-versioned-bridge-standards-and-system-assessment-design.md`
>
> 实施分支：`codex/06-5-interaction-redesign`
>
> 计划基线：`60b9e04`

## 1. 目标

本计划把规范相关内容从 Python、TypeScript 和 C++ 业务流程中抽离，建立可注册、可版本化、可审计的规范框架，并交付首批两个规范实现：

- JTG/T H21—2011《公路桥梁技术状况评定标准》；
- JTG 5120—2021《公路桥涵养护规范》。

同时完成以下关联修改：

1. 桥梁创建时根据桥型和数量生成实际构件台账，编号可修改；
2. 实际构件与规范标准类别分层，允许同一桥梁在不同年度检测中采用不同规范；
3. Word 导入合同升级为 2.0，只保留病害事实、病害标度、尺寸和照片，不再读取评分或扣分；
4. C++ 后端成为唯一评分权威，前端通过试算接口显示结果；
5. 所有账号在允许编辑的状态下均可新增、删除病害；
6. 病害页面隐藏“结构部位”；
7. 范围尺寸保存最小值和最大值；
8. 修复编辑锁心跳和大草稿派生计算造成的周期性短暂无响应；
9. 正式结果固定规范版本、规则包校验值、构件台账版本、输入快照和计算轨迹。

## 2. 关键实施决定

### 2.1 当前“检测项目”的落点

当前系统没有跨桥梁的独立项目聚合，`inspection_years` 已承担一座桥一次年度检测项目及其修订版本。因此首期把项目规范组合绑定到 `inspection_years`：

- 同一桥梁不同年度可选择不同规范；
- 年度修订继承原规范组合，主动更换规范时创建新的组合修订；
- 不在本次额外创建跨桥梁项目管理模块；
- 将来若新增跨桥梁项目，只需让多个 `inspection_years` 引用同一项目级规范组合。

### 2.2 桥梁创建时的模板不等于评分绑定

创建桥梁时用户选择一个已安装技术标准作为“构件台账模板来源”，选择桥型并填写构件数量。该选择只用于生成初始实际构件和初始映射，不永久绑定桥梁评分规范。

年度检测创建时再次选择正式使用的技术状况评定标准和养护规范。正式评分只使用年度检测绑定的规范组合。

### 2.3 合同版本

`BridgeAnnualInspectionData` 从 1.2 升级到 2.0，属于有意的破坏性升级：

- 删除 `ratings`；
- 删除 Word `defect_deduction`；
- 增加原始构件编号；
- 区分 Word 原始结构部位与系统解析出的内部结构部位；
- 增加实际构件和标准类别的可空关联字段；
- 尺寸支持单值和范围；
- 手工创建病害可以使用人工来源引用。

最终版本不保留 1.2 运行时兼容路径。实施期间可以短暂同时接受 1.2/2.0 以保持任务提交可测试，但最后一个清理任务必须删除旧路径。

### 2.4 规则包与计算代码

- 规则数据使用版本化 JSON 文件；
- 规则包清单包含官方规范身份、包版本、接口版本、算法 ID 和来源；
- 包摘要按排序后的相对文件名和规范化 JSON 内容计算 SHA-256；计算时排除清单中的 `content_checksum` 字段，再与清单声明值比较；
- H21 通用算法由 C++ 实现，分类、权重、扣分、等级和控制规则来自规则包；
- 将来算法不同的规范通过新的 evaluator 适配器接入；
- 前端只通过 API 获取目录和结果；
- Python 不加载评分规则。

### 2.5 既有数据

用户已确认没有需要保留的桥梁或历史正式评分，并会自行删除现有桥梁。迁移脚本仍不得自动删除桥梁、导入记录、照片或文件；旧评分兼容代码和旧字段在新流程完成后通过明确的清理迁移退出。

## 3. 全局实施规则

1. 严格按任务顺序实施；每个任务先写失败测试，再写最小实现，再运行定向测试。
2. 每个任务保持仓库可构建或明确处于同一协调提交内，不把跨语言合同拆成不可运行的半状态。
3. 不提交或删除 `.claude/`、`test-inputs/`、`test-output/`、`runtime/`、`backend-cpp/archive/`、`tools-python/archive/` 和本地截图。
4. 官方 PDF 只用于核对，不提交到仓库；仓库只保存结构化规则、来源 URL、条款号和必要短标签。
5. 规则录入必须双重核对：结构完整性测试 + 独立人工基准案例。不得仅凭现有程序输出作为预期值。
6. 评分过程中不提前舍入；仅在规范规定的节点或最终展示层执行明确舍入。
7. 前端、Python 或客户端请求中的最终分数永远不是正式输入。
8. 新迁移依次使用 010、011、012、013、014，并全部纳入数据库 smoke。
9. 新增 C++ 文件显式加入 `backend-cpp/CMakeLists.txt`。
10. 所有路由执行认证；规范启停和包同步状态变更要求管理员权限。
11. API、日志和错误响应不返回服务器绝对路径、SQL 或规则包原始文件路径。
12. 每个任务提交前运行 `git diff --check`，只暂存该任务列出的文件。
13. 本计划不是模块 07；未经用户明确许可不设计或实现模块 07。

## 4. 实施任务

### Task 1：修复编辑锁心跳导致的周期性短暂无响应

**修改文件**：

- `frontend/src/pages/ReviewWorkspacePage.tsx`
- `frontend/src/pages/ReviewWorkspacePage.test.tsx`
- `frontend/src/review/grouping.ts`
- `frontend/src/review/grouping.test.ts`
- `frontend/src/review/components/DefectsSection.tsx`
- `frontend/src/review/components/DefectsSection.test.tsx`

**先写测试**：

1. 心跳请求进行中时病害输入框不禁用；
2. 心跳请求进行中时当前焦点不丢失；
3. 心跳成功且锁摘要未变时不重复更新页面锁状态；
4. 单次网络失败进入重试提示，但在租约仍有效时不立即禁用整页；
5. 后端明确返回锁失效、令牌不匹配或租约到期时才切换为只读；
6. 279 条病害下，锁状态变化不会重新执行每条病害的无关派生逻辑；
7. `buildStatistics` 和 `needsAttention` 只在草稿变化时重新计算。

**实现要点**：

- 把 `heartbeat_in_flight` 与 `lost` 分离；
- 删除心跳开始时 `setLockPhase("uncertain")` 的行为；
- `effectiveReadOnly` 不因普通请求进行中改变；
- 使用 `useMemo` 缓存统计和待处理项；
- 稳定心跳 effect 依赖和回调；
- 避免创建内容相同的新锁摘要；
- 先用 React 测试和性能计数证明问题已消失，不在本任务引入列表虚拟化。

**定向验证**：

```powershell
Set-Location 'D:\vs2022 code\bridge-report-system\frontend'
npm run test -- --run src/pages/ReviewWorkspacePage.test.tsx src/review/grouping.test.ts src/review/components/DefectsSection.test.tsx
```

**建议提交**：`fix(review): keep editing responsive during lock heartbeat`

### Task 2：建立规范包合同、加载器和注册表

**新增文件**：

- `standards/README.md`
- `standards/schemas/technical-condition-package.schema.json`
- `standards/schemas/maintenance-package.schema.json`
- `backend-cpp/include/bridge_report/standards/StandardModels.hpp`
- `backend-cpp/include/bridge_report/standards/StandardPackageLoader.hpp`
- `backend-cpp/include/bridge_report/standards/StandardRegistry.hpp`
- `backend-cpp/src/standards/StandardModels.cpp`
- `backend-cpp/src/standards/StandardPackageLoader.cpp`
- `backend-cpp/src/standards/StandardRegistry.cpp`
- `backend-cpp/tests/test_standard_package_loader.cpp`
- `backend-cpp/tests/test_standard_registry.cpp`
- `backend-cpp/tests/fixtures/standards/valid-technical/`
- `backend-cpp/tests/fixtures/standards/valid-maintenance/`
- `backend-cpp/tests/fixtures/standards/invalid-checksum/`

**修改文件**：

- `backend-cpp/include/bridge_report/config/AppConfig.hpp`
- `backend-cpp/src/config/AppConfig.cpp`
- `backend-cpp/src/main.cpp`
- `backend-cpp/CMakeLists.txt`

**先写测试**：

1. 技术评定和养护规则包均能加载；
2. 缺少清单、身份、包版本、接口版本或算法 ID 时拒绝；
3. 未知接口版本拒绝；
4. 同一身份和版本但摘要不同拒绝；
5. 摘要与排序、换行和对象键顺序无关；
6. 重复规则 ID、断开的引用和跨包引用拒绝；
7. 注册表可以按 `family + standard_id + package_version` 精确获取；
8. 同一标准多个包版本可以并存；
9. evaluator 工厂可以注册测试用的非 H21 算法；
10. 错误不包含规范根绝对路径。

**实现要点**：

- 增加 `BRIDGE_REPORT_STANDARDS_ROOT` 配置；
- 启动时先扫描、验证、计算摘要，再注册；
- 规范包对象加载后只读；
- 业务层只依赖接口和注册表；
- 启动时任何启用规范包损坏都记录健康错误，不静默跳过；
- 本任务只建立框架，不录入正式 H21/JTG 5120 规则。

**定向验证**：

```powershell
cmake --build --preset vs2022-x64-debug
.\build\vs-debug\Debug\bridge_report_backend_tests.exe --gtest_filter=StandardPackageLoaderTest.*:StandardRegistryTest.*
```

**建议提交**：`feat(standards): add versioned package registry`

### Task 3：录入并验证首批规范规则包

**新增文件**：

- `standards/technical-condition/jtg-t-h21-2011/1.0.0/manifest.json`
- `standards/technical-condition/jtg-t-h21-2011/1.0.0/bridge-types.json`
- `standards/technical-condition/jtg-t-h21-2011/1.0.0/component-taxonomy.json`
- `standards/technical-condition/jtg-t-h21-2011/1.0.0/inventory-templates.json`
- `standards/technical-condition/jtg-t-h21-2011/1.0.0/defect-indicators.json`
- `standards/technical-condition/jtg-t-h21-2011/1.0.0/deduction-rules.json`
- `standards/technical-condition/jtg-t-h21-2011/1.0.0/weights.json`
- `standards/technical-condition/jtg-t-h21-2011/1.0.0/grade-boundaries.json`
- `standards/technical-condition/jtg-t-h21-2011/1.0.0/single-item-controls.json`
- `standards/technical-condition/jtg-t-h21-2011/1.0.0/sources.json`
- `standards/maintenance/jtg-5120-2021/1.0.0/manifest.json`
- `standards/maintenance/jtg-5120-2021/1.0.0/maintenance-levels.json`
- `standards/maintenance/jtg-5120-2021/1.0.0/inspection-types.json`
- `standards/maintenance/jtg-5120-2021/1.0.0/periodic-inspection-requirements.json`
- `standards/maintenance/jtg-5120-2021/1.0.0/sources.json`
- `backend-cpp/tests/test_jtg_h21_package.cpp`
- `backend-cpp/tests/test_jtg_5120_package.cpp`

**修改文件**：

- `backend-cpp/CMakeLists.txt`

**录入规则**：

1. H21 覆盖规范列出的全部桥型、部件分类、权重、病害指标、标度、扣分、等级和单项控制；
2. JTG 5120 首期只录入已确认边界：养护检查等级、检查类别、定期检查周期和检查内容；
3. 每条规则使用稳定 ID，并记录条款号、表号或附录来源；
4. 不复制大段规范正文，只保存计算所需结构化事实和短标签；
5. 每个标准构件类别声明所属桥型、结构层级和是否可生成实际构件；
6. 每个病害规则声明适用构件和允许标度；
7. 所有权重集合、桥型引用和控制条件通过完整性校验。

**先写测试**：

1. H21 所有桥型均可加载；
2. 每个桥型所有评分层级引用闭合；
3. 权重集合满足规范约束；
4. 所有病害规则都有允许标度和扣分定义；
5. 每条可执行规则有来源引用；
6. JTG 5120 规则包没有评分公式或 H21 扣分表；
7. 两本规范身份、实施日期、版本和 family 正确；
8. 修改任意规则文件都会导致摘要变化。

**人工核对门**：规则录入完成后，按官方 PDF 目录逐章勾选；未完成第二次独立核对前不得进入正式评分验收。

**建议提交**：`feat(standards): add H21 and JTG 5120 rule packages`

### Task 4：迁移规范注册和年度项目规范组合

**新增文件**：

- `database/migrations/010_standard_packages_and_profiles.sql`
- `database/tests/010_standard_packages_and_profiles_smoke.sql`
- `scripts/dev/check-database.ps1`
- `backend-cpp/include/bridge_report/db/StandardRepository.hpp`
- `backend-cpp/src/db/StandardRepository.cpp`
- `backend-cpp/tests/test_standard_repository.cpp`

**修改文件**：

- `backend-cpp/src/main.cpp`
- `backend-cpp/CMakeLists.txt`

**数据库对象**：

- `standard_packages`：保存身份、family、官方版本、包版本、接口版本、摘要、启用状态和同步时间；
- `project_standard_profiles`：保存技术评定标准、养护规范、修订号、状态、创建人和变更原因；
- `inspection_years.standard_profile_id`：绑定当前年度检测项目使用的组合；
- 唯一约束阻止同身份同版本不同摘要；
- 已被项目或正式结果引用的包不能删除。

**先写测试**：

1. 010 可从 001—009 基线执行并可重复运行；
2. 启动同步新增包但不覆盖摘要冲突；
3. 已启用、停用和故障状态可区分；
4. 普通用户不能改变启用状态；
5. 项目组合必须分别引用正确 family；
6. 组合产生正式结果后不可原地改写；
7. 年度修订可以继承组合；
8. 删除被引用规范包失败；
9. 当前空业务数据库无需旧评分回填。

**验证命令**：运行新的全库检查脚本和 `StandardRepositoryTest.*`。现有 `check-module06-db.ps1` 保持模块 06.5 的原验证边界，不继续承担后续全项目迁移。

**建议提交**：`feat(database): register versioned standard profiles`

### Task 5：提供规范目录、启停和年度选择 API

**新增文件**：

- `backend-cpp/include/bridge_report/http/StandardRoutes.hpp`
- `backend-cpp/src/http/StandardRoutes.cpp`
- `backend-cpp/include/bridge_report/standards/StandardCatalogModels.hpp`
- `backend-cpp/src/standards/StandardCatalogModels.cpp`
- `backend-cpp/tests/test_standard_routes.cpp`
- `frontend/src/api/standardsApi.ts`
- `frontend/src/api/standardsApi.test.ts`
- `frontend/src/standards/StandardsAdminPanel.tsx`
- `frontend/src/standards/StandardsAdminPanel.test.tsx`

**修改文件**：

- `backend-cpp/src/main.cpp`
- `backend-cpp/CMakeLists.txt`
- `backend-cpp/include/bridge_report/db/WorkspaceRepository.hpp`
- `backend-cpp/src/db/WorkspaceRepository.cpp`
- `backend-cpp/src/http/WorkspaceRoutes.cpp`
- `backend-cpp/tests/test_workspace_routes.cpp`
- `backend-cpp/tests/test_workspace_repository.cpp`
- `frontend/src/api/workspaceApi.ts`
- `frontend/src/api/workspaceApi.test.ts`
- `frontend/src/workspace/CreateInspectionDialog.tsx`
- `frontend/src/workspace/CreateInspectionDialog.test.tsx`
- `frontend/src/pages/BridgesPage.tsx`
- `frontend/src/pages/BridgesPage.test.tsx`

**API 能力**：

- 列出已安装、已启用规范和版本；
- 获取支持桥型、构件目录、病害目录和养护要求；
- 管理员启用或停用未被故障阻断的包；
- 创建年度检测时选择技术评定标准和养护规范；
- 读取年度工作区时返回锁定组合。

**先写测试**：

1. 目录接口不返回文件路径和内部异常；
2. 普通用户可读已启用目录但不能启停；
3. 创建年度必须选择一个 technical-condition 和一个 maintenance 包；
4. family 错配返回稳定错误；
5. 只有一个启用选项时前端默认选中但仍显示版本；
6. 同一桥梁不同年度可选择不同规范；
7. 已有年度冲突响应仍返回原年度 ID；
8. 工作区显示规范编号、规则包版本和状态。
9. 只有管理员看到规范启停入口，停用不影响历史结果读取。

**建议提交**：`feat(workspace): select standards per inspection project`

### Task 6：迁移版本化构件台账和规范映射

**新增文件**：

- `database/migrations/011_component_inventory_revisions.sql`
- `database/tests/011_component_inventory_revisions_smoke.sql`

**修改文件**：

- `scripts/dev/check-database.ps1`
- `database/tests/007_bridge_administration_smoke.sql`

**数据库模型**：

- 继续使用 `bridge_components.id` 作为稳定物理构件身份；
- `bridge_component_inventory_revisions`：桥梁、版本号、草稿/已确认状态、基线版本、确认人和时间；
- `bridge_component_inventory_entries`：某版本中的构件编号、现场名称、现场类型、跨位、启用状态和排序；
- `bridge_component_standard_mappings`：台账条目到规范包标准类别、内部结构部位和确认状态的映射；
- `bridge_component_generation_batches`：模板来源、桥型、输入数量和生成时间；
- `component_aliases` 继续绑定稳定物理构件 ID；
- 现有 `bridge_components` 的结构部位和可变显示字段标记为旧投影，新代码逐步停止依赖。

**先写 smoke**：

1. 011 可在 010 后执行并可重复运行；
2. 同桥版本号唯一；
3. 同一台账版本内“现场类型 + 构件编号”唯一；
4. 同一条目和规范包只能有一个有效映射；
5. 已确认版本不能直接修改条目；
6. 已被病害引用的物理构件不能删除；
7. 未引用的草稿构件可以删除；
8. 停用原因和时间成对约束；
9. 桥梁删除仍正确级联台账和映射；
10. 正式结果引用的台账版本受 `restrict` 保护。

**建议提交**：`feat(database): version bridge component inventories`

### Task 7：实现构件生成、编辑、确认和映射后端

**新增文件**：

- `backend-cpp/include/bridge_report/inventory/ComponentInventoryModels.hpp`
- `backend-cpp/src/inventory/ComponentInventoryModels.cpp`
- `backend-cpp/include/bridge_report/inventory/ComponentInventoryGenerator.hpp`
- `backend-cpp/src/inventory/ComponentInventoryGenerator.cpp`
- `backend-cpp/include/bridge_report/db/ComponentInventoryRepository.hpp`
- `backend-cpp/src/db/ComponentInventoryRepository.cpp`
- `backend-cpp/include/bridge_report/http/ComponentInventoryRoutes.hpp`
- `backend-cpp/src/http/ComponentInventoryRoutes.cpp`
- `backend-cpp/tests/test_component_inventory_generator.cpp`
- `backend-cpp/tests/test_component_inventory_repository.cpp`
- `backend-cpp/tests/test_component_inventory_routes.cpp`

**修改文件**：

- `backend-cpp/src/main.cpp`
- `backend-cpp/CMakeLists.txt`
- `backend-cpp/src/deletion/BridgeDeletionModels.cpp`
- `backend-cpp/src/db/BridgeDeletionRepository.cpp`
- `backend-cpp/tests/test_bridge_deletion_models.cpp`
- `backend-cpp/tests/test_bridge_deletion_repository.cpp`

**先写测试**：

1. 5 跨、每跨 6 片主梁生成 30 个稳定物理构件和默认编号；
2. 不同构件类别可使用不同编号模板；
3. 修改显示编号不改变物理构件 ID；
4. 重复编号、空编号和不合法数量被拒绝；
5. 自定义现场名称必须映射到所选规范标准类别；
6. 无法映射的构件保持待处理，不静默归入“其他”；
7. 已确认版本的修改自动创建下一草稿版本；
8. 已引用构件只能停用；
9. 台账未确认或映射未完成时返回明确阻断项；
10. 桥梁删除影响统计包含新增台账对象。

**API 能力**：生成草稿、读取版本、修改条目、增加构件、删除未引用构件、停用构件、维护映射、确认台账。

**建议提交**：`feat(inventory): manage generated bridge components`

### Task 8：扩展桥梁创建和构件台账前端

**新增文件**：

- `frontend/src/api/componentInventoryApi.ts`
- `frontend/src/api/componentInventoryApi.test.ts`
- `frontend/src/bridges/BridgeInventoryWizard.tsx`
- `frontend/src/bridges/BridgeInventoryWizard.test.tsx`
- `frontend/src/bridges/ComponentInventoryEditor.tsx`
- `frontend/src/bridges/ComponentInventoryEditor.test.tsx`

**修改文件**：

- `frontend/src/bridges/CreateBridgeDialog.tsx`
- `frontend/src/bridges/CreateBridgeDialog.test.tsx`
- `frontend/src/pages/BridgeDetailPage.tsx`
- `frontend/src/styles.css`

**交互步骤**：

1. 填写桥梁基础信息；
2. 选择构件模板来源规范和桥型；
3. 填写跨数及各类构件数量；
4. 预览自动生成编号；
5. 创建桥梁和草稿台账；
6. 修改编号、现场名称、所属跨和映射；
7. 确认台账。

**先写测试**：

1. 模板来源明确提示“不绑定未来评分规范”；
2. 规范和桥型改变时清除不兼容数量，不保留脏映射；
3. 自动编号预览与后端返回一致；
4. 编号可编辑且内部 ID 不展示；
5. 未映射项集中显示并可跳转；
6. 台账确认前给出数量、重复和映射摘要；
7. 已引用构件只显示停用，不显示永久删除；
8. 创建桥梁后即使用户暂时退出，待确认台账仍可继续编辑。

**建议提交**：`feat(bridges): create and confirm component inventories`

### Task 9：实现 H21 技术状况评定引擎

**新增文件**：

- `backend-cpp/include/bridge_report/standards/TechnicalConditionStandard.hpp`
- `backend-cpp/include/bridge_report/standards/AssessmentModels.hpp`
- `backend-cpp/include/bridge_report/standards/H21Evaluator.hpp`
- `backend-cpp/src/standards/AssessmentModels.cpp`
- `backend-cpp/src/standards/H21Evaluator.cpp`
- `backend-cpp/tests/test_h21_component_evaluation.cpp`
- `backend-cpp/tests/test_h21_part_evaluation.cpp`
- `backend-cpp/tests/test_h21_bridge_evaluation.cpp`
- `backend-cpp/tests/test_h21_grade_and_controls.cpp`
- `samples/scoring/standards/jtg-t-h21-2011/`

**修改文件**：

- `backend-cpp/src/standards/StandardRegistry.cpp`
- `backend-cpp/CMakeLists.txt`
- `samples/scoring/component_score_cases.json`

**先写基准测试**：

1. 无病害完整台账；
2. 单构件单病害；
3. 单构件多病害且输入顺序不同；
4. 构件评分 DP 边界和 DP=100；
5. 部件均值、最低值和 t 值组合；
6. 桥面系、上部结构、下部结构权重；
7. 全桥评分；
8. 各等级边界；
9. 单项控制触发和不触发；
10. H21 全部桥型至少一个完整层级案例；
11. 规则缺失或输入构件不适用时失败，不按 100 分兜底；
12. 结构化轨迹能复核每一步输入、规则和输出。

**实现要点**：

- 把现有 `ComponentScore` 算法迁移进 H21 evaluator，暂留薄兼容包装供过渡测试；
- 所有分类、权重、标度、扣分和控制条件从规则包读取；
- 中间计算使用足够精度，不提前四舍五入；
- evaluator 是纯计算，不访问数据库或 HTTP；
- 输出同时包含机器可读轨迹和短用户说明；
- 使用人工核算或规范示例建立预期值，不以旧 Python/前端实现为真源。

**建议提交**：`feat(standards): evaluate bridges with H21`

### Task 10：实现 JTG 5120 养护规则查询适配器

**新增文件**：

- `backend-cpp/include/bridge_report/standards/MaintenanceStandard.hpp`
- `backend-cpp/include/bridge_report/standards/Jtg5120MaintenanceStandard.hpp`
- `backend-cpp/src/standards/Jtg5120MaintenanceStandard.cpp`
- `backend-cpp/tests/test_jtg_5120_maintenance_standard.cpp`

**修改文件**：

- `backend-cpp/src/standards/StandardRegistry.cpp`
- `backend-cpp/src/http/StandardRoutes.cpp`
- `backend-cpp/CMakeLists.txt`

**先写测试**：

1. 可读取养护检查等级；
2. 可读取初始、日常、经常、定期和特殊检查分类元数据；
3. 首期定期检查周期和检查内容按上下文返回；
4. 不支持或缺少必要上下文时返回明确问题；
5. 输出带规则来源；
6. 适配器不暴露评分接口，也不包含 H21 公式；
7. 未来 maintenance 标准可以使用相同接口注册。

**建议提交**：`feat(standards): expose JTG 5120 maintenance rules`

### Task 11：升级 BridgeAnnualInspectionData 2.0 并停止 Word 评分输入

**协调修改文件**：

- `tools-python/bridge_report_tools/contracts/annual_inspection.py`
- `tools-python/bridge_report_tools/contracts/export_schema.py`
- `tools-python/bridge_report_tools/importers/word_importer.py`
- `tools-python/bridge_report_tools/importers/defect_tables.py`
- `tools-python/tests/test_annual_inspection_contract.py`
- `tools-python/tests/importers/test_word_importer.py`
- `tools-python/tests/importers/test_defect_tables.py`
- `contracts/bridge_annual_inspection_data.schema.json`
- `backend-cpp/include/bridge_report/contracts/AnnualInspectionContract.hpp`
- `backend-cpp/src/contracts/AnnualInspectionContract.cpp`
- `backend-cpp/src/review/ContractCompatibility.cpp`
- `backend-cpp/tests/test_annual_inspection_contract.cpp`
- `backend-cpp/tests/test_contract_compatibility.cpp`
- `frontend/src/contracts/annualInspection.ts`
- `frontend/src/contracts/annualInspection.test.ts`
- `frontend/src/review/testFixtures.ts`

**2.0 病害字段要点**：

- `source_structure_part`：Word 原始结构部位，可空，仅作证据；
- `component_name`：Word 或人工现场名称；
- `component_number`：原始或人工构件编号；
- `bridge_component_id`：后端解析出的稳定物理构件 ID，可空；
- `standard_component_category_id`：当前规范类别，可空；
- `resolved_structure_part`：根据规范映射得到的内部结构部位，可空；
- `defect_scale`：Word 或人工标度，可空草稿；
- 删除 `defect_deduction`；
- 删除整个 `ratings`；
- 人工候选允许 `source_ref.source_type = manual`，Word 页表行字段可空。

**先写测试**：

1. 合同版本严格为 2.0；
2. 缺少 `ratings` 仍合法，存在 `ratings` 在最终模式下拒绝；
3. 存在 Word 扣分字段在最终模式下拒绝；
4. Word 包含评分表时导入成功且输出没有评分候选、评分警告和 `rating_table_not_found`；
5. 构件编号进入病害候选；
6. Python 输出的数据库关联字段为 null；
7. 人工来源引用合法；
8. 三端 schema 和运行时校验一致；
9. 契约错误继续返回精确字段路径。

**过渡要求**：本任务可以保留仅供下一任务切换的 1.2 兼容读取，但必须用显式 feature gate 标识，并在 Task 18 删除。Python 新输出从本任务开始只生成 2.0。

**建议提交**：`feat(contract): remove imported ratings in version 2`

### Task 12：扩展范围尺寸并迁移正式尺寸事实

**新增文件**：

- `database/migrations/012_defect_measurement_ranges.sql`
- `database/tests/012_defect_measurement_ranges_smoke.sql`

**修改文件**：

- `tools-python/bridge_report_tools/contracts/annual_inspection.py`
- `tools-python/bridge_report_tools/importers/measurements.py`
- `tools-python/tests/importers/test_measurements.py`
- `tools-python/tests/importers/test_real_word_regression.py`
- `contracts/bridge_annual_inspection_data.schema.json`
- `backend-cpp/src/contracts/AnnualInspectionContract.cpp`
- `backend-cpp/src/db/ReviewRepository.cpp`
- `backend-cpp/tests/test_annual_inspection_contract.cpp`
- `backend-cpp/tests/test_review_repository.cpp`
- `frontend/src/contracts/annualInspection.ts`
- `frontend/src/contracts/annualInspection.test.ts`
- `frontend/src/review/measurementParser.ts`
- `frontend/src/review/measurementParser.test.ts`
- `frontend/src/review/grouping.ts`
- `frontend/src/review/grouping.test.ts`
- `scripts/dev/check-database.ps1`

**合同**：

- `value_type = single | range`；
- 单值使用 `value`；
- 范围使用 `minimum_value` 和 `maximum_value`；
- 增加 `is_approximate`；
- 始终保留 `source_text` 和规范化单位。

**先写测试**：

1. `0.5~4.0m`、`0.5～4.0m`、`15至20m` 保存范围；
2. `约1.0m²` 保存近似单值；
3. `长度20.0m` 保存稳定单值；
4. 最小值大于最大值拒绝；
5. single/range 字段互斥约束；
6. 同一尺寸只产生一个稳定警告；
7. Python 和前端不再为稳定表达派生同义警告；
8. 012 增加 `value_type`、最小值、最大值、近似标志和 check constraint；
9. 正式入库和读取保留范围端点。

**建议提交**：`feat(measurements): preserve numeric ranges`

### Task 13：开放人工新增、删除病害并隐藏结构部位

**修改文件**：

- `frontend/src/review/reviewDraft.ts`
- `frontend/src/review/reviewDraft.test.ts`
- `frontend/src/review/components/DefectsSection.tsx`
- `frontend/src/review/components/DefectsSection.test.tsx`
- `frontend/src/review/components/DefectPhotoGroup.tsx`
- `frontend/src/review/components/DefectPhotoGroup.test.tsx`
- `frontend/src/review/components/UnlinkedPhotosPanel.tsx`
- `frontend/src/review/components/UnlinkedPhotosPanel.test.tsx`
- `frontend/src/pages/ReviewWorkspacePage.tsx`
- `frontend/src/pages/ReviewWorkspacePage.test.tsx`
- `frontend/src/styles.css`
- `backend-cpp/src/review/DraftValidation.cpp`
- `backend-cpp/tests/test_draft_validation.cpp`

**新增动作**：

- `add_defect`：使用 `manual_defect_<uuid>` 生成不复用 ID；
- `delete_defect`：删除病害并把关联照片改为未关联；
- `link_defect_component`：选择实际构件并填入内部映射字段。

**先写测试**：

1. 普通账号首次待校对且持锁时可新增、删除；
2. 管理员完整重开时可新增、删除；
3. `warnings_only` 重开不能新增、删除；
4. 已确认、只读或无锁不能新增、删除；
5. 新增病害至少要求构件类别、构件编号、位置、类型和描述；
6. 标度可暂空，但正式确认前必须补齐；
7. 页面不渲染结构部位下拉框；
8. 删除病害不删除照片，照片进入未关联区域；
9. 删除后显示序号连续，但系统 ID 不复用；
10. 后端拒绝重复候选 ID、伪造内部结构部位和 warnings_only 结构变化；
11. 手工新增和删除写入草稿审计摘要。

**实现要点**：候选 ID 工厂在测试中可注入；`resolved_structure_part` 只能由所选构件映射产生，前端不提供自由编辑入口。

**建议提交**：`feat(review): let editors add and remove defects`

### Task 14：实现病害到实际构件的匹配与人工确认

**新增文件**：

- `backend-cpp/include/bridge_report/inventory/ComponentMatcher.hpp`
- `backend-cpp/src/inventory/ComponentMatcher.cpp`
- `backend-cpp/tests/test_component_matcher.cpp`
- `frontend/src/review/components/ComponentMatchField.tsx`
- `frontend/src/review/components/ComponentMatchField.test.tsx`

**修改文件**：

- `backend-cpp/src/db/WordImportRepository.cpp`
- `backend-cpp/src/http/ReviewRoutes.cpp`
- `backend-cpp/src/review/DraftValidation.cpp`
- `backend-cpp/tests/test_word_import_repository.cpp`
- `backend-cpp/tests/test_review_routes.cpp`
- `frontend/src/api/reviewApi.ts`
- `frontend/src/api/reviewApi.test.ts`
- `frontend/src/review/components/DefectPhotoGroup.tsx`
- `frontend/src/review/grouping.ts`
- `frontend/src/review/reviewNavigation.ts`

**匹配顺序**：

1. 构件编号和现场类型完全匹配；
2. 已确认别名匹配；
3. 规范化编号生成候选；
4. 多候选或无候选时由用户选择。

**先写测试**：

1. 完全匹配自动关联；
2. 已确认别名自动关联并记录方式；
3. 多候选不自动选择；
4. 模糊文本不静默匹配；
5. 台账或规范映射变化后可以重新匹配；
6. 用户选择实际构件后内部结构部位来自映射；
7. 未匹配病害进入“需要处理”并精确跳转；
8. 未确认台账只能提供候选，不能满足正式确认；
9. API 不允许客户端伪造不属于当前桥梁的构件 ID。

**建议提交**：`feat(review): match defects to inventory components`

### Task 15：迁移系统评定运行、结果和轨迹

**新增文件**：

- `database/migrations/013_system_assessment_runs.sql`
- `database/tests/013_system_assessment_runs_smoke.sql`

**修改文件**：

- `scripts/dev/check-database.ps1`

**数据库对象**：

- `assessment_runs`：试算/正式、输入摘要、规范包、项目组合、台账版本、状态、确认人和时间；
- `assessment_component_results`：实际构件级结果；
- `assessment_part_results`：部件和结构级结果；
- `assessment_control_results`：单项控制；
- `assessment_rule_traces`：结构化执行步骤；
- `condition_ratings.assessment_run_id`：兼容现有档案查询的系统结果投影来源。

**先写 smoke**：

1. 013 可在 012 后执行并可重复运行；
2. 正式运行必须引用规范包、规范组合和已确认台账版本；
3. 输入摘要、规则包摘要和结果状态必填；
4. 同一年度只有一个当前正式评定，修订保留历史；
5. 试算可以清理，正式结果不能级联丢失；
6. 轨迹规则 ID、输入和输出 JSON 有结构约束；
7. 结果分数范围和非有限值由应用层及数据库共同保护；
8. 被正式结果引用的规范包和台账版本不能删除。

**建议提交**：`feat(database): persist system assessment runs`

### Task 16：实现评定试算 API 和前端结果展示

**新增文件**：

- `backend-cpp/include/bridge_report/assessment/AssessmentService.hpp`
- `backend-cpp/src/assessment/AssessmentService.cpp`
- `backend-cpp/include/bridge_report/http/AssessmentRoutes.hpp`
- `backend-cpp/src/http/AssessmentRoutes.cpp`
- `backend-cpp/tests/test_assessment_service.cpp`
- `backend-cpp/tests/test_assessment_routes.cpp`
- `frontend/src/api/assessmentApi.ts`
- `frontend/src/api/assessmentApi.test.ts`
- `frontend/src/review/assessmentState.ts`
- `frontend/src/review/assessmentState.test.ts`
- `frontend/src/review/components/AssessmentSection.tsx`
- `frontend/src/review/components/AssessmentSection.test.tsx`

**修改文件**：

- `backend-cpp/src/main.cpp`
- `backend-cpp/CMakeLists.txt`
- `frontend/src/pages/ReviewWorkspacePage.tsx`
- `frontend/src/pages/ReviewWorkspacePage.test.tsx`
- `frontend/src/review/components/ReviewSidebar.tsx`
- `frontend/src/review/grouping.ts`
- `frontend/src/styles.css`

**API 输入**：导入记录 ID、当前草稿、客户端草稿 revision 和编辑锁令牌。后端自行加载年度规范组合和已确认台账版本，忽略任何客户端评分字段。

**API 输出**：客户端 revision、输入摘要、规范身份、各级结果、等级、控制结果、结构化轨迹和待处理项。

**先写测试**：

1. 无锁提交未保存草稿试算被拒绝；
2. 后端使用服务器端规范组合和台账，不接受客户端覆盖；
3. 未匹配构件、缺标度和未确认台账返回结构化阻断；
4. 完整草稿返回 H21 结果和轨迹；
5. 同输入和规则包产生相同摘要和结果；
6. 前端停止编辑 500—800ms 后合并请求；
7. 新请求取消或废弃旧请求；
8. 旧 revision 响应不能覆盖新草稿；
9. 请求中页面保持可编辑和焦点；
10. 用户可手动重新试算；
11. 评定问题进入现有跳转和高亮模型；
12. 页面显示系统规范和规则包版本，不显示 Word 评分。

**建议提交**：`feat(assessment): preview system-calculated ratings`

### Task 17：把正式确认切换到系统自主评分

**新增文件**：

- `backend-cpp/include/bridge_report/assessment/AssessmentConfirmationService.hpp`
- `backend-cpp/src/assessment/AssessmentConfirmationService.cpp`
- `backend-cpp/tests/test_assessment_confirmation_service.cpp`

**修改文件**：

- `backend-cpp/src/http/ImportConfirmRoutes.cpp`
- `backend-cpp/src/review/PreflightReport.cpp`
- `backend-cpp/src/review/ConfirmPlan.cpp`
- `backend-cpp/src/review/DraftValidation.cpp`
- `backend-cpp/include/bridge_report/db/ReviewRepository.hpp`
- `backend-cpp/src/db/ReviewRepository.cpp`
- `backend-cpp/src/db/WorkspaceRepository.cpp`
- `backend-cpp/src/db/ComponentArchiveRepository.cpp`
- `backend-cpp/tests/test_preflight_report.cpp`
- `backend-cpp/tests/test_confirm_plan.cpp`
- `backend-cpp/tests/test_draft_validation.cpp`
- `backend-cpp/tests/test_review_repository.cpp`
- `backend-cpp/tests/test_workspace_repository.cpp`
- `backend-cpp/tests/test_component_archive_repository.cpp`
- `frontend/src/api/reviewApi.ts`
- `frontend/src/review/confirmFlow.ts`
- `frontend/src/review/confirmFlow.test.ts`
- `frontend/src/pages/ReviewWorkspacePage.tsx`

**事务顺序**：

1. 锁定导入记录、年度检测、规范组合和台账版本；
2. 校验用户、编辑锁、草稿 revision 和当前版本；
3. 校验所有病害关联、类型和标度；
4. 加载精确规则包并复核摘要；
5. 在事务持锁期间运行纯 evaluator；
6. 写病害、尺寸和照片事实；
7. 写正式 `assessment_run`、各级结果、控制结果和轨迹；
8. 写 `condition_ratings` 系统结果投影，供现有档案查询；
9. 更新 `inspection_years.overall_score/overall_grade`；
10. 提交后释放编辑锁并进入现有归档流程。

**先写测试**：

1. 正式确认完全不读取草稿评分；
2. 客户端伪造最终分数不影响结果；
3. 最后试算输入未变时正式结果完全一致；
4. 台账、规范、草稿或锁变化会中止；
5. evaluator 异常时整个事务回滚，不留下部分事实；
6. 正式结果引用精确包摘要和台账版本；
7. 结构化轨迹永久保存；
8. 年度修订生成新运行，不覆盖旧运行；
9. 档案页继续显示系统构件评分和结构评分；
10. `condition_ratings` 中不存在 Word 来源分或人工选择状态；
11. 确认响应计数和现有前端提示更新为系统评定语义。

**建议提交**：`feat(assessment): confirm system-owned bridge ratings`

### Task 18：删除旧评分代码、清理旧字段并完成全量回归

**删除文件**：

- `tools-python/bridge_report_tools/importers/rating_tables.py`
- `tools-python/bridge_report_tools/importers/component_ratings.py`
- `tools-python/bridge_report_tools/scoring/component_score.py`
- `tools-python/bridge_report_tools/scoring/__init__.py`
- `tools-python/tests/test_component_score.py`
- `frontend/src/review/componentScore.ts`
- `frontend/src/review/componentScore.test.ts`
- `frontend/src/review/components/RatingsSection.tsx`
- `frontend/src/review/components/RatingsSection.test.tsx`
- `backend-cpp/include/bridge_report/review/ComponentScore.hpp`
- `backend-cpp/src/review/ComponentScore.cpp`
- `backend-cpp/tests/test_component_score.cpp`

**新增文件**：

- `database/migrations/014_remove_imported_rating_legacy.sql`
- `database/tests/014_remove_imported_rating_legacy_smoke.sql`

**修改文件**：

- `tools-python/bridge_report_tools/importers/word_rules/` 下仅服务评分表的规则和测试
- `tools-python/tests/importers/test_real_word_regression.py`
- `backend-cpp/CMakeLists.txt`
- `backend-cpp/tests/support/review_fixtures.hpp`
- 所有仍构造合同 1.2 `ratings` 的 C++、前端和 Python fixtures
- `database/dev/seed_module05_review_sample.sql`
- `scripts/dev/check-database.ps1`
- `README.md`

**014 清理**：

- 删除 `condition_ratings.source_score`、`calculated_score`、`score_validation_status`、`score_resolution_reason` 和旧计算明细列；
- 删除 `defect_observations.defect_deduction` 和 `component_score` 旧来源字段；
- 保留 `condition_ratings` 作为系统评定投影，并要求 `assessment_run_id`；
- 删除旧评分状态 check constraint；
- 更新桥梁、年度和导入删除影响统计。
- 迁移开始时检查旧评分或旧扣分列是否仍有非空业务行；若存在则明确失败，要求先按已确认方案删除现有桥梁，不静默丢弃数据。

**最终静态检查**：

1. 生产代码中不存在 `parse_rating_tables`、`build_component_rating_candidates`、`rating_table_not_found`；
2. Python 和 TypeScript 中不存在独立评分公式；
3. `BridgeAnnualInspectionData` 只有 2.0；
4. Word 规则中不再识别评分表；
5. 旧人工状态“人工接受Word值/人工采用复算值”不再出现在生产代码和数据库约束；
6. `JTG/T H21-2011 4.1.1` 不再散落在三端，只存在规范包来源和 H21 evaluator 测试；
7. 模块 07 没有新文件或代码。

**真实 Word 新预期**：

- 25 条病害继续解析；
- 31 个照片候选、36 个 Word 图片、31 张可归档照片继续成立；
- 病害标度和尺寸继续保留；
- 导入 JSON 不再出现原 15 个评分候选；
- 完成构件台账匹配后，由 H21 evaluator 产生系统评分；
- 系统评分预期来自独立人工核算基准，不从原报告复制。

**建议提交**：`refactor(assessment): remove imported rating legacy`

## 5. 每阶段验证门

### Gate A：规范框架与规则包

完成 Task 2—5 后：

- 两本规范包通过摘要、引用和完整性校验；
- 规范目录和年度选择可用；
- H21 和 JTG 5120 职责没有混合；
- 使用测试规范证明未来可以注册第三本技术评定标准。

### Gate B：构件台账

完成 Task 6—8 后：

- 创建桥梁可生成并确认构件台账；
- 修改编号不破坏稳定 ID；
- 同一物理构件可建立不同规范映射；
- 正式引用构件不能物理删除。

### Gate C：合同与病害编辑

完成 Task 11—14 后：

- 新 Word 导入只产生合同 2.0；
- 不读取评分或扣分；
- 范围尺寸完整保存；
- 所有账号在允许状态下可新增、删除病害；
- 病害能关联实际构件，结构部位不显示。

### Gate D：自主评分

完成 Task 9、15—17 后：

- 前端试算和正式确认使用同一 H21 evaluator；
- 正式结果绑定规范包、台账版本和输入摘要；
- 客户端不能指定最终分数；
- 现有档案查询读取系统评分投影。

### Gate E：旧路径清零

完成 Task 18 后：

- 旧评分解析和三端重复公式全部删除；
- 合同 1.2 不再被运行时接受；
- 数据库不再保存 Word 来源分与人工二选一状态；
- 全量回归通过。

## 6. 全量自动验证

### 6.1 Python

```powershell
Set-Location 'D:\vs2022 code\bridge-report-system\tools-python'
uv run pytest -q
```

真实 Word：

```powershell
$env:BRIDGE_REPORT_REAL_WORD_PATH='D:\vs2022 code\bridge-report-system\test-inputs\word-import\绕阳河二号桥报告b8e246e8-cd4d-4202-8d4d-49c56dd34389.docx'
uv run pytest -q tests/importers/test_real_word_regression.py
```

### 6.2 前端

```powershell
Set-Location 'D:\vs2022 code\bridge-report-system\frontend'
npm run test -- --run
npm run build
```

### 6.3 C++

```powershell
Set-Location 'D:\vs2022 code\bridge-report-system'
cmake --build --preset vs2022-x64-debug
$env:BRIDGE_REPORT_TEST_DATABASE_URL='postgresql://bridge_report:bridge_report_dev@127.0.0.1:5432/bridge_report_system'
.\build\vs-debug\Debug\bridge_report_backend_tests.exe --gtest_brief=1
```

### 6.4 数据库

```powershell
Set-Location 'D:\vs2022 code\bridge-report-system'
.\scripts\dev\check-database.ps1
```

数据库检查必须明确顺序执行 002—014；001 的独立骨架检查保持现有流程。

### 6.5 静态残留检查

```powershell
rg -n "parse_rating_tables|build_component_rating_candidates|rating_table_not_found|人工接受Word值|人工采用复算值" tools-python frontend/src backend-cpp database
rg -n "compute_component_score|ComponentScore" tools-python frontend/src backend-cpp
```

允许命中仅限历史设计文档、迁移注释或明确的 H21 evaluator 迁移说明；生产路径不得命中旧实现。

## 7. 手工验收

### 7.1 性能

1. 打开包含约 279 条病害的校对页；
2. 在输入框持续编辑并跨过至少两个 30 秒心跳周期；
3. 验证输入框不禁用、不失焦、不丢字符；
4. 修改病害后观察延迟试算，页面保持响应；
5. 快速连续修改，确认旧评分结果不回写。

### 7.2 桥梁和构件台账

1. 创建梁式桥并输入跨数与每跨主梁数量；
2. 检查自动编号；
3. 修改一个构件编号和现场名称；
4. 确认台账；
5. 创建另一个年度并选择不同测试规范，验证同一物理构件可重新映射。

### 7.3 Word 导入和病害

1. 导入真实 Word；
2. 验证页面没有 Word 评分候选；
3. 验证病害、标度、照片和范围尺寸；
4. 处理未匹配构件；
5. 普通用户新增一条病害；
6. 删除一条病害，验证照片进入未关联区域；
7. 验证页面不显示结构部位。

### 7.4 系统评分

1. 补齐全部病害标度并完成构件映射；
2. 执行试算，展开构件、部件、结构和全桥计算轨迹；
3. 与独立人工核算基准比较；
4. 立即确认，验证试算与正式结果一致；
5. 在确认前制造草稿变化，验证旧试算不能被保存；
6. 用户在系统外自行与原报告比较，系统不自动调整结果。

### 7.5 规范版本

1. 安装测试用第二版本规则包；
2. 验证新年度可选择新版本；
3. 验证旧正式结果仍显示原版本和摘要；
4. 验证已引用旧包不能删除；
5. 验证管理员可以停用其用于新项目，但历史结果仍可读。

## 8. 最终交付条件

满足以下条件后才可声明完成：

1. 设计文档第 23 节的 16 项验收标准全部有自动测试或手工证据；
2. Python、前端、C++、数据库、构建和真实 Word 回归全部通过；
3. H21 规则完成第二次独立核对；
4. H21 全部桥型有完整性测试，当前梁式桥有端到端基准；
5. JTG 5120 首期边界完成且没有混入评分公式；
6. Word 评分读取、评分警告和三端重复评分代码全部删除；
7. 前端经过至少两个心跳周期的大草稿手工验收；
8. 受保护的未跟踪目录和文件没有提交或删除；
9. `git diff --check` 通过；
10. 用户明确验收后才允许另行更新 `PROJECT_CONTEXT.md` 为已完成状态并推送；
11. 模块 07 仍未开始。

## 9. 提交顺序建议

1. `fix(review): keep editing responsive during lock heartbeat`
2. `feat(standards): add versioned package registry`
3. `feat(standards): add H21 and JTG 5120 rule packages`
4. `feat(database): register versioned standard profiles`
5. `feat(workspace): select standards per inspection project`
6. `feat(database): version bridge component inventories`
7. `feat(inventory): manage generated bridge components`
8. `feat(bridges): create and confirm component inventories`
9. `feat(standards): evaluate bridges with H21`
10. `feat(standards): expose JTG 5120 maintenance rules`
11. `feat(contract): remove imported ratings in version 2`
12. `feat(measurements): preserve numeric ranges`
13. `feat(review): let editors add and remove defects`
14. `feat(review): match defects to inventory components`
15. `feat(database): persist system assessment runs`
16. `feat(assessment): preview system-calculated ratings`
17. `feat(assessment): confirm system-owned bridge ratings`
18. `refactor(assessment): remove imported rating legacy`

实施过程中可以在不改变依赖顺序的前提下合并紧密耦合的协调提交，但不得把未经测试的规则录入、合同破坏性升级和正式确认重写混为一个不可审查的大提交。
