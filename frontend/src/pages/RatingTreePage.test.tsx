import { fireEvent, render, screen, waitFor } from "@testing-library/react";
import { MemoryRouter, Route, Routes } from "react-router-dom";
import { beforeEach, describe, expect, it, vi } from "vitest";

import * as ratingTreeApi from "../api/ratingTreeApi";
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
  sources: [],
};

const root: ratingTreeApi.RatingTreeNodeSummary = {
  id: "root-1",
  node_key: "org.bridge.root",
  parent_node_id: null,
  display_name: "梁式桥",
  node_type: "bridge_type",
  sort_order: 1,
  bridge_type_ids: ["h21.bridge_type.beam"],
  component_category_ids: [],
  scoring_mode: "non_scoring",
  h21_indicator_id: null,
  is_selectable: false,
  is_scoring: false,
};

const detail: ratingTreeApi.RatingTreeNode = {
  ...root,
  organization_note: "单位桥梁评定树入口。",
  allowed_scales: [],
  h21_indicator_name: null,
  h21_source_table: null,
  scale_descriptions: {},
  deduction_points: {},
  path: [{ id: root.id, node_key: root.node_key, display_name: root.display_name, node_type: root.node_type }],
  sources: [],
};

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
      async (_baseUrl, _versionId, parentId) => parentId === null ? [root] : [],
    );
    vi.spyOn(ratingTreeApi, "fetchRatingTreeNode").mockResolvedValue(detail);
    vi.spyOn(ratingTreeApi, "searchRatingTree").mockResolvedValue([]);
  });

  it("renders a read-only tree and node detail without management actions", async () => {
    renderPage();

    expect(await screen.findByRole("heading", { name: "单位桥梁评定树" })).toBeInTheDocument();
    fireEvent.click(screen.getByRole("button", { name: "梁式桥" }));
    expect(await screen.findByText("单位桥梁评定树入口。")).toBeInTheDocument();
    expect(screen.queryByRole("button", { name: /新增|编辑|发布|停用|删除/ })).not.toBeInTheDocument();
  });

  it("restores search and selection after the route component is remounted", async () => {
    const first = renderPage();
    await screen.findByRole("heading", { name: "单位桥梁评定树" });
    fireEvent.change(screen.getByRole("searchbox"), { target: { value: "裂缝" } });
    fireEvent.click(screen.getByRole("button", { name: "梁式桥" }));
    await screen.findByText("单位桥梁评定树入口。");
    first.unmount();

    renderPage();
    await screen.findByRole("heading", { name: "单位桥梁评定树" });
    expect(screen.getByRole("searchbox")).toHaveValue("裂缝");
    await waitFor(() => expect(ratingTreeApi.fetchRatingTreeNode).toHaveBeenCalledWith(
      expect.any(String),
      "version-1",
      "root-1",
    ));
  });
});
