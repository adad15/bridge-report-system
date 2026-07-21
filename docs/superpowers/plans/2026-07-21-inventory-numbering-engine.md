# 构件台账标准化编号引擎 实施计划

> **For agentic workers:** 逐任务实施；每步用 `- [ ]` 勾选。先写失败测试 → 跑到失败 → 最小实现 → 跑到通过 → 提交。

**Goal:** 把构件台账生成的"3 种编号方式 + 前后缀"替换为"按《构件编号规则》逐部件模板生成"的后端编号引擎，梁式桥全部部件按文档形式自动生成标准编号。

**Architecture:** 每个部件的编号形式写成**模板字符串**（如 `{span}-{i}#梁`、`{ab}#台{side}侧翼墙`），照《构件编号规则》一字对应；一个**模板展开引擎**按占位符的取值范围做嵌套迭代生成编号与所属位置。占位符范围由骨架（孔数 N → 孔/墩/台/支承线）与各部件用户填的数量派生。部件目录（梁式桥）是数据。本计划只做后端引擎、目录、生成器与路由校验；向导 UI、匹配器、绑定界面各自后续计划。

**Tech Stack:** C++20，Drogon，jsoncpp，GoogleTest；现有 `backend-cpp/src/inventory/*`。

**真值来源：** 用户提供的《构件编号规则》与 `docs/superpowers/specs/2026-07-21-component-inventory-standard-numbering-and-binding-design.md`。

---

## 文件结构

- 新增 `backend-cpp/include/bridge_report/inventory/NumberingTemplate.hpp` / `src/inventory/NumberingTemplate.cpp`：占位符、模板、展开引擎。
- 新增 `backend-cpp/include/bridge_report/inventory/BeamBridgePartCatalog.hpp` / `src/inventory/BeamBridgePartCatalog.cpp`：梁式桥部件目录（模板 + 默认名 + 规范类别 + 结构分部 + 数量输入定义）。
- 修改 `backend-cpp/src/inventory/ComponentInventoryGenerator.cpp` + `include/.../ComponentInventoryGenerator.hpp`：改为按目录 + 模板 + 数量生成。
- 修改 `backend-cpp/include/.../ComponentInventoryModels.hpp` + `src/inventory/ComponentInventoryModels.cpp`：`GenerateInventoryInput` 增加部件数量输入解析（`part_counts`），保留旧字段直至生成器切换完成。
- 修改 `backend-cpp/src/http/ComponentInventoryRoutes.cpp`：校验改为按目录部件 + 数量。
- 测试：`backend-cpp/tests/test_numbering_template.cpp`、`test_beam_bridge_part_catalog.cpp`、既有 `test_component_inventory_generator.cpp`、`test_component_inventory_routes.cpp`。
- 修改 `backend-cpp/CMakeLists.txt`：登记新增源文件与测试。

约定：所有新增 `.cpp` 必须显式加入 `backend-cpp/CMakeLists.txt`（`bridge_report_backend_core` 与 `bridge_report_backend_tests`）。

---

## Task 1: 占位符取值范围

**Files:**
- Create: `backend-cpp/include/bridge_report/inventory/NumberingTemplate.hpp`
- Create: `backend-cpp/src/inventory/NumberingTemplate.cpp`
- Test: `backend-cpp/tests/test_numbering_template.cpp`
- Modify: `backend-cpp/CMakeLists.txt`

占位符与取值（照《规则》骨架）：
- `{span}` 孔：`1..N`，值 = `"1"`,`"2"`…；位置 = `第k孔`。
- `{pier}` 墩：`1..N-1`，值 = `"1"`…；位置 = `第k墩`。
- `{ab}` 台：迭代 `{0, N}`，值 = `"0"`,`"N"`；位置 = `第k台`。
- `{line}` 支承线：`0..N`，渲染整段 `"0#台"`/`"1#墩"`/…/`"N#台"`（端点为台、其余为墩）；位置 = 同渲染值。
- `{side}` 左右：迭代 `{左, 右}`，值 = `"左"`,`"右"`；位置 = `左侧/右侧`。
- `{c1}` `{c2}` `{c3}` 计数维：`1..count`，值 = `"1"`…；无独立位置。

- [ ] **Step 1: 写失败测试** `test_numbering_template.cpp`

```cpp
#include <gtest/gtest.h>
#include "bridge_report/inventory/NumberingTemplate.hpp"

namespace nt = bridge_report::inventory;

TEST(NumberingRangeTest, SkeletonRangesFollowTopology) {
    const auto ctx = nt::NumberingContext{/*span_count=*/5};
    EXPECT_EQ(nt::placeholder_values(ctx, nt::Placeholder::Span, 0),
              (std::vector<nt::PlaceValue>{{"1","第1孔"},{"2","第2孔"},{"3","第3孔"},
                                           {"4","第4孔"},{"5","第5孔"}}));
    EXPECT_EQ(nt::placeholder_values(ctx, nt::Placeholder::Pier, 0),
              (std::vector<nt::PlaceValue>{{"1","第1墩"},{"2","第2墩"},
                                           {"3","第3墩"},{"4","第4墩"}}));
    EXPECT_EQ(nt::placeholder_values(ctx, nt::Placeholder::Abutment, 0),
              (std::vector<nt::PlaceValue>{{"0","第0台"},{"5","第5台"}}));
    EXPECT_EQ(nt::placeholder_values(ctx, nt::Placeholder::SupportLine, 0),
              (std::vector<nt::PlaceValue>{{"0#台","0#台"},{"1#墩","1#墩"},{"2#墩","2#墩"},
                                           {"3#墩","3#墩"},{"4#墩","4#墩"},{"5#台","5#台"}}));
    EXPECT_EQ(nt::placeholder_values(ctx, nt::Placeholder::Side, 0),
              (std::vector<nt::PlaceValue>{{"左","左侧"},{"右","右侧"}}));
    EXPECT_EQ(nt::placeholder_values(ctx, nt::Placeholder::Count, 3),
              (std::vector<nt::PlaceValue>{{"1",""},{"2",""},{"3",""}}));
}
```

- [ ] **Step 2: 跑到失败**

Run: `cmake --build --preset vs2022-x64-debug --target bridge_report_backend_tests`
Expected: 编译失败（`NumberingTemplate.hpp` 不存在 / 符号未定义）。

- [ ] **Step 3: 最小实现** — `NumberingTemplate.hpp`

```cpp
#pragma once
#include <string>
#include <vector>

namespace bridge_report::inventory {

enum class Placeholder { Span, Pier, Abutment, SupportLine, Side, Count };

struct NumberingContext { int span_count{0}; };

struct PlaceValue {
    std::string token;     // 拼进编号的文本，如 "1"、"0#台"、"左"
    std::string location;  // 所属位置文案，无则空串
    bool operator==(const PlaceValue&) const = default;
};

// count 仅对 Placeholder::Count 有意义（1..count）；其余维用 span_count 派生。
std::vector<PlaceValue> placeholder_values(
    const NumberingContext& ctx, Placeholder placeholder, int count);

}  // namespace bridge_report::inventory
```

`NumberingTemplate.cpp`：

```cpp
#include "bridge_report/inventory/NumberingTemplate.hpp"

namespace bridge_report::inventory {

std::vector<PlaceValue> placeholder_values(
    const NumberingContext& ctx, const Placeholder placeholder, const int count) {
    std::vector<PlaceValue> values;
    const int n = ctx.span_count;
    switch (placeholder) {
        case Placeholder::Span:
            for (int k = 1; k <= n; ++k)
                values.push_back({std::to_string(k), "第" + std::to_string(k) + "孔"});
            break;
        case Placeholder::Pier:
            for (int k = 1; k <= n - 1; ++k)
                values.push_back({std::to_string(k), "第" + std::to_string(k) + "墩"});
            break;
        case Placeholder::Abutment:
            values.push_back({"0", "第0台"});
            if (n > 0) values.push_back({std::to_string(n), "第" + std::to_string(n) + "台"});
            break;
        case Placeholder::SupportLine:
            for (int k = 0; k <= n; ++k) {
                const std::string label =
                    std::to_string(k) + ((k == 0 || k == n) ? "#台" : "#墩");
                values.push_back({label, label});
            }
            break;
        case Placeholder::Side:
            values.push_back({"左", "左侧"});
            values.push_back({"右", "右侧"});
            break;
        case Placeholder::Count:
            for (int k = 1; k <= count; ++k) values.push_back({std::to_string(k), ""});
            break;
    }
    return values;
}

}  // namespace bridge_report::inventory
```

在 `CMakeLists.txt` 的 `bridge_report_backend_core` 源列表加入 `src/inventory/NumberingTemplate.cpp`；在 `bridge_report_backend_tests` 加入 `tests/test_numbering_template.cpp`。

- [ ] **Step 4: 跑到通过**

Run: `cmake --build --preset vs2022-x64-debug --target bridge_report_backend_tests` 后
`.\build\vs-debug\Debug\bridge_report_backend_tests.exe --gtest_filter=NumberingRangeTest.*`
Expected: PASS。

- [ ] **Step 5: 提交**

```bash
git add backend-cpp/include/bridge_report/inventory/NumberingTemplate.hpp backend-cpp/src/inventory/NumberingTemplate.cpp backend-cpp/tests/test_numbering_template.cpp backend-cpp/CMakeLists.txt
git commit -m "feat(inventory): numbering placeholder ranges"
```

---

## Task 2: 模板展开引擎

模板 = 含占位符的字符串 + 每个占位符用哪个 `Placeholder` 及（对 Count 维）取哪个计数。占位符按在模板中**从左到右**为外层到内层做嵌套迭代；每种组合替换后得到一条编号，位置文案取"有位置的占位符"里最外层的那个（如 `第3孔`），无则空。

模板示例（照《规则》）：`{span}-{c1}#梁`、`{span}-{c1}-{c2}#横隔梁`、`{line}基础`、`{ab}#台{side}侧翼墙`、`{side}侧人行道`、`排水系统`。

**Files:**
- Modify: `backend-cpp/include/bridge_report/inventory/NumberingTemplate.hpp`
- Modify: `backend-cpp/src/inventory/NumberingTemplate.cpp`
- Test: `backend-cpp/tests/test_numbering_template.cpp`

- [ ] **Step 1: 写失败测试**（追加到 `test_numbering_template.cpp`）

```cpp
TEST(NumberingExpandTest, TwoLevelSpanMember) {
    nt::NumberingTemplate tpl;
    tpl.pattern = "{span}-{c1}#梁";
    tpl.slots = { {"{span}", nt::Placeholder::Span, 0}, {"{c1}", nt::Placeholder::Count, 13} };
    const auto out = nt::expand(tpl, nt::NumberingContext{5});
    ASSERT_EQ(out.size(), 65u);
    EXPECT_EQ(out.front().number, "1-1#梁");
    EXPECT_EQ(out.front().location, "第1孔");
    EXPECT_EQ(out.back().number, "5-13#梁");
    EXPECT_EQ(out.back().location, "第5孔");
}

TEST(NumberingExpandTest, ThreeLevelDiaphragm) {
    nt::NumberingTemplate tpl;
    tpl.pattern = "{span}-{c1}-{c2}#横隔梁";
    tpl.slots = { {"{span}", nt::Placeholder::Span, 0},
                  {"{c1}", nt::Placeholder::Count, 12},
                  {"{c2}", nt::Placeholder::Count, 2} };
    const auto out = nt::expand(tpl, nt::NumberingContext{5});
    ASSERT_EQ(out.size(), 5u * 12u * 2u);
    EXPECT_EQ(out.front().number, "1-1-1#横隔梁");
    EXPECT_EQ(out.back().number, "5-12-2#横隔梁");
}

TEST(NumberingExpandTest, SupportLineAndAbutmentAndSide) {
    nt::NumberingTemplate base; base.pattern = "{line}基础";
    base.slots = { {"{line}", nt::Placeholder::SupportLine, 0} };
    const auto base_out = nt::expand(base, nt::NumberingContext{5});
    ASSERT_EQ(base_out.size(), 6u);
    EXPECT_EQ(base_out.front().number, "0#台基础");
    EXPECT_EQ(base_out[1].number, "1#墩基础");
    EXPECT_EQ(base_out.back().number, "5#台基础");

    nt::NumberingTemplate wing; wing.pattern = "{ab}#台{side}侧翼墙";
    wing.slots = { {"{ab}", nt::Placeholder::Abutment, 0}, {"{side}", nt::Placeholder::Side, 0} };
    const auto wing_out = nt::expand(wing, nt::NumberingContext{5});
    ASSERT_EQ(wing_out.size(), 4u);
    EXPECT_EQ(wing_out.front().number, "0#台左侧翼墙");
    EXPECT_EQ(wing_out.back().number, "5#台右侧翼墙");

    nt::NumberingTemplate walk; walk.pattern = "{side}侧人行道";
    walk.slots = { {"{side}", nt::Placeholder::Side, 0} };
    const auto walk_out = nt::expand(walk, nt::NumberingContext{5});
    ASSERT_EQ(walk_out.size(), 2u);
    EXPECT_EQ(walk_out.front().number, "左侧人行道");
}

TEST(NumberingExpandTest, WholeBridgeNoPlaceholder) {
    nt::NumberingTemplate tpl; tpl.pattern = "排水系统"; tpl.slots = {};
    const auto out = nt::expand(tpl, nt::NumberingContext{5});
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out.front().number, "排水系统");
    EXPECT_EQ(out.front().location, "");
}
```

- [ ] **Step 2: 跑到失败**

Run: 构建 `bridge_report_backend_tests`。Expected: 编译失败（`NumberingTemplate`/`expand` 未定义）。

- [ ] **Step 3: 最小实现** — 在 `NumberingTemplate.hpp` 追加：

```cpp
struct NumberingSlot {
    std::string token;        // 模板里的占位符，如 "{span}"
    Placeholder placeholder;
    int count{0};             // 仅 Placeholder::Count 用
};

struct NumberingTemplate {
    std::string pattern;                 // 如 "{span}-{c1}#梁"
    std::vector<NumberingSlot> slots;    // 出现顺序 = 外层到内层
};

struct GeneratedNumber {
    std::string number;
    std::string location;   // 最外层有位置的占位符的位置文案，无则空
};

std::vector<GeneratedNumber> expand(const NumberingTemplate& tpl, const NumberingContext& ctx);
```

`NumberingTemplate.cpp` 追加（含 `<algorithm>`）：

```cpp
static std::string replace_first(std::string s, const std::string& from, const std::string& to) {
    const auto pos = s.find(from);
    if (pos != std::string::npos) s.replace(pos, from.size(), to);
    return s;
}

std::vector<GeneratedNumber> expand(const NumberingTemplate& tpl, const NumberingContext& ctx) {
    std::vector<GeneratedNumber> results{{tpl.pattern, ""}};
    for (const auto& slot : tpl.slots) {
        const auto values = placeholder_values(ctx, slot.placeholder, slot.count);
        std::vector<GeneratedNumber> next;
        next.reserve(results.size() * values.size());
        for (const auto& acc : results) {
            for (const auto& value : values) {
                GeneratedNumber item;
                item.number = replace_first(acc.number, slot.token, value.token);
                // 位置取最外层有位置的占位符：只有当前还没有位置且本维有位置时填。
                item.location = acc.location.empty() ? value.location : acc.location;
                next.push_back(std::move(item));
            }
        }
        results = std::move(next);
    }
    return results;
}
```

- [ ] **Step 4: 跑到通过**

Run: 构建后 `bridge_report_backend_tests.exe --gtest_filter=NumberingExpandTest.*`
Expected: PASS（4 个用例）。

- [ ] **Step 5: 提交**

```bash
git add backend-cpp/include/bridge_report/inventory/NumberingTemplate.hpp backend-cpp/src/inventory/NumberingTemplate.cpp backend-cpp/tests/test_numbering_template.cpp
git commit -m "feat(inventory): expand numbering templates"
```

---

## Task 3: 梁式桥部件目录

按《构件编号规则》把梁式桥每个部件的模板、默认名、规范类别、结构分部、数量输入键写成数据。规范类别 id 取自 `standards/technical-condition/jtg-t-h21-2011/1.0.1/component-taxonomy.json`。

**Files:**
- Create: `backend-cpp/include/bridge_report/inventory/BeamBridgePartCatalog.hpp`
- Create: `backend-cpp/src/inventory/BeamBridgePartCatalog.cpp`
- Test: `backend-cpp/tests/test_beam_bridge_part_catalog.cpp`
- Modify: `backend-cpp/CMakeLists.txt`

- [ ] **Step 1: 写失败测试** `test_beam_bridge_part_catalog.cpp`

```cpp
#include <gtest/gtest.h>
#include "bridge_report/inventory/BeamBridgePartCatalog.hpp"

namespace nt = bridge_report::inventory;

TEST(BeamCatalogTest, HasEighteenPartsWithStableKeys) {
    const auto& parts = nt::beam_bridge_parts();
    EXPECT_GE(parts.size(), 18u);
    const auto* girder = nt::find_part(parts, "beam.girder");
    ASSERT_NE(girder, nullptr);
    EXPECT_EQ(girder->default_name, "梁");
    EXPECT_EQ(girder->standard_component_category_id, "h21.component.beam.upper_bearing");
    EXPECT_EQ(girder->structure_part, "superstructure");
    EXPECT_EQ(girder->number_template, "{span}-{c1}#梁");
}

TEST(BeamCatalogTest, GirderGeneratesDocForms) {
    const auto* girder = nt::find_part(nt::beam_bridge_parts(), "beam.girder");
    ASSERT_NE(girder, nullptr);
    // 每孔 13 片，5 孔 → 1-1#梁 … 5-13#梁
    auto tpl = girder->number_template_with_counts({13});
    const auto out = nt::expand(tpl, nt::NumberingContext{5});
    ASSERT_EQ(out.size(), 65u);
    EXPECT_EQ(out.front().number, "1-1#梁");
    EXPECT_EQ(out.back().number, "5-13#梁");
}

TEST(BeamCatalogTest, DeckPavementAndBaseFollowDoc) {
    const auto* pav = nt::find_part(nt::beam_bridge_parts(), "deck.pavement");
    ASSERT_NE(pav, nullptr);
    EXPECT_EQ(pav->number_template, "{span}#跨桥面铺装");
    const auto pav_out = nt::expand(pav->number_template_with_counts({}), nt::NumberingContext{5});
    ASSERT_EQ(pav_out.size(), 5u);
    EXPECT_EQ(pav_out.back().number, "5#跨桥面铺装");

    const auto* base = nt::find_part(nt::beam_bridge_parts(), "lower.foundation");
    ASSERT_NE(base, nullptr);
    EXPECT_EQ(base->number_template, "{line}基础");
}
```

- [ ] **Step 2: 跑到失败**

Run: 构建 `bridge_report_backend_tests`。Expected: 编译失败（`BeamBridgePartCatalog.hpp` 不存在）。

- [ ] **Step 3: 最小实现** — `BeamBridgePartCatalog.hpp`

```cpp
#pragma once
#include <string>
#include <vector>
#include "bridge_report/inventory/NumberingTemplate.hpp"

namespace bridge_report::inventory {

// 一个部件的数量输入键（用户在向导里逐个填的数量维），按模板里 Count 占位符出现顺序。
struct PartCountInput { std::string key; std::string label; };

struct BeamBridgePart {
    std::string part_key;                          // 稳定键，如 "beam.girder"
    std::string default_name;                      // 默认现场名，可被用户改
    std::string standard_component_category_id;    // 规范评定类别
    std::string structure_part;                    // superstructure/substructure/deck_system
    std::string number_template;                   // 含 {span}/{c1}… 的模板
    std::vector<Placeholder> fixed_slots;          // 非 Count 占位符（按模板出现顺序）
    std::vector<PartCountInput> count_inputs;      // Count 占位符对应的用户数量键

    // 把 count_inputs 的实际数量填进模板 slots，得到可展开的 NumberingTemplate。
    NumberingTemplate number_template_with_counts(const std::vector<int>& counts) const;
};

const std::vector<BeamBridgePart>& beam_bridge_parts();
const BeamBridgePart* find_part(const std::vector<BeamBridgePart>& parts, const std::string& key);

}  // namespace bridge_report::inventory
```

`BeamBridgePartCatalog.cpp`（模板逐条照《规则》；`fixed_slots` 与 `count_inputs` 按模板里占位符从左到右的顺序穿插——`number_template_with_counts` 按模板中占位符出现顺序重建 `slots`）：

```cpp
#include "bridge_report/inventory/BeamBridgePartCatalog.hpp"
#include <algorithm>

namespace bridge_report::inventory {
namespace {

// 按占位符在 pattern 中出现的先后，交替消费 fixed_slots 与 counts，重建有序 slots。
NumberingTemplate assemble(const BeamBridgePart& part, const std::vector<int>& counts) {
    NumberingTemplate tpl; tpl.pattern = part.number_template;
    struct Tok { std::size_t pos; std::string token; Placeholder ph; bool is_count; };
    std::vector<Tok> toks;
    const auto add = [&](const std::string& token, Placeholder ph, bool is_count) {
        const auto pos = part.number_template.find(token);
        if (pos != std::string::npos) toks.push_back({pos, token, ph, is_count});
    };
    add("{span}", Placeholder::Span, false);
    add("{pier}", Placeholder::Pier, false);
    add("{ab}", Placeholder::Abutment, false);
    add("{line}", Placeholder::SupportLine, false);
    add("{side}", Placeholder::Side, false);
    add("{c1}", Placeholder::Count, true);
    add("{c2}", Placeholder::Count, true);
    add("{c3}", Placeholder::Count, true);
    std::sort(toks.begin(), toks.end(), [](const Tok& a, const Tok& b) { return a.pos < b.pos; });
    std::size_t count_i = 0;
    for (const auto& t : toks) {
        const int c = t.is_count ? (count_i < counts.size() ? counts[count_i++] : 0) : 0;
        tpl.slots.push_back({t.token, t.ph, c});
    }
    return tpl;
}

}  // namespace

NumberingTemplate BeamBridgePart::number_template_with_counts(const std::vector<int>& counts) const {
    return assemble(*this, counts);
}

const std::vector<BeamBridgePart>& beam_bridge_parts() {
    static const std::vector<BeamBridgePart> parts = {
        {"beam.girder", "梁", "h21.component.beam.upper_bearing", "superstructure",
         "{span}-{c1}#梁", {Placeholder::Span}, {{"girders_per_span", "每孔梁片数"}}},
        {"beam.wet_joint", "湿接缝", "h21.component.beam.upper_general", "superstructure",
         "{span}-{c1}#湿接缝", {Placeholder::Span}, {{"joints_per_span", "每孔湿接缝条数"}}},
        {"beam.diaphragm", "横隔梁", "h21.component.beam.upper_general", "superstructure",
         "{span}-{c1}-{c2}#横隔梁", {Placeholder::Span},
         {{"gaps_per_span", "每孔梁间数"}, {"beams_per_gap", "每梁间道数"}}},
        {"beam.bearing", "支座", "h21.component.bearing", "superstructure",
         "{span}-{c1}-{c2}#支座", {Placeholder::Span},
         {{"piers_per_span", "每孔墩数"}, {"bearings_per_pier", "每墩支座数"}}},
        {"lower.pier_column", "墩柱", "h21.component.lower.pier", "substructure",
         "{pier}-{c1}#墩柱", {Placeholder::Pier}, {{"columns_per_pier", "每墩柱数"}}},
        {"lower.pier_cap", "盖梁", "h21.component.lower.pier", "substructure",
         "{pier}#墩盖梁", {Placeholder::Pier}, {}},
        {"lower.tie_beam", "系梁", "h21.component.lower.pier", "substructure",
         "{pier}-{c1}#系梁", {Placeholder::Pier}, {{"tie_beams_per_pier", "每墩系梁数"}}},
        {"lower.abutment_body", "台身", "h21.component.lower.abutment", "substructure",
         "{ab}#台", {Placeholder::Abutment}, {}},
        {"lower.abutment_cap", "台帽", "h21.component.lower.abutment", "substructure",
         "{ab}#台帽", {Placeholder::Abutment}, {}},
        {"lower.foundation", "基础", "h21.component.lower.foundation", "substructure",
         "{line}基础", {Placeholder::SupportLine}, {}},
        {"lower.wing_wall", "翼墙", "h21.component.lower.wing_or_ear_wall", "substructure",
         "{ab}#台{side}侧翼墙", {Placeholder::Abutment, Placeholder::Side}, {}},
        {"lower.cone_slope", "锥坡", "h21.component.lower.cone_or_protection_slope", "substructure",
         "{ab}#台{side}侧锥坡", {Placeholder::Abutment, Placeholder::Side}, {}},
        {"lower.protection_slope", "护坡", "h21.component.lower.cone_or_protection_slope", "substructure",
         "{ab}#台护坡", {Placeholder::Abutment}, {}},
        {"deck.pavement", "桥面铺装", "h21.component.deck.pavement", "deck_system",
         "{span}#跨桥面铺装", {Placeholder::Span}, {}},
        {"deck.expansion_joint", "伸缩缝", "h21.component.deck.expansion_joint", "deck_system",
         "{c1}#伸缩缝", {}, {{"expansion_joint_count", "伸缩缝数量"}}},
        {"deck.sidewalk", "人行道", "h21.component.deck.sidewalk", "deck_system",
         "{side}侧人行道", {Placeholder::Side}, {}},
        {"deck.railing", "栏杆", "h21.component.deck.railing", "deck_system",
         "{side}侧栏杆", {Placeholder::Side}, {}},
        {"deck.drainage", "排水系统", "h21.component.deck.drainage", "deck_system",
         "排水系统", {}, {}},
        {"deck.lighting", "照明、标志", "h21.component.deck.lighting_signs", "deck_system",
         "照明、标志", {}, {}},
    };
    return parts;
}

const BeamBridgePart* find_part(const std::vector<BeamBridgePart>& parts, const std::string& key) {
    const auto it = std::find_if(parts.begin(), parts.end(),
        [&](const BeamBridgePart& p) { return p.part_key == key; });
    return it == parts.end() ? nullptr : &*it;
}

}  // namespace bridge_report::inventory
```

在 `CMakeLists.txt` 加入 `src/inventory/BeamBridgePartCatalog.cpp` 与 `tests/test_beam_bridge_part_catalog.cpp`。

> 注：台身/台帽/护坡/翼墙/锥坡等下部结构模板照《规则》拟定；`{side}` 是否输出"侧"、支座三级维度的确切计数，待有真实下部结构病害表（表 2.2-1）时以样例校准，改的只是本目录数据。

- [ ] **Step 4: 跑到通过**

Run: 构建后 `bridge_report_backend_tests.exe --gtest_filter=BeamCatalogTest.*`
Expected: PASS。

- [ ] **Step 5: 提交**

```bash
git add backend-cpp/include/bridge_report/inventory/BeamBridgePartCatalog.hpp backend-cpp/src/inventory/BeamBridgePartCatalog.cpp backend-cpp/tests/test_beam_bridge_part_catalog.cpp backend-cpp/CMakeLists.txt
git commit -m "feat(inventory): beam bridge part catalog"
```

---

## Task 4: 生成器改为按目录 + 数量生成

`GenerateInventoryInput` 增加 `int span_count` 已存在；新增 `std::vector<PartSelection> part_selections`（部件键 + 现场名 + 各数量）。`generate_component_inventory` 改为：对每个选中部件，用目录模板 + 数量 `expand`，产出 `GeneratedInventoryEntry`（编号、名称、规范类别、结构分部、位置、sort_order）；跨部件 sort_order 递增；同一 `site_component_type` 内编号去重。

**Files:**
- Modify: `backend-cpp/include/bridge_report/inventory/ComponentInventoryModels.hpp`
- Modify: `backend-cpp/src/inventory/ComponentInventoryModels.cpp`
- Modify: `backend-cpp/include/bridge_report/inventory/ComponentInventoryGenerator.hpp`
- Modify: `backend-cpp/src/inventory/ComponentInventoryGenerator.cpp`
- Test: `backend-cpp/tests/test_component_inventory_generator.cpp`

- [ ] **Step 1: 写失败测试**（新增到 `test_component_inventory_generator.cpp`）

```cpp
TEST(ComponentInventoryGeneratorTest, GeneratesFromPartCatalog) {
    inventory::GenerateInventoryInput input;
    input.span_count = 5;
    inventory::PartSelection girders;
    girders.part_key = "beam.girder";
    girders.site_name = "空心板";              // 用户改名
    girders.counts = {13};                     // 每孔 13 片
    input.part_selections.push_back(girders);

    inventory::PartSelection pavement;
    pavement.part_key = "deck.pavement";
    pavement.site_name = "桥面铺装";
    input.part_selections.push_back(pavement);

    const auto result = inventory::generate_component_inventory(input);
    ASSERT_TRUE(result.ok()) << result.error_message;
    ASSERT_EQ(result.entries.size(), 65u + 5u);
    EXPECT_EQ(result.entries.front().component_number, "1-1#空心板");   // 名称进类型词
    EXPECT_EQ(result.entries.front().site_component_type, "空心板");
    EXPECT_EQ(result.entries.front().standard_component_category_id,
              "h21.component.beam.upper_bearing");
    EXPECT_EQ(result.entries.front().structure_part, "superstructure");
    EXPECT_EQ(result.entries.front().span_or_location, "第1孔");
    EXPECT_EQ(result.entries[65].component_number, "1#跨桥面铺装");
}

TEST(ComponentInventoryGeneratorTest, RejectsUnknownPartAndDuplicateNumbers) {
    inventory::GenerateInventoryInput unknown;
    unknown.span_count = 3;
    unknown.part_selections.push_back({"beam.nope", "?", {1}});
    EXPECT_EQ(inventory::generate_component_inventory(unknown).error_code, "unknown_part_key");

    inventory::GenerateInventoryInput empty;
    empty.span_count = 3;
    EXPECT_EQ(inventory::generate_component_inventory(empty).error_code, "inventory_groups_required");
}
```

> 名称进类型词：模板里的类型词用部件 `site_name`（用户改名后）替换默认名。实现时模板改为占位类型词 `{name}`，`assemble` 时用 `site_name` 填 `{name}`。相应把 Task 3 目录里的 "梁"/"湿接缝"… 改为 `{name}`（如 `"{span}-{c1}#{name}"`），默认名仍为 "梁" 等。更新 Task 3 的断言：`girder->number_template == "{span}-{c1}#{name}"`，并在 `number_template_with_counts` 前先把 `{name}` 替换为传入名称（新增重载 `number_template_with(name, counts)`）。

- [ ] **Step 2: 跑到失败** — 构建失败（`PartSelection`/新生成器签名未定义）。

- [ ] **Step 3: 最小实现**

`ComponentInventoryModels.hpp` 增加：

```cpp
struct PartSelection {
    std::string part_key;
    std::string site_name;        // 现场名（默认取目录 default_name，可改）
    std::vector<int> counts;      // 按目录 count_inputs 顺序
};
```

在 `GenerateInventoryInput` 增加 `std::vector<PartSelection> part_selections;`（保留旧 `groups` 字段暂不删，后续清理）。

`BeamBridgePart` 增加把 `{name}` 一并填入的重载（`BeamBridgePartCatalog.*`）：

```cpp
NumberingTemplate BeamBridgePart::number_template_with(
    const std::string& name, const std::vector<int>& counts) const {
    BeamBridgePart copy = *this;
    const auto pos = copy.number_template.find("{name}");
    if (pos != std::string::npos) copy.number_template.replace(pos, 6, name.empty() ? default_name : name);
    return copy.number_template_with_counts(counts);
}
```

`ComponentInventoryGenerator.cpp` 重写核心（保留 `InventoryGenerationResult`/`ok()`）：

```cpp
InventoryGenerationResult generate_component_inventory(const GenerateInventoryInput& input) {
    InventoryGenerationResult result;
    if (input.part_selections.empty()) {
        result.error_code = "inventory_groups_required";
        result.error_message = "至少需要一个构件生成部件。";
        return result;
    }
    const auto& parts = beam_bridge_parts();
    std::set<std::pair<std::string, std::string>> unique_numbers;
    int sort_order = 0;
    for (const auto& sel : input.part_selections) {
        const auto* part = find_part(parts, sel.part_key);
        if (part == nullptr) {
            result.entries.clear();
            result.error_code = "unknown_part_key";
            result.error_message = "构件部件不在梁式桥目录中：" + sel.part_key;
            return result;
        }
        const std::string name = sel.site_name.empty() ? part->default_name : sel.site_name;
        const auto tpl = part->number_template_with(name, sel.counts);
        const auto numbers = expand(tpl, NumberingContext{input.span_count});
        for (const auto& gen : numbers) {
            if (gen.number.empty() ||
                !unique_numbers.emplace(name, gen.number).second) {
                result.entries.clear();
                result.error_code = "duplicate_inventory_component_number";
                result.error_message = "同一现场构件类型内生成了重复编号。";
                return result;
            }
            GeneratedInventoryEntry entry;
            entry.component_number = gen.number;
            entry.site_component_type = name;
            entry.site_name = name;
            entry.standard_component_category_id = part->standard_component_category_id;
            entry.structure_part = part->structure_part;
            if (!gen.location.empty()) entry.span_or_location = gen.location;
            entry.sort_order = ++sort_order;
            entry.generation_key = sel.part_key + ":" + std::to_string(entry.sort_order);
            result.entries.push_back(std::move(entry));
        }
    }
    return result;
}
```

补 `#include` 与 `<set>`；`ComponentInventoryGenerator.hpp` 声明不变（签名一致）。

- [ ] **Step 4: 跑到通过**

Run: 构建后 `bridge_report_backend_tests.exe --gtest_filter=ComponentInventoryGeneratorTest.*`
Expected: 新用例 PASS；旧的按 NumberingMode 的用例若冲突，改为用 `PartSelection`（更新 `test_component_inventory_generator.cpp` 旧用例为新入参）。

- [ ] **Step 5: 提交**

```bash
git add backend-cpp/include/bridge_report/inventory/ComponentInventoryModels.hpp backend-cpp/src/inventory/ComponentInventoryModels.cpp backend-cpp/include/bridge_report/inventory/ComponentInventoryGenerator.hpp backend-cpp/src/inventory/ComponentInventoryGenerator.cpp backend-cpp/include/bridge_report/inventory/BeamBridgePartCatalog.hpp backend-cpp/src/inventory/BeamBridgePartCatalog.cpp backend-cpp/tests/test_component_inventory_generator.cpp backend-cpp/tests/test_beam_bridge_part_catalog.cpp
git commit -m "feat(inventory): generate inventory from part catalog"
```

---

## Task 5: 生成输入解析与路由校验

`parse_generate_inventory_input` 增加解析 `span_count` + `part_selections`（`part_key`/`site_name`/`counts`）。路由 `validate_inventory_generation_standard` 改为：每个选中部件必须在梁式桥目录中、其规范类别在该规范包 taxonomy 且适用桥型且 generatable、数量非负且不超上限、生成总数不超 50000。

**Files:**
- Modify: `backend-cpp/src/inventory/ComponentInventoryModels.cpp`
- Modify: `backend-cpp/src/http/ComponentInventoryRoutes.cpp`
- Modify: `backend-cpp/include/bridge_report/http/ComponentInventoryRoutes.hpp`
- Test: `backend-cpp/tests/test_component_inventory_routes.cpp`

- [ ] **Step 1: 写失败测试**（替换 `test_component_inventory_routes.cpp` 中依赖旧 `groups` 的用例，新增）

```cpp
TEST(ComponentInventoryRoutesTest, ParsesPartSelections) {
    Json::Value body;
    body["standard_package_id"] = "11111111-1111-1111-1111-111111111111";
    body["bridge_type_id"] = "h21.bridge_type.beam";
    body["span_count"] = 5;
    Json::Value sel;
    sel["part_key"] = "beam.girder";
    sel["site_name"] = "空心板";
    sel["counts"].append(13);
    body["part_selections"].append(sel);
    inventory::GenerateInventoryInput output;
    std::string code, message;
    ASSERT_TRUE(inventory::parse_generate_inventory_input(body, output, code, message)) << message;
    ASSERT_EQ(output.part_selections.size(), 1u);
    EXPECT_EQ(output.part_selections[0].part_key, "beam.girder");
    EXPECT_EQ(output.part_selections[0].site_name, "空心板");
    ASSERT_EQ(output.part_selections[0].counts.size(), 1u);
    EXPECT_EQ(output.part_selections[0].counts[0], 13);
}
```

- [ ] **Step 2: 跑到失败** — 构建失败（解析未支持 `part_selections`）。

- [ ] **Step 3: 最小实现** — `parse_generate_inventory_input` 增加：`span_count` 校验（0..1000）；遍历 `body["part_selections"]`，每项要求 `part_key` 非空字符串、`site_name` 可空字符串、`counts` 为整数数组（每个 0..10000），组装 `PartSelection`。`validate_inventory_generation_standard`（签名可保留，改内部）：对每个 `part_selection`，`find_part` 非空、其 `standard_component_category_id` 在包 taxonomy 且 `definition_supports_bridge_type` 且 `generatable`、`structure_part` 与 taxonomy 一致；否则回 `inventory_component_category_not_supported`。空 `part_selections` 回 `inventory_groups_required`。

- [ ] **Step 4: 跑到通过**

Run: `bridge_report_backend_tests.exe --gtest_filter=ComponentInventoryRoutesTest.*`
Expected: PASS。

- [ ] **Step 5: 提交**

```bash
git add backend-cpp/src/inventory/ComponentInventoryModels.cpp backend-cpp/src/http/ComponentInventoryRoutes.cpp backend-cpp/include/bridge_report/http/ComponentInventoryRoutes.hpp backend-cpp/tests/test_component_inventory_routes.cpp
git commit -m "feat(inventory): validate part-selection generation"
```

---

## Task 6: 全量回归与旧路径清理

**Files:**
- Modify: 视需要 `backend-cpp/src/inventory/ComponentInventoryModels.*`（移除不再用的 `NumberingMode`/`GenerationGroupInput` 前提是无其他引用）
- Test: 全量

- [ ] **Step 1: 搜索旧类型引用**

Run: `grep -rn "NumberingMode\|GenerationGroupInput\|numbering_mode" backend-cpp frontend`
Expected: 记录所有引用；仅当后端无残留业务引用时删除旧枚举/结构；前端引用留待"向导计划"处理，本计划不动前端。

- [ ] **Step 2: 后端隔离 schema 全量**

Run: `powershell -ExecutionPolicy Bypass -File scripts/dev/check-backend-tests.ps1`
Expected: `Isolated backend check passed ...`；全部构件台账相关测试通过。

- [ ] **Step 3: Debug 构建（后端 exe 需先停）**

Run: `cmake --build --preset vs2022-x64-debug`
Expected: 链接通过（若 LNK1168，先停正在运行的 `bridge-report-backend` exe）。

- [ ] **Step 4: 卫生检查**

Run: `git diff --check` 与 `git status --short`
Expected: 无空白错误；`.claude/`、`backend-cpp/archive/`、`.superpowers/` 等未纳入提交。

- [ ] **Step 5: 提交**

```bash
git add -u backend-cpp
git commit -m "chore(inventory): retire legacy numbering-mode path"
```

---

## Self-Review 结论

- **Spec 覆盖**：本计划覆盖 spec §3（编号规则→模板/目录）、§4-A 台账生成的后端部分、§6.1 中"编号生成器/部件目录"。§4-C 匹配、§4-D 绑定、向导 UI 属后续计划，已在开头声明。
- **占位符扫描**：无 TBD；下部结构模板以真实报告校准的说明是**范围声明**而非占位（模板已给出可运行的初值）。
- **类型一致**：`PlaceValue`/`NumberingSlot`/`NumberingTemplate`/`GeneratedNumber`/`BeamBridgePart`/`PartSelection` 全程一致；Task 4 已把 Task 3 的类型词占位统一为 `{name}` 并同步更新断言。

## 后续计划（各自独立）

1. **前端台账向导重写**：孔数 + 逐部件勾选/填数量 → 调用本引擎生成。
2. **导入保真 + 部件名称→类别对照 + 匹配器**：`ComponentMatcher` 改为"部件类别 + 编号（精确）"。
3. **绑定界面 + 端点**：布局 B 分组核对 + 行级绑定 + 标记缺失。
4. **校对页实际构件只读化**。
