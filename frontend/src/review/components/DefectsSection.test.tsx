import { fireEvent, render, screen, waitFor } from "@testing-library/react";
import { beforeEach, describe, expect, it, vi } from "vitest";

import { fetchInventorySummary, searchInventoryEntries } from "../../api/componentInventoryApi";
import { ApiError } from "../../api/apiClient";
import { matchDefectRatingTreeNodes } from "../../api/defectMatchingApi";
import { addManualDefect, fetchResolutionWorkspace } from "../../api/resolutionApi";
import { fetchApplicableRatingTreeDefects, fetchRatingTreeNode } from "../../api/ratingTreeApi";
import { data } from "../testFixtures";
import { DefectsSection } from "./DefectsSection";

vi.mock("../../api/componentInventoryApi", async (importOriginal) => {
  const actual = await importOriginal<typeof import("../../api/componentInventoryApi")>();
  return { ...actual, fetchInventorySummary: vi.fn(), searchInventoryEntries: vi.fn() };
});
vi.mock("../../api/ratingTreeApi", async (importOriginal) => {
  const actual = await importOriginal<typeof import("../../api/ratingTreeApi")>();
  return {
    ...actual,
    fetchApplicableRatingTreeDefects: vi.fn(),
    fetchRatingTreeNode: vi.fn(),
  };
});

vi.mock("../../api/defectMatchingApi", async (importOriginal) => {
  const actual = await importOriginal<typeof import("../../api/defectMatchingApi")>();
  return { ...actual, matchDefectRatingTreeNodes: vi.fn() };
});

vi.mock("../../api/resolutionApi", async (importOriginal) => {
  const actual = await importOriginal<typeof import("../../api/resolutionApi")>();
  return { ...actual, fetchResolutionWorkspace: vi.fn(), addManualDefect: vi.fn() };
});

const mockedMatchDefects = vi.mocked(matchDefectRatingTreeNodes);
const mockedFetchWorkspace = vi.mocked(fetchResolutionWorkspace);
const mockedAddManualDefect = vi.mocked(addManualDefect);
const mockedFetchSummary = vi.mocked(fetchInventorySummary);

// 5.0：绑定与评分树结果住在关系表里，页面自己去 `GET /resolution-workspace` 取。
// 夹具照读模型的形状造，别在这里另起一套——两边一分叉，页面读得到而测试读不到。
interface Resolved {
  candidateId: string;
  componentId?: string;
  nodeId?: string | null;
  matchMethod?: string | null;
}

function workspaceGroups(resolved: Resolved[]) {
  return resolved.map((item, index) => ({
    group_id: `group-${index + 1}`,
    source_component_name: "主梁",
    source_component_number: "2-1#梁",
    normalized_component_number: "2-1#梁",
    resolution_mode: "single",
    status: "bound",
    match_method: "exact",
    inventory_revision_id: "revision-1",
    version: 1,
    ambiguous: false,
    split_eligible: false,
    split_expanded_count: null,
    targets: [{
      bridge_component_id: item.componentId ?? "component-1",
      component_number: "2-1#梁",
      site_component_type: "主梁",
      site_name: "主梁",
      standard_component_category_id: "h21.component.beam",
      standard_bridge_type_id: "bridge-type-1",
    }],
    candidates: [],
    allowed_actions: [],
    blocked_reasons: [],
    members: [{
      member_id: `member-${index + 1}`,
      source_candidate_id: item.candidateId,
      source_order: index,
      instances: [{
        resolved_defect_instance_id: `instance-${index + 1}`,
        target_id: `target-${index + 1}`,
        bridge_component_id: item.componentId ?? "component-1",
        instance_order: 1,
        instance_status: "active" as const,
        is_photo_owner: true,
        version: 1,
        component_resolution_version: 1,
        overridden_fields: [],
        effective_facts: {},
        rating_resolution: {
          present: Boolean(item.nodeId),
          status: item.nodeId ? "matched" : null,
          rating_tree_node_id: item.nodeId ?? null,
          match_method: item.matchMethod ?? (item.nodeId ? "manual" : null),
          version: 1,
          content_changed_after_manual_resolution: false,
        },
      }],
    }],
  }));
}

function workspace(resolved: Resolved[] = [{ candidateId: "defect_0001" }]) {
  return {
    import_record_id: "record-1",
    bridge_id: "bridge-1",
    draft_version: 1,
    inventory_confirmed: true,
    inventory_revision_id: "revision-1",
    rating_tree: {
      version_id: "tree-version-1",
      tree_name: "单位桥梁评定树",
      package_version: "1.0.3",
    },
    groups: workspaceGroups(resolved),
    parts: [],
    progress: {
      group_count: resolved.length, bound_count: resolved.length,
      unresolved_count: 0, ambiguous_count: 0, missing_count: 0,
      instance_count: resolved.length, active_instance_count: resolved.length,
      rating_matched_count: 0, rating_unresolved_count: 0, rating_missing_count: 0,
    },
  };
}
const mockedSearchEntries = vi.mocked(searchInventoryEntries);

// 台账条目只在搜索命中时才回来；映射随条目一起返回。
const entry = (overrides: Record<string, unknown> = {}) => ({
  id: "entry-1",
  bridge_component_id: "component-1",
  component_number: "1-1#梁",
  site_name: "主梁",
  site_component_type: "主梁",
  span_or_location: null,
  is_active: true,
  deactivated_at: null,
  deactivation_reason: null,
  sort_order: 1,
  remarks: null,
  is_referenced: false,
  position: 0,
  mappings: [{
    id: "mapping-1",
    standard_package_id: "package-1",
    standard_bridge_type_id: "bridge-type-1",
    standard_component_category_id: "h21.component.beam",
    structure_part: "superstructure" as const,
    mapping_source: "template",
    confirmation_status: "confirmed",
    is_active: true,
  }],
  ...overrides,
});

// 分组汇总代替整份台账：修订版 id 与 (桥型, 规范类别) 都在这里。
const summary = () => ({
  revision: {
    id: "revision-1", bridge_id: "bridge-1", revision_number: 1, status: "confirmed",
    baseline_revision_id: null, confirmed_at: null, active_entry_count: 1,
  },
  groups: [{
    site_component_type: "主梁",
    structure_part: "superstructure" as const,
    active_count: 1,
    first_number: "1-1#梁",
    last_number: "1-1#梁",
    confirmed_count: 1,
    pending_count: 0,
    unmapped_count: 0,
    standard_package_id: "package-1",
    standard_component_category_id: "h21.component.beam",
    standard_bridge_type_id: "bridge-type-1",
  }],
  blockers: {
    total: 0, individual_total: 0,
    by_code: { inventory_empty: 0, component_mapping_required: 0 }, samples: [],
  },
});
const mockedFetchApplicableNodes = vi.mocked(fetchApplicableRatingTreeDefects);
const mockedFetchTreeNode = vi.mocked(fetchRatingTreeNode);

describe("DefectsSection", () => {
  beforeEach(() => {
    mockedFetchWorkspace.mockReset();
    mockedFetchWorkspace.mockResolvedValue(workspace() as never);
    mockedAddManualDefect.mockReset();
    mockedFetchSummary.mockReset();
    mockedFetchSummary.mockResolvedValue(summary());
    mockedSearchEntries.mockReset();
    mockedSearchEntries.mockResolvedValue({ total: 1, entries: [entry()] });
    mockedFetchApplicableNodes.mockReset();
    mockedFetchApplicableNodes.mockResolvedValue([]);
    mockedFetchTreeNode.mockReset();
    mockedMatchDefects.mockReset();
    mockedMatchDefects.mockResolvedValue({
      rating_tree_version_id: "tree-version-1",
      summary: {
        processed: 0, auto_bound: 0, candidates: 0, composite: 0,
        unmatched: 0, prerequisite_missing: 0, failed: 0, skipped: 0,
      },
      results: [],
    });
  });

  // 首屏那几秒顶部计数是错的：规则没到时每条病害都被记上一条"正在加载评定树规则"
  // 的问题，而"可批量确认"的判据是一条问题都没有，于是全落进待处理，显示成
  // 待处理 362 / 可批量确认 0——与真的如此长得一模一样。
  //
  // 这条必须从页面这一层验：工具条自己的单测是把开关当属性直接传进去的，页面忘了
  // 接线它照样绿（第一版正是如此，接线那段脚本没跑到，功能其实没生效）。
  it("marks the counts unknown until the rating tree rules arrive", async () => {
    let releaseSummary: (value: ReturnType<typeof summary>) => void = () => {};
    mockedFetchSummary.mockReturnValue(
      new Promise<ReturnType<typeof summary>>((resolve) => { releaseSummary = resolve; }),
    );
    const draft = data();
    render(<DefectsSection draft={draft} importRecordId="record-1" baseUrl="http://backend" bridgeId="bridge-1" selectedCandidateId={null} onSelect={vi.fn()} dispatch={vi.fn()} ratingTree={{ version_id: "tree-version-1", tree_name: "单位桥梁评定树", package_version: "1.0.0", content_checksum: "sha256:test" }} allowStructureChanges />);

    // 规则还在路上：算不出来的筹码显示"—"且点不动。
    const pending = await screen.findByRole("button", { name: "待处理 —" });
    expect(pending).toBeDisabled();
    expect(screen.getByRole("button", { name: "可批量确认 —" })).toBeDisabled();
    // 病害列表照常显示，一条不挡——草稿早就到了，错的只有派生计数。
    expect(screen.getByRole("button", { name: /^全部 \d+$/ })).toBeEnabled();

    releaseSummary(summary());

    await waitFor(() =>
      expect(screen.getByRole("button", { name: /^待处理 \d+$/ })).toBeEnabled());
    expect(screen.queryByRole("button", { name: "可批量确认 —" })).not.toBeInTheDocument();
  });

  // 这一段此前会下载整份台账（现网一座桥 5174 条构件、3.6 MB），只为两件事：
  // 拿修订版 id，以及知道每个构件属于哪个规范类别。两者分组汇总里都有。
  it("never downloads the whole inventory", async () => {
    const draft = data();
    render(<DefectsSection draft={draft} importRecordId="record-1" baseUrl="http://backend" bridgeId="bridge-1" selectedCandidateId={null} onSelect={vi.fn()} dispatch={vi.fn()} ratingTree={{ version_id: "tree-version-1", tree_name: "单位桥梁评定树", package_version: "1.0.0", content_checksum: "sha256:test" }} allowStructureChanges />);

    await waitFor(() => expect(mockedFetchSummary).toHaveBeenCalled());
    fireEvent.click(screen.getByRole("button", { name: "新增病害" }));
    // 打开表单也不取台账：构件要搜了才来。
    expect(mockedSearchEntries).not.toHaveBeenCalled();

    fireEvent.change(screen.getByLabelText("搜索构件"), { target: { value: "1-1" } });
    await waitFor(() => expect(mockedSearchEntries).toHaveBeenCalled());
    const [, revisionId, keyword, limit, , bindingEligible] = mockedSearchEntries.mock.calls[0];
    expect(revisionId).toBe("revision-1");
    expect(keyword).toBe("1-1");
    expect(limit).toBe(20);
    expect(bindingEligible).toBe(true);
  });

  it("adds a manual defect from an actual mapped component and allows an empty scale", async () => {
    mockedFetchApplicableNodes.mockResolvedValue([{
      id: "tree-node-crack",
      node_key: "org.bridge.defect.crack",
      parent_node_id: "tree-group",
      display_number: "5.1.1-1",
      display_name: "裂缝",
      node_type: "defect",
      sort_order: 1,
      bridge_type_ids: ["bridge-type-1"],
      component_category_ids: ["h21.component.beam"],
      scoring_mode: "inherit_h21",
      h21_indicator_id: "h21.defect.crack",
      is_selectable: true,
      is_scoring: true,
    }]);
    mockedFetchTreeNode.mockResolvedValue({
      id: "tree-node-crack",
      node_key: "org.bridge.defect.crack",
      parent_node_id: "tree-group",
      display_number: "5.1.1-1",
      display_name: "裂缝",
      node_type: "defect",
      sort_order: 1,
      bridge_type_ids: ["bridge-type-1"],
      component_category_ids: ["h21.component.beam"],
      scoring_mode: "inherit_h21",
      h21_indicator_id: "h21.defect.crack",
      is_selectable: true,
      is_scoring: true,
      organization_note: "",
      allowed_scales: [1, 2, 3, 4, 5],
      h21_indicator_name: "裂缝",
      h21_source_table: "表5.3.1",
      scale_descriptions: { "1": "完好", "2": "轻微" },
      deduction_points: { "1": 0, "2": 15 },
      path: [],
      sources: [],
    });
    const dispatch = vi.fn();
    const draft = data();
    const created = { ...data().defects[0], candidate_id: "manual_defect_0001" };
    mockedAddManualDefect.mockResolvedValue({
      source_defect: created as never,
      draft_version: 2,
      result: {
        affected_groups: workspace([{ candidateId: "manual_defect_0001" }]).groups as never,
        progress: workspace().progress,
      },
    });
    draft.defects = [];

    render(<DefectsSection draft={draft} importRecordId="record-1" baseUrl="http://backend" bridgeId="bridge-1" selectedCandidateId={null} onSelect={vi.fn()} dispatch={dispatch} ratingTree={{ version_id: "tree-version-1", tree_name: "单位桥梁评定树", package_version: "1.0.0", content_checksum: "sha256:test" }} editLockToken="lock-1" allowStructureChanges />);
    fireEvent.click(screen.getByRole("button", { name: "新增病害" }));
    // 构件按需检索：先搜，再从命中结果里选。此前是把整份台账灌进下拉并默认选中第一条。
    fireEvent.change(screen.getByLabelText("搜索构件"), { target: { value: "1-1#梁" } });
    await waitFor(() => expect(screen.getByLabelText("实际构件")).toBeEnabled());
    fireEvent.change(screen.getByLabelText("实际构件"), { target: { value: "entry-1" } });
    await waitFor(() => expect(screen.getByLabelText("实际构件")).toHaveValue("entry-1"));
    fireEvent.change(screen.getByLabelText("新增病害位置"), { target: { value: "第1跨梁底" } });
    await waitFor(() => expect(screen.getByLabelText("新增病害类型")).toHaveValue(""));
    fireEvent.change(screen.getByLabelText("新增病害类型"), { target: { value: "tree-node-crack" } });
    fireEvent.change(screen.getByLabelText("新增病害描述"), { target: { value: "梁底纵向裂缝" } });
    fireEvent.click(screen.getByRole("button", { name: "添加病害" }));

    // 5.0：手工新增走专用命令，构件与评分树节点在后端同一事务里写入。
    // 退回"建组 + 自动匹配"是把用户明确的点选降级成一次猜测。
    await waitFor(() => expect(mockedAddManualDefect).toHaveBeenCalledWith(
      "http://backend",
      "record-1",
      {
        bridge_component_id: "component-1",
        rating_tree_node_id: "tree-node-crack",
        defect_facts: {
          defect_type: "裂缝",
          defect_location: "第1跨梁底",
          defect_description: "梁底纵向裂缝",
          defect_scale: null,
        },
        expected_inventory_revision_id: "revision-1",
      },
      1,
      "lock-1",
    ));
    // 新候选必须并进本地草稿：只更新版本却保留旧草稿，下一次整份保存
    // 就会把它当成"用户删掉了"。
    await waitFor(() => expect(dispatch).toHaveBeenCalledWith({
      type: "replace_draft",
      data: { ...draft, defects: [created] },
    }));
  });
  it("disables editable controls but keeps photo viewing available in a read-only review", () => {
    const draft = data();
    draft.photos[0] = { ...draft.photos[0], linked_defect_candidate_id: null };
    render(<DefectsSection draft={draft} importRecordId="record-1" baseUrl="http://backend" bridgeId="bridge-1" selectedCandidateId="defect_0001" selectedPhotoCandidateId="photo_0001" onSelect={vi.fn()} dispatch={vi.fn()} disabled />);

    // 筛选仍可使用，详情内正式字段和业务动作被锁定。
    expect(screen.getByRole("textbox", { name: "搜索病害" })).toBeEnabled();
    expect(screen.getByRole("textbox", { name: "位置" })).toBeDisabled();
    expect(screen.getByRole("combobox", { name: "评定树病害" })).toBeDisabled();
    expect(screen.getByRole("button", { name: "添加照片" })).toBeDisabled();
    expect(screen.getByRole("button", { name: "确认缺图" })).toBeDisabled();
    expect(screen.getByRole("button", { name: "确认本组" })).toBeDisabled();
    expect(screen.queryByRole("button", { name: "确认并查看下一条" })).not.toBeInTheDocument();

    // 快速列表与缩略图查看不禁用：只读态仍能检查导入结果。
    expect(screen.getByRole("button", { name: /2-1#梁/ })).toBeEnabled();
    expect(screen.getByRole("button", { name: "查看未归属照片 2.1-1" })).toBeEnabled();
  });

  // 无匹配的病害没有候选按钮可点，这个下拉是唯一能给它定规范病害的入口，必须常驻。
  it("keeps the rating tree picker in reach whether or not a node is bound", () => {
    const node = {
      id: "tree-node-crack",
      node_key: "org.bridge.defect.crack",
      parent_node_id: null,
      display_number: "5.1.1-1",
      display_name: "裂缝",
      node_type: "defect" as const,
      sort_order: 1,
      bridge_type_ids: ["bridge-type-1"],
      component_category_ids: ["h21.component.beam"],
      scoring_mode: "inherit_h21" as const,
      h21_indicator_id: "h21.defect.crack",
      is_selectable: true,
      is_scoring: true,
    };
    mockedFetchApplicableNodes.mockResolvedValue([node]);
    mockedFetchTreeNode.mockResolvedValue({
      ...node,
      organization_note: "",
      allowed_scales: [1, 2, 3],
      h21_indicator_name: "裂缝",
      h21_source_table: "表5.3.1",
      scale_descriptions: {},
      deduction_points: {},
      path: [],
      sources: [],
    });
    const unbound = data();
    unbound.defects[0] = {
      ...unbound.defects[0],
    };
    const props = {
      importRecordId: "record-1",
      baseUrl: "http://backend",
      bridgeId: "bridge-1",
      selectedCandidateId: "defect_0001",
      onSelect: vi.fn(),
      dispatch: vi.fn(),
      ratingTree: {
        version_id: "tree-version-1",
        tree_name: "单位桥梁评定树",
        package_version: "1.0.3",
        content_checksum: "sha256:test",
      },
    };

    const { rerender } = render(<DefectsSection draft={unbound} {...props} />);
    expect(screen.getByRole("combobox", { name: "评定树病害" })).toBeInTheDocument();

    // 定了之后下拉不消失：它同时是当前值的显示和改选的入口。
    const bound = data();
    bound.defects[0] = {
      ...bound.defects[0],
    };
    rerender(<DefectsSection draft={bound} {...props} />);
    expect(screen.getByRole("combobox", { name: "评定树病害" })).toBeInTheDocument();
  });

  it("locks defects outside the reopen scope while keeping warning defects editable", () => {
    const draft = data();
    const [first] = draft.defects;
    // 第一条病害带警告（可编辑），克隆出第二条无警告（应锁定）。
    draft.defects = [
      { ...first, warnings: [{ code: "w", message: "警告", severity: "warning" }] },
      { ...first, candidate_id: "defect_0002", warnings: [] },
    ];

    const commonProps = {
      draft,
      importRecordId: "record-1",
      baseUrl: "http://backend",
      bridgeId: "bridge-1",
      onSelect: vi.fn(),
      dispatch: vi.fn(),
      isDefectEditable: (defect: (typeof draft.defects)[number]) => defect.warnings.length > 0,
    };
    const { rerender } = render(
      <DefectsSection
        selectedCandidateId="defect_0001"
        {...commonProps}
      />
    );

    expect(screen.getByRole("textbox", { name: "位置" })).toBeEnabled();
    rerender(<DefectsSection selectedCandidateId="defect_0002" {...commonProps} />);
    expect(screen.getByRole("textbox", { name: "位置" })).toBeDisabled();
  });

  it("paginates defect cards and jumps to the selected defect's page", () => {
    const draft = data();
    const template = draft.defects[0];
    draft.defects = Array.from({ length: 60 }, (_, index) => ({
      ...template,
      candidate_id: `defect_${String(index + 1).padStart(4, "0")}`,
      component_number: `${index + 1}#梁`,
      photo_references: [],
    }));
    const props = {
      importRecordId: "record-1",
      baseUrl: "http://backend",
      bridgeId: "bridge-1",
      onSelect: vi.fn(),
      dispatch: vi.fn(),
    };

    const { rerender } = render(
      <DefectsSection draft={draft} selectedCandidateId={null} {...props} />
    );
    expect(screen.getByText("1#梁")).toBeInTheDocument();
    expect(screen.queryByText("51#梁")).not.toBeInTheDocument();
    expect(screen.getByText(/第 1 \/ 2 页（共 60 条）/)).toBeInTheDocument();

    fireEvent.click(screen.getByRole("button", { name: "下一页" }));
    expect(screen.getByText("51#梁")).toBeInTheDocument();
    expect(screen.queryByText("1#梁")).not.toBeInTheDocument();
    fireEvent.click(screen.getByRole("button", { name: "上一页" }));

    // 待处理跳转选中第 55 条 -> 自动翻到它所在的第 2 页。
    rerender(<DefectsSection draft={draft} selectedCandidateId="defect_0055" {...props} />);
    expect(screen.getAllByText("55#梁").length).toBeGreaterThan(0);
    expect(screen.queryByText("1#梁")).not.toBeInTheDocument();
  });

  it("opens a resizable split detail pane and closes it explicitly", () => {
    window.localStorage.removeItem("bridge-report:defect-detail-width-percent");
    const onCloseDetail = vi.fn();
    render(
      <DefectsSection
        draft={data()}
        importRecordId="record-1"
        baseUrl="http://backend"
        bridgeId="bridge-1"
        selectedCandidateId="defect_0001"
        onSelect={vi.fn()}
        onCloseDetail={onCloseDetail}
        dispatch={vi.fn()}
      />
    );

    const separator = screen.getByRole("separator", { name: "调整精细维护区域宽度" });
    expect(separator).toHaveAttribute("aria-valuenow", "67");
    fireEvent.keyDown(separator, { key: "ArrowLeft" });
    expect(separator).toHaveAttribute("aria-valuenow", "69");
    expect(Number(window.localStorage.getItem("bridge-report:defect-detail-width-percent"))).toBeCloseTo(68.67);

    fireEvent.click(screen.getByRole("button", { name: "关闭精细维护" }));
    expect(onCloseDetail).toHaveBeenCalledTimes(1);
  });

  it("keeps the just-confirmed row pinned until the filter changes", async () => {
    const treeNode = {
      id: "tree-node-crack",
      node_key: "org.bridge.defect.crack",
      parent_node_id: "tree-group",
      display_number: "5.1.1-1",
      display_name: "裂缝",
      node_type: "defect",
      sort_order: 1,
      bridge_type_ids: ["bridge-type-1"],
      component_category_ids: ["h21.component.beam"],
      scoring_mode: "inherit_h21" as const,
      h21_indicator_id: "h21.defect.crack",
      is_selectable: true,
      is_scoring: true,
      organization_note: "",
      allowed_scales: [2],
      h21_indicator_name: "裂缝",
      h21_source_table: "表5.3.1",
      scale_descriptions: { "2": "轻微裂缝" },
      deduction_points: { "2": 15 },
      path: [],
      sources: [],
    };
    mockedFetchApplicableNodes.mockResolvedValue([treeNode]);
    mockedFetchTreeNode.mockResolvedValue(treeNode);
    mockedFetchWorkspace.mockResolvedValue(workspace([
      { candidateId: "defect_0001", nodeId: treeNode.id },
    ]) as never);
    const draft = data();
    draft.defects[0] = {
      ...draft.defects[0],
      photo_references: [{
        photo_number: "2.1-1",
        resolution: "matched",
        photo_candidate_id: "photo_0001",
        resolved_defect_candidate_id: "defect_0001",
        review_note: null,
      }],
    };
    const dispatch = vi.fn();
    const props = {
      importRecordId: "record-1",
      baseUrl: "http://backend",
      bridgeId: "bridge-1",
      selectedCandidateId: "defect_0001",
      onSelect: vi.fn(),
      dispatch,
      ratingTree: { version_id: "tree-version-1", tree_name: "单位桥梁评定树", package_version: "1.0.0", content_checksum: "sha256:test" },
    };
    const { rerender } = render(<DefectsSection draft={draft} {...props} />);
    await waitFor(() => expect(screen.getByRole("button", { name: "可批量确认 1" })).toBeInTheDocument());
    fireEvent.click(screen.getByRole("button", { name: "可批量确认 1" }));
    fireEvent.click(screen.getByRole("button", { name: "确认本组" }));
    expect(dispatch).toHaveBeenCalledWith({ type: "confirm_defect_groups", candidateIds: ["defect_0001"] });

    const confirmedDraft = {
      ...draft,
      defects: [{ ...draft.defects[0], group_review_status: "已确认" as const }],
    };
    rerender(<DefectsSection draft={confirmedDraft} {...props} />);
    expect(screen.getByRole("button", { name: /^2-1#梁/ })).toBeInTheDocument();

    fireEvent.click(screen.getByRole("button", { name: "待处理 0" }));
    expect(screen.queryByRole("button", { name: /^2-1#梁/ })).not.toBeInTheDocument();
  });

  describe("批量匹配", () => {
    const treeNode = {
      id: "tree-node-water",
      node_key: "org.bridge.defect.water",
      parent_node_id: "tree-group",
      display_number: "5.1.1-2",
      display_name: "水损",
      node_type: "defect",
      sort_order: 1,
      bridge_type_ids: ["bridge-type-1"],
      component_category_ids: ["h21.component.beam"],
      scoring_mode: "reference_h21" as const,
      h21_indicator_id: "h21.defect.5_1_1_6",
      is_selectable: true,
      is_scoring: true,
      organization_note: "",
      allowed_scales: [1, 2],
      h21_indicator_name: "混凝土碳化",
      h21_source_table: "表5.1.1",
      scale_descriptions: { "1": "轻微", "2": "明显" },
      deduction_points: { "1": 0, "2": 15 },
      path: [],
      sources: [],
    };

    function matchProps(dispatch = vi.fn()) {
      return {
        importRecordId: "record-1",
        baseUrl: "http://backend",
        bridgeId: "bridge-1",
        selectedCandidateId: null,
        onSelect: vi.fn(),
        dispatch,
        ratingTree: {
          version_id: "tree-version-1",
          tree_name: "单位桥梁评定树",
          package_version: "1.0.3",
          content_checksum: "sha256:test",
        },
      };
    }

    function boundDraft() {
      const draft = data();
      draft.defects[0] = {
        ...draft.defects[0],
        photo_references: [],
      };
      return draft;
    }

    it("uses a single batch request and applies unique automatic results without confirming them", async () => {
      mockedFetchApplicableNodes.mockResolvedValue([treeNode]);
      mockedMatchDefects.mockResolvedValue({
        rating_tree_version_id: "tree-version-1",
        summary: {
          processed: 1, auto_bound: 1, candidates: 0, composite: 0,
          unmatched: 0, prerequisite_missing: 0, failed: 0, skipped: 0,
        },
        results: [{
          candidate_id: "defect_0001",
          outcome: "auto_bound",
          skipped: false,
          rating_tree_node_id: treeNode.id,
          match_method: "controlled_keyword",
          match_evidence: "命中受控关键词“渗水”。",
          reason_code: null,
          reason_message: null,
          candidates: [],
        }],
      });
      const dispatch = vi.fn();

      render(<DefectsSection draft={boundDraft()} {...matchProps(dispatch)} />);

      await waitFor(() => expect(mockedMatchDefects).toHaveBeenCalledTimes(1));
      // 5.0：自动结果由后端写进评分树解析表，页面重新拉一次工作区看结果，
      // 而不是把它塞回草稿。草稿里再存一份就会与权威状态两头不一致。
      await waitFor(() => expect(mockedFetchWorkspace).toHaveBeenCalledTimes(2));
      expect(dispatch).not.toHaveBeenCalledWith(
        expect.objectContaining({ type: "select_rating_tree_nodes" }),
      );
      // 自动匹配不确认病害。
      expect(dispatch).not.toHaveBeenCalledWith(
        expect.objectContaining({ type: "confirm_defect_groups" }),
      );
      // 多个候选、疑似组合与无匹配的条数由统计筹码负责，汇总只说筹码说不出来的。
      await waitFor(() => expect(
        screen.getByText(/共 1 条 · 自动匹配 1 条 · 跳过 0 条/),
      ).toBeInTheDocument());
    });

    it("shows composite and candidate results as their own states instead of plain unmatched", async () => {
      mockedFetchApplicableNodes.mockResolvedValue([treeNode]);
      mockedMatchDefects.mockResolvedValue({
        rating_tree_version_id: "tree-version-1",
        summary: {
          processed: 1, auto_bound: 0, candidates: 0, composite: 1,
          unmatched: 0, prerequisite_missing: 0, failed: 0, skipped: 0,
        },
        results: [{
          candidate_id: "defect_0001",
          outcome: "composite",
          skipped: false,
          rating_tree_node_id: null,
          match_method: "fuzzy_candidate",
          match_evidence: null,
          reason_code: "composite_defect",
          reason_message: "同一条记录明确命中多个规范病害。",
          candidates: [
            { rating_tree_node_id: treeNode.id, display_name: "水损", match_method: "controlled_alias", evidence: "命中别名“受渗水侵蚀”。" },
            { rating_tree_node_id: "tree-node-spalling", display_name: "剥落、掉角", match_method: "controlled_keyword", evidence: "命中关键词“剥蚀”。" },
          ],
        }],
      });

      render(<DefectsSection draft={boundDraft()} {...matchProps()} />);

      // 左侧列表不打开详情就能读出状态，顶部统计把组合病害与无结果分开计数。
      await waitFor(() => expect(
        document.querySelector(".defect-quick-match.composite"),
      ).toHaveTextContent("疑似组合病害"));
      expect(screen.getByRole("button", { name: "疑似组合病害 1" })).toBeInTheDocument();
      expect(screen.getByRole("button", { name: "无匹配结果 0" })).toBeInTheDocument();
    });

    // 三个头条问题只在顶部统计卡上有入口，下拉框不再重复一份；筛选生效时
    // 下拉框必须如实说明，不能显示成"全部问题"。
    // 原来这里是个"问题类型"下拉，与顶部统计筹码筛的是同一个维度，只是长相不同；
    // 而它筛得到的病害必然也在问题分组里（有问题 ⇒ 不可批量确认 ⇒ 待处理），
    // 问题分组还多给了标题、条数和整组操作。改成按部件筛，与筹码正交。
    it("filters by component part instead of duplicating the issue chips", async () => {
      mockedFetchApplicableNodes.mockResolvedValue([treeNode]);

      render(<DefectsSection draft={boundDraft()} {...matchProps()} />);
      await waitFor(() => expect(mockedMatchDefects).toHaveBeenCalled());

      const select = await screen.findByLabelText("按部件筛选") as HTMLSelectElement;
      expect([...select.options][0].textContent).toBe("全部部件");
      // 与筹码是两个维度，不该再出现问题类型的那几项。
      const labels = [...select.options].map((option) => option.textContent);
      expect(labels).not.toContain("构件未绑定");
      expect(labels).not.toContain("照片待处理");
    });

    it("reports a matcher failure with a retry entry instead of a silent no-match", async () => {
      mockedFetchApplicableNodes.mockResolvedValue([treeNode]);
      mockedMatchDefects.mockRejectedValue(new ApiError(
        "rating_tree_catalog_unavailable", "本年度锁定的评定树当前不可用。",
      ));

      render(<DefectsSection draft={boundDraft()} {...matchProps()} />);

      const alert = await screen.findByRole("alert");
      expect(alert).toHaveTextContent("评定树目录当前不可用");
      expect(screen.getByRole("button", { name: "重试" })).toBeInTheDocument();
      expect(screen.getByRole("button", { name: "无匹配结果 0" })).toBeInTheDocument();
    });

    it("waits for the description field to lose focus before rematching that defect", async () => {
      mockedFetchApplicableNodes.mockResolvedValue([treeNode]);
      const dispatch = vi.fn();

      render(
        <DefectsSection
          draft={boundDraft()}
          {...matchProps(dispatch)}
          selectedCandidateId="defect_0001"
        />,
      );
      await waitFor(() => expect(mockedMatchDefects).toHaveBeenCalledTimes(1));

      const description = screen.getByLabelText("病害描述");
      fireEvent.change(description, { target: { value: "板底存在渗水泛碱" } });
      // 输入过程中不发请求：只有 dispatch 记录了这次编辑。
      expect(mockedMatchDefects).toHaveBeenCalledTimes(1);
      expect(dispatch).toHaveBeenCalledWith(expect.objectContaining({
        type: "edit_defect_field",
        field: "defect_description",
      }));

      fireEvent.blur(description);
      await waitFor(() => expect(mockedMatchDefects).toHaveBeenCalledTimes(2));
      // 第 4 个实参是解析索引，候选范围排在它后面。
      expect(mockedMatchDefects.mock.calls[1][4]).toEqual(["defect_0001"]);
    });

    it("rematches the current filter scope through one request", async () => {
      mockedFetchApplicableNodes.mockResolvedValue([treeNode]);

      render(<DefectsSection draft={boundDraft()} {...matchProps()} />);
      await waitFor(() => expect(mockedMatchDefects).toHaveBeenCalledTimes(1));

      fireEvent.click(screen.getByRole("button", { name: /^重新匹配（当前筛选 1）/ }));

      await waitFor(() => expect(mockedMatchDefects).toHaveBeenCalledTimes(2));
      // 第 4 个实参是解析索引，候选范围排在它后面。
      expect(mockedMatchDefects.mock.calls[1][4]).toEqual(["defect_0001"]);
    });

    it("groups exact source identities and dispatches one batch rating-tree assignment", async () => {
      mockedFetchApplicableNodes.mockResolvedValue([treeNode]);
      mockedMatchDefects.mockResolvedValue({
        rating_tree_version_id: "tree-version-1",
        summary: {
          processed: 2, auto_bound: 0, candidates: 0, composite: 0,
          unmatched: 2, prerequisite_missing: 0, failed: 0, skipped: 0,
        },
        results: ["defect_0001", "defect_0002"].map((candidateId) => ({
          candidate_id: candidateId,
          outcome: "unmatched" as const,
          skipped: false,
          rating_tree_node_id: null,
          match_method: null,
          match_evidence: null,
          reason_code: "no_matching_rule",
          reason_message: "来源分组与指标没有精确对应关系。",
          candidates: [],
        })),
      });
      mockedFetchWorkspace.mockResolvedValue(workspace([
        { candidateId: "defect_0001", componentId: "component-1" },
        { candidateId: "defect_0002", componentId: "component-2" },
      ]) as never);
      const draft = boundDraft();
      draft.defects = ["component-1", "component-2"].map((_componentId, index) => ({
        ...draft.defects[0],
        candidate_id: `defect_000${index + 1}`,
        component_number: `1-${index + 1}#板`,
        defect_type: "",
        defect_description: "存在黑点痕迹",
        source_defect_group_id: "source-group-a",
        source_defect_group_number: "5.1.1",
        source_defect_indicator_id: "source-indicator-a",
        source_defect_indicator_number: "5.1.1-8",
      }));
      const dispatch = vi.fn();

      render(
        <DefectsSection
          draft={draft}
          {...matchProps(dispatch)}
        />,
      );
      await waitFor(() => expect(mockedMatchDefects).toHaveBeenCalled());
      fireEvent.click(screen.getByRole("button", { name: "问题分组（1）" }));
      const picker = await screen.findByRole("combobox", { name: "为 存在黑点痕迹 选择评定树病害" });
      fireEvent.change(picker, { target: { value: treeNode.id } });
      fireEvent.click(screen.getByRole("button", { name: "应用到本组 2 条" }));
      fireEvent.click(screen.getByRole("button", { name: "应用到本组" }));

      expect(dispatch).toHaveBeenCalledWith({
        type: "select_rating_tree_nodes",
        candidateIds: ["defect_0001", "defect_0002"],
        versionId: "tree-version-1",
        nodeId: treeNode.id,
        nodeName: treeNode.display_name,
        isScoring: true,
        matchEvidence: "用户按相同来源身份批量指定评定树病害",
      });
    });

    it("confirms all safe range-split defects in an issue group even when they have no photos", async () => {
      mockedFetchApplicableNodes.mockResolvedValue([treeNode]);
      mockedFetchTreeNode.mockResolvedValue(treeNode);
      mockedFetchWorkspace.mockResolvedValue(workspace([
        { candidateId: "defect_0001", nodeId: treeNode.id },
      ]) as never);
      const draft = boundDraft();
      draft.photos = [];
      draft.defects[0] = {
        ...draft.defects[0],
        defect_type: "渗水泛碱",
        group_review_status: "待确认",
        source_defect_group_id: "source-group-water",
        source_defect_group_number: "5.1.1",
        source_defect_indicator_id: "source-indicator-water",
        source_defect_indicator_number: "5.1.1-13",
        warnings: [{
          code: "component_range_split_review_required",
          message: "该病害由构件范围拆分，请人工核对构件、病害和照片关联。",
          severity: "warning",
          target_candidate_id: "defect_0001",
        }],
        photo_references: [],
      };
      const dispatch = vi.fn();

      render(
        <DefectsSection
          draft={draft}
          {...matchProps(dispatch)}
        />,
      );

      await waitFor(() => expect(mockedFetchTreeNode).toHaveBeenCalledWith(
        "http://backend",
        "tree-version-1",
        treeNode.id,
      ));
      fireEvent.click(screen.getByRole("button", { name: "问题分组（1）" }));
      fireEvent.click(await screen.findByRole("button", { name: "确认本组可确认项（1）" }));
      expect(screen.getByText("无照片记录也会被确认；现有病害选择和照片关联保持不变。")).toBeInTheDocument();
      fireEvent.click(screen.getByRole("button", { name: "确认 1 条" }));

      expect(dispatch).toHaveBeenCalledWith({
        type: "confirm_defect_groups",
        candidateIds: ["defect_0001"],
      });
    });
  });
});
