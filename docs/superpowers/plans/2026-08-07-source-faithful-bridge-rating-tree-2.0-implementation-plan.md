# 实施计划：仿照来源软件的桥梁评定树 2.0

设计文档：[2026-08-07-source-faithful-bridge-rating-tree-2.0-design.md](../specs/2026-08-07-source-faithful-bridge-rating-tree-2.0-design.md)

日期：2026-08-07

> 状态：已实施。当前生成器默认发布 `organization-bridge/2.0.3` 并锁定 H21 1.0.4；2.0.3 的结构与评分语义沿用 2.0.2。本文任务清单保留为历史执行记录。

## 实施目标

发布 `organization-bridge/2.0.0`，使第 5–10 节桥梁评定树的结构、顺序、显示编号和指标复用
关系与来源软件一致；来源离线库病害按 `judgeTreeId + judgeIndexId` 精确绑定；评分仍完全由
JTG/T H21—2011 规范包执行；没有 H21 依据的“其它病害”和扩展病害暂不扣分。

## 实施约束

- 来源软件只提供检测数据和一次性的树结构参照，运行时不得依赖来源软件评定树表。
- 只处理第 5–10 节；第 11–14 节不建节点、不建映射、不设计状态。
- 2.0.0 不发布别名或关键词规则；运行时取消所有文字自动匹配和文字候选推荐。
- 不复制来源软件的 `judgeStandard`、扣分值、得分、等级或权重。
- 现有 1.0.1–1.0.3 包保持不变；现有数据为测试数据，不建设历史数据迁移。
- 工作区已有大量未提交改动。每个提交必须按路径限定，不能带入无关文件或改写用户改动。
- 所有来源数据库连接使用 SQLite `mode=ro`；完整来源库不得提交到仓库。

## 关键实现决定

### 显式编号

包模型和有效树节点新增可选 `display_number`。数据库 `rating_tree_nodes` 新增可空列
`display_number`：2.0.0 必填，1.x 历史节点保持空值。API 返回该字段，前端只在字段为空时
使用旧 `node_key` 推导逻辑。

### 来源映射的运行时存储

发布包使用独立的 `source-index-map.json`。编译器把每条来源映射附着到目标有效节点，
`RatingTreeRepository` 将它们写入节点现有的 `detail_json.source_mappings`。这样不增加第二套
节点外键表，也能在数据库重新装载有效树后完成精确匹配。

### 来源身份契约

来源病害在共享契约中保留四个原始字段：

```text
source_defect_group_id          = outerCheckData.judgeTreeId
source_defect_indicator_id      = outerCheckData.judgeIndexId
source_defect_group_number      = judgeTree.chapterNum
source_defect_indicator_number  = judgeIndex.tableNum
```

当前 `source_defect_indicator_id` 被临时写成派生的 `h21.defect.*`；本实施将其纠正为来源软件
原始 `judgeIndexId`。测试数据无需迁移。

### 匹配行为

`RatingTreeResolver` 只保留：

1. 来源分组 ID + 来源指标 ID；
2. 来源分组编号 + 来源指标编号；
3. 无唯一结果时返回未识别；
4. 用户手工选择继续由既有草稿校验链保护。

名称精确匹配、别名、关键词、片段、包含关系和模糊候选全部停止使用。旧包中的相关文件和
数据库表保留为历史内容，但运行时解析器不读取它们作结论。

---

## Task 0：记录基线和保护工作区

**修改：无。**

步骤：

- [ ] 记录 `git status --short`，确认当前已暂存和未暂存的用户改动。
- [ ] 记录后端、Python、前端与数据库测试基线；区分本任务前已存在的失败。
- [ ] 记录当前 1.0.3 包的校验值、节点数、别名数和关键词规则数。
- [ ] 对来源离线库只读计算 SHA-256，记录 `judgeTree`、`judgeIndex`、`judgeTree2Index`
      行数以及第 5–10 节唯一“分组—指标”关系数 403。
- [ ] 不复制活动库直接作为测试输入；后续导出器只产出脱敏结构快照。

验证：

```powershell
git status --short
git diff --check
uv run pytest
npm run test
```

后端和数据库使用仓库现有检查脚本记录基线。

提交：无。

---

## Task 1：扩展共享契约，保留完整来源评定身份

**修改：**

- `tools-python/bridge_report_tools/contracts/annual_inspection.py`
- `tools-python/tests/test_annual_inspection_contract.py`
- `contracts/bridge_annual_inspection_data.schema.json`
- `backend-cpp/src/contracts/AnnualInspectionContract.cpp`
- `backend-cpp/tests/test_annual_inspection_contract.cpp`
- `frontend/src/contracts/annualInspection.ts`
- `frontend/src/contracts/annualInspection.test.ts`
- `samples/contracts/bridge_annual_inspection_data.v3.valid.json`
- `samples/contracts/bridge_annual_inspection_data.v3.with-comparison.json`

步骤：

- [ ] 先写失败测试，断言四个来源身份字段允许 `null` 或非空字符串，拒绝空白字符串。
- [ ] 将 `source_defect_indicator_id` 的注释和语义改成来源 `judgeIndexId`，不再代表 H21 ID。
- [ ] 新增 `source_defect_group_id`、`source_defect_group_number`、
      `source_defect_indicator_number`。
- [ ] 四个来源字段都是只读证据；清理派生评定树字段时不得清空它们。
- [ ] 从 Python Pydantic 模型重新生成共享 JSON Schema，不手工调整生成结构。
- [ ] 同步 C++ 校验、TypeScript 类型守卫和两份契约样例。
- [ ] 保留现有 `rating_tree_match_method = source_indicator` 枚举值，避免引入只改名字的迁移。

验证：

```powershell
cd tools-python
uv run python -m bridge_report_tools.contracts.export_schema
uv run pytest tests/test_annual_inspection_contract.py
cd ../frontend
npm run test -- src/contracts/annualInspection.test.ts
```

后端构建后运行：

```powershell
.\build\vs-debug\Debug\bridge_report_backend_tests.exe --gtest_filter=*AnnualInspectionContract*
```

提交建议：

```text
feat(contract): preserve source rating group and indicator identity
```

---

## Task 2：扩展评定树包模型和加载校验

**修改：**

- `standards/schemas/rating-tree-extension-package.schema.json`
- `backend-cpp/include/bridge_report/rating_tree/RatingTreeModels.hpp`
- `backend-cpp/src/rating_tree/RatingTreePackageLoader.cpp`
- `backend-cpp/tests/test_rating_tree_package_loader.cpp`

步骤：

- [ ] 先写失败测试：节点可以提供非空 `display_number`；空字符串必须拒绝；旧包省略时仍可加载。
- [ ] 在 `RatingTreeExtensionNode` 和 `EffectiveRatingTreeNode` 中增加可选
      `display_number`。
- [ ] 定义 `RatingTreeSourceMapping`，包含来源分组/指标 ID、编号和目标节点稳定 ID。
- [ ] 解析 `source-index-map.json`，要求每条映射字段完整、目标是可选择的 `defect` 节点。
- [ ] 拒绝重复的 ID 对、重复的编号对、缺失目标和指向分组/占位节点的映射。
- [ ] 允许包不声明 `aliases.json` 和 `matching-rules.json`；不降低 `entry_files` 的路径和
      校验值保护。
- [ ] 将 `display_number` 和来源映射纳入包内容校验值。
- [ ] 更新 JSON Schema 的节点和来源映射定义；保持 `contract_version = 1`，因为新字段对旧包
      是向后兼容的可选扩展。

关键测试：

```text
9.1.2 + 9.1.1-1 → org.bridge.defect.9_1_2__9_1_1_1
9.1.2 + 9.1.2-1 → org.bridge.defect.9_1_2_1
相同来源编号对指向两个节点 → 加载失败
映射指向不可选择节点 → 加载失败
```

验证：

```powershell
cmake --build --preset vs-debug
.\build\vs-debug\Debug\bridge_report_backend_tests.exe --gtest_filter=RatingTreePackageLoaderTest.*
```

提交建议：

```text
feat(rating-tree): load explicit numbers and source mappings
```

---

## Task 3：编译并持久化显式编号和来源映射

**新增：**

- `database/migrations/024_rating_tree_display_number.sql`
- `database/tests/024_rating_tree_display_number_smoke.sql`

**修改：**

- `backend-cpp/src/rating_tree/RatingTreeCompiler.cpp`
- `backend-cpp/src/db/RatingTreeRepository.cpp`
- `backend-cpp/tests/test_rating_tree_compiler.cpp`
- `backend-cpp/tests/test_rating_tree_repository.cpp`
- `backend-cpp/src/http/RatingTreeRoutes.cpp`
- `backend-cpp/tests/test_rating_tree_routes.cpp`

步骤：

- [ ] 先写编译器失败测试，证明来源映射目标不存在、H21 引用不存在和
      `non_scoring + h21_indicator_id` 都会阻止编译。
- [ ] 编译器把扩展节点的 `display_number` 原样复制到有效节点。
- [ ] 将包级来源映射附着到目标有效节点，并纳入有效树规范化校验值。
- [ ] 迁移为 `rating_tree_nodes` 增加可空 `display_number text`，不得回填或修改已发布 1.x
      节点。
- [ ] 同步新树时写入 `display_number`；将来源映射写入
      `detail_json.source_mappings`。
- [ ] 从数据库装载有效树时恢复 `display_number` 和来源映射，保证重启后匹配行为不变。
- [ ] 节点列表、子节点、搜索、详情和路径 API 都返回 `display_number`；1.x 返回 `null`。
- [ ] 数据库冒烟测试验证新列约束、已发布节点不可修改和 1.x 空值兼容。

实现注意：

- 来源映射只写入 `detail_json`，不新增独立映射表。
- 路径元素也需要 `display_number`，否则详情页面包屑仍会回到推导编号。
- `canonical_tree()` 必须包含编号和映射，任何二者变化都应改变有效树校验值。

验证：

```powershell
cmake --build --preset vs-debug
.\build\vs-debug\Debug\bridge_report_backend_tests.exe --gtest_filter=RatingTreeCompilerTest.*:RatingTreeRepositoryTest.*:RatingTreeRoutesTest.*
.\scripts\dev\check-database.ps1
```

提交建议：

```text
feat(rating-tree): persist display numbers and source mappings
```

---

## Task 4：来源离线库输出原始分组和指标身份

**修改：**

- `tools-python/bridge_report_tools/importers/source_db/reader.py`
- `tools-python/bridge_report_tools/importers/source_db/defects.py`
- `tools-python/bridge_report_tools/importers/source_db/context.py`
- `tools-python/tests/importers/source_db/test_reader.py`
- `tools-python/tests/importers/source_db/test_defects.py`
- `tools-python/tests/importers/source_db/test_endpoint.py`
- `tools-python/tests/importers/source_db/test_baigu_regression.py`

步骤：

- [ ] 先写失败测试：来源记录中的 `judgeTreeId`、`judgeIndexId`、章节编号和指标编号四者
      原样进入契约候选。
- [ ] 将 `judgeTree(id, chapterNum, name)` 加入来源库必要结构检查，并增加按 ID 读取分组编号。
- [ ] 保留现有 `judgeIndex(id, tableNum, name)` 读取，不再把 `tableNum` 转成
      `h21.defect.*`。
- [ ] 删除 `defects.py` 中的 `h21_indicator_id()` 字符串换算和“找不到后退回文字匹配”注释。
- [ ] `source_defect_group_id` 写 `SourceDefect.judge_tree_id`；
      `source_defect_indicator_id` 写 `SourceDefect.judge_index_id`。
- [ ] 分组或指标目录项缺失时保留已有原始 ID、编号留空并添加明确 warning，不用名称猜编号。
- [ ] 验证源数据库仍以 `mode=ro` 打开，同一输入连续两次产出完全一致。

关键样例：

```text
outerCheckData.judgeTreeId = G-9.1.2
outerCheckData.judgeIndexId = I-9.1.1-1
→ group_id=G-9.1.2, indicator_id=I-9.1.1-1
→ group_number=9.1.2, indicator_number=9.1.1-1
```

验证：

```powershell
cd tools-python
uv run pytest tests/importers/source_db/test_reader.py tests/importers/source_db/test_defects.py tests/importers/source_db/test_endpoint.py tests/importers/source_db/test_baigu_regression.py
```

提交建议：

```text
feat(import): preserve exact source rating identities
```

---

## Task 5：把解析器收敛为来源精确匹配

**修改：**

- `backend-cpp/include/bridge_report/rating_tree/RatingTreeResolver.hpp`
- `backend-cpp/src/rating_tree/RatingTreeResolver.cpp`
- `backend-cpp/src/review/DefectRatingTreeMatching.cpp`
- `backend-cpp/src/review/DraftValidation.cpp`
- `backend-cpp/tests/test_rating_tree_resolver.cpp`
- `backend-cpp/tests/test_defect_rating_tree_matching.cpp`
- `backend-cpp/tests/test_draft_validation.cpp`
- `frontend/src/api/defectMatchingApi.ts`
- `frontend/src/api/defectMatchingApi.test.ts`

步骤：

- [ ] 先重写解析器测试，明确文字名称、别名、关键词和包含关系全部返回 `unmatched`。
- [ ] `RatingTreeMatchInput` 接收四个来源身份字段。
- [ ] 先按来源 ID 对精确查找；没有结果时才按编号对精确查找。
- [ ] 匹配前仍保留桥型和构件类别适用范围校验，来源对表不能绕过节点适用性。
- [ ] 精确命中评分节点时回填节点、H21 指标和允许标度。
- [ ] 精确命中 `non_scoring` 节点时回填节点，H21 指标保持 `null`，允许标度为空。
- [ ] 删除名称精确、别名、关键词、组合病害和模糊候选的运行时分支；没有结构化来源身份时直接
      返回 `unmatched`。
- [ ] ID 对和编号对都不存在时返回明确原因码，不允许继续走文字。
- [ ] 批量匹配、草稿保存复核和构件重绑重算都传递四个来源字段。
- [ ] 前端批量匹配请求补齐四个来源字段，不能只在草稿 JSON 中保存却不发送。
- [ ] 人工选择保护逻辑保持不变；用户手工选择后，普通文字修改不得覆盖节点。

关键测试：

```text
9.1.2 + 9.1.1-1 → 盖梁上下文的蜂窝、麻面
9.1.1 + 9.1.1-1 → 墩身上下文的蜂窝、麻面
9.1.2 + 9.1.2-2 → 可绑定、non_scoring
只有“盖梁裂缝” → unmatched
只有“裂缝” → unmatched
来源对表不存在 → unmatched，不回退文字
```

验证：

```powershell
cmake --build --preset vs-debug
.\build\vs-debug\Debug\bridge_report_backend_tests.exe --gtest_filter=RatingTreeResolverTest.*:DefectRatingTreeMatchingTest.*:DraftValidationTest.*
cd frontend
npm run test -- src/api/defectMatchingApi.test.ts
```

提交建议：

```text
feat(rating-tree): match defects only by exact source identity
```

---

## Task 6：建立脱敏快照和确定性生成器

**新增：**

- `tools-python/bridge_report_tools/rating_tree/__init__.py`
- `tools-python/bridge_report_tools/rating_tree/source_snapshot.py`
- `tools-python/bridge_report_tools/rating_tree/package_generator.py`
- `tools-python/tests/rating_tree/__init__.py`
- `tools-python/tests/rating_tree/test_source_snapshot.py`
- `tools-python/tests/rating_tree/test_package_generator.py`
- `standards/source-material/datacheck-bridge-tree/source-tree.json`
- `standards/source-material/datacheck-bridge-tree/source-metadata.json`
- `standards/source-material/datacheck-bridge-tree/corrections.json`
- `standards/source-material/datacheck-bridge-tree/group-scope-map.json`
- `standards/source-material/datacheck-bridge-tree/scoring-overrides.json`

步骤：

- [ ] 用内存 SQLite 先写失败测试，覆盖只读打开、字段白名单、章节过滤、排序和脱敏。
- [ ] 导出器只读取 `judgeTree`、`judgeIndex`、`judgeTree2Index` 的必要字段；
      `judgeStandard` 如需核查，只导出与映射判断直接相关且不含业务数据的摘要。
- [ ] 在 SQL 查询阶段只选择第 5–10 节，第 11–14 节不进入快照。
- [ ] 快照记录原数据库 SHA-256、导出时间、表行数和 403 组关系计数，但不记录本机绝对路径。
- [ ] 快照数组使用显式稳定顺序；不能依赖 SQLite 未声明的自然行顺序。
- [ ] 生成器严格应用 `corrections.json`。修正前原值不一致时失败，不静默套用。
- [ ] `group-scope-map.json` 显式声明来源分组的 H21 桥型和构件范围；叶节点继承父分组范围，
      不从中文名称猜构件类别。
- [ ] 常规评分关系通过真实 H21 指标目录验证；特殊参照关系和 `non_scoring` 决策放入
      `scoring-overrides.json`，不把业务例外写死在 Python 条件分支中。
- [ ] 生成器输出稳定的 `tree.json`、`source-index-map.json`、`corrections.json`、
      `sources.json` 和 `manifest.json`。
- [ ] Python 校验值算法与 C++ `RatingTreePackageLoader::calculate_checksum()` 完全一致；
      先用 1.0.3 包做跨语言校验值对照测试。
- [ ] 相同输入连续生成两次，逐字节比较全部文件相同。

修正清单首项：

```text
9.2.1 + 9.2.1-11
“墩身其它病害” → “台身其它病害”
impact = display_only
scoring_mode = non_scoring
```

验证：

```powershell
cd tools-python
uv run pytest tests/rating_tree
uv run python -m bridge_report_tools.rating_tree.package_generator --check
```

提交建议：

```text
feat(rating-tree): generate packages from a sanitized source snapshot
```

---

## Task 7：生成并审计 organization-bridge/2.0.0

**新增：**

- `standards/rating-tree/organization-bridge/2.0.0/manifest.json`
- `standards/rating-tree/organization-bridge/2.0.0/tree.json`
- `standards/rating-tree/organization-bridge/2.0.0/source-index-map.json`
- `standards/rating-tree/organization-bridge/2.0.0/corrections.json`
- `standards/rating-tree/organization-bridge/2.0.0/sources.json`

**修改：**

- `backend-cpp/tests/test_rating_tree_package_loader.cpp`
- `backend-cpp/tests/test_rating_tree_compiler.cpp`
- `backend-cpp/tests/test_rating_tree_repository.cpp`
- `standards/README.md`

步骤：

- [ ] 从已提交的脱敏快照生成 2.0.0，不手工编辑生成文件。
- [ ] 清单只声明五个入口文件，不包含 `aliases.json` 或 `matching-rules.json`。
- [ ] H21 来源锁定 `jtg-t-h21-2011/1.0.3`，养护来源继续锁定
      `jtg-5120-2021/1.0.0`。
- [ ] 集成测试验证包校验值、403 条来源关系、所有目标节点存在且可选择。
- [ ] 验证树中没有第 11–14 节节点或来源映射。
- [ ] 验证所有 `inherit_h21/reference_h21` 指标真实存在于 H21 1.0.3。
- [ ] 验证所有无 H21 依据的扩展节点为 `non_scoring` 且没有 H21 引用。
- [ ] 核对 `9.1.2`、`9.2.2` 和其他复用分组的编号、顺序和目标节点。
- [ ] 更新 `standards/README.md`，记录重新生成、校验和发布规则。

必须通过的断言：

```text
source-index-map.json 关系数 = 403
9.1.2 下 9.1.1-1 显示不变
9.1.2-1 → h21.defect.9_1_2
9.1.2-2 → non_scoring
2.0.0 aliases.size() = 0
2.0.0 keyword_rules.size() = 0
```

验证：

```powershell
cmake --build --preset vs-debug
.\build\vs-debug\Debug\bridge_report_backend_tests.exe --gtest_filter=RatingTreePackageLoaderIntegrationTest.*:RatingTreeCompilerIntegrationTest.*:RatingTreeRepositoryIntegrationTest.*
```

提交建议：

```text
feat(rating-tree): publish source-faithful bridge tree 2.0.0
```

---

## Task 8：前端使用显式编号并默认选择 2.0.0

**修改：**

- `frontend/src/api/ratingTreeApi.ts`
- `frontend/src/api/workspaceApi.ts`
- `frontend/src/rating-tree/ratingTreeLabels.ts`
- `frontend/src/rating-tree/ratingTreeLabels.test.ts`
- `frontend/src/rating-tree/RatingTreeNavigator.tsx`
- `frontend/src/rating-tree/RatingTreeNodeDetail.tsx`
- `frontend/src/pages/RatingTreePage.test.tsx`
- `frontend/src/review/components/DefectDetailEditor.tsx`
- `frontend/src/review/components/DefectsSection.tsx`
- `frontend/src/review/components/DefectsSection.test.tsx`
- `frontend/src/workspace/CreateInspectionDialog.tsx`
- `frontend/src/workspace/CreateInspectionDialog.test.tsx`
- `backend-cpp/src/http/RatingTreeRoutes.cpp`
- `backend-cpp/tests/test_rating_tree_routes.cpp`

步骤：

- [ ] 前端 API 类型为节点和路径元素增加 `display_number: string | null`。
- [ ] `ratingTreeSectionNumber()` 优先返回显式编号；只有 1.x 的空值节点才执行旧 key 推导。
- [ ] 2.0.0 的 `9.1.2-1` 不再依赖显示覆盖；保留覆盖仅作为旧 1.x 回退兼容。
- [ ] 导航树、节点详情、面包屑和病害手工选择器统一使用“编号 + 名称”。
- [ ] 列表键和保存值继续使用节点 UUID/稳定 ID，绝不使用显示编号。
- [ ] 版本列表 API 将同一 `tree_code` 下最高语义版本标记为 `is_default`，并把默认版本排在前面。
- [ ] 创建年度时即使存在多个已发布版本，也自动预选 `is_default = true` 的 2.0.0；用户仍可
      显式选择旧版本。
- [ ] 评定树首页无版本参数时进入默认版本，而不是依赖数据库返回的偶然插入顺序。
- [ ] 更新前端测试，覆盖同一编号出现在不同父节点、长名称、显式编号和旧包回退。

关键界面断言：

```text
9.1.2 盖梁和系梁
├─ 9.1.1-1 蜂窝、麻面
├─ 9.1.1-2 剥落、露筋
├─ 9.1.1-3 空洞、孔洞
├─ 9.1.1-4 钢筋锈蚀
├─ 9.1.1-5 混凝土碳化、腐蚀
├─ 9.1.2-1 裂缝
└─ 9.1.2-2 盖梁和系梁病害
```

验证：

```powershell
cd frontend
npm run test -- src/rating-tree/ratingTreeLabels.test.ts src/pages/RatingTreePage.test.tsx src/workspace/CreateInspectionDialog.test.tsx src/review/components/DefectsSection.test.tsx
npm run build
```

提交建议：

```text
feat(rating-tree): display source numbers and default to version 2
```

---

## Task 9：端到端来源导入和评分验证

**修改：**

- `tools-python/tests/importers/source_db/test_baigu_regression.py`
- `backend-cpp/tests/test_word_import_routes.cpp`
- `backend-cpp/tests/test_defect_matching_routes.cpp`
- `backend-cpp/tests/test_assessment_service.cpp`
- 根据测试夹具需要更新 `frontend/src/review/testFixtures.ts`

步骤：

- [ ] 用固定来源快照/脱敏夹具导入包含直接 H21、上下文复用、参照 H21、其它病害四类记录。
- [ ] 验证来源 ID 对优先；人为改成未知 ID 后能用唯一编号对回退。
- [ ] 验证未知 ID 和未知编号进入无法识别，不根据病害文字绑定。
- [ ] 验证 `9.1.2 + 9.1.1-1` 落到盖梁上下文节点，而不是墩身节点。
- [ ] 验证 `9.1.2-1` 使用 H21 表 9.1.2 标度和扣分。
- [ ] 验证 `9.1.2-2` 能保存、显示和进入报告数据，但不产生扣分。
- [ ] 验证来源软件的 DP、score、grade 和 weight 字段不进入本系统评分输入。
- [ ] 验证构件重新绑定后按同一来源身份重算，结果稳定。
- [ ] Word 导入只有文字时保持未识别，允许用户手工选择节点。

完成条件：

- 第 5–10 节来源关系映射覆盖率为 100%；
- 精确来源样例无错绑；
- 纯文字样例自动绑定数为 0；
- H21 评分回归通过；
- `non_scoring` 病害不改变构件扣分结果。

验证：

```powershell
cd tools-python
uv run pytest tests/importers/source_db/test_baigu_regression.py
cd ..
cmake --build --preset vs-debug
.\build\vs-debug\Debug\bridge_report_backend_tests.exe --gtest_filter=*SourceDb*:*DefectMatching*:*Assessment*
```

提交建议：

```text
test(rating-tree): cover exact source import and H21 scoring
```

---

## Task 10：全量回归、数据库重建和视觉验收

**修改：**

- 只修复本任务引入的失败；不得顺手整理无关模块。

步骤：

- [ ] 对开发数据库执行迁移 024；确认后端启动后同步并发布 2.0.0。
- [ ] 因现有均为测试数据，清理或重建测试年度，使用默认 2.0.0 重新导入。
- [ ] 运行 Python、后端、数据库和前端全量测试。
- [ ] 连续运行生成器两次，确认工作区无生成差异。
- [ ] 启动后端和前端，使用 Playwright 检查评定树桌面与移动视口。
- [ ] 截图核对第 5–10 节，以及 `9.1.2`、`9.2.2` 的编号、顺序、展开和长文本布局。
- [ ] 检查浏览器控制台无错误，节点文本不重叠，展开控件不导致布局跳动。
- [ ] 通过 API 抽查默认版本、节点详情、来源精确匹配和 `non_scoring` 响应。
- [ ] 最后运行 `git diff --check`，只提交本计划范围内文件。

验证：

```powershell
cd tools-python
uv run pytest
cd ../frontend
npm run test
npm run build
cd ..
cmake --build --preset vs-debug
.\build\vs-debug\Debug\bridge_report_backend_tests.exe
.\scripts\dev\check-database.ps1
git diff --check
```

完成条件：

- 后端启动无评定树加载、编译或数据库同步错误；
- API 将 2.0.0 标记为默认版本；
- 403 条来源关系全部可定位到唯一内部节点；
- 页面显示编号与来源软件一致，修正项与 `corrections.json` 一致；
- 文字自动匹配为零，人工选择仍可用；
- H21 评分与本任务前的规范计算结果一致；
- 工作区原有无关改动未被回退或带入本任务提交。

提交建议：

```text
test(rating-tree): verify bridge tree 2.0 end to end
```

## 实施顺序摘要

```text
共享来源身份契约
→ 包模型和来源映射加载
→ 有效树持久化与 API
→ 来源导入输出原始身份
→ 解析器只做精确对表
→ 脱敏快照和确定性生成器
→ 发布 2.0.0
→ 前端显式编号和默认版本
→ 端到端导入、评分与视觉验收
```

每个任务完成后只运行相关快速测试；Task 10 再执行全量回归。任何来源关系无法确定 H21 依据时，
优先标为 `non_scoring` 并列入核查，不允许为了通过测试而猜测评分映射。
