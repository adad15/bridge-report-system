import { render, screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it, vi } from "vitest";

import { fetchLatestComponentInventory } from "../../api/componentInventoryApi";
import {
  bindComponent,
  bindInspectionRatingTree,
  fetchComponentBinding,
  markComponentMissing,
  previewComponentRangeSplit,
  INVENTORY_REVISION_CHANGED,
  type ComponentBindingOverview,
} from "../../api/importBindingApi";
import { ApiError } from "../../api/apiClient";
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
    previewComponentRangeSplit: vi.fn(),
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

function overview(status: "unmatched" | "bound" | "missing"): ComponentBindingOverview {
  return {
    inventory_confirmed: true,
    inventory_revision_id: "rev-1",
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
            split_eligible: false,
            split_expanded_count: null,
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
        "tree-1",
        "rev-1"
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
    }, "rev-1"));

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

  // 写操作要声明依据哪个台账版本；漏传的话后端会自己挑一个，用户看到的候选就和
  // 校验用的台账对不上了。
  it("sends the overview revision id with every write", async () => {
    vi.mocked(markComponentMissing).mockResolvedValue(overview("missing"));
    render(<ComponentBindingWorkspace importId="i1" bridgeId="bridge-1" />);

    await userEvent.click(await screen.findByLabelText("标记缺失 1-1#梁"));

    await waitFor(() => expect(markComponentMissing).toHaveBeenCalled());
    expect(vi.mocked(markComponentMissing).mock.calls[0][3]).toBe("rev-1");
  });

  // 版本变了只弹一条错误是不够的：候选已经过期，用户会对着旧数据反复重试。
  it("refreshes the overview when the backend reports the revision changed", async () => {
    vi.mocked(markComponentMissing).mockRejectedValue(
      new ApiError(INVENTORY_REVISION_CHANGED, "构件台账版本已变化，请刷新后重试。"),
    );
    const refreshed = overview("unmatched");
    refreshed.inventory_revision_id = "rev-2";
    vi.mocked(fetchComponentBinding)
      .mockResolvedValueOnce(overview("unmatched"))
      .mockResolvedValueOnce(refreshed);

    render(<ComponentBindingWorkspace importId="i1" bridgeId="bridge-1" />);
    await userEvent.click(await screen.findByLabelText("标记缺失 1-1#梁"));

    // 概览被重新拉取，并且明确告诉用户发生了什么。
    await waitFor(() => expect(fetchComponentBinding).toHaveBeenCalledTimes(2));
    expect(await screen.findByText(/台账版本已变化/)).toBeInTheDocument();

    // 刷新之后的写操作必须带上新版本。
    vi.mocked(markComponentMissing).mockResolvedValue(refreshed);
    await userEvent.click(screen.getByLabelText("标记缺失 1-1#梁"));
    await waitFor(() => expect(markComponentMissing).toHaveBeenCalledTimes(2));
    expect(vi.mocked(markComponentMissing).mock.calls[1][3]).toBe("rev-2");
  });

  it("opens the split dialog before the preview request finishes and ignores a late result after close", async () => {
    const splitOverview = overview("unmatched");
    splitOverview.groups[0].rows[0].component_number = "1-1#梁~1-25#梁";
    splitOverview.groups[0].rows[0].split_eligible = true;
    splitOverview.groups[0].rows[0].split_expanded_count = 25;
    vi.mocked(fetchComponentBinding).mockResolvedValue(splitOverview);

    let resolvePreview!: (value: Awaited<ReturnType<typeof previewComponentRangeSplit>>) => void;
    vi.mocked(previewComponentRangeSplit).mockReturnValue(new Promise((resolve) => {
      resolvePreview = resolve;
    }));

    render(<ComponentBindingWorkspace importId="i1" bridgeId="bridge-1" />);
    await userEvent.click(await screen.findByLabelText("选择拆分 1-1#梁~1-25#梁"));
    await userEvent.click(screen.getByRole("button", { name: /拆分构件/ }));

    expect(screen.getByRole("dialog", { name: "拆分构件范围" })).toBeInTheDocument();
    expect(screen.getByText("正在计算拆分影响…")).toBeInTheDocument();
    await userEvent.click(screen.getByRole("button", { name: "取消" }));
    expect(screen.queryByRole("dialog", { name: "拆分构件范围" })).not.toBeInTheDocument();

    resolvePreview({
      items: [],
      totals: {
        selected_range_count: 1,
        source_defect_count: 1,
        result_defect_count: 25,
        result_photo_count: 0,
        bound_count: 25,
        ambiguous_count: 0,
        unmatched_count: 0,
      },
      impact_token: "sha256:late",
    });
    await Promise.resolve();
    expect(screen.queryByRole("dialog", { name: "拆分构件范围" })).not.toBeInTheDocument();
  });
});
