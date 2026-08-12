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
  h21_package_version: "1.0.2",
  is_default: true,
  sources: [],
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
  [group511.id, []],
]);

const details = new Map<string, ratingTreeApi.RatingTreeNode>([
  [root.id, detailFor(root)],
  [group5.id, detailFor(group5)],
  [group51.id, detailFor(group51)],
  [group511.id, detailFor(group511)],
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

describe("RatingTreePage", () => {
  beforeEach(() => {
    vi.restoreAllMocks();
    clearRatingTreeViewStateForTests();
    vi.spyOn(ratingTreeApi, "fetchRatingTreeVersion").mockResolvedValue(version);
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
      component_categories: [],
    } as never);
  });

  it("renders a read-only tree and node detail without management actions", async () => {
    renderPage();

    expect(await screen.findByRole("heading", { name: "单位桥梁评定树" })).toBeInTheDocument();
    expect(await screen.findByRole("button", { name: "5 梁式桥上部结构" })).toBeInTheDocument();
    expect(screen.queryByRole("button", { name: "桥梁有效评定树" })).not.toBeInTheDocument();
    expect(await screen.findByText("梁式桥上部结构说明。")).toBeInTheDocument();
    expect(screen.getAllByRole("button", { name: "5.1 混凝土梁式桥" })).toHaveLength(2);
    fireEvent.click(screen.getAllByRole("button", { name: "5.1 混凝土梁式桥" })[0]);
    expect(await screen.findByRole("heading", { name: "5.1 混凝土梁式桥" })).toBeInTheDocument();
    expect(screen.getAllByRole("button", {
      name: "5.1.1 上部承重构件、上部一般构件",
    })).toHaveLength(2);
    expect(await screen.findByText("全部桥型（1 类）")).toBeInTheDocument();
    expect(screen.queryByText("h21.bridge_type.beam")).not.toBeInTheDocument();
    expect(screen.queryByText("org.bridge.root")).not.toBeInTheDocument();
    expect(screen.queryByRole("heading", { name: "规则来源" })).not.toBeInTheDocument();
    expect(screen.queryByRole("button", { name: /新增|编辑|发布|停用|删除/ })).not.toBeInTheDocument();
  });

  it("restores search and selection after the route component is remounted", async () => {
    const first = renderPage();
    await screen.findByRole("heading", { name: "单位桥梁评定树" });
    fireEvent.change(screen.getByRole("searchbox"), { target: { value: "裂缝" } });
    fireEvent.click(screen.getByRole("button", { name: "5 梁式桥上部结构" }));
    await screen.findByText("梁式桥上部结构说明。");
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
