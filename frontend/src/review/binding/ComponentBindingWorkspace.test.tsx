import { render, screen, waitFor, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it, vi } from "vitest";

import { searchInventoryEntries } from "../../api/componentInventoryApi";
import { bindInspectionRatingTree } from "../../api/inspectionRatingTreeApi";
import {
  applyComponentResolution,
  createResolutionPlan,
  fetchResolutionWorkspace,
  INVENTORY_REVISION_CHANGED,
  type ResolutionWorkspace,
  type WorkspaceComponentGroup,
  type WorkspaceComponentSummary,
} from "../../api/resolutionApi";
import { ApiError } from "../../api/apiClient";
import { fetchRatingTreeVersions } from "../../api/ratingTreeApi";
import { ComponentBindingWorkspace } from "./ComponentBindingWorkspace";

vi.mock("../../api/inspectionRatingTreeApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../../api/inspectionRatingTreeApi")>();
  return { ...original, bindInspectionRatingTree: vi.fn() };
});

// 5.0：绑定改走解析工作区。写操作只回受影响对象，页面成功后重取整份工作区，
// 所以这些用例里 fetchResolutionWorkspace 会被调用多次。
vi.mock("../../api/resolutionApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../../api/resolutionApi")>();
  return {
    ...original,
    fetchResolutionWorkspace: vi.fn(),
    applyComponentResolution: vi.fn(),
    createResolutionPlan: vi.fn(),
    applyResolutionPlan: vi.fn(),
  };
});

vi.mock("../../api/ratingTreeApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../../api/ratingTreeApi")>();
  return { ...original, fetchRatingTreeVersions: vi.fn() };
});

vi.mock("../../api/componentInventoryApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../../api/componentInventoryApi")>();
  return { ...original, searchInventoryEntries: vi.fn() };
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

function summary(id: string, number = "1-1#梁"): WorkspaceComponentSummary {
  return {
    bridge_component_id: id,
    component_number: number,
    site_component_type: "空心板",
    site_name: "空心板",
    standard_component_category_id: "h21.component.beam.upper_bearing",
    standard_bridge_type_id: "h21.bridge_type.beam",
  };
}

function group(overrides: Partial<WorkspaceComponentGroup> = {}): WorkspaceComponentGroup {
  return {
    group_id: "g1",
    source_component_name: "上部承重构件",
    source_component_number: "1-1#梁",
    normalized_component_number: "1-1#梁",
    resolution_mode: "single",
    status: "unresolved",
    match_method: null,
    inventory_revision_id: "rev-1",
    version: 1,
    ambiguous: false,
    split_eligible: false,
    split_expanded_count: null,
    side_pair_option: null,
    targets: [],
    candidates: [],
    members: [
      { member_id: "m1", source_candidate_id: "d1", source_order: 0, instances: [] },
      { member_id: "m2", source_candidate_id: "d2", source_order: 1, instances: [] },
      { member_id: "m3", source_candidate_id: "d3", source_order: 2, instances: [] },
    ],
    allowed_actions: ["bind", "mark_missing"],
    blocked_reasons: [],
    ...overrides,
  };
}

function workspaceOf(groups: WorkspaceComponentGroup[]): ResolutionWorkspace {
  return {
    import_record_id: "i1",
    bridge_id: "bridge-1",
    draft_version: 1,
    inventory_confirmed: true,
    inventory_revision_id: "rev-1",
    rating_tree: null,
    groups,
    parts: [],
    progress: {
      group_count: groups.length, bound_count: 0, unresolved_count: 0, ambiguous_count: 0,
      missing_count: 0, instance_count: 0, active_instance_count: 0,
      rating_matched_count: 0, rating_unresolved_count: 0, rating_missing_count: 0,
    },
  };
}

function overview(status: "unmatched" | "bound" | "missing"): ResolutionWorkspace {
  return workspaceOf([group({
    status: status === "unmatched" ? "unresolved" : status === "bound" ? "bound" : "missing",
    // 候选带完整展示信息，否则这条用例覆盖不到下拉里的候选项。
    candidates: status === "unmatched" ? [summary("c1")] : [],
    targets: status === "bound" ? [summary("c1")] : [],
    match_method: status === "bound" ? "manual" : null,
  })]);
}

// 百股大桥那一行：报告写"两侧护栏"，台账里是左侧栏杆/右侧栏杆两件。
function railingOverview(sidePair = true): ResolutionWorkspace {
  return workspaceOf([group({
    group_id: "g-railing",
    source_component_name: "栏杆、护栏",
    source_component_number: "两侧护栏",
    normalized_component_number: "两侧护栏",
    members: [{ member_id: "m1", source_candidate_id: "d1", source_order: 0, instances: [] }],
    side_pair_option: sidePair
      ? {
          label: "两侧 · 左侧栏杆 + 右侧栏杆",
          bridge_component_ids: ["railing-left", "railing-right"],
        }
      : null,
  })]);
}

describe("ComponentBindingWorkspace", () => {
  beforeEach(() => {
    vi.resetAllMocks();
    vi.mocked(searchInventoryEntries).mockResolvedValue({ total: 0, entries: [] });
    vi.mocked(fetchResolutionWorkspace).mockResolvedValue(overview("unmatched"));
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
    render(<ComponentBindingWorkspace importId="i1" bridgeId="bridge-1" lockToken="lock-1" />);
    expect(await screen.findByText("上部承重构件")).toBeInTheDocument();
    expect(screen.getByText("引用 3 条")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "待处理 1" })).toBeInTheDocument();
  });

  it("binds the selected published rating tree for the inspection year", async () => {
    // 绑评定树仍走既有接口；页面只看它成功与否，随后重取工作区拿新的树信息。
    vi.mocked(bindInspectionRatingTree).mockResolvedValue({} as never);
    const bound = overview("unmatched");
    bound.rating_tree = {
      version_id: "tree-1",
      tree_name: "单位桥梁有效评定树",
      package_version: "1.0.2",
      h21_package_version: "1.0.3",
      maintenance_package_version: "1.0.0",
    };
    vi.mocked(fetchResolutionWorkspace)
      .mockResolvedValueOnce(overview("unmatched"))
      .mockResolvedValue(bound);
    render(<ComponentBindingWorkspace importId="i1" bridgeId="bridge-1" lockToken="lock-1" />);

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
        "rev-1",
        "lock-1"
      )
    );
    expect(await screen.findByText("评定树已绑定。")).toBeInTheDocument();
    expect(screen.getByText("单位桥梁有效评定树 1.0.2")).toBeInTheDocument();
  });

  // 已标记缺失需能单独查看：核对"台账确实没有"是一次独立的复核动作，
  // 混在"已处理"里看不见。
  it("filters missing rows on their own", async () => {
    vi.mocked(fetchResolutionWorkspace).mockResolvedValue(overview("missing"));
    render(<ComponentBindingWorkspace importId="i1" bridgeId="bridge-1" lockToken="lock-1" />);

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
    vi.mocked(applyComponentResolution).mockResolvedValue(
      { affected_groups: [], progress: overview("bound").progress });
    // 写成功后页面重取工作区，第二次取到的是已绑定的样子。
    vi.mocked(fetchResolutionWorkspace)
      .mockResolvedValueOnce(overview("unmatched"))
      .mockResolvedValue(overview("bound"));
    render(<ComponentBindingWorkspace importId="i1" bridgeId="bridge-1" lockToken="lock-1" />);

    await userEvent.selectOptions(
      await screen.findByLabelText("为 1-1#梁 选择实际构件"), "c1");

    await waitFor(() => expect(applyComponentResolution).toHaveBeenCalledWith(
      "http://127.0.0.1:18080", "i1", "g1",
      {
        expected_version: 1,
        action: "bind",
        targets: [{ bridge_component_id: "c1", target_role: "primary" }],
        expected_inventory_revision_id: "rev-1",
      },
      "lock-1"));

    // 绑定后该行从默认视图消失，只剩"全部已处理"提示。
    expect(await screen.findByText("全部构件已处理完毕。")).toBeInTheDocument();
    expect(screen.queryByText("已绑定")).not.toBeInTheDocument();

    await userEvent.click(screen.getByRole("button", { name: "已绑定 1" }));
    expect(screen.getByText("已绑定")).toBeInTheDocument();
  });

  it("marks a row missing and enables entering review when all resolved", async () => {
    vi.mocked(applyComponentResolution).mockResolvedValue(
      { affected_groups: [], progress: overview("missing").progress });
    vi.mocked(fetchResolutionWorkspace)
      .mockResolvedValueOnce(overview("unmatched"))
      .mockResolvedValue(overview("missing"));
    const onEnterReview = vi.fn();
    render(<ComponentBindingWorkspace importId="i1" bridgeId="bridge-1" lockToken="lock-1" onEnterReview={onEnterReview} />);

    await userEvent.click(await screen.findByLabelText("标记缺失 1-1#梁"));

    await waitFor(() => expect(applyComponentResolution).toHaveBeenCalled());
    const enter = await screen.findByRole("button", { name: "全部绑定完成，进入校对" });
    expect(enter).toBeEnabled();
    await userEvent.click(enter);
    expect(onEnterReview).toHaveBeenCalled();
  });

  // 绑定类写操作都改后端草稿；不上报的话校对分区会一直显示改动前的病害，
  // 保存时还会把旧内容盖回去。
  it("reports every draft-mutating operation so the review draft can be refreshed", async () => {
    vi.mocked(applyComponentResolution).mockResolvedValue(
      { affected_groups: [], progress: overview("missing").progress });
    vi.mocked(fetchResolutionWorkspace)
      .mockResolvedValueOnce(overview("unmatched"))
      .mockResolvedValue(overview("missing"));
    const onDraftInvalidated = vi.fn();
    render(
      <ComponentBindingWorkspace
        importId="i1"
        bridgeId="bridge-1" lockToken="lock-1"
        onDraftInvalidated={onDraftInvalidated}
      />,
    );

    // 首屏加载只是读取，不算改写。
    await screen.findByLabelText("标记缺失 1-1#梁");
    expect(onDraftInvalidated).not.toHaveBeenCalled();

    await userEvent.click(screen.getByLabelText("标记缺失 1-1#梁"));

    await waitFor(() => expect(onDraftInvalidated).toHaveBeenCalledTimes(1));
  });

  // 一条"两侧护栏"若只绑左侧，右侧会留在满分，栏杆部件分算出 76 而非 56。
  // 选项必须摆在候选之上，让人先看见"两侧"这条路。
  it("offers the two-sided option above the individual components", async () => {
    vi.mocked(fetchResolutionWorkspace).mockResolvedValue(railingOverview());
    render(<ComponentBindingWorkspace importId="i1" bridgeId="bridge-1" lockToken="lock-1" />);

    const select = await screen.findByLabelText("为 两侧护栏 选择实际构件");
    const options = within(select).getAllByRole("option").map((o) => o.textContent);
    expect(options[1]).toBe("两侧 · 左侧栏杆 + 右侧栏杆");
  });

  it("binds both components at once and refreshes the review draft", async () => {
    vi.mocked(fetchResolutionWorkspace).mockResolvedValue(railingOverview());
    vi.mocked(applyComponentResolution).mockResolvedValue(
      { affected_groups: [], progress: railingOverview(false).progress });
    vi.mocked(fetchResolutionWorkspace)
      .mockResolvedValueOnce(railingOverview())
      .mockResolvedValue(railingOverview(false));
    const onDraftInvalidated = vi.fn();
    render(
      <ComponentBindingWorkspace
        importId="i1"
        bridgeId="bridge-1"
        lockToken="lock-1"
        onDraftInvalidated={onDraftInvalidated}
      />,
    );

    const select = await screen.findByLabelText("为 两侧护栏 选择实际构件");
    await userEvent.selectOptions(select, "__side_pair__");

    await waitFor(() => expect(applyComponentResolution).toHaveBeenCalledTimes(1));
    // 两侧整体绑定：一次请求两个目标，靠 target_role 区分左右。
    expect(applyComponentResolution).toHaveBeenCalledWith(
      expect.anything(),
      "i1",
      "g-railing",
      {
        expected_version: 1,
        action: "bind",
        targets: [
          { bridge_component_id: "railing-left", target_role: "left" },
          { bridge_component_id: "railing-right", target_role: "right" },
        ],
        expected_inventory_revision_id: "rev-1",
      },
      "lock-1",
    );
    // 单条绑定不该被误触发：哨兵值不是构件 id。
    // 单条绑定不该被误触发：哨兵值不是构件 id，只应发出一次两目标绑定。
    expect(applyComponentResolution).toHaveBeenCalledTimes(1);
    // 这次写会增删病害，草稿必须重取。
    await waitFor(() => expect(onDraftInvalidated).toHaveBeenCalledTimes(1));
  });

  it("omits the two-sided option when the backend reports no pair", async () => {
    vi.mocked(fetchResolutionWorkspace).mockResolvedValue(railingOverview(false));
    render(<ComponentBindingWorkspace importId="i1" bridgeId="bridge-1" lockToken="lock-1" />);

    const select = await screen.findByLabelText("为 两侧护栏 选择实际构件");
    expect(within(select).queryByText(/^两侧 · /)).not.toBeInTheDocument();
  });

  // 首屏只等概览。此前并排拉一份完整台账（约 3.4 MB），两个都回来才渲染。
  // 评定树版本列表是另一个非阻塞请求，所以不能笼统断言"只发一次请求"。
  it("loads only the overview on first paint", async () => {
    render(<ComponentBindingWorkspace importId="i1" bridgeId="bridge-1" lockToken="lock-1" />);
    expect(await screen.findByText("上部承重构件")).toBeInTheDocument();

    expect(fetchResolutionWorkspace).toHaveBeenCalledTimes(1);
    expect(fetchRatingTreeVersions).toHaveBeenCalledTimes(1);
  });

  // 概览自带候选的展示信息，搜索之前就该看得见。
  it("shows overview candidates before any search", async () => {
    render(<ComponentBindingWorkspace importId="i1" bridgeId="bridge-1" lockToken="lock-1" />);
    const select = await screen.findByLabelText("为 1-1#梁 选择实际构件");

    expect(within(select).getByRole("option", { name: /候选 · 1-1#梁/ })).toBeInTheDocument();
    expect(searchInventoryEntries).not.toHaveBeenCalled();
  });

  it("searches the server after a pause and keeps candidates in front", async () => {
    vi.mocked(searchInventoryEntries).mockResolvedValue({
      total: 1,
      entries: [{
        id: "entry-c9", bridge_component_id: "c9", component_number: "9-9#梁",
        site_name: "边跨空心板", site_component_type: "空心板", span_or_location: null,
        is_active: true, deactivated_at: null, deactivation_reason: null,
        sort_order: 2, remarks: null, is_referenced: false, mappings: [], position: 1,
      }],
    });
    render(<ComponentBindingWorkspace importId="i1" bridgeId="bridge-1" lockToken="lock-1" />);
    await userEvent.type(await screen.findByLabelText("搜索实际构件 1-1#梁"), "9-9");

    await waitFor(() => expect(searchInventoryEntries).toHaveBeenCalled());
    const [, revisionId, keyword, limit, , bindingEligible] =
      vi.mocked(searchInventoryEntries).mock.calls[0];
    expect(revisionId).toBe("rev-1");
    expect(keyword).toBe("9-9");
    expect(limit).toBe(20);
    expect(bindingEligible).toBe(true);

    const select = screen.getByLabelText("为 1-1#梁 选择实际构件");
    await waitFor(() =>
      expect(within(select).getByRole("option", { name: /9-9#梁/ })).toBeInTheDocument());
    // 候选在前，搜索结果追加在后，且候选不占用 limit 名额。
    const options = within(select).getAllByRole("option").map((o) => o.textContent ?? "");
    expect(options[1]).toContain("候选 · ");
    expect(options[2]).toContain("9-9#梁");
  });

  // 搜索失败不该把概览带来的候选也一起清掉。
  it("keeps candidates when the search request fails", async () => {
    vi.mocked(searchInventoryEntries).mockRejectedValue(new Error("boom"));
    render(<ComponentBindingWorkspace importId="i1" bridgeId="bridge-1" lockToken="lock-1" />);
    await userEvent.type(await screen.findByLabelText("搜索实际构件 1-1#梁"), "9-9");

    await waitFor(() => expect(searchInventoryEntries).toHaveBeenCalled());
    const select = screen.getByLabelText("为 1-1#梁 选择实际构件");
    expect(within(select).getByRole("option", { name: /候选 · 1-1#梁/ })).toBeInTheDocument();
  });

  // 写操作要声明依据哪个台账版本；漏传的话后端会自己挑一个，用户看到的候选就和
  // 校验用的台账对不上了。
  it("sends the overview revision id with every write", async () => {
    vi.mocked(applyComponentResolution).mockResolvedValue(
      { affected_groups: [], progress: overview("missing").progress });
    vi.mocked(fetchResolutionWorkspace)
      .mockResolvedValueOnce(overview("unmatched"))
      .mockResolvedValue(overview("missing"));
    render(<ComponentBindingWorkspace importId="i1" bridgeId="bridge-1" lockToken="lock-1" />);

    await userEvent.click(await screen.findByLabelText("标记缺失 1-1#梁"));

    await waitFor(() => expect(applyComponentResolution).toHaveBeenCalled());
    expect(vi.mocked(applyComponentResolution).mock.calls[0][3])
      .toMatchObject({ expected_inventory_revision_id: "rev-1" });
  });

  // 版本变了只弹一条错误是不够的：候选已经过期，用户会对着旧数据反复重试。
  it("refreshes the overview when the backend reports the revision changed", async () => {
    vi.mocked(applyComponentResolution).mockRejectedValue(
      new ApiError(INVENTORY_REVISION_CHANGED, "构件台账版本已变化，请刷新后重试。"),
    );
    const refreshed = overview("unmatched");
    refreshed.inventory_revision_id = "rev-2";
    vi.mocked(fetchResolutionWorkspace)
      .mockResolvedValueOnce(overview("unmatched"))
      .mockResolvedValueOnce(refreshed);

    render(<ComponentBindingWorkspace importId="i1" bridgeId="bridge-1" lockToken="lock-1" />);
    await userEvent.click(await screen.findByLabelText("标记缺失 1-1#梁"));

    // 概览被重新拉取，并且明确告诉用户发生了什么。
    await waitFor(() => expect(fetchResolutionWorkspace).toHaveBeenCalledTimes(2));
    expect(await screen.findByText(/台账版本已变化/)).toBeInTheDocument();

    // 刷新之后的写操作必须带上新版本。
    vi.mocked(applyComponentResolution).mockResolvedValue(
      { affected_groups: [], progress: refreshed.progress });
    await userEvent.click(screen.getByLabelText("标记缺失 1-1#梁"));
    await waitFor(() => expect(applyComponentResolution).toHaveBeenCalledTimes(2));
    expect(vi.mocked(applyComponentResolution).mock.calls[1][3])
      .toMatchObject({ expected_inventory_revision_id: "rev-2" });
  });

  it("opens the split dialog before the preview request finishes and ignores a late result after close", async () => {
    const splitOverview = workspaceOf([group({
      source_component_number: "1-1#梁~1-25#梁",
      normalized_component_number: "1-1#梁~1-25#梁",
      split_eligible: true,
      split_expanded_count: 25,
    })]);
    vi.mocked(fetchResolutionWorkspace).mockResolvedValue(splitOverview);

    let resolvePreview!: (value: Awaited<ReturnType<typeof createResolutionPlan>>) => void;
    vi.mocked(createResolutionPlan).mockReturnValue(new Promise((resolve) => {
      resolvePreview = resolve;
    }));

    render(<ComponentBindingWorkspace importId="i1" bridgeId="bridge-1" lockToken="lock-1" />);
    await userEvent.click(await screen.findByLabelText("选择拆分 1-1#梁~1-25#梁"));
    await userEvent.click(screen.getByRole("button", { name: /拆分构件/ }));

    expect(screen.getByRole("dialog", { name: "拆分构件范围" })).toBeInTheDocument();
    expect(screen.getByText("正在计算展开影响…")).toBeInTheDocument();
    await userEvent.click(screen.getByRole("button", { name: "取消" }));
    expect(screen.queryByRole("dialog", { name: "拆分构件范围" })).not.toBeInTheDocument();

    resolvePreview({
      plan_token: "late-plan", operation_type: "range_expand",
      expires_at: "2026-08-28T10:15:00+08:00",
      will_apply_count: 1, skipped_count: 0, blocked_count: 0,
      instances_before: 1, instances_after: 25, rating_recomputed_count: 0,
      inventory_revision_id: "rev-1", rating_tree_version_id: null, rows: [],
    });
    await Promise.resolve();
    expect(screen.queryByRole("dialog", { name: "拆分构件范围" })).not.toBeInTheDocument();
  });

  it("selects and clears every split-eligible row in one component group", async () => {
    // 读模型是平铺的：一个组就是界面上的一行，部件由 source_component_name 归拢。
    const splitOverview = workspaceOf([
      group({ group_id: "g1", source_component_number: "1-1#梁~1-5#梁",
              split_eligible: true, split_expanded_count: 5 }),
      group({ group_id: "g2", source_component_number: "2-1#梁~2-5#梁",
              split_eligible: true, split_expanded_count: 5 }),
      group({ group_id: "g3", source_component_number: "3-1#梁" }),
      group({ group_id: "g4", source_component_name: "下部结构",
              source_component_number: "4-1#墩~4-5#墩",
              split_eligible: true, split_expanded_count: 5 }),
    ]);
    vi.mocked(fetchResolutionWorkspace).mockResolvedValue(splitOverview);

    render(<ComponentBindingWorkspace importId="i1" bridgeId="bridge-1" lockToken="lock-1" />);

    const selectGroup = await screen.findByLabelText("全选 上部承重构件 待拆分构件");
    await userEvent.click(selectGroup);

    expect(screen.getByLabelText("选择拆分 1-1#梁~1-5#梁")).toBeChecked();
    expect(screen.getByLabelText("选择拆分 2-1#梁~2-5#梁")).toBeChecked();
    expect(screen.queryByLabelText("选择拆分 3-1#梁")).not.toBeInTheDocument();
    expect(screen.getByLabelText("选择拆分 4-1#墩~4-5#墩")).not.toBeChecked();
    expect(screen.getByRole("button", { name: /拆分构件/ })).toHaveTextContent("2");
    expect(screen.getByText("取消全选 2")).toBeInTheDocument();

    await userEvent.click(selectGroup);
    expect(screen.getByLabelText("选择拆分 1-1#梁~1-5#梁")).not.toBeChecked();
    expect(screen.getByLabelText("选择拆分 2-1#梁~2-5#梁")).not.toBeChecked();
    expect(screen.getByRole("button", { name: /拆分构件/ })).toBeDisabled();
  });

  it("shows a partial group selection and completes it from the group checkbox", async () => {
    const splitOverview = workspaceOf([
      group({ group_id: "g1", source_component_number: "1-1#梁~1-5#梁",
              split_eligible: true, split_expanded_count: 5 }),
      group({ group_id: "g2", source_component_number: "2-1#梁~2-5#梁",
              split_eligible: true, split_expanded_count: 5 }),
    ]);
    vi.mocked(fetchResolutionWorkspace).mockResolvedValue(splitOverview);

    render(<ComponentBindingWorkspace importId="i1" bridgeId="bridge-1" lockToken="lock-1" />);
    await userEvent.click(await screen.findByLabelText("选择拆分 1-1#梁~1-5#梁"));

    const selectGroup = screen.getByLabelText("全选 上部承重构件 待拆分构件");
    expect(selectGroup).toBePartiallyChecked();

    await userEvent.click(selectGroup);
    expect(screen.getByLabelText("选择拆分 1-1#梁~1-5#梁")).toBeChecked();
    expect(screen.getByLabelText("选择拆分 2-1#梁~2-5#梁")).toBeChecked();
  });



  // 分组全选一键就能顶破 2000 条那个整批上限，而超限时连预览都出不来。后端已经在
  // details 里点名是哪个目标卡住的，前端不带出来的话用户只能逐行取消勾选去试。
  it("names the target that blew the split limit", async () => {
    const splitOverview = workspaceOf([
      group({ group_id: "g1", source_component_number: "2-1#梁~2-5#梁",
              split_eligible: true, split_expanded_count: 5 }),
    ]);
    vi.mocked(fetchResolutionWorkspace).mockResolvedValue(splitOverview);
    vi.mocked(createResolutionPlan).mockRejectedValue(
      new ApiError(
        "component_range_split_result_limit_exceeded",
        "本次拆分生成的病害数量超过上限。",
        { details: { part_name: "上部承重构件", component_number: "2-1#梁~2-5#梁" } }
      )
    );

    render(<ComponentBindingWorkspace importId="i1" bridgeId="bridge-1" lockToken="lock-1" />);
    await userEvent.click(await screen.findByLabelText("选择拆分 2-1#梁~2-5#梁"));
    await userEvent.click(screen.getByRole("button", { name: /拆分构件/ }));

    expect(
      await screen.findByText(/超过上限.*上部承重构件 · 2-1#梁~2-5#梁/)
    ).toBeInTheDocument();
  });

  // 行数说明不了规模：一行 "~5" 带 3 条病害就是 15 条。全选之前得看得见这个数。
  it("projects how many defects the selection will produce", async () => {
    const member = (id: string) =>
      ({ member_id: id, source_candidate_id: id, source_order: 0, instances: [] });
    const splitOverview = workspaceOf([
      group({ group_id: "g1", source_component_number: "1-1#梁~1-5#梁",
              members: [member("a"), member("b"), member("c")],
              split_eligible: true, split_expanded_count: 5 }),
      group({ group_id: "g2", source_component_number: "2-1#梁~2-5#梁",
              members: [member("d"), member("e")],
              split_eligible: true, split_expanded_count: 5 }),
    ]);
    vi.mocked(fetchResolutionWorkspace).mockResolvedValue(splitOverview);

    render(<ComponentBindingWorkspace importId="i1" bridgeId="bridge-1" lockToken="lock-1" />);

    // 分组旁边给的是"全选会生成多少"：3×5 + 2×5 = 25。
    expect(await screen.findByText("约 25 条")).toBeInTheDocument();

    // 已勾选的累计数跟着走：只选第一行就是 15。
    await userEvent.click(screen.getByLabelText("选择拆分 1-1#梁~1-5#梁"));
    const splitButton = screen.getByRole("button", { name: /拆分构件/ });
    await waitFor(() => expect(splitButton).toHaveTextContent("约 15 条"));
  });

  // 六个绑定写接口现在都要求编辑锁。没有编辑权还让人勾满一屏、点下去才报错，
  // 是把后端的硬边界藏起来。
  it("disables every write control and says why without an edit lock", async () => {
    const splitOverview = workspaceOf([
      group({ source_component_number: "1-1#梁~1-5#梁",
              candidates: [summary("c1", "1-1#梁~1-5#梁")],
              split_eligible: true, split_expanded_count: 5 }),
    ]);
    vi.mocked(fetchResolutionWorkspace).mockResolvedValue(splitOverview);

    render(<ComponentBindingWorkspace importId="i1" bridgeId="bridge-1" lockToken={null} />);

    expect(
      await screen.findByText("当前页面没有编辑权，绑定与拆分均不可用；取得编辑权后即可操作。")
    ).toBeInTheDocument();
    expect(screen.getByLabelText("选择拆分 1-1#梁~1-5#梁")).toBeDisabled();
    expect(screen.getByLabelText("全选 上部承重构件 待拆分构件")).toBeDisabled();
    expect(screen.getByLabelText("为 1-1#梁~1-5#梁 选择实际构件")).toBeDisabled();
    // 概览本身是只读的，没有编辑权照样要能看。
    expect(screen.getByText("上部承重构件")).toBeInTheDocument();
  });

});
