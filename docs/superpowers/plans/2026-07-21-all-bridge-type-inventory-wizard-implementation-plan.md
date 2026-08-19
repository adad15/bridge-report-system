# 全桥型构件台账标准编号 + 向导重写 + 旧路径退休 实施计划

> **2026-08-14 后续变更：** 河床已通过 H21 1.0.4 正式开放生成，并由评定树 2.0.3 锁定；不再使用产品目录例外。当前规则见 `docs/superpowers/specs/2026-08-14-riverbed-inventory-generation-design.md`。本计划其余内容保留为历史实施记录。

> **For agentic workers:** 逐任务实施；每步 `- [ ]` 勾选。先写失败测试 → 跑到失败 → 最小实现 → 跑到通过 → 提交。

**Goal:** 把构件台账"选规范/桥型 → 填孔数 → 逐部件勾选/填数量 → 自动生成标准编号"的新流程，从只覆盖梁式桥**扩到 H21 全部 6 种桥型**；下部结构/桥面系/支座部件在桥型间共享，各桥型的上部结构（拱圈/拱片/拱肋/斜拉索/主缆/索塔…）按《规则》的 9 种形状先拟**临时编号形状**（标 `provisional`，后续用真实报告校准）；前端向导按此统一重写；六桥型全覆盖后**彻底退休旧 `groups`/`NumberingMode` 路径**。

**Architecture:** 已建的编号引擎（`NumberingTemplate` 展开 + `PartSelection` 生成/校验/解析）本就桥型无关。本计划把"梁式桥目录"泛化为**按规范类别 id 的扁平部件全集**，每个 H21 可生成类别对应 1..n 个部件（编号形状 + 默认名 + 数量输入）。"某桥型有哪些部件" = **该部件的 `standard_component_category_id` 在所选规范包 taxonomy 里对该桥型 `generatable`** 的交集——无需在目录里维护 `bridge_type_ids`，taxonomy 是唯一真值。生成器按 `part_key` 查找不变；校验（已有）本就检查"类别适用桥型且可生成"，对任意桥型即成立；新增一个**目录端点**按桥型返回可用部件喂给向导卡片。

**Tech Stack:** 后端 C++20 / Drogon / jsoncpp / GoogleTest（`backend-cpp/src/inventory/*`、`src/http/ComponentInventoryRoutes.cpp`）；前端 React + TS + Vitest + Testing Library（`frontend/src/bridges/*`、`frontend/src/api/*`）。

**真值来源：** 用户提供的《构件编号规则》（梁式桥形状）＋ `docs/superpowers/specs/2026-07-21-component-inventory-standard-numbering-and-binding-design.md`（§3 形状抽象、§4-A 向导流程、§6.2 前端边界）＋ `standards/technical-condition/jtg-t-h21-2011/1.0.1/component-taxonomy.json`（各桥型可生成类别的唯一真值）。

**范围决策（本计划相对 spec 的变更，需在 Task 1 同步改 spec）：**
- spec §2.2 非目标"首版只覆盖梁式桥"→ 改为**覆盖 H21 全部 6 种桥型**；非梁桥型上部结构编号形状为**临时形状**，与 spec §3.2 [注]"下部结构待真实报告校准"同性质，不阻塞，落到绑定界面人工兜底。
- 六桥型全覆盖后，旧 `groups`/`NumberingMode`/前后缀 UI 全部删除（Task 6）。

---

## H21 六桥型 · 部件目录全表（本计划的作者化真值）

> 形状占位符语义见引擎：`{span}`孔1..N、`{pier}`墩1..N−1、`{ab}`台{0,N}、`{line}`支承线0..N、`{side}`左/右、`{c1..c3}`用户数量维、`{name}`现场名（填类型词）。`prov` = 临时形状待校准。

### A. 共享 · 支座（superstructure）
| part_key | 默认名 | 类别 id | 形状 | 数量输入 | prov |
| --- | --- | --- | --- | --- | --- |
| bearing.support | 支座 | bearing | `{span}-{c1}-{c2}#{name}` | 每孔墩数, 每墩支座数 | 否 |

### B. 共享 · 下部结构（substructure；梁/3拱/斜拉适用，悬索见 F）
| part_key | 默认名 | 类别 id | 形状 | 数量输入 | prov |
| --- | --- | --- | --- | --- | --- |
| lower.pier_column | 墩柱 | lower.pier | `{pier}-{c1}#{name}` | 每墩柱数 | 否 |
| lower.pier_cap | 盖梁 | lower.pier | `{pier}#墩{name}` | — | 否 |
| lower.tie_beam | 系梁 | lower.pier | `{pier}-{c1}#{name}` | 每墩系梁数 | 否 |
| lower.abutment_body | 台 | lower.abutment | `{ab}#{name}` | — | 否 |
| lower.abutment_cap | 台帽 | lower.abutment | `{ab}#{name}` | — | 否 |
| lower.foundation | 基础 | lower.foundation | `{line}{name}` | — | 否 |
| lower.wing_wall | 翼墙 | lower.wing_or_ear_wall | `{ab}#台{side}侧{name}` | — | 否 |
| lower.cone_slope | 锥坡 | lower.cone_or_protection_slope | `{ab}#台{side}侧{name}` | — | 否 |
| lower.protection_slope | 护坡 | lower.cone_or_protection_slope | `{ab}#台{name}` | — | 否 |
| lower.riverbed | 河床 | lower.riverbed | `{name}` | —（整桥单一条目） | 否 |
| lower.regulation | 调治构造物 | lower.regulation_structure | `{c1}#{name}` | 数量 | prov |

### C. 共享 · 桥面系（deck_system；全桥型一致）
| part_key | 默认名 | 类别 id | 形状 | 数量输入 | prov |
| --- | --- | --- | --- | --- | --- |
| deck.pavement | 桥面铺装 | deck.pavement | `{span}#跨{name}` | — | 否 |
| deck.expansion_joint | 伸缩缝 | deck.expansion_joint | `{c1}#{name}` | 伸缩缝数量 | 否 |
| deck.sidewalk | 人行道 | deck.sidewalk | `{side}侧{name}` | — | 否 |
| deck.railing | 栏杆 | deck.railing | `{side}侧{name}` | — | 否 |
| deck.drainage | 排水系统 | deck.drainage | `{name}` | — | 否 |
| deck.lighting | 照明、标志 | deck.lighting_signs | `{name}` | — | 否 |

### D. 梁式桥 · 上部（superstructure）
| part_key | 默认名 | 类别 id | 形状 | 数量输入 | prov |
| --- | --- | --- | --- | --- | --- |
| beam.girder | 梁 | beam.upper_bearing | `{span}-{c1}#{name}` | 每孔梁片数 | 否 |
| beam.wet_joint | 湿接缝 | beam.upper_general | `{span}-{c1}#{name}` | 每孔湿接缝条数 | 否 |
| beam.diaphragm | 横隔梁 | beam.upper_general | `{span}-{c1}-{c2}#{name}` | 每孔梁间数, 每梁间道数 | 否 |

### E. 拱桥三型 · 上部（provisional）
| part_key | 默认名 | 类别 id | 形状 | 数量输入 |
| --- | --- | --- | --- | --- |
| arch.main_ring | 主拱圈 | arch.main_ring | `{span}-{c1}#{name}` | 每孔拱圈数 |
| arch.spandrel | 拱上结构 | arch.spandrel | `{span}-{c1}#{name}` | 每孔拱上结构数 |
| arch.deck_slab | 桥面板 | arch.deck_slab | `{span}#跨{name}` | — |
| arch.segment | 拱片 | arch.rigid_or_truss_segment | `{span}-{c1}#{name}` | 每孔拱片数 |
| arch.transverse_link | 横向联结系 | arch.transverse_link | `{span}-{c1}#{name}` | 每孔联结系数 |
| carch.rib | 拱肋 | composite_arch.arch_rib | `{span}-{c1}#{name}` | 每孔拱肋数 |
| carch.transverse_link | 横向联结系 | composite_arch.transverse_link | `{span}-{c1}#{name}` | 每孔联结系数 |
| carch.column | 立柱 | composite_arch.column | `{span}-{c1}#{name}` | 每孔立柱数 |
| carch.hanger | 吊杆 | composite_arch.hanger | `{span}-{c1}#{name}` | 每孔吊杆数 |
| carch.tie_rod | 系杆 | composite_arch.tie_rod | `{span}-{c1}#{name}` | 每孔系杆数 |
| carch.deck_slab_or_beam | 桥面板 | composite_arch.deck_slab_or_beam | `{span}-{c1}#{name}` | 每孔桥面板数 |

### F. 斜拉桥 · 上部（provisional）
| part_key | 默认名 | 类别 id | 形状 | 数量输入 |
| --- | --- | --- | --- | --- |
| cs.main_girder | 主梁 | cable_stayed.main_girder | `{span}-{c1}#{name}` | 每孔主梁数 |
| cs.tower | 索塔 | cable_stayed.tower | `{c1}#{name}` | 索塔数量 |
| cs.cable | 斜拉索 | cable_stayed.cable_system | `{c1}#{name}` | 斜拉索数量 |

### G. 悬索桥 · 上部 + 特例下部（provisional）
| part_key | 默认名 | 类别 id | 分部 | 形状 | 数量输入 |
| --- | --- | --- | --- | --- | --- |
| sp.stiffening_girder | 加劲梁 | suspension.stiffening_girder | 上部 | `{span}-{c1}#{name}` | 每孔加劲梁数 |
| sp.tower | 索塔 | suspension.tower | 上部 | `{c1}#{name}` | 索塔数量 |
| sp.main_saddle | 主鞍 | suspension.main_saddle | 上部 | `{c1}#{name}` | 主鞍数量 |
| sp.main_cable | 主缆 | suspension.main_cable | 上部 | `{side}侧{name}` | — |
| sp.cable_clamp | 索夹 | suspension.cable_clamp | 上部 | `{c1}#{name}` | 索夹数量 |
| sp.hanger | 吊索 | suspension.hanger | 上部 | `{c1}#{name}` | 吊索数量 |
| sp.anchorage_rod | 锚杆 | suspension.anchorage_rod | 上部 | `{c1}#{name}` | 锚杆数量 |
| sp.anchorage | 锚碇 | suspension.anchorage | 下部 | `{c1}#{name}` | 锚碇数量 |
| sp.tower_foundation | 索塔基础 | suspension.tower_foundation | 下部 | `{c1}#{name}` | 索塔基础数量 |
| sp.splay_saddle | 散索鞍 | suspension.splay_saddle | 下部 | `{c1}#{name}` | 散索鞍数量 |

> `structure_part` 一律取自 taxonomy 该类别的 `structure_part`（避免与规范不一致）；表内"分部"仅供人读。共享类别若 taxonomy 未对某桥型标 generatable，则该桥型不出现该部件（如悬索桥无 `lower.pier`）——交由端点/校验的 taxonomy 交集自然过滤，目录不写桥型清单。

---

## 文件结构

- 重命名 `BeamBridgePartCatalog.{hpp,cpp}` → `ComponentPartCatalog.{hpp,cpp}`；`BeamBridgePart`→`CatalogPart`，`beam_bridge_parts()`→`component_parts()`，增补上表全部部件与 `bool provisional` 字段。
- 修改 `ComponentInventoryGenerator.cpp`、`ComponentInventoryRoutes.cpp`（改用新名；校验逻辑不变）。
- 新增目录端点：`ComponentInventoryRoutes.cpp` + `include/.../ComponentInventoryRoutes.hpp`（`serialize_part_catalog` 或就地）。
- 前端：`api/componentInventoryApi.ts`（类型+`fetchPartCatalog`）、新增 `bridges/inventoryNumbering.ts`（TS 展开器）、重写 `bridges/BridgeInventoryWizard.tsx`。
- 测试：`tests/test_component_part_catalog.cpp`（原 `test_beam_bridge_part_catalog.cpp` 重命名扩充）、`tests/test_component_inventory_routes.cpp`、`api/componentInventoryApi.test.ts`、`bridges/inventoryNumbering.test.ts`、`bridges/BridgeInventoryWizard.test.tsx`。
- `backend-cpp/CMakeLists.txt`：更新重命名后的源/测试文件名。
- 更新 spec §2.2/§3.2 范围说明。

约定：新增/重命名 `.cpp` 必须同步 `backend-cpp/CMakeLists.txt` 的 `bridge_report_backend_core` 与 `bridge_report_backend_tests`。

---

## Task 1: 目录泛化为全桥型 + 补全部件 + 同步 spec

**Files:** 重命名 `BeamBridgePartCatalog.*`→`ComponentPartCatalog.*` 与测试；改 `Generator.cpp`/`Routes.cpp` 引用名；`CMakeLists.txt`；spec。

- [ ] **Step 1: 写失败测试** `tests/test_component_part_catalog.cpp`（保留原梁式桥断言，新增）

```cpp
namespace nt = bridge_report::inventory;

TEST(PartCatalogTest, CoversEveryGeneratableCategoryAcrossBridgeTypes) {
    const auto& parts = nt::component_parts();
    // 关键类别都有部件（含新增调治构造物与非梁上部）
    for (const char* key : {"beam.girder","lower.regulation","bearing.support",
                            "arch.main_ring","cs.tower","sp.main_cable","sp.anchorage"})
        EXPECT_NE(nt::find_part(parts, key), nullptr) << key;
}

TEST(PartCatalogTest, BeamGirderUnchangedDocForm) {
    const auto* g = nt::find_part(nt::component_parts(), "beam.girder");
    ASSERT_NE(g, nullptr);
    const auto out = nt::expand(g->number_template_with("梁", {13}), nt::NumberingContext{5});
    ASSERT_EQ(out.size(), 65u);
    EXPECT_EQ(out.front().number, "1-1#梁");
    EXPECT_EQ(out.back().number, "5-13#梁");
}

TEST(PartCatalogTest, ProvisionalSuperstructureExpandsSanely) {
    const auto* ring = nt::find_part(nt::component_parts(), "arch.main_ring");
    ASSERT_NE(ring, nullptr);
    EXPECT_TRUE(ring->provisional);
    const auto out = nt::expand(ring->number_template_with("主拱圈", {2}), nt::NumberingContext{3});
    ASSERT_EQ(out.size(), 6u);            // 3 孔 × 2
    EXPECT_EQ(out.front().number, "1-1#主拱圈");

    const auto* cable = nt::find_part(nt::component_parts(), "sp.main_cable");
    ASSERT_NE(cable, nullptr);
    const auto cout = nt::expand(cable->number_template_with("主缆", {}), nt::NumberingContext{4});
    ASSERT_EQ(cout.size(), 2u);           // 左/右
    EXPECT_EQ(cout.front().number, "左侧主缆");
}
```

- [ ] **Step 2: 跑到失败** — 构建失败（`component_parts`/`CatalogPart::provisional` 未定义）。

- [ ] **Step 3: 最小实现**
  - `ComponentPartCatalog.hpp`：`BeamBridgePart`→`CatalogPart`，加 `bool provisional{false};`；`beam_bridge_parts()`→`component_parts()`；`find_part` 签名不变（参数改 `CatalogPart`）。
  - `ComponentPartCatalog.cpp`：把上表 A–G 全部部件写进 `component_parts()`；共享 A/B/C 与梁 D 沿用现值（补 `lower.regulation`）；E/F/G 各部件 `provisional=true`。`number_template_with`/`assemble` 逻辑不变。
  - `Generator.cpp`/`Routes.cpp`：`beam_bridge_parts()`→`component_parts()`；include 改名。
  - `CMakeLists.txt`：源/测试文件重命名。
  - spec §2.2 改"覆盖 6 桥型 + 非梁上部临时形状"；§3.2 [注] 补一句"非梁桥型上部同此临时策略"。

- [ ] **Step 4: 跑到通过** — `bridge_report_backend_tests.exe --gtest_filter=PartCatalogTest.*`。

- [ ] **Step 5: 提交** `feat(inventory): generalize part catalog to all bridge types`

---

## Task 2: 部件目录端点

按所选规范包 + 桥型返回可用部件（taxonomy 交集），喂向导卡片。

**Files:** `ComponentInventoryRoutes.cpp` + `.hpp`；`tests/test_component_inventory_routes.cpp`。

- [ ] **Step 1: 写失败测试**（追加到 routes 测试）

```cpp
TEST(ComponentInventoryRoutesTest, SerializesPartCatalogForBridgeType) {
    // 造含 h21.component.beam.upper_bearing + lower.pier 的 taxonomy 包（generatable, 支持 beam）
    bridge_report::standards::StandardPackage package = /* … 见既有用例构造法 … */;
    Json::Value out = http::serialize_part_catalog(package, "h21.bridge_type.beam");
    ASSERT_TRUE(out.isArray());
    // 含 beam.girder，且带 number_template 与 count_inputs 标签
    bool has_girder = false;
    for (const auto& p : out)
        if (p["part_key"].asString() == "beam.girder") {
            has_girder = true;
            EXPECT_EQ(p["number_template"].asString(), "{span}-{c1}#{name}");
            ASSERT_TRUE(p["count_inputs"].isArray());
            EXPECT_EQ(p["count_inputs"][0]["label"].asString(), "每孔梁片数");
            EXPECT_EQ(p["structure_part"].asString(), "superstructure");
        }
    EXPECT_TRUE(has_girder);
    // 该桥型 taxonomy 不含拱类别 → 无拱部件
    for (const auto& p : out) EXPECT_NE(p["part_key"].asString().rfind("arch.", 0), 0u);
}
```

- [ ] **Step 2: 跑到失败** — `serialize_part_catalog` 未声明。

- [ ] **Step 3: 最小实现**
  - `serialize_part_catalog(const StandardPackage&, const std::string& bridge_type_id) -> Json::Value`：遍历 `component_parts()`，保留其 `standard_component_category_id` 在包 `component-taxonomy.json` 且 `definition_supports_bridge_type` 且 `generatable` 且 `structure_part` 与 taxonomy 一致者；每项输出 `{part_key, default_name, structure_part, standard_component_category_id, number_template, provisional, count_inputs:[{key,label}]}`。
  - 注册 `GET /api/component-inventories/part-catalog`：`require_user`；query `standard_package_id`（uuid 校验）+ `bridge_type_id`；`load_request_package`；返回 `{ "parts": serialize_part_catalog(...) }`；包不可用→409。加 OPTIONS。
  - `.hpp` 导出 `serialize_part_catalog`（供单测）。

- [ ] **Step 4: 跑到通过** — routes 测试全绿。

- [ ] **Step 5: 提交** `feat(inventory): part-catalog endpoint by bridge type`

---

## Task 3: 前端 API — 类型 + 目录拉取 + 输入契约

**Files:** `api/componentInventoryApi.ts`；`api/componentInventoryApi.test.ts`。

- [ ] **Step 1: 写失败测试**（追加）

```ts
it("fetches the part catalog for a bridge type", async () => {
  fetchMock.mockResponseOnce(JSON.stringify({ parts: [{
    part_key: "beam.girder", default_name: "梁", structure_part: "superstructure",
    standard_component_category_id: "h21.component.beam.upper_bearing",
    number_template: "{span}-{c1}#{name}", provisional: false,
    count_inputs: [{ key: "girders_per_span", label: "每孔梁片数" }],
  }] }));
  const parts = await fetchPartCatalog("http://backend", "pkg-1", "h21.bridge_type.beam");
  expect(parts[0].part_key).toBe("beam.girder");
  expect(parts[0].count_inputs[0].label).toBe("每孔梁片数");
});

it("posts part_selections without legacy groups", async () => {
  fetchMock.mockResponseOnce(JSON.stringify({ revision }));
  await generateComponentInventory("http://backend", "bridge-1", {
    standard_package_id: "pkg-1", bridge_type_id: "h21.bridge_type.beam", span_count: 5,
    part_selections: [{ part_key: "beam.girder", site_name: "空心板", counts: [13] }],
  });
  const body = JSON.parse(fetchMock.mock.calls[0][1]!.body as string);
  expect(body.part_selections[0].part_key).toBe("beam.girder");
  expect(body.groups).toBeUndefined();
});
```

- [ ] **Step 2: 跑到失败** — `fetchPartCatalog`/`part_selections` 未定义。

- [ ] **Step 3: 最小实现**
  - 加类型 `CatalogPartCountInput{key,label}`、`CatalogPart{part_key,default_name,structure_part,standard_component_category_id,number_template,provisional,count_inputs}`、`PartSelection{part_key,site_name,counts}`。
  - `GenerateComponentInventoryInput`：加 `part_selections?: PartSelection[]`；`groups?`、`template_id?` 改可选（Task 6 删旧路径后清理）。
  - `fetchPartCatalog(baseUrl, packageId, bridgeTypeId): Promise<CatalogPart[]>` → GET，带 query。

- [ ] **Step 4: 跑到通过** — api 测试全绿。

- [ ] **Step 5: 提交** `feat(inventory): frontend part-catalog api + part_selections`

---

## Task 4: 前端 TS 编号预览器（镜像引擎）

纯函数，镜像 §3.1 骨架与展开，向导卡片即时预览；与 C++ 同以《规则》文档形式为交叉校验。

**Files:** 新增 `bridges/inventoryNumbering.ts`、`bridges/inventoryNumbering.test.ts`。

- [ ] **Step 1: 写失败测试**

```ts
import { expandTemplate } from "./inventoryNumbering";

it("expands span×member like the backend", () => {
  const out = expandTemplate("{span}-{c1}#{name}", "梁", [13], 5);
  expect(out.length).toBe(65);
  expect(out[0]).toEqual({ number: "1-1#梁", location: "第1孔" });
  expect(out.at(-1)!.number).toBe("5-13#梁");
});
it("expands support line and side", () => {
  expect(expandTemplate("{line}{name}", "基础", [], 5)[0].number).toBe("0#台基础");
  expect(expandTemplate("{side}侧{name}", "主缆", [], 4).map(x=>x.number)).toEqual(["左侧主缆","右侧主缆"]);
});
it("whole-bridge template yields one", () => {
  expect(expandTemplate("{name}", "排水系统", [], 5)).toEqual([{ number: "排水系统", location: "" }]);
});
```

- [ ] **Step 2: 跑到失败** — 模块不存在。

- [ ] **Step 3: 最小实现** — `expandTemplate(pattern, name, counts, spanCount)`：先替 `{name}`；按 pattern 中占位符从左到右取值范围（span 1..N、pier 1..N−1、ab {0,N}、line 0..N（端台中墩）、side 左/右、c1..c3 计数），嵌套迭代 `replace_first`，location 取最外层有位置者。与 `NumberingTemplate.cpp` 一一对应。

- [ ] **Step 4: 跑到通过** — `vitest run inventoryNumbering`。

- [ ] **Step 5: 提交** `feat(inventory): client-side numbering preview`

---

## Task 5: 向导重写（全桥型统一）

选规范包 + 桥型 → 填孔数 → 从目录端点拉部件 → 逐部件卡片（勾选启用 + 现场名 + 各数量维）→ 预览 → `onPlanChange` 发 `part_selections`。移除编号方式/前后缀/模板数量项 UI。`provisional` 部件旁标"临时编号（待校准）"。

**Files:** 重写 `bridges/BridgeInventoryWizard.tsx`；重写 `bridges/BridgeInventoryWizard.test.tsx`。父组件 `ComponentInventoryEditor.tsx`/`CreateBridgeDialog.tsx` 只转发 plan，无需改。

- [ ] **Step 1: 写失败测试**（mock `fetchPartCatalog` 返回梁式桥部件 + 一个 `provisional` 斜拉部件）

```tsx
it("emits part_selections for enabled parts with counts", async () => {
  const onPlanChange = vi.fn();
  render(<BridgeInventoryWizard onPlanChange={onPlanChange} />);
  await userEvent.selectOptions(await screen.findByLabelText("桥型"), "h21.bridge_type.beam");
  await userEvent.type(screen.getByLabelText("跨数"), "5");
  await userEvent.click(screen.getByLabelText("启用 梁"));
  await userEvent.type(screen.getByLabelText("梁 每孔梁片数"), "13");
  await waitFor(() => expect(onPlanChange).toHaveBeenLastCalledWith(expect.objectContaining({
    bridge_type_id: "h21.bridge_type.beam", span_count: 5,
    part_selections: [expect.objectContaining({ part_key: "beam.girder", counts: [13] })],
  })));
  expect(screen.getByText(/1-1#梁/)).toBeInTheDocument();               // 预览
});

it("marks provisional parts and renames flow into the number", async () => {
  /* 选斜拉桥；启用主梁改名后编号带新名；provisional 部件显示“临时编号（待校准）” */
});
```

- [ ] **Step 2: 跑到失败** — 旧向导无这些控件。

- [ ] **Step 3: 最小实现** — 重写向导：状态 = `{packageId, bridgeTypeId, spanCount, enabled:Set, names:Record, counts:Record}`；选桥型后 `fetchPartCatalog`；按 `structure_part` 分组渲染卡片（勾选 + 名称输入 + 每个 `count_inputs` 一个数字框 + `provisional` 徽章 + `expandTemplate` 预览前 N 条）；派生 `part_selections`（仅勾选、名称非空、数量维完整且总规模合理）→ `onPlanChange`。删除 `NumberingMode`/prefix/suffix/`previewInventoryNumbers`/模板数量项相关代码。

- [ ] **Step 4: 跑到通过** — `vitest run BridgeInventoryWizard`；`npm run build`。

- [ ] **Step 5: 提交** `feat(inventory): rewrite wizard to part-selection cards`

---

## Task 6: 退休旧 groups/NumberingMode 路径（前后端 + 测试迁移）

六桥型全覆盖后旧路径无消费者，删除。

**Files:** `ComponentInventoryModels.{hpp,cpp}`、`ComponentInventoryGenerator.cpp`、`ComponentInventoryRoutes.cpp`、三份后端测试、`api/componentInventoryApi.ts`；全量。

- [ ] **Step 1: 搜索引用** `grep -rn "NumberingMode\|GenerationGroupInput\|numbering_mode\|\.groups\b" backend-cpp frontend`，逐一处理。

- [ ] **Step 2: 后端删旧路径** — 删 `NumberingMode`/`parse_numbering_mode`/`to_string`/`GenerationGroupInput`/`GenerateInventoryInput::groups`；`generate_component_inventory` 去掉 groups 分支（空 `part_selections` 直接 `inventory_part_selections_required`）；`validate_inventory_generation_standard` 去掉 legacy 分支与模板/quantity 校验；`parse_generate_inventory_input` 去掉 groups 与 `template_id` 必填。

- [ ] **Step 3: 迁移测试** — `test_component_inventory_generator.cpp`/`test_component_inventory_routes.cpp`/`test_component_inventory_repository.cpp` 里所有 `input.groups` 用例改为 `part_selections`（仓储集成夹具用 `beam.girder` 等目录部件播种）。

- [ ] **Step 4: 前端删旧字段** — `GenerateComponentInventoryInput` 去掉 `groups`/`template_id`（保留 `input_quantities` 若仍需，否则一并删）；删残余引用。

- [ ] **Step 5: 全量回归**
  - `scripts/dev/check-backend-tests.ps1`（隔离 schema 全绿）。
  - `cmake --build --preset vs2022-x64-debug`（后端 exe 先停）。
  - 前端 `npm run test` + `npm run build`。
  - `git diff --check`；`.claude/`、`archive/`、`.superpowers/` 未提交。

- [ ] **Step 6: 提交** `chore(inventory): retire legacy numbering-mode path`

---

## Self-Review 结论

- **Spec 覆盖**：实现 spec §4-A 向导（全桥型）、§6.2 前端边界；§3.3 形状抽象复用到非梁上部（临时）。§4-C 匹配、§4-D 绑定仍属后续计划。
- **范围变更已显式**：spec §2.2 非目标被本计划推翻（全桥型 + 非梁上部临时形状），Task 1 同步改 spec，非静默。
- **taxonomy 为桥型真值**：目录不写 `bridge_type_ids`，端点/校验用包 taxonomy 交集，悬索桥特例下部自然分流；新增桥型/类别只需补目录部件。
- **临时形状非占位**：E/F/G 均给出可运行初值并标 `provisional`，与 spec §3.2 [注] 同性质，绑定界面兜底，不阻塞。
- **类型一致**：`CatalogPart`/`PartSelection`/`GeneratedInventoryEntry` 全程一致；前端 `expandTemplate` 与 C++ `expand` 以《规则》文档形式交叉校验。

## 后续计划（不在本计划）

导入保真 + 部件名→类别对照 + `ComponentMatcher` 精确匹配；绑定界面 + 端点；校对页实际构件只读化。非梁桥型上部编号形状拿到真实报告后校准（改的只是目录数据与 `provisional` 标记）。
