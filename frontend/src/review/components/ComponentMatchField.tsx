import { useMemo, useState, type Dispatch } from "react";

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

// 台账可能有数千个构件；下拉只渲染"已关联 + 系统候选 + 搜索结果前若干项"，
// 绝不能把全量台账塞进每张病害卡的 <option> 列表（数百卡 × 数千项会直接冻死页面）。
const MAX_SEARCH_RESULTS = 20;

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
    return inventory ? "尚未匹配，可搜索后选择" : "尚未建立构件台账";
  }
  if (defect.component_match_method === "exact") return "系统精确匹配";
  if (defect.component_match_method === "confirmed_alias") return "系统按已确认别名匹配";
  if (defect.component_match_method === "manual") return "人工已选择";
  return "已关联";
}

export function ComponentMatchField({ defect, inventory, dispatch, disabled = false }: ComponentMatchFieldProps) {
  const [search, setSearch] = useState("");
  const usable = useMemo(() => usableEntries(inventory), [inventory]);
  const byComponentId = useMemo(
    () => new Map(usable.map((entry) => [entry.bridge_component_id, entry])),
    [usable],
  );

  const term = search.trim();
  const options = useMemo(() => {
    const picked = new Map<string, ComponentInventoryEntry>();
    const linked = defect.bridge_component_id ? byComponentId.get(defect.bridge_component_id) : undefined;
    if (linked) picked.set(linked.bridge_component_id, linked);
    for (const candidateId of defect.component_match_candidate_ids ?? []) {
      const entry = byComponentId.get(candidateId);
      if (entry) picked.set(entry.bridge_component_id, entry);
    }
    if (term) {
      for (const entry of usable) {
        if (picked.size >= MAX_SEARCH_RESULTS + (linked ? 1 : 0)) break;
        if (
          entry.component_number.includes(term) ||
          entry.site_component_type.includes(term) ||
          entry.site_name.includes(term)
        ) {
          picked.set(entry.bridge_component_id, entry);
        }
      }
    }
    return [...picked.values()];
  }, [byComponentId, defect.bridge_component_id, defect.component_match_candidate_ids, term, usable]);

  const candidates = new Set(defect.component_match_candidate_ids ?? []);
  const inventoryConfirmed = inventory?.status === "已确认" || inventory?.status === "confirmed";
  return (
    <div
      id={reviewTargetId("defect-field", defect.candidate_id, "component_match")}
      className="component-match-field defect-fact-span"
    >
      <input
        aria-label="搜索实际构件"
        className="component-match-search"
        disabled={disabled || !inventory || usable.length === 0}
        placeholder="输入编号或名称搜索"
        value={search}
        onChange={(event) => setSearch(event.target.value)}
      />
      <select
        aria-label="实际构件"
        disabled={disabled || !inventory || usable.length === 0}
        value={defect.bridge_component_id ?? ""}
        onChange={(event) => {
          const entry = byComponentId.get(event.target.value);
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
        <option value="">
          {term && options.length === 0 ? "没有匹配的构件" : "请选择实际构件（可先搜索）"}
        </option>
        {options.map((entry) => (
          <option key={entry.id} value={entry.bridge_component_id}>
            {candidates.has(entry.bridge_component_id) ? "候选 · " : ""}{entry.component_number} / {entry.site_component_type}
          </option>
        ))}
      </select>
      <small>{matchLabel(defect, inventory)}{inventory && !inventoryConfirmed ? "；当前台账尚未确认" : ""}</small>
    </div>
  );
}
