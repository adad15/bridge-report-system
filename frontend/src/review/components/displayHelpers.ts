import type { Ratings, ReviewStatus } from "../../contracts/annualInspection";
import type { AttentionItem } from "../grouping";
import type { RatingTarget } from "../reviewDraft";

// 纯展示辅助函数：不修改任何数据，只把领域数据整理成组件可以直接渲染的形状。
// 供 NeedsAttentionSection / RatingsSection 使用，也是本任务 TDD 的测试目标。

/**
 * 把一条 AttentionItem 格式化为“需要处理”列表里的单行文案：[kind] candidateId: message。
 */
export function formatAttentionItem(item: AttentionItem): string {
  return `[${item.kind}] ${item.candidateId}: ${item.message}`;
}

export interface RatingRow {
  level: "全桥" | "结构分部" | "评价部件";
  name: string;
  score: number;
  grade: string | null;
  weight: number | null;
  status: ReviewStatus;
  // target 与 reviewDraft.ts 的 edit_rating_field/set_rating_status 目标定位方式一致，
  // 让 RatingsSection 可以直接拿某一行的 target 去 dispatch，不需要重新按 level 反查。
  target: RatingTarget;
}

/**
 * 把 Ratings（全桥 + 结构分部[] + 评价部件[]）展开成一份统一的表格行数组，
 * 供 RatingsSection 渲染成单张评分表。评价部件没有等级字段（等级只存在结构分部一级），
 * grade 记为 null；只有结构分部携带权重，全桥和评价部件记为 null。
 */
export function ratingRows(ratings: Ratings): RatingRow[] {
  const rows: RatingRow[] = [
    {
      level: "全桥",
      name: "全桥",
      score: ratings.overall.total_score,
      grade: ratings.overall.overall_grade,
      weight: null,
      status: ratings.overall.review_status,
      target: "overall",
    },
  ];

  for (const part of ratings.structure_parts) {
    rows.push({
      level: "结构分部",
      name: part.structure_part,
      score: part.structure_score,
      grade: part.grade,
      weight: part.weight,
      status: part.review_status,
      target: { part: part.structure_part },
    });
  }

  ratings.evaluation_parts.forEach((part, index) => {
    rows.push({
      level: "评价部件",
      name: part.evaluation_part,
      score: part.part_score,
      grade: null,
      weight: null,
      status: part.review_status,
      target: { evaluation: index },
    });
  });

  return rows;
}
