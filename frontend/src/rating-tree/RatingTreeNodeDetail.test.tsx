import { render, screen, within } from "@testing-library/react";
import { describe, expect, it, vi } from "vitest";

import type { RatingTreeNode, RatingTreeVersion } from "../api/ratingTreeApi";
import { RatingTreeNodeDetail } from "./RatingTreeNodeDetail";

describe("RatingTreeNodeDetail", () => {
  it("explains source water scales and H21 deduction reference", () => {
    const version = {
      id: "tree-201",
      tree_code: "organization-bridge",
      tree_name: "单位桥梁评定树",
      package_version: "2.0.1",
      tree_content_checksum: "sha256:test",
      status: "published",
      published_at: "2026-08-08T00:00:00Z",
      is_default: true,
      contract_version: 1,
      node_count: 500,
      sources: [],
    } satisfies RatingTreeVersion;
    const node = {
      id: "water",
      node_key: "org.bridge.defect.9_1_1_10",
      parent_node_id: "pier",
      display_number: "9.1.1-10",
      display_name: "水损害",
      node_type: "defect",
      sort_order: 100,
      bridge_type_ids: [],
      component_category_ids: [],
      scoring_mode: "reference_h21",
      h21_indicator_id: "h21.defect.9_1_1_5",
      is_selectable: true,
      is_scoring: true,
      organization_note: "判定描述来自来源软件，扣分值引用 H21 对应标度曲线。",
      allowed_scales: [1, 2, 3, 4],
      h21_indicator_name: "混凝土碳化、腐蚀",
      h21_source_table: "9.1.1-5",
      uses_source_scale_descriptions: true,
      scale_descriptions: {
        "1": "少量；范围＜5%",
        "2": "局部渗水泛碱；范围＜10%",
        "3": "渗水、水蚀严重；范围＜30%",
        "4": "—",
      },
      deduction_points: { "1": 0, "2": 25, "3": 40, "4": 50 },
      path: [],
      sources: [],
    } satisfies RatingTreeNode;

    render(
      <RatingTreeNodeDetail
        version={version}
        node={node}
        children={[]}
        catalog={null}
        loading={false}
        childrenLoading={false}
        onSelectChild={vi.fn()}
      />,
    );

    expect(screen.getByText("参与评分")).toBeInTheDocument();
    expect(screen.getByText("参照 H21 扣分")).toBeInTheDocument();
    expect(screen.getByText("H21 扣分参照")).toBeInTheDocument();
    expect(screen.getByText("表 9.1.1-5")).toBeInTheDocument();
    expect(screen.getByText(
      "判定来源：来源软件 9.1.1-10；扣分参照：H21 9.1.1-5",
    )).toBeInTheDocument();
    const scaleTwo = screen.getByText("局部渗水泛碱；范围＜10%").closest("tr");
    expect(scaleTwo).not.toBeNull();
    expect(within(scaleTwo!).getByText("25")).toBeInTheDocument();
  });
});
