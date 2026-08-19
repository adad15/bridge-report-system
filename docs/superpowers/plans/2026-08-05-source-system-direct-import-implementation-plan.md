# 实施计划：从来源软件的离线库直接导入

设计文档：[2026-08-05-source-system-direct-import-design.md](../specs/2026-08-05-source-system-direct-import-design.md)
日期：2026-08-05

## 前置约束

- **不改契约**：`BridgeAnnualInspectionData` 一个字段都不动，`source_type` 用已有的
  `接口同步`。
- **不动 Word 解析链**：`importers/` 下 docx 相关文件、C++ 的 Word 上传与仓储、
  前端的 Word 导入对话框，本计划一行不改。既有测试一条不改、必须全绿。
- **不删任何代码。**
- **离线库只读**：一律 `file:...?mode=ro` 打开。**不能用"文件修改时间未变"来验证**——
  来源软件自己在运行时会持续写入该库（实测 10:32 与 12:18 各变一次），修改时间变化
  不代表我们写了。改用两条可验证的约束：①单元测试断言连接 URI 含 `mode=ro`；
  ②导入前后各取一次快照做 SHA-256 对比，不一致时只作提示不作失败（因为可能是对方
  程序写的），并在日志中记录。
- **不接管台账**：源数据只用于认领构件、挂载病害。
- 不导入扣分、得分、等级、权重；不做跨年度对比与报告正文。

---

## Task 0：记录基线

- [ ] 复制一份离线库到工作目录作为固定快照，记录路径、体积、SHA-256 与 `tasks` 行数。
      **后续所有任务一律对着这份快照跑**，不直接读活动库——活动库随对方程序变动，
      会让"反复导入结果一致"这条验收失去意义。
- [ ] 记录三套测试基线：后端、前端、Python。
- [ ] 导出现有 2024 年度那 361 条病害与 166 张照片候选为 JSON，作为对账基准。

验证：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/dev/check-backend-tests.ps1
```

提交：无。

---

## Task 1：源库读取

**新增：**

- `tools-python/bridge_report_tools/importers/source_db/__init__.py`
- `tools-python/bridge_report_tools/importers/source_db/reader.py`
- `tools-python/tests/importers/source_db/test_reader.py`

步骤：

- [ ] 先写失败测试：用内存 SQLite 造五张表的最小样本，断言读取结果的字段、数量与排序稳定。
- [ ] 实现 `open_source_db(path)`：只读打开，缺表或缺列时抛出带明确 code 的错误，
      **不得静默返回残缺数据**（设计 §11.1）。
- [ ] 实现 `load_task(db, task_id)`：任务元信息（桥名、检测日期）。
- [ ] 实现 `load_component_tree(db, task_id)`：`taskTrees`，含层级码、构件类型、父级关系。
- [ ] 实现 `load_defects(db, task_id)`：`outerCheckData` 全字段，含 `judgeIndexId`、
      `degree`、各尺寸列与单位列、`treeId`。
- [ ] 实现 `load_photos(db, task_id)`：`images`，**不读 `fileName` 本体**，只返回 id 与
      元信息；base64 留到 Task 3 按需取，避免把 42 MB 读进内存。
- [ ] 排序固定，保证同一份库反复读取结果一致。

完成条件：

- 对快照读取百股大桥 2024（`4ec7bd71`）得到 278 个台账节点（其中具体构件 259 个）、
  279 条病害、166 张挂在病害上的照片；
- 连续两次读取结果 diff 为空；
- 单元测试断言连接使用了 `mode=ro`。

提交建议：

```text
feat(import): read inspection data from the source offline database
```

---

## Task 2：组装病害与台账认领

**新增：**

- `tools-python/bridge_report_tools/importers/source_db/defects.py`
- `tools-python/tests/importers/source_db/test_defects.py`

步骤：

- [ ] 先写失败测试，至少覆盖：尺寸列拼成 `measurements`；`degree` 落到 `defect_scale`；
      `judgeIndexId` 落到指标；病害类型为空仍能产出合法候选；
      **`component_name` 取不到时按回退链产出非空值**（设计 §4）。
- [ ] 实现构件认领：`treeId` → `taskTrees` → 构件编号与父级类别名。
- [ ] 实现 `component_name` 回退链：父级名 → `memberTypeName` → 上级部位名；
      全空则该条带警告，**不得让整次导入失败**。
- [ ] 实现指标换算：源指标编号 → `h21.defect.*`，填进契约已有的
      `standard_defect_indicator_id`。**H21 里没有的编号一律留空，不硬凑。**

      不需要对表，也不需要动评定树。实测三年百股 974 条：441 条的指标在梁式桥作用域下
      唯一可定；514 条是「渗水泛碱」，现有别名表本来就认得（正是当前 184 条能匹配上的
      原因）；仅 3 条落在「碳化 vs 水损」这个同作用域二选一上，描述里有「渗水」「水蚀」
      足以区分；其余 16 条走现有人工流程。合计 98% 可自动定好。

      **评定树不能改成"给水损一个自己的 H21 编号"**：编译器强制要求参与评分的节点
      引用的 H21 指标必须真实存在（`rating_tree_h21_indicator_missing`），而 H21 的
      5.1.1 只有 12 项；且水损的标度与扣分表正是从碳化那条指标取的，改了就没标度可选。
- [ ] 实现尺寸组装：数量/长度/宽度/高度/面积一 + 各自单位列 → `measurements`；
      来源结构化列优先，并用 `importers/measurements.py` 从病害描述补齐缺失维度。
- [ ] 位置由 `pos` / `posStake` / `posPart1..5` 组装。
- [ ] 范围写法的构件编号原样保留，拆分交给 C++ 现有 `ComponentRangeParser`。

完成条件：

- 2024 那份数据产出 279 条病害候选；
- `standard_defect_indicator_id` 的填充率与"H21 有该指标"的条数一致，不多不少；
- 无 `component_name` 为空的候选；
- 与现有 361 条的对账可解释（设计 §10.2）。

提交建议：

```text
feat(import): assemble defect candidates from source records
```

---

## Task 3：照片

**新增：**

- `tools-python/bridge_report_tools/importers/source_db/photos.py`
- `tools-python/tests/importers/source_db/test_photos.py`

步骤：

- [ ] 先写失败测试：编号按部位分段连续；题注按规则拼出；类型为空时退回用描述；
      题注重复不加序号；base64 解码后落盘且文件可读。
- [ ] 实现编号生成：按台账层级码首段分组，映射表**可配置**（`001→2.1` 等），
      组内按层级码与病害顺序从 1 连续编号。
- [ ] 实现题注生成：`构件编号 + 空格 + 病害类型`，类型为空时用描述。
- [ ] 实现 base64 解码落盘到临时照片目录，逐张流式处理，不一次性读入全部。
- [ ] 产出 `photo_references`，使照片与病害的关联在契约层面与 Word 路一致。
- [ ] 范围行的照片挂到拆分后第一个构件，并带警告标记待确认。

完成条件：

- 2024 那份产出 166 张照片候选，每个部位段内编号连续无断号；
- 题注全部非空；
- 临时目录中的文件数与候选数一致，抽查可正常打开。

提交建议：

```text
feat(import): generate photo numbers and captions for source imports
```

---

## Task 4：解析端点

**新增：**

- `tools-python/bridge_report_tools/importers/source_db/context.py`
- `tools-python/tests/importers/source_db/test_endpoint.py`

**修改：**

- `tools-python/bridge_report_tools/main.py`

步骤：

- [ ] 先写失败测试：端点返回 `{data, temporary_photo_files}`；`source_type` 为
      `接口同步`；缺表时返回带 code 的 400；taskId 不存在时返回可操作的错误信息。
- [ ] 定义 `SourceImportRequest`：离线库路径、taskId、临时照片目录，以及与
      `WordImportRequest` 相同的那套导入上下文字段。
- [ ] 复用 `WordImportResponse` 的响应形状，**不新建响应类型**。
- [ ] 注册 `POST /imports/source/parse`。
- [ ] 产出的契约通过 Python 侧严格校验。

完成条件：

- 端点对真实库产出可通过契约校验的完整数据；
- 同一输入反复调用，产出 JSON 逐字节一致。

提交建议：

```text
feat(import): add the source database parse endpoint
```

---

## Task 5：后端与前端接线

**修改：**

- `backend-cpp/src/http/WordImportRoutes.cpp`（或新增来源分支所在文件）
- `backend-cpp/src/db/WordImportRepository.cpp`（仅调用分支，不改写入逻辑）
- `frontend/src/workspace/ImportWordDialog.tsx`

步骤：

- [ ] 先写失败测试：来源为源库时调用新端点；为 Word 时行为与现在**完全一致**。
- [ ] 匹配器增加一条兜底路径：现有文字匹配（精确名/别名/关键词/片段）全部落空时，
      才用契约里的 `standard_defect_indicator_id` 按"指标 + 桥型 + 构件类别"选节点；
      该组合在树中不唯一时不自动定，落候选交人工。**文字匹配优先级不变。**
- [x] ~~`ImportBindingRepository` 重绑时不再清空 `standard_defect_indicator_id`~~
      **这一步做不到，已改方案。** 那个字段是派生量：`DraftValidation`、
      `DefectRatingTreeMatching`、`ImportBindingRepository` 三处都会在重算前清空它，
      数据库触发器（019）还要求它必须等于所选节点的 `h21_indicator_id`。只在其中一处
      不清空，第一次保存草稿仍会被抹掉。改为**新增契约字段
      `defects[].source_defect_indicator_id`**：只有导入方写一次，任何派生逻辑都不碰。
- [x] C++ 侧按导入来源选择端点与请求体，响应处理、照片归档、`parsed_result_json`
      写入路径**不变**。来源判定取自导入记录的 `source_type == 接口同步`。
- [x] 离线库**不上传**（见设计 §6.1）：新增
      `POST /api/inspection-years/{id}/import-records/source` 登记一份 `.srcref`
      引用文件（路径 + taskId），迁移 023 放行该扩展名。
- [x] 前端导入对话框增加来源选项（默认走来源软件），选择源库时要求填路径与 taskId。
- [x] 界面明确提示前置操作：**必须先在桌面程序里打开该桥**（设计 §11.2）。

完成条件：

- Word 导入的既有测试一条不改、全部通过；
- 源库导入可端到端跑通并写入 `parsed_result_json`。

提交建议：

```text
feat(import): let import records choose their data source
```

---

## Task 6：三年度验收与对账

**新增：**

- `tools-python/tests/importers/source_db/test_baigu_regression.py`（或一次性验证脚本）

步骤：

- [x] 对百股大桥三个年度（2024 / 2025 / 2026）各跑一次导入，只读产出，不写库。
- [x] 2024 那份与 Task 0 的基准对账，逐条列出差异并解释（设计 §10.2）。
- [x] 统计三个年度的指标填充率、`component_name` 空值数、照片编号连续性。
- [x] 记录导入耗时与临时目录体积。

**实测结果**（快照 SHA-256 `b9e3c828…c5b`，跑前跑后一致）：

| | 2024 | 2025 | 2026 |
| --- | ---: | ---: | ---: |
| 病害 / 照片 | 279 / 166 | 314 / 253 | 381 / 455 |
| 指标填充 | 100% | 100% | 100% |
| `component_name` 为空 | 0 | 0 | 0 |
| 构件找不到 / 走回退 | 0 / 0 | 0 / 0 | 0 / 0 |
| 照片编号 | 2.1 1~78、2.2 1~21、2.3 1~67 | 2.1 1~105、2.2 1~76、2.3 1~72 | 2.1 1~216、2.2 1~106、2.3 1~133 |
| 空题注 | 0 | 0 | 0 |
| 耗时 / 临时目录 | 0.4s / 30.6 MB | 0.7s / 71.6 MB | 1.4s / 125.7 MB |
| 两次产出逐字节一致 | 是 | 是 | 是 |

2024 与 Word 基线（361 病害 / 166 照片）对账：

- **总数完全对上。** 单构件 264 条 + 15 条范围行展开 97 个 = 361。
- **构件编号零差异。** 336 个编号，只在 Word 出现 0 个、只在来源库出现 0 个、
  同一编号条数不同 0 个。
- 按展开后同一口径的字段填充率：指标 51.2% → **100%**；标度 93.1% → 93.1%；
  位置 47.4% → 47.4%（此前看到的 44.1% 只是范围行未展开导致的分母差异）。
- **唯一的负向差异：结构化尺寸 85.6% → 81.2%，共 16 条，单向**（没有一条是反过来的）。
  原因单一且已查实：检测员把范围值写进了自由文本（`渗水泛碱,L=15至20m`、
  `长度范围：0.5~4.0m`），来源软件的结构化尺寸列只填得了单值，所以留空；
  Word 的正则反而能从文字里抠出这个范围。**数据没丢**——`defect_description`
  原样带着那句话。后续已在来源结构化列优先的前提下，用 `measurements.py` 从描述补齐
  缺失维度；同维度冲突时保留来源结构化值并提示人工复核。

固化为 `tools-python/tests/importers/source_db/test_baigu_regression.py`：
设了 `BRIDGE_REPORT_SOURCE_DB_SNAPSHOT` 才跑（17 passed），没设则整体跳过。

完成条件：

- 三个年度均无契约校验失败；
- 指标填充率均 ≥ 99%；
- 2024 的差异全部可解释，无法解释的差异一条都不允许。

提交建议：

```text
test(import): verify source imports against three years of Baigu bridge
```

---

## Task 7：回归

- [ ] `scripts/dev/check-backend-tests.ps1` 全绿，迁移各跑两遍。
- [ ] Python 测试全绿。
- [ ] 前端 `tsc -b` + `vitest` + `vite build` 全绿。
- [ ] 快照文件 SHA-256 自 Task 0 起未变（证明我们没写它）。
- [ ] 人工：用源库导入建一条**测试用**导入记录，走完校对与确认入库，确认评分能算出来。
      **不要动 2024 那条正在校对的记录。**

提交：无。

---

## 最终验收清单

1. 同一份库、同一个 taskId 反复导入，产出逐字节一致；
2. 三个年度导入无契约校验失败，指标填充率均 ≥ 99%；
3. 2024 与现有 Word 导入结果对账全部可解释；
4. 照片编号分段连续、题注非空；
5. 快照文件 SHA-256 全程未变，且连接一律 `mode=ro`；
6. Word 导入路径的既有测试一条未改且全绿；
7. 后端、Python、前端三套测试全绿。

## 上线顺序提醒

设计 §12 记了清理清单，但**本期一行不删**。数据源切换稳定跑过一个真实年度、确认
入库无误之后，再单独评估 Word 解析链与病害匹配的去留。在那之前两条路并存。
