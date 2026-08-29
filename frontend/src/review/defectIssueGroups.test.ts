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

// 问题组的标题给规范名，不给报告原文（迁移 029）。
//
// 组本来就是按同一个来源身份、同一类问题归的，标题给规范名才说得清这一组是什么。报告
// 原文是「失效」「破损」这种时，拿它当标题等于没说——实测这座桥 275 条自动匹配的病害里
// 273 条的 defect_type 仍是原文。
it("titles a group by the rating tree node rather than the report wording", () => {
  const rows = [unmatchedRow("d1", "group-a", "indicator-a", "失效")];
  rows[0].ratingTreeNode = {
    id: "tree-node-joint",
    node_key: "org.bridge.defect.10_2_1_4",
    display_number: "10.2.1-4",
    display_name: "伸缩缝失效",
  } as unknown as DefectReviewRow["ratingTreeNode"];

  const groups = buildDefectIssueGroups(rows);

  expect(groups).toHaveLength(1);
  expect(groups[0].title).toBe("10.2.1-4 伸缩缝失效");
});

// 一条都没定评定树时，报告原文是唯一能说的东西，仍要退回去。
it("falls back to the report wording when no row has a node yet", () => {
  const row = unmatchedRow("d1", "group-a", "indicator-a", "板底存在垂黑痕迹");
  row.defect.defect_type = "渗水泛碱";

  const groups = buildDefectIssueGroups([row]);

  expect(groups).toHaveLength(1);
  expect(groups[0].title).toBe("渗水泛碱");
});

// 病害名称也空着时才退到描述——报告里确实有整列留空的行。
it("falls back to the description when even the wording is blank", () => {
  const groups = buildDefectIssueGroups([
    unmatchedRow("d1", "group-a", "indicator-a", "板底存在垂黑痕迹"),
  ]);

  expect(groups).toHaveLength(1);
  expect(groups[0].title).toBe("板底存在垂黑痕迹");
});
