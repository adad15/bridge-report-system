import { useMemo } from "react";

import type { ComponentInventoryEntry, ComponentInventoryRevision } from "../../api/componentInventoryApi";
import type { DefectCandidate } from "../../contracts/annualInspection";
import { reviewTargetId } from "../reviewNavigation";

interface ComponentMatchFieldProps {
  defect: DefectCandidate;
  inventory: ComponentInventoryRevision | null;
}

function usableEntries(inventory: ComponentInventoryRevision | null): ComponentInventoryEntry[] {
  if (!inventory) return [];
  return inventory.entries.filter(
    (entry) => entry.is_active && entry.mappings.some((mapping) => mapping.is_active),
  );
}

function matchLabel(defect: DefectCandidate, inventory: ComponentInventoryRevision | null): string {
  if (defect.component_match_method === "missing") return "已在绑定界面标记缺失";
  if (!defect.bridge_component_id) {
    if ((defect.component_match_candidate_ids?.length ?? 0) > 0) return "存在候选，请在构件绑定界面确认";
    return inventory ? "尚未绑定，请在构件绑定界面处理" : "尚未建立构件台账";
  }
  if (defect.component_match_method === "exact") return "系统精确匹配";
  if (defect.component_match_method === "confirmed_alias") return "系统按已确认别名匹配";
  if (defect.component_match_method === "manual") return "已在绑定界面人工绑定";
  return "已关联";
}

// §4-E：校对页的"实际构件"只读展示，绑定统一在构件绑定界面完成。
export function ComponentMatchField({ defect, inventory }: ComponentMatchFieldProps) {
  const usable = useMemo(() => usableEntries(inventory), [inventory]);
  const bound = defect.bridge_component_id
    ? usable.find((entry) => entry.bridge_component_id === defect.bridge_component_id)
    : undefined;
  const display = bound
    ? `${bound.component_number} / ${bound.site_component_type}`
    : defect.component_match_method === "missing"
      ? "已标记缺失"
      : "未绑定";

  return (
    <div
      id={reviewTargetId("defect-field", defect.candidate_id, "component_match")}
      className="component-match-field defect-fact-span"
    >
      <span aria-label="实际构件" className="component-match-readonly">{display}</span>
      <small>{matchLabel(defect, inventory)}</small>
    </div>
  );
}
