import { fireEvent, render, screen } from "@testing-library/react";
import { expect, it, vi } from "vitest";

import { DefectReviewToolbar } from "./DefectReviewToolbar";

const summary = {
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

  fireEvent.click(screen.getByRole("checkbox", { name: "全选筛选内可确认项（8）" }));

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
  const checkbox = screen.getByRole("checkbox", { name: "全选筛选内可确认项（8）" }) as HTMLInputElement;
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

  expect(screen.getByRole("checkbox", { name: "全选筛选内可确认项（0）" })).toBeDisabled();
});
