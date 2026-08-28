import { describe, expect, it } from "vitest";
import { UNRESOLVED, type DefectResolution } from "./resolutionIndex";

import type { DefectMatchCandidate, DefectMatchOutcome, DefectMatchResult } from "../api/defectMatchingApi";
import type { RatingTreeNode } from "../api/ratingTreeApi";
import { data as completeData } from "./testFixtures";
import { buildDefectPhotoReviewModel } from "./defectPhotoReviewModel";
import { createReviewDraftReducer } from "./reviewDraft";

function matchResult(
  outcome: DefectMatchOutcome,
  candidates: DefectMatchCandidate[],
): DefectMatchResult {
  return {
    candidate_id: "defect_0001",
    outcome,
    skipped: false,
    rating_tree_node_id: null,
    match_method: null,
    match_evidence: null,
    reason_code: null,
    reason_message: null,
    candidates,
  };
}

const treeNode: RatingTreeNode = {
  id: "tree-node-crack",
  node_key: "org.bridge.defect.crack",
  parent_node_id: "tree-group",
  display_number: "5.1.1-1",
  display_name: "裂缝",
  node_type: "defect",
  sort_order: 1,
  bridge_type_ids: ["bridge-type-1"],
  component_category_ids: ["category-1"],
  scoring_mode: "inherit_h21",
  h21_indicator_id: "indicator-crack",
  is_selectable: true,
  is_scoring: true,
  organization_note: "",
  allowed_scales: [1, 2, 3],
  h21_indicator_name: "裂缝",
  h21_source_table: "5.1.1-1",
  scale_descriptions: { "1": "完好", "2": "轻微", "3": "明显" },
  deduction_points: { "1": 0, "2": 15, "3": 30 },
  path: [],
  sources: [],
};

// 5.0：解析状态不在草稿里。下面的夹具在造草稿的同时把对应的解析快照记在这里，
// treeWiring() 把它一并交给模型——否则四十多个调用点每处都要再传一遍，
// 漏一处就是“这一处以为没绑定”。
let fixtureResolution = new Map<string, DefectResolution>();

function setResolution(candidateId: string, patch: Partial<DefectResolution>) {
  fixtureResolution.set(candidateId, {
    ...UNRESOLVED,
    ...(fixtureResolution.get(candidateId) ?? {}),
    ...patch,
  });
}

function treeWiring() {
  return {
    ratingTreeVersionId: "tree-version-1",
    ratingTreeNodes: [treeNode],
    applicableTreeNodeIdsByComponent: new Map([["component-1", new Set([treeNode.id])]]),
    treeRulesReady: true,
    resolution: fixtureResolution,
  };
}

function safeDraft() {
  fixtureResolution = new Map();
  const draft = completeData();
  const defect = draft.defects[0];
  setResolution(defect.candidate_id, {
    bridgeComponentId: "component-1",
    standardComponentCategoryId: "category-1",
    ratingTreeVersionId: "tree-version-1",
    ratingTreeNodeId: treeNode.id,
    ratingMatchMethod: "exact",
    ratingStatus: "matched",
    hasRating: true,
    activeInstanceCount: 1,
  });
  defect.defect_scale = 2;
  defect.review_status = "待确认";
  defect.group_review_status = "待确认";
  defect.photo_references = [{
    photo_number: "2.1-1",
    resolution: "matched",
    photo_candidate_id: "photo_0001",
    resolved_defect_candidate_id: "defect_0001",
    review_note: null,
  }];
  return draft;
}

// 三条病害各挂一个构件，用来验按部件走查的排序。
// 故意让"要人工判断的"落在部件顺序的中间，好看出两个键谁是主的。
function orderingDraft() {
  const draft = safeDraft();
  const base = draft.defects[0];
  draft.defects = [
    { ...base, candidate_id: "d-railing" },
    { ...base, candidate_id: "d-girder" },
    { ...base, candidate_id: "d-pier" },
  ];
  const bound = fixtureResolution.get(base.candidate_id) ?? UNRESOLVED;
  for (const [candidateId, componentId] of [
    ["d-railing", "c-railing"], ["d-girder", "c-girder"], ["d-pier", "c-pier"],
  ] as const) {
    setResolution(candidateId, { ...bound, bridgeComponentId: componentId });
  }
  draft.photos = [];
  return draft;
}

// 后端排好的次序：板 → 墩柱 → 栏杆（上部 → 下部 → 桥面系）。
const componentOrder = new Map([["c-girder", 0], ["c-pier", 1], ["c-railing", 2]]);

function orderingInput(extra: Record<string, unknown> = {}) {
  // 调用方自带草稿时不再重建：重建会连带重置 fixtureResolution，把调用方刚写进去的
  // 解析快照冲掉。
  const draft = extra.draft ?? orderingDraft();
  return {
    draft,
    ratingTreeVersionId: "tree-version-1",
    ratingTreeNodes: [treeNode],
    applicableTreeNodeIdsByComponent: new Map([
      ["c-girder", new Set([treeNode.id])],
      ["c-pier", new Set([treeNode.id])],
      ["c-railing", new Set([treeNode.id])],
    ]),
    treeRulesReady: true,
    resolution: fixtureResolution,
    assessmentIssues: [],
    ...extra,
  };
}

describe("buildDefectPhotoReviewModel", () => {
  // 校对时是照着纸质报告逐部件核对的，列表就该按 板 → 铰缝 → 支座 → 墩柱 → … 一条
  // 顺下来，而不是按"要不要人工判断"把部件打散。
  it("orders rows by the component review order the backend gives", () => {
    const model = buildDefectPhotoReviewModel(
      orderingInput({ componentOrder }) as never);
    expect(model.rows.map((row) => row.candidateId))
      .toEqual(["d-girder", "d-pier", "d-railing"]);
  });

  // 顺序还没取到时不能把列表打乱：退回原来的优先级排序。
  it("falls back to the priority order until the component order arrives", () => {
    const model = buildDefectPhotoReviewModel(orderingInput() as never);
    expect(model.rows.map((row) => row.candidateId))
      .toEqual(["d-railing", "d-girder", "d-pier"]);
  });

  // 下拉按部件筛：与顶部筹码（筛问题类型）正交，可以叠加使用。
  it("counts each part in review order and filters by it", () => {
    const componentPart = new Map([
      ["c-girder", "板"], ["c-pier", "墩柱"], ["c-railing", "栏杆"],
    ]);
    const all = buildDefectPhotoReviewModel(
      orderingInput({ componentOrder, componentPart }) as never);
    // 顺序与列表一致：板 → 墩柱 → 栏杆，而不是按名字或出现次序。
    expect(all.summary.parts).toEqual([
      { name: "板", count: 1 }, { name: "墩柱", count: 1 }, { name: "栏杆", count: 1 },
    ]);

    const onlyPier = buildDefectPhotoReviewModel(
      orderingInput({ componentOrder, componentPart, partFilter: "墩柱" }) as never);
    expect(onlyPier.rows.map((row) => row.candidateId)).toEqual(["d-pier"]);
    // 计数不受筛选影响：筛到某个部件之后，其余部件仍要显示各自的条数，
    // 否则一旦筛进去就再也看不出别的部件还有多少条。
    expect(onlyPier.summary.parts).toEqual(all.summary.parts);
  });

  it("filters the defects that have no component at all", () => {
    const draft = orderingDraft();
    draft.defects.push({ ...draft.defects[0], candidate_id: "d-unbound" });
    // 不给它写解析快照：默认就是"还没绑构件"，正是这条用例要的状态。
    const componentPart = new Map([
      ["c-girder", "板"], ["c-pier", "墩柱"], ["c-railing", "栏杆"],
    ]);
    const model = buildDefectPhotoReviewModel(orderingInput({
      draft, componentOrder, componentPart, partFilter: "__unbound__",
    }) as never);
    expect(model.rows.map((row) => row.candidateId)).toEqual(["d-unbound"]);
  });

  // 没绑构件的病害还不属于任何部件，插在中间会打断走查，排最后。
  it("puts defects without a component at the end", () => {
    const draft = orderingDraft();
    draft.defects.push({ ...draft.defects[0], candidate_id: "d-unbound" });
    // 不给它写解析快照：默认就是"还没绑构件"，正是这条用例要的状态。
    const model = buildDefectPhotoReviewModel(
      orderingInput({ draft, componentOrder }) as never);
    expect(model.rows[model.rows.length - 1].candidateId).toBe("d-unbound");
  });


  it("allows exact and controlled-alias tree matches but blocks fuzzy suggestions", () => {
    const draft = safeDraft();
    setResolution(draft.defects[0].candidate_id, {
      ratingTreeVersionId: "tree-version-1",
      ratingTreeNodeId: treeNode.id,
      ratingMatchMethod: "exact",
    });
    const input = {
      draft,
      ratingTreeVersionId: "tree-version-1",
      ratingTreeNodes: [treeNode],
      applicableTreeNodeIdsByComponent: new Map([["component-1", new Set([treeNode.id])]]),
      treeRulesReady: true,
      resolution: fixtureResolution,
      assessmentIssues: [],
    };

    expect(buildDefectPhotoReviewModel(input).rows[0].batchEligible).toBe(true);
    setResolution(draft.defects[0].candidate_id, { ratingMatchMethod: "controlled_alias" });
    expect(buildDefectPhotoReviewModel(input).rows[0].batchEligible).toBe(true);
    setResolution(draft.defects[0].candidate_id, { ratingMatchMethod: "fuzzy_candidate" });
    expect(buildDefectPhotoReviewModel(input).rows[0].problems.map((problem) => problem.code))
      .toContain("rating_tree_fuzzy_review_required");
  });

  it("validates tree version, component scope and allowed scales independently", () => {
    const draft = safeDraft();
    setResolution(draft.defects[0].candidate_id, {
      ratingTreeVersionId: "old-version",
      ratingTreeNodeId: treeNode.id,
      ratingMatchMethod: "manual",
    });
    draft.defects[0].defect_scale = 5;
    const row = buildDefectPhotoReviewModel({
      draft,
      ratingTreeVersionId: "tree-version-1",
      ratingTreeNodes: [treeNode],
      applicableTreeNodeIdsByComponent: new Map([["component-1", new Set<string>()]]),
      treeRulesReady: true,
      resolution: fixtureResolution,
      assessmentIssues: [],
    }).rows[0];

    expect(row.problems.map((problem) => problem.code)).toEqual(expect.arrayContaining([
      "rating_tree_version_mismatch",
      "rating_tree_node_not_applicable",
      "scale_not_allowed",
    ]));
  });

  it("marks a complete group as safe for batch confirmation", () => {
    const model = buildDefectPhotoReviewModel({
      draft: safeDraft(),
      ...treeWiring(),
      assessmentIssues: [],
    });

    expect(model.summary).toEqual({
      // 没给 componentPart 时这条病害归不到任何部件，落进"未绑定构件"那一档。
      parts: [{ name: "__unbound__", count: 1 }],
      all: 1,
      pending: 0,
      batchable: 1,
      confirmed: 0,
      composite: 0,
      candidates: 0,
      unmatched: 0,
    });
    expect(model.safeCandidateIds.has("defect_0001")).toBe(true);
  });

  it("allows a non-scoring node to be confirmed from its applicable summary before details load", () => {
    const draft = safeDraft();
    const nonScoringSummary = {
      ...treeNode,
      id: "tree-node-other",
      display_name: "其它病害",
      scoring_mode: "non_scoring" as const,
      h21_indicator_id: null,
      is_scoring: false,
    };
    setResolution(draft.defects[0].candidate_id, {
      ratingTreeNodeId: nonScoringSummary.id,
      ratingMatchMethod: "manual",
      ratingStatus: "matched",
      hasRating: true,
    });
    draft.defects[0] = {
      ...draft.defects[0],
      defect_type: nonScoringSummary.display_name,
      defect_scale: null,
    };

    const row = buildDefectPhotoReviewModel({
      draft,
      ratingTreeVersionId: "tree-version-1",
      ratingTreeNodes: [],
      ratingTreeNodeSummaries: [nonScoringSummary],
      applicableTreeNodeIdsByComponent: new Map([
        ["component-1", new Set([nonScoringSummary.id])],
      ]),
      treeRulesReady: true,
      resolution: fixtureResolution,
      assessmentIssues: [],
    }).rows[0];

    expect(row.problems).toEqual([]);
    expect(row.confirmEligible).toBe(true);
    expect(row.batchEligible).toBe(true);
  });

  it("keeps a scoring node blocked until its scale rules are available", () => {
    const summaryWithoutScaleRules = {
      ...treeNode,
      allowed_scales: undefined,
      scale_descriptions: undefined,
    };
    const row = buildDefectPhotoReviewModel({
      draft: safeDraft(),
      ratingTreeVersionId: "tree-version-1",
      ratingTreeNodes: [],
      ratingTreeNodeSummaries: [summaryWithoutScaleRules],
      applicableTreeNodeIdsByComponent: new Map([
        ["component-1", new Set([treeNode.id])],
      ]),
      treeRulesReady: true,
      resolution: fixtureResolution,
      assessmentIssues: [],
    }).rows[0];

    expect(row.problems.map((problem) => problem.code)).toContain("rating_tree_node_loading");
    expect(row.confirmEligible).toBe(false);
  });

  it("allows individual confirmation of a range split without photos but keeps it out of batch confirmation", () => {
    const draft = safeDraft();
    draft.defects[0].photo_references = [];
    draft.defects[0].warnings = [{
      code: "component_range_split_review_required",
      message: "该病害由构件范围拆分，请人工核对构件、病害和照片关联。",
      severity: "warning",
      target_candidate_id: "defect_0001",
    }];

    const row = buildDefectPhotoReviewModel({
      draft,
      ...treeWiring(),
      assessmentIssues: [],
    }).rows[0];

    expect(row.problems.map((problem) => problem.code))
      .toEqual(["component_range_split_review_required"]);
    expect(row.confirmEligible).toBe(true);
    expect(row.batchEligible).toBe(false);
  });

  // 导入器的尺寸解析是启发式的，报出来的往往只是位置里的墩号、桩号。这条警告的原文
  // 就写着"请人工确认"，而确认本组正是那次确认——拿它挡住确认按钮，警告就永远消不掉。
  it("lets a human confirm past the low-confidence measurement warning", () => {
    const draft = safeDraft();
    draft.defects[0].measurement_text = "渗水泛碱,L=10m，近20#墩1m处，渗水滴漏";
    draft.defects[0].warnings = [{
      code: "measurement_parse_low_confidence",
      message: "尺寸表达未能稳定结构化，请人工确认。",
      severity: "warning",
      target_candidate_id: "defect_0001",
    }];

    const row = buildDefectPhotoReviewModel({
      draft,
      ...treeWiring(),
      assessmentIssues: [],
    }).rows[0];

    expect(row.problems.map((problem) => problem.code))
      .toEqual(["measurement_parse_low_confidence"]);
    expect(row.confirmEligible).toBe(true);
    // 批量确认没有"人看一眼"这一步，仍然把它挡在外面。
    expect(row.batchEligible).toBe(false);
  });

  // 前端自己也会推导同一条问题（尺寸原文里有"数字+单位"却一条都没结构化）。它的门槛比
  // 导入器窄得多——导入器那边"大面积"三个字里的"面积"就够触发，前端这边必须见到数字挨着
  // 单位。确认掉导入器那条警告之后，这条派生的不能再把病害拽回待处理，否则界面上就是
  // "点得动、点完还挂着待处理"。
  it("still counts as confirmed when the derived measurement warning stays", () => {
    const draft = safeDraft();
    draft.defects[0].measurement_text = "缝宽约3mm，未量长度";
    draft.defects[0].measurements = [];
    draft.defects[0].group_review_status = "已确认";
    draft.defects[0].warnings = [];

    const row = buildDefectPhotoReviewModel({
      draft,
      ...treeWiring(),
      assessmentIssues: [],
    }).rows[0];

    expect(row.problems.map((problem) => problem.code))
      .toEqual(["measurement_parse_low_confidence"]);
    expect(row.status).toBe("confirmed");
    expect(row.confirmEligible).toBe(false);
  });

  // 来源软件的结构化尺寸与描述原文对不上时报的。来源值已经保留，没有任何数据可补，
  // 除了人工确认无从了结。
  it("lets a human confirm past a source measurement conflict", () => {
    const draft = safeDraft();
    draft.defects[0].warnings = [{
      code: "source_measurement_conflict",
      message: "来源结构化长度与病害描述中的尺寸表达不一致，已保留来源结构化值，请人工复核。",
      severity: "warning",
      target_candidate_id: "defect_0001",
    }];

    const row = buildDefectPhotoReviewModel({
      draft,
      ...treeWiring(),
      assessmentIssues: [],
    }).rows[0];

    expect(row.problems.map((problem) => problem.code)).toEqual(["source_measurement_conflict"]);
    expect(row.confirmEligible).toBe(true);
  });

  // 标度这条要成对地看：警告本身放行，但缺口由实时的 scale_not_allowed 兜着——
  // 标度没选出来就仍然确认不了，选对了两条一起消失。
  it("acknowledges an invalid source scale but still needs a valid one picked", () => {
    const draft = safeDraft();
    draft.defects[0].warnings = [{
      code: "defect_scale_invalid",
      message: "病害标度“轻微”不是正整数，请人工确认。",
      severity: "warning",
      target_candidate_id: "defect_0001",
    }];
    draft.defects[0].defect_scale = null;

    const unpicked = buildDefectPhotoReviewModel({
      draft,
      ...treeWiring(),
      assessmentIssues: [],
    }).rows[0];
    expect(unpicked.problems.map((problem) => problem.code)).toContain("scale_not_allowed");
    expect(unpicked.confirmEligible).toBe(false);

    draft.defects[0].defect_scale = 2;
    const picked = buildDefectPhotoReviewModel({
      draft,
      ...treeWiring(),
      assessmentIssues: [],
    }).rows[0];
    expect(picked.problems.map((problem) => problem.code)).toEqual(["defect_scale_invalid"]);
    expect(picked.confirmEligible).toBe(true);
  });

  // 放行只对"请人工确认"那一类成立：真的缺数据仍然要挡。
  it("keeps blocking when a real gap sits alongside an acknowledgeable warning", () => {
    const draft = safeDraft();
    setResolution(draft.defects[0].candidate_id, { bridgeComponentId: null });
    draft.defects[0].warnings = [{
      code: "measurement_parse_low_confidence",
      message: "尺寸表达未能稳定结构化，请人工确认。",
      severity: "warning",
      target_candidate_id: "defect_0001",
    }];

    const row = buildDefectPhotoReviewModel({
      draft,
      ...treeWiring(),
      assessmentIssues: [],
    }).rows[0];

    expect(row.problems.map((problem) => problem.code)).toContain("component_required");
    expect(row.confirmEligible).toBe(false);
  });

  it("treats one linked archived photo as an atomic batch-confirm candidate", () => {
    const draft = safeDraft();
    draft.defects[0].photo_references[0] = {
      photo_number: "2.1-1",
      resolution: "pending",
      photo_candidate_id: null,
      resolved_defect_candidate_id: null,
      review_note: null,
    };
    const row = buildDefectPhotoReviewModel({
      draft,
      ...treeWiring(),
      assessmentIssues: [],
    }).rows[0];

    expect(row.problems).toEqual([]);
    expect(row.batchEligible).toBe(true);
  });

  it("blocks batch confirmation until a rating tree defect is chosen", () => {
    const draft = safeDraft();
    setResolution(draft.defects[0].candidate_id, {
      ratingTreeNodeId: null, ratingMatchMethod: null, ratingStatus: null, hasRating: false });
    const row = buildDefectPhotoReviewModel({
      draft,
      ...treeWiring(),
      assessmentIssues: [],
    }).rows[0];

    expect(row.problems.map((problem) => problem.code)).toContain("rating_tree_node_required");
    expect(row.batchEligible).toBe(false);
  });

  it("drops backend issues that restate a problem the frontend already derived", () => {
    const draft = safeDraft();
    setResolution(draft.defects[0].candidate_id, {
      ratingTreeNodeId: null, ratingMatchMethod: null, ratingStatus: null, hasRating: false });
    const row = buildDefectPhotoReviewModel({
      draft,
      ...treeWiring(),
      assessmentIssues: [{
        // 码名必须与 AssessmentService.cpp 实际发出的一致，别名表才去得掉重。
        code: "assessment_rating_tree_node_required",
        message: "病害尚未选择当前年度评定树中的有效节点。",
        entity_type: "defect",
        entity_id: "defect_0001",
        field_path: "rating_tree_node_id",
        rule_id: "",
      }],
    }).rows[0];

    expect(row.problems.map((problem) => problem.code)).toEqual(["rating_tree_node_required"]);
    expect(row.batchEligible).toBe(false);
  });

  it("keeps a backend issue that has no frontend equivalent", () => {
    const row = buildDefectPhotoReviewModel({
      draft: safeDraft(),
      ...treeWiring(),
      assessmentIssues: [{
        code: "assessment_defect_scale_required",
        message: "病害缺少有效的规范标度。",
        entity_type: "defect",
        entity_id: "defect_0001",
        field_path: "defect_scale",
        rule_id: "",
      }],
    }).rows[0];

    expect(row.problems.map((problem) => problem.code)).toEqual(["assessment_defect_scale_required"]);
  });

  it("labels controlled matches, candidates and composite defects distinctly", () => {
    const draft = safeDraft();
    setResolution(draft.defects[0].candidate_id, {
      ratingTreeVersionId: "tree-version-1",
      ratingTreeNodeId: treeNode.id,
      ratingMatchMethod: "controlled_keyword",
    });
    const treeInput = {
      draft,
      ratingTreeVersionId: "tree-version-1",
      ratingTreeNodes: [treeNode],
      applicableTreeNodeIdsByComponent: new Map([["component-1", new Set([treeNode.id])]]),
      treeRulesReady: true,
      resolution: fixtureResolution,
      assessmentIssues: [],
    };

    const bound = buildDefectPhotoReviewModel(treeInput).rows[0];
    expect(bound.matchState).toBe("auto_bound");
    expect(bound.matchLabel).toBe("自动匹配：裂缝");
    // 自动绑定不确认：仍然停留在待确认，只是满足条件后可批量确认。
    expect(bound.defect.group_review_status).toBe("待确认");
    expect(bound.batchEligible).toBe(true);

    setResolution(draft.defects[0].candidate_id, {
      ratingTreeNodeId: null, ratingMatchMethod: null, ratingStatus: null, hasRating: false });
    const composite = buildDefectPhotoReviewModel({
      ...treeInput,
      matchResults: new Map([["defect_0001", matchResult("composite", [
        { rating_tree_node_id: treeNode.id, display_name: "裂缝", match_method: "controlled_alias", evidence: "命中别名" },
        { rating_tree_node_id: "tree-node-water", display_name: "水损", match_method: "controlled_keyword", evidence: "命中关键词" },
      ])]]),
    }).rows[0];
    expect(composite.matchState).toBe("composite");
    expect(composite.matchLabel).toBe("疑似组合病害");
    expect(composite.batchEligible).toBe(false);
    expect(composite.matchCandidates).toHaveLength(2);

    const candidates = buildDefectPhotoReviewModel({
      ...treeInput,
      matchResults: new Map([["defect_0001", matchResult("candidates", [
        { rating_tree_node_id: treeNode.id, display_name: "裂缝", match_method: "fuzzy_candidate", evidence: "文字相似" },
      ])]]),
    }).rows[0];
    expect(candidates.matchState).toBe("candidates");
    expect(candidates.matchLabel).toBe("候选 1 项");
    expect(candidates.batchEligible).toBe(false);
  });

  it("keeps missing prerequisites and matcher failures out of the plain unmatched bucket", () => {
    const draft = safeDraft();
    setResolution(draft.defects[0].candidate_id, {
      ratingTreeNodeId: null, ratingMatchMethod: null, ratingStatus: null, hasRating: false });
    const treeInput = {
      draft,
      ratingTreeVersionId: "tree-version-1",
      ratingTreeNodes: [treeNode],
      applicableTreeNodeIdsByComponent: new Map([["component-1", new Set([treeNode.id])]]),
      treeRulesReady: true,
      assessmentIssues: [],
    };

    const missing = buildDefectPhotoReviewModel({
      ...treeInput,
      matchResults: new Map([["defect_0001", {
        ...matchResult("prerequisite_missing", []),
        reason_code: "component_not_bound",
        reason_message: "该病害尚未绑定实际构件。",
      }]]),
    });
    expect(missing.rows[0].matchState).toBe("prerequisite_missing");
    expect(missing.summary.unmatched).toBe(0);
    expect(missing.rows[0].problems.map((problem) => problem.code))
      .toContain("rating_tree_prerequisite_missing");

    const failed = buildDefectPhotoReviewModel({
      ...treeInput,
      matchResults: new Map([["defect_0001", {
        ...matchResult("service_error", []),
        reason_code: "matcher_failed",
        reason_message: "匹配服务执行失败，请稍后重试。",
      }]]),
    });
    expect(failed.rows[0].matchState).toBe("service_error");
    expect(failed.summary.unmatched).toBe(0);
    expect(failed.rows[0].problems.map((problem) => problem.code))
      .toContain("rating_tree_matcher_failed");
  });

  // 列表每行只剩一个徽标，所以终态必须由它自己说出来，不能靠另一个状态徽标兜底。
  it("says an ignored defect is ignored on the single row badge", () => {
    const draft = safeDraft();
    draft.defects[0].review_status = "已忽略";
    const row = buildDefectPhotoReviewModel({
      draft,
      ...treeWiring(),
      assessmentIssues: [],
    }).rows[0];

    expect(row.status).toBe("ignored");
    expect(row.matchState).toBe("ignored");
    expect(row.matchLabel).toBe("已忽略");
  });

  it("keeps confirmed rows readable from the same single badge", () => {
    const draft = safeDraft();
    draft.defects[0].group_review_status = "已确认";
    const row = buildDefectPhotoReviewModel({
      draft,
      ...treeWiring(),
      assessmentIssues: [],
    }).rows[0];

    expect(row.status).toBe("confirmed");
    expect(row.matchLabel).toBe("已确认：裂缝");
  });

  it("never lets an automatic result relabel a manual or confirmed record", () => {
    const draft = safeDraft();
    setResolution(draft.defects[0].candidate_id, {
      ratingTreeVersionId: "tree-version-1",
      ratingTreeNodeId: treeNode.id,
      ratingMatchMethod: "manual",
    });
    const row = buildDefectPhotoReviewModel({
      draft,
      ratingTreeVersionId: "tree-version-1",
      ratingTreeNodes: [treeNode],
      applicableTreeNodeIdsByComponent: new Map([["component-1", new Set([treeNode.id])]]),
      treeRulesReady: true,
      resolution: fixtureResolution,
      assessmentIssues: [],
      matchResults: new Map([["defect_0001", { ...matchResult("auto_bound", []), skipped: true }]]),
    }).rows[0];

    expect(row.matchState).toBe("manual");
    expect(row.matchLabel).toBe("人工选择：裂缝");
  });

  it("puts composite, candidate and unmatched rows ahead of batchable ones", () => {
    const draft = safeDraft();
    draft.defects = ["defect_0001", "defect_0002", "defect_0003"].map((candidateId, index) => {
      setResolution(candidateId, {
        bridgeComponentId: "component-1",
        ratingTreeVersionId: "tree-version-1",
        ratingTreeNodeId: index === 0 ? treeNode.id : null,
        ratingMatchMethod: index === 0 ? ("exact" as const) : null,
        hasRating: index === 0,
        activeInstanceCount: 1,
      });
      return { ...draft.defects[0], candidate_id: candidateId, photo_references: [] };
    });
    const model = buildDefectPhotoReviewModel({
      draft,
      ratingTreeVersionId: "tree-version-1",
      ratingTreeNodes: [treeNode],
      applicableTreeNodeIdsByComponent: new Map([["component-1", new Set([treeNode.id])]]),
      treeRulesReady: true,
      assessmentIssues: [],
      matchResults: new Map([
        ["defect_0002", matchResult("candidates", [
          { rating_tree_node_id: treeNode.id, display_name: "裂缝", match_method: "fuzzy_candidate", evidence: "相似" },
        ])],
        ["defect_0003", matchResult("composite", [])],
      ]),
    });

    expect(model.rows.map((row) => row.candidateId))
      .toEqual(["defect_0003", "defect_0002", "defect_0001"]);
  });

  it("filters by composite, candidate and unmatched issue buckets", () => {
    const draft = safeDraft();
    draft.defects = ["defect_0001", "defect_0002"].map((candidateId) => {
      setResolution(candidateId, {
        bridgeComponentId: "component-1",
        ratingTreeVersionId: "tree-version-1",
        activeInstanceCount: 1,
      });
      return { ...draft.defects[0], candidate_id: candidateId, photo_references: [] };
    });
    const base = {
      draft,
      ratingTreeVersionId: "tree-version-1",
      ratingTreeNodes: [treeNode],
      applicableTreeNodeIdsByComponent: new Map([["component-1", new Set([treeNode.id])]]),
      treeRulesReady: true,
      assessmentIssues: [],
      matchResults: new Map([
        ["defect_0001", matchResult("composite", [])],
        ["defect_0002", matchResult("unmatched", [])],
      ]),
    };

    expect(buildDefectPhotoReviewModel({ ...base, issueFilter: "composite" as const })
      .rows.map((row) => row.candidateId)).toEqual(["defect_0001"]);
    expect(buildDefectPhotoReviewModel({ ...base, issueFilter: "unmatched" as const })
      .rows.map((row) => row.candidateId)).toEqual(["defect_0002"]);
    expect(buildDefectPhotoReviewModel(base).summary.composite).toBe(1);
    expect(buildDefectPhotoReviewModel(base).summary.unmatched).toBe(1);
  });

  // 照片问题的唯一来源是卡片：能不能入库由 ConfirmPlan 按"已确认 + 有归档文件"判定，
  // 派生的问题必须和那套判定对齐。
  it("blocks batch confirmation while a Word photo number is still unhandled", () => {
    const draft = safeDraft();
    draft.defects[0].photo_references.push({
      photo_number: "2.1-9",
      resolution: "pending",
      photo_candidate_id: null,
      resolved_defect_candidate_id: null,
      review_note: null,
    });

    const row = buildDefectPhotoReviewModel({
      draft,
      ...treeWiring(),
      assessmentIssues: [],
    }).rows[0];

    expect(row.problems.map((problem) => problem.code)).toContain("photo_reference_pending");
    expect(row.batchEligible).toBe(false);
  });

  it("clears the problem once the missing photo is acknowledged", () => {
    const draft = safeDraft();
    draft.defects[0].photo_references.push({
      photo_number: "2.1-9",
      resolution: "missing",
      photo_candidate_id: null,
      resolved_defect_candidate_id: null,
      review_note: null,
    });

    const row = buildDefectPhotoReviewModel({
      draft,
      ...treeWiring(),
      assessmentIssues: [],
    }).rows[0];

    expect(row.problems).toEqual([]);
    expect(row.batchEligible).toBe(true);
  });

  it("does not require a second confirmation for a linked archived photo", () => {
    const draft = safeDraft();

    const row = buildDefectPhotoReviewModel({
      draft,
      ...treeWiring(),
      assessmentIssues: [],
    }).rows[0];

    expect(row.batchEligible).toBe(true);
  });

  it("flags a confirmed photo whose archived file is missing", () => {
    const draft = safeDraft();
    draft.photos[0] = {
      ...draft.photos[0],
      extracted_file: { ...draft.photos[0].extracted_file, archive_relative_path: null },
    };

    const row = buildDefectPhotoReviewModel({
      draft,
      ...treeWiring(),
      assessmentIssues: [],
    }).rows[0];

    expect(row.problems.map((problem) => problem.code)).toContain("photo_archive_missing");
    expect(row.batchEligible).toBe(false);
  });

  it("exposes the same card list the photo panel renders", () => {
    const row = buildDefectPhotoReviewModel({
      draft: safeDraft(),
      ...treeWiring(),
      assessmentIssues: [],
    }).rows[0];

    expect(row.photoCards.map((card) => [card.kind, card.photoNumber]))
      .toEqual([["photo", "2.1-1"]]);
  });

  it("blocks repeated photo references and searches by photo number", () => {
    const draft = safeDraft();
    draft.defects.push({
      ...draft.defects[0],
      candidate_id: "defect_0002",
      component_number: "2-2#梁",
      photo_references: [{
        photo_number: "2.1-1",
        resolution: "pending",
        photo_candidate_id: null,
        resolved_defect_candidate_id: null,
        review_note: null,
      }],
    });
    const model = buildDefectPhotoReviewModel({
      draft,
      ...treeWiring(),
      assessmentIssues: [],
      filter: "needs_attention",
      issueFilter: "photo_pending",
      search: "2.1-1",
    });

    expect(model.rows).toHaveLength(2);
    expect(model.rows.every((row) =>
      row.problems.some((problem) => problem.code === "photo_number_conflict")
    )).toBe(true);
  });

  describe("照片编号占用统计", () => {
    function conflictModel(draft: ReturnType<typeof safeDraft>) {
      return buildDefectPhotoReviewModel({ draft, ...treeWiring(), assessmentIssues: [] });
    }

    function hasProblem(model: ReturnType<typeof conflictModel>, code: string) {
      return model.rows.some((row) => row.problems.some((problem) => problem.code === code));
    }

    function reference(photoNumber: string, overrides: Record<string, unknown> = {}) {
      return {
        photo_number: photoNumber,
        resolution: "pending" as const,
        photo_candidate_id: null,
        resolved_defect_candidate_id: null,
        review_note: null,
        ...overrides,
      };
    }

    // 范围拆分把整份引用清单复制给了每一侧，两条病害于是都声称拥有 2.1-1。
    // 人工摘掉不属于自己的那条之后冲突必须跟着消失，否则这两条永远确认不了。
    it("clears the conflict once the copied reference is removed", () => {
      const draft = safeDraft();
      draft.defects.push({
        ...draft.defects[0],
        candidate_id: "defect_0002",
        component_number: "2-2#梁",
        photo_references: [
          reference("2.1-1"),
          reference("2.1-2", {
            resolution: "matched",
            photo_candidate_id: "photo_0002",
            resolved_defect_candidate_id: "defect_0002",
          }),
        ],
      });
      draft.photos.push({
        ...draft.photos[0],
        candidate_id: "photo_0002",
        photo_number: "2.1-2",
        linked_defect_candidate_id: "defect_0002",
      });

      expect(hasProblem(conflictModel(draft), "photo_number_conflict")).toBe(true);

      const cleaned = createReviewDraftReducer()(draft, {
        type: "remove_photo_reference",
        defectCandidateId: "defect_0002",
        photoNumber: "2.1-1",
      });

      expect(hasProblem(conflictModel(cleaned), "photo_number_conflict")).toBe(false);
      expect(hasProblem(conflictModel(cleaned), "photo_reference_pending")).toBe(false);
    });

    // 库里 photo_number 没有唯一约束：两条病害各挂一张同编号的图会一路写进报告，
    // 让编号这个交叉引用作废。只数引用条目看不见这种重号。
    it("flags two defects that each carry a photo with the same number", () => {
      const draft = safeDraft();
      draft.defects[0] = { ...draft.defects[0], photo_references: [] };
      draft.defects.push({
        ...draft.defects[0],
        candidate_id: "defect_0002",
        component_number: "2-2#梁",
      });
      draft.photos.push({
        ...draft.photos[0],
        candidate_id: "photo_0002",
        linked_defect_candidate_id: "defect_0002",
      });

      expect(conflictModel(draft).rows.every((row) =>
        row.problems.some((problem) => problem.code === "photo_number_conflict")
      )).toBe(true);
    });

    // 已确认缺图等于当面认了"原报告就没这张图"，不再跟别人抢编号。
    it("stops counting a reference that is confirmed missing", () => {
      const draft = safeDraft();
      draft.defects.push({
        ...draft.defects[0],
        candidate_id: "defect_0002",
        component_number: "2-2#梁",
        photo_references: [reference("2.1-1", { resolution: "missing" })],
      });

      expect(hasProblem(conflictModel(draft), "photo_number_conflict")).toBe(false);
    });

    // 已忽略的病害不入库，占不住任何编号。
    it("stops counting an ignored defect", () => {
      const draft = safeDraft();
      draft.defects.push({
        ...draft.defects[0],
        candidate_id: "defect_0002",
        component_number: "2-2#梁",
        review_status: "已忽略",
        photo_references: [reference("2.1-1")],
      });

      expect(hasProblem(conflictModel(draft), "photo_number_conflict")).toBe(false);
    });
  });
});
