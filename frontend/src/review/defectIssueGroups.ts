import type { DefectReviewRow } from "./defectPhotoReviewModel";

export type DefectIssueGroupKind = "unmatched" | "problem";

export interface DefectIssueGroup {
  key: string;
  kind: DefectIssueGroupKind;
  rows: DefectReviewRow[];
  title: string;
  sourceGroupNumber: string | null;
  sourceIndicatorNumber: string | null;
  componentCategoryId: string | null;
  hasExactSourceIdentity: boolean;
  problemMessages: string[];
  rangeSplitConfirmableRows: DefectReviewRow[];
}

function clean(value: string | null | undefined): string | null {
  const result = value?.trim();
  return result ? result : null;
}

function groupTitle(rows: DefectReviewRow[]): string {
  const names = [...new Set(rows
    .map((row) => clean(row.defect.defect_type))
    .filter((value): value is string => value !== null))];
  if (names.length === 1) return names[0];

  const descriptions = [...new Set(rows
    .map((row) => clean(row.defect.defect_description))
    .filter((value): value is string => value !== null))];
  if (descriptions.length === 1) return descriptions[0];
  return names[0] ?? descriptions[0] ?? "未命名病害";
}

export function buildDefectIssueGroups(rows: DefectReviewRow[]): DefectIssueGroup[] {
  const buckets = new Map<string, DefectReviewRow[]>();
  for (const row of rows) {
    if (row.status !== "needs_attention") continue;
    const defect = row.defect;
    const groupId = clean(defect.source_defect_group_id);
    const indicatorId = clean(defect.source_defect_indicator_id);
    const categoryId = clean(defect.standard_component_category_id);
    const unmatched = !defect.rating_tree_node_id && row.matchState === "unmatched";
    const exactIdentity = groupId && indicatorId
      ? `${groupId}\u0000${indicatorId}`
      : `candidate:${defect.candidate_id}`;
    const problemKey = unmatched
      ? "unmatched"
      : row.problems.map((problem) => problem.code).sort().join("|") || "other";
    const contextKey = unmatched
      ? exactIdentity
      : defect.rating_tree_node_id ?? exactIdentity;
    const key = `${problemKey}\u0000${contextKey}\u0000${categoryId ?? ""}`;
    const bucket = buckets.get(key) ?? [];
    bucket.push(row);
    buckets.set(key, bucket);
  }

  return [...buckets.entries()]
    .map(([key, groupedRows]) => {
      const first = groupedRows[0].defect;
      const kind: DefectIssueGroupKind =
        !first.rating_tree_node_id && groupedRows[0].matchState === "unmatched"
          ? "unmatched"
          : "problem";
      return {
        key,
        kind,
        rows: groupedRows,
        title: groupTitle(groupedRows),
        sourceGroupNumber: clean(first.source_defect_group_number),
        sourceIndicatorNumber: clean(first.source_defect_indicator_number),
        componentCategoryId: clean(first.standard_component_category_id),
        hasExactSourceIdentity: Boolean(
          clean(first.source_defect_group_id) && clean(first.source_defect_indicator_id),
        ),
        problemMessages: [...new Set(groupedRows.flatMap((row) =>
          row.problems.map((problem) => problem.message)))],
        rangeSplitConfirmableRows: groupedRows.filter((row) =>
          row.confirmEligible && row.problems.some(
            (problem) => problem.code === "component_range_split_review_required",
          )),
      };
    })
    .sort((left, right) =>
      right.rows.length - left.rows.length || left.title.localeCompare(right.title, "zh-CN"));
}
