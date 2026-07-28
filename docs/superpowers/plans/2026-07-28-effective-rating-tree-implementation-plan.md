# 桥梁有效评定树实施计划

> **For agentic workers:** 逐任务实施，每完成一项才进入下一项。严格执行“先写失败测试 → 确认失败原因 → 最小实现 → 测试通过 → 只提交本任务相关文件”。工作区已有用户未提交修改，禁止覆盖、清理或顺带提交。

**Goal:** 实现 [桥梁有效评定树与现有规范融合设计](../specs/2026-07-28-effective-rating-tree-design.md)：把 JTG/T H21—2011、JTG 5120—2021 和单位桥梁评定树编译成不可变版本，强制年度检测通过评定树选择病害，同时继续使用 H21 计算内核完成规范评分。

**Architecture:** 规范文件仍是规则真源。新增 `rating-tree` 单位扩展包、C++ 加载器与编译器；编译结果同步到 PostgreSQL，并通过只读 API 提供给前端。`project_standard_profiles` 锁定评定树版本，同时保留其底层 H21、JTG 5120 包身份。病害保存用户选择的树节点，H21 指标由服务端解析并写入现有 `standard_defect_indicator_id`。评分服务拒绝绕过树节点的输入，解析成功后继续调用现有 H21 计算器。

**Tech Stack:** JSON Schema；C++20 / Drogon / JsonCpp / GoogleTest；PostgreSQL；React 18 / TypeScript / Vitest。

## 实施边界

第一期只做桥梁：

- 梁式桥；
- 拱式桥；
- 悬索桥；
- 斜拉桥；
- 桥梁下部结构；
- 桥面系。

不做：

- 涵洞；
- 隧道；
- 涵洞 JTG 5120 分支；
- 在线树编辑器；
- 管理员规则写接口；
- 单位自定义扣分表；
- 修改 H21 公式、权重、扣分或等级边界；
- `其他病害`具体录入和评分。

`其他病害（暂不计分）`只生成不可选择的占位标题。

## 不得改变的核心决策

1. 产品层不再保留纯 H21 年度评定入口。
2. H21 规则包和计算器不删除，它们是底层评分真源。
3. 新建年度只选择一个已发布评定树版本，底层两套规范包由树版本确定。
4. 管理员和普通用户都只能查看评定树，不能在线编辑。
5. 单位节点只能继承或引用 H21；第一期不能覆盖 H21 数值。
6. JTG 5120 只提供检查养护语义，不产生第二套一般技术状况扣分。
7. 同一单位节点可以同时保存 H21 评分来源、JTG 5120 检查来源和单位来源。
8. Word 解析规则不改；树匹配发生在构件上下文已确定之后。
9. 名称完全匹配或受控别名唯一命中才可自动绑定；模糊匹配只给候选。
10. `standard_defect_indicator_id`由服务端从树节点派生，客户端不能任意指定。
11. 同一构件多个单位节点解析到同一 H21 指标时，仍按该指标最高标度只计一次。
12. 历史正式评分不被新树版本重算覆盖。

## 实施前工作区约束

当前工作区已有其他未提交修改，至少包含：

- `backend-cpp/include/bridge_report/db/ComponentInventoryRepository.hpp`
- `backend-cpp/include/bridge_report/inventory/ComponentInventoryGenerator.hpp`
- `frontend/src/pages/ReviewWorkspacePage.tsx`
- `frontend/src/pages/ReviewWorkspacePage.test.tsx`
- `frontend/src/review/components/DefectDetailEditor.tsx`
- `frontend/src/review/components/DefectQuickReviewList.tsx`
- `frontend/src/review/components/DefectsSection.tsx`
- `frontend/src/review/defectPhotoReviewModel.ts`
- `frontend/src/styles.css`
- 多份模块 03 图和规格
- `samples/scoring/component_score_cases.json`
- `.claude/`
- `scripts/dev/start-all.ps1`
- `docs/superpowers/评定树/`

其中病害详情、病害派生模型、工作区页面和样式与本功能必然重叠。每个相关任务开始前保存局部 diff，使用小范围补丁合并，禁止整文件覆盖，禁止 `git reset --hard`、`git checkout --` 或清理用户文件。

---

## Task 0：记录基线并确认现有测试

- [ ] 记录 `git status --short` 和当前分支。
- [ ] 保存所有将要重叠文件的局部 diff。
- [ ] 运行现有 C++、前端、Python 和数据库基线测试。
- [ ] 若基线已有失败，只记录与隔离，不顺带修复无关问题。

验证：

```powershell
git status --short
cmake --build --preset vs-debug
.\build\vs-debug\Debug\bridge_report_backend_tests.exe --gtest_brief=1
npm run test -- --run
uv run pytest
.\scripts\dev\check-database.ps1
```

提交：无。

## Task 1：补全 H21 标度文字真源

**修改：**

- `standards/schemas/technical-condition-package.schema.json`
- `standards/technical-condition/jtg-t-h21-2011/1.0.1/defect-indicators.json`
- `standards/technical-condition/jtg-t-h21-2011/1.0.1/manifest.json`
- `backend-cpp/tests/test_jtg_h21_package.cpp`
- `backend-cpp/tests/test_standard_package_loader.cpp`

步骤：

- [ ] 先写失败测试：每个参与评分指标的每个允许标度都必须有非空判定文字。
- [ ] 在 schema 中为指标增加结构化 `scale_descriptions`，键必须覆盖 `allowed_scales`。
- [ ] 从 H21 正式表格补录判定文字，不从截图猜测扣分。
- [ ] 更新规则包内容校验值。
- [ ] 验证缺标度说明、重复标度和额外标度均导致规则包加载失败。

完成条件：

- H21 的标度、判定文字和扣分均可由稳定 ID 关联；
- 原有 H21 评分用例结果完全不变。

提交建议：

```text
feat(standards): add H21 scale descriptions
```

## Task 2：建立单位评定树规则包和 schema

**新增：**

- `standards/schemas/rating-tree-extension-package.schema.json`
- `standards/rating-tree/organization-bridge/1.0.0/manifest.json`
- `standards/rating-tree/organization-bridge/1.0.0/tree.json`
- `standards/rating-tree/organization-bridge/1.0.0/aliases.json`
- `standards/rating-tree/organization-bridge/1.0.0/sources.json`
- `backend-cpp/tests/fixtures/rating-tree/`

**修改：**

- `standards/README.md`

步骤：

- [ ] 先定义稳定节点 ID、父子关系、节点类型、排序、适用桥型、适用构件、评分模式和来源引用。
- [ ] 只转录 `docs/superpowers/评定树/` 中已确认的桥梁分支。
- [ ] 对每个 `inherit_h21` 节点保存 H21 指标引用。
- [ ] 对每个 `reference_h21` 节点保存明确引用；第一期至少包含“水损 → 混凝土碳化”。
- [ ] 将“渗水泛碱 → 水损”写成限定桥型和构件范围的受控别名。
- [ ] 将 `其他病害（暂不计分）`写为不可选择 `placeholder`。
- [ ] 不转录涵洞、隧道和涵洞 JTG 5120 分支。
- [ ] 为截图中的每个桥梁叶节点建立人工核对清单，验证名称、路径、顺序和规范引用。

完成条件：

- 规则文件能够完整表达单位桥梁树；
- 单位包中不存在扣分值、权重、评分公式和等级边界；
- 所有评分节点都显式引用合法 H21 指标。

提交建议：

```text
feat(standards): add organization bridge rating tree
```

## Task 3：实现评定树模型、加载器和确定性校验值

**新增：**

- `backend-cpp/include/bridge_report/rating_tree/RatingTreeModels.hpp`
- `backend-cpp/src/rating_tree/RatingTreeModels.cpp`
- `backend-cpp/include/bridge_report/rating_tree/RatingTreePackageLoader.hpp`
- `backend-cpp/src/rating_tree/RatingTreePackageLoader.cpp`
- `backend-cpp/tests/test_rating_tree_package_loader.cpp`

**修改：**

- `backend-cpp/CMakeLists.txt`

步骤：

- [ ] 先写加载有效包、无效清单、路径逃逸、摘要冲突、重复 ID、缺父节点和循环的失败测试。
- [ ] 复用 `StandardPackageLoader` 的规范化 JSON 和 SHA-256 约定，不另造不兼容摘要算法。
- [ ] 解析节点、别名、来源和单位扩展版本。
- [ ] 对文件列表排序后计算内容摘要，保证同内容重复加载得到相同结果。
- [ ] 加载器只读本地规则文件，不接收 HTTP 写入。

完成条件：

- 合法单位包可加载；
- 非法结构在编译前即被拒绝；
- 测试不依赖真实截图文件路径。

提交建议：

```text
feat(rating-tree): load organization extension packages
```

## Task 4：实现有效树编译器

**新增：**

- `backend-cpp/include/bridge_report/rating_tree/RatingTreeCompiler.hpp`
- `backend-cpp/src/rating_tree/RatingTreeCompiler.cpp`
- `backend-cpp/tests/test_rating_tree_compiler.cpp`
- `backend-cpp/tests/support/rating_tree_fixtures.hpp`

步骤：

- [ ] 先写最小 H21、JTG 5120、单位扩展的编译失败测试。
- [ ] 按固定顺序合并三种来源。
- [ ] 同一病害只有在名称和范围一致，或单位规则显式声明等价时才合并。
- [ ] `inherit_h21`和`reference_h21`都解析 H21 指标、适用构件、标度说明、扣分表和来源表号。
- [ ] 只有 JTG 5120 来源的节点生成 `non_scoring`。
- [ ] `placeholder`不可选择、不计分、无标度。
- [ ] 参与评分但 H21 引用缺失、不适用或规则不完整时编译失败。
- [ ] 排除非桥梁分支。
- [ ] 对标准化编译结果计算 `tree_content_checksum`。
- [ ] 使用黄金文件验证节点数、路径、来源合并和确定性摘要。

完成条件：

- 相同输入生成字节级稳定的有效树；
- 编译结果没有第二套扣分值；
- “水损”显示单位名称，但解析到混凝土碳化。

提交建议：

```text
feat(rating-tree): compile immutable effective trees
```

## Task 5：实现上下文解析器和自动匹配

**新增：**

- `backend-cpp/include/bridge_report/rating_tree/RatingTreeResolver.hpp`
- `backend-cpp/src/rating_tree/RatingTreeResolver.cpp`
- `backend-cpp/tests/test_rating_tree_resolver.cpp`

步骤：

- [ ] 先写桥型、构件类别、原始名称和树版本四维上下文测试。
- [ ] 名称完全一致且唯一时返回 `exact`。
- [ ] 受控别名唯一命中时返回 `controlled_alias`。
- [ ] 文字相似只返回排序候选，不返回已绑定结果。
- [ ] 多候选返回歧义状态。
- [ ] 跨桥型、跨构件类别和跨版本不得匹配。
- [ ] 解析结果同时返回树节点 ID、H21 指标 ID、允许标度和匹配证据。

完成条件：

- “渗水泛碱”只在声明范围内自动解析到“水损”；
- 其他相似病害不会被静默绑定。

提交建议：

```text
feat(rating-tree): resolve scoped defect matches
```

## Task 6：新增评定树数据库结构与约束

**新增：**

- `database/migrations/018_rating_tree_versions.sql`
- `database/tests/018_rating_tree_versions_smoke.sql`

数据库对象：

- `rating_tree_versions`
- `rating_tree_nodes`
- `rating_tree_node_sources`
- `rating_tree_aliases`
- `project_standard_profiles.rating_tree_version_id`
- `defect_observations.rating_tree_node_id`

步骤：

- [ ] 先写 smoke test，验证表、外键、唯一约束、状态约束和索引。
- [ ] 树版本保存三个来源身份、版本、摘要、树摘要、状态和发布时间。
- [ ] 节点保存父子关系、显示信息、适用范围、评分模式、H21 解析目标和只读详情。
- [ ] 来源表允许一个节点关联多种来源。
- [ ] 别名唯一键包含树版本、桥型、构件类别和别名。
- [ ] `project_standard_profiles`保留两套规范包 ID，并增加树版本外键。
- [ ] 用触发器验证 profile 的两套规范包与树版本来源完全一致。
- [ ] 用触发器保护已发布树及其节点、来源和别名不可更新或删除。
- [ ] `defect_observations.rating_tree_node_id`与现有 H21 指标共同建立评分查询索引。

迁移策略：

- 初次迁移允许历史 profile 的树版本为空；
- 新建 profile 必须有树版本；
- 启动同步完成唯一回填后再执行非空一致性检查；
- 无法唯一回填的测试数据列入清理清单，不静默猜测。

提交建议：

```text
feat(db): add immutable rating tree storage
```

## Task 7：实现数据库同步、发布和历史 profile 回填

**新增：**

- `backend-cpp/include/bridge_report/db/RatingTreeRepository.hpp`
- `backend-cpp/src/db/RatingTreeRepository.cpp`
- `backend-cpp/tests/test_rating_tree_repository.cpp`

**修改：**

- `backend-cpp/src/main.cpp`
- `backend-cpp/CMakeLists.txt`
- `backend-cpp/include/bridge_report/db/StandardRepository.hpp`
- `backend-cpp/src/db/StandardRepository.cpp`

步骤：

- [ ] 测试首次同步插入、同摘要幂等、同版本异摘要冲突和编译失败不发布。
- [ ] 启动时先加载标准包，再编译评定树，最后在单一事务中写版本、节点、来源和别名。
- [ ] 只有通过全部校验的版本才标记 `published`。
- [ ] 同步错误进入健康状态，但不能覆盖已发布版本。
- [ ] 按 H21、JTG 5120 包组合查找唯一已发布树，为历史 profile 回填树版本。
- [ ] 一个组合存在多个候选时不自动回填，记录明确故障。
- [ ] profile 创建和修订改为接收树版本，并由服务端派生底层包 ID。

完成条件：

- 重启不会重复生成不同版本；
- 失败同步不破坏现有年度；
- 已发布树没有任何数据库写路径。

提交建议：

```text
feat(rating-tree): sync published tree versions
```

## Task 8：新增只读评定树 API

**新增：**

- `backend-cpp/include/bridge_report/http/RatingTreeRoutes.hpp`
- `backend-cpp/src/http/RatingTreeRoutes.cpp`
- `backend-cpp/tests/test_rating_tree_routes.cpp`

**修改：**

- `backend-cpp/src/main.cpp`
- `backend-cpp/CMakeLists.txt`

接口：

```text
GET /api/rating-trees
GET /api/rating-trees/{versionId}
GET /api/rating-trees/{versionId}/nodes
GET /api/rating-trees/{versionId}/nodes/{nodeId}
GET /api/rating-trees/{versionId}/search
GET /api/rating-trees/{versionId}/applicable-defects
```

步骤：

- [ ] 测试版本列表只返回已发布或明确请求的停用版本。
- [ ] 节点接口返回完整路径、评分模式、标度说明、扣分和多来源。
- [ ] `applicable-defects`强制要求桥型与构件类别。
- [ ] 搜索返回命中节点及祖先路径。
- [ ] 大树节点接口支持父节点过滤或分页。
- [ ] 验证路由表中不存在 POST、PUT、PATCH、DELETE。
- [ ] API 不返回规则文件绝对路径。

提交建议：

```text
feat(api): expose read-only rating trees
```

## Task 9：年度创建改为强制选择评定树

**修改：**

- `backend-cpp/include/bridge_report/http/WorkspaceRoutes.hpp`
- `backend-cpp/src/http/WorkspaceRoutes.cpp`
- `backend-cpp/include/bridge_report/db/WorkspaceRepository.hpp`
- `backend-cpp/src/db/WorkspaceRepository.cpp`
- `backend-cpp/tests/test_workspace_routes.cpp`
- `backend-cpp/tests/test_workspace_repository.cpp`
- `frontend/src/workspace/CreateInspectionDialog.tsx`
- `frontend/src/workspace/CreateInspectionDialog.test.tsx`
- `frontend/src/api/workspaceApi.ts`
- `frontend/src/api/workspaceApi.test.ts`

步骤：

- [ ] 将创建请求从两个 package ID 改为 `rating_tree_version_id`。
- [ ] 后端验证树已发布、未停用且底层包可用。
- [ ] 根据树版本查找或创建不可变规范 profile。
- [ ] 年度只绑定该 profile。
- [ ] 前端只有一个评定树选择框；唯一版本自动选择，多个版本显式选择。
- [ ] 页面展示树名称、版本、H21 版本和 JTG 5120 版本。
- [ ] 删除用户直接组合两套规范包的产品入口。
- [ ] 修订年度继承原树版本，不自动升级。

完成条件：

- 客户端无法提交任意 H21/JTG 5120 组合；
- 旧接口请求返回明确契约错误，不被静默兼容。

提交建议：

```text
feat(workspace): require a published rating tree
```

## Task 10：扩展病害草稿合同但保持 Word 解析不变

**修改：**

- `tools-python/bridge_report_tools/contracts/annual_inspection.py`
- `tools-python/tests/test_annual_inspection_contract.py`
- `tools-python/bridge_report_tools/importers/defect_tables.py`
- `tools-python/tests/importers/test_defect_tables.py`
- `contracts/bridge_annual_inspection_data.schema.json`
- `frontend/src/contracts/annualInspection.ts`
- `frontend/src/contracts/annualInspection.test.ts`
- `backend-cpp/src/contracts/AnnualInspectionContract.cpp`
- `backend-cpp/tests/test_annual_inspection_contract.cpp`
- `samples/contracts/bridge_annual_inspection_data.v3.valid.json`
- `samples/contracts/bridge_annual_inspection_data.v3.with-comparison.json`

病害新增：

```text
rating_tree_version_id
rating_tree_node_id
rating_tree_match_method
rating_tree_match_evidence
```

步骤：

- [ ] 新字段允许解析初始值为空。
- [ ] Word 导入仍按原规则产生病害文字、标度、构件原文和照片引用。
- [ ] Python 不读取评定树、不复制匹配算法。
- [ ] 后端在加载年度校对上下文后负责树匹配和字段填充。
- [ ] 原始 `defect_type`和`defect_description`永远不被节点显示名称覆盖。
- [ ] `standard_defect_indicator_id`保留，但明确为服务端派生字段。

完成条件：

- 同一 Word 样例的病害数量、原始文字和照片候选完全不变；
- 新树字段不让解析器产生虚假绑定。

提交建议：

```text
feat(contract): add rating tree defect references
```

## Task 11：将评定树接入校对后端和正式入库

**修改：**

- `backend-cpp/include/bridge_report/review/ReviewModels.hpp`
- `backend-cpp/src/review/ReviewModels.cpp`
- `backend-cpp/src/review/DraftValidation.cpp`
- `backend-cpp/src/review/ConfirmPlan.cpp`
- `backend-cpp/src/db/ReviewRepository.cpp`
- `backend-cpp/src/http/ReviewRoutes.cpp`
- 对应 GoogleTest 文件

步骤：

- [ ] 工作区读取年度树版本并把只读树上下文返回前端。
- [ ] 构件绑定完成后调用 `RatingTreeResolver`。
- [ ] 唯一 exact 或 controlled alias 命中时写树节点和匹配证据。
- [ ] 模糊或歧义结果只写候选，不写已绑定节点。
- [ ] 保存草稿时忽略客户端伪造的 H21 指标，服务端从树节点重新解析。
- [ ] 构件变化导致节点不适用时清除树节点和 H21 指标，保留病害、标度来源、照片及 Word 证据。
- [ ] 正式确认前校验树版本、节点归属、构件适用性、评分模式和标度。
- [ ] 正式入库写 `rating_tree_node_id`和解析后的 `standard_defect_indicator_id`。
- [ ] `non_scoring`可保存但不伪造 H21 指标；`placeholder`不能被选择。

完成条件：

- 绕过前端直接提交 H21 指标不能改变评分来源；
- 重新绑定构件不会沿用旧树映射；
- 照片关系不因重新选择树节点而丢失。

提交建议：

```text
feat(review): resolve defects through rating trees
```

## Task 12：评分服务强制验证评定树

**修改：**

- `backend-cpp/include/bridge_report/assessment/AssessmentService.hpp`
- `backend-cpp/src/assessment/AssessmentService.cpp`
- `backend-cpp/src/assessment/AssessmentConfirmationService.cpp`
- `backend-cpp/tests/test_assessment_service.cpp`
- `backend-cpp/tests/test_assessment_confirmation_service.cpp`
- `backend-cpp/tests/test_h21_component_evaluation.cpp`
- `backend-cpp/tests/test_h21_bridge_evaluation.cpp`
- `samples/scoring/component_score_cases.json`

步骤：

- [ ] 评分上下文增加树版本身份和摘要。
- [ ] 每条参与评分病害必须同时具备合法树节点和服务端解析的 H21 指标。
- [ ] 验证树节点属于年度锁定版本且适用于实际构件。
- [ ] `non_scoring`跳过扣分，并在追踪中记录节点与跳过原因。
- [ ] `placeholder`引用、跨版本引用和 H21 解析不一致直接阻止正式评定。
- [ ] 同构件同 H21 指标继续取最高标度，避免多个单位别名重复扣分。
- [ ] 评分运行保存树版本 ID、树摘要、H21 包 ID 和 profile ID。
- [ ] 证明所有原 H21 黄金用例通过树入口后结果一致。
- [ ] 增加“水损参照碳化”和 JTG 5120-only 非计分用例。

完成条件：

- 产品层无法执行不带树版本的正式评分；
- H21 计算器代码和公式不被复制或改写。

提交建议：

```text
feat(assessment): enforce rating tree scoring inputs
```

## Task 13：实现前端评定树 API、缓存与只读页面

**新增：**

- `frontend/src/api/ratingTreeApi.ts`
- `frontend/src/api/ratingTreeApi.test.ts`
- `frontend/src/rating-tree/ratingTreeViewState.ts`
- `frontend/src/rating-tree/ratingTreeViewState.test.ts`
- `frontend/src/pages/RatingTreePage.tsx`
- `frontend/src/pages/RatingTreePage.test.tsx`
- `frontend/src/rating-tree/RatingTreeNavigator.tsx`
- `frontend/src/rating-tree/RatingTreeNodeDetail.tsx`

**修改：**

- `frontend/src/App.tsx`
- `frontend/src/styles.css`

步骤：

- [ ] 新增 `/rating-trees/:versionId`只读路由。
- [ ] 左侧显示树、搜索、展开收起和命中路径。
- [ ] 右侧显示完整路径、适用范围、评分模式、H21 来源、JTG 5120 来源、单位说明、标度判定和扣分。
- [ ] `placeholder`显示“暂不计分”且没有选择操作。
- [ ] 页面不显示新增、编辑、发布、停用和删除按钮，管理员也一样。
- [ ] 数据按版本缓存；同版本再次进入不重复等待。
- [ ] 展开节点、搜索词和当前节点按版本保存，返回页面立即恢复。
- [ ] 大树采用按父节点加载或虚拟列表。
- [ ] 页面直接访问、刷新、无权限写操作和 API 错误都有测试。

完成条件：

- 页面是独立入口，不依附某条病害；
- 切换页面后搜索和展开状态保留；
- 页面没有任何规则写请求。

提交建议：

```text
feat(frontend): add read-only rating tree explorer
```

## Task 14：病害与照片页面改用树节点

**修改：**

- `frontend/src/review/components/DefectDetailEditor.tsx`
- `frontend/src/review/components/DefectQuickReviewList.tsx`
- `frontend/src/review/components/DefectsSection.tsx`
- `frontend/src/review/defectPhotoReviewModel.ts`
- `frontend/src/review/reviewDraft.ts`
- `frontend/src/pages/ReviewWorkspacePage.tsx`
- `frontend/src/api/reviewApi.ts`
- 对应 Vitest 文件
- `frontend/src/styles.css`

步骤：

- [ ] 把“规范病害”选择器替换为当前树版本、桥型和实际构件范围内的叶节点。
- [ ] 选择树节点后由 API 返回允许标度和 H21 评分来源。
- [ ] 显示单位完整路径、匹配方式、是否计分和基础规范来源。
- [ ] exact/受控别名自动命中可进入快速确认；模糊建议不能进入批量安全项。
- [ ] 未选树节点、节点不适用、标度非法和照片异常保持独立提示。
- [ ] 增加“在评定树中查看”，打开当前年度树版本的准确节点。
- [ ] 构件改变后立即清除前端旧节点显示，等待后端重新解析。
- [ ] 保持现有右侧可拖拽详情抽屉、分页、筛选和照片关系交互不退化。

完成条件：

- 用户不能直接选择或提交 H21 指标；
- 大量病害可通过唯一匹配继续批量确认；
- 需要判断的病害仍进入精细维护。

提交建议：

```text
feat(review): select defects from the rating tree
```

## Task 15：迁移、诊断与历史结果保护

**新增或修改：**

- `database/migrations/019_require_rating_tree_binding.sql`
- `database/tests/019_require_rating_tree_binding_smoke.sql`
- `scripts/dev/check-database.ps1`
- `backend-cpp/src/review/PreflightReport.cpp`
- `backend-cpp/src/assessment/AssessmentService.cpp`
- 对应测试

步骤：

- [ ] 在默认树同步成功后，按原 profile 的 H21/JTG 5120 组合回填树版本。
- [ ] 按原 `standard_defect_indicator_id`查找当前树节点。
- [ ] 唯一节点自动迁移；多个单位节点引用同一 H21 指标时进入人工确认。
- [ ] 原始病害、描述、标度、照片、Word 证据和正式评分结果不改写。
- [ ] 对已经正式确认的历史结果只补追踪身份，不重算覆盖。
- [ ] 清理无法迁移的测试数据前输出精确清单并要求显式执行。
- [ ] 回填完成后强制新年度/profile 必须绑定树版本。
- [ ] 健康检查报告树加载、编译、同步、发布和回填状态。

完成条件：

- 正常历史年度能够打开并显示绑定树；
- 歧义数据不会被错误自动匹配；
- 系统不再产生新的无树年度。

提交建议：

```text
feat(migration): require rating tree bindings
```

## Task 16：全链路回归与验收

- [ ] 运行所有 C++ 单元及数据库集成测试。
- [ ] 运行所有前端测试和生产构建。
- [ ] 运行 Python 合同及真实 Word 回归。
- [ ] 运行数据库迁移和 smoke tests。
- [ ] 启动前后端验证健康状态。
- [ ] 用真实测试 Word 验证：解析数量不变、构件绑定后自动匹配、病害确认、照片关系、正式评分。
- [ ] 手工核对单位截图桥梁分支与只读页面路径。
- [ ] 核对“水损”显示、标度、扣分来源及评分追踪。
- [ ] 核对“其他病害（暂不计分）”不可选择。
- [ ] 核对管理员界面也没有规则编辑入口。
- [ ] 核对旧树版本年度在发布新版本后结果不变。

最终命令：

```powershell
cmake --build --preset vs-debug
.\build\vs-debug\Debug\bridge_report_backend_tests.exe --gtest_brief=1
npm run test -- --run
npm run build
uv run pytest
.\scripts\dev\check-database.ps1
curl.exe -s http://127.0.0.1:18080/health
```

## 最终验收清单

- [ ] 所有新建年度必须选择已发布评定树。
- [ ] 产品界面没有纯 H21 评定模式。
- [ ] H21 仍是唯一技术状况评分算法来源。
- [ ] JTG 5120 检查养护来源可追踪但不重复扣分。
- [ ] 单位桥梁树层级、名称和排序与确认资料一致。
- [ ] 每个计分节点都有完整 H21 标度文字与扣分来源。
- [ ] 病害匹配受桥型、构件和版本约束。
- [ ] 模糊匹配不会自动确认。
- [ ] `standard_defect_indicator_id`只能由服务端派生。
- [ ] 同一 H21 指标不会因单位节点数量增加而重复扣分。
- [ ] 只读树页再次进入立即显示并保留状态。
- [ ] 管理员和普通用户都不能修改规则。
- [ ] 历史正式结果不被新版本覆盖。
- [ ] 涵洞和隧道未进入第一期功能。
