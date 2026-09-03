import { fireEvent, render, screen } from "@testing-library/react";
import { expect, it, vi } from "vitest";

import { DefectReviewToolbar } from "./DefectReviewToolbar";

const summary = {
  parts: [{ name: "板", count: 7 }, { name: "铰缝", count: 5 }],
  defectTypes: [{ name: "渗水泛碱", count: 10 }, { name: "其它病害", count: 2 }],
  all: 12,
  pending: 4,
  batchable: 8,
  confirmed: 0,
  composite: 0,
  candidates: 0,
  unmatched: 0,
};

function renderToolbar({
  selectableCount = 8,
  allSelectableSelected = false,
  someSelectableSelected = false,
  onToggleSelectAll = vi.fn(),
  countsPending = false,
  matchCountsPending = false,
} = {}) {
  render(
    <DefectReviewToolbar
      summary={summary}
      filter="all"
      issueFilter={null}
      search=""
      selectedCount={0}
      selectableCount={selectableCount}
      allSelectableSelected={allSelectableSelected}
      someSelectableSelected={someSelectableSelected}
      viewMode="records"
      issueGroupCount={4}
      countsPending={countsPending}
      matchCountsPending={matchCountsPending}
      partFilter={null}
      onPartFilterChange={vi.fn()}
      defectTypeFilter={null}
      onDefectTypeFilterChange={vi.fn()}
      rematchScopeLabel="全部"
      rematchCount={12}
      onFilterChange={vi.fn()}
      onIssueFilterChange={vi.fn()}
      onSearchChange={vi.fn()}
      onToggleSelectAll={onToggleSelectAll}
      onViewModeChange={vi.fn()}
      onBatchConfirm={vi.fn()}
      onRematch={vi.fn()}
    />,
  );
  return { onToggleSelectAll };
}

it("selects every batch-eligible defect in the current filter", () => {
  const onToggleSelectAll = vi.fn();
  renderToolbar({ onToggleSelectAll });

  fireEvent.click(screen.getByRole("checkbox", { name: "全选可确认项（8）" }));

  expect(onToggleSelectAll).toHaveBeenCalledTimes(1);
});

it("shows partial and complete selection states", () => {
  const { rerender } = render(
    <DefectReviewToolbar
      summary={summary}
      filter="all"
      issueFilter={null}
      search=""
      selectedCount={3}
      selectableCount={8}
      allSelectableSelected={false}
      someSelectableSelected
      viewMode="records"
      issueGroupCount={4}
      partFilter={null}
      onPartFilterChange={vi.fn()}
      defectTypeFilter={null}
      onDefectTypeFilterChange={vi.fn()}
      rematchScopeLabel="全部"
      rematchCount={12}
      onFilterChange={vi.fn()}
      onIssueFilterChange={vi.fn()}
      onSearchChange={vi.fn()}
      onToggleSelectAll={vi.fn()}
      onViewModeChange={vi.fn()}
      onBatchConfirm={vi.fn()}
      onRematch={vi.fn()}
    />,
  );
  const checkbox = screen.getByRole("checkbox", { name: "全选可确认项（8）" }) as HTMLInputElement;
  expect(checkbox.indeterminate).toBe(true);
  expect(checkbox).not.toBeChecked();

  rerender(
    <DefectReviewToolbar
      summary={summary}
      filter="all"
      issueFilter={null}
      search=""
      selectedCount={8}
      selectableCount={8}
      allSelectableSelected
      someSelectableSelected={false}
      viewMode="records"
      issueGroupCount={4}
      partFilter={null}
      onPartFilterChange={vi.fn()}
      defectTypeFilter={null}
      onDefectTypeFilterChange={vi.fn()}
      rematchScopeLabel="全部"
      rematchCount={12}
      onFilterChange={vi.fn()}
      onIssueFilterChange={vi.fn()}
      onSearchChange={vi.fn()}
      onToggleSelectAll={vi.fn()}
      onViewModeChange={vi.fn()}
      onBatchConfirm={vi.fn()}
      onRematch={vi.fn()}
    />,
  );
  expect(checkbox.indeterminate).toBe(false);
  expect(checkbox).toBeChecked();
});

it("disables select all when the current filter has no batch-eligible defects", () => {
  renderToolbar({ selectableCount: 0 });

  expect(screen.getByRole("checkbox", { name: "全选可确认项（0）" })).toBeDisabled();
});

// 评定树规则没到时，"待处理/可批量确认"是算不出来的：规则缺席会给每条病害记上一条
// 问题，而"可批量确认"的判据是一条问题都没有，于是全都落进待处理。显示成 362 与 0
// 会让人以为真是这样——加载期显示的必须是"还不知道"，不是一个假数字。
it("shows a dash instead of a fake zero while the rating tree rules load", () => {
  renderToolbar({ countsPending: true });

  expect(screen.getByRole("option", { name: "待处理（—）" })).toBeInTheDocument();
  expect(screen.getByRole("option", { name: "可批量确认（—）" })).toBeInTheDocument();
  // 已确认与全部不看问题，规则没到也照常算得出来，不该跟着变成"—"。
  expect(screen.getByRole("option", { name: "已确认（0）" })).toBeInTheDocument();
  expect(screen.getByRole("option", { name: "全部状态（12）" })).toBeInTheDocument();
});

// 留着能点的话，点进去是一屏空列表——那和"确实一条都没有"又长得一样，
// 等于换个地方继续误导。
it("disables the counts it cannot compute yet", () => {
  renderToolbar({ countsPending: true });

  expect(screen.getByRole("option", { name: "待处理（—）" })).toBeDisabled();
  expect(screen.getByRole("option", { name: "可批量确认（—）" })).toBeDisabled();
  expect(screen.getByRole("option", { name: "全部状态（12）" })).toBeEnabled();
});

// 匹配结果没回来时没有任何一条会被判成"无匹配"，那个 0 同样是假的。
it("marks the match statistics as unknown until the match results arrive", () => {
  renderToolbar({ matchCountsPending: true });

  expect(screen.getByRole("option", { name: "无匹配结果（—）" })).toBeDisabled();
  expect(screen.getByRole("option", { name: "疑似组合病害（—）" })).toBeDisabled();
  expect(screen.getByRole("option", { name: "有多个候选（—）" })).toBeDisabled();
  // 两路数据互不相干：匹配没到不该把待处理也说成未知。
  expect(screen.getByRole("option", { name: "待处理（4）" })).toBeEnabled();
});

it("shows the real counts once both sides are ready", () => {
  renderToolbar();

  expect(screen.getByRole("option", { name: "待处理（4）" })).toBeEnabled();
  expect(screen.getByRole("option", { name: "可批量确认（8）" })).toBeEnabled();
  expect(screen.getByRole("option", { name: "无匹配结果（0）" })).toBeEnabled();
});

it("places the disease type filter in the free space before search", () => {
  const onDefectTypeFilterChange = vi.fn();
  render(
    <DefectReviewToolbar
      summary={summary}
      filter="all"
      issueFilter={null}
      search=""
      selectedCount={0}
      selectableCount={8}
      allSelectableSelected={false}
      someSelectableSelected={false}
      viewMode="records"
      issueGroupCount={4}
      partFilter={null}
      onPartFilterChange={vi.fn()}
      defectTypeFilter={null}
      onDefectTypeFilterChange={onDefectTypeFilterChange}
      rematchScopeLabel="全部"
      rematchCount={12}
      onFilterChange={vi.fn()}
      onIssueFilterChange={vi.fn()}
      onSearchChange={vi.fn()}
      onToggleSelectAll={vi.fn()}
      onViewModeChange={vi.fn()}
      onBatchConfirm={vi.fn()}
      onRematch={vi.fn()}
    />,
  );

  const typeFilter = screen.getByRole("combobox", { name: "按病害类型筛选" });
  const search = screen.getByRole("textbox", { name: "搜索病害" });
  expect(typeFilter.compareDocumentPosition(search) & Node.DOCUMENT_POSITION_FOLLOWING)
    .toBeTruthy();
  expect(screen.getByRole("option", { name: "其它病害（2）" })).toBeInTheDocument();
  fireEvent.change(typeFilter, { target: { value: "其它病害" } });
  expect(onDefectTypeFilterChange).toHaveBeenCalledWith("其它病害");
});
