import type { Dispatch } from "react";

import type { ComponentInventoryEntry, ComponentInventoryRevision, StructurePart as InventoryStructurePart } from "../../api/componentInventoryApi";
import type { DefectCandidate, StructurePart } from "../../contracts/annualInspection";
import { reviewTargetId } from "../reviewNavigation";
import type { ReviewDraftAction } from "../reviewDraft";

const STRUCTURE_PART_LABELS: Record<InventoryStructurePart, StructurePart> = {
  overall: "全桥",
  superstructure: "上部结构",
  substructure: "下部结构",
  deck_system: "桥面系",
  other: "其他",
};

interface ComponentMatchFieldProps {
  defect: DefectCandidate;
  inventory: ComponentInventoryRevision | null;
  dispatch: Dispatch<ReviewDraftAction>;
  disabled?: boolean;
}

function usableEntries(inventory: ComponentInventoryRevision | null): ComponentInventoryEntry[] {
  if (!inventory) return [];
  return inventory.entries.filter(
    (entry) => entry.is_active && entry.mappings.some((mapping) => mapping.is_active),
  );
}

function matchLabel(defect: DefectCandidate, inventory: ComponentInventoryRevision | null): string {
  if (!defect.bridge_component_id) {
    if ((defect.component_match_candidate_ids?.length ?? 0) > 0) return "存在候选，等待人工确认";
    return inventory ? "尚未匹配" : "尚未建立构件台账";
  }
  if (defect.component_match_method === "exact") return "系统精确匹配";
  if (defect.component_match_method === "confirmed_alias") return "系统按已确认别名匹配";
  if (defect.component_match_method === "manual") return "人工已选择";
  return "已关联";
}

export function ComponentMatchField({ defect, inventory, dispatch, disabled = false }: ComponentMatchFieldProps) {
  const candidates = new Set(defect.component_match_candidate_ids ?? []);
  const ordered = usableEntries(inventory).sort((left, right) => {
    const leftCandidate = candidates.has(left.bridge_component_id) ? 0 : 1;
    const rightCandidate = candidates.has(right.bridge_component_id) ? 0 : 1;
    return leftCandidate - rightCandidate || left.sort_order - right.sort_order;
  });
  const inventoryConfirmed = inventory?.status === "已确认" || inventory?.status === "confirmed";
  return (
    <div
      id={reviewTargetId("defect-field", defect.candidate_id, "component_match")}
      className="component-match-field defect-fact-span"
    >
      <select
        aria-label="实际构件"
        disabled={disabled || !inventory || ordered.length === 0}
        value={defect.bridge_component_id ?? ""}
        onChange={(event) => {
          const entry = ordered.find((item) => item.bridge_component_id === event.target.value);
          const mapping = entry?.mappings.find((item) => item.is_active);
          if (!entry || !mapping || !inventory) return;
          dispatch({
            type: "link_defect_component",
            candidateId: defect.candidate_id,
            component: {
              componentName: entry.site_component_type,
              componentNumber: entry.component_number,
              bridgeComponentId: entry.bridge_component_id,
              standardComponentCategoryId: mapping.standard_component_category_id,
              resolvedStructurePart: STRUCTURE_PART_LABELS[mapping.structure_part],
              inventoryRevisionId: inventory.id,
            },
          });
        }}
      >
        <option value="">请选择实际构件</option>
        {ordered.map((entry) => (
          <option key={entry.id} value={entry.bridge_component_id}>
            {candidates.has(entry.bridge_component_id) ? "候选 · " : ""}{entry.component_number} / {entry.site_component_type}
          </option>
        ))}
      </select>
      <small>{matchLabel(defect, inventory)}{inventory && !inventoryConfirmed ? "；当前台账尚未确认" : ""}</small>
    </div>
  );
}
