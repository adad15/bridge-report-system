# 病害线索整理工作台实施计划

> **For agentic workers:** 逐任务实施，每完成一项才进入下一项。严格执行"先写失败测试 → 确认失败原因 → 最小实现 → 测试通过 → 只提交本任务相关文件"。工作区可能有他人未提交修改，禁止覆盖、清理或顺带提交。

**Goal:** 实现 [病害线索整理工作台设计](../specs/2026-08-25-defect-thread-batch-triage-design.md)。百股大桥 1197 条未绑定观测、0 条线索，现有"一条观测一张卡"的整理页在冷启动时无法使用：1197 张卡各发一次候选请求把浏览器连接池堵死，而候选推荐的是已有线索，线索为 0 时必然为空。把决策单位从"一条观测"提升到"一个批次"，1197 次决策降到 144 个批次 + 10 个异常簇。

**Architecture:** 档案页收敛为只读结果页，线索整理独立成工作台。批次不持久化——每次进入按当前有效事实实时归组。服务端一次算出批次、精确匹配与异常关系，前端卡片只展示和收集选择。落库走"批量锁定 → 完整校验 → 批量写入"三阶段单事务，且**不信任客户端提交的组描述**，全部从锁定后的数据库行重新计算。

**Tech Stack:** C++20 / Drogon / JsonCpp / GoogleTest；React 18 / TypeScript / Vitest；PostgreSQL。

## 实施边界

不做：

- 模块 07 的跨年对比结论（发展、减轻、修复、新增）；
- 批次持久化表、个人任务状态表；
- 批量修改线索名称；
- 跨浏览器/跨设备持久化"剔除"与"暂不处理"；
- 年度入库后自动预归组；
- `batch_id` 的跨快照稳定编码（首版只在同一次读取内有效）；
- 单条线索改名接口（设计 §10 记为已知缺口，本计划不补）；
- 修改任何年度事实：标度、尺寸、照片、评分、病害原文。

## 不得改变的核心决策

1. **服务端不信任客户端组装的业务事实。** 客户端提交的 `group_id`、`action`、
   `target_thread_id`、观测清单只表达用户选择；组的构件、类型、位置一律从锁定后的
   数据库行重算（设计 §7.3、§8.3）。
2. **幂等判定必须先于令牌校验。** 第一次成功会更新 `updated_at`，重试必然携带旧令牌；
   先判 `already_completed` 才有机会识别真实重试（设计 §9.3）。
3. **批次级人工确认，不逐组。** 不得在界面或文档中表述为"每一组都被人看过"
   （设计 §3.3）。真正需要逐组判断的必须进异常簇。
4. **空位置是一等公民。** 归一化后两侧位置都为空即视为相等；数据库存 `null`；
   线索名无位置时只有病害类型，不带悬空分隔符（设计 §6.1、§10）。
5. **批次不物化。** 不新增批次表，不做缓存失效（设计 §14.3）。
6. **批次明细不分页。** 首版一次返回完整明细，以此保证跨页选择与提交清单一致
   （设计 §8.2）。
7. **一个批次一次提交。** 事务单位是本次提交的全部组，不由前端拆分（设计 §8.3）。
8. **overlap 检测必须同时覆盖已有线索**，否则增量场景会为位置相近的已有病害重复建线索
   （设计 §6.5）。
9. **模块 07 引用检查必须显式调用。** 见下方提醒。
10. 线索字段从锁定后的观测派生，取最新年度原文（设计 §10）。

## 实施前工作区约束

计划编写时工作区有他人未提交修改（约 33 个文件）。若实施时与本计划涉及的文件重叠，
按仓库既定做法处理：逐个比对"新增行数 vs 本次实际改动量"，混合文件不要 `git add`，
改用 `git hash-object -w --path <p> <tmp>` + `git update-index --cacheinfo` 入索引，
再把暂存版换进工作区编译跑测试验证后还原。

禁止 `git add -A`、`git reset --hard`、`git checkout --`、`git clean`。

> **模块 07 引用检查的真实现状（务必先读）：**
> `validate_observation_state`（`DefectThreadRepository.cpp:59`）**只做三项**——年度当前
> 有效且已确认、观测是正式事实、`updated_at` 令牌匹配。模块 07 的引用检查是独立函数
> `referenced_by_confirmed_comparison`，**而且只在 `old_thread_id.has_value()`（重绑路径）
> 时才被调用**（同文件 :300）。首次绑定路径根本不查它，批量 create 走的正是这条路。
> 不要写成"复用现有校验"就以为覆盖到了。

> **仓储事务提醒：** 事务函数里不要给仓储留具名变量，否则事务会活过 `tx.reset()`，
> 提交回调永远不来，30 秒后报 `db_commit_failed` 且看不出真因。一律用临时量。

---

## Task 0：记录基线

- [ ] 记录 `git status --short`、当前分支与 HEAD。
- [ ] 跑后端与前端基线并记录通过数。计划编写时观察到：前端 454 passed；
      后端 `scripts/dev/check-backend-tests.ps1` 通过（输出 `Isolated backend check passed`）。
      实际基线以本机跑出的为准。
- [ ] 记录 `bridge_report_system` 库中百股大桥的当前统计，供 Task 3 夹具比对：

```sql
select count(*) filter (where o.defect_thread_id is null) as unbound,
       count(distinct o.bridge_component_id) as components,
       (select count(*) from defect_threads) as threads
from defect_observations o
join inspection_years iy on iy.id = o.inspection_year_id
where iy.is_current and iy.status = '已确认';
```

- [ ] 若已有失败，只记录与隔离，不顺带修复无关问题。

提交：无。

---

## Task 1：共享线索规范键（纯函数）

**新增：**

- `backend-cpp/include/bridge_report/review/ThreadCanonicalKey.hpp`
- `backend-cpp/src/review/ThreadCanonicalKey.cpp`
- `backend-cpp/tests/test_thread_canonical_key.cpp`

**修改：**

- `backend-cpp/src/review/ThreadSuggestions.cpp`（改用共享键，放开空位置全等）
- `backend-cpp/tests/test_thread_suggestions.cpp`

接口：

```cpp
struct ThreadCanonicalKey {
    std::string bridge_component_id;
    std::string normalized_defect_type;
    std::string normalized_defect_location;  // 空字符串表示"无位置"

    bool operator==(const ThreadCanonicalKey&) const = default;
    /// 稳定序列化，用于 group_id 哈希与日志。
    [[nodiscard]] std::string canonical_string() const;
};

[[nodiscard]] ThreadCanonicalKey make_thread_canonical_key(
    const std::string& bridge_component_id,
    const std::string& defect_type,
    const std::string& defect_location);

/// 位置为非全等的互相包含关系时为真；任一侧为空时恒为假。
[[nodiscard]] bool locations_overlap(
    const std::string& normalized_left, const std::string& normalized_right);
```

步骤：

- [ ] 先写失败测试：`null`、空字符串、纯空白位置 → 规范化后都得到 `""`，三者互相相等。
- [ ] 先写失败测试：病害类型的空格、全角字符、ASCII 大小写差异 → 规范化后相等。
      （现有 `normalize_suggestion_text` 已覆盖，此测试锁的是**类型也走归一化**这件事。）
- [ ] 实现 `make_thread_canonical_key`，内部复用 `normalize_suggestion_text`。
- [ ] 实现 `canonical_string()`：字段间用不可能出现在业务文本中的分隔符（如 `\x1f`），
      **空位置与空字符串编码一致**（两者本就是同一个规范值）。
- [ ] 实现 `locations_overlap`：两侧都非空、互相包含、且不相等。
- [ ] 补测试：`"大小里程侧"` 与 `"大小里程侧及左悬臂底部"` → overlap 为真。
- [ ] 补测试：两侧位置相等 → overlap 为假（那是精确匹配，不是重叠）。
- [ ] 补测试：任一侧为空 → overlap 为假。
- [ ] **改 `ThreadSuggestions.cpp`：`location_exact` 改用规范键比较，去掉
      "双方位置非空"的限制。** 这是设计 §6.4 明确要求同步调整的点——全桥 229 个铰缝
      构件的渗水泛碱都不写更细位置，不改这条它们在候选侧永远匹配不上，与批次侧口径分裂。
- [ ] 补 `test_thread_suggestions.cpp`：两侧位置都为空、同构件同类型 → `location_exact`
      为真、进入候选。

完成条件：纯函数，无 IO；候选与归组两处共用同一个键构造函数，不再各自拼字符串。

提交建议：

```text
feat(review): share one canonical key between suggestions and grouping
```

---

## Task 2：归组、批次与异常簇（纯函数）

**新增：**

- `backend-cpp/include/bridge_report/review/ThreadTriageGrouping.hpp`
- `backend-cpp/src/review/ThreadTriageGrouping.cpp`
- `backend-cpp/tests/test_thread_triage_grouping.cpp`

接口：

```cpp
struct TriageObservationInput {
    std::string id, bridge_component_id, structure_part, component_type;
    std::string business_component_code, defect_type, defect_location, updated_at;
    int inspection_year{0};
};

struct TriageThreadInput {
    std::string id, system_number, thread_name;
    std::string bridge_component_id, defect_type, defect_location, updated_at;
};

enum class TriageAction { Create, Bind };

struct TriageGroup {
    std::string group_id;
    ThreadCanonicalKey key;
    std::vector<TriageObservationInput> observations;  // 按年度升序
    std::optional<std::string> matched_thread_id;      // Bind 时有值
};

struct TriageBatch {
    std::string batch_id;
    TriageAction action{TriageAction::Create};
    std::string structure_part, component_type, defect_type, defect_location;
    std::vector<int> year_set;                          // 升序
    std::vector<TriageGroup> groups;                    // 按业务编号稳定排序
    std::string fingerprint;
};

struct TriageOverlapTarget {
    enum class Kind { Group, Thread } kind{Kind::Group};
    std::string id, display_name, system_number, normalized_location;
};

struct TriageManualCluster {
    std::string cluster_id;
    std::vector<std::string> reason_codes;              // multiple_in_year 等
    std::vector<TriageGroup> groups;
    std::vector<TriageOverlapTarget> overlap_targets;
    std::vector<TriageThreadInput> related_threads;
};

struct TriageModel {
    std::vector<TriageBatch> batches;                   // 按覆盖观测数降序
    std::vector<TriageManualCluster> manual_clusters;
    std::string snapshot_fingerprint;
    int unbound_observation_count{0};
    int batchable_group_count{0}, batchable_observation_count{0};
    int manual_group_count{0}, manual_observation_count{0};
};

[[nodiscard]] TriageModel build_triage_model(
    std::vector<TriageObservationInput> observations,
    std::vector<TriageThreadInput> existing_threads);
```

步骤：

- [ ] 先写失败测试：同构件、规范化同类型同位置、跨三年 → 归一组，观测按年度升序。
- [ ] 实现归组：按 `ThreadCanonicalKey` 聚合。
- [ ] 先写失败测试：组内某年两条 → 进异常簇，`reason_codes` 含 `multiple_in_year`，
      **不出现在任何批次里**。
- [ ] 先写失败测试：同构件同类型下两个未绑定组位置互相包含 → **两组都进同一个异常簇**，
      `overlap_targets` 中对方 `kind == Group`。
- [ ] 先写失败测试：未绑定组与同构件**已有线索**位置互相包含但不相等 → 进异常簇，
      `overlap_targets` 中对方 `kind == Thread` 且带 `system_number`。
      这条是设计 §6.5 的增量场景漏洞，漏了会为已有病害重复建线索。
- [ ] 先写失败测试：组精确命中两条已有线索 → 异常簇，`ambiguous_thread`。
- [ ] 实现精确匹配：用规范键比较同构件已有线索；恰好一条 → `Bind` 并记
      `matched_thread_id`；零条且无重叠 → `Create`。
- [ ] 先写失败测试：空位置组与空位置已有线索 → 精确命中，`Bind`。
- [ ] 实现批次键 `(structure_part, component_type, normalized_type,
      normalized_location, year_set, action)`。
- [ ] 补测试：年份集合不同 → 不同批次；`structure_part` 不同 → 不同批次
      （即便 `component_type` 相同）。
- [ ] 实现 `group_id` / `batch_id` / `fingerprint`：显式哈希算法（SHA-256 截断），
      规范序列化，年份集合升序。**不得使用 `std::hash`**——它不保证跨进程稳定。
- [ ] 实现批次排序（覆盖观测数降序）与组内稳定排序（业务编号）。
- [ ] 补测试：同一份输入两次调用产出完全相同的 id 与指纹（确定性）。

完成条件：纯函数，无 IO；批次与异常簇互斥且完备（每个组恰好落在其一）。

提交建议：

```text
feat(review): group unbound observations into triage batches and clusters
```

---

## Task 3：真实数据夹具

**新增：**

- `backend-cpp/tests/fixtures/baigu_triage_snapshot.json`
- `backend-cpp/tests/test_thread_triage_fixture.cpp`

步骤：

- [ ] 从本机 `bridge_report_system` 库导出百股大桥当前的未绑定观测与已有线索，
      写成 `TriageObservationInput` / `TriageThreadInput` 的 JSON 形状。
- [ ] **夹具头部记录来源信息**：导出日期、导入记录编号、观测集合内容哈希。
      没有它，将来数据变了只会看到数量断言失败，追不回是数据变了还是规则变了
      （设计 §12.5）。
- [ ] 先写失败测试，断言设计 §12.5 的基线：

```text
1197 条观测 → 491 组
481 组可批量处理，覆盖 1174 条观测，落在 144 个批次里
3 组 multiple_in_year
7 组 location_overlap（4 对关系）
0 组 ambiguous_thread（冷启动无线索）
```

- [ ] 补断言：最大批次是"铰缝 · 渗水泛碱 · 无位置 · 2024,2025,2026"，**163 组 / 489 条**。
      该形状下共 166 组 / 503 条，其中 3 组"某年多条"（14 条观测）被挡在批次外。
- [ ] 补断言：全部 481 组的动作都是 `Create`（冷启动线索为 0）。

> 设计 §4 已核实：加入 `structure_part` 与类型归一化后，上述数字与原始分析完全一致
> （本桥 `component_type` 不跨结构部位）。若本机跑出不同数字，先查数据快照是否已变，
> 不要直接改断言。

完成条件：夹具可离线运行，不依赖数据库连接。

提交建议：

```text
test(review): pin the Baigu triage grouping baseline
```

---

## Task 4：TriageQueryService 与工作台摘要接口

**新增：**

- `backend-cpp/include/bridge_report/review/TriageQueryService.hpp`
- `backend-cpp/src/review/TriageQueryService.cpp`
- `backend-cpp/tests/test_triage_query_service.cpp`

**修改：**

- `backend-cpp/src/http/ComponentArchiveRoutes.cpp`（注册新路由）
- `backend-cpp/CMakeLists.txt`

接口：

```http
GET /api/bridges/{bridge_id}/thread-triage
```

步骤：

- [ ] 实现取数：当前有效且已确认年度、`review_status` 为已确认/已修改、
      `defect_thread_id is null` 的观测；以及相关构件的已有线索。**一次查询取完，
      不按构件循环。**
- [ ] 调 `build_triage_model` 得到只读模型。
- [ ] 序列化摘要：总数统计、批次摘要（不含全部观测）、每批三条确定性样例、
      异常簇完整上下文（含 `overlap_targets`）。
- [ ] 样例取法：组按业务编号稳定排序后取头、中、尾各一条。**不得随机**——刷新后样例
      变来变去，人无法复核。
- [ ] 补数据库集成测试：构造两个年度、若干观测，断言摘要里的批次数与统计。
- [ ] 补测试：旧修订版年度的观测不进模型。
- [ ] 补测试：非正式观测（待确认）不进模型。

完成条件：`TriageQueryService` 只读，不写库，不物化批次。

提交建议：

```text
feat(review): serve the thread triage workbench summary
```

---

## Task 5：批次明细接口

**修改：**

- `backend-cpp/src/review/TriageQueryService.cpp`
- `backend-cpp/src/http/ComponentArchiveRoutes.cpp`
- `backend-cpp/tests/test_triage_query_service.cpp`

接口：

```http
GET /api/bridges/{bridge_id}/thread-triage/batches/{batch_id}
```

步骤：

- [ ] 一次返回该批次完整明细：batch id、动作、指纹、全部 group id、构件业务编号、
      每组目标线索摘要（仅 bind）、每条观测的 id / `updated_at` / 年份 / 位置原文 /
      标度 / 尺寸摘要 / 照片 id。**不返回照片二进制。**
- [ ] **不分页**（核心决策 6）。
- [ ] `batch_id` 在当前数据下不存在时返回 `triage_batch_changed`，提示前端刷新摘要。
- [ ] 补测试：明细里每条观测都带 `updated_at`（前端 apply 要用）。
- [ ] 补测试：`bind` 批次的每个组各自带不同的 `target_thread_id`。
- [ ] 记录最大批次（163 组 / 489 条）的响应字节数，与设计 §8.2 的 150–250 KB 估算比对，
      把实测值写回设计文档。

完成条件：打开一个批次只发一次请求，且拿到的清单足以构造 apply。

提交建议：

```text
feat(review): return one batch's full triage detail in a single response
```

---

## Task 6：ThreadResolutionService 与三阶段事务

**新增：**

- `backend-cpp/include/bridge_report/review/ThreadResolutionService.hpp`
- `backend-cpp/src/review/ThreadResolutionService.cpp`
- `backend-cpp/tests/test_thread_resolution_service.cpp`

步骤：

- [ ] 实现三阶段（设计 §9.1），顺序不得调换：
      1）收集去重全部观测 id；2）**按 UUID 固定顺序**批量 `SELECT ... FOR UPDATE`；
      3）只读校验并**收集全部错误**；4）有错即回滚；5）批量写入；6）集中重算
      `first_seen` / `latest_seen`；7）提交。
- [ ] 先写失败测试：**任一观测令牌过期 → 整批回滚，数据库中一条线索都没建。**
      这条锁的是"全成或全败"不是嘴上说说。
- [ ] 先写失败测试：三个组各有一处问题 → `issues` 列出**三条**，不是第一条就返回。
- [ ] 实现服务端重验证（设计 §7.3），逐项对应错误码：跨桥、跨构件、组内规范键不一致、
      某年多条、重复观测 id、`create` 组已存在精确匹配、`bind` 目标不是重算出的匹配、
      目标线索跨桥/跨构件。
- [ ] **显式调用 `referenced_by_confirmed_comparison`**——不要指望
      `validate_observation_state` 覆盖（见文首提醒）。
- [ ] 实现目标线索侧的引用检查：`bind` 时目标线索被已确认对比引用 → 该组拒绝，
      原因码 `thread_referenced_by_confirmed_comparison`（设计 §9.2）。
- [ ] 实现线索字段派生（设计 §10）：类型与位置取**最新年度观测的去首尾空白原文**；
      全空写 `null`；`thread_name` 无位置时只有类型；`confirmation_status` 写 `人工已确认`。
- [ ] 先写失败测试：空位置批量创建 → 数据库 `defect_location` 为 `null`，
      `thread_name` 为 `渗水泛碱`（**不是 `渗水泛碱｜`**），再次归组时精确命中该线索。
- [ ] 补测试：两个观测集合交叉的并发请求，因固定锁序不产生应用级死锁。

完成条件：一个事务、一次批量锁定、错误一次性收齐。

提交建议：

```text
feat(review): apply thread triage batches in one validated transaction
```

---

## Task 7：幂等判定

**修改：**

- `backend-cpp/src/review/ThreadResolutionService.cpp`
- `backend-cpp/tests/test_thread_resolution_service.cpp`

步骤：

- [ ] 按设计 §9.3 固定顺序：锁定 → **先判 `already_completed`** → 全部符合则成功返回
      （允许旧令牌）→ 否则校验 `updated_at` → 否则分型冲突。
- [ ] 判据按动作分开（核心决策 2）：
      - `create`：组内全部观测绑到**同一条**线索，且该线索规范键 **等于本组规范键**；
      - `bind`：在此之上还要求该线索就是请求里的 `target_thread_id`。
- [ ] 先写失败测试：`create` 成功后携带旧令牌重试 → `already_completed`，
      **不重复建线索**（线索总数不变）。
- [ ] 先写失败测试：`bind` 成功后携带旧令牌重试 → `already_completed`。
- [ ] 先写失败测试：组内部分观测已绑、部分未绑 → `partially_bound` 冲突，
      **不得误判为已完成**。
- [ ] 先写失败测试：观测绑到了规范键相同但并非 `target_thread_id` 的另一条线索 →
      `bound_to_other_thread` 冲突。
- [ ] 先写失败测试：组内观测分散在多条线索上 → `thread_split_conflict`。
- [ ] `idempotency_key` 随请求接收并记入审计日志，但判定仍以业务状态为准
      （设计 §11 列为 P2 增强）。

完成条件：真实重试成功、他人操作造成的绑定不被误判为幂等。

提交建议：

```text
feat(review): decide already-completed before the concurrency check
```

---

## Task 8：apply 与 resolve 接口

**修改：**

- `backend-cpp/src/http/ComponentArchiveRoutes.cpp`
- `backend-cpp/tests/test_component_archive_routes.cpp`

接口：

```http
POST /api/bridges/{bridge_id}/thread-triage/apply
POST /api/bridges/{bridge_id}/thread-triage/resolve
```

步骤：

- [ ] 实现 apply 请求解析（设计 §8.3 的形状），非法组合直接 400：
      `create` 带 `target_thread_id`、`bind` 缺 `target_thread_id`、`groups` 为空、
      超过服务端上限。
- [ ] 实现成功响应（设计 §8.3）：`status`、`groups_applied`、`threads_created`、
      `observations_bound`、`results[]`（含 `thread_system_number`）。
- [ ] 实现失败响应（设计 §9.4）：409 + `issues[]`，每条带 `group_id`、
      `bridge_component_id`、`observation_id`、`reason_code`、`message`。
- [ ] `already_completed` 返回 **200**，不是 409——它表示"你要的状态已达成"。
- [ ] 实现 resolve：异常簇的一次人工决策（合并为一条新线索 / 绑定已有线索 /
      分别处理）。非精确合并必须携带显式确认字段。
- [ ] 补路由测试：解析用纯函数单测，事务路径复用 Task 6/7 的集成测试。

完成条件：错误码表（设计 §8.3、§9.4）全部有对应实现与测试。

提交建议：

```text
feat(review): expose the triage apply and resolve endpoints
```

---

## Task 9：前端 API 层与工作台页面

**新增：**

- `frontend/src/api/threadTriageApi.ts`
- `frontend/src/triage/TriageBatchCard.tsx`
- `frontend/src/triage/TriageBatchDetail.tsx`
- `frontend/src/pages/ThreadTriagePage.tsx`
- 对应 `.test.tsx`

**修改：**

- `frontend/src/App.tsx`（路由）
- `frontend/src/pages/ComponentArchivePage.tsx`（入口指向新页）

步骤：

- [ ] 先写失败测试：**首屏只发一个摘要请求**；未展开批次时**不发任何
      `thread-suggestions` 请求**。这条锁的是 N+1 那个坑，必须防回归。
- [ ] 实现摘要页：批次按覆盖观测数降序，卡片显示分部、部件类型、病害、位置、
      年份集合、动作、组数、观测数。
- [ ] 先写失败测试：卡片渲染 `1#墩盖梁` 而**不是**"盖梁"；缺 `business_component_code`
      时回退到构件系统编号。现有页面因为这一处从根上没法用。
- [ ] 实现展开：打开批次只拉一次完整明细；以构件为行、年份为列横向展示；可勾选剔除。
- [ ] 先写失败测试：剔掉两组后提交，apply 负载中不含这两组。
- [ ] 先写失败测试：提交前显示"将处理 N 组、M 条观测"。
- [ ] 先写失败测试：`bind` 批次卡片**不显示**单一 BHXS 编号，展开后每组显示各自编号。
- [ ] 实现状态归属（设计 §11.4）：批次卡片是纯展示组件，不自行请求、不自行写库；
      展开/剔除/提交/刷新/错误由页面或专用 hook 统一管理。
- [ ] 失败响应能把 `issues` 标记到对应的组与观测上。

完成条件：整个工作台的请求数与批次数无关。

提交建议：

```text
feat(triage): build the batch view of the thread triage workbench
```

---

## Task 10：异常簇界面

**新增：**

- `frontend/src/triage/ManualClusterCard.tsx` + 测试

步骤：

- [ ] 先写失败测试：`location_overlap` 簇**同屏**展示相关的全部位置组及其历年观测；
      对方是已有线索时展示 `thread_name` 与 BHXS 编号。
      不得退化为"一观测一卡"——那样人会丢掉判断该合还是该分所需的上下文。
- [ ] 先写失败测试：`multiple_in_year` 把同一年的多条观测并排展示。
- [ ] 先写失败测试：`ambiguous_thread` 列出全部精确命中的线索及编号。
- [ ] 实现动作：合并为一条新线索 / 绑定已有线索 / 分别处理 / 暂不处理。
- [ ] 统计口径按簇、组、观测分别显示，**不要把"10 组"写成"10 条"**。

完成条件：异常簇保留关系上下文，动作直接对应 resolve 接口。

提交建议：

```text
feat(triage): resolve anomaly clusters with their relationships intact
```

---

## Task 11：档案页收敛与旧路径下线

**修改：**

- `frontend/src/pages/ComponentArchivePage.tsx`
- `frontend/src/archive/ThreadBindingCard.tsx`（或删除）
- `frontend/src/pages/DefectThreadReviewPage.tsx`（或删除）

步骤：

- [ ] 档案页收敛为只读结果页（设计 §2.1）：保留构件清单、线索、历年观测、评分、
      修订历史、未归入线索的计数与入口；不再承担批量候选与复杂绑定。
- [ ] 旧整理页的逐卡候选请求下线。若 `ThreadBindingCard` 仍被重绑入口复用，
      **至少把候选请求改成点开才发**，不再 mount 即请求。
- [ ] 确认新工作台不依赖 `unbound-defect-observations` 与 `thread-suggestions`
      的逐卡流程（设计 §8.5）。
- [ ] 旧接口在迁移期保留给现有功能与测试，**但删除无人使用的页面逻辑与对应测试**。
- [ ] 跑全量前端测试，与 Task 0 基线比对：删除旧页面会减少测试数，**必须逐条说明
      减少的是哪些、为什么**，不能只说"通过了"。

完成条件：模块 06 不再同时存在两套整理流程。

提交建议：

```text
refactor(archive): make the component archive a read-only result page
```

---

## Task 12：端到端核对与性能基准

步骤：

- [ ] 在本机真实数据上走完整链路：打开工作台 → 确认"铰缝·渗水泛碱·三年"批次 →
      163 条线索建成、489 条观测绑定 → 档案页对应构件看到三年并排。
- [ ] 验证设计 §13 的 14 条验收标准，逐条打勾并记录实际观察。
- [ ] 特别验：
      - 剔除一组后提交，被剔组保持未绑定；**刷新页面后该组重新出现在原批次中**
        （剔除不持久化）；
      - 提交成功后重放同一请求 → `already_completed`，线索总数不变；
      - 空位置的 163 条线索名为 `渗水泛碱`，数据库 `defect_location` 为 `null`。
- [ ] 记录性能基准（设计 §12.6）：摘要查询耗时、最大批次明细字节数与耗时、
      163 组 apply 的事务耗时、SQL 往返次数、前端首屏请求数与照片请求数。
      **把目标值写回设计 §12.6**，把估算换成实测。
- [ ] 全量跑后端与前端，与 Task 0 基线比对，确认只增不减（Task 11 的删除除外）。

验证：

```bash
cmake --build "D:\vs2022 code\bridge-report-system\backend-cpp\build\vs-debug" --config Debug
```

提交建议：

```text
test(triage): cover the thread triage workbench end to end
```

---

## 最终验收清单

- [ ] 打开工作台只发一个摘要请求，无逐观测候选请求风暴。
- [ ] 144 个批次摘要按覆盖观测数降序，样例稳定不随刷新变化。
- [ ] 所有卡片显示业务编号，可区分 `1#墩盖梁` 与 `10#墩盖梁`。
- [ ] "铰缝·渗水泛碱·三年"批次一次事务建成 163 条线索、绑定 489 条观测。
- [ ] `bind` 批次逐组绑到各构件自己的线索，界面无批次级单一 BHXS 编号。
- [ ] 未绑定组与已有线索位置包含时进异常簇，不自动 `create`。
- [ ] 任一观测并发变化 → 整批失败，数据库无部分写入，`issues` 列出全部问题。
- [ ] 重试返回 `already_completed`，不重复建线索。
- [ ] 空位置批量创建、命名无悬空分隔符、以后年度可精确匹配。
- [ ] 异常簇保留关系上下文，可完成合并 / 绑定 / 拆分。
- [ ] 剔除与"暂不处理"刷新后回到原批次（不持久化）。
- [ ] 档案页为只读结果页，模块 06 不再有两套整理流程。
- [ ] 全部处理完后，档案页"尚未归入跨年线索"提示消失。
- [ ] 后端与前端测试相对 Task 0 基线只增不减；Task 11 的删除已逐条说明。
