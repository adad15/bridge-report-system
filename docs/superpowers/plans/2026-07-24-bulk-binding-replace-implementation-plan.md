# 构件绑定 · 批量查找替换 实施计划

> **For agentic workers:** 逐任务实施；每步 `- [ ]` 勾选。先写失败测试 → 跑到失败 → 最小实现 → 跑到通过 → 提交。

**Goal:** 实现 spec [2026-07-24-bulk-binding-replace-design.md](../specs/2026-07-24-bulk-binding-replace-design.md)：在构件绑定的分组表头提供「批量替换」，用一条带数字通配的查找替换规则，把整组因写法差异而未匹配的行（如报告"第32孔桥面" vs 台账"32#跨桥面铺装"）一次绑定完成。应用前给出可核对的预览。

**Architecture:** 不新增表、不改数据形态。替换**只用于查找台账目标**，不改写报告原文（`component_number` 保真有断言锁定）；命中后写入的仍是 `bridge_component_id`，与手工绑定完全一致，故撤销沿用既有的行内"取消绑定"。预览所需数据（台账 `entries`、分组行）在绑定页已全部加载，**预览纯前端计算，不新增查询接口**。应用新增一个批量端点，在 `ImportBindingRepository` 既有的"`待校对` 相读改 `parsed_result_json`"路径里一次处理全部目标，任一非法则整批不写。

**Tech Stack:** 后端 C++20 / Drogon / GoogleTest（`backend-cpp/src/db/ImportBindingRepository.*`、`src/http/ImportBindingRoutes.cpp`）；前端 React + TS + Vitest（`frontend/src/review/binding/*`、`frontend/src/api/importBindingApi.ts`）。

**真值来源：** spec §3（通配语义）、§4（匹配与应用语义）、§5（接口）、§6（错误处理）。

**决策点（spec 已定，实施时不得改变）：**
1. `*` 匹配一段连续数字（`\d+`，至少一位）；其余字符按字面处理，**不引入正则**。
2. 替换串第 k 个 `*` 取查找串第 k 段数字；替换 `*` 个数可少于查找，**不得多于**（多于即非法模式）。
3. 模式匹配作用于**报告原文**，不预先归一化——异常空白应在预览里显形为"不符合查找模式"，不得静默抹平。
4. 仅 `unmatched` / `ambiguous` 行参与；已绑定、已标记缺失的行不参与也不受影响。
5. 查台账**不限定部件类别**，只按 `normalize_component_number` 归一化后的编号比对；多命中判歧义并跳过（宁可跳过让人工处理，不可绑错）。
6. 批量端点**原子**：任一目标非法则整批不写。

---

## 文件结构

- 新增 `frontend/src/review/binding/replacePattern.ts`：通配模式的编译与应用（纯函数，无 React 依赖，便于单测）。
- 新增 `frontend/src/review/binding/BulkReplaceDialog.tsx`：查找/替换输入 + 预览表 + 应用。
- 修改 `frontend/src/review/binding/ComponentBindingWorkspace.tsx`：分组表头加入口，接批量绑定结果。
- 修改 `frontend/src/api/importBindingApi.ts`：新增 `bindComponentsBatch`。
- 修改 `backend-cpp/include/bridge_report/db/ImportBindingRepository.hpp` / `src/db/ImportBindingRepository.cpp`：新增 `bind_components_batch`。
- 修改 `backend-cpp/src/http/ImportBindingRoutes.cpp`：新增 `POST …/component-binding/bind-batch`。
- 测试：`frontend/src/review/binding/replacePattern.test.ts`、`BulkReplaceDialog.test.tsx`、`backend-cpp/tests/test_import_binding_repository.cpp`（批量与原子性）。

---

## Task 1：通配模式引擎（前端纯函数）

- [ ] 写失败测试 `replacePattern.test.ts`：
  - 单 `*`：`第*孔桥面` + `*#跨桥面铺装` 作用于 `第32孔桥面` → `32#跨桥面铺装`
  - 多 `*` 按位置对应：`第*孔第*片板` + `*-*#板` 作用于 `第3孔第5片板` → `3-5#板`
  - 替换 `*` 少于查找：`第*孔第*片板` + `*#跨` → `3#跨`（取第 1 段）
  - 字面转义：查找 `1.1#板` **不得**把 `.` 当通配，`1x1#板` 不应命中
  - `*` 需至少一位数字：`第孔桥面` 不命中 `第*孔桥面`
  - 非法模式：替换 `*` 多于查找 → 返回错误而非抛异常
  - 不命中时返回"未命中"而非空串
- [ ] 跑到失败
- [ ] 实现 `compilePattern(find, replace)` → `{ ok: true, apply(text): string | null } | { ok: false, error }`；内部把查找串按 `*` 切段、各段用 `escapeRegExp` 转义后以 `(\d+)` 连接
- [ ] 跑到通过 → 提交

## Task 2：预览计算（前端纯函数）

- [ ] 写失败测试（同文件）：给定行集合 + 台账 entries + 模式，产出四种判定
  - 不符合查找模式 / 恰好 1 个命中 / 0 个命中 / ≥2 个命中
  - 已绑定、已标记缺失的行**不出现在结果中**
  - 命中比对走归一化（全角 `＃`、多余空格、大小写应能对上）
- [ ] 跑到失败
- [ ] 新增 `frontend/src/review/binding/normalizeComponentNumber.ts`，逐条镜像 C++ [`normalize_component_number`](../../../backend-cpp/src/inventory/ComponentMatcher.cpp)：trim → `－/–/—`→`-` → `＃`→`#` → 去全角空格 → 去所有空白 → 转小写 → 去尾部 `#`
- [ ] 实现 `buildReplacePreview(rows, entries, pattern)`，比对走上述归一化
- [ ] 跑到通过 → 提交

> **已确认：前端此前没有编号归一化的镜像**（搜索 `frontend/src` 只有 URL/单位/关键词等其它用途的 `normalize`）。本任务**必然引入第二处实现**，两边必须加交叉注释指明对方位置，并在前端文件头写明"权威实现在 C++，此处为镜像，改动须同步"——`inventoryNumbering.ts` 与 C++ 编号引擎已是同类镜像关系，沿用该做法。
>
> 归一化规则若两边分叉，症状是"预览说能绑、后端却判无此编号"，且只在含全角字符或异常空白的行上出现，极难定位。测试里必须包含全角 `＃`、全角空格、大小写三种输入。

## Task 3：后端批量端点

- [ ] 写失败测试 `test_import_binding_repository.cpp`：
  - 批量绑定 3 个目标 → 全部写入，返回的 overview 状态正确
  - 任一目标的 `component_number` 在该导入中不存在 → **整批不写**，原有状态不变
  - 任一 `bridge_component_id` 不在台账中 → 整批不写
  - 导入不处于 `待校对` → 拒绝
- [ ] 跑到失败
- [ ] 实现 `bind_components_batch(import_id, targets)`：单次读改写 `parsed_result_json`，先全量校验再统一落库
- [ ] 路由 `POST …/component-binding/bind-batch`，请求体 `{ "targets": [{ part_name, component_number, bridge_component_id }] }`，返回 `{ overview }`；错误返回首个出错目标与原因
- [ ] 跑到通过 → 提交

## Task 4：对话框与入口

- [ ] 写失败测试 `BulkReplaceDialog.test.tsx`：
  - 填入模式后预览表列出各行判定与合计（"将绑定 N 行 · 跳过 M 行"）
  - 模式非法时提示且"应用"禁用
  - 无可绑行时"应用"禁用
  - 点"应用"以正确的 targets 调用 `bindComponentsBatch`
- [ ] 跑到失败
- [ ] 实现对话框；`ComponentBindingWorkspace` 分组表头加「批量替换」按钮，仅在该组存在未匹配/歧义行时显示
- [ ] 应用成功后用返回的 overview 更新页面并 `onOverviewChange` 上报（侧栏待处理数同步）
- [ ] 跑到通过 → 提交

## Task 5：样式与全量回归

- [ ] 对话框样式沿用页面既有规范（次要按钮白底描边、主操作蓝底 `.is-primary-action`、`.dialog-backdrop`/`.workspace-dialog`）；预览表复用 `.data-table`
- [ ] 前端全量测试 + `tsc -b && vite build`
- [ ] 后端隔离全量 `scripts/dev/check-backend-tests.ps1`（Debug 产物）
- [ ] 目测：用真实数据在"桥面铺装"组跑一次 `第*孔桥面` → `*#跨桥面铺装`，确认 26 行的实际结果与预览一致
- [ ] 提交

---

## 不做（spec §2.2 / §8）

- 不改写报告原文，不改匹配器的自动匹配逻辑。
- 不保存规则、不跨导入复用。
- 不处理范围写法（`1-1#板~1-25#板`）——需一条病害拆成多条，涉及数据形态变更，另行设计。
