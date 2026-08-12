import { render, screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it, vi } from "vitest";

import { fetchLatestComponentInventory } from "../../api/componentInventoryApi";
import {
  bindComponent,
  bindInspectionRatingTree,
  fetchComponentBinding,
  markComponentMissing,
} from "../../api/importBindingApi";
import { fetchRatingTreeVersions } from "../../api/ratingTreeApi";
import { ComponentBindingWorkspace } from "./ComponentBindingWorkspace";

vi.mock("../../api/importBindingApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../../api/importBindingApi")>();
  return {
    ...original,
    fetchComponentBinding: vi.fn(),
    bindComponent: vi.fn(),
    bindInspectionRatingTree: vi.fn(),
    markComponentMissing: vi.fn(),
    clearComponentBinding: vi.fn(),
  };
});

vi.mock("../../api/ratingTreeApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../../api/ratingTreeApi")>();
  return { ...original, fetchRatingTreeVersions: vi.fn() };
});

vi.mock("../../api/componentInventoryApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../../api/componentInventoryApi")>();
  return { ...original, fetchLatestComponentInventory: vi.fn() };
});

const inventory = {
  id: "rev-1",
  bridge_id: "bridge-1",
  revision_number: 1,
  status: "已确认" as const,
  baseline_revision_id: null,
  confirmed_at: null,
  entries: [
    {
      id: "entry-1",
      bridge_component_id: "c1",
      component_number: "1-1#梁",
      site_name: "空心板",
      site_component_type: "空心板",
      span_or_location: null,
      is_active: true,
      deactivated_at: null,
      deactivation_reason: null,
      sort_order: 1,
      remarks: null,
      is_referenced: false,
      mappings: [
        {
          id: "m1",
          standard_package_id: "p1",
          standard_bridge_type_id: "h21.bridge_type.beam",
          standard_component_category_id: "h21.component.beam.upper_bearing",
          structure_part: "superstructure" as const,
          mapping_source: "规范模板",
          confirmation_status: "已确认",
          is_active: true,
        },
      ],
    },
  ],
};

function overview(status: "unmatched" | "bound" | "missing") {
  return {
    inventory_confirmed: true,
    groups: [
      {
        part_name: "上部承重构件",
        total: 1,
        bound: status === "bound" ? 1 : 0,
        unmatched: status === "unmatched" ? 1 : 0,
        ambiguous: 0,
        missing: status === "missing" ? 1 : 0,
        rows: [
          {
            component_number: "1-1#梁",
            defect_count: 3,
            status,
            bridge_component_id: status === "bound" ? "c1" : null,
            candidate_component_ids: status === "unmatched" ? ["c1"] : [],
          },
        ],
      },
    ],
  };
}

describe("ComponentBindingWorkspace", () => {
  beforeEach(() => {
    vi.resetAllMocks();
    vi.mocked(fetchLatestComponentInventory).mockResolvedValue(inventory);
    vi.mocked(fetchComponentBinding).mockResolvedValue(overview("unmatched"));
    vi.mocked(fetchRatingTreeVersions).mockResolvedValue([
      {
        id: "tree-1",
        tree_code: "org.bridge.root",
        tree_name: "单位桥梁有效评定树",
        package_version: "1.0.2",
        tree_content_checksum: "sha256:test",
        status: "published",
        published_at: "2026-07-30T00:00:00Z",
        is_default: true,
        h21_package_version: "1.0.3",
        maintenance_package_version: "1.0.0",
      },
    ]);
  });

  it("renders grouped rows with reference counts", async () => {
    render(<ComponentBindingWorkspace importId="i1" bridgeId="bridge-1" />);
    expect(await screen.findByText("上部承重构件")).toBeInTheDocument();
    expect(screen.getByText("引用 3 条")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "待处理 1" })).toBeInTheDocument();
  });

  it("binds the selected published rating tree for the inspection year", async () => {
    vi.mocked(bindInspectionRatingTree).mockResolvedValue({
      ...overview("unmatched"),
      rating_tree: {
        version_id: "tree-1",
        tree_name: "单位桥梁有效评定树",
        package_version: "1.0.2",
        h21_package_version: "1.0.3",
        maintenance_package_version: "1.0.0",
      },
    });
    render(<ComponentBindingWorkspace importId="i1" bridgeId="bridge-1" />);

    const button = await screen.findByRole("button", { name: "绑定评定树" });
    // 评定树版本列表是独立于构件行的另一个请求（故意不阻塞首屏），按钮出现时
    // 默认选中值可能还没回来，必须等它落定再断言，否则整套并行跑时会偶发失败。
    await waitFor(() =>
      expect(screen.getByLabelText("选择年度评定树")).toHaveValue("tree-1")
    );
    await userEvent.click(button);

    await waitFor(() =>
      expect(bindInspectionRatingTree).toHaveBeenCalledWith(
        "http://127.0.0.1:18080",
        "i1",
        "tree-1"
      )
    );
    expect(await screen.findByText("评定树已绑定。")).toBeInTheDocument();
    expect(screen.getByText("单位桥梁有效评定树 1.0.2")).toBeInTheDocument();
  });

  // 已标记缺失需能单独查看：核对"台账确实没有"是一次独立的复核动作，
  // 混在"已处理"里看不见。
  it("filters missing rows on their own", async () => {
    vi.mocked(fetchComponentBinding).mockResolvedValue(overview("missing"));
    render(<ComponentBindingWorkspace importId="i1" bridgeId="bridge-1" />);

    // 默认只看待处理，已标记缺失的行不在其中。
    expect(await screen.findByText("全部构件已处理完毕。")).toBeInTheDocument();
    expect(screen.queryByText("已标记缺失")).not.toBeInTheDocument();

    await userEvent.click(screen.getByRole("button", { name: "已标记缺失 1" }));
    expect(screen.getByText("已标记缺失")).toBeInTheDocument();

    // 切到"已绑定"应为空，且不得再说"全部处理完毕"。
    await userEvent.click(screen.getByRole("button", { name: "已绑定 0" }));
    expect(screen.getByText("该状态下没有构件。")).toBeInTheDocument();
  });

  // 已处理的行占绝大多数，默认收起才能让待处理的凸显出来；要看时再展开。
  it("hides a row once it is bound and reveals it on demand", async () => {
    vi.mocked(bindComponent).mockResolvedValue(overview("bound"));
    render(<ComponentBindingWorkspace importId="i1" bridgeId="bridge-1" />);

    await userEvent.selectOptions(
      await screen.findByLabelText("为 1-1#梁 选择实际构件"), "c1");

    await waitFor(() => expect(bindComponent).toHaveBeenCalledWith("http://127.0.0.1:18080", "i1", {
      part_name: "上部承重构件",
      component_number: "1-1#梁",
      bridge_component_id: "c1",
    }));

    // 绑定后该行从默认视图消失，只剩"全部已处理"提示。
    expect(await screen.findByText("全部构件已处理完毕。")).toBeInTheDocument();
    expect(screen.queryByText("已绑定")).not.toBeInTheDocument();

    await userEvent.click(screen.getByRole("button", { name: "已绑定 1" }));
    expect(screen.getByText("已绑定")).toBeInTheDocument();
  });

  it("marks a row missing and enables entering review when all resolved", async () => {
    vi.mocked(markComponentMissing).mockResolvedValue(overview("missing"));
    const onEnterReview = vi.fn();
    render(<ComponentBindingWorkspace importId="i1" bridgeId="bridge-1" onEnterReview={onEnterReview} />);

    await userEvent.click(await screen.findByLabelText("标记缺失 1-1#梁"));

    await waitFor(() => expect(markComponentMissing).toHaveBeenCalled());
    const enter = await screen.findByRole("button", { name: "全部绑定完成，进入校对" });
    expect(enter).toBeEnabled();
    await userEvent.click(enter);
    expect(onEnterReview).toHaveBeenCalled();
  });

  // 绑定类写操作都改后端草稿；不上报的话校对分区会一直显示改动前的病害，
  // 保存时还会把旧内容盖回去。
  it("reports every draft-mutating operation so the review draft can be refreshed", async () => {
    vi.mocked(markComponentMissing).mockResolvedValue(overview("missing"));
    const onDraftInvalidated = vi.fn();
    render(
      <ComponentBindingWorkspace
        importId="i1"
        bridgeId="bridge-1"
        onDraftInvalidated={onDraftInvalidated}
      />,
    );

    // 首屏加载只是读取，不算改写。
    await screen.findByLabelText("标记缺失 1-1#梁");
    expect(onDraftInvalidated).not.toHaveBeenCalled();

    await userEvent.click(screen.getByLabelText("标记缺失 1-1#梁"));

    await waitFor(() => expect(onDraftInvalidated).toHaveBeenCalledTimes(1));
  });
});
