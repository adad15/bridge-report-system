import { DownOutlined, RightOutlined, SearchOutlined } from "@ant-design/icons";
import { Input, Select } from "antd";
import { useLayoutEffect, useRef, useState } from "react";

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

/** 分组按桥梁自身的结构顺序排，工程师就是这么想一座桥的，不按字典序。 */
const STRUCTURE_PART_ORDER = ["上部结构", "下部结构", "桥面系", "全桥", "其他"];

const SORT_MODES = {
  score: "评分低→高",
  code: "按编号",
  unbound: "待整理优先",
} as const;

type SortMode = keyof typeof SORT_MODES;

interface ComponentGroup {
  key: string;
  structurePart: string;
  componentType: string;
  items: ComponentSummary[];
  /** 组内最低评分：不展开也能看出哪一组里藏着差构件。 */
  worstScore: number | null;
  unboundTotal: number;
}

function structurePartRank(part: string): number {
  const index = STRUCTURE_PART_ORDER.indexOf(part);
  return index === -1 ? STRUCTURE_PART_ORDER.length : index;
}

function buildGroups(components: ComponentSummary[], sort: SortMode): ComponentGroup[] {
  const byKey = new Map<string, ComponentGroup>();
  for (const component of components) {
    const key = `${component.structure_part} · ${component.component_type}`;
    let group = byKey.get(key);
    if (!group) {
      group = {
        key,
        structurePart: component.structure_part,
        componentType: component.component_type,
        items: [],
        worstScore: null,
        unboundTotal: 0,
      };
      byKey.set(key, group);
    }
    group.items.push(component);
    group.unboundTotal += component.unbound_count;
    if (component.latest_score !== null) {
      group.worstScore = group.worstScore === null
        ? component.latest_score
        : Math.min(group.worstScore, component.latest_score);
    }
  }

  const groups = [...byKey.values()].sort((left, right) => {
    const byPart = structurePartRank(left.structurePart) - structurePartRank(right.structurePart);
    return byPart !== 0 ? byPart : left.componentType.localeCompare(right.componentType, "zh");
  });

  for (const group of groups) group.items.sort(compareBy(sort));
  return groups;
}

function compareBy(sort: SortMode) {
  return (left: ComponentSummary, right: ComponentSummary): number => {
    if (sort === "unbound" && left.unbound_count !== right.unbound_count) {
      return right.unbound_count - left.unbound_count;
    }
    if (sort === "score") {
      // 没有评分的排在最后：它不是"0 分"，只是还没算出来。
      if (left.latest_score === null || right.latest_score === null) {
        if (left.latest_score !== right.latest_score) return left.latest_score === null ? 1 : -1;
      } else if (left.latest_score !== right.latest_score) {
        return left.latest_score - right.latest_score;
      }
    }
    return left.business_component_code.localeCompare(right.business_component_code, "zh", {
      numeric: true,
    });
  };
}

// A1 左侧构件列表（模块 06 §7.2）：收录任意年度当前有效版本中出现过正式病害的构件，
// 最新年度未出现也不会消失。支持关键字搜索、结构分部筛选与组内排序。
//
// 按「结构分部 · 构件类型」分组：一座桥有几百个构件，平铺时每行长得一模一样，
// 真正区分彼此的是编号而不是类型。所以编号当主标题，类型收进分组标题。
//
// 这一屏的密度全靠"不印常量"：结构分部进分组标题，0 条待整理不渲染，
// 年度跨度整个去掉（要看跨度点进档案页就是）。剩下编号和评分两样，一行放两个。
export function ComponentListPanel({ components, selectedComponentId, onSelect }: ComponentListPanelProps) {
  const [keyword, setKeyword] = useState("");
  const [structurePart, setStructurePart] = useState<(typeof STRUCTURE_PART_FILTERS)[number]>("全部");
  const [sort, setSort] = useState<SortMode>("score");
  const [collapsedKeys, setCollapsedKeys] = useState<Set<string>>(() => new Set());
  const listRef = useRef<HTMLDivElement | null>(null);
  const selectedItemRef = useRef<HTMLLIElement | null>(null);

  const normalizedKeyword = keyword.trim();
  const filtered = components.filter((component) => {
    if (structurePart !== "全部" && component.structure_part !== structurePart) return false;
    if (normalizedKeyword === "") return true;
    return (
      component.component_type.includes(normalizedKeyword) ||
      component.business_component_code.includes(normalizedKeyword) ||
      component.system_number.includes(normalizedKeyword)
    );
  });
  const groups = buildGroups(filtered, sort);

  /**
   * 只有选中项**不在可视范围内**时才滚动，且只滚到刚好露出为止。
   *
   * 这个效果是给深链接用的（URL 直接带构件 id，列表得把它露出来）。但它同样会在
   * 点击时触发——无条件把选中项顶到列表最上面，表现就是"点哪儿，哪儿往上跳"。
   * 用户点的是眼前看得见的那一行，界面不该动。
   */
  useLayoutEffect(() => {
    const list = listRef.current;
    const selectedItem = selectedItemRef.current;
    if (!list || !selectedItem) return;
    const top = selectedItem.offsetTop - list.offsetTop;
    const bottom = top + selectedItem.offsetHeight;
    if (top >= list.scrollTop && bottom <= list.scrollTop + list.clientHeight) return;
    list.scrollTop = top < list.scrollTop ? top : bottom - list.clientHeight;
  }, [selectedComponentId, filtered.length]);

  function toggleGroup(key: string): void {
    setCollapsedKeys((current) => {
      const next = new Set(current);
      if (next.has(key)) next.delete(key);
      else next.add(key);
      return next;
    });
  }

  return (
    <div className="archive-list-panel">
      <div className="archive-list-filters">
        <Input
          aria-label="搜索构件"
          placeholder="搜索构件编号或名称"
          value={keyword}
          suffix={<SearchOutlined className="archive-list-search-icon" />}
          onChange={(event) => setKeyword(event.target.value)}
        />
        <div className="archive-list-filter-row">
          <Select
            aria-label="结构分部筛选"
            value={structurePart}
            onChange={setStructurePart}
            options={STRUCTURE_PART_FILTERS.map((part) => ({
              value: part,
              label: part === "全部" ? "全部结构" : part,
            }))}
          />
          <Select
            aria-label="排序方式"
            value={sort}
            onChange={setSort}
            options={Object.entries(SORT_MODES).map(([value, label]) => ({ value: value as SortMode, label }))}
          />
        </div>
      </div>
      {groups.length === 0 ? (
        <p className="archive-empty-hint">没有符合条件的构件。</p>
      ) : (
        <div ref={listRef} className="archive-component-list">
          {groups.map((group) => {
            const collapsed = collapsedKeys.has(group.key);
            return (
              <section key={group.key} className="archive-component-group">
                <button
                  type="button"
                  className="archive-group-head"
                  aria-expanded={!collapsed}
                  onClick={() => toggleGroup(group.key)}
                >
                  {collapsed ? <RightOutlined /> : <DownOutlined />}
                  <span className="archive-group-name">{group.key}</span>
                  <span className="archive-group-count">{group.items.length}</span>
                  {group.unboundTotal > 0 ? (
                    <span className="archive-group-unbound">{group.unboundTotal} 待整理</span>
                  ) : null}
                  {group.worstScore !== null ? (
                    <span className="archive-group-worst">最低 {roundScoreToTwoDecimals(group.worstScore)}</span>
                  ) : null}
                </button>
                {collapsed ? null : (
                  // 单行 + 自适应分栏：窄栏一列，宽一点就自动两列，把右边的空白用起来。
                  <ul className="archive-group-items">
                    {group.items.map((component) => (
                        <li
                          key={component.id}
                          ref={component.id === selectedComponentId ? selectedItemRef : null}
                        >
                          <button
                            type="button"
                            className={component.id === selectedComponentId
                              ? "archive-component-item active" : "archive-component-item"}
                            onClick={() => onSelect(component.id)}
                          >
                            <strong className="archive-component-code">
                              {component.business_component_code}
                            </strong>
                            {/* 0 条待整理是这屏的常态，逐行印一遍只会淹掉真正有待整理的那几个。 */}
                            {component.unbound_count > 0 ? (
                              <span className="archive-component-unbound">{component.unbound_count}</span>
                            ) : null}
                            <span className={component.latest_score === 100
                              ? "archive-component-score is-full" : "archive-component-score"}>
                              {component.latest_score !== null
                                ? roundScoreToTwoDecimals(component.latest_score) : "—"}
                            </span>
                          </button>
                        </li>
                    ))}
                  </ul>
                )}
              </section>
            );
          })}
        </div>
      )}
    </div>
  );
}
