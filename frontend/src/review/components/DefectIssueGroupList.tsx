import { useMemo, useState } from "react";

import type { RatingTreeNodeSummary } from "../../api/ratingTreeApi";
import {
  ratingTreeOptionLabel,
  sortRatingTreeNodes,
} from "../../rating-tree/ratingTreeLabels";
import type { DefectIssueGroup } from "../defectIssueGroups";

interface DefectIssueGroupListProps {
  groups: DefectIssueGroup[];
  nodesByComponent: ReadonlyMap<string, RatingTreeNodeSummary[]>;
  disabled?: boolean;
  onApplyNode: (group: DefectIssueGroup, node: RatingTreeNodeSummary) => void;
  onConfirmGroup: (group: DefectIssueGroup) => void;
  onOpenDefect: (candidateId: string) => void;
}

function commonNodes(
  group: DefectIssueGroup,
  nodesByComponent: ReadonlyMap<string, RatingTreeNodeSummary[]>,
): RatingTreeNodeSummary[] {
  const boundIds = group.rows.map((row) => row.resolution.bridgeComponentId);
  const componentIds = [...new Set(boundIds.filter((id): id is string => Boolean(id)))];
  if (componentIds.length === 0 || componentIds.length !== new Set(boundIds).size) {
    return [];
  }
  const first = nodesByComponent.get(componentIds[0]) ?? [];
  const remaining = componentIds.slice(1).map((id) =>
    new Set((nodesByComponent.get(id) ?? []).map((node) => node.id)));
  return sortRatingTreeNodes(
    first.filter((node) => remaining.every((ids) => ids.has(node.id))),
  );
}

export function DefectIssueGroupList({
  groups,
  nodesByComponent,
  disabled = false,
  onApplyNode,
  onConfirmGroup,
  onOpenDefect,
}: DefectIssueGroupListProps) {
  const [selectedNodes, setSelectedNodes] = useState<Map<string, string>>(new Map());
  const nodeOptions = useMemo(
    () => new Map(groups.map((group) => [group.key, commonNodes(group, nodesByComponent)])),
    [groups, nodesByComponent],
  );

  if (groups.length === 0) {
    return <p className="empty-review-result">当前筛选下没有可分组的待处理病害。</p>;
  }

  return (
    <div className="defect-issue-groups">
      <div className="defect-issue-groups-summary">
        <strong>{groups.length} 个问题组</strong>
        <span>共 {groups.reduce((total, group) => total + group.rows.length, 0)} 条待处理病害</span>
      </div>
      {groups.map((group) => {
        const options = nodeOptions.get(group.key) ?? [];
        const selectedNodeId = selectedNodes.get(group.key) ?? "";
        const selectedNode = options.find((node) => node.id === selectedNodeId);
        const canAssign =
          group.kind === "unmatched" &&
          group.hasExactSourceIdentity &&
          group.componentCategoryId !== null &&
          options.length > 0;
        const confirmableCount = group.rangeSplitConfirmableRows.length;
        const excludedCount = group.rows.length - confirmableCount;
        const componentNumbers = [...new Set(group.rows
          .map((row) => row.defect.component_number ?? row.defect.component_name))];
        return (
          <section className="defect-issue-group" key={group.key}>
            <div className="defect-issue-group-heading">
              <div>
                <p>{group.kind === "unmatched" ? "无匹配结果" : "待处理问题"}</p>
                <h3>{group.title}</h3>
              </div>
              <strong>{group.rows.length} 条</strong>
            </div>
            <div className="defect-issue-group-meta">
              {group.sourceGroupNumber || group.sourceIndicatorNumber ? (
                <span>来源编号 {group.sourceGroupNumber ?? "?"} / {group.sourceIndicatorNumber ?? "?"}</span>
              ) : null}
              <span>
                构件 {componentNumbers.slice(0, 4).join("、")}
                {componentNumbers.length > 4 ? ` 等 ${componentNumbers.length} 个` : ""}
              </span>
            </div>
            <ul className="defect-issue-group-problems">
              {group.problemMessages.slice(0, 3).map((message) => <li key={message}>{message}</li>)}
            </ul>
            <div className="defect-issue-group-samples">
              {group.rows.slice(0, 3).map((row) => (
                <button
                  type="button"
                  key={row.candidateId}
                  onClick={() => onOpenDefect(row.candidateId)}
                >
                  {row.defect.component_number ?? row.defect.component_name}
                  <span>{row.defect.defect_location || "未记录位置"}</span>
                </button>
              ))}
            </div>
            {canAssign ? (
              <div className="defect-issue-group-action">
                <select
                  aria-label={`为 ${group.title} 选择评定树病害`}
                  disabled={disabled}
                  value={selectedNodeId}
                  onChange={(event) => setSelectedNodes((current) => {
                    const next = new Map(current);
                    next.set(group.key, event.target.value);
                    return next;
                  })}
                >
                  <option value="">请选择评定树病害</option>
                  {options.map((node) => (
                    <option key={node.id} value={node.id}>
                      {ratingTreeOptionLabel(node, options)}{node.is_scoring ? "" : "（暂不计分）"}
                    </option>
                  ))}
                </select>
                <button
                  type="button"
                  className="review-action-primary"
                  disabled={disabled || !selectedNode}
                  onClick={() => selectedNode && onApplyNode(group, selectedNode)}
                >
                  应用到本组 {group.rows.length} 条
                </button>
              </div>
            ) : confirmableCount > 0 ? (
              <div className="defect-issue-group-action defect-issue-group-confirm-action">
                <p className="defect-issue-group-manual">
                  {excludedCount > 0
                    ? `${confirmableCount} 条可确认，${excludedCount} 条存在其他问题。`
                    : "本组仅需确认构件范围拆分结果。"}
                </p>
                <button
                  type="button"
                  className="review-action-primary"
                  disabled={disabled}
                  onClick={() => onConfirmGroup(group)}
                >
                  确认本组可确认项（{confirmableCount}）
                </button>
              </div>
            ) : (
              <p className="defect-issue-group-manual">
                {group.kind === "unmatched" && !group.hasExactSourceIdentity
                  ? "来源身份不完整，需逐条核对。"
                  : "本组包含其他待处理项，需打开样例继续处理。"}
              </p>
            )}
          </section>
        );
      })}
    </div>
  );
}
