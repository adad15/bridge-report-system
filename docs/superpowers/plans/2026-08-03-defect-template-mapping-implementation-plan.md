# 实施计划：用来源软件的病害描述模板建立受控映射

设计文档：[2026-08-03-defect-template-mapping-design.md](../specs/2026-08-03-defect-template-mapping-design.md)
日期：2026-08-03

## 前置约束

- 不改匹配算法，不删任何现有匹配层。本计划只产出数据。
- 已发布的规则包不可改写：新别名必须落在新的包版本 `organization-bridge/1.0.4`。
- 不迁移历史数据，不改写任何已确认的病害。
- 离线库只读打开，任何步骤都不写回来源软件的数据。

---

## Task 0：记录基线

- [ ] 记录当前可自动定节点的条数（现为 361 条中 185 条），保存查询与结果。
- [ ] 记录后端测试数、前端测试文件数与用例数，作为回归对照。
- [ ] 确认离线库路径与快照时间，把该文件复制到工作目录只读使用，不原地打开。

验证：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/dev/check-backend-tests.ps1
```

提交：无。

---

## Task 1：导出脚本

**新增：**

- `tools-python/bridge_report_tools/standards/__init__.py`
- `tools-python/bridge_report_tools/standards/defect_template_source.py`
- `tools-python/tests/standards/test_defect_template_source.py`

步骤：

- [ ] 先写失败测试：用一个内存 SQLite 造出五张表的最小样本，断言导出结果的三元组
      数量、字段与排序稳定。
- [ ] 实现 `load_source_triples(db_path)`：只读打开（`file:...?mode=ro`），三表联查
      去重，返回带 `index_id` 的有序列表。**必须按 `judgeIndex.id` 取**——`tableNum`
      在库里会重复（`5.1.1-13` 是两个不同指标，`1.3.1` 重复 24 次），按编号取会串行。
- [ ] 实现 `classify(triples, h21_indicators)`：分成三档——a 编号对上 H21 且分组
      属于定检章节、
      b 同章节但超出 H21 编号（单位扩展项）、c 属于别的标准。返回三组与统计。
      **b 档不得并入 c 档丢弃**，它包含水损这类 H21 没有、评定树自己加的指标。
- [ ] 输出必须按固定键排序，保证同一份库反复导出字节一致。

完成条件：

- 对真实离线库导出得到 808 条三元组，分档为 a 362 / b 12 / c 434；
- 连续两次导出结果 diff 为空。

提交建议：

```text
feat(standards): export defect templates from the source offline database
```

---

## Task 2：源构件分组对表（人工）

**新增：**

- `standards/rating-tree/organization-bridge/1.0.4/source-component-map.json`
- `standards/rating-tree/organization-bridge/1.0.4/source-index-map.json`

步骤：

- [ ] 先填 7 行的指标对表 `source-index-map.json`：b 档那 7 个单位扩展指标
      （`5.1.1-13` 水损、`9.1.1-10` / `9.2.1-10` 水损害、`8.6.1` 减震装置，
      以及三个"其它病害"）→ 评定树节点 id。填不出的显式留空并注明。
- [ ] 由 Task 1 输出 a 档涉及的 45 个源分组清单，生成待填模板（每行含源分组编号、
      名称、留空的 H21 构件类别数组）。
- [ ] 人工填写每个分组对应的 H21 构件类别（可多个）。填不出的留空并注明原因。
- [ ] 脚本校验：每个填入的构件类别都必须存在于 H21 `component-taxonomy.json`；
      不存在则报错退出。
- [ ] 未填写的分组不报错，但在生成阶段整组跳过并计入报告。

完成条件：

- 45 行构件对表与 7 行指标对表全部有明确结论（填写或显式留空 + 原因）；
- 校验脚本零错误。

提交建议：

```text
feat(standards): map source component groups to H21 categories
```

---

## Task 3：别名生成器

**新增：**

- `tools-python/bridge_report_tools/standards/defect_template_aliases.py`
- `tools-python/tests/standards/test_defect_template_aliases.py`

步骤：

- [ ] 先写失败测试，至少覆盖：唯一映射生成一条别名；同作用域多指向被降级为候选规则
      且不进别名表；源分组未对表时整组跳过；H21 指标在评定树里没有节点时跳过并报告。
- [ ] 实现生成：对每条三元组，用构件对表得到构件类别集合；a 档用指标编号反查评定树
      节点，b 档用指标对表直接取节点 id；
      与节点自带的 `bridge_type_ids` × `component_category_ids` 取交集后产出条目。
- [ ] 按 `(模板, 桥型, 构件类别)` 归并；命中多个节点的一律不写别名，改写候选规则。
- [ ] 产出三份文件：`aliases.json` 增量、候选规则增量、以及一份人类可读的生成报告
      （每类跳过的条数与原因、冲突清单）。

完成条件：

- 冲突项零漏报，别名表中不存在任何一个作用域指向多个节点的条目；
- 生成报告能逐条解释 808 → 最终条数之间的每一次减少。

提交建议：

```text
feat(standards): generate controlled aliases from defect templates
```

---

## Task 4：发布 1.0.4 规则包

**新增：**

- `standards/rating-tree/organization-bridge/1.0.4/`（manifest / tree / aliases /
  matching-rules / sources / source-component-map / source-index-map）

步骤：

- [ ] 从 1.0.3 复制 `tree.json` 与 `sources.json`，节点不变。
- [ ] 合并 Task 3 的别名增量到 `aliases.json`，合并候选规则增量到 `matching-rules.json`。
- [ ] `manifest.json` 的 `package_version` 改为 `1.0.4`，`entry_files` 加入
      `source-component-map.json` 与 `source-index-map.json`。
- [ ] 计算 `content_checksum`：清空该字段后按后端同一套规范化 JSON 规则算摘要，
      或先填占位启动后端、从校验失败日志里取实际摘要回填。
- [ ] 后端启动后确认包同步成功、无 checksum 冲突、`/health/db` 的
      `rating_tree_failed_count` 为 0。

完成条件：

- 1.0.3 的文件一个字节都没被改动；
- 1.0.4 装载成功且 `rating_tree_published_count` 增加 1。

提交建议：

```text
feat(standards): publish rating tree package 1.0.4 with template aliases
```

---

## Task 5：离线验证收益

**新增：**

- `tools-python/tests/standards/test_defect_template_coverage.py`（或一次性验证脚本）

步骤：

- [ ] 取 2024 年度那份草稿的 361 条病害（只读导出为 JSON，不动数据库）。
- [ ] 用 1.0.4 的别名表离线跑一遍匹配判定，统计：可自动定节点、落候选、仍无匹配三档。
- [ ] 与 Task 0 的基线对比，逐类列出变化。
- [ ] 抽查设计文档 §9 点名的四组：33 条剥落掉角、10 条桥面破损、2 条桥台水损害应
      自动定上；26 条横向裂缝与 30 条墩身水损应落候选。

完成条件：

- 自动定节点条数较基线明确上升，且上升的每一条都能追溯到具体的别名条目；
- 没有任何一条从"已定"变成"未定"。

提交建议：

```text
test(standards): verify template alias coverage against the 2024 draft
```

---

## Task 6：回归与验收

- [ ] `scripts/dev/check-backend-tests.ps1` 全绿，迁移各跑两遍。
- [ ] Python 测试全绿。
- [ ] 前端 `tsc -b` + `vitest` + `vite build` 全绿（本期不改前端，用于确认无连带影响）。
- [ ] 后端启动无 checksum 冲突、无规则包装载失败。
- [ ] 人工：在构件绑定页把一个**测试用**年度绑定到 1.0.4，确认匹配结果符合 Task 5 的
      离线预测。**不要**先动 2024 那个正在校对的年度。

验证：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/dev/check-backend-tests.ps1
```

提交：无。

---

## 最终验收清单

1. 808 条三元组可复跑导出，两次结果一致；
2. 45 行构件对表与 7 行指标对表全部有结论，且构件类别都通过 H21 分类校验；
3. 别名表中不存在作用域冲突的条目，冲突全部降级为候选；
4. 1.0.3 未被改动，1.0.4 装载成功；
5. 2024 年度草稿离线验证显示自动定节点条数上升，且无一条由已定变未定；
6. 后端、Python、前端三套测试全绿。

## 上线顺序提醒

设计文档 §7 已写明：年度要用上新别名必须重新绑定，而重绑会重算该年度全部病害匹配。
2024 年度目前校对到一半，**建议等这一轮校对确认入库之后再升级**，或者先在测试年度上
验证。这一步由用户决定，不在本计划的自动执行范围内。
