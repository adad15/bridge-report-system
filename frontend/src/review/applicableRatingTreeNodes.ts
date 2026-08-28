// 一条来源病害能选哪些评定树病害节点。
//
// 节点适用性只取决于构件的 (桥型, 规范类别)，所以答案对单构件病害就是那件构件的适用
// 集合。区间展开的病害一条挂多件构件，正式确认时每件各生成一条病害观测、各自参与评分，
// 因此下拉框只能给出**对每一件都适用**的节点——交集。给并集的话，用户能选中一个只对
// 其中几件成立的节点，随后判据（defectPhotoReviewModel 按 every 判）又把它标成
// "不适用于当前实际构件"，选完即错。
//
// 少了这个函数各处就会退回"按单一 bridgeComponentId 取"，而那一项对多实例病害按约定
// 是 null，下拉框直接空掉。

import type { RatingTreeNodeSummary } from "../api/ratingTreeApi";
import { sortRatingTreeNodes } from "../rating-tree/ratingTreeLabels";

/**
 * 取这些构件共同适用的评定树病害节点。
 *
 * 构件集合为空时返回空数组：没绑构件就无从确定适用范围，这与"绑了但交集为空"是两回事，
 * 但对下拉框而言都是没有可选项。某个构件在表里查不到（适用节点还没加载完）同样按空处理。
 */
export function applicableRatingTreeNodes(
  componentIds: readonly string[],
  nodesByComponent: ReadonlyMap<string, RatingTreeNodeSummary[]>,
): RatingTreeNodeSummary[] {
  const unique = [...new Set(componentIds)];
  if (unique.length === 0) return [];
  const first = nodesByComponent.get(unique[0]) ?? [];
  const rest = unique.slice(1).map(
    (componentId) => new Set((nodesByComponent.get(componentId) ?? []).map((node) => node.id)),
  );
  return sortRatingTreeNodes(first.filter((node) => rest.every((ids) => ids.has(node.id))));
}
