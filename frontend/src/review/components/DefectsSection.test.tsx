import { fireEvent, render, screen, waitFor } from "@testing-library/react";
import { beforeEach, describe, expect, it, vi } from "vitest";

import { fetchLatestComponentInventory } from "../../api/componentInventoryApi";
import { ApiError } from "../../api/apiClient";
import { matchDefectRatingTreeNodes } from "../../api/defectMatchingApi";
import { fetchApplicableRatingTreeDefects, fetchRatingTreeNode } from "../../api/ratingTreeApi";
import { data } from "../testFixtures";
import { DefectsSection } from "./DefectsSection";

vi.mock("../../api/componentInventoryApi", async (importOriginal) => {
  const actual = await importOriginal<typeof import("../../api/componentInventoryApi")>();
  return { ...actual, fetchLatestComponentInventory: vi.fn() };
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

const mockedMatchDefects = vi.mocked(matchDefectRatingTreeNodes);
const mockedFetchInventory = vi.mocked(fetchLatestComponentInventory);
const mockedFetchApplicableNodes = vi.mocked(fetchApplicableRatingTreeDefects);
const mockedFetchTreeNode = vi.mocked(fetchRatingTreeNode);

describe("DefectsSection", () => {
  beforeEach(() => {
    mockedFetchInventory.mockReset();
    mockedFetchApplicableNodes.mockReset();
    mockedFetchApplicableNodes.mockResolvedValue([]);
    mockedFetchTreeNode.mockReset();
    mockedMatchDefects.mockReset();
    mockedMatchDefects.mockResolvedValue({
      summary: {
        processed: 0, auto_bound: 0, candidates: 0, composite: 0,
        unmatched: 0, prerequisite_missing: 0, failed: 0, skipped: 0,
      },
      results: [],
      rating_tree_version_id: "tree-version-1",
    });
  });

  it("adds a manual defect from an actual mapped component and allows an empty scale", async () => {
    mockedFetchInventory.mockResolvedValue({
      id: "revision-1",
      bridge_id: "bridge-1",
      revision_number: 1,
      status: "confirmed",
      baseline_revision_id: null,
      confirmed_at: null,
      entries: [{
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
        mappings: [{
          id: "mapping-1",
          standard_package_id: "package-1",
          standard_bridge_type_id: "bridge-type-1",
          standard_component_category_id: "h21.component.beam",
          structure_part: "superstructure",
          mapping_source: "template",
          confirmation_status: "confirmed",
          is_active: true,
        }],
      }],
    });
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
    draft.defects = [];

    render(<DefectsSection draft={draft} importRecordId="record-1" baseUrl="http://backend" bridgeId="bridge-1" selectedCandidateId={null} onSelect={vi.fn()} dispatch={dispatch} ratingTree={{ version_id: "tree-version-1", tree_name: "单位桥梁评定树", package_version: "1.0.0", content_checksum: "sha256:test" }} allowStructureChanges />);
    fireEvent.click(screen.getByRole("button", { name: "新增病害" }));
    await waitFor(() => expect(screen.getByLabelText("实际构件")).toHaveValue("entry-1"));
    fireEvent.change(screen.getByLabelText("新增病害位置"), { target: { value: "第1跨梁底" } });
    await waitFor(() => expect(screen.getByLabelText("新增病害类型")).toHaveValue(""));
    fireEvent.change(screen.getByLabelText("新增病害类型"), { target: { value: "tree-node-crack" } });
    fireEvent.change(screen.getByLabelText("新增病害描述"), { target: { value: "梁底纵向裂缝" } });
    fireEvent.click(screen.getByRole("button", { name: "添加病害" }));

    expect(dispatch).toHaveBeenCalledWith({
      type: "add_defect",
      input: {
        componentName: "主梁",
        componentNumber: "1-1#梁",
        bridgeComponentId: "component-1",
        standardComponentCategoryId: "h21.component.beam",
        resolvedStructurePart: "上部结构",
        inventoryRevisionId: "revision-1",
        defectLocation: "第1跨梁底",
        defectType: "裂缝",
        ratingTreeVersionId: "tree-version-1",
        ratingTreeNodeId: "tree-node-crack",
        defectDescription: "梁底纵向裂缝",
        defectScale: null,
        isScoring: true,
      },
    });
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
    mockedFetchInventory.mockResolvedValue({
      id: "revision-1", bridge_id: "bridge-1", revision_number: 1, status: "confirmed",
      baseline_revision_id: null, confirmed_at: null, entries: [],
    });
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
      bridge_component_id: "component-1",
      standard_component_category_id: "h21.component.beam",
      rating_tree_node_id: null,
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
      bridge_component_id: "component-1",
      standard_component_category_id: "h21.component.beam",
      rating_tree_node_id: "tree-node-crack",
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
    const draft = data();
    draft.defects[0] = {
      ...draft.defects[0],
      bridge_component_id: "component-1",
      standard_component_category_id: "h21.component.beam",
      rating_tree_version_id: "tree-version-1",
      rating_tree_node_id: treeNode.id,
      rating_tree_match_method: "exact",
      standard_defect_indicator_id: "h21.defect.crack",
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
      componentInventory: {
        id: "revision-1",
        bridge_id: "bridge-1",
        revision_number: 1,
        status: "confirmed",
        baseline_revision_id: null,
        confirmed_at: null,
        entries: [{
          id: "entry-1",
          bridge_component_id: "component-1",
          component_number: "2-1#梁",
          site_name: "主梁",
          site_component_type: "主梁",
          span_or_location: null,
          is_active: true,
          deactivated_at: null,
          deactivation_reason: null,
          sort_order: 1,
          remarks: null,
          is_referenced: true,
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
        }],
      },
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
        bridge_component_id: "component-1",
        standard_component_category_id: "h21.component.beam",
        rating_tree_node_id: null,
        rating_tree_match_method: null,
        photo_references: [],
      };
      return draft;
    }

    it("uses a single batch request and applies unique automatic results without confirming them", async () => {
      mockedFetchApplicableNodes.mockResolvedValue([treeNode]);
      mockedMatchDefects.mockResolvedValue({
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
        rating_tree_version_id: "tree-version-1",
      });
      const dispatch = vi.fn();

      render(<DefectsSection draft={boundDraft()} {...matchProps(dispatch)} />);

      await waitFor(() => expect(mockedMatchDefects).toHaveBeenCalledTimes(1));
      await waitFor(() => expect(dispatch).toHaveBeenCalledWith({
        type: "apply_rating_tree_auto_matches",
        versionId: "tree-version-1",
        matches: [{
          candidateId: "defect_0001",
          nodeId: treeNode.id,
          matchMethod: "controlled_keyword",
          matchEvidence: "命中受控关键词“渗水”。",
          isScoring: true,
        }],
      }));
      // 自动匹配不确认病害：reducer 之外没有任何确认动作被派发。
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
        rating_tree_version_id: "tree-version-1",
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
    it("offers each issue filter in exactly one place", async () => {
      mockedFetchApplicableNodes.mockResolvedValue([treeNode]);

      render(<DefectsSection draft={boundDraft()} {...matchProps()} />);
      await waitFor(() => expect(mockedMatchDefects).toHaveBeenCalled());

      const select = screen.getByLabelText("问题类型") as HTMLSelectElement;
      const optionLabels = [...select.options].map((option) => option.textContent);
      expect(optionLabels).toEqual(["全部问题", "构件未绑定", "标度待选择", "照片待处理"]);

      fireEvent.click(screen.getByRole("button", { name: "疑似组合病害 0" }));
      expect(select.value).toBe("__headline__");
      expect(screen.getByRole("option", { name: "已按上方统计筛选" })).toBeDisabled();
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
      expect(mockedMatchDefects.mock.calls[1][3]).toEqual(["defect_0001"]);
    });

    it("rematches the current filter scope through one request", async () => {
      mockedFetchApplicableNodes.mockResolvedValue([treeNode]);

      render(<DefectsSection draft={boundDraft()} {...matchProps()} />);
      await waitFor(() => expect(mockedMatchDefects).toHaveBeenCalledTimes(1));

      fireEvent.click(screen.getByRole("button", { name: /^重新匹配（当前筛选 1）/ }));

      await waitFor(() => expect(mockedMatchDefects).toHaveBeenCalledTimes(2));
      expect(mockedMatchDefects.mock.calls[1][3]).toEqual(["defect_0001"]);
    });

    it("groups exact source identities and dispatches one batch rating-tree assignment", async () => {
      mockedFetchApplicableNodes.mockResolvedValue([treeNode]);
      mockedMatchDefects.mockResolvedValue({
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
        rating_tree_version_id: "tree-version-1",
      });
      const draft = boundDraft();
      draft.defects = ["component-1", "component-2"].map((componentId, index) => ({
        ...draft.defects[0],
        candidate_id: `defect_000${index + 1}`,
        component_number: `1-${index + 1}#板`,
        bridge_component_id: componentId,
        defect_type: "",
        defect_description: "存在黑点痕迹",
        source_defect_group_id: "source-group-a",
        source_defect_group_number: "5.1.1",
        source_defect_indicator_id: "source-indicator-a",
        source_defect_indicator_number: "5.1.1-8",
      }));
      const dispatch = vi.fn();
      const componentInventory = {
        id: "revision-1",
        bridge_id: "bridge-1",
        revision_number: 1,
        status: "confirmed" as const,
        baseline_revision_id: null,
        confirmed_at: null,
        entries: ["component-1", "component-2"].map((componentId, index) => ({
          id: `entry-${index + 1}`,
          bridge_component_id: componentId,
          component_number: `1-${index + 1}#板`,
          site_name: "桥面板",
          site_component_type: "桥面板",
          span_or_location: null,
          is_active: true,
          deactivated_at: null,
          deactivation_reason: null,
          sort_order: index + 1,
          remarks: null,
          is_referenced: true,
          mappings: [{
            id: `mapping-${index + 1}`,
            standard_package_id: "package-1",
            standard_bridge_type_id: "bridge-type-1",
            standard_component_category_id: "h21.component.beam",
            structure_part: "superstructure" as const,
            mapping_source: "template",
            confirmation_status: "confirmed",
            is_active: true,
          }],
        })),
      };

      render(
        <DefectsSection
          draft={draft}
          {...matchProps(dispatch)}
          componentInventory={componentInventory}
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
      const draft = boundDraft();
      draft.photos = [];
      draft.defects[0] = {
        ...draft.defects[0],
        rating_tree_version_id: "tree-version-1",
        rating_tree_node_id: treeNode.id,
        rating_tree_match_method: "source_indicator",
        standard_defect_indicator_id: treeNode.h21_indicator_id,
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
      const componentInventory = {
        id: "revision-1",
        bridge_id: "bridge-1",
        revision_number: 1,
        status: "confirmed" as const,
        baseline_revision_id: null,
        confirmed_at: null,
        entries: [{
          id: "entry-1",
          bridge_component_id: "component-1",
          component_number: "2-1#梁",
          site_name: "主梁",
          site_component_type: "主梁",
          span_or_location: null,
          is_active: true,
          deactivated_at: null,
          deactivation_reason: null,
          sort_order: 1,
          remarks: null,
          is_referenced: true,
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
        }],
      };
      const dispatch = vi.fn();

      render(
        <DefectsSection
          draft={draft}
          {...matchProps(dispatch)}
          componentInventory={componentInventory}
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
