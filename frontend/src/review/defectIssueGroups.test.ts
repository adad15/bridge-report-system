import { describe, expect, it } from "vitest";

import { data } from "./testFixtures";
import type { DefectReviewRow } from "./defectPhotoReviewModel";
import { buildDefectIssueGroups } from "./defectIssueGroups";
import { UNRESOLVED } from "./resolutionIndex";

function unmatchedRow(
  candidateId: string,
  groupId: string | null,
  indicatorId: string | null,
  description: string,
): DefectReviewRow {
  const defect = {
    ...data().defects[0],
    candidate_id: candidateId,
    component_number: candidateId,
    source_defect_group_id: groupId,
    source_defect_group_number: "5.1.1",
    source_defect_indicator_id: indicatorId,
    source_defect_indicator_number: "5.1.1-8",
    defect_type: "",
    defect_description: description,
    photo_references: [],
  };
  return {
    candidateId,
    defect,
    // 5.0：解析状态挂在行上，不在 defect 上。
    resolution: {
      ...UNRESOLVED,
      bridgeComponentId: `component-${candidateId}`,
      standardComponentCategoryId: "h21.component.deck.slab",
      activeInstanceCount: 1,
    },
    photos: [],
    problems: [{
      code: "rating_tree_node_required",
      category: "defect_type",
      message: "尚未选择评定树病害。",
    }],
    confirmEligible: false,
    batchEligible: false,
    status: "needs_attention",
    matchState: "unmatched",
    matchLabel: "未找到匹配",
    matchResult: null,
    matchCandidates: [],
    photoCards: [],
    ratingTreeNode: null,
  };
}

function rangeSplitRow(candidateId: string, confirmEligible = true): DefectReviewRow {
  const result = unmatchedRow(candidateId, "group-a", "indicator-a", "渗水泛碱");
  result.resolution = {
    ...result.resolution,
    ratingTreeNodeId: "tree-node-water",
    ratingMatchMethod: "source_indicator",
  };
  result.defect.defect_type = "渗水泛碱";
  result.problems = [{
    code: "component_range_split_review_required",
    category: "other",
    message: "该病害由构件范围拆分，请人工核对构件、病害和照片关联。",
  }];
  result.confirmEligible = confirmEligible;
  result.matchState = "auto_bound";
  result.matchLabel = "来源软件标注";
  return result;
}

describe("buildDefectIssueGroups", () => {
  it("groups unmatched defects only when their exact source identity and component category match", () => {
    const groups = buildDefectIssueGroups([
      unmatchedRow("defect-1", "group-a", "indicator-a", "存在黑点痕迹"),
      unmatchedRow("defect-2", "group-a", "indicator-a", "存在黑点痕迹"),
      unmatchedRow("defect-3", "group-b", "indicator-a", "存在黑点痕迹"),
    ]);

    expect(groups.map((group) => group.rows.map((row) => row.candidateId))).toEqual([
      ["defect-1", "defect-2"],
      ["defect-3"],
    ]);
    expect(groups[0]).toMatchObject({
      kind: "unmatched",
      title: "存在黑点痕迹",
      hasExactSourceIdentity: true,
      sourceIndicatorNumber: "5.1.1-8",
    });
  });

  it("keeps records with incomplete source ids separate even when their text and numbers match", () => {
    const groups = buildDefectIssueGroups([
      unmatchedRow("defect-1", null, null, "存在黑点痕迹"),
      unmatchedRow("defect-2", null, null, "存在黑点痕迹"),
    ]);

    expect(groups).toHaveLength(2);
    expect(groups.every((group) => !group.hasExactSourceIdentity)).toBe(true);
  });

  it("does not include batchable or confirmed rows in issue groups", () => {
    const batchable = unmatchedRow("defect-1", "group-a", "indicator-a", "裂缝");
    batchable.status = "batchable";
    batchable.batchEligible = true;

    expect(buildDefectIssueGroups([batchable])).toEqual([]);
  });

  it("keeps only individually safe range-split rows in the group confirmation set", () => {
    const confirmable = rangeSplitRow("defect-1");
    const blocked = rangeSplitRow("defect-2", false);

    const [group] = buildDefectIssueGroups([confirmable, blocked]);

    expect(group.kind).toBe("problem");
    expect(group.rangeSplitConfirmableRows.map((row) => row.candidateId)).toEqual(["defect-1"]);
  });
});
