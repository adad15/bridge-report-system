# 导入保真 + 部件名称→类别对照 + 匹配器改精确匹配 实施计划

> **For agentic workers:** 逐任务实施；每步 `- [ ]` 勾选。先写失败测试 → 跑到失败 → 最小实现 → 跑到通过 → 提交。

**Goal:** 把导入病害与实际构件的匹配从"构件编号 + 构件名 vs 台账现场名"改为 spec §4-C 的**部件类别 + 构件编号（精确）**：报告"部件名称"列经固定对照表解析为规范类别，构件编号做无害归一化（保留类型词）后精确比对，**构件名不参与匹配**；同时确认/加固导入数据保真（`component_name`/`component_number` 原文存储、原文显示，不裁剪不归一化存储）。

**Architecture:** 新增纯 C++ **部件名称→规范类别对照**（H21 规范部件名 + 常见同义写法 → 类别 id）。同一部件名在不同桥型可能对应不同类别（如"桥面板"、"横向联结系"），对照表对这类名称返回**候选类别集合**，由该桥台账里实际存在的类别（`InventoryMapping.standard_component_category_id`）取交集**天然消歧**——一座桥只有一种桥型，只会命中其一。匹配器改为：对每条病害，解析其部件名称→候选类别，命中"活动映射类别 ∈ 候选类别 且 归一化编号相等"的实际构件；唯一命中且台账已确认→自动绑定，多命中→候选（歧义），无命中→未匹配。绑定界面（人工兜底）属后续模块 #3。

**Tech Stack:** C++20 / GoogleTest；`backend-cpp/src/inventory/ComponentMatcher.*`、新增 `ComponentCategoryLexicon.*`；集成点 `backend-cpp/src/db/WordImportRepository.cpp`（唯一调用方）。

**真值来源：** spec `docs/superpowers/specs/2026-07-21-component-inventory-standard-numbering-and-binding-design.md` §4-B/§4-C/§6.1；`standards/technical-condition/jtg-t-h21-2011/1.0.1/component-taxonomy.json`（类别 id 与规范名）。

**决策点（已由用户拍板）：**
1. **部件名称→类别对照**（已定）：部件名称是**规范固定用词、无同义变体**（"上部承重构件"通用），对照表 = 规范部件名 → 类别 id（键取 taxonomy 类别名，去掉括号内示例）。**不需要同义写法表**。仅跨桥型真正同名者（`横向联结系` = arch/composite_arch；`索塔` = cable_stayed/suspension）返回多候选，靠该桥台账实际类别集合消歧。
2. **类型词匹配**（已定）：构件编号**保留类型词**做精确比对（现有 `normalize_component_number` 行为：无害归一化但不剥类型词）。报告与台账类型词不同（空心板/梁/板）→ **不自动命中**，落绑定界面人工绑。构件名（类型词）本身不参与"名称→类别"解析。
3. **ConfirmedAlias（人工确认别名）路径**：本计划**保留**别名自动匹配（编号 + 别名文本），退休与否留到绑定模块 #3。
4. **导入保真**：预计无需改存储（现管线已透传原文），加**断言测试**锁定"不归一化存储"，若发现裁剪再修。

---

## 部件名称→类别对照表（H21，初稿待核）

> 候选集合列多于 1 项者靠桥型台账消歧。同义写法为**初拟**，待领域核对。

| 规范类别 id（去前缀 h21.component.） | 规范名 | 报告常见写法（同义，初拟） |
| --- | --- | --- |
| beam.upper_bearing | 上部承重构件 | 上部承重构件；主梁；挂梁；主梁板；梁板 |
| beam.upper_general | 上部一般构件 | 上部一般构件；湿接缝；横隔板；横隔梁；桥面板（梁式） |
| bearing | 支座 | 支座 |
| lower.pier | 桥墩 | 桥墩；墩柱；盖梁；系梁 |
| lower.abutment | 桥台 | 桥台；台身；台帽 |
| lower.foundation | 墩台基础 | 墩台基础；基础 |
| lower.wing_or_ear_wall | 翼墙、耳墙 | 翼墙；耳墙 |
| lower.cone_or_protection_slope | 锥坡、护坡 | 锥坡；护坡 |
| lower.regulation_structure | 调治构造物 | 调治构造物；导流构造物 |
| deck.pavement | 桥面铺装 | 桥面铺装；铺装 |
| deck.expansion_joint | 伸缩缝装置 | 伸缩缝装置；伸缩缝 |
| deck.sidewalk | 人行道 | 人行道 |
| deck.railing | 栏杆、护栏 | 栏杆；护栏 |
| deck.drainage | 排水系统 | 排水系统；排水 |
| deck.lighting_signs | 照明、标志 | 照明、标志；照明；标志 |
| arch.main_ring | 主拱圈 | 主拱圈；拱圈 |
| arch.spandrel | 拱上结构 | 拱上结构 |
| arch.deck_slab **/** composite_arch.deck_slab_or_beam | 桥面板（梁） | 桥面板；桥面板（梁）〔多候选，桥型消歧〕 |
| arch.rigid_or_truss_segment | 刚架/桁架拱片 | 刚架拱片；桁架拱片；拱片 |
| arch.transverse_link **/** composite_arch.transverse_link | 横向联结系 | 横向联结系〔多候选，桥型消歧〕 |
| composite_arch.arch_rib | 拱肋 | 拱肋 |
| composite_arch.column | 立柱 | 立柱 |
| composite_arch.hanger | 吊杆 | 吊杆 |
| composite_arch.tie_rod | 系杆（含锚具） | 系杆 |
| cable_stayed.main_girder | 主梁（斜拉） | 主梁〔与 beam 上部承重同名，桥型消歧〕 |
| cable_stayed.tower **/** suspension.tower | 索塔 | 索塔；桥塔〔多候选，桥型消歧〕 |
| cable_stayed.cable_system | 斜拉索系统 | 斜拉索；拉索 |
| suspension.stiffening_girder | 加劲梁 | 加劲梁 |
| suspension.main_saddle | 主鞍 | 主鞍；索鞍 |
| suspension.main_cable | 主缆 | 主缆 |
| suspension.cable_clamp | 索夹 | 索夹 |
| suspension.hanger | 吊索及钢护筒 | 吊索；吊杆（悬索）〔与组合拱吊杆同名，桥型消歧〕 |
| suspension.anchorage_rod | 锚杆 | 锚杆 |
| suspension.anchorage | 锚碇 | 锚碇 |
| suspension.tower_foundation | 索塔基础 | 索塔基础 |
| suspension.splay_saddle | 散索鞍 | 散索鞍 |

> 归一化匹配同名的报告写法时，先对名称做同样无害归一化（首尾空白、全半角、去括号内注、去空格）再查表。

---

## 文件结构

- 新增 `backend-cpp/include/bridge_report/inventory/ComponentCategoryLexicon.hpp` / `src/inventory/ComponentCategoryLexicon.cpp`：`resolve_component_categories(part_name) -> std::vector<std::string>`。
- 修改 `backend-cpp/include/bridge_report/inventory/ComponentMatcher.hpp` / `src/inventory/ComponentMatcher.cpp`：匹配键改为类别 + 编号。
- 修改 `backend-cpp/src/db/WordImportRepository.cpp`：集成点无需改数据流（`DefectComponentText.component_name` 仍传报告部件名称）；如匹配器签名变则同步。
- 测试：`backend-cpp/tests/test_component_category_lexicon.cpp`、`test_component_matcher.cpp`、`test_word_import_repository.cpp`（保真断言）。
- `backend-cpp/CMakeLists.txt`：登记新增源/测试。

约定：新增 `.cpp` 必须加入 `CMakeLists.txt` 的 `bridge_report_backend_core` 与 `bridge_report_backend_tests`。

---

## Task 1: 部件名称→规范类别对照

**Files:** Create `ComponentCategoryLexicon.{hpp,cpp}`；Test `test_component_category_lexicon.cpp`；Modify `CMakeLists.txt`。

- [ ] **Step 1: 失败测试**

```cpp
#include <gtest/gtest.h>
#include "bridge_report/inventory/ComponentCategoryLexicon.hpp"
namespace nt = bridge_report::inventory;

TEST(CategoryLexiconTest, MapsRegulationNamesAndSynonyms) {
    EXPECT_EQ(nt::resolve_component_categories("上部承重构件"),
              (std::vector<std::string>{"h21.component.beam.upper_bearing"}));
    EXPECT_EQ(nt::resolve_component_categories("主梁"),   // 与斜拉主梁同名 → 多候选
              (std::vector<std::string>{"h21.component.beam.upper_bearing",
                                        "h21.component.cable_stayed.main_girder"}));
    EXPECT_EQ(nt::resolve_component_categories(" 伸缩缝 "),  // 同义 + 空白
              (std::vector<std::string>{"h21.component.deck.expansion_joint"}));
    EXPECT_TRUE(nt::resolve_component_categories("不存在的部件").empty());
}
```

- [ ] **Step 2: 跑到失败**（头文件不存在）。
- [ ] **Step 3: 最小实现** — 头声明 `resolve_component_categories`；cpp 用 `static const std::unordered_map<std::string, std::vector<std::string>>`（键为归一化后的名称，值为候选类别 id），查表前对入参做无害归一化（复用/移入一个 `normalize_component_name`）。填入上表。
- [ ] **Step 4: 跑到通过**。
- [ ] **Step 5: 提交** `feat(inventory): component-name to category lexicon`

---

## Task 2: 匹配器改为 部件类别 + 编号

**Files:** Modify `ComponentMatcher.{hpp,cpp}`；Test `test_component_matcher.cpp`。

- [ ] **Step 1: 失败测试**（重写 `test_component_matcher.cpp`；entry 用真实类别 id，defect.component_name 用规范部件名）

```cpp
// 台账条目：编号 + 现场名 + 真实类别 id
InventoryEntry girder = entry("c1", "1-1#梁", "空心板", "h21.component.beam.upper_bearing");

TEST(ComponentMatcherTest, CategoryPlusNumberUniqueMatchBinds) {
    auto r = match_defect_component({"1-1#梁", "上部承重构件"},
                                    revision({girder}, "已确认"), {});
    ASSERT_TRUE(r.matched_entry.has_value());
    EXPECT_EQ(r.method, ComponentMatchMethod::Exact);
    EXPECT_EQ(r.matched_entry->bridge_component_id, "c1");
}
TEST(ComponentMatcherTest, SiteNameDoesNotAffectMatch) {   // 构件名不参与
    auto r = match_defect_component({"1-1#梁", "上部承重构件"},
        revision({entry("c1","1-1#梁","随便改的名","h21.component.beam.upper_bearing")}, "已确认"), {});
    ASSERT_TRUE(r.matched_entry.has_value());
}
TEST(ComponentMatcherTest, WrongCategoryDoesNotMatch) {
    auto r = match_defect_component({"1-1#梁", "支座"}, revision({girder}, "已确认"), {});
    EXPECT_FALSE(r.matched_entry.has_value());
    EXPECT_TRUE(r.candidate_component_ids.empty());
}
TEST(ComponentMatcherTest, MultiComponentPartSeparatedByTypeWord) {  // 同类别靠编号类型词区分
    InventoryEntry joint = entry("c2","1-1#湿接缝","湿接缝","h21.component.beam.upper_general");
    InventoryEntry diaph = entry("c3","1-1-1#横隔梁","横隔梁","h21.component.beam.upper_general");
    auto r = match_defect_component({"1-1#湿接缝","上部一般构件"}, revision({joint,diaph},"已确认"), {});
    ASSERT_TRUE(r.matched_entry.has_value());
    EXPECT_EQ(r.matched_entry->bridge_component_id, "c2");
}
TEST(ComponentMatcherTest, FullWidthNormalizationStillExact) {
    auto r = match_defect_component({" 1－1＃梁 ", "上部承重构件"}, revision({girder},"已确认"), {});
    ASSERT_TRUE(r.matched_entry.has_value());  // 归一化后精确命中（保留类型词"梁"）
}
TEST(ComponentMatcherTest, UnconfirmedInventoryOnlyCandidates) {
    auto r = match_defect_component({"1-1#梁","上部承重构件"}, revision({girder},"draft"), {});
    EXPECT_FALSE(r.matched_entry.has_value());
    EXPECT_EQ(r.candidate_component_ids, std::vector<std::string>{"c1"});
}
```

> `entry(...)` 测试工具需加第 4 参数 `category_id`（替代原来 `"category-"+id`）。

- [ ] **Step 2: 跑到失败**。
- [ ] **Step 3: 最小实现** — `match_defect_component`：
  - `const auto categories = resolve_component_categories(defect.component_name);`（空则直接无候选返回）。
  - 归一化 `defect.component_number`（现有 `normalize_component_number`，保留类型词）。
  - 遍历 `usable_entries`（活动 + 有活动映射）：命中条件 = `活动映射.category ∈ categories` 且 `normalize(entry.number) == 归一化编号`。
  - 分级：`Exact` 唯一 + 台账已确认 → 绑定；非空 → 候选；空 → 走别名/无。
  - 保留 `ConfirmedAlias` 路径（编号相等 + 别名文本匹配 `component_name`）作为兜底自动匹配；`NormalizedCandidate` 因主路径已归一化，可并入或移除（择一，测试对齐）。
  - 删除 `name_matches_entry`（对 site_component_type/site_name 的比较）。
- [ ] **Step 4: 跑到通过**。
- [ ] **Step 5: 提交** `feat(inventory): match defects by category and number`

---

## Task 3: 导入保真断言 + 集成校验

**Files:** Modify（如需）`WordImportRepository.cpp`；Test `test_word_import_repository.cpp`。

- [ ] **Step 1: 失败/表征测试** — 造一条 `component_number="33-25#板"`、`component_name="上部一般构件"` 的病害，走 `match_imported_defects`，断言：持久化后读回的 `component_number`/`component_name` **与原文逐字一致**（不裁剪类型词、不归一化存储）；匹配结果字段（method/candidate_ids/category）符合类别+编号语义。
- [ ] **Step 2: 跑到失败/表征**（若已保真则直接通过，转为回归锁定）。
- [ ] **Step 3: 最小实现** — 仅当发现存储处有归一化/裁剪时修正为原文存储；集成点把 `DefectComponentText.component_name` 明确为报告"部件名称"列（已是）。
- [ ] **Step 4: 跑到通过**（隔离 schema）。
- [ ] **Step 5: 提交** `test(inventory): lock import fidelity for defect component text`

---

## Task 4: 全量回归

- [ ] `powershell scripts/dev/check-backend-tests.ps1`（隔离 schema 全绿；psql 需 `PGCLIENTENCODING=UTF8`）。
- [ ] `cmake --build --preset vs2022-x64-debug`（后端 exe 先停；MSB3073/discovery flake 可忽略，以 exe 时间戳 + 测试全过为准）。
- [ ] 前端 `npm run test`（匹配是后端逻辑，前端无关，确认无回归即可）。
- [ ] `git diff --check`；保护目录（`.claude/`、`backend-cpp/archive/`）不提交。
- [ ] 提交（如有清理）。

---

## Self-Review 结论

- **Spec 覆盖**：实现 §4-C 匹配（部件类别 + 编号精确、构件名不参与、多构件靠类型词区分、三级分级）与 §4-B/§6.1 保真语义。绑定界面 §4-D、校对页只读 §4-E 属后续模块。
- **全桥型一致**：对照表覆盖全 6 桥型类别；同名跨桥型靠"桥台账实际类别集合"消歧，无需在匹配器引入桥型参数。
- **决策显式**：同义写法为初稿待领域核对；别名路径保留待绑定模块定；保真预计无需改代码，以断言锁定。
- **类型一致**：`DefectComponentText`/`ComponentMatchResult`/`InventoryEntry` 不变；新增 `resolve_component_categories` 纯函数。

## 后续模块（不在本计划）

绑定仓储/路由 + 绑定界面（§4-D）；校对页实际构件只读化（§4-E）；非梁桥型上部编号形状真实报告校准。
