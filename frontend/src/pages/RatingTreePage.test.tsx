import { fireEvent, render, screen, waitFor } from "@testing-library/react";
import { MemoryRouter, Route, Routes } from "react-router-dom";
import { beforeEach, describe, expect, it, vi } from "vitest";

import * as ratingTreeApi from "../api/ratingTreeApi";
import * as standardsApi from "../api/standardsApi";
import { clearRatingTreeViewStateForTests } from "../rating-tree/ratingTreeViewState";
import { RatingTreePage } from "./RatingTreePage";

const version: ratingTreeApi.RatingTreeVersion = {
  id: "version-1",
  tree_code: "organization-bridge",
  tree_name: "单位桥梁评定树",
  package_version: "1.0.0",
  tree_content_checksum: `sha256:${"a".repeat(64)}`,
  status: "published",
  published_at: "2026-07-28",
  contract_version: 1,
  node_count: 440,
  is_default: true,
  sources: [{ source_type: "technical_condition", package_version: "1.0.2" }],
};

const root: ratingTreeApi.RatingTreeNodeSummary = {
  id: "root-1",
  node_key: "org.bridge.root",
  parent_node_id: null,
  display_number: null,
  display_name: "桥梁有效评定树",
  node_type: "root",
  sort_order: 0,
  bridge_type_ids: ["h21.bridge_type.beam"],
  component_category_ids: [],
  scoring_mode: "non_scoring",
  h21_indicator_id: null,
  is_selectable: false,
  is_scoring: false,
};

const group5: ratingTreeApi.RatingTreeNodeSummary = {
  ...root,
  id: "group-5",
  node_key: "org.bridge.group.5",
  parent_node_id: root.id,
  display_name: "梁式桥上部结构",
  node_type: "structure_group",
  sort_order: 50,
};

const group51: ratingTreeApi.RatingTreeNodeSummary = {
  ...group5,
  id: "group-5-1",
  node_key: "org.bridge.group.5_1",
  parent_node_id: group5.id,
  display_name: "混凝土梁式桥",
  sort_order: 10,
};

const group511: ratingTreeApi.RatingTreeNodeSummary = {
  ...group51,
  id: "group-5-1-1",
  node_key: "org.bridge.group.5_1_1",
  parent_node_id: group51.id,
  display_name: "上部承重构件、上部一般构件",
  node_type: "component_group",
  component_category_ids: [
    "h21.component.beam.upper_bearing",
    "h21.component.beam.upper_general",
  ],
};

const defect5111: ratingTreeApi.RatingTreeNodeSummary = {
  ...group511,
  id: "defect-5-1-1-1",
  node_key: "org.bridge.defect.5_1_1_1",
  parent_node_id: group511.id,
  display_number: "5.1.1-1",
  display_name: "蜂窝、麻面",
  node_type: "defect",
  scoring_mode: "inherit_h21",
  h21_indicator_id: "h21.indicator.honeycomb",
  is_selectable: true,
  is_scoring: true,
  allowed_scales: [1, 2, 3],
};

function detailFor(node: ratingTreeApi.RatingTreeNodeSummary): ratingTreeApi.RatingTreeNode {
  return {
    ...node,
    organization_note: `${node.display_name}说明。`,
    allowed_scales: [],
    h21_indicator_name: null,
    h21_source_table: null,
    scale_descriptions: {},
    deduction_points: {},
    path: [
      {
        id: root.id,
        node_key: root.node_key,
        display_number: root.display_number,
        display_name: root.display_name,
        node_type: root.node_type,
      },
      {
        id: node.id,
        node_key: node.node_key,
        display_number: node.display_number,
        display_name: node.display_name,
        node_type: node.node_type,
      },
    ],
    sources: [],
  };
}

const children = new Map<string, ratingTreeApi.RatingTreeNodeSummary[]>([
  ["root", [root]],
  [root.id, [group5]],
  [group5.id, [group51]],
  [group51.id, [group511]],
  [group511.id, [defect5111]],
]);

const defectDetail: ratingTreeApi.RatingTreeNode = {
  ...detailFor(defect5111),
  allowed_scales: [1, 2, 3],
  h21_indicator_name: "蜂窝、麻面",
  h21_source_table: "表 4.2.1",
  scale_descriptions: { "1": "局部轻微", "2": "较大范围", "3": "大面积严重" },
  deduction_points: { "1": 5, "2": 15, "3": 25 },
};

const details = new Map<string, ratingTreeApi.RatingTreeNode>([
  [root.id, detailFor(root)],
  [group5.id, detailFor(group5)],
  [group51.id, detailFor(group51)],
  [group511.id, detailFor(group511)],
  [defect5111.id, defectDetail],
]);

function renderPage() {
  return render(
    <MemoryRouter initialEntries={["/rating-trees/version-1"]}>
      <Routes>
        <Route path="/rating-trees/:versionId" element={<RatingTreePage />} />
      </Routes>
    </MemoryRouter>,
  );
}

// 顶栏的"评定树"链接不带版本号，来回切页走的是这条路径。
function renderIndex() {
  return render(
    <MemoryRouter initialEntries={["/rating-trees"]}>
      <Routes>
        <Route path="/rating-trees" element={<RatingTreePage />} />
        <Route path="/rating-trees/:versionId" element={<RatingTreePage />} />
      </Routes>
    </MemoryRouter>,
  );
}

describe("RatingTreePage", () => {
  beforeEach(() => {
    vi.restoreAllMocks();
    clearRatingTreeViewStateForTests();
    vi.spyOn(ratingTreeApi, "fetchRatingTreeVersion").mockResolvedValue(version);
    vi.spyOn(ratingTreeApi, "fetchRatingTreeVersions").mockResolvedValue([version as never]);
    vi.spyOn(ratingTreeApi, "fetchRatingTreeChildren").mockImplementation(
      async (_baseUrl, _versionId, parentId) =>
        children.get(parentId ?? "root") ?? [],
    );
    vi.spyOn(ratingTreeApi, "fetchRatingTreeNode").mockImplementation(
      async (_baseUrl, _versionId, nodeId) => details.get(nodeId) ?? detailFor(root),
    );
    vi.spyOn(ratingTreeApi, "searchRatingTree").mockResolvedValue([]);
    vi.spyOn(standardsApi, "fetchStandardPackages").mockResolvedValue([{
      id: "h21-package",
      family: "technical_condition",
      algorithm_id: "jtg-h21-2011",
      package_version: "1.0.2",
      sync_status: "正常",
    } as never]);
    vi.spyOn(standardsApi, "fetchStandardCatalog").mockResolvedValue({
      bridge_types: [{ id: "h21.bridge_type.beam", name: "梁式桥" }],
      component_categories: [
        { id: "h21.component.beam.upper_bearing", name: "上部承重构件（主梁、挂梁）" },
        { id: "h21.component.beam.upper_general", name: "上部一般构件（湿接缝、横隔板等）" },
      ],
    } as never);
  });

  it("renders a read-only tree and node detail without management actions", async () => {
    renderPage();

    expect(await screen.findByRole("heading", { name: "单位桥梁评定树" })).toBeInTheDocument();
    // 目录树里的节点带着完整名称做 title；右侧下级列表里是按钮，两处各一个。
    expect(await screen.findByTitle("5 梁式桥上部结构")).toBeInTheDocument();
    expect(screen.queryByTitle("桥梁有效评定树")).not.toBeInTheDocument();
    expect(screen.queryByRole("heading", { name: "单位说明" })).not.toBeInTheDocument();
    // 子节点是展开根节点后另取一次的，机器忙时会晚于上面那次断言到达，所以这里要等。
    expect(await screen.findByTitle("5.1 混凝土梁式桥")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "5.1 混凝土梁式桥" })).toBeInTheDocument();
    fireEvent.click(screen.getByTitle("5.1 混凝土梁式桥"));
    expect(await screen.findByRole("heading", { name: "5.1 混凝土梁式桥" })).toBeInTheDocument();
    expect(await screen.findByTitle("5.1.1 上部承重构件、上部一般构件")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "5.1.1 上部承重构件、上部一般构件" })).toBeInTheDocument();
    expect(await screen.findByText("梁式桥")).toBeInTheDocument();
    expect(screen.queryByText("h21.bridge_type.beam")).not.toBeInTheDocument();
    expect(screen.queryByText("org.bridge.root")).not.toBeInTheDocument();
    expect(screen.queryByRole("heading", { name: "规则来源" })).not.toBeInTheDocument();
    expect(screen.queryByRole("button", { name: /新增|编辑|发布|停用|删除/ })).not.toBeInTheDocument();
  });

  it("shows named component scopes and scale rules for defect nodes", async () => {
    renderPage();

    await screen.findByRole("heading", { name: "单位桥梁评定树" });
    fireEvent.click(await screen.findByTitle("5.1 混凝土梁式桥"));
    fireEvent.click(await screen.findByTitle("5.1.1 上部承重构件、上部一般构件"));
    fireEvent.click(await screen.findByTitle("5.1.1-1 蜂窝、麻面"));

    expect(await screen.findByRole("heading", { name: "5.1.1-1 蜂窝、麻面" })).toBeInTheDocument();
    expect(await screen.findByText("上部承重构件（主梁、挂梁）")).toBeInTheDocument();
    expect(screen.getByText("上部一般构件（湿接缝、横隔板等）")).toBeInTheDocument();
    expect(screen.getByRole("heading", { name: "标度判定与扣分" })).toBeInTheDocument();
    expect(screen.queryByRole("heading", { name: "下级评定项目" })).not.toBeInTheDocument();
    expect(screen.queryByRole("heading", { name: "单位说明" })).not.toBeInTheDocument();
    expect(screen.getByText("大面积严重")).toBeInTheDocument();
  });

  it("skips the version lookup when returning to the tree in the same session", async () => {
    const lookup = ratingTreeApi.fetchRatingTreeVersions;

    const first = renderIndex();
    await screen.findByRole("heading", { name: "单位桥梁评定树" });
    expect(lookup).toHaveBeenCalledTimes(1);
    first.unmount();

    // 第二次进来直接跳到记住的版本，不再问一遍"有哪些版本、哪个是默认的"。
    renderIndex();
    await screen.findByRole("heading", { name: "单位桥梁评定树" });
    expect(lookup).toHaveBeenCalledTimes(1);
  });

  it("renders the tree before the previously expanded branches finish loading", async () => {
    const first = renderIndex();
    await screen.findByRole("heading", { name: "单位桥梁评定树" });
    fireEvent.click(screen.getByTitle("5 梁式桥上部结构"));
    await screen.findByTitle("5.1 混凝土梁式桥");
    first.unmount();

    // 重进时上次展开的子树按层拉，每层一个往返；这些往返不该挡在首屏前面。
    let releaseChildren: (() => void) | null = null;
    const blocked = new Promise<void>((resolve) => {
      releaseChildren = resolve;
    });
    vi.spyOn(ratingTreeApi, "fetchRatingTreeChildren").mockImplementation(
      async (_baseUrl, _versionId, parentId) => {
        if (parentId === group5.id) await blocked;
        return children.get(parentId ?? "root") ?? [];
      },
    );

    renderIndex();
    // 5.1 还堵在网络里，树本身已经画出来了。
    expect(await screen.findByTitle("5 梁式桥上部结构")).toBeInTheDocument();
    expect(screen.queryAllByTitle("5.1 混凝土梁式桥")).toHaveLength(0);

    releaseChildren!();
    await waitFor(() =>
      expect(screen.queryAllByTitle("5.1 混凝土梁式桥").length).toBeGreaterThan(0),
    );
  });

  it("restores search and selection after the route component is remounted", async () => {
    const first = renderPage();
    await screen.findByRole("heading", { name: "单位桥梁评定树" });
    fireEvent.change(screen.getByRole("searchbox"), { target: { value: "裂缝" } });
    fireEvent.click(screen.getByTitle("5 梁式桥上部结构"));
    await screen.findByRole("heading", { name: "5 梁式桥上部结构" });
    first.unmount();

    renderPage();
    await screen.findByRole("heading", { name: "单位桥梁评定树" });
    expect(screen.getByRole("searchbox")).toHaveValue("裂缝");
    await waitFor(() => expect(ratingTreeApi.fetchRatingTreeNode).toHaveBeenCalledWith(
      expect.any(String),
      "version-1",
      "group-5",
    ));
  });
});
