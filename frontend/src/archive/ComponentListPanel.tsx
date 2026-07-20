import { useState } from "react";

import type { ComponentSummary } from "../api/componentArchiveApi";

function roundScoreToTwoDecimals(value: number): number {
  return Math.round((value + Number.EPSILON) * 100) / 100;
}

interface ComponentListPanelProps {
  components: ComponentSummary[];
  selectedComponentId: string | null;
  onSelect: (componentId: string) => void;
}

const STRUCTURE_PART_FILTERS = ["全部", "上部结构", "下部结构", "桥面系", "全桥", "其他"] as const;

// A1 左侧构件列表（模块 06 §7.2）：收录任意年度当前有效版本中出现过正式病害的构件，
// 最新年度未出现也不会消失。支持关键字搜索、结构分部筛选与"存在未绑定观测"筛选。
export function ComponentListPanel({ components, selectedComponentId, onSelect }: ComponentListPanelProps) {
  const [keyword, setKeyword] = useState("");
  const [structurePart, setStructurePart] = useState<(typeof STRUCTURE_PART_FILTERS)[number]>("全部");
  const [unboundOnly, setUnboundOnly] = useState(false);

  const normalizedKeyword = keyword.trim();
  const filtered = components.filter((component) => {
    if (structurePart !== "全部" && component.structure_part !== structurePart) return false;
    if (unboundOnly && component.unbound_count === 0) return false;
    if (normalizedKeyword === "") return true;
    return (
      component.component_type.includes(normalizedKeyword) ||
      component.business_component_code.includes(normalizedKeyword) ||
      component.system_number.includes(normalizedKeyword)
    );
  });

  return (
    <aside className="archive-list-panel">
      <div className="archive-list-filters">
        <input
          aria-label="搜索构件"
          placeholder="搜索构件编号 / 部件名称…"
          value={keyword}
          onChange={(event) => setKeyword(event.target.value)}
        />
        <select
          aria-label="结构分部筛选"
          value={structurePart}
          onChange={(event) => setStructurePart(event.target.value as (typeof STRUCTURE_PART_FILTERS)[number])}
        >
          {STRUCTURE_PART_FILTERS.map((part) => (
            <option key={part} value={part}>
              {part}
            </option>
          ))}
        </select>
        <label className="archive-unbound-filter">
          <input type="checkbox" checked={unboundOnly} onChange={(event) => setUnboundOnly(event.target.checked)} />
          仅看有未绑定观测
        </label>
      </div>
      {filtered.length === 0 ? (
        <p className="archive-empty-hint">没有符合条件的构件。</p>
      ) : (
        <ul className="archive-component-list">
          {filtered.map((component) => (
            <li key={component.id}>
              <button
                type="button"
                className={component.id === selectedComponentId ? "archive-component-item active" : "archive-component-item"}
                onClick={() => onSelect(component.id)}
              >
                <span className="archive-component-title">
                  <strong>{component.component_type}</strong>
                  <small>{component.structure_part}｜{component.business_component_code}</small>
                </span>
                <span className="archive-component-meta">
                  <span>线索 {component.thread_count}</span>
                  <span className={component.unbound_count > 0 ? "archive-unbound-count" : ""}>
                    未绑定 {component.unbound_count}
                  </span>
                  <span>
                    {component.first_seen_year === component.latest_seen_year
                      ? component.first_seen_year
                      : `${component.first_seen_year}-${component.latest_seen_year}`}
                  </span>
                  {component.latest_score !== null ? (
                    <span>评分 {roundScoreToTwoDecimals(component.latest_score)}</span>
                  ) : null}
                </span>
              </button>
            </li>
          ))}
        </ul>
      )}
    </aside>
  );
}
