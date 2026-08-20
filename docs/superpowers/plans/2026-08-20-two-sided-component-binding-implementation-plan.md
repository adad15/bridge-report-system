# 两侧构件绑定实施计划

> **For agentic workers:** 逐任务实施，每完成一项才进入下一项。严格执行"先写失败测试 → 确认失败原因 → 最小实现 → 测试通过 → 只提交本任务相关文件"。工作区可能有他人未提交修改，禁止覆盖、清理或顺带提交。

**Goal:** 实现 [两侧构件绑定设计](../specs/2026-08-20-two-sided-component-binding-design.md)：在绑定下拉里为人行道与栏杆提供"两侧"选项，一次把该行病害拆成左右两条并各自绑定。修掉调查报告的 BG-2024-02（百股大桥全桥高 0.40 分）。

**Architecture:** 复用既有拆分机制。`materialize_component_range_splits` 只消费 `analysis.work_items[].matches`，从不碰 `parse_component_range`，因此只需换一种方式构造 analysis——matches 来自人工选定的构件而非范围展开。侧别配对判定独立成 `inventory` 层纯函数，由 `overview()` 调用并随行返回。

**Tech Stack:** C++20 / Drogon / JsonCpp / GoogleTest；React 18 / TypeScript / Vitest；PostgreSQL。

## 实施边界

不做：

- 翼墙、锥坡的按台"两侧"配对；
- 解析"两侧""全幅"等文字（本方案刻意不读编号里的词）；
- 拆分时的病害量分摊（沿用原样复制的既有约定）；
- BG-2024-01（其它病害评分政策）、BG-2024-03（防排水标度）、BG-2024-05（锥坡数量口径）、BG-2024-06（试算页展示非评分病害）；
- preview / impact_token 两阶段提交；
- 修改 `parse_component_range` 或 `normalize_component_number`。

## 不得改变的核心决策

1. 选项由**放行名单 + 结构校验**双条件产生，二者缺一不可（设计"配对判定"）。
2. 名单以标准部件类别 id 为键，不用报告部件名称字符串。
3. 名单写在代码里，不进规则包。
4. 全程不解析病害编号里的文字。
5. 复用 `materialize_component_range_splits`，不新写拆分逻辑。
6. `component_match_method` 取 `"manual"`，与既有 `bind` 一致。
7. 三道闸门一道不减：编辑锁（路由拦 + 事务内复查）、`expected_revision_id`、年度版本锁定。
8. 标度与病害量原样复制，靠 `append_split_warning` 提示核对。
9. 前端写入后必须调 `onDraftInvalidated()`。

## 实施前工作区约束

计划编写时工作区干净。若实施时又出现他人未提交修改且与本计划文件重叠，按仓库既定做法处理：逐个比对"新增行数 vs 本次实际改动量"，混合文件不要 `git add`，改用 `git hash-object -w --path <p> <tmp>` + `git update-index --cacheinfo` 入索引，再把暂存版换进工作区编译跑测试验证后还原。

禁止 `git add -A`、`git reset --hard`、`git checkout --`、`git clean`。

> **仓储事务提醒：** `ComponentInventoryRepository` 按值持有 `DbClientPtr`。事务函数里留具名变量会让事务活过 `tx.reset()`，提交回调永远不来，30 秒后报 `db_commit_failed` 且看不出真因。一律用临时量：`ComponentInventoryRepository(tx).xxx(...)`。

---

## Task 0：记录基线

- [ ] 记录 `git status --short` 与当前分支。
- [ ] 跑后端与前端基线，记录通过数（基线：后端 576 passed / 1 skipped）。
- [ ] 若已有失败，只记录与隔离，不顺带修复无关问题。

验证：

```bash
cmake --build --preset vs2022-x64-release
```

提交：无。

---

## Task 1：侧别配对判定（纯函数）

**新增：**

- `backend-cpp/include/bridge_report/inventory/SideComponentPair.hpp`
- `backend-cpp/src/inventory/SideComponentPair.cpp`
- `backend-cpp/tests/test_side_component_pair.cpp`

接口：

```cpp
struct SideComponentPair {
    std::string left_bridge_component_id;
    std::string right_bridge_component_id;
    std::string left_component_number;
    std::string right_component_number;
};

// 名单内类别且该类别下启用构件恰好成一对左右时返回配对，否则 nullopt。
[[nodiscard]] std::optional<SideComponentPair> find_side_component_pair(
    const InventoryRevision& revision,
    const std::string& standard_component_category_id);
```

步骤：

- [ ] 先写失败测试：栏杆类别下有"左侧栏杆/右侧栏杆"两件 → 返回配对，左右不颠倒。
- [ ] 实现名单（`h21.component.deck.sidewalk`、`h21.component.deck.railing`），
      注释写明为什么是名单而不是纯结构规则，以及为什么不放规则包。
- [ ] 实现结构校验：类别下**启用且有生效映射**的构件恰好两件，且把其一编号的
      "左"替换为"右"后与另一件的归一化编号相等。
- [ ] 补测试：人行道类别 → 返回配对。
- [ ] 补测试：锥坡类别（6 件）、翼墙类别（4 件）→ `nullopt`。
- [ ] **补测试：翼墙类别恰好只剩一对成对构件 → 仍然 `nullopt`。**
      这条是名单存在的理由，必须单独锁住，否则日后有人"顺手简化"回纯结构规则不会被发现。
- [ ] 补测试：名单内类别但只生成了一侧（1 件）→ `nullopt`。
- [ ] 补测试：类别下 2 件但不成对（如 0#台左侧 + 33#台右侧）→ `nullopt`。
- [ ] 补测试：停用构件不计入件数。

完成条件：纯函数，无 IO，可独立单测；判定不读病害编号。

提交建议：

```text
feat(inventory): detect the left/right component pair of a part category
```

---

## Task 2：overview 随行返回配对选项

**修改：**

- `backend-cpp/include/bridge_report/db/ImportBindingRepository.hpp`（`BindingRow` 增字段）
- `backend-cpp/src/db/ImportBindingRepository.cpp`（`overview()`）
- `backend-cpp/src/http/ImportBindingRoutes.cpp`（序列化）
- `backend-cpp/tests/test_import_binding_repository.cpp`

步骤：

- [ ] 先写失败测试：一条编号为"两侧护栏"的未匹配病害，其行带回一条侧别配对选项，
      成员是左右栏杆的 `bridge_component_id`。
- [ ] `BindingRow` 增 `std::optional<SideComponentPair> side_pair`，与 `split_eligible`
      同一处计算（仅 `unmatched` / `ambiguous` 行）。
- [ ] 类别取自该行已解析的部件类别；解析不出类别时不给选项。
- [ ] 路由序列化为 `side_pair_option: { label, bridge_component_ids[] }`，
      label 由两件构件编号拼出（如"两侧 · 左侧栏杆 + 右侧栏杆"）。
- [ ] 补测试：已绑定行（`bound`）不带选项。
- [ ] 补测试：锥坡行不带选项。

完成条件：只读路径，不写库；已有绑定概览测试不回归。

提交建议：

```text
feat(binding): surface the two-sided option on eligible binding rows
```

---

## Task 3：多构件绑定的分析构造器（纯函数）

**修改：**

- `backend-cpp/include/bridge_report/review/ComponentRangeSplitPlanner.hpp`
- `backend-cpp/src/review/ComponentRangeSplitPlanner.cpp`
- `backend-cpp/tests/test_component_range_split_planner.cpp`

接口：

```cpp
// 与 analyze_component_range_splits 同型，但 matches 来自人工选定的构件。
[[nodiscard]] ComponentRangeSplitAnalysis analyze_component_multi_bind(
    const Json::Value& current,
    const ComponentRangeSplitTarget& target,
    const std::vector<std::string>& bridge_component_ids,
    const inventory::InventoryRevision& revision);
```

步骤：

- [ ] 先写失败测试：一行 1 条病害 + 2 个构件 → analysis 有 1 个 work item、2 个 match，
      每个 match 带 `bridge_component_id`、台账真实编号、类别、结构部位、
      `match_method == "manual"`。
- [ ] 实现：按 id 从台账取条目构造 match；沿用现有的 `is_ineligible` 资格校验
      （已绑定或已标缺失 → `IneligibleTarget`）与"目标行不存在"分支。
- [ ] 校验每个构件的类别与 `target.part_name` 解析出的类别相符，不符则拒绝，整批不写。
- [ ] 补测试：走一遍 `materialize_component_range_splits`，产出 2 条病害，
      编号 / 绑定 / `manual` / `待确认` / `range_split_origin` 均正确。
- [ ] 补测试：照片整套复制，`linked_defect_candidate_id` 指向新候选。
- [ ] 补测试：标度与病害量原样复制到两条。
- [ ] 补测试：已绑定行 → `IneligibleTarget`，`result_json` 不变。
- [ ] 补测试：构件 id 在台账中不存在 → 拒绝。
- [ ] 补测试：空 id 列表 / 单个 id → 拒绝（本操作语义是多构件）。

完成条件：纯函数；`materialize` 一行未改。

提交建议：

```text
feat(review): build a split analysis from manually chosen components
```

---

## Task 4：`bind_multi` 写操作

**修改：**

- `backend-cpp/include/bridge_report/db/ImportBindingRepository.hpp`
- `backend-cpp/src/db/ImportBindingRepository.cpp`
- `backend-cpp/tests/test_import_binding_repository.cpp`

步骤：

- [ ] 先写失败测试：调用 `bind_multi` 后，`parsed_result_json` 里那条"两侧护栏"
      变成两条，各自绑定左右栏杆。
- [ ] 实现事务：锁 `import_records` → 校验状态与编辑锁 → 锁年度 → 校验
      `expected_revision_id` → 构造 analysis → materialize → 写回 → 返回新 overview。
      锁顺序与既有写路径一致（`import_records → inspection_years`）。
- [ ] 补测试：编辑锁在事务内失效 → `EditLockInvalid`，一条不写。
- [ ] 补测试：`expected_revision_id` 与事务内解析不符 → `component_inventory_revision_changed`。
- [ ] 补测试：年度未锁定时成功路径锁定年度版本。
- [ ] 补测试：失败路径不得留下年度锁定（回滚）。
- [ ] 每条新测试都验证"改回旧行为就变红"，再改回来。

完成条件：三道闸门齐全；失败路径不留副作用。

提交建议：

```text
feat(binding): add bind_multi to split one row across several components
```

---

## Task 5：路由

**修改：**

- `backend-cpp/include/bridge_report/http/ImportBindingRoutes.hpp`
- `backend-cpp/src/http/ImportBindingRoutes.cpp`
- `backend-cpp/tests/test_import_binding_routes.cpp`

步骤：

- [ ] 先写失败测试：`POST …/component-binding/bind-multi` 正常路径回 200 与新 overview。
- [ ] 实现路由，入参 `{ part_name, component_number, bridge_component_ids[],
      expected_inventory_revision_id }`，头部取 `X-Edit-Lock-Token`。
- [ ] 错误码映射沿用既有一套，不新造码。
- [ ] 补测试：缺锁令牌 / 锁失效 → 对应状态码。
- [ ] 补测试：台账版本不符 → 409 `component_inventory_revision_changed`。
- [ ] 补测试：构件 id 列表为空 → 400。

提交建议：

```text
feat(binding): expose the bind-multi endpoint
```

---

## Task 6：前端下拉选项

**修改：**

- `frontend/src/api/importBindingApi.ts`
- `frontend/src/review/binding/ComponentBindingWorkspace.tsx`
- 对应 `.test.tsx`

步骤：

- [ ] 先写失败测试：行带 `side_pair_option` 时，下拉出现"两侧 · …"且排在候选之上。
- [ ] `BindingRow` 增 `side_pair_option`；新增 `bindComponentsMulti(...)`，
      `lockToken` 与 `expectedInventoryRevisionId` 都是**必填参数**，与既有六个写接口一致。
- [ ] 下拉 value 用哨兵前缀（如 `multi:`）分流；选中后调新接口。
- [ ] **写入后调 `onDraftInvalidated()`**——病害有增删，父页面草稿不重取会显示旧数据。
- [ ] 补测试：无 `side_pair_option` 的行下拉里没有该选项。
- [ ] 补测试：写入后触发了 `onDraftInvalidated`。
- [ ] 补测试：无编辑权限时该选项不可点。

提交建议：

```text
feat(review): offer a two-sided binding option in the component dropdown
```

---

## Task 7：评分回归

**修改：**

- `backend-cpp/tests/test_h21_part_evaluation.cpp`

步骤：

- [ ] 补测试：栏杆两件构件分 `[60, 100]` → 部件分 **76**（改前的错误结果，锁住公式本身）。
- [ ] 补测试：栏杆两件构件分 `[60, 60]` → 部件分 **56**（报告值）。
- [ ] 两条都断言 `t == 10`，避免日后 t 表改动悄悄改变结论。

> 这两条锁的是 H21 公式，不是本功能的逻辑；它们让"56 从哪来"在代码里有据可查。

提交建议：

```text
test(h21): pin the railing part score for both binding outcomes
```

---

## Task 8：端到端核对

- [ ] 用测试夹具走完整链路：一条"两侧护栏"病害 → 绑定概览出选项 → 调 bind-multi →
      草稿变两条 → 试算 → 栏杆部件分 56。
- [ ] 全量跑后端与前端，与 Task 0 基线比对，确认只增不减。
- [ ] 若本地有百股大桥数据，另跑一次实测：全桥应由 84.42 落到 **84.02**（桥面系
      71.51 → 69.51）。无数据则跳过并注明。

验证：

```bash
cmake --build --preset vs2022-x64-release
```

提交建议：

```text
test(binding): cover the two-sided binding end to end
```
