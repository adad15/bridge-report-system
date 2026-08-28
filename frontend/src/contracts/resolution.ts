// 构件解析与评分树解析的前端类型。
//
// 这些值 5.0 之前挂在 `DefectCandidate` 上，现在归关系表所有
// （`import_rating_resolutions.match_method`，见
// docs/superpowers/specs/2026-08-27-import-component-rating-resolution-separation-design.md §8.5）。
// 它们不再是来源事实的一部分，所以也不放在 annualInspection.ts 里——放回去会让
// 下一个人以为草稿又能带解析字段了。

/**
 * 评定树病害的匹配方式。前三种是后端分层确定性匹配写入的自动结果，`fuzzy_candidate`
 * 只是候选提示（永远不落 rating_tree_node_id），`manual` 是人工选择且不被自动结果覆盖。
 */
export const RATING_TREE_MATCH_METHODS = [
  "exact",
  "controlled_alias",
  "controlled_keyword",
  "fuzzy_candidate",
  // 来源软件直接标注的评定指标：不是从文字推断的，来源要能区分开
  "source_indicator",
  "manual",
] as const;

export type RatingTreeMatchMethod = (typeof RATING_TREE_MATCH_METHODS)[number];

/** 构件解析的匹配方式，对应 `import_component_resolution_groups.match_method`。 */
export const COMPONENT_RESOLUTION_MATCH_METHODS = [
  "exact",
  "confirmed_alias",
  "manual",
  "side_pair",
  "range",
] as const;

export type ComponentResolutionMatchMethod =
  (typeof COMPONENT_RESOLUTION_MATCH_METHODS)[number];

/** 来源构件组的解析状态。`ambiguous` 是读模型派生标签，不是这里的状态（§4.7）。 */
export type ComponentResolutionStatus = "unresolved" | "bound" | "missing";

/** 病害解析实例的状态（§9.3）。 */
export type DefectInstanceStatus = "active" | "ignored";

/** 评分树解析状态（§9.2）。 */
export type RatingResolutionStatus = "unresolved" | "matched";
